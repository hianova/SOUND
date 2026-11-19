#!/usr/bin/env python3
"""
sound_sabotage_check.py — Single-Fault C17 Sabotage Mutation Gate for SOUND v2.1.0
===================================================================================
Empirically proves regression-suite completeness across all SOUND Pure-GPR layers by
injecting 18 stratified single-fault mutations into `sound.h`:
  1. `bilattice_hume_evid`  (3 mutants: Hume IS->OUGHT, Hume OUGHT->IS, evidential leaf)
  2. `hohfeld_kanlock`      (3 mutants: KanLock immunity, power gate, dual-duty coherence)
  3. `prepared_isolation`   (3 mutants: policy-slot isolation, cross-plane sort, priv-esc)
  4. `siphash_atomic_batch` (3 mutants: SipHash downgrade guard, span hash, evid ceiling)
  5. `weighted_searu`       (3 mutants: lex specialis weight drop, KanLock pin drop, energy)
  6. `chart_glue_fir_lift`  (3 mutants: conflict export, policy-slot import, policy FIR)

Every mutant must:
  1. Match uniquely (`count == 1`) in `sound.h` (0 silent no-op mutants).
  2. Compile cleanly with strict `-std=c17 -mgeneral-regs-only -Werror` (`compile_rc == 0`).
  3. Be caught dynamically at runtime by `sound selftest` (`run_rc != 0`, 100.0% kill rate).
"""

from __future__ import annotations

import concurrent.futures
import os
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class SoundMutant:
    name: str
    category: str
    target: str
    replacement: str


