#!/usr/bin/env python3
"""
SOUND v3.0.0 — Pure-Theory Experiment 2: Discrete Cech Sheaf Cohomology Atlas
=============================================================================
Verifies the topological law |Z^1| / |B^1| = |H^1(G, G_coeff)| = |G_coeff|^{|E|-|V|+1}
for connected simple graphs P_3 (beta_1=0), C_3 (beta_1=1), C_4 (beta_1=1), K_4 (beta_1=3)
over finite gauge groups Z_2, Z_4, and V_4 = Z_2 x Z_2, computes exact gauge orbits
on C_3(Z_4), and cross-checks against Ring-0 `./build/sound classify-sheaf`.
"""

from __future__ import annotations

import itertools
import json
import subprocess
import tempfile
from pathlib import Path
from typing import Dict, List, Tuple


ROOT = Path(__file__).resolve().parent.parent
SOUND_BIN = ROOT / "build" / "sound"


def add_z2(a: int, b: int) -> int:
    return (a + b) & 1


def sub_z2(a: int, b: int) -> int:
    return (a - b) & 1


def add_z4(a: int, b: int) -> int:
    return (a + b) & 3


def sub_z4(a: int, b: int) -> int:
    return (a - b) & 3


def add_v4(a: int, b: int) -> int:
    return a ^ b


def sub_v4(a: int, b: int) -> int:
    return a ^ b


def compute_coboundary_image_size(
    num_v: int,
    edges: List[Tuple[int, int]],
    order: int,
    sub_fn,
) -> Tuple[int, int, int]:
    """Enumerate all 0-cochains phi: V -> G to find |B^1| = |im d^0|, |Z^1| = |G|^|E|, |H^1|."""
    b1_set = set()
    for phi in itertools.product(range(order), repeat=num_v):
        cob = tuple(sub_fn(phi[v], phi[u]) for u, v in edges)
        b1_set.add(cob)
    z1_size = order ** len(edges)
    b1_size = len(b1_set)
    assert z1_size % b1_size == 0
    h1_size = z1_size // b1_size
    return z1_size, b1_size, h1_size


def main() -> int:
    if not SOUND_BIN.is_file():
        raise FileNotFoundError(f"Missing {SOUND_BIN}; run `make all` first")

    with tempfile.TemporaryDirectory(prefix="snd_exp_sheaf_") as td:
        out_json = Path(td) / "sheaf.json"
        res = subprocess.run(
            [str(SOUND_BIN), "classify-sheaf", "--out-json", str(out_json)],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            check=False,
        )
        assert res.returncode == 0, res.stderr
        c17 = json.loads(out_json.read_text(encoding="utf-8"))

    graphs: Dict[str, Tuple[int, List[Tuple[int, int]]]] = {
        "P3 (chain)": (3, [(0, 1), (1, 2)]),
        "C3 (tri)  ": (3, [(0, 1), (1, 2), (2, 0)]),
        "C4 (sq)   ": (4, [(0, 1), (1, 2), (2, 3), (3, 0)]),
        "K4 (comp) ": (4, [(0, 1), (0, 2), (0, 3), (1, 2), (1, 3), (2, 3)]),
    }
    groups = [
        ("Z2", 2, sub_z2),
        ("Z4", 4, sub_z4),
        ("V4", 4, sub_v4),
    ]

    print("=== SOUND v3.0.0 Experiment 2: Discrete Cech Cohomology Atlas ===")
    print("  Graph      | |V| |E| beta1 | Group |  |Z^1| |  |B^1| | |H^1| | Obstructed")
    print("  -----------+-----+---+-------+-------+--------+--------+-------+-----------")

    recorded: Dict[Tuple[str, str], Tuple[int, int, int]] = {}
    for gname, (nv, edges) in graphs.items():
        beta1 = len(edges) - nv + 1
        for kname, order, sub_fn in groups:
            z1, b1, h1 = compute_coboundary_image_size(nv, edges, order, sub_fn)
            assert h1 == order ** beta1, f"Topological mismatch on {gname}/{kname}: {h1} != {order**beta1}"
            obstructed = z1 - b1
            recorded[(gname.strip(), kname)] = (z1, b1, h1)
            print(
                f"  {gname} | {nv:3d} |{len(edges):2d} | {beta1:5d} | {kname:5s} | "
                f"{z1:6d} | {b1:6d} | {h1:5d} | {obstructed:10d}"
            )

    # Cross-check against C17 classify-sheaf
    c3_z4_z1, c3_z4_b1, _ = recorded[("C3 (tri)", "Z4")]
    c4_z4_z1, c4_z4_b1, _ = recorded[("C4 (sq)", "Z4")]
    p3_z4_z1, p3_z4_b1, _ = recorded[("P3 (chain)", "Z4")]
    c3_v4_z1, c3_v4_b1, _ = recorded[("C3 (tri)", "V4")]

    assert c17["c3_z4_total"] == c3_z4_z1 and c17["c3_z4_coboundary"] == c3_z4_b1
    assert c17["c3_z4_obstructed"] == c3_z4_z1 - c3_z4_b1
    assert c17["c4_z4_total"] == c4_z4_z1 and c17["c4_z4_coboundary"] == c4_z4_b1
    assert c17["c4_z4_obstructed"] == c4_z4_z1 - c4_z4_b1
    assert c17["p3_z4_total"] == p3_z4_z1 and c17["p3_z4_coboundary"] == p3_z4_b1
    assert c17["c3_v4_total"] == c3_v4_z1 and c17["c3_v4_coboundary"] == c3_v4_b1
    assert c17["gauge_invariance_ok"] == 1

    # Verify explicit C3(Z4) gauge-orbit holonomy decomposition
    hol_counts = {0: 0, 1: 0, 2: 0, 3: 0}
    for g01, g12, g20 in itertools.product(range(4), repeat=3):
        h = add_z4(add_z4(g01, g12), g20)
        hol_counts[h] += 1
        # Check gauge invariance across all 64 gauge shifts
        for f0, f1, f2 in itertools.product(range(4), repeat=3):
            sg01 = (g01 + f1 - f0) & 3
            sg12 = (g12 + f2 - f1) & 3
            sg20 = (g20 + f0 - f2) & 3
            assert ((sg01 + sg12 + sg20) & 3) == h
    assert all(hol_counts[k] == 16 for k in range(4))
    print(f"\n[C3(Z4) Holonomy Gauge Orbits] 0->{hol_counts[0]}, 1->{hol_counts[1]}, 2->{hol_counts[2]}, 3->{hol_counts[3]} (gauge invariance exhaustive over 64^2 pairs)")
    print("[Experiment 2 PASS] Exact Cech cohomology |H^1| = |G|^beta_1 & Ring-0 C17 parity verified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
