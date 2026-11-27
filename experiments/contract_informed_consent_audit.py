#!/usr/bin/env python3
"""
SOUND v3.0.0 — Experiment 5: Asymmetric Contract Informed-Consent, Compositional Loophole & Coverage Audit
=============================================================================================================
Audits 5 real-world paired contract families (10 contracts) plus 2 end-to-end raw-text
Coverage Gate intake pipelines:
  1. elder_investment_policy_{predatory,fair}.json
  2. joint_guarantor_loan_{predatory,fair}.json
  3. senior_care_facility_{predatory,fair}.json
  4. housing_lease_trap_{predatory,fair}.json
  5. compositional_annex_loophole_{predatory,fair}.json (0 single-clause faults, caught by H^1/Hohfeld graph!)
  6. Natural-language intake: elder_policy_raw_zh.txt + omitting LLM proposer -> fail-closed COVERAGE_GAP veto
                              elder_policy_raw_zh.txt + downplaying LLM proposer -> fail-closed FACT_FIDELITY_MISMATCH veto
                              elder_policy_fair_raw_zh.txt + complete fair proposer -> CERTIFIED

Verifies:
  - 5/5 predatory JSON contracts fail-closed VETOED with repaired_certified == 1, plus plain-language receipt.
  - 5/5 fair JSON contracts CERTIFIED with zero violations and benefit_drift <= 0.
  - Raw-text Intake Gate catches omitting proposer AND semantic-downplaying proposer (0 uncovered spans yet caught by Fact-Fidelity!), passing fair contract + optional INFO Cognitive Opacity check.
  - All 13 dynamically emitted Lean 4 certificates compile under `-DwarningAsError=true`,
    pass `leanchecker`, and have zero `sorryAx` and zero `Lean.ofReduceBool`.
"""

from __future__ import annotations

import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import List, Tuple


def _find_elan_tool(name: str) -> Path:
    env_override = os.environ.get(name.upper())
    if env_override:
        return Path(env_override)
    which_p = shutil.which(name)
    if which_p:
        return Path(which_p)
    home_p = Path.home() / ".elan" / "bin" / name
    if home_p.exists():
        return home_p
    return Path("/Users/kuangtalin/.elan/bin") / name


ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
SOUND_BIN = BUILD / "sound"
CONTRACTS_DIR = ROOT / "benchmarks" / "contracts"
LEAN = _find_elan_tool("lean")
LEANCHECKER = _find_elan_tool("leanchecker")

sys.path.insert(0, str(ROOT / "tools"))
from sound_contract_intake import DEFAULT_INFO_BIN, run_intake_audit

PAIRS = [
    ("elder_investment_policy", "elder_investment_policy_predatory.json", "elder_investment_policy_fair.json"),
    ("joint_guarantor_loan", "joint_guarantor_loan_predatory.json", "joint_guarantor_loan_fair.json"),
    ("senior_care_facility", "senior_care_facility_predatory.json", "senior_care_facility_fair.json"),
    ("housing_lease_trap", "housing_lease_trap_predatory.json", "housing_lease_trap_fair.json"),
    ("compositional_annex_loophole", "compositional_annex_loophole_predatory.json", "compositional_annex_loophole_fair.json"),
]


