#!/usr/bin/env python3
"""
SOUND v3.0.0 — Independent Exact-Logic & Cohomology Oracle (Gate 5).
======================================================================
Replaces `leanshared` JSI with independent pure-Python verification and live
Lean 4 + `leanchecker` + `0 sorryAx` certificate replay:
  1. 10/10 Golden `SoundArena` (`16,384B`) Lean<->C17 bit-exact mask parity &
     `2,144B` `"FIR1"` byte-for-byte parity + SEARU repair verification.
  2. Independent pure-Python verification of all 4 `classify-*` universes:
     - `classify-bilattice` (Belnap B4 + 64 N=3 + 4,096 N=4 attack graphs)
     - `classify-sheaf` (64 C3 Z4/V4 + 256 C4 + 16 P3 + gauge invariance)
     - `classify-hohfeld` (8-pos V4 group + 82,944 trace transitions + BFS orbits)
     - `classify-projection` (256 unary B4 maps + 16 Strawson pairs + 256 conds)
  3. All 6 `hunt` modes (`paradox-min`, `sheaf-h1`, `chisholm`, `norm-trap`,
     `aspect-firewall`, `presup-filter`): pure-Python witness audit, `"FIR1"` check,
     plus live `lean -DwarningAsError=true` + `leanchecker` + `0 sorryAx` replay.
"""

from __future__ import annotations

import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
from collections import deque
from pathlib import Path
from typing import Dict, List, Optional, Tuple


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
LEAN = _find_elan_tool("lean")
LEANCHECKER = _find_elan_tool("leanchecker")


def run_cmd(cmd: List[str], env: Optional[Dict[str, str]] = None, stdin_text: Optional[str] = None) -> subprocess.CompletedProcess:
    return subprocess.run(
        cmd,
        cwd=str(ROOT),
        env=env,
        input=stdin_text,
        capture_output=True,
        text=True,
        check=False,
    )


# ---------------------------------------------------------------------------
# 1. Golden 16KB SoundArena + 2,144B FIR1 Lean <-> C17 Bit-Exact Parity
# ---------------------------------------------------------------------------

def verify_golden_parity() -> None:
    manifest_path = BUILD / "golden" / "golden_manifest.json"
    if not manifest_path.is_file():
        raise AssertionError(f"Missing golden manifest: {manifest_path}; run Gate 1 first")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    cases = manifest["cases"]
    assert len(cases) == 10, f"Expected 10 golden cases, got {len(cases)}"

    for c in cases:
        snd_path = BUILD / "golden" / f"{c['name']}.snd"
        assert snd_path.is_file() and snd_path.stat().st_size == 16384
        res = run_cmd([str(SOUND_BIN), "verify", str(snd_path), "--json"])
        assert res.returncode == 0, f"sound verify failed for {c['name']}: {res.stderr}"
        got = json.loads(res.stdout)
        for k in (
            "num_nodes",
            "node_mask",
            "i_star",
            "p_star",
            "established_mask",
            "defeated_mask",
            "conflict_mask",
            "penumbra_mask",
        ):
            assert got[k] == c[k], f"{c['name']} mismatch on {k}: C17={got[k]} Lean={c[k]}"

    for name in ("case3_park", "case2_auction_pre"):
        snd_path = BUILD / "golden" / f"{name}.snd"
        lean_fir = BUILD / "golden" / f"{name}.fir"
        cli_fir = BUILD / "golden" / f"{name}_cli.fir"
        res = run_cmd([str(SOUND_BIN), "lift-fir", str(snd_path), str(cli_fir)])
        assert res.returncode == 0, f"lift-fir failed on {name}: {res.stderr}"
        b_lean = lean_fir.read_bytes()
        b_cli = cli_fir.read_bytes()
        assert len(b_lean) == 2144 and b_lean == b_cli, f"FIR1 parity mismatch on {name}"

    rep_res = run_cmd([str(SOUND_BIN), "repair", str(BUILD / "golden" / "nixon_diamond.snd")])
    assert rep_res.returncode == 0 and "conv=1" in rep_res.stdout, rep_res.stdout + rep_res.stderr
    rep_fir = BUILD / "golden" / "nixon_repaired_cli.fir"
    lift_rep = run_cmd([
        str(SOUND_BIN),
        "lift-fir",
        str(BUILD / "golden" / "nixon_diamond.snd"),
        str(rep_fir),
        "--repaired",
    ])
    assert lift_rep.returncode == 0 and rep_fir.read_bytes().startswith(b"FIR1")
    print(f"  [Oracle-Golden] 10/10 16KB SoundArena + 2x 2,144B FIR1 + SEARU repair bit-exact PASS")