def build_sound_mutants() -> list[SoundMutant]:
    return [
        # --- Category 1: 4-Valued Bilattice, Bidirectional Hume & Evidential Floor (3 mutants) ---
        SoundMutant(
            "BILATTICE_HUME_IS_TO_OUGHT_BYPASS",
            "bilattice_hume_evid",
            "    if ((clause_mask & (node_mask & (~factual_mask))) != UINT64_C(0)) {\n"
            "        return true;\n"
            "    }\n"
            "    if (diag_reason_out != NULL) {\n"
            "        *diag_reason_out |= (uint32_t)SOUND_REASON_HUME_IS_TO_OUGHT;\n"
            "    }\n"
            "    return false;",
            "    if ((clause_mask & (node_mask & (~factual_mask))) != UINT64_C(0)) {\n"
            "        return true;\n"
            "    }\n"
            "    if (diag_reason_out != NULL) {\n"
            "        *diag_reason_out |= (uint32_t)SOUND_REASON_HUME_IS_TO_OUGHT;\n"
            "    }\n"
            "    return true;",
        ),
        SoundMutant(
            "BILATTICE_HUME_OUGHT_TO_IS_BYPASS",
            "bilattice_hume_evid",
            "    if (nd->sort == (uint8_t)SOUND_SORT_FACTUAL) {\n"
            "        if ((clause_mask & (~factual_mask)) != UINT64_C(0)) {\n"
            "            if (diag_reason_out != NULL) {\n"
            "                *diag_reason_out |= (uint32_t)SOUND_REASON_HUME_OUGHT_TO_IS;\n"
            "            }\n"
            "            return false;\n"
            "        }\n"
            "        return true;\n"
            "    }",
            "    if (nd->sort == (uint8_t)SOUND_SORT_FACTUAL) {\n"
            "        if ((clause_mask & (~factual_mask)) != UINT64_C(0)) {\n"
            "            if (diag_reason_out != NULL) {\n"
            "                *diag_reason_out |= (uint32_t)SOUND_REASON_HUME_OUGHT_TO_IS;\n"
            "            }\n"
            "            return true;\n"
            "        }\n"
            "        return true;\n"
            "    }",
        ),
        SoundMutant(
            "BILATTICE_EVIDENTIAL_LEAF_BYPASS",
            "bilattice_hume_evid",
            "    if (nd->evid >= (uint8_t)SOUND_EVID_INFERENTIAL && clause_mask == UINT64_C(0)) {\n"
            "        if (diag_reason_out != NULL) {\n"
            "            *diag_reason_out |= (uint32_t)SOUND_REASON_EVIDENTIAL_LEAF;\n"
            "        }\n"
            "        return false;\n"
            "    }",
            "    if (nd->evid >= (uint8_t)SOUND_EVID_INFERENTIAL && clause_mask == UINT64_C(0)) {\n"
            "        if (diag_reason_out != NULL) {\n"
            "            *diag_reason_out |= (uint32_t)SOUND_REASON_EVIDENTIAL_LEAF;\n"
            "        }\n"
            "        return true;\n"
            "    }",
        ),
        # --- Category 2: Hohfeld Conservation & KanLock Immunity (3 mutants) ---
        SoundMutant(
            "HOHFELD_KANLOCK_IMMUNITY_BYPASS",
            "hohfeld_kanlock",
            "    bool commit = (!imm_hit) & pow_ok & coh_ok;",
            "    bool commit = pow_ok & coh_ok;",
        ),
        SoundMutant(
            "HOHFELD_NO_POWER_BYPASS",
            "hohfeld_kanlock",
            "    bool pow_ok = (arena->map.norm.power_mask & bit) != UINT64_C(0);",
            "    bool pow_ok = true;",
        ),
        SoundMutant(
            "HOHFELD_COHERENCE_CLASH_ALLOW",
            "hohfeld_kanlock",
            "    bool coh_ok = (dp & dn) == UINT64_C(0);",
            "    bool coh_ok = true;",
        ),
        # --- Category 3: Prepared-Statement Policy vs Bind Isolation (3 mutants) ---
        SoundMutant(
            "PREPARED_POLICY_SLOT_BYPASS",
            "prepared_isolation",
            "        if ((ctrl->policy_mask & tbit) != UINT64_C(0)) {\n"
            "            inject_reasons |= (uint32_t)SOUND_INJECT_POLICY_SLOT;\n"
            "        }",
            "        if (false && (ctrl->policy_mask & tbit) != UINT64_C(0)) {\n"
            "            inject_reasons |= (uint32_t)SOUND_INJECT_POLICY_SLOT;\n"
            "        }",
        ),
        SoundMutant(
            "PREPARED_CROSS_PLANE_SORT_BYPASS",
            "prepared_isolation",
            "        if (nd->sort != (uint8_t)SOUND_SORT_FACTUAL || c->sort != (uint8_t)SOUND_SORT_FACTUAL) {\n"
            "            inject_reasons |= (uint32_t)SOUND_INJECT_CROSS_PLANE_SORT;\n"
            "        }",
            "        if (false && nd->sort != (uint8_t)SOUND_SORT_FACTUAL &&\n"
            "            c->sort != (uint8_t)SOUND_SORT_FACTUAL) {\n"
            "            inject_reasons |= (uint32_t)SOUND_INJECT_CROSS_PLANE_SORT;\n"
            "        }",
        ),
        SoundMutant(
            "PREPARED_PRIV_ESCALATION_BYPASS",
            "prepared_isolation",
            "        if (c->node_flags != UINT32_C(0) || c->evid == (uint8_t)SOUND_EVID_ENACTED_NORM ||\n"
            "            c->force == (uint8_t)SOUND_FORCE_DECLARATIVE ||\n"
            "            (nd->flags & SOUND_NODE_FLAG_CONSTITUTIVE) != UINT32_C(0)) {\n"
            "            inject_reasons |= (uint32_t)SOUND_INJECT_PRIV_ESCALATION;\n"
            "        }",
            "        if (false && c->node_flags != UINT32_C(0) && c->evid == (uint8_t)SOUND_EVID_ENACTED_NORM &&\n"
            "            c->force == (uint8_t)SOUND_FORCE_DECLARATIVE &&\n"
            "            (nd->flags & SOUND_NODE_FLAG_CONSTITUTIVE) != UINT32_C(0)) {\n"
            "            inject_reasons |= (uint32_t)SOUND_INJECT_PRIV_ESCALATION;\n"
            "        }",
        ),
        # --- Category 4: SipHash-2-4 Keyed Sealing, Span Hash & Atomic Batch (3 mutants) ---
        SoundMutant(
            "SIPHASH_FINALIZATION_XFF_CORRUPT",
            "siphash_atomic_batch",
            "    c->v2 ^= UINT64_C(0xFF);",
            "    c->v2 ^= UINT64_C(0xFE);",
        ),
        SoundMutant(
            "UTTERANCE_SPAN_HASH_BYPASS",
            "siphash_atomic_batch",
            "        if (actual_hash != c->payload_hash) {\n"
            "            *fiber_out = SOUND_FIBER_FALLBACK_SPAN;\n"
            "            *inject_reasons_out = (uint32_t)SOUND_INJECT_SPAN_HASH_MISMATCH;\n"
            "            return false;\n"
            "        }",
            "        if (actual_hash != c->payload_hash && false) {\n"
            "            *fiber_out = SOUND_FIBER_FALLBACK_SPAN;\n"
            "            *inject_reasons_out = (uint32_t)SOUND_INJECT_SPAN_HASH_MISMATCH;\n"
            "            return false;\n"
            "        }",
        ),
        SoundMutant(
            "BATCH_EVID_CEILING_BYPASS",
            "siphash_atomic_batch",
            "        if ((ctrl->ctrl_flags & SOUND_FOREST_CTRL_PROPOSER_LOW_EVID) != UINT32_C(0) &&\n"
            "            c->evid < (uint8_t)SOUND_EVID_REPORTATIVE) {\n"
            "            inject_reasons |= (uint32_t)SOUND_INJECT_EVID_CEILING;\n"
            "        }",
            "        if ((ctrl->ctrl_flags & SOUND_FOREST_CTRL_PROPOSER_LOW_EVID) != UINT32_C(0) &&\n"
            "            c->evid < (uint8_t)SOUND_EVID_REPORTATIVE && false) {\n"
            "            inject_reasons |= (uint32_t)SOUND_INJECT_EVID_CEILING;\n"
            "        }",
        ),
        # --- Category 5: Normative Hierarchy (Lex Specialis) Weighted SEARU (3 mutants) ---
        SoundMutant(
            "WEIGHTED_SEARU_IGNORE_PRIORITY",
            "weighted_searu",
            "    uint8_t lw[SOUND_MAX_NODES];\n"
            "    for (uint32_t i = 0U; i < SOUND_MAX_NODES; i++) {\n"
            "        lw[i] = (priority_weights[i] > UINT8_C(0)) ? priority_weights[i] : UINT8_C(1);\n"
            "    }",
            "    uint8_t lw[SOUND_MAX_NODES];\n"
            "    for (uint32_t i = 0U; i < SOUND_MAX_NODES; i++) {\n"
            "        lw[i] = (priority_weights[i] == UINT8_C(255)) ? UINT8_C(1) : UINT8_C(1);\n"
            "    }",
        ),
        SoundMutant(
            "WEIGHTED_SEARU_KANLOCK_PIN_DROP",
            "weighted_searu",
            "    uint8_t lw[SOUND_MAX_NODES];\n"
            "    for (uint32_t i = 0U; i < SOUND_MAX_NODES; i++) {\n"
            "        lw[i] = (priority_weights[i] > UINT8_C(0)) ? priority_weights[i] : UINT8_C(1);\n"
            "    }\n\n"
            "    uint64_t init_canvas[4] = {est | (conf & imm), UINT64_C(0), UINT64_C(0), UINT64_C(0)};\n"
            "    uint64_t kan_pin[4] = {pinned, UINT64_MAX, UINT64_MAX, UINT64_MAX};",
            "    uint8_t lw[SOUND_MAX_NODES];\n"
            "    for (uint32_t i = 0U; i < SOUND_MAX_NODES; i++) {\n"
            "        lw[i] = (priority_weights[i] > UINT8_C(0)) ? priority_weights[i] : UINT8_C(1);\n"
            "    }\n\n"
            "    uint64_t init_canvas[4] = {est | (conf & imm), UINT64_C(0), UINT64_C(0), UINT64_C(0)};\n"
            "    uint64_t kan_pin[4] = {pinned & (~imm), UINT64_MAX, UINT64_MAX, UINT64_MAX};",
        ),
        SoundMutant(
            "WEIGHTED_SEARU_CONFLICT_ENERGY_DROP",
            "weighted_searu",
            "                if (hit != UINT64_C(0)) {\n"
            "                    energy += UINT64_C(65536);\n"
            "                    frust[0] |= ub | hit;\n"
            "                }\n"
            "                if (!sound_node_fires(arena, u, s, s, nm, factual_mask)) {\n"
            "                    energy += UINT64_C(32768);\n"
            "                    frust[0] |= ub;\n"
            "                }",
            "                if (hit != UINT64_C(0)) {\n"
            "                    frust[0] |= ub | hit;\n"
            "                }\n"
            "                if (!sound_node_fires(arena, u, s, s, nm, factual_mask)) {\n"
            "                    frust[0] |= ub;\n"
            "                }",
        ),
        # --- Category 6: Cross-64-Node Chart Gluing & Policy-Eval FIR1 Lifter (3 mutants) ---
        SoundMutant(
            "CHART_GLUE_ALLOW_CONFLICT_EXPORT",
            "chart_glue_fir_lift",
            "    if ((export_mask & src->map.eval_out.conflict_mask) != UINT64_C(0) ||\n"
            "        (export_mask & (~src->map.eval_out.established_mask)) != UINT64_C(0)) {\n"
            "        return SOUND_FIBER_HOLD_INCOHERENT;\n"
            "    }\n"
            "    sound_zero_bytes(port_out, sizeof(*port_out));",
            "    if (((export_mask & src->map.eval_out.conflict_mask) != UINT64_C(0) ||\n"
            "         (export_mask & (~src->map.eval_out.established_mask)) != UINT64_C(0)) &&\n"
            "        false) {\n"
            "        return SOUND_FIBER_HOLD_INCOHERENT;\n"
            "    }\n"
            "    sound_zero_bytes(port_out, sizeof(*port_out));",
        ),
        SoundMutant(
            "CHART_GLUE_ALLOW_POLICY_SLOT_IMPORT",
            "chart_glue_fir_lift",
            "        if (inj != (uint32_t)SOUND_INJECT_NONE) {\n"
            "            ctrl->last_reject_reason = inj;\n"
            "            return sound_record_fiber(dst, SOUND_FIBER_FALLBACK_POLICY_VIOLATION);\n"
            "        }\n"
            "        dst_import_mask |= dbit;",
            "        if (inj != (uint32_t)SOUND_INJECT_NONE && false) {\n"
            "            ctrl->last_reject_reason = inj;\n"
            "            return sound_record_fiber(dst, SOUND_FIBER_FALLBACK_POLICY_VIOLATION);\n"
            "        }\n"
            "        dst_import_mask |= dbit;",
        ),
        SoundMutant(
            "POLICY_FIR_LIFT_DROP_POSTLOOP",
            "chart_glue_fir_lift",
            "        if (!b.overflow) {\n"
            "            p_star_id = sound__pbld_gamma(&b, arena, n, node_mask, factual_mask, bind_mask,\n"
            "                                          root_mask, plain_bind_mask, der_order, n_der, zero_id,\n"
            "                                          UINT16_C(0), false, cur_i_id);\n"
            "            i_star_id = sound__pbld_gamma(&b, arena, n, node_mask, factual_mask, bind_mask,\n"
            "                                          root_mask, plain_bind_mask, der_order, n_der, zero_id,\n"
            "                                          UINT16_C(0), false, p_star_id);\n"
            "        }",
            "        if (!b.overflow) {\n"
            "            p_star_id = zero_id;\n"
            "            i_star_id = cur_i_id;\n"
            "        }",
        ),
    ]


