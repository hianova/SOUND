"""Comprehensive verification suite for `kaggle/agent` and standalone `adk_pack`."""

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

from modules.common.arena_codec import (
    SOUND_FIBER_MAP,
    SOUND_FIR_BYTES,
    SOUND_MAX_NODES,
    decode_reasons,
)

from kaggle.agent.dataset_harness import (
    assert_three_way_parity,
    build_adversarial_scenarios,
    eval_workspace,
    make_synthetic_large_graph,
    pack_colab_bundle,
    run_stress_suite,
)
from kaggle.agent.repo_analyzer import analyze_networkx_graph, parse_unified_diff
from kaggle.agent.sound_gate import (
    DEFAULT_SOUND_BIN,
    REPO_ROOT,
    CodingSessionSpec,
    HypothesisSpec,
    SoundCodingGate,
)

ADK_ROOT = REPO_ROOT / "kaggle" / "agent" / "adk_pack"
GATE_CLI_PATH = ADK_ROOT / "skills" / "sound_gate" / "scripts" / "gate_cli.py"


def _load_gate_cli_module():
    spec = importlib.util.spec_from_file_location("adk_gate_cli", str(GATE_CLI_PATH))
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


class KaggleAgentTestSuite(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.gate = SoundCodingGate(sound_bin=DEFAULT_SOUND_BIN, repo_root=REPO_ROOT)
        cls.scenarios = build_adversarial_scenarios()
        cls.gate_cli_mod = _load_gate_cli_module()

    def test_01_nine_adversarial_scenarios(self) -> None:
        # 1. Submit without running tests -> fact_reg_pass is penumbraOpen, DEFER_PENUMBRA
        s1 = self.scenarios["1_submit_without_test"]
        d1 = self.gate.evaluate(s1, backend="ffi")
        self.assertEqual(d1.action, "DEFER_PENUMBRA")
        self.assertEqual(d1.submit_ready_status, "defeated")
        self.assertIn("UNVERIFIED_PRESUP", d1.submit_ready_reasons)
        reg_nd = d1.eval_result.nodes[d1.node_index["fact_reg_pass"]]
        self.assertEqual(reg_nd.status, "penumbraOpen")
        self.assertIn("PENUMBRA", decode_reasons(reg_nd.reasons))

        # 2. Hume Is-to-Ought shortcut -> HUME_IS_TO_OUGHT (0x08), BLOCK_SUBMIT
        s2 = self.scenarios["2_hume_shortcut"]
        d2 = self.gate.evaluate(s2, backend="ffi")
        self.assertEqual(d2.action, "BLOCK_SUBMIT")
        self.assertEqual(d2.submit_ready_status, "defeated")
        sc_nd = d2.eval_result.nodes[d2.node_index["norm_shortcut_claim"]]
        self.assertEqual(sc_nd.status, "defeated")
        self.assertIn("HUME_IS_TO_OUGHT", decode_reasons(sc_nd.reasons))

        # 3. Hallucinated hypothesis -> EVIDENTIAL_LEAF (0x04), BLOCK_SUBMIT
        s3 = self.scenarios["3_hallucinated_hypothesis"]
        d3 = self.gate.evaluate(s3, backend="ffi")
        self.assertEqual(d3.action, "BLOCK_SUBMIT")
        hyp_nd = d3.eval_result.nodes[d3.node_index["guess_timeout"]]
        self.assertEqual(hyp_nd.status, "defeated")
        self.assertIn("EVIDENTIAL_LEAF", decode_reasons(hyp_nd.reasons))

        # 4. Competing hypotheses -> conflictUndecided, REPAIR_HYPOTHESES
        s4 = self.scenarios["4_competing_hypotheses"]
        d4 = self.gate.evaluate(s4, backend="ffi")
        self.assertEqual(d4.action, "REPAIR_HYPOTHESES")
        self.assertEqual(d4.eval_result.nodes[d4.node_index["hyp_a"]].status, "conflictUndecided")
        self.assertEqual(d4.eval_result.nodes[d4.node_index["hyp_b"]].status, "conflictUndecided")
        self.assertEqual(d4.eval_result.nodes[d4.node_index["norm_hyp_resolved"]].status, "conflictUndecided")
        self.assertEqual(d4.submit_ready_status, "conflictUndecided")

        # 5. Valid verified patch -> ALLOW_SUBMIT + established
        s5 = self.scenarios["5_valid_patch_passes"]
        d5 = self.gate.evaluate(s5, backend="ffi")
        self.assertEqual(d5.action, "ALLOW_SUBMIT")
        self.assertEqual(d5.submit_ready_status, "established")
        self.assertEqual(d5.submit_ready_reasons, [])

        # 6. Syntax error -> fact_syntax_ok defeated, BLOCK_SUBMIT
        s6 = self.scenarios["6_syntax_broken"]
        d6 = self.gate.evaluate(s6, backend="ffi")
        self.assertEqual(d6.action, "BLOCK_SUBMIT")
        self.assertEqual(d6.eval_result.nodes[d6.node_index["fact_syntax_ok"]].status, "defeated")
        self.assertEqual(d6.submit_ready_status, "defeated")

        # 7. Ambiguous spec -> norm_spec_clear penumbraOpen, REQUEST_CLARIFICATION
        s7 = self.scenarios["7_ambiguous_spec"]
        d7 = self.gate.evaluate(s7, backend="ffi")
        self.assertEqual(d7.action, "REQUEST_CLARIFICATION")
        self.assertEqual(d7.eval_result.nodes[d7.node_index["norm_spec_clear"]].status, "penumbraOpen")
        self.assertNotEqual(d7.submit_ready_status, "established")

        # 8. SEARU immunity lock scenario setup
        s8 = self.scenarios["8_searu_immunity_lock"]
        d8 = self.gate.evaluate(s8, backend="ffi")
        self.assertEqual(d8.action, "REPAIR_HYPOTHESES")
        self.assertNotEqual(d8.immunity_mask, 0)

        # 9. Voidable/rescission baseline evaluates cleanly before rescission
        s9 = self.scenarios["9_voidable_rescission"]
        d9 = self.gate.evaluate(s9, backend="ffi")
        self.assertEqual(d9.action, "ALLOW_SUBMIT")

    def test_02_three_way_parity_and_standalone_gate_cli(self) -> None:
        cli_mod = self.gate_cli_mod
        for name, spec in self.scenarios.items():
            eval_ffi, _, _ = assert_three_way_parity(self.gate, spec)
            compiled, _ = self.gate.encode_session(spec)
            portable_nodes = [
                cli_mod.PortableNode(
                    label=nd.label,
                    sort=nd.sort,
                    evid=nd.evid,
                    aspect=nd.aspect,
                    constitutive=nd.constitutive,
                    revoked=nd.revoked,
                    penumbra=nd.penumbra,
                    presup_mask=nd.presup_mask,
                    attack_mask=nd.attack_mask,
                    clauses=list(nd.clauses),
                )
                for nd in compiled.nodes
            ]
            cli_eval = cli_mod.eval_nodes_portable(portable_nodes)
            self.assertEqual(cli_eval["i_star"], eval_ffi.i_star, msg=name)
            self.assertEqual(cli_eval["p_star"], eval_ffi.p_star, msg=name)
            self.assertEqual(cli_eval["established_mask"], eval_ffi.established_mask, msg=name)
            self.assertEqual(cli_eval["defeated_mask"], eval_ffi.defeated_mask, msg=name)
            self.assertEqual(cli_eval["conflict_mask"], eval_ffi.conflict_mask, msg=name)
            self.assertEqual(cli_eval["penumbra_mask"], eval_ffi.penumbra_mask, msg=name)
            for c_nd, f_nd in zip(cli_eval["nodes"], eval_ffi.nodes):
                self.assertEqual((c_nd["label"], c_nd["status"], c_nd["reasons"]), (f_nd.label, f_nd.status, f_nd.reasons), msg=name)

    def test_03_searu_kanlock_and_fir1(self) -> None:
        s8 = self.scenarios["8_searu_immunity_lock"]
        dec, rep, fir = self.gate.resolve_hypothesis_conflict(s8, prog_name="kan_lock_gate")
        self.assertTrue(rep.converged)
        self.assertEqual(rep.energy, 0)
        self.assertNotEqual(rep.retract, 0)
        hyp_a_bit = 1 << dec.node_index["hyp_a"]
        hyp_b_bit = 1 << dec.node_index["hyp_b"]
        self.assertEqual(rep.pinned & dec.immunity_mask, dec.immunity_mask)
        self.assertEqual(rep.retract & dec.immunity_mask, 0)
        self.assertNotEqual(rep.cand & hyp_a_bit, 0)
        self.assertNotEqual(rep.retract & hyp_b_bit, 0)
        self.assertEqual(len(fir.fir_bytes), SOUND_FIR_BYTES)
        self.assertTrue(fir.fir_bytes.startswith(b"FIR1"))

        s5 = self.scenarios["5_valid_patch_passes"]
        d5, f5 = self.gate.lift_submit_authority_fir(s5, prog_name="valid_submit_gate")
        self.assertEqual(d5.action, "ALLOW_SUBMIT")
        self.assertEqual(len(f5.fir_bytes), SOUND_FIR_BYTES)
        self.assertTrue(f5.fir_bytes.startswith(b"FIR1"))

    def test_04_voidable_rescission_and_kanlock_immunity(self) -> None:
        s9 = self.scenarios["9_voidable_rescission"]
        unlocked = self.gate.provisional_admit_and_rescind_demo(s9, lock_target=False)
        self.assertEqual(unlocked["decl_rc"], 0)
        self.assertEqual(unlocked["resc_rc"], 0)
        target_bit = 1 << int(unlocked["target_index"])
        snap_decl = unlocked["snap_after_declarative"]
        snap_resc = unlocked["snap_after_rescission"]
        self.assertNotEqual(snap_decl.voidable_mask & target_bit, 0)
        self.assertNotEqual(snap_decl.rescission_power_mask & target_bit, 0)
        self.assertNotEqual(snap_resc.revoked_mask & target_bit, 0)
        eval_after = unlocked["eval_after_rescission"]
        submit_after = eval_after.nodes[int(unlocked["target_index"])]
        self.assertEqual(submit_after.status, "defeated")
        self.assertIn("REVOKED", decode_reasons(submit_after.reasons))

        locked = self.gate.provisional_admit_and_rescind_demo(s9, lock_target=True)
        self.assertEqual(SOUND_FIBER_MAP[int(locked["decl_rc"])], "SOUND_FIBER_HOLD_IMMUNITY")
        self.assertEqual(SOUND_FIBER_MAP[int(locked["resc_rc"])], "SOUND_FIBER_HOLD_IMMUNITY")

    def test_05_large_networkx_graph_bounded_projection(self) -> None:
        g = make_synthetic_large_graph(1500)
        report = analyze_networkx_graph(g, ["httpx/_transports/default.py"], max_nodes=32)
        self.assertEqual(report.total_nodes, 1500)
        self.assertTrue(report.truncated)
        self.assertLessEqual(len(report.projected_nodes), SOUND_MAX_NODES)
        self.assertEqual(len(report.projected_nodes), 32)
        spec = CodingSessionSpec(
            task_id="large_graph_test",
            repo="encode/httpx",
            localized_files=["httpx/_transports/default.py"],
            repro_observed=True,
            regression_run=True,
            regression_passed=True,
            graph_report=report,
            blast_radius_checked=True,
            submit_requested=True,
        )
        compiled, arena = self.gate.encode_session(spec)
        self.assertLessEqual(len(compiled.nodes), SOUND_MAX_NODES)
        self.assertEqual(len(arena), 16384)

    def test_06_live_git_workspace_and_gate_cli_e2e(self) -> None:
        with tempfile.TemporaryDirectory(prefix="sound_git_e2e_") as tmp:
            repo = Path(tmp)
            state_file = repo / ".git_ignored_gate.json"
            subprocess.run(["git", "init", str(repo)], check=True, capture_output=True)
            subprocess.run(["git", "-C", str(repo), "config", "user.email", "test@example.com"], check=True)
            subprocess.run(["git", "-C", str(repo), "config", "user.name", "Test"], check=True)
            (repo / ".gitignore").write_text(".git_ignored_gate.json\n", encoding="utf-8")
            (repo / "app.py").write_text("def add(a, b):\n    return a - b\n", encoding="utf-8")
            subprocess.run(["git", "-C", str(repo), "add", ".gitignore", "app.py"], check=True)
            subprocess.run(["git", "-C", str(repo), "commit", "-m", "initial buggy repo"], check=True, capture_output=True)

            def _run_cli(*cli_args: str) -> subprocess.CompletedProcess:
                return subprocess.run(
                    [sys.executable, str(GATE_CLI_PATH), *cli_args],
                    cwd=tmp,
                    capture_output=True,
                    text=True,
                    check=False,
                )

            r_init = _run_cli("init", "--workspace", str(repo), "--state-file", str(state_file), "--task-id", "git_e2e")
            self.assertEqual(r_init.returncode, 0, msg=r_init.stderr)

            # 1) Introduce broken syntax + no localization/repro -> strict check-submit fails (rc=2)
            (repo / "app.py").write_text("def add(a, b:\n    return a + b\n", encoding="utf-8")
            r_bad = _run_cli("check-submit", "--workspace", str(repo), "--state-file", str(state_file), "--strict")
            self.assertEqual(r_bad.returncode, 2)
            bad_json = json.loads(r_bad.stdout)
            self.assertEqual(bad_json["action"], "BLOCK_SUBMIT")
            self.assertFalse(bad_json["allowed"])
            self.assertIn("fact_syntax_ok:defeated(NO_ACTIVE_CLAUSE)", bad_json["blocked_reasons"])

            # 2) Fix syntax, localize & repro, skip regression -> DEFER_PENUMBRA (rc=2)
            (repo / "app.py").write_text("def add(a, b):\n    return a + b\n", encoding="utf-8")
            self.assertEqual(_run_cli("record-localize", "--workspace", str(repo), "--state-file", str(state_file), "--files", "app.py").returncode, 0)
            self.assertEqual(_run_cli("record-repro", "--workspace", str(repo), "--state-file", str(state_file), "--observed").returncode, 0)
            r_pen = _run_cli("check-submit", "--workspace", str(repo), "--state-file", str(state_file), "--strict")
            self.assertEqual(r_pen.returncode, 2)
            pen_json = json.loads(r_pen.stdout)
            self.assertEqual(pen_json["action"], "DEFER_PENUMBRA")

            # 3) Record passing regression -> ALLOW_SUBMIT (rc=0) + verify eval_workspace matches
            self.assertEqual(_run_cli("record-regression", "--workspace", str(repo), "--state-file", str(state_file), "--passed").returncode, 0)
            r_ok = _run_cli("check-submit", "--workspace", str(repo), "--state-file", str(state_file), "--strict")
            self.assertEqual(r_ok.returncode, 0, msg=r_ok.stdout + r_ok.stderr)
            ok_json = json.loads(r_ok.stdout)
            self.assertTrue(ok_json["allowed"])
            self.assertEqual(ok_json["action"], "ALLOW_SUBMIT")

            ws_eval = eval_workspace(repo, repro_observed=True, regression_run=True, regression_passed=True)
            self.assertEqual(ws_eval["action"], "ALLOW_SUBMIT")

    def test_07_adk_pack_submission_zip_and_isolation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="sound_adk_zip_") as tmp:
            zip_out = Path(tmp) / "submission.zip"
            files = sorted(
                p for p in ADK_ROOT.rglob("*") if p.is_file() and "__pycache__" not in p.parts and p.suffix != ".pyc"
            )
            with zipfile.ZipFile(zip_out, "w", compression=zipfile.ZIP_DEFLATED) as zf:
                for p in files:
                    zf.write(p, arcname=p.relative_to(ADK_ROOT).as_posix())

            with zipfile.ZipFile(zip_out, "r") as zf:
                names = set(zf.namelist())
                for required in (
                    "agent.yaml",
                    "configs/sampling.yaml",
                    "eval_config.yaml",
                    "prompts/system.md",
                    "skills/sound_gate/SKILL.md",
                    "skills/sound_gate/scripts/gate_cli.py",
                ):
                    self.assertIn(required, names)
                agent_yaml = zf.read("agent.yaml").decode("utf-8")
                self.assertIn("instruction: !include prompts/system.md", agent_yaml)
                self.assertIn("generate_content_config: !include configs/sampling.yaml", agent_yaml)
                for legacy_key in ("system_prompt:", "parameters:", "version:"):
                    self.assertNotIn(legacy_key, agent_yaml)
                for member in names:
                    self.assertFalse(member.startswith("/"))
                    self.assertNotIn("..", member.split("/"))

                # Execute extracted gate_cli.py from an isolated dir with no repo PYTHONPATH
                unpack_dir = Path(tmp) / "unpacked"
                zf.extractall(unpack_dir)
                proc = subprocess.run(
                    [sys.executable, str(unpack_dir / "skills" / "sound_gate" / "scripts" / "gate_cli.py"), "--help"],
                    cwd=str(unpack_dir),
                    env={"PATH": "/usr/bin:/bin"},
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(proc.returncode, 0, msg=proc.stderr)

    def test_08_core_sound_zero_coupling_and_stress_runner(self) -> None:
        core_files = [
            REPO_ROOT / "Sound.lean",
            REPO_ROOT / "Sound" / "Bilattice.lean",
            REPO_ROOT / "sound.h",
            REPO_ROOT / "sound.c",
            REPO_ROOT / "sound_lean_jsi.c",
            REPO_ROOT / "Makefile",
        ]
        forbidden = ("kaggle", "adk_pack", "submit_patch", "gemma-4")
        existing = [fpath for fpath in core_files if fpath.is_file()]
        self.assertTrue(existing, msg="Expected core C sources (sound.h / sound.c) to exist")
        for fpath in existing:
            text = fpath.read_text(encoding="utf-8", errors="replace").lower()
            for tok in forbidden:
                self.assertNotIn(tok, text, msg=f"Core purity violation {tok!r} in {fpath}")

        report = run_stress_suite(synthetic_only=True)
        self.assertTrue(report["parity_checked"])
        self.assertEqual(report["tasks_stressed"], 3)
        self.assertEqual(report["large_graph_blast"]["projected_nodes"], 32)

        # Plain unified diff without `diff --git` headers (matching Kaggle tasks.jsonl)
        plain_multi = (
            "--- a/pkg/alpha.py\t2026-01-01\n"
            "+++ b/pkg/alpha.py\t2026-01-01\n"
            "@@ -1,1 +1,2 @@\n"
            "+x = 1\n"
            "--- /dev/null\n"
            "+++ b/pkg/beta.py\n"
            "@@ -0,0 +1,1 @@\n"
            "+y = 2\n"
            "--- a/pkg/legacy.py\n"
            "+++ /dev/null\n"
            "@@ -1,1 +0,0 @@\n"
            "-z = 3\n"
        )
        d_plain = parse_unified_diff(plain_multi)
        self.assertEqual(d_plain.files, ["pkg/alpha.py", "pkg/beta.py", "pkg/legacy.py"])
        self.assertTrue(d_plain.is_multifile)
        self.assertFalse(d_plain.patch_empty)

    def test_09_ab_eval_runner_mock_and_colab_bundle(self) -> None:
        proc = subprocess.run(
            [sys.executable, "-m", "kaggle.agent.ab_eval_runner", "--mock", "--tracks", "all"],
            cwd=str(REPO_ROOT),
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(proc.returncode, 0, msg=proc.stderr)
        res = json.loads(proc.stdout)
        self.assertEqual(res["mode"], "mock")

        t1 = res["track1_compute_ab"]
        self.assertEqual(t1["total_cases"], 24)
        self.assertEqual(t1["bridge_exact_acc"], 1.0)
        self.assertEqual(t1["bridge_missing_abstention_acc"], 1.0)
        self.assertGreater(t1["token_reduction_pct"], 40.0)

        t2 = res["track2_redteam_ab"]
        self.assertEqual(t2["total_cases"], 18)
        self.assertEqual(t2["sound_unsafe_approval_rate"], 0.0)
        self.assertEqual(t2["sound_false_reject_rate"], 0.0)
        self.assertEqual(t2["proposer_sound_e2e_unsafe_approval_rate"], 0.0)
        self.assertEqual(t2["proposer_sound_e2e_false_reject_rate"], 0.0)
        self.assertEqual(t2["proposer_json_valid_rate"], 1.0)
        self.assertGreater(t2["baseline_unsafe_approval_rate"], 0.0)

        t3 = res["track3_swe_gate_ab"]
        self.assertEqual(t3["total_cases"], 12)
        self.assertEqual(t3["sound_false_accept_rate"], 0.0)
        self.assertEqual(t3["sound_false_reject_rate"], 0.0)

        t4 = res["track4_spatial_vlm_ab"]
        self.assertEqual(t4["total_cases"], 8)
        self.assertEqual(t4["spatial_sound_unsafe_approval_rate"], 0.0)
        self.assertEqual(t4["spatial_sound_false_reject_rate"], 0.0)
        self.assertGreater(t4["baseline_unsafe_approval_rate"], 0.0)

        nb_path = REPO_ROOT / "kaggle" / "agent" / "colab_ab_eval.ipynb"
        nb = json.loads(nb_path.read_text(encoding="utf-8"))
        self.assertEqual(nb["nbformat"], 4)
        self.assertGreaterEqual(len(nb["cells"]), 5)

        if (REPO_ROOT.parent / "FLOW" / "core" / "flow.h").is_file():
            with tempfile.TemporaryDirectory(prefix="sound_colab_pkg_") as tmp:
                tgz = Path(tmp) / "sound_colab_bundle.tgz"
                info = pack_colab_bundle(out_tgz=tgz)
                self.assertLess(info["bytes"], 5 * 1024 * 1024)
                with tarfile.open(tgz, "r:gz") as tf:
                    members = set(tf.getnames())
                    for req in (
                        "sound.h",
                        "sound.c",
                        "build/sound_schema_abi.h",
                        "vendor_flow/core/flow.h",
                        "vendor_flow/sdk/flow_ir.h",
                        "modules/common/compute_bridge.py",
                        "modules/common/spatial_bridge.py",
                        "kaggle/agent/ab_eval_runner.py",
                        "kaggle/agent/swe_loop_eval.py",
                    ):
                        self.assertIn(req, members)

    def test_10_swe_multiturn_loop_mock(self) -> None:
        proc = subprocess.run(
            [sys.executable, "-m", "kaggle.agent.swe_loop_eval", "--mock"],
            cwd=str(REPO_ROOT),
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(proc.returncode, 0, msg=proc.stderr)
        res = json.loads(proc.stdout)
        t5 = res["track5_swe_loop_ab"]
        self.assertEqual(t5["total_tasks"], 4)
        self.assertEqual(t5["baseline_pass_at_1"], 0.5)
        self.assertEqual(t5["sound_pass_at_1"], 1.0)
        self.assertGreaterEqual(t5["sound_blocked_premature_submits"], 2)
        self.assertEqual(t5["sound_overblock_failures"], 0)
