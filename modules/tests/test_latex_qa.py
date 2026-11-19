"""Unit and end-to-end tests for `modules/latex/` (Semantic LaTeX <-> SOUND MVP)."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from modules.common.arena_codec import SoundFiber, fnv1a_64, policy_verify_bytes
from modules.common.mcq_engine import validate_topic_pack
from modules.latex import (
    LatexCompileError,
    compile_latex_file,
    compile_latex_text,
    emit_latex_report,
    escape_latex,
    export_topic_pack,
    probe_premise_necessity,
    run_embedded_qa_suite,
    validate_emitted_latex_structure,
    verify_scenario,
)

REPO_ROOT = Path(__file__).resolve().parents[2]
SOUND_BIN = REPO_ROOT / "build" / "sound"
DOCS_DIR = REPO_ROOT / "modules" / "latex" / "docs"
MATH_TEX = DOCS_DIR / "real_analysis_evt_ivt.tex"
VAT_TEX = DOCS_DIR / "tw_vat_statute_provisos.tex"


class LatexQATestSuite(unittest.TestCase):
    """Verify semantic LaTeX compilation, Ring-0 Q&A probes, ablation, and LaTeX emission."""

    def test_01_comment_masking_nested_math_and_exact_byte_spans(self) -> None:
        tex = r"""
\documentclass{article}
\begin{document}
\sounddoc{mini_math}{math}{INTL}{Nested Math \{100\%\} Spec}
% \begin{soundtheorem}[id=fake_commented]{Should Be Ignored}
\begin{soundaxiom}[id=ax_base]{Completeness $\frac{1}{2} \in \mathbb{R}$}
\label{ax:base}
Axiom with 50\% escaped percent and $\{x \in \mathbb{R}\}$.
\soundkanlock
\end{soundaxiom}
\begin{soundhyp}[id=hyp_x]{Non-zero $x \neq 0$}
\label{hyp:x}
Given $f(x) = \frac{1}{x^2 + \{1\}}$.
\end{soundhyp}
\begin{soundtheorem}[id=thm_inv]{Continuous Reciprocal}
\label{thm:inv}
$\frac{1}{x}$ is continuous on $K$.
\soundclause{ax_base, hyp_x}
\soundpresup{hyp_x}
\end{soundtheorem}
\end{document}
"""
        doc = compile_latex_text(tex, SOUND_BIN)
        self.assertNotIn("fake_commented", doc.node_index)
        self.assertEqual(len(doc.nodes), 3)
        for nd in doc.nodes:
            sliced = doc.source_bytes[nd.span_start : nd.span_end].decode("utf-8")
            self.assertTrue(sliced.startswith(f"\\begin{{{nd.env_name}}}"))
            self.assertTrue(sliced.endswith(f"\\end{{{nd.env_name}}}"))
            self.assertEqual(nd.payload_hash, fnv1a_64(doc.source_bytes[nd.span_start : nd.span_end]))
        v_ok = verify_scenario(
            doc, SOUND_BIN, bind_facts=["hyp_x"], target_node="thm_inv", claim_status="established"
        )
        self.assertTrue(v_ok.comprehension_pass)
        self.assertEqual(v_ok.actual_status, "established")

    def test_02_compile_validation_guards(self) -> None:
        no_doc = r"\begin{soundhyp}[id=h1]{H}\end{soundhyp}"
        with self.assertRaises(LatexCompileError):
            compile_latex_text(no_doc, SOUND_BIN)

        dangling = r"""
\sounddoc{bad_ref}{math}{INTL}{Bad}
\begin{soundtheorem}[id=t1]{T}\soundclause{missing_node}\end{soundtheorem}
"""
        with self.assertRaises(LatexCompileError):
            compile_latex_text(dangling, SOUND_BIN)

        bad_bind = r"""
\sounddoc{bad_bind}{math}{INTL}{Bad}
\begin{soundhyp}[id=h1]{H}\soundkanlock\end{soundhyp}
"""
        with self.assertRaises(LatexCompileError):
            compile_latex_text(bad_bind, SOUND_BIN)

        unclosed = r"""