def _replay_contract_lean(idx: int, label: str, lean_path: Path, tmp_dir: Path) -> Tuple[str, int]:
    mod_name = f"CertContractAudit{idx}"
    olean_path = tmp_dir / f"{mod_name}.olean"
    src = lean_path.read_text(encoding="utf-8")
    assert not re.search(r"\b(sorry|admit)\b", src), f"sorry/admit found in {lean_path}"
    ns_m = re.search(r"^\s*namespace\s+(\S+)", src, re.MULTILINE)
    assert ns_m is not None, f"Missing namespace in {lean_path}"
    ns = ns_m.group(1)
    thms = re.findall(r"^\s*theorem\s+(\S+)", src, re.MULTILINE)
    assert len(thms) >= 3, f"Expected >=3 theorems in {lean_path}, got {thms}"

    env = dict(os.environ)
    env["LEAN_PATH"] = f"{BUILD / 'lean'}:{tmp_dir}"
    c_res = subprocess.run(
        [str(LEAN), "-DwarningAsError=true", "--root", str(tmp_dir), "-o", str(olean_path), str(lean_path)],
        cwd=str(ROOT),
        env=env,
        capture_output=True,
        text=True,
        check=False,
    )
    assert c_res.returncode == 0, f"lean compile failed for {label}:\n{c_res.stdout}\n{c_res.stderr}"

    chk_res = subprocess.run(
        [str(LEANCHECKER), mod_name],
        cwd=str(ROOT),
        env=env,
        capture_output=True,
        text=True,
        check=False,
    )
    assert chk_res.returncode == 0, f"leanchecker failed for {label}:\n{chk_res.stdout}\n{chk_res.stderr}"

    axiom_in = f"import {mod_name}\n" + "".join(f"#print axioms {ns}.{t}\n" for t in thms)
    ax_res = subprocess.run(
        [str(LEAN), "-DwarningAsError=true", "--stdin"],
        cwd=str(ROOT),
        env=env,
        input=axiom_in,
        capture_output=True,
        text=True,
        check=False,
    )
    assert ax_res.returncode == 0, f"axiom audit failed for {label}:\n{ax_res.stderr}"
    assert "sorryAx" not in ax_res.stdout, f"sorryAx leaked in {label}:\n{ax_res.stdout}"
    assert "Lean.ofReduceBool" not in ax_res.stdout, f"Lean.ofReduceBool leaked in {label}:\n{ax_res.stdout}"
    return label, len(thms)


