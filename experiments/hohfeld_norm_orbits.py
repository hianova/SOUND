#!/usr/bin/env python3
"""
SOUND v3.0.0 — Pure-Theory Experiment 3: Hohfeldian V4 Orbits & Normative Algebra
==================================================================================
Verifies:
  1. Complete 8-position Hohfeld V4 = {id, Correlative, Opposite, Dual} group laws
     and exact 2-orbit partition (4 first-order + 4 second-order positions).
  2. Exhaustive 82,944 3-slot transition safety checks (0 coherence violations,
     0 immunity leaks, 0 disability leaks) and BFS reachability lattice |R| = 3^(3-|I|).
  3. Chisholm Contrary-to-Duty comparison (Classical SDL explosion vs SOUND
     stratified consistency) and Lex Specialis vs KanLock priority tie analysis,
     cross-checked against `./build/sound classify-hohfeld`, `hunt chisholm`, `norm-trap`.
"""

from __future__ import annotations

import json
import subprocess
import tempfile
from collections import deque
from pathlib import Path
from typing import Dict


ROOT = Path(__file__).resolve().parent.parent
SOUND_BIN = ROOT / "build" / "sound"

POSITIONS = (
    "Claim",
    "Privilege",
    "Duty",
    "NoRight",
    "Power",
    "Immunity",
    "Liability",
    "Disability",
)

CORRELATIVE = {
    "Claim": "Duty",
    "Duty": "Claim",
    "Privilege": "NoRight",
    "NoRight": "Privilege",
    "Power": "Liability",
    "Liability": "Power",
    "Immunity": "Disability",
    "Disability": "Immunity",
}

OPPOSITE = {
    "Claim": "NoRight",
    "NoRight": "Claim",
    "Privilege": "Duty",
    "Duty": "Privilege",
    "Power": "Disability",
    "Disability": "Power",
    "Immunity": "Liability",
    "Liability": "Immunity",
}


def dual(p: str) -> str:
    return CORRELATIVE[OPPOSITE[p]]


def verify_v4_group() -> int:
    for p in POSITIONS:
        assert CORRELATIVE[CORRELATIVE[p]] == p
        assert OPPOSITE[OPPOSITE[p]] == p
        assert dual(dual(p)) == p
        assert CORRELATIVE[OPPOSITE[p]] == OPPOSITE[CORRELATIVE[p]]
        assert len({p, CORRELATIVE[p], OPPOSITE[p], dual(p)}) == 4

    orbits = set()
    for p in POSITIONS:
        orbits.add(frozenset({p, CORRELATIVE[p], OPPOSITE[p], dual(p)}))
    assert len(orbits) == 2
    return len(orbits)


def bfs_reachable(pw: int, imm: int) -> int:
    vis = {(0, 0)}
    dq = deque([(0, 0)])
    while dq:
        cp, cn = dq.popleft()
        for slot in range(3):
            bit = 1 << slot
            if (imm & bit) != 0 or (pw & bit) == 0:
                continue
            for act in range(16):
                dp = (cp | bit) if (act & 1) else cp
                dp = (dp & ~bit) if (act & 2) else dp
                dn = (cn | bit) if (act & 4) else cn
                dn = (dn & ~bit) if (act & 8) else dn
                if (dp & dn) == 0 and (dp, dn) not in vis:
                    vis.add((dp, dn))
                    dq.append((dp, dn))
    return len(vis)


def run_cli_json(args: list[str], out_path: Path) -> Dict:
    res = subprocess.run(
        [str(SOUND_BIN), *args, "--out-json", str(out_path)],
        cwd=str(ROOT),
        capture_output=True,
        text=True,
        check=False,
    )
    assert res.returncode == 0, res.stderr
    return json.loads(out_path.read_text(encoding="utf-8"))


