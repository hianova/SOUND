#!/usr/bin/env python3
"""
SOUND v3.0.0 — Pure-Theory Experiment 4: Formal Linguistics Projection Atlas
=============================================================================
Exhaustively computes:
  1. All 4^4 = 256 unary operators B4 -> B4 classified by involution, t-antitonicity,
     k-monotonicity, and unique classical Bilattice negation.
  2. All 16 Strawson Presupposition Hole pairs (quantifying bivalent Truth-Gap collapse)
     and all 256 conditional pairs verifying Karttunen/Heim Filter & Irrealis Plug.
  3. Vendler (+/-Dynamic x +/-Telic) aspectual lattice and 5-Force Conservation laws.

Cross-checked against `./build/sound classify-projection`, `hunt aspect-firewall`,
and `hunt presup-filter`.
"""

from __future__ import annotations

import itertools
import json
import subprocess
import tempfile
from pathlib import Path
from typing import Dict


ROOT = Path(__file__).resolve().parent.parent
SOUND_BIN = ROOT / "build" / "sound"

B4_NAMES = {0: "Bot(00)", 1: "F(01)", 2: "T(10)", 3: "Both(11)"}


def b4_neg(x: int) -> int:
    return ((x & 2) >> 1) | ((x & 1) << 1)


def b4_and_t(x: int, y: int) -> int:
    return ((x & y) & 2) | ((x | y) & 1)


def b4_or_t(x: int, y: int) -> int:
    return ((x | y) & 2) | ((x & y) & 1)


def b4_le_t(x: int, y: int) -> bool:
    return ((x & 2) == 0 or (y & 2) != 0) and ((y & 1) == 0 or (x & 1) != 0)


def b4_le_k(x: int, y: int) -> bool:
    return (x & (~y & 3)) == 0


def eval_presup(a: int, p: int) -> int:
    return a if p == 2 else 0


def proj_neg(a: int, p: int) -> tuple[int, int]:
    return (b4_neg(a), p)


def proj_irrealis(_a: int, _p: int) -> tuple[int, int]:
    return (0, 2)


def proj_cond(a1: int, p1: int, a2: int, p2: int) -> tuple[int, int]:
    na = b4_or_t(b4_neg(a1), a2)
    np = b4_and_t(p1, p2) if a1 == 2 else p1
    return na, np


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

    inv_c = 0
    t_anti_c = 0
    k_mono_c = 0
    cls_neg_c = 0
    for code in range(256):
        f = [(code >> (2 * i)) & 3 for i in range(4)]
        inv = all(f[f[x]] == x for x in range(4))
        ta = all((not b4_le_t(x, y)) or b4_le_t(f[y], f[x]) for x in range(4) for y in range(4))
        km = all((not b4_le_k(x, y)) or b4_le_k(f[x], f[y]) for x in range(4) for y in range(4))
        if inv:
            inv_c += 1
        if ta:
            t_anti_c += 1
        if km:
            k_mono_c += 1
        if inv and ta and km and f[2] == 1 and f[1] == 2:
            cls_neg_c += 1

    # Presupposition taxonomy over all 16 PresupExpr pairs (a, p) in B4 x B4
    hole_pairs = 0
    gap_pairs = 0
    for a, p in itertools.product(range(4), repeat=2):
        na, np = proj_neg(a, p)
        if np == p and proj_neg(na, np) == (a, p):
            hole_pairs += 1
        if p != 2:
            if eval_presup(a, p) == 0 and eval_presup(na, np) == 0:
                gap_pairs += 1
        pa, pp = proj_irrealis(a, p)
        assert pp == 2 and eval_presup(pa, pp) == 0

    # Conditional filter over all 256 (A, B) pairs
    cond_pairs = 0
    filtered_false = 0
    projected_true = 0
    poisoned_ante = 0
    for a1, p1, a2, p2 in itertools.product(range(4), repeat=4):
        cond_pairs += 1
        _, cp = proj_cond(a1, p1, a2, p2)
        if p1 == 2 and a1 == 1:
            assert cp == 2
            filtered_false += 1
        elif p1 == 2 and a1 == 2:
            assert cp == p2
            projected_true += 1
        elif p1 != 2:
            assert cp != 2
            poisoned_ante += 1

    with tempfile.TemporaryDirectory(prefix="snd_exp_ling_") as td:
        tdir = Path(td)
        cpj = run_cli_json(["classify-projection"], tdir / "proj.json")
        afj = run_cli_json(["hunt", "aspect-firewall", "--seed", "0x42"], tdir / "aspect.json")
        pfj = run_cli_json(["hunt", "presup-filter", "--seed", "0x55"], tdir / "presup.json")

    assert cpj["total_unary_maps"] == 256
    assert cpj["involutive_maps"] == inv_c
    assert cpj["t_antitone_maps"] == t_anti_c
    assert cpj["k_monotone_maps"] == k_mono_c
    assert cpj["classical_neg_maps"] == cls_neg_c == 1
    assert cpj["hole_presup_pairs"] == hole_pairs == 16
    assert cpj["strawson_gap_pairs"] == gap_pairs == 12
    assert cpj["cond_filter_ok"] == 1 and cpj["plug_irrealis_ok"] == 1 and cpj["force_invariants_ok"] == 1

    assert afj["aspect_bijection_ok"] == 1 and afj["telic_blocked_mask"] == 3
    assert afj["fir_eval_ok"] is True
    assert pfj["eval_fail_b"] == 0 and pfj["eval_neg_fail_b"] == 0
    assert pfj["cond_false_presup"] == 2 and pfj["cond_true_presup"] == 0
    assert pfj["force_hume_ok"] == 1 and pfj["force_query_ok"] == 1
    assert pfj["force_irrealis_ok"] == 1 and pfj["force_directive_ok"] == 1

    print("=== SOUND v3.0.0 Experiment 4: Formal Linguistics Projection Atlas ===")
    print(f"  [256 Unary B4 Maps] total=256, involutive={inv_c}, t-antitone={t_anti_c}, k-monotone={k_mono_c}, classical-neg={cls_neg_c}")
    print(f"  [Presupposition Hole/Plug] hole_pairs={hole_pairs}/16, Strawson truth-gap pairs={gap_pairs}/12, Irrealis plug=16/16")
    print(f"  [Conditional Filter (256 pairs)] false-ante filtered={filtered_false}, true-ante projected={projected_true}, poisoned={poisoned_ante}")
    print(f"  [Vendler Aspect Firewall] bijection_ok={afj['aspect_bijection_ok']}, atelic_blocked_mask=0x{afj['telic_blocked_mask']:x}")
    print("[Experiment 4 PASS] Exhaustive B4, Presupposition, Aspect & Force laws match Ring-0 C17.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
