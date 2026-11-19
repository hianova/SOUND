#!/usr/bin/env python3
"""Standalone pure-stdlib SOUND gate CLI for Google ADK / Kaggle Docker sandboxes.

Contains zero third-party or repository-local imports so it executes inside an
isolated container sharing only `/workspace`.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Set, Tuple

SOUND_MAX_NODES = 64
SOUND_MAX_CLAUSES = 8
NODE_FLAG_CONSTITUTIVE = 1

SORT_MAP: Dict[str, int] = {"factual": 0, "normative": 1, "institutional": 2}
EVID_MAP: Dict[str, int] = {
    "direct": 0,
    "sensory": 1,
    "enacted_norm": 2,
    "inferential": 3,
    "reportative": 4,
    "assumed": 5,
}
ASPECT_MAP: Dict[str, int] = {"state": 0, "activity": 1, "accomplishment": 2, "achievement": 3}
STATUS_CODE_MAP: Dict[int, str] = {
    0: "established",
    1: "defeated",
    2: "conflictUndecided",
    3: "penumbraOpen",
}
REASON_FLAGS: Dict[int, str] = {
    0x01: "PENUMBRA",
    0x02: "REVOKED",
    0x04: "EVIDENTIAL_LEAF",
    0x08: "HUME_IS_TO_OUGHT",
    0x10: "HUME_OUGHT_TO_IS",
    0x20: "UNVERIFIED_PRESUP",
    0x40: "ATTACKED",
    0x80: "NO_ACTIVE_CLAUSE",
}


@dataclass(frozen=True)
class PortableNode:
    label: str
    sort: str
    evid: str
    aspect: str = "state"
    constitutive: bool = False
    revoked: bool = False
    penumbra: bool = False
    presup_mask: int = 0
    attack_mask: int = 0
    clauses: List[int] = field(default_factory=list)


def decode_reasons(reasons: int) -> List[str]:
    return [name for bit, name in sorted(REASON_FLAGS.items()) if reasons & bit]


def _sanitize_label(raw: str, max_len: int = 24) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_]", "_", raw.strip())
    return (cleaned or "node")[:max_len]


def _clause_valid(
    nd: PortableNode, clause_mask: int, node_mask: int, factual_mask: int
) -> Tuple[bool, int]:
    if clause_mask != (clause_mask & node_mask):
        return False, 0
    evid_code = EVID_MAP[nd.evid]
    sort_code = SORT_MAP[nd.sort]
    if evid_code >= EVID_MAP["inferential"] and clause_mask == 0:
        return False, 0x04
    if sort_code == SORT_MAP["factual"]:
        if (clause_mask & ~factual_mask) != 0:
            return False, 0x10
        return True, 0
    if (
        evid_code == EVID_MAP["enacted_norm"]
        or nd.constitutive
        or (clause_mask & (node_mask & ~factual_mask)) != 0
    ):
        return True, 0
    return False, 0x08


def _node_fires(
    u: int,
    nd: PortableNode,
    blocked_by: int,
    active_set: int,
    node_mask: int,
    factual_mask: int,
    revoked_mask: int,
    penumbra_mask: int,
) -> bool:
    ub = 1 << u
    if ((revoked_mask | penumbra_mask) & ub) != 0:
        return False
    if (nd.attack_mask & blocked_by) != 0:
        return False
    if (nd.presup_mask & active_set) != nd.presup_mask:
        return False
    for cm_raw in nd.clauses[:SOUND_MAX_CLAUSES]:
        cm = cm_raw & node_mask
        ok, _ = _clause_valid(nd, cm, node_mask, factual_mask)
        if ok and (cm & active_set) == cm:
            return True
    return False


def _gamma(
    nodes: Sequence[PortableNode],
    blocked_by: int,
    node_mask: int,
    factual_mask: int,
    revoked_mask: int,
    penumbra_mask: int,
) -> int:
    n = min(len(nodes), SOUND_MAX_NODES)
    cur = 0
    for _ in range(n):
        nxt = 0
        for u in range(n):
            if _node_fires(
                u, nodes[u], blocked_by, cur, node_mask, factual_mask, revoked_mask, penumbra_mask
            ):
                nxt |= 1 << u
        if nxt == cur:
            break
        cur = nxt
    return cur


def eval_nodes_portable(nodes: Sequence[PortableNode]) -> Dict[str, Any]:
    n = len(nodes)
    if n > SOUND_MAX_NODES:
        raise ValueError(f"SoundArena allows at most {SOUND_MAX_NODES} nodes, got {n}")
    node_mask = ((1 << n) - 1) if n < 64 else 0xFFFFFFFFFFFFFFFF
    factual_mask = 0
    revoked_mask = 0
    penumbra_mask = 0
    for u, nd in enumerate(nodes):
        ub = 1 << u
        if SORT_MAP[nd.sort] == SORT_MAP["factual"]:
            factual_mask |= ub
        if nd.revoked:
            revoked_mask |= ub
        if nd.penumbra:
            penumbra_mask |= ub

    cur_i = 0
    for _ in range(n):
        p_tmp = _gamma(nodes, cur_i, node_mask, factual_mask, revoked_mask, penumbra_mask)
        nxt_i = _gamma(nodes, p_tmp, node_mask, factual_mask, revoked_mask, penumbra_mask)
        if nxt_i == cur_i:
            break
        cur_i = nxt_i

    p_star = _gamma(nodes, cur_i, node_mask, factual_mask, revoked_mask, penumbra_mask)
    i_star = _gamma(nodes, p_star, node_mask, factual_mask, revoked_mask, penumbra_mask)
    pen = penumbra_mask & node_mask
    non_pen = node_mask & ~pen
    est = i_star & non_pen
    conf = (p_star & ~i_star) & non_pen
    defm = non_pen & ~p_star

    node_results: List[Dict[str, Any]] = []
    for u, nd in enumerate(nodes):
        ub = 1 << u
        if (pen & ub) != 0:
            st_str, rs_val = STATUS_CODE_MAP[3], 0x01
        elif (est & ub) != 0:
            st_str, rs_val = STATUS_CODE_MAP[0], 0
        elif (conf & ub) != 0:
            st_str, rs_val = STATUS_CODE_MAP[2], 0x40
        else:
            st_str, rs_val = STATUS_CODE_MAP[1], 0
            if (revoked_mask & ub) != 0:
                rs_val |= 0x02
            if (nd.attack_mask & p_star) != 0:
                rs_val |= 0x40
            if (nd.presup_mask & i_star) != nd.presup_mask:
                rs_val |= 0x20
            any_valid = False
            for cm_raw in nd.clauses[:SOUND_MAX_CLAUSES]:
                ok, bit_rs = _clause_valid(nd, cm_raw & node_mask, node_mask, factual_mask)
                rs_val |= bit_rs
                if ok:
                    any_valid = True
            if (not any_valid) or (rs_val == 0):
                rs_val |= 0x80
        node_results.append(
            {
                "index": u,
                "label": nd.label,
                "status": st_str,
                "reasons": rs_val,
                "reason_names": decode_reasons(rs_val),
            }
        )

    return {
        "num_nodes": n,
        "i_star": i_star,
        "p_star": p_star,
        "established_mask": est,
        "defeated_mask": defm,
        "conflict_mask": conf,
        "penumbra_mask": pen,
        "nodes": node_results,
    }


def _default_state_path(workspace: Path) -> Path:
    resolved = str(workspace.resolve()).encode("utf-8")
    digest = hashlib.sha256(resolved).hexdigest()[:12]
    return Path(tempfile.gettempdir()) / f"sound_adk_gate_{digest}.json"


def _load_state(state_path: Path, workspace: Path) -> Dict[str, Any]:
    if state_path.is_file():
        return json.loads(state_path.read_text(encoding="utf-8"))
    return {
        "task_id": workspace.resolve().name,
        "workspace": str(workspace.resolve()),
        "localized_files": [],
        "blast_radius_checked": True,
        "repro_observed": False,
        "regression_run": False,
        "regression_passed": None,
        "ambiguous_spec": False,
        "hume_shortcut_claim": False,
        "hypotheses": [],
        "revoked_nodes": [],
    }


def _save_state(state_path: Path, state: Dict[str, Any]) -> None:
    state_path.parent.mkdir(parents=True, exist_ok=True)
    state_path.write_text(json.dumps(state, indent=2, sort_keys=True), encoding="utf-8")


def inspect_workspace_diff(workspace: Path) -> Dict[str, Any]:
    diff_proc = subprocess.run(
        ["git", "-C", str(workspace), "diff", "--no-ext-diff", "HEAD"],
        capture_output=True,
        text=True,
        check=False,
    )
    status_proc = subprocess.run(
        ["git", "-C", str(workspace), "status", "--porcelain"],
        capture_output=True,
        text=True,
        check=False,
    )
    changed_files: List[str] = []
    seen: Set[str] = set()
    added_lines = 0
    removed_lines = 0
    diff_text = diff_proc.stdout if diff_proc.returncode == 0 else ""

    for line in diff_text.splitlines():
        m = re.match(r"^diff --git a/(.+?) b/(.+)$", line)
        if m:
            f = m.group(2)
            if f not in seen:
                seen.add(f)
                changed_files.append(f)
        elif line.startswith("+") and not line.startswith("+++ "):
            added_lines += 1
        elif line.startswith("-") and not line.startswith("--- "):
            removed_lines += 1

    untracked_files: List[str] = []
    if status_proc.returncode == 0:
        for ln in status_proc.stdout.splitlines():
            if len(ln) < 4:
                continue
            code, rel = ln[:2], ln[3:].strip()
            if " -> " in rel:
                rel = rel.split(" -> ", 1)[1].strip()
            if code == "??":
                untracked_files.append(rel)
            if rel not in seen and (workspace / rel).exists():
                seen.add(rel)
                changed_files.append(rel)

    syntax_errors: List[str] = []
    for rel in changed_files:
        if not rel.endswith(".py"):
            continue
        fpath = workspace / rel
        if not fpath.is_file():
            continue
        try:
            ast.parse(fpath.read_text(encoding="utf-8", errors="replace"), filename=rel)
        except SyntaxError as exc:
            syntax_errors.append(f"{rel}:{exc.lineno or 0}:{exc.offset or 0}: {exc.msg}")

    patch_empty = (added_lines == 0 and removed_lines == 0 and len(untracked_files) == 0)
    return {
        "changed_files": changed_files,
        "untracked_files": untracked_files,
        "added_lines": added_lines,
        "removed_lines": removed_lines,
        "patch_empty": patch_empty,
        "syntax_errors": syntax_errors,
    }


def evaluate_state_dict(state: Dict[str, Any], ws_info: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    if ws_info is not None:
        patch_nonempty = not bool(ws_info["patch_empty"])
        syntax_clean = len(ws_info["syntax_errors"]) == 0
    else:
        patch_nonempty = bool(state.get("patch_nonempty", bool(state.get("localized_files"))))
        syntax_clean = bool(state.get("syntax_clean", True))

    localized_ok = len(state.get("localized_files", [])) > 0
    repro_ok = bool(state.get("repro_observed", False))
    blast_ok = bool(state.get("blast_radius_checked", True))
    reg_run = bool(state.get("regression_run", False))
    reg_penumbra = not reg_run
    reg_passed = reg_run and (state.get("regression_passed") is True)
    ambiguous_spec = bool(state.get("ambiguous_spec", False))
    revoked_set = set(state.get("revoked_nodes", []))

    raw_nodes: List[Dict[str, Any]] = []
    node_index: Dict[str, int] = {}

    def _add(
        label: str,
        *,
        sort: str,
        evid: str,
        constitutive: bool = False,
        penumbra: bool = False,
        presup_mask: int = 0,
        attack_mask: int = 0,
        clauses: Optional[List[int]] = None,
        aliases: Sequence[str] = (),
    ) -> int:
        clean = _sanitize_label(label)
        if clean in node_index:
            clean = _sanitize_label(f"{len(raw_nodes)}_{clean}")
        idx = len(raw_nodes)
        revoked = (clean in revoked_set) or (label in revoked_set) or any(a in revoked_set for a in aliases)
        raw_nodes.append(
            {
                "label": clean,
                "sort": sort,
                "evid": evid,
                "constitutive": constitutive,
                "revoked": revoked,
                "penumbra": penumbra,
                "presup_mask": presup_mask,
                "attack_mask": attack_mask,
                "clauses": list(clauses or []),
            }
        )
        node_index[clean] = idx
        for alias in aliases:
            if alias not in node_index:
                node_index[alias] = idx
        return idx

    idx_patch = _add("fact_patch_nonempty", sort="factual", evid="direct", clauses=[0] if patch_nonempty else [])
    idx_syntax = _add("fact_syntax_ok", sort="factual", evid="direct", clauses=[0] if syntax_clean else [])
    _add("fact_localized", sort="factual", evid="direct" if localized_ok else "assumed", clauses=[0])
    idx_repro = _add("fact_repro_obs", sort="factual", evid="sensory" if repro_ok else "assumed", clauses=[0])
    _add("fact_blast_ok", sort="factual", evid="direct" if blast_ok else "assumed", clauses=[0])
    idx_reg = _add(
        "fact_reg_pass",
        sort="factual",
        evid="direct",
        penumbra=reg_penumbra,
        clauses=[0] if (reg_penumbra or reg_passed) else [],
    )
    _add("norm_spec_clear", sort="normative", evid="enacted_norm", penumbra=ambiguous_spec, clauses=[0])

    hyps = list(state.get("hypotheses", []))[: SOUND_MAX_NODES - 12]
    hyp_indices: Dict[str, int] = {}
    for h_pos, h in enumerate(hyps):
        hid = str(h["id"])
        supported = bool(h.get("supported_by_repro", False)) and not bool(h.get("is_unverified_guess", False))
        lbl = _sanitize_label(hid if hid.startswith("hyp_") else f"hyp_{hid}")
        h_idx = _add(
            lbl,
            sort="factual",
            evid="direct" if supported else "assumed",
            clauses=[1 << idx_repro] if supported else [0],
            aliases=(hid, f"hyp_{h_pos}"),
        )
        hyp_indices[hid] = h_idx

    for h in hyps:
        src_idx = hyp_indices[str(h["id"])]
        src_bit = 1 << src_idx
        for other in h.get("contradicts", []):
            dst_idx = hyp_indices.get(str(other))
            if dst_idx is not None and dst_idx != src_idx:
                dst_bit = 1 << dst_idx
                raw_nodes[src_idx]["attack_mask"] |= dst_bit
                raw_nodes[dst_idx]["attack_mask"] |= src_bit

    idx_hyp_resolved: Optional[int] = None
    if hyps:
        clause_list = [1 << hyp_indices[str(h["id"])] for h in hyps[:SOUND_MAX_CLAUSES]]
        idx_hyp_resolved = _add(
            "norm_hyp_resolved",
            sort="institutional",
            evid="direct",
            constitutive=True,
            clauses=clause_list,
        )

    idx_shortcut: Optional[int] = None
    if state.get("hume_shortcut_claim", False):
        idx_shortcut = _add(
            "norm_shortcut_claim",
            sort="normative",
            evid="inferential",
            constitutive=False,
            clauses=[(1 << idx_patch) | (1 << idx_reg)],
        )

    precond_mask = 0
    for k in range(7):
        precond_mask |= 1 << k
    if idx_hyp_resolved is not None:
        precond_mask |= 1 << idx_hyp_resolved
    if idx_shortcut is not None:
        precond_mask |= 1 << idx_shortcut

    idx_eligible = _add(
        "norm_gate_eligible",
        sort="institutional",
        evid="enacted_norm",
        constitutive=True,
        presup_mask=precond_mask,
        clauses=[0],
    )
    _add(
        "submit_ready",
        sort="normative",
        evid="enacted_norm",
        constitutive=False,
        presup_mask=1 << idx_eligible,
        clauses=[1 << idx_eligible],
    )

    p_nodes = [
        PortableNode(
            label=item["label"],
            sort=item["sort"],
            evid=item["evid"],
            constitutive=item["constitutive"],
            revoked=item["revoked"],
            penumbra=item["penumbra"],
            presup_mask=item["presup_mask"],
            attack_mask=item["attack_mask"],
            clauses=item["clauses"],
        )
        for item in raw_nodes
    ]
    eval_res = eval_nodes_portable(p_nodes)
    submit_idx = node_index["submit_ready"]
    submit_node = eval_res["nodes"][submit_idx]

    blocked_reasons: List[str] = []
    has_penumbra = False
    has_conflict = False
    has_defeated = False
    for nr in eval_res["nodes"]:
        if nr["label"] in ("submit_ready", "norm_gate_eligible", "norm_hyp_resolved"):
            continue
        if nr["status"] == "penumbraOpen":
            has_penumbra = True
            blocked_reasons.append(f"{nr['label']}:penumbraOpen(PENUMBRA)")
        elif nr["status"] == "conflictUndecided":
            has_conflict = True
            blocked_reasons.append(f"{nr['label']}:conflictUndecided(ATTACKED)")
        elif nr["status"] == "defeated":
            has_defeated = True
            r_names = ",".join(nr["reason_names"])
            blocked_reasons.append(f"{nr['label']}:defeated({r_names})")

    if submit_node["status"] == "established":
        action = "ALLOW_SUBMIT"
    elif has_conflict:
        action = "REPAIR_HYPOTHESES"
    elif has_defeated:
        action = "BLOCK_SUBMIT"
    elif ambiguous_spec or eval_res["nodes"][node_index["norm_spec_clear"]]["status"] == "penumbraOpen":
        action = "REQUEST_CLARIFICATION"
    elif has_penumbra:
        action = "DEFER_PENUMBRA"
    else:
        action = "BLOCK_SUBMIT"

    return {
        "task_id": state.get("task_id", "unknown"),
        "allowed": action == "ALLOW_SUBMIT",
        "action": action,
        "submit_ready_status": submit_node["status"],
        "submit_ready_reasons": submit_node["reason_names"],
        "blocked_reasons": blocked_reasons,
        "workspace_diff": ws_info,
        "eval": eval_res,
    }


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Portable SOUND coding-agent submission gate")
    sub = parser.add_subparsers(dest="cmd", required=True)

    def _add_common(p: argparse.ArgumentParser) -> None:
        p.add_argument("--workspace", default=".", help="Workspace repository directory")
        p.add_argument("--state-file", default=None, help="Explicit JSON state file path")

    p_init = sub.add_parser("init", help="Initialize fresh coding session gate state")
    _add_common(p_init)
    p_init.add_argument("--task-id", default=None, help="Task identifier")
    p_init.add_argument("--ambiguous-spec", action="store_true", help="Mark task spec as ambiguous")

    p_loc = sub.add_parser("record-localize", help="Record localized files & blast-radius check")
    _add_common(p_loc)
    p_loc.add_argument("--files", nargs="+", required=True, help="Localized source/test paths")
    p_loc.add_argument("--blast-checked", action="store_true", default=True)
    p_loc.add_argument("--no-blast-check", dest="blast_checked", action="store_false")

    p_rep = sub.add_parser("record-repro", help="Record failing repro observation")
    _add_common(p_rep)
    p_rep.add_argument("--observed", action="store_true", default=True)
    p_rep.add_argument("--not-observed", dest="observed", action="store_false")

    p_hyp = sub.add_parser("record-hyp", help="Register or update a root-cause hypothesis")
    _add_common(p_hyp)
    p_hyp.add_argument("--id", required=True, help="Hypothesis ID")
    p_hyp.add_argument("--description", default="", help="Short summary")
    p_hyp.add_argument("--supported-by-repro", action="store_true")
    p_hyp.add_argument("--unverified-guess", action="store_true")
    p_hyp.add_argument("--contradicts", nargs="*", default=[])
    p_hyp.add_argument("--retract", action="store_true", help="Remove/retract an existing hypothesis")

    p_reg = sub.add_parser("record-regression", help="Record targeted test suite result")
    _add_common(p_reg)
    grp = p_reg.add_mutually_exclusive_group(required=True)
    grp.add_argument("--passed", action="store_true")
    grp.add_argument("--failed", action="store_true")

    p_chk = sub.add_parser("check-submit", help="Inspect workspace diff/syntax and evaluate gate")
    _add_common(p_chk)
    p_chk.add_argument("--strict", action="store_true", help="Exit 2 when gate denies submission")

    p_st = sub.add_parser("status", help="Show current session gate state without checking git diff")
    _add_common(p_st)

    args = parser.parse_args(argv)
    ws = Path(args.workspace).resolve()
    state_path = Path(args.state_file) if args.state_file else _default_state_path(ws)

    if args.cmd == "init":
        state = {
            "task_id": args.task_id or ws.name,
            "workspace": str(ws),
            "localized_files": [],
            "blast_radius_checked": True,
            "repro_observed": False,
            "regression_run": False,
            "regression_passed": None,
            "ambiguous_spec": bool(args.ambiguous_spec),
            "hume_shortcut_claim": False,
            "hypotheses": [],
            "revoked_nodes": [],
        }
        _save_state(state_path, state)
        print(json.dumps({"ok": True, "cmd": "init", "state_file": str(state_path), "state": state}, indent=2))
        return 0

    state = _load_state(state_path, ws)
    if args.cmd == "record-localize":
        existing = list(state.get("localized_files", []))
        for f in args.files:
            if f not in existing:
                existing.append(f)
        state["localized_files"] = existing
        state["blast_radius_checked"] = bool(args.blast_checked)
        _save_state(state_path, state)
        print(json.dumps({"ok": True, "cmd": "record-localize", "localized_files": existing}, indent=2))
        return 0

    if args.cmd == "record-repro":
        state["repro_observed"] = bool(args.observed)
        _save_state(state_path, state)
        print(json.dumps({"ok": True, "cmd": "record-repro", "repro_observed": state["repro_observed"]}, indent=2))
        return 0

    if args.cmd == "record-hyp":
        hyps = [h for h in state.get("hypotheses", []) if h.get("id") != args.id]
        if not args.retract:
            hyps.append(
                {
                    "id": args.id,
                    "description": args.description,
                    "supported_by_repro": bool(args.supported_by_repro),
                    "is_unverified_guess": bool(args.unverified_guess),
                    "contradicts": list(args.contradicts),
                }
            )
        state["hypotheses"] = hyps
        _save_state(state_path, state)
        print(json.dumps({"ok": True, "cmd": "record-hyp", "hypotheses": hyps}, indent=2))
        return 0

    if args.cmd == "record-regression":
        state["regression_run"] = True
        state["regression_passed"] = bool(args.passed)
        _save_state(state_path, state)
        print(
            json.dumps(
                {
                    "ok": True,
                    "cmd": "record-regression",
                    "regression_run": True,
                    "regression_passed": state["regression_passed"],
                },
                indent=2,
            )
        )
        return 0

    if args.cmd == "status":
        verdict = evaluate_state_dict(state, ws_info=None)
        print(json.dumps({"state_file": str(state_path), "state": state, "verdict": verdict}, indent=2))
        return 0

    # check-submit
    ws_info = inspect_workspace_diff(ws)
    verdict = evaluate_state_dict(state, ws_info=ws_info)
    print(json.dumps(verdict, indent=2))
    if args.strict and not verdict["allowed"]:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