def main() -> int:
    if not SOUND_BIN.is_file():
        raise FileNotFoundError(f"Missing {SOUND_BIN}; run `make all` first")

    num_orbits = verify_v4_group()

    trans = 0
    coh_viol = 0
    imm_leak = 0
    dis_leak = 0
    for pos in range(8):
        for neg in range(8):
            if pos & neg:
                continue
            for pw in range(8):
                for imm in range(8):
                    for slot in range(3):
                        bit = 1 << slot
                        for act in range(16):
                            trans += 1
                            gp = bool(act & 1)
                            rp = bool(act & 2)
                            gn = bool(act & 4)
                            rn = bool(act & 8)
                            if (imm & bit) != 0 or (pw & bit) == 0:
                                np, nn = pos, neg
                            else:
                                dp = (pos | bit) if gp else pos
                                dp = (dp & ~bit) if rp else dp
                                dn = (neg | bit) if gn else neg
                                dn = (dn & ~bit) if rn else dn
                                if dp & dn:
                                    np, nn = pos, neg
                                else:
                                    np, nn = dp, dn
                            if np & nn:
                                coh_viol += 1
                            if (np & imm) != (pos & imm) or (nn & imm) != (neg & imm):
                                imm_leak += 1
                            dis_m = (~pw) & 7
                            if (np & dis_m) != (pos & dis_m) or (nn & dis_m) != (neg & dis_m):
                                dis_leak += 1

    reach_by_imm = {
        0: bfs_reachable(7, 0),
        1: bfs_reachable(7, 1),
        2: bfs_reachable(7, 3),
        3: bfs_reachable(7, 7),
    }
    for k, v in reach_by_imm.items():
        assert v == 3 ** (3 - k), f"Expected 3^(3-{k})={3**(3-k)}, got {v}"

    with tempfile.TemporaryDirectory(prefix="snd_exp_hoh_") as td:
        tdir = Path(td)
        ch = run_cli_json(["classify-hohfeld"], tdir / "hohfeld.json")
        chi = run_cli_json(["hunt", "chisholm"], tdir / "chisholm.json")
        nt = run_cli_json(["hunt", "norm-trap", "--seed", "0x77"], tdir / "norm_trap.json")

    assert ch["v4_group_ok"] == 1 and ch["v4_orbits"] == num_orbits == 2
    assert ch["transitions_checked"] == trans == 82944
    assert ch["coherence_violations"] == coh_viol == 0
    assert ch["immunity_leaks"] == imm_leak == 0
    assert ch["disability_leaks"] == dis_leak == 0
    assert ch["reachable_imm0"] == reach_by_imm[0] == 27
    assert ch["reachable_imm1"] == reach_by_imm[1] == 9
    assert ch["reachable_imm2"] == reach_by_imm[2] == 3
    assert ch["reachable_imm3"] == reach_by_imm[3] == 1

    assert chi["established_mask"] == 0xC and chi["defeated_mask"] == 0x3 and chi["conflict_mask"] == 0
    assert chi["fir_eval_ok"] is True
    assert nt["freeze_ok"] == 1 and nt["energy_keep_a"] == nt["energy_keep_b"] < nt["energy_both"]
    assert nt["kanlock_win_energy"] < nt["kanlock_both_energy"]

    print("=== SOUND v3.0.0 Experiment 3: Hohfeldian V4 Orbits & Normative Algebra ===")
    print(f"  [V4 Cube] 8 positions -> {num_orbits} faithful 4-element V4 orbits (Klein symmetry PASS)")
    print(f"  [3-Slot Safety] transitions={trans}, coh_viol={coh_viol}, imm_leak={imm_leak}, dis_leak={dis_leak}")
    print("  [Kinetic-Freeze Lattice] |I|=0 -> 27, |I|=1 -> 9, |I|=2 -> 3, |I|=3 -> 1 (exact 3^(3-|I|))")
    print(f"  [Chisholm Stratified CTD] est=0x{chi['established_mask']:x}, def=0x{chi['defeated_mask']:x}, conf=0x{chi['conflict_mask']:x} (0 explosion)")
    print(f"  [Lex Specialis vs KanLock] symmetric tie E_A={nt['energy_keep_a']} == E_B={nt['energy_keep_b']}; KanLock E={nt['kanlock_win_energy']}")
    print("[Experiment 3 PASS] All Hohfeldian V4, Kinetic-Freeze, and Chisholm invariants match Ring-0 C17.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