# ---------------------------------------------------------------------------
# 2. Pure-Python Reference Algorithms for Classify Universes
# ---------------------------------------------------------------------------

def py_b4_neg(x: int) -> int:
    return ((x & 2) >> 1) | ((x & 1) << 1)


def py_b4_conf(x: int) -> int:
    return py_b4_neg(x) ^ 3


def py_b4_and_t(x: int, y: int) -> int:
    return ((x & y) & 2) | ((x | y) & 1)


def py_b4_or_t(x: int, y: int) -> int:
    return ((x | y) & 2) | ((x & y) & 1)


def py_b4_le_t(x: int, y: int) -> bool:
    return ((x & 2) == 0 or (y & 2) != 0) and ((y & 1) == 0 or (x & 1) != 0)


def py_b4_le_k(x: int, y: int) -> bool:
    return (x & (~y & 3)) == 0


def py_count_classical(n: int, atk_to: List[int]) -> int:
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


def py_eval_bilattice(n: int, atk_to: List[int]) -> Tuple[int, int]:
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


def py_classify_digraph(n: int) -> Dict[str, int]:
    edges = [(src, dst) for dst in range(n) for src in range(n) if src != dst]
    total = 1 << len(edges)
    grounded = 0
    uniq_ng = 0
    multi = 0
    unsolv = 0
    conflict = 0
    nm = (1 << n) - 1
    for g in range(total):
        atk = [0] * n
        for idx, (src, dst) in enumerate(edges):
            if (g >> idx) & 1:
                atk[dst] |= 1 << src
        models = py_count_classical(n, atk)
        ist, pst = py_eval_bilattice(n, atk)
        if models == 1 and ist == pst:
            grounded += 1
        elif models == 1:
            uniq_ng += 1
        elif models >= 2:
            multi += 1
        else:
            unsolv += 1
        if (pst & (~ist)) & nm:
            conflict += 1
    return {
        "total": total,
        "grounded_unique": grounded,
        "unique_nongrounded": uniq_ng,
        "multi_stable": multi,
        "classical_unsolvable": unsolv,
        "conflict_nonempty": conflict,
    }


def verify_classify_bilattice() -> None:
    out_json = BUILD / "certs" / "classify_bilattice.json"
    res = run_cmd([str(SOUND_BIN), "classify-bilattice", "--out-json", str(out_json)])
    assert res.returncode == 0, res.stderr
    got = json.loads(out_json.read_text(encoding="utf-8"))
    for k in ("b4_v4_ok", "b4_demorgan_ok", "b4_interlaced_ok", "b4_distrib_ok"):
        assert got[k] == 1
    py3 = py_classify_digraph(3)
    py4 = py_classify_digraph(4)
    for k, v in py3.items():
        assert got[f"n3_{k}"] == v, f"n3_{k}: C17={got[f'n3_{k}']} Py={v}"
    for k, v in py4.items():
        assert got[f"n4_{k}"] == v, f"n4_{k}: C17={got[f'n4_{k}']} Py={v}"
    print(
        f"  [Oracle-Bilattice] N=3({py3['total']}) N=4({py4['total']}) exact phase atlas PASS"
    )