\sounddoc{bad_end}{math}{INTL}{Bad}
\begin{soundhyp}[id=h1]{H}
"""
        with self.assertRaises(LatexCompileError):
            compile_latex_text(unclosed, SOUND_BIN)

    def test_03_math_doc_embedded_qa_searu_and_topic_pack(self) -> None:
        doc = compile_latex_file(MATH_TEX, SOUND_BIN)
        pack = export_topic_pack(doc)
        validate_topic_pack(pack)
        self.assertEqual(doc.pack_id, "math_real_evt_ivt")

        verdicts = run_embedded_qa_suite(doc, SOUND_BIN)
        self.assertEqual(len(verdicts), 7)
        by_id = {v.qa_id: v for v in verdicts}
        for qid, v in by_id.items():
            self.assertTrue(v.comprehension_pass, f"Math probe {qid} failed: {v.diagnosis_zh}")

        searu_v = by_id["qa_searu_dim_conflict"]
        self.assertEqual(searu_v.actual_status, "conflictUndecided")
        self.assertIsNotNone(searu_v.repair_result)
        self.assertTrue(searu_v.repair_result.converged)
        self.assertEqual(searu_v.repair_result.energy, 0)
        self.assertEqual(len(searu_v.retracted_nodes), 1)

        tamp_v = by_id["qa_tamper_kanlock_axiom"]
        self.assertEqual(tamp_v.landing_fiber, SoundFiber.FALLBACK_POLICY_VIOLATION)
        self.assertIn("IMMUNITY_SLOT", tamp_v.inject_reasons)
        self.assertIn("POLICY_SLOT", tamp_v.inject_reasons)

        irr_v = by_id["qa_irrealis_hyp"]
        self.assertEqual(irr_v.landing_fiber, SoundFiber.FALLBACK_IRREALIS)

    def test_04_tw_vat_law_embedded_qa_provisos_and_tamper(self) -> None:
        doc = compile_latex_file(VAT_TEX, SOUND_BIN)
        pack = export_topic_pack(doc)
        validate_topic_pack(pack)
        self.assertEqual(doc.pack_id, "tw_vat_latex_provisos")

        verdicts = run_embedded_qa_suite(doc, SOUND_BIN)
        self.assertEqual(len(verdicts), 7)
        by_id = {v.qa_id: v for v in verdicts}
        for qid, v in by_id.items():
            self.assertTrue(v.comprehension_pass, f"Law probe {qid} failed: {v.diagnosis_zh}")

        self.assertEqual(by_id["qa_waive_exempt_credit"].actual_status, "established")
        self.assertIn("DIRECT_CLAIM_FORBID", by_id["qa_tamper_direct_forbid"].inject_reasons)
        self.assertIn("POLICY_SLOT", by_id["qa_tamper_policy_effect"].inject_reasons)

        # Without waiver, fresh agriculture exemption blocks input tax credit
        no_waive = verify_scenario(
            doc,
            SOUND_BIN,
            bind_facts=["fact_fresh_agri", "fact_input_invoice"],
            target_node="eff_input_credit",
            claim_status="defeated",
            expected_reasons=["ATTACKED"],
        )
        self.assertTrue(no_waive.comprehension_pass)

    def test_05_socratic_premise_necessity_ablation(self) -> None:
        math_doc = compile_latex_file(MATH_TEX, SOUND_BIN)
        rows = probe_premise_necessity(
            math_doc,
            SOUND_BIN,
            base_facts=[
                "hyp_closed",
                "hyp_bounded",
                "hyp_finite_dim",
                "hyp_nonempty",
                "hyp_continuous",
                "hyp_strict_mono",  # unnecessary for EVT
            ],
            target_node="thm_evt",
        )
        nec_map = {r["dropped_fact"]: r["necessary"] for r in rows}
        self.assertTrue(nec_map["hyp_closed"])
        self.assertTrue(nec_map["hyp_bounded"])
        self.assertTrue(nec_map["hyp_finite_dim"])
        self.assertTrue(nec_map["hyp_nonempty"])
        self.assertTrue(nec_map["hyp_continuous"])
        self.assertFalse(nec_map["hyp_strict_mono"])

        vat_doc = compile_latex_file(VAT_TEX, SOUND_BIN)
        vat_rows = probe_premise_necessity(
            vat_doc,
            SOUND_BIN,
            base_facts=["fact_export_goods", "fact_input_invoice", "fact_within_3yr"],
            target_node="eff_zero_refund",
        )
        vat_nec = {r["dropped_fact"]: r["necessary"] for r in vat_rows}
        self.assertTrue(vat_nec["fact_export_goods"])
        self.assertTrue(vat_nec["fact_input_invoice"])
        self.assertFalse(vat_nec["fact_within_3yr"])

    def test_06_wrong_learner_claim_diagnosis_and_tamper_seal(self) -> None:
        doc = compile_latex_file(MATH_TEX, SOUND_BIN)
        wrong = verify_scenario(
            doc,
            SOUND_BIN,
            bind_facts=["hyp_closed", "hyp_bounded", "hyp_finite_dim", "hyp_continuous"],
            target_node="thm_evt",
            claim_status="established",
        )
        self.assertFalse(wrong.comprehension_pass)
        self.assertEqual(wrong.actual_status, "defeated")
        self.assertIn("UNVERIFIED_PRESUP", wrong.reason_names)
        self.assertTrue(
            any(r.node_id == "hyp_nonempty" and r.tex_label == "hyp:nonempty" for r in wrong.provenance_refs)
        )
        self.assertIn("尚缺必要前提", wrong.diagnosis_zh)

        # Bit-flip policy_mask inside sealed arena -> Ring-0 policy_verify catches tampering
        tampered = bytearray(doc.arena_bytes)
        tampered[216 * 64 + 32] ^= 0xFF
        self.assertEqual(
            policy_verify_bytes(bytes(tampered), SOUND_BIN),
            SoundFiber.FALLBACK_POLICY_TAMPER,
        )

    def test_07_emit_latex_report_determinism_and_structure(self) -> None:
        self.assertEqual(escape_latex(r"100% _ $ & # ~ ^ {a}"), r"100\% \_ \$ \& \# \textasciitilde{} \textasciicircum{} \{a\}")

        for tex_path, target, facts in [
            (MATH_TEX, "thm_evt", ["hyp_closed", "hyp_bounded", "hyp_finite_dim", "hyp_nonempty", "hyp_continuous"]),
            (VAT_TEX, "eff_zero_refund", ["fact_export_goods", "fact_input_invoice"]),
        ]:
            doc = compile_latex_file(tex_path, SOUND_BIN)
            verdicts = run_embedded_qa_suite(doc, SOUND_BIN)
            abl = probe_premise_necessity(doc, SOUND_BIN, base_facts=facts, target_node=target)
            rep1 = emit_latex_report(doc, verdicts, ablations=abl, standalone=True)
            rep2 = emit_latex_report(doc, verdicts, ablations=abl, standalone=True)
            self.assertEqual(rep1, rep2)
            self.assertEqual(validate_emitted_latex_structure(rep1), [])
            self.assertIn(r"\section{Socratic Premise Necessity Ablation}", rep1)

    def test_08_cli_subcommands_and_interactive_repl(self) -> None:
        with tempfile.TemporaryDirectory(prefix="sound_latex_cli_") as tmp_dir:
            tmp = Path(tmp_dir)
            snd_out = tmp / "vat.snd"
            pack_out = tmp / "vat_pack.json"
            rep_out = tmp / "math_report.tex"
            repl_out = tmp / "repl_report.tex"

            for cmd in [
                [sys.executable, "-m", "modules.latex.cli", "compile", str(VAT_TEX), "--out-snd", str(snd_out), "--out-pack", str(pack_out)],
                [sys.executable, "-m", "modules.latex.cli", "verify", str(MATH_TEX)],
                [sys.executable, "-m", "modules.latex.cli", "verify", str(VAT_TEX)],
                [sys.executable, "-m", "modules.latex.cli", "ablate", str(VAT_TEX), "--target", "eff_zero_refund", "--facts", "fact_export_goods,fact_input_invoice"],
                [sys.executable, "-m", "modules.latex.cli", "ask", str(MATH_TEX), "--target", "thm_evt", "--bind", "hyp_closed,hyp_bounded,hyp_finite_dim,hyp_nonempty,hyp_continuous", "--claim", "established"],
                [sys.executable, "-m", "modules.latex.cli", "emit-report", str(MATH_TEX), "--out", str(rep_out), "--ablate-target", "thm_evt", "--ablate-facts", "hyp_closed,hyp_bounded,hyp_finite_dim,hyp_nonempty,hyp_continuous"],
            ]:
                proc = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True, check=False)
                self.assertEqual(proc.returncode, 0, f"CLI failed {cmd}: {proc.stderr}\n{proc.stdout}")

            self.assertEqual(len(snd_out.read_bytes()), 16384)
            validate_topic_pack(json.loads(pack_out.read_text(encoding="utf-8")))
            self.assertEqual(validate_emitted_latex_structure(rep_out.read_text(encoding="utf-8")), [])

            repl_script = "\n".join(
                [
                    "list",
                    "bind fact_export_goods fact_input_invoice",
                    "ask eff_zero_refund established",
                    "tamper eff_zero_refund",
                    f"emit {repl_out}",
                    "quit",
                    "",
                ]
            )
            repl_proc = subprocess.run(
                [sys.executable, "-m", "modules.latex.cli", "interactive", str(VAT_TEX)],
                cwd=REPO_ROOT,
                input=repl_script,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(repl_proc.returncode, 0, repl_proc.stderr + repl_proc.stdout)
            self.assertIn("SOUND_FIBER_COMMITTED", repl_proc.stdout)
            self.assertIn("SOUND_FIBER_FALLBACK_POLICY_VIOLATION", repl_proc.stdout)
            self.assertEqual(validate_emitted_latex_structure(repl_out.read_text(encoding="utf-8")), [])


if __name__ == "__main__":
    unittest.main()
