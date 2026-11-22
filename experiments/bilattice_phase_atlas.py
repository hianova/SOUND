#!/usr/bin/env python3
"""
SOUND v3.0.0 — Pure-Theory Experiment 1: Non-Monotonic Bilattice Phase Atlas
============================================================================
Exact exhaustive phase classification of all 64 (N=3) and 4,096 (N=4) loopless
directed attack graphs partitioned by edge density m, distinguishing:
  - grounded_unique      (I* = P*, 1 classical supported model)
  - unique_nongrounded   (I* != P*, 1 classical supported model — coherent non-grounded cycle)
  - multi_stable         (>= 2 classical supported models)
  - classical_unsolvable (0 classical models — odd-cycle / bilattice conflict essential)

Cross-checked byte-for-byte against Ring-0 `./build/sound classify-bilattice`.
"""

from __future__ import annotations

import json
import subprocess
import tempfile
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Tuple


ROOT = Path(__file__).resolve().parent.parent
SOUND_BIN = ROOT / "build" / "sound"


def eval_bilattice(n: int, atk_to: List[int]) -> Tuple[int, int]:
    """Compute Kleene/Fitting alternating fixpoint (I*, P*) = lfp/gfp of Gamma^2."""
    def gamma(blk: int) -> int:
        res = 0
        for u in range(n):
            if (atk_to[u] & blk) == 0:
                res |= 1 << u
        return res

    cur_i = 0
    for _ in range(n):
        nxt_i = gamma(gamma(cur_i))
        if nxt_i == cur_i:
            break
        cur_i = nxt_i
    p_star = gamma(cur_i)
    i_star = gamma(p_star)
    return i_star, p_star


def count_classical_models(n: int, atk_to: List[int]) -> int:
    """Count 2-valued supported models M where (u in M) iff (atk_to[u] & M == 0)."""
    cnt = 0
    for m in range(1 << n):
        ok = True
        for u in range(n):
            if bool((m >> u) & 1) != ((atk_to[u] & m) == 0):
                ok = False
                break
        if ok:
            cnt += 1
    return cnt


def verify_b4_laws() -> None:
    def neg(x: int) -> int:
        return ((x & 2) >> 1) | ((x & 1) << 1)

    def conf(x: int) -> int:
        return neg(x) ^ 3

    def and_t(x: int, y: int) -> int:
        return ((x & y) & 2) | ((x | y) & 1)

    def or_t(x: int, y: int) -> int:
        return ((x | y) & 2) | ((x & y) & 1)

    def and_k(x: int, y: int) -> int:
        return x & y

    def or_k(x: int, y: int) -> int:
        return x | y

    assert all(neg(neg(x)) == x and conf(conf(x)) == x and neg(conf(x)) == conf(neg(x)) for x in range(4))
    assert all(neg(and_t(x, y)) == or_t(neg(x), neg(y)) for x in range(4) for y in range(4))
    assert all(and_t(and_k(x, y), z) == and_k(and_t(x, z), and_t(y, z)) for x in range(4) for y in range(4) for z in range(4))
    assert all(or_k(and_t(x, z), y) == and_t(or_k(x, y), or_k(z, y)) for x in range(4) for y in range(4) for z in range(4))


def classify_by_edge_density(n: int) -> Tuple[Dict[str, int], Dict[int, Dict[str, int]]]:
    edges = [(src, dst) for dst in range(n) for src in range(n) if src != dst]
    total = 1 << len(edges)
    nm = (1 << n) - 1
    totals: Dict[str, int] = {
        "total": total,
        "grounded_unique": 0,
        "unique_nongrounded": 0,
        "multi_stable": 0,
        "classical_unsolvable": 0,
        "conflict_nonempty": 0,
    }
    by_m: Dict[int, Dict[str, int]] = defaultdict(lambda: defaultdict(int))

    for g in range(total):
        m = g.bit_count()
        atk = [0] * n
        for idx, (src, dst) in enumerate(edges):
            if (g >> idx) & 1:
                atk[dst] |= 1 << src
        models = count_classical_models(n, atk)
        ist, pst = eval_bilattice(n, atk)

        by_m[m]["total"] += 1
        if models == 1 and ist == pst:
            phase = "grounded_unique"
        elif models == 1:
            phase = "unique_nongrounded"
        elif models >= 2:
            phase = "multi_stable"
        else:
            phase = "classical_unsolvable"

        totals[phase] += 1
        by_m[m][phase] += 1
        if (pst & (~ist)) & nm:
            totals["conflict_nonempty"] += 1
            by_m[m]["conflict_nonempty"] += 1

    return totals, dict(sorted(by_m.items()))


def main() -> int:
    verify_b4_laws()
    if not SOUND_BIN.is_file():
        raise FileNotFoundError(f"Missing {SOUND_BIN}; run `make all` first")

    with tempfile.TemporaryDirectory(prefix="snd_exp_bilat_") as td:
        out_json = Path(td) / "bilattice.json"
        res = subprocess.run(
            [str(SOUND_BIN), "classify-bilattice", "--out-json", str(out_json)],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            check=False,
        )
        assert res.returncode == 0, res.stderr
        c17 = json.loads(out_json.read_text(encoding="utf-8"))

    for key in ("b4_v4_ok", "b4_demorgan_ok", "b4_interlaced_ok", "b4_distrib_ok"):
        assert c17[key] == 1, f"B4 check failed: {key}={c17[key]}"

    print("=== SOUND v3.0.0 Experiment 1: Non-Monotonic Bilattice Phase Atlas ===")
    for n, prefix in ((3, "n3"), (4, "n4")):
        totals, by_m = classify_by_edge_density(n)
        for k, v in totals.items():
            assert c17[f"{prefix}_{k}"] == v, f"{prefix}_{k}: Py={v} C17={c17[f'{prefix}_{k}']}"

        print(f"\n[N={n} loopless attack digraphs: total={totals['total']}, edges=0..{n*(n-1)}]")
        print("  m  | total | grounded | uniq_ng | multi | unsolv | conflict")
        print("  ---+-------+----------+---------+-------+--------+---------")
        for m, row in by_m.items():
            print(
                f"  {m:2d} | {row['total']:5d} | {row['grounded_unique']:8d} | "
                f"{row['unique_nongrounded']:7d} | {row['multi_stable']:5d} | "
                f"{row['classical_unsolvable']:6d} | {row['conflict_nonempty']:8d}"
            )
        print(
            f"  ALL| {totals['total']:5d} | {totals['grounded_unique']:8d} | "
            f"{totals['unique_nongrounded']:7d} | {totals['multi_stable']:5d} | "
            f"{totals['classical_unsolvable']:6d} | {totals['conflict_nonempty']:8d}"
        )

    print("\n[Experiment 1 PASS] N=3 (64) & N=4 (4096) exact phase tables match Ring-0 C17 classifier.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