def verify_classify_sheaf() -> None:
    out_json = BUILD / "certs" / "classify_sheaf.json"
    res = run_cmd([str(SOUND_BIN), "classify-sheaf", "--out-json", str(out_json)])
    assert res.returncode == 0, res.stderr
    got = json.loads(out_json.read_text(encoding="utf-8"))

    c3_z4_cob = sum(
        1
        for a in range(4)
        for b in range(4)
        for c in range(4)
        if ((a + b + c) & 3) == 0
    )
    c4_z4_cob = sum(
        1
        for a in range(4)
        for b in range(4)
        for c in range(4)
        for d in range(4)
        if ((a + b + c + d) & 3) == 0
    )
    p3_z4_cob = sum(
        1
        for g01 in range(4)
        for g12 in range(4)
        if any(
            ((f1 - f0) & 3) == g01 and ((f2 - f1) & 3) == g12
            for f0 in range(4)
            for f1 in range(4)
            for f2 in range(4)
        )
    )
    c3_v4_cob = sum(
        1
        for a in range(4)
        for b in range(4)
        for c in range(4)
        if (a ^ b ^ c) == 0
    )
    assert got["c3_z4_total"] == 64 and got["c3_z4_coboundary"] == c3_z4_cob
    assert got["c3_z4_obstructed"] == 64 - c3_z4_cob
    assert got["c4_z4_total"] == 256 and got["c4_z4_coboundary"] == c4_z4_cob
    assert got["c4_z4_obstructed"] == 256 - c4_z4_cob
    assert got["p3_z4_total"] == 16 and got["p3_z4_coboundary"] == p3_z4_cob
    assert got["c3_v4_total"] == 64 and got["c3_v4_coboundary"] == c3_v4_cob
    assert got["gauge_invariance_ok"] == 1
    print(
        f"  [Oracle-Sheaf] C3(16/64) C4(64/256) P3(16/16) H^1 cohomology exact PASS"
    )


def verify_classify_hohfeld() -> None:
    out_json = BUILD / "certs" / "classify_hohfeld.json"
    res = run_cmd([str(SOUND_BIN), "classify-hohfeld", "--out-json", str(out_json)])
    assert res.returncode == 0, res.stderr
    got = json.loads(out_json.read_text(encoding="utf-8"))

    # Exhaustive 3-slot transition model in Python
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

    def bfs_reach(pw: int, imm: int) -> int:
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

    assert got["v4_group_ok"] == 1 and got["v4_orbits"] == 2
    assert got["transitions_checked"] == trans
    assert got["coherence_violations"] == coh_viol == 0
    assert got["immunity_leaks"] == imm_leak == 0
    assert got["disability_leaks"] == dis_leak == 0
    assert got["reachable_imm0"] == bfs_reach(7, 0) == 27
    assert got["reachable_imm1"] == bfs_reach(7, 1) == 9
    assert got["reachable_imm2"] == bfs_reach(7, 3) == 3
    assert got["reachable_imm3"] == bfs_reach(7, 7) == 1
    print(f"  [Oracle-Hohfeld] V4(8 pos) + {trans} transitions (27/9/3/1 orbits) exact PASS")


def verify_classify_projection() -> None:
    out_json = BUILD / "certs" / "classify_projection.json"
    res = run_cmd([str(SOUND_BIN), "classify-projection", "--out-json", str(out_json)])
    assert res.returncode == 0, res.stderr
    got = json.loads(out_json.read_text(encoding="utf-8"))

    inv_c = 0
    t_anti_c = 0
    k_mono_c = 0
    cls_neg_c = 0
    for code in range(256):
        f = [(code >> (2 * i)) & 3 for i in range(4)]
        inv = all(f[f[x]] == x for x in range(4))
        ta = all((not py_b4_le_t(x, y)) or py_b4_le_t(f[y], f[x]) for x in range(4) for y in range(4))
        km = all((not py_b4_le_k(x, y)) or py_b4_le_k(f[x], f[y]) for x in range(4) for y in range(4))
        if inv:
            inv_c += 1
        if ta:
            t_anti_c += 1
        if km:
            k_mono_c += 1
        if inv and ta and km and f[2] == 1 and f[1] == 2:
            cls_neg_c += 1

    assert got["total_unary_maps"] == 256
    assert got["involutive_maps"] == inv_c
    assert got["t_antitone_maps"] == t_anti_c
    assert got["k_monotone_maps"] == k_mono_c
    assert got["classical_neg_maps"] == cls_neg_c == 1
    assert got["hole_presup_pairs"] == 16
    assert got["strawson_gap_pairs"] == 12
    assert got["cond_filter_ok"] == 1
    assert got["plug_irrealis_ok"] == 1
    assert got["force_invariants_ok"] == 1
    print(
        f"  [Oracle-Linguistics] 256 B4 maps (inv={inv_c}, t_anti={t_anti_c}, k_mono={k_mono_c}, neg={cls_neg_c}) PASS"
    )


