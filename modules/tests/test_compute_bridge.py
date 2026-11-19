"""Cross-domain verification tests for `modules/common/compute_bridge.py`."""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from fractions import Fraction
from pathlib import Path

from modules.common.arena_codec import (
    SOUND_FLOW_AUX_OFFSET,
    SOUND_MAX_NODES,
    ArenaNodeSpec,
    SoundVerifyError,
    decode_reasons,
    encode_arena,
    load_schema_hash,
    read_flow_aux_words,
    verify_arena_bytes,
)
from modules.common.compute_bridge import (
    COMPUTE_AUX_MAGIC,
    compile_compute_to_fir1,
    emit_lean4_certificate,
    eval_fir1_bytes_u64,
    evaluate_bridged_arena,
    evaluate_compute_spec,
    pack_compute_flow_aux,
    round_half_up,
    unpack_compute_flow_aux,
)

REPO_ROOT = Path(__file__).resolve().parents[2]
MODULES_ROOT = Path(__file__).resolve().parents[1]
SOUND_BIN = REPO_ROOT / "build" / "sound"
FLOW_ROOT = REPO_ROOT.parent / "FLOW"


class ComputeBridgeTestSuite(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.schema_hash = load_schema_hash(REPO_ROOT)

    def test_01_domain_free_purity(self) -> None:
        forbidden_re = re.compile(
            r"\b(?:accounting|tax|taxation|ifrs|ias|vat|depreciation|rpg|damage|kaggle)\b"
        )
        engine_files = (
            MODULES_ROOT / "common" / "compute_bridge.py",
            MODULES_ROOT / "common" / "compute_bridge_contract.json",
        )
        for fpath in engine_files:
            text = fpath.read_text(encoding="utf-8", errors="replace").lower()
            m = forbidden_re.search(text)
            self.assertIsNone(
                m,
                msg=f"Domain term {m.group(0)!r} leaked into {fpath}" if m else "",
            )

        core_files = (
            REPO_ROOT / "Sound.lean",
            REPO_ROOT / "Sound" / "Bilattice.lean",
            REPO_ROOT / "sound.h",
            REPO_ROOT / "sound.c",
            REPO_ROOT / "sound_lean_jsi.c",
            REPO_ROOT / "Makefile",
        )
        existing = [f for f in core_files if f.is_file()]
        self.assertTrue(existing, msg="Expected core C sources (sound.h / sound.c) to exist")
        for fpath in existing:
            text = fpath.read_text(encoding="utf-8", errors="replace").lower()
            self.assertNotIn("compute_bridge", text, msg=f"Bridge leaked into {fpath}")

    def test_02_bidirectional_sound_gating_quantitative_case(self) -> None:
        # Stage 1 rule branch A (capitalize asset & prorate 9/12 depreciation) vs branch B (expense outright)
        def _build_nodes(capitalize_active: bool) -> list[ArenaNodeSpec]:
            return [
                ArenaNodeSpec(label="std_ias16_cap", sort="normative", evid="enacted_norm", clauses=[0]),
                ArenaNodeSpec(label="std_ias16_exp", sort="normative", evid="enacted_norm", clauses=[0]),
                ArenaNodeSpec(
                    label="fact_major_upgrade",
                    sort="factual",
                    evid="direct",
                    clauses=[0] if capitalize_active else [],
                ),
                ArenaNodeSpec(
                    label="fact_routine_maint",
                    sort="factual",
                    evid="direct",
                    clauses=[] if capitalize_active else [0],
                ),
                ArenaNodeSpec(
                    label="eff_capitalize",
                    sort="normative",
                    evid="direct",
                    constitutive=True,
                    presup_mask=1 << 2,
                    attack_mask=1 << 3,
                    clauses=[1 << 0],
                ),
                ArenaNodeSpec(
                    label="eff_expense",
                    sort="normative",
                    evid="direct",
                    constitutive=True,
                    presup_mask=1 << 3,
                    attack_mask=1 << 2,
                    clauses=[1 << 1],
                ),
                ArenaNodeSpec(label="fact_opt_match", sort="factual", evid="assumed", clauses=[0]),
                ArenaNodeSpec(
                    label="opt_claim",
                    sort="normative",
                    evid="enacted_norm",
                    presup_mask=(1 << 4) | (1 << 6),
                    clauses=[1 << 4],
                ),
            ]

        compute_spec = {
            "aux_scale": 2,
            "vars": {
                "cost": 1200000,
                "salvage": 200000,
                "life_years": 5,
                "months": 9,
                "routine_outlay": 50000,
                "opt_b_amt": 150000,
            },
            "exprs": {
                "annual_depr": ["div", ["sub", "cost", "salvage"], "life_years"],
                "period_depr": ["round_half_up", ["mul", "annual_depr", ["div", "months", 12]], 0],
                "routine_exp": "routine_outlay",
            },
            "guards": {
                "annual_depr": "eff_capitalize",
                "period_depr": "eff_capitalize",
                "routine_exp": "eff_expense",
            },
            "predicates": {
                "fact_opt_match": ["eq", "period_depr", "opt_b_amt"],
            },
        }

        # Case A: eff_capitalize established -> period_depr == 150000, routine_exp gated to 0
        res_ffi = evaluate_bridged_arena(
            _build_nodes(True), compute_spec, SOUND_BIN, schema_hash=self.schema_hash, backend="ffi"
        )
        res_cli = evaluate_bridged_arena(
            _build_nodes(True), compute_spec, SOUND_BIN, schema_hash=self.schema_hash, backend="cli"
        )
        self.assertEqual(res_ffi.eval_result.established_mask, res_cli.eval_result.established_mask)
        self.assertEqual(res_ffi.compute.values["period_depr"].status, "ok")
        self.assertEqual(res_ffi.compute.values["period_depr"].value, Fraction(150000, 1))
        self.assertEqual(res_ffi.compute.values["routine_exp"].status, "gated_inactive")
        self.assertEqual(res_ffi.compute.values["routine_exp"].value, Fraction(0, 1))
        self.assertEqual(res_ffi.eval_result.nodes[6].status, "established")
        self.assertEqual(res_ffi.eval_result.nodes[7].status, "established")

        # Case B: eff_capitalize defeated -> period_depr gated_inactive (0), claim blocked
        res_def = evaluate_bridged_arena(
            _build_nodes(False), compute_spec, SOUND_BIN, schema_hash=self.schema_hash, backend="ffi"
        )
        self.assertEqual(res_def.compute.values["period_depr"].status, "gated_inactive")
        self.assertEqual(res_def.compute.values["period_depr"].value, Fraction(0, 1))
        self.assertEqual(res_def.compute.values["routine_exp"].status, "ok")
        self.assertEqual(res_def.compute.values["routine_exp"].value, Fraction(50000, 1))
        self.assertEqual(res_def.eval_result.nodes[6].status, "defeated")
        self.assertEqual(res_def.eval_result.nodes[7].status, "defeated")

    def test_03_pro_rata_conservation_recurrence_and_solve_linear(self) -> None:
        spec = {
            "vars": {
                "total": 1000000,
                "weights": [300, 500, 400],
                "principal": 100000,
                "rate": "0.05",
                "tax_rate": "0.05",
                "fixed_fee": 1000,
                "target_total": 1500,
            },
            "exprs": {
                "alloc": ["pro_rata", "total", "weights", 0, "half_up"],
                "alloc_sum": ["sum", "alloc"],
                "bond_3yr": [
                    "recur_fold",
                    "principal",
                    3,
                    ["mul", "s", ["add", 1, "rate"]],
                    "s",
                    "k",
                ],
                "pretax_base": ["solve_linear", "tax_rate", "fixed_fee", "target_total"],
            },
            "predicates": {
                "fact_alloc_conserved": ["sum_eq", "alloc", ["vec", "total"]],
                "fact_base_exact": ["eq", "pretax_base", 10000],
            },
        }
        rep = evaluate_compute_spec(spec)
        self.assertEqual(rep.values["alloc"].vector, [Fraction(250000), Fraction(416667), Fraction(333333)])
        self.assertEqual(rep.values["alloc_sum"].value, Fraction(1000000))
        self.assertTrue(rep.predicates["fact_alloc_conserved"].truth)
        # 100000 * (1.05)^3 = 100000 * 9261 / 8000 = 115762.5
        self.assertEqual(rep.values["bond_3yr"].value, Fraction(231525, 2))
        self.assertEqual(rep.values["pretax_base"].value, Fraction(10000))
        self.assertTrue(rep.predicates["fact_base_exact"].truth)

    def test_04_penumbra_on_missing_var_or_div_zero(self) -> None:
        nodes = [
            ArenaNodeSpec(label="fact_missing_ok", sort="factual", evid="direct", clauses=[0]),
            ArenaNodeSpec(label="fact_div_ok", sort="factual", evid="direct", clauses=[0]),
        ]
        spec = {
            "vars": {"a": 100, "zero_den": 0},
            "exprs": {
                "from_missing": ["add", "a", "unknown_qty"],
                "from_zero_div": ["div", "a", "zero_den"],
            },
            "predicates": {
                "fact_missing_ok": ["gt", "from_missing", 0],
                "fact_div_ok": ["eq", "from_zero_div", 0],
            },
        }
        bridged = evaluate_bridged_arena(nodes, spec, SOUND_BIN, schema_hash=self.schema_hash)
        self.assertEqual(bridged.compute.values["from_missing"].status, "undetermined")
        self.assertEqual(bridged.compute.values["from_zero_div"].status, "undetermined")
        self.assertEqual(bridged.eval_result.nodes[0].status, "penumbraOpen")
        self.assertIn("PENUMBRA", decode_reasons(bridged.eval_result.nodes[0].reasons))
        self.assertEqual(bridged.eval_result.nodes[1].status, "penumbraOpen")

    def test_05_cross_domain_reusability(self) -> None:
        spec = {
            "vars": {
                "mass": 4,
                "velocity": 15,
                "buf_req": 4096,
                "buf_cap": 2048,
                "stride": 64,
            },
            "exprs": {
                "kinetic": ["mul", "1/2", "mass", ["pow_int", "velocity", 2]],
                "clamped_len": ["max", "stride", ["min", "buf_req", "buf_cap"]],
            },
            "predicates": {
                "kinetic_ok": ["in_range", "kinetic", 400, 500],
                "len_ok": ["eq", "clamped_len", 2048],
            },
        }
        rep = evaluate_compute_spec(spec)
        self.assertEqual(rep.values["kinetic"].value, Fraction(450))
        self.assertTrue(rep.predicates["kinetic_ok"].truth)
        self.assertEqual(rep.values["clamped_len"].value, Fraction(2048))
        self.assertTrue(rep.predicates["len_ok"].truth)

    def test_06_flow_aux_roundtrip_and_merkle_tamper(self) -> None:
        nodes = [ArenaNodeSpec(label="fact_check", sort="factual", evid="direct", clauses=[0])]
        spec = {
            "aux_scale": 4,
            "vars": {"x": "123.45", "y": "-3.45"},
            "exprs": {"net": ["add", "x", "y"], "frac": ["div", 1, 8]},
            "predicates": {"fact_check": ["eq", "net", 120]},
        }
        bridged = evaluate_bridged_arena(nodes, spec, SOUND_BIN, schema_hash=self.schema_hash)
        words = read_flow_aux_words(bridged.arena_bytes)
        decoded = unpack_compute_flow_aux(words)
        self.assertEqual(decoded["magic"], COMPUTE_AUX_MAGIC)
        self.assertEqual(decoded["aux_scale"], 4)
        self.assertEqual(decoded["num_entries"], 2)
        self.assertEqual(decoded["pred_true_mask"], 1)
        self.assertEqual(decoded["entries"][0]["scaled_i64"], 1200000)
        self.assertEqual(decoded["entries"][1]["scaled_i64"], 1250)  # round(0.125 * 10^4) == 1250

        # Flipping 1 bit inside flow_aux must break SoundArena Merkle integrity (SOUND_ERR_MAC)
        tampered = bytearray(bridged.arena_bytes)
        tampered[SOUND_FLOW_AUX_OFFSET + 8] ^= 0x01
        with self.assertRaises(SoundVerifyError) as ctx:
            verify_arena_bytes(bytes(tampered), SOUND_BIN, backend="ffi")
        self.assertIn("SOUND_ERR_MAC", str(ctx.exception))

    def test_07_fir1_lowering_and_c17_flow_ir_parity(self) -> None:
        spec = {
            "vars": {"x": 120, "y": -35, "lim": 100},
            "exprs": {
                "abs_y": ["abs", "y"],
                "bounded": ["min", "lim", ["max", 0, ["add", "x", "y"]]],
                "scaled": ["if_else", ["gt", "x", "lim"], ["mul", "bounded", 3], "bounded"],
            },
            "predicates": {
                "p_ge": ["ge", "scaled", 255],
                "p_lt": ["lt", "y", 0],
                "p_eq": ["eq", "abs_y", 35],
            },
        }
        rep = evaluate_compute_spec(spec)
        fir_bytes, in_names, defaults = compile_compute_to_fir1(spec, prog_name="cb_int_test")
        self.assertTrue(fir_bytes.startswith(b"FIR1"))
        self.assertEqual(in_names, ["x", "y", "lim"])
        self.assertEqual(defaults, [120, -35, 100])

        py_outs = eval_fir1_bytes_u64(fir_bytes, [120, (-35) & 0xFFFFFFFFFFFFFFFF, 100])
        self.assertEqual(py_outs, [35, 85, 255, 1, 1, 1])
        self.assertEqual(Fraction(py_outs[2]), rep.values["scaled"].value)

        flow_root = FLOW_ROOT if (FLOW_ROOT / "sdk" / "flow_ir.h").is_file() else (REPO_ROOT / "vendor_flow")
        flow_ir_h = flow_root / "sdk" / "flow_ir.h"
        cc_bin = shutil.which("clang") or shutil.which("cc")
        if flow_ir_h.is_file() and cc_bin:
            with tempfile.TemporaryDirectory(prefix="sound_cb_fir_") as tmp:
                tmp_dir = Path(tmp)
                fir_file = tmp_dir / "test.fir"
                c_file = tmp_dir / "harness.c"
                bin_file = tmp_dir / "harness"
                fir_file.write_bytes(fir_bytes)
                c_file.write_text(
                    '#include <stdio.h>\n'
                    '#include <stdint.h>\n'
                    '#include <stdlib.h>\n'
                    '#include "flow_ir.h"\n'
                    'int main(int argc, char **argv) {\n'
                    '    if (argc < 2) return 2;\n'
                    '    FILE *f = fopen(argv[1], "rb");\n'
                    '    if (!f) return 3;\n'
                    '    uint8_t buf[FLOW_IR_MAX_FILE_BYTES];\n'
                    '    size_t n = fread(buf, 1, sizeof(buf), f);\n'
                    '    fclose(f);\n'
                    '    flow_ir_prog_t p;\n'
                    '    if (!flow_ir_load_bytes(&p, buf, n)) return 4;\n'
                    '    if (!flow_ir_validate(&p)) return 5;\n'
                    '    if (!flow_ir_admits_unitary(&p)) return 6;\n'
                    '    uint64_t ins[3] = {UINT64_C(120), (uint64_t)INT64_C(-35), UINT64_C(100)};\n'
                    '    uint64_t outs[8] = {0};\n'
                    '    if (!flow_ir_eval_u64(&p, ins, outs)) return 7;\n'
                    '    for (uint32_t i = 0; i < p.num_outputs; ++i) {\n'
                    '        printf("%llu%s", (unsigned long long)outs[i], (i + 1 < p.num_outputs) ? "," : "\\n");\n'
                    '    }\n'
                    '    return 0;\n'
                    '}\n',
                    encoding="utf-8",
                )
                cc = subprocess.run(
                    [
                        cc_bin,
                        "-std=c17",
                        "-O2",
                        f"-I{flow_root / 'core'}",
                        f"-I{flow_root / 'sdk'}",
                        str(c_file),
                        "-o",
                        str(bin_file),
                    ],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(cc.returncode, 0, msg=cc.stderr)
                run = subprocess.run([str(bin_file), str(fir_file)], capture_output=True, text=True, check=False)
                self.assertEqual(run.returncode, 0, msg=run.stderr)
                c_outs = [int(tok) for tok in run.stdout.strip().split(",")]
                self.assertEqual(c_outs, py_outs)

    def test_08_lean4_kernel_certificate_and_cli(self) -> None:
        spec = {
            "vars": {"base": 1200000, "res": 200000, "years": 5, "m": 9, "expected": 150000},
            "exprs": {
                "annual": ["div", ["sub", "base", "res"], "years"],
                "part": ["round_half_up", ["mul", "annual", ["div", "m", 12]], 0],
                "neg_half": ["round_half_up", "-0.5", 0],
            },
            "predicates": {
                "exact_match": ["eq", "part", "expected"],
                "half_rule_ok": ["eq", "neg_half", -1],
            },
        }
        rep = evaluate_compute_spec(spec)
        self.assertEqual(rep.values["part"].value, Fraction(150000))
        self.assertEqual(rep.values["neg_half"].value, Fraction(-1))
        lean_src = emit_lean4_certificate(spec, rep)
        self.assertNotIn("import ", lean_src)
        self.assertIn("structure Q", lean_src)
        self.assertIn("decide", lean_src)

        env = dict(os.environ)
        env["PATH"] = f"{Path.home() / '.elan' / 'bin'}:{env.get('PATH', '')}"
        lean_bin = shutil.which("lean", path=env["PATH"])
        if lean_bin:
            with tempfile.TemporaryDirectory(prefix="sound_cb_lean_") as tmp:
                lean_file = Path(tmp) / "Cert.lean"
                lean_file.write_text(lean_src, encoding="utf-8")
                proc = subprocess.run(
                    [lean_bin, "-DwarningAsError=true", str(lean_file)],
                    env=env,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

        cli_spec = {
            "vars": {"a": 100, "b": 25},
            "exprs": {"sum_ab": ["add", "a", "b"]},
            "predicates": {"ok": ["eq", "sum_ab", 125]},
        }
        cli_proc = subprocess.run(
            [sys.executable, "-m", "modules.common.cli", "compute", "-"],
            cwd=str(REPO_ROOT),
            input=json.dumps(cli_spec),
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(cli_proc.returncode, 0, msg=cli_proc.stderr)
        cli_out = json.loads(cli_proc.stdout)
        self.assertEqual(cli_out["values"]["sum_ab"]["value"], "125")
        self.assertTrue(cli_out["predicates"]["ok"]["truth"])
        self.assertGreater(cli_out["aux_words_used"], 0)