def _run_one_mutant(
    mutant: SoundMutant,
    sound_h_src: str,
    sound_c_path: Path,
    repo_root: Path,
    flow_core: Path,
    flow_sdk: Path,
    cc: str,
) -> tuple[SoundMutant, int, int, int, str]:
    count = sound_h_src.count(mutant.target)
    if count != 1:
        return (mutant, count, -1, -1, f"target match count={count} (expected 1)")

    mutated_h = sound_h_src.replace(mutant.target, mutant.replacement, 1)
    with tempfile.TemporaryDirectory(prefix=f"snd_mut_{mutant.name}_") as td:
        tmp_dir = Path(td)
        (tmp_dir / "sound.h").write_text(mutated_h, encoding="utf-8")
        c_copy = tmp_dir / "sound.c"
        shutil.copy2(sound_c_path, c_copy)
        exe_path = tmp_dir / "sound"

        cmd_cc = [
            cc,
            "-std=c17",
            "-mgeneral-regs-only",
            "-fsanitize=signed-integer-overflow,shift,bounds,null,return,unreachable",
            "-fsanitize-trap=all",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Wundef",
            "-Wmissing-prototypes",
            "-Werror",
            "-O2",
            '-DSOUND_VERSION_STRING="sabotage"',
            "-I",
            str(tmp_dir),
            "-I",
            str(repo_root / "build"),
            "-isystem",
            str(flow_core),
            "-isystem",
            str(flow_sdk),
            str(c_copy),
            "-o",
            str(exe_path),
        ]
        comp = subprocess.run(cmd_cc, capture_output=True, text=True, check=False)
        if comp.returncode != 0:
            return (
                mutant,
                count,
                comp.returncode,
                -1,
                f"compile failed:\n{comp.stderr.strip()}",
            )

        run = subprocess.run([str(exe_path), "selftest"], capture_output=True, text=True, check=False)
        detail = (run.stdout or run.stderr).strip()
        return (mutant, count, comp.returncode, run.returncode, detail)