# ---------------------------------------------------------------------------
# 3. Live `hunt` + Witness Audit + Parallel `lean` + `leanchecker` Replay
# ---------------------------------------------------------------------------

def _replay_one_lean_cert(mode: str, lean_file: Path, mod_name: str) -> Tuple[str, int]:
    src = lean_file.read_text(encoding="utf-8")
    if re.search(r"\b(sorry|admit)\b", src):
        raise AssertionError(f"{lean_file} contains sorry/admit!")

    env = dict(os.environ)
    env["LEAN_PATH"] = f"{BUILD / 'lean'}:{BUILD / 'certs'}"

    # Determine namespace + theorems in cert file
    ns_m = re.search(r"^\s*namespace\s+(\S+)", src, re.MULTILINE)
    assert ns_m is not None, f"Missing namespace in {lean_file}"
    ns = ns_m.group(1)
    thms = re.findall(r"^\s*theorem\s+(\S+)", src, re.MULTILINE)
    assert len(thms) >= 2, f"Expected >=2 theorems in {lean_file}, got {thms}"

    # Compile to .olean under build/certs/<mod_name>.olean
    # Note: leanchecker looks for module <mod_name> when named <mod_name>.lean -> <mod_name>.olean
    olean = BUILD / "certs" / f"{mod_name}.olean"
    c_res = run_cmd(
        [str(LEAN), "-DwarningAsError=true", "--root", str(BUILD / "certs"), "-o", str(olean), str(lean_file)],
        env=env,
    )
    assert c_res.returncode == 0, f"lean compile failed for {mode}:\n{c_res.stdout}\n{c_res.stderr}"

    chk_res = run_cmd([str(LEANCHECKER), mod_name], env=env)
    assert chk_res.returncode == 0, f"leanchecker failed for {mode}:\n{chk_res.stdout}\n{chk_res.stderr}"

    axiom_script = f"import {mod_name}\n" + "".join(f"#print axioms {ns}.{t}\n" for t in thms)
    ax_res = run_cmd([str(LEAN), "-DwarningAsError=true", "--stdin"], env=env, stdin_text=axiom_script)
    assert ax_res.returncode == 0, f"axiom check failed for {mode}:\n{ax_res.stderr}"
    assert "sorryAx" not in ax_res.stdout, f"sorryAx leaked in {mode}!\n{ax_res.stdout}"
    return mode, len(thms)