def main() -> int:
    assert SOUND_BIN.is_file(), f"Missing {SOUND_BIN}; run `make all` first."
    print("[Experiment 5] Informed-Consent, Compositional Loophole Graph & Coverage Gate Audit")

    with tempfile.TemporaryDirectory(prefix="sound_contracts_") as td:
        tmp_dir = Path(td)
        lean_jobs: List[Tuple[int, str, Path, Path]] = []
        idx = 0

        for family, pred_file, fair_file in PAIRS:
            pred_res: dict = {}
            fair_res: dict = {}
            for role, fname in (("predatory", pred_file), ("fair", fair_file)):
                tag = f"{family}_{role}"
                in_p = CONTRACTS_DIR / fname
                lean_p = tmp_dir / f"{tag}.lean"
                fir_p = tmp_dir / f"{tag}.fir2"
                json_p = tmp_dir / f"{tag}.json"
                cert_p = tmp_dir / f"{tag}.cert.bin"
                rt_json_p = tmp_dir / f"{tag}.rt.json"
                spec_sndc = tmp_dir / f"{tag}.sndc"
                sndc_json_p = tmp_dir / f"{tag}.sndc.json"
                receipt_p = tmp_dir / f"{tag}.receipt.md"
                proc = subprocess.run(
                    [
                        str(SOUND_BIN),
                        "audit-contract",
                        str(in_p),
                        "--out-lean",
                        str(lean_p),
                        "--out-fir",
                        str(fir_p),
                        "--out-json",
                        str(json_p),
                        "--out-cert",
                        str(cert_p),
                        "--out-receipt",
                        str(receipt_p),
                    ],
                    cwd=str(ROOT),
                    capture_output=True,
                    text=True,
                    check=False,
                )
                assert proc.returncode == 0, f"audit-contract failed on {fname}:\n{proc.stderr}"
                assert fir_p.read_bytes().startswith(b"FIR2"), f"Missing FIR2 header for {fname}"
                assert cert_p.read_bytes().startswith(b"SNDC"), f"Missing SNDC cert for {fname}"
                assert cert_p.stat().st_size < json_p.stat().st_size
                u_res = subprocess.run(
                    [str(SOUND_BIN), "unpack-cert", str(cert_p), str(rt_json_p)],
                    cwd=str(ROOT),
                    capture_output=True,
                    text=True,
                    check=False,
                )
                assert u_res.returncode == 0 and rt_json_p.read_bytes() == json_p.read_bytes()
                pk_res = subprocess.run(
                    [str(SOUND_BIN), "pack-contract", str(in_p), str(spec_sndc)],
                    cwd=str(ROOT),
                    capture_output=True,
                    text=True,
                    check=False,
                )
                assert pk_res.returncode == 0 and spec_sndc.stat().st_size < in_p.stat().st_size
                au_res = subprocess.run(
                    [str(SOUND_BIN), "audit-contract", str(spec_sndc), "--out-json", str(sndc_json_p)],
                    cwd=str(ROOT),
                    capture_output=True,
                    text=True,
                    check=False,
                )
                assert au_res.returncode == 0 and sndc_json_p.read_bytes() == json_p.read_bytes()
                data = json.loads(json_p.read_text(encoding="utf-8"))
                assert data["fir_eval_ok"] is True, f"FIR eval failed for {fname}"
                receipt_txt = receipt_p.read_text(encoding="utf-8")

                if role == "predatory":
                    assert data["certified"] == 0 and data["verdict"] == "VETOED"
                    assert data["viol_flags"] != 0
                    assert (data["num_predatory_clauses"] + data["num_malicious_edges"]) >= 3
                    assert data["benefit_drift"] > 0
                    assert data["repaired_certified"] == 1
                    assert "VETOED — DO NOT SIGN" in receipt_txt
                    if family == "compositional_annex_loophole":
                        assert data["num_predatory_clauses"] == 0
                        assert data["graph_sheaf_h1_obstructed"] == 1
                        assert (data["graph_bypass_mask"] & 1) == 1
                        assert "REDLINE SEVER" in receipt_txt and "H^1 != 0" in receipt_txt
                    pred_res = data
                else:
                    assert data["certified"] == 1 and data["verdict"] == "CERTIFIED"
                    assert data["viol_flags"] == 0
                    assert data["num_predatory_clauses"] == 0
                    assert data["graph_sheaf_h1_obstructed"] == 0
                    assert data["graph_bypass_mask"] == 0
                    assert data["benefit_drift"] <= 0
                    assert "CERTIFIED — SAFE TO SIGN" in receipt_txt
                    fair_res = data

                lean_jobs.append((idx, tag, lean_p, tmp_dir))
                idx += 1

            print(
                f"  {family:<28} | predatory: VETOED (worst={pred_res['worst_case_loss']:>6}, "
                f"drift=+{pred_res['benefit_drift']:<6}, viol=0x{pred_res['viol_flags']:03x}, "
                f"clauses={pred_res['num_predatory_clauses']}, edges={pred_res['num_malicious_edges']}, "
                f"rep={pred_res['repaired_worst_loss']}) | "
                f"fair: CERTIFIED (worst={fair_res['worst_case_loss']:>5}, drift={fair_res['benefit_drift']})"
            )

        # Raw-text Natural Language Intake Coverage Gate contrast (ZH contract)
        omit_lean = tmp_dir / "intake_omit.lean"
        omit_fir = tmp_dir / "intake_omit.fir"
        omit_rcpt = tmp_dir / "intake_omit.receipt.md"
        omit_res, uncovered_omit = run_intake_audit(
            CONTRACTS_DIR / "elder_policy_raw_zh.txt",
            CONTRACTS_DIR / "elder_policy_proposer_omitting.json",
            sound_bin=SOUND_BIN,
            out_lean=omit_lean,
            out_fir=omit_fir,
            out_receipt=omit_rcpt,
        )
        assert omit_res["certified"] == 0 and omit_res["verdict"] == "VETOED"
        assert omit_res["coverage_gaps"] == 1 and len(uncovered_omit) == 1
        assert (omit_res["viol_flags"] & 0x800) == 0x800
        assert "Coverage Gate" in omit_rcpt.read_text(encoding="utf-8")
        lean_jobs.append((idx, "intake_zh_omitting_proposer", omit_lean, tmp_dir))
        idx += 1

        fair_in_lean = tmp_dir / "intake_fair.lean"
        fair_in_fir = tmp_dir / "intake_fair.fir"
        fair_in_rcpt = tmp_dir / "intake_fair.receipt.md"
        fair_in_res, uncovered_fair = run_intake_audit(
            CONTRACTS_DIR / "elder_policy_fair_raw_zh.txt",
            CONTRACTS_DIR / "elder_policy_proposer_fair.json",
            sound_bin=SOUND_BIN,
            out_lean=fair_in_lean,
            out_fir=fair_in_fir,
            out_receipt=fair_in_rcpt,
        )
        assert fair_in_res["certified"] == 1 and fair_in_res["verdict"] == "CERTIFIED"
        assert fair_in_res["coverage_gaps"] == 0 and len(uncovered_fair) == 0
        assert len(fair_in_res["fidelity_mismatches"]) == 0
        lean_jobs.append((idx, "intake_zh_fair_complete", fair_in_lean, tmp_dir))
        idx += 1

        # Semantic-downplaying proposer: 100% sentence span coverage (0 uncovered),
        # yet shrinks 15,000 -> 5,000 & launders clause 3 unilateral penalty as benign.
        dp_lean = tmp_dir / "intake_downplay.lean"
        dp_fir = tmp_dir / "intake_downplay.fir"
        dp_rcpt = tmp_dir / "intake_downplay.receipt.md"
        dp_res, uncovered_dp = run_intake_audit(
            CONTRACTS_DIR / "elder_policy_raw_zh.txt",
            CONTRACTS_DIR / "elder_policy_proposer_downplaying.json",
            sound_bin=SOUND_BIN,
            out_lean=dp_lean,
            out_fir=dp_fir,
            out_receipt=dp_rcpt,
        )
        assert dp_res["certified"] == 0 and dp_res["verdict"] == "VETOED"
        assert len(uncovered_dp) == 0, f"Expected 0 uncovered spans, got {uncovered_dp}"
        assert len(dp_res["fidelity_mismatches"]) >= 2, dp_res["fidelity_mismatches"]
        assert dp_res["coverage_gaps"] == len(dp_res["fidelity_mismatches"])
        assert (dp_res["viol_flags"] & 0x800) == 0x800
        dp_rcpt_txt = dp_rcpt.read_text(encoding="utf-8")
        assert "FACT_FIDELITY_MISMATCH" in dp_rcpt_txt and "money_downplay" in dp_rcpt_txt
        lean_jobs.append((idx, "intake_zh_downplay_proposer", dp_lean, tmp_dir))

        # Verify 2-repo portability (gracefully skips Cognitive Opacity when INFO binary is absent)
        no_info_res, _ = run_intake_audit(
            CONTRACTS_DIR / "elder_policy_fair_raw_zh.txt",
            CONTRACTS_DIR / "elder_policy_proposer_fair.json",
            sound_bin=SOUND_BIN,
            info_bin=Path("/nonexistent/info_bin"),
        )
        assert no_info_res["cognitive_opacity"] is None

        opacity_str = "info_absent=skip"
        if DEFAULT_INFO_BIN.is_file():
            assert fair_in_res["cognitive_opacity"] is not None
            assert dp_res["cognitive_opacity"] is not None
            assert "Cognitive Opacity Audit (INFO MDL Bridge)" in dp_rcpt_txt
            opacity_str = (
                f"opacity(fair NCD={fair_in_res['cognitive_opacity']['ncd']:.3f}, "
                f"pred NCD={dp_res['cognitive_opacity']['ncd']:.3f})"
            )

        print(
            f"  {'raw_zh_intake_gates':<28} | omit: VETOED (gaps={omit_res['coverage_gaps']}) | "
            f"downplay: VETOED (uncovered=0, fidelity={len(dp_res['fidelity_mismatches'])}, viol=0x{dp_res['viol_flags']:03x}) | "
            f"fair: CERTIFIED ({opacity_str})"
        )

        with concurrent.futures.ThreadPoolExecutor(max_workers=len(lean_jobs)) as ex:
            futs = [ex.submit(_replay_contract_lean, j_idx, lbl, lp, td_p) for (j_idx, lbl, lp, td_p) in lean_jobs]
            verified = [f.result() for f in futs]

        total_thms = sum(cnt for _, cnt in verified)
        print(
            f"  [PASS] 5/5 predatory + omitting + downplaying VETOED, 5/5 fair + complete intake CERTIFIED | "
            f"{len(lean_jobs)}/{len(lean_jobs)} live Lean certs ({total_thms} theorems) leanchecker PASS."
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