def main() -> int:
    repo_root = Path(__file__).resolve().parents[1]
    sound_h_path = repo_root / "sound.h"
    sound_c_path = repo_root / "sound.c"
    schema_hdr = repo_root / "build" / "sound_schema_abi.h"
    flow_dir = Path(os.environ.get("FLOW_DIR", str(repo_root.parent / "FLOW"))).resolve()
    flow_core = flow_dir / "core"
    flow_sdk = flow_dir / "sdk"
    cc = os.environ.get("CC", "clang")

    if not schema_hdr.is_file():
        print(f"ERROR: missing {schema_hdr}; run `make all` first.", file=sys.stderr)
        return 1

    sound_h_src = sound_h_path.read_text(encoding="utf-8")
    mutants = build_sound_mutants()

    # Baseline sanity check: unmutated sound.h + sound.c must compile and PASS selftest
    t0 = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="snd_mut_baseline_") as td:
        tmp_dir = Path(td)
        (tmp_dir / "sound.h").write_text(sound_h_src, encoding="utf-8")
        c_copy = tmp_dir / "sound.c"
        shutil.copy2(sound_c_path, c_copy)
        exe_path = tmp_dir / "sound"
        base_cc = subprocess.run(
            [
                cc,
                "-std=c17",
                "-mgeneral-regs-only",
                "-fsanitize=signed-integer-overflow,shift,bounds,null,return,unreachable",
                "-fsanitize-trap=all",
                "-Wall",
                "-Wextra",
                "-Wpedantic",
                "-Wundef",
                "-Wmissing-prototypes",
                "-Werror",
                "-O2",
                '-DSOUND_VERSION_STRING="sabotage"',
                "-I",
                str(tmp_dir),
                "-I",
                str(repo_root / "build"),
                "-isystem",
                str(flow_core),
                "-isystem",
                str(flow_sdk),
                str(c_copy),
                "-o",
                str(exe_path),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        if base_cc.returncode != 0:
            print(f"ERROR: baseline compile failed:\n{base_cc.stderr}", file=sys.stderr)
            return 1
        base_run = subprocess.run([str(exe_path), "selftest"], capture_output=True, text=True, check=False)
        if base_run.returncode != 0:
            print(f"ERROR: baseline selftest failed:\n{base_run.stdout}\n{base_run.stderr}", file=sys.stderr)
            return 1

    workers = min(8, max(1, (os.cpu_count() or 4)))
    results: list[tuple[SoundMutant, int, int, int, str]] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as ex:
        futs = [
            ex.submit(_run_one_mutant, m, sound_h_src, sound_c_path, repo_root, flow_core, flow_sdk, cc)
            for m in mutants
        ]
        for f in futs:
            results.append(f.result())

    killed = 0
    bad = 0
    cat_counts: dict[str, list[int]] = {}
    for m, count, comp_rc, run_rc, detail in results:
        cat_counts.setdefault(m.category, [0, 0])
        cat_counts[m.category][1] += 1
        if count == 1 and comp_rc == 0 and run_rc != 0:
            killed += 1
            cat_counts[m.category][0] += 1
            print(f"  [KILLED] {m.category:22s} | {m.name:36s} (rc={run_rc})")
        else:
            bad += 1
            print(
                f"  [SURVIVED/ERR] {m.category:22s} | {m.name:36s} "
                f"(count={count} cc={comp_rc} rc={run_rc})\n    {detail}"
            )

    dt = time.monotonic() - t0
    pct = (100.0 * killed / len(mutants)) if mutants else 0.0
    print("  --- Category Summary ---")
    for cat, (k, tot) in cat_counts.items():
        print(f"    {cat:22s}: {k}/{tot}")
    print(f"  [SABOTAGE-GATE] killed={killed}/{len(mutants)} ({pct:.1f}%) in {dt:.2f}s")
    return 0 if (bad == 0 and killed == len(mutants)) else 1


if __name__ == "__main__":
    raise SystemExit(main())