def verify_all_hunts_and_certs() -> None:
    cert_dir = BUILD / "certs"
    cert_dir.mkdir(parents=True, exist_ok=True)
    modes = [
        ("paradox-min", "CertParadoxMin"),
        ("sheaf-h1", "CertSheafH1"),
        ("chisholm", "CertChisholm"),
        ("norm-trap", "CertNormTrap"),
        ("aspect-firewall", "CertAspectFirewall"),
        ("presup-filter", "CertPresupFilter"),
    ]
    jobs = []
    for mode, mod_name in modes:
        lean_p = cert_dir / f"{mod_name}.lean"
        fir_p = cert_dir / f"{mode}.fir"
        json_p = cert_dir / f"{mode}.json"
        res = run_cmd([
            str(SOUND_BIN),
            "hunt",
            mode,
            "--out-lean",
            str(lean_p),
            "--out-fir",
            str(fir_p),
            "--out-json",
            str(json_p),
        ])
        assert res.returncode == 0, f"hunt {mode} failed: {res.stderr}"
        fir_bytes = fir_p.read_bytes()
        assert len(fir_bytes) >= 64 and fir_bytes.startswith(b"FIR1"), f"Bad FIR1 on {mode}"
        w = json.loads(json_p.read_text(encoding="utf-8"))
        assert w["fir_eval_ok"] is True

        # Independent witness checks per mode
        if mode == "paradox-min":
            atk = w["atk_to"]
            assert py_count_classical(3, atk) == w["classical_models"] == 0
            ist, pst = py_eval_bilattice(3, atk)
            assert ist == w["i_star"] == 0 and pst == w["p_star"] == 7 and w["conflict_mask"] == 7
        elif mode == "sheaf-h1":
            sg = w["sg"]
            assert ((sg[0] + sg[1] + sg[2]) & 3) == w["gauge_holonomy_z4"] != 0
            assert w["holonomy_z4"] == w["gauge_holonomy_z4"]
            assert w["is_coboundary_z4"] == 0
            t01, t12, t20 = int(w["t01"], 16), int(w["t12"], 16), int(w["t20"], 16)
            assert (t01 ^ t12 ^ t20) == int(w["xor_holonomy"], 16) != 0
        elif mode == "chisholm":
            assert w["established_mask"] == 0xC and w["defeated_mask"] == 0x3
            assert w["conflict_mask"] == 0 and w["penumbra_mask"] == 0
        elif mode == "norm-trap":
            assert w["freeze_ok"] == 1
            assert w["energy_keep_a"] == w["energy_keep_b"] < w["energy_both"]
            assert w["kanlock_win_energy"] < w["kanlock_both_energy"]
        elif mode == "aspect-firewall":
            assert w["aspect_bijection_ok"] == 1 and w["telic_blocked_mask"] == 3
            assert w["j_start"] + 2 <= w["j_end"] and w["i_end"] < w["j_end"]
            assert w["declarative_atelic_fiber"] == 13
        elif mode == "presup-filter":
            assert w["eval_fail_b"] == 0 and w["eval_neg_fail_b"] == 0
            assert w["plug_presup"] == 2 and w["plug_eval"] == 0
            assert w["cond_false_presup"] == 2 and w["cond_true_presup"] == w["b_presup"]
            assert (
                w["force_hume_ok"] == 1
                and w["force_query_ok"] == 1
                and w["force_irrealis_ok"] == 1
                and w["force_directive_ok"] == 1
            )

        jobs.append((mode, lean_p, mod_name))

    # Parallel Lean compile + leanchecker + #print axioms replay
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(jobs)) as ex:
        futs = [ex.submit(_replay_one_lean_cert, m, lp, mn) for (m, lp, mn) in jobs]
        summary = []
        for f in futs:
            m, n_thms = f.result()
            summary.append(f"{m}({n_thms} thms)")
    print(f"  [Oracle-CertReplay] 6/6 live lean + leanchecker + 0 sorryAx PASS: {', '.join(summary)}")


def main() -> int:
    if not SOUND_BIN.is_file():
        print(f"ERROR: missing {SOUND_BIN}; run `make all` first.", file=sys.stderr)
        return 1
    (BUILD / "certs").mkdir(parents=True, exist_ok=True)
    print("[Gate 5] SOUND v3.0.0 Independent Exact-Logic Oracle & Live Lean Certificate Replay")
    verify_golden_parity()
    verify_classify_bilattice()
    verify_classify_sheaf()
    verify_classify_hohfeld()
    verify_classify_projection()
    verify_all_hunts_and_certs()
    print("[Gate 5] PASS (100% Lean<->C17 parity, 4/4 classifiers, 6/6 live Lean+leanchecker certs)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
