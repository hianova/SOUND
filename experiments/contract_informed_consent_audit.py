#!/usr/bin/env python3
"""
SOUND v3.0.0 — Experiment 5: Asymmetric Contract Informed-Consent & Benefit-Drift Audit
=========================================================================================
Audits 4 real-world paired contract families (8 contracts total) using `./build/sound audit-contract`:
  1. elder_investment_policy_{predatory,fair}.json
  2. joint_guarantor_loan_{predatory,fair}.json
  3. senior_care_facility_{predatory,fair}.json
  4. housing_lease_trap_{predatory,fair}.json

Verifies:
  - 4/4 predatory contracts are fail-closed VETOED with positive Benefit Drift, identified
    predatory clauses, valid FIR1 consent gates, and repaired_certified == 1.
  - 4/4 fair contracts are CERTIFIED with zero violations and benefit_drift <= 0.
  - All 8 emitted Lean 4 certificates compile under `-DwarningAsError=true`, pass
    `leanchecker`, and have zero `sorryAx` and zero `Lean.ofReduceBool`.
"""

from __future__ import annotations

import concurrent.futures
import json
import os
import re
import shutil
import subprocess
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

PAIRS = [
    ("elder_investment_policy", "elder_investment_policy_predatory.json", "elder_investment_policy_fair.json"),
    ("joint_guarantor_loan", "joint_guarantor_loan_predatory.json", "joint_guarantor_loan_fair.json"),
    ("senior_care_facility", "senior_care_facility_predatory.json", "senior_care_facility_fair.json"),
    ("housing_lease_trap", "housing_lease_trap_predatory.json", "housing_lease_trap_fair.json"),
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
    print("[Experiment 5] Asymmetric Contract Informed-Consent & Benefit-Drift Audit")

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
                fir_p = tmp_dir / f"{tag}.fir"
                json_p = tmp_dir / f"{tag}.json"
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
                    ],
                    cwd=str(ROOT),
                    capture_output=True,
                    text=True,
                    check=False,
                )
                assert proc.returncode == 0, f"audit-contract failed on {fname}:\n{proc.stderr}"
                assert fir_p.read_bytes().startswith(b"FIR1"), f"Missing FIR1 header for {fname}"
                data = json.loads(json_p.read_text(encoding="utf-8"))
                assert data["fir_eval_ok"] is True, f"FIR eval failed for {fname}"

                if role == "predatory":
                    assert data["certified"] == 0 and data["verdict"] == "VETOED"
                    assert data["viol_flags"] != 0
                    assert data["num_predatory_clauses"] >= 3
                    assert data["benefit_drift"] > 0
                    assert data["repaired_certified"] == 1
                    pred_res = data
                else:
                    assert data["certified"] == 1 and data["verdict"] == "CERTIFIED"
                    assert data["viol_flags"] == 0
                    assert data["num_predatory_clauses"] == 0
                    assert data["benefit_drift"] <= 0
                    fair_res = data

                lean_jobs.append((idx, tag, lean_p, tmp_dir))
                idx += 1

            print(
                f"  {family:<24} | predatory: VETOED (base={pred_res['base_loss_sum']:>6}, "
                f"worst={pred_res['worst_case_loss']:>6}, drift=+{pred_res['benefit_drift']:<6}, "
                f"viol=0x{pred_res['viol_flags']:02x}, pred={pred_res['num_predatory_clauses']}, "
                f"rep_worst={pred_res['repaired_worst_loss']}) | "
                f"fair: CERTIFIED (worst={fair_res['worst_case_loss']:>5}, "
                f"drift={fair_res['benefit_drift']})"
            )

        with concurrent.futures.ThreadPoolExecutor(max_workers=len(lean_jobs)) as ex:
            futs = [ex.submit(_replay_contract_lean, j_idx, lbl, lp, td_p) for (j_idx, lbl, lp, td_p) in lean_jobs]
            verified = [f.result() for f in futs]

        total_thms = sum(cnt for _, cnt in verified)
        print(
            f"  [PASS] 4/4 predatory VETOED + 4/4 fair CERTIFIED | "
            f"8/8 live Lean certs ({total_thms} theorems) leanchecker PASS, 0 sorryAx, 0 ofReduceBool."
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
