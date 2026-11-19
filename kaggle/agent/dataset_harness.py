"""Dataset inspection, synthetic & real-task stress harness, and FFI/CLI/Portable parity check."""

from __future__ import annotations

import argparse
import json
import os
import statistics
import struct
import sys
import time
import zlib
from collections import Counter
from pathlib import Path, PurePosixPath
from typing import Any, Dict, List, Optional, Sequence, Tuple

from modules.common.arena_codec import ArenaEvalResult, SOUND_FIR_BYTES, SOUND_MAX_NODES

from .repo_analyzer import (
    analyze_networkx_graph,
    check_python_syntax,
    inspect_git_workspace,
    load_graph_file,
    parse_unified_diff,
)
from .sound_gate import (
    DEFAULT_SOUND_BIN,
    REPO_ROOT,
    CodingSessionSpec,
    HypothesisSpec,
    SoundCodingGate,
)

DEFAULT_DATA_DIR = REPO_ROOT / "kaggle" / "data"


def discover_data_dir(explicit: Optional[Path] = None) -> Optional[Path]:
    """Locate unpacked Kaggle competition dataset directory if available locally."""
    candidates: List[Path] = []
    if explicit is not None:
        candidates.append(Path(explicit))
    candidates.append(DEFAULT_DATA_DIR)
    downloads = Path.home() / "Downloads"
    try:
        if downloads.is_dir():
            for entry in sorted(downloads.iterdir()):
                if "gemma" in entry.name.lower() and entry.is_dir():
                    candidates.append(entry)
    except OSError:
        pass

    for cand in candidates:
        if cand.is_dir() and (
            (cand / "tasks.jsonl").is_file()
            or (cand / "graphs").is_dir()
            or (cand / "snapshots").is_dir()
        ):
            return cand
    return None


def make_synthetic_tasks() -> List[Dict[str, str]]:
    """Build representative Python SWE-style synthetic tasks matching Kaggle schema."""
    return [
        {
            "instance_id": "synthetic__httpx__timeout_guard",
            "repo": "encode/httpx",
            "problem_statement": "Ensure PoolTimeout message formats float timeout cleanly without AttributeError.",
            "patch": (
                "diff --git a/httpx/_transports/default.py b/httpx/_transports/default.py\n"
                "--- a/httpx/_transports/default.py\n"
                "+++ b/httpx/_transports/default.py\n"
                "@@ -112,6 +112,8 @@ def map_httpcore_exceptions():\n"
                "+        timeout_val = getattr(exc, 'timeout', None)\n"
                "+        detail = f'{timeout_val:.2f}s' if isinstance(timeout_val, (int, float)) else 'unknown'\n"
                "         raise PoolTimeout(detail) from exc\n"
            ),
        },
        {
            "instance_id": "synthetic__rich__wrap_indent",
            "repo": "Textualize/rich",
            "problem_statement": "Fix zero-width cell wrap crash when console width is below prefix width.",
            "patch": (
                "diff --git a/rich/text.py b/rich/text.py\n"
                "--- a/rich/text.py\n"
                "+++ b/rich/text.py\n"
                "@@ -640,4 +640,5 @@ class Text:\n"
                "+        effective_width = max(1, max_width)\n"
                "-        effective_width = max_width\n"
            ),
        },
        {
            "instance_id": "synthetic__fastapi__lifespan_state",
            "repo": "fastapi/fastapi",
            "problem_statement": "Preserve app.state bindings across mounted sub-application lifespan events.",
            "patch": (
                "diff --git a/fastapi/routing.py b/fastapi/routing.py\n"
                "--- a/fastapi/routing.py\n"
                "+++ b/fastapi/routing.py\n"
                "@@ -88,3 +88,5 @@ class Mount:\n"
                "+        if scope.get('app') is not None:\n"
                "+            scope.setdefault('state', scope['app'].state)\n"
            ),
        },
    ]


def make_synthetic_large_graph(num_nodes: int = 1500) -> Dict[str, Any]:
    """Generate a deterministic 1,500-node NetworkX node-link JSON graph for blast-radius capping."""
    nodes: List[Dict[str, Any]] = []
    links: List[Dict[str, str]] = []
    modules = ["httpx/_client.py", "httpx/_transports/default.py", "rich/text.py", "fastapi/routing.py"]
    for i in range(num_nodes):
        mod = modules[i % len(modules)]
        nodes.append(
            {
                "id": f"sym_{i:04d}",
                "file": mod,
                "name": f"fn_{i:04d}",
            }
        )
        if i > 0:
            links.append({"source": f"sym_{i - 1:04d}", "target": f"sym_{i:04d}"})
        if i >= 5:
            links.append({"source": f"sym_{i - 5:04d}", "target": f"sym_{i:04d}"})
    return {"directed": True, "multigraph": False, "nodes": nodes, "links": links}


def eval_signature(res: ArenaEvalResult) -> Tuple[object, ...]:
    """Canonical comparable tuple across FFI, CLI, and portable SOUND evaluations."""
    return (
        res.num_nodes,
        res.i_star,
        res.p_star,
        res.established_mask,
        res.defeated_mask,
        res.conflict_mask,
        res.penumbra_mask,
        tuple((nd.label, nd.status, nd.reasons) for nd in res.nodes),
    )


def assert_three_way_parity(
    gate: SoundCodingGate,
    spec: CodingSessionSpec,
) -> Tuple[ArenaEvalResult, float, float]:
    """Run FFI, CLI, and portable evaluators on `spec`, asserting identical bilattice results."""
    t0 = time.perf_counter()
    dec_ffi = gate.evaluate(spec, backend="ffi")
    ffi_ms = (time.perf_counter() - t0) * 1000.0

    t1 = time.perf_counter()
    dec_cli = gate.evaluate(spec, backend="cli")
    cli_ms = (time.perf_counter() - t1) * 1000.0

    dec_port = gate.evaluate(spec, backend="portable")

    sig_ffi = eval_signature(dec_ffi.eval_result)
    sig_cli = eval_signature(dec_cli.eval_result)
    sig_port = eval_signature(dec_port.eval_result)
    if sig_ffi != sig_cli or sig_ffi != sig_port:
        raise AssertionError(
            f"3-way SOUND parity mismatch on {spec.task_id}: "
            f"ffi={sig_ffi!r} cli={sig_cli!r} portable={sig_port!r}"
        )
    if dec_ffi.action != dec_cli.action or dec_ffi.action != dec_port.action:
        raise AssertionError(f"Action mismatch on {spec.task_id}: {dec_ffi.action} vs {dec_port.action}")
    return dec_ffi.eval_result, ffi_ms, cli_ms


def build_adversarial_scenarios() -> Dict[str, CodingSessionSpec]:
    """Return the 9 canonical adversarial SWE gate stress scenarios."""
    sample_diff = make_synthetic_tasks()[0]["patch"]
    broken_err = check_python_syntax("broken.py", "def bad(:\n    pass\n")
    base_diff = parse_unified_diff(sample_diff)
    broken_diff = parse_unified_diff(
        "diff --git a/broken.py b/broken.py\n--- a/broken.py\n+++ b/broken.py\n@@ -1 +1,2 @@\n+def bad(:\n",
        file_contents={"broken.py": "def bad(:\n    pass\n"},
    )
    assert broken_err is not None

    return {
        "1_submit_without_test": CodingSessionSpec(
            task_id="scen_1_submit_without_test",
            repo="encode/httpx",
            diff_summary=base_diff,
            localized_files=["httpx/_transports/default.py"],
            repro_observed=True,
            regression_run=False,
            regression_passed=None,
            submit_requested=True,
        ),
        "2_hume_shortcut": CodingSessionSpec(
            task_id="scen_2_hume_shortcut",
            repo="encode/httpx",
            diff_summary=base_diff,
            localized_files=["httpx/_transports/default.py"],
            repro_observed=True,
            regression_run=True,
            regression_passed=True,
            hume_shortcut_claim=True,
            submit_requested=True,
        ),
        "3_hallucinated_hypothesis": CodingSessionSpec(
            task_id="scen_3_hallucinated_hyp",
            repo="encode/httpx",
            diff_summary=base_diff,
            localized_files=["httpx/_transports/default.py"],
            repro_observed=True,
            regression_run=True,
            regression_passed=True,
            hypotheses=[
                HypothesisSpec(
                    hyp_id="guess_timeout",
                    description="Guess timeout attribute dropped without reading trace",
                    supported_by_repro=False,
                    is_unverified_guess=True,
                )
            ],
            submit_requested=True,
        ),
        "4_competing_hypotheses": CodingSessionSpec(
            task_id="scen_4_competing_hyps",
            repo="encode/httpx",
            diff_summary=base_diff,
            localized_files=["httpx/_transports/default.py"],
            repro_observed=True,
            regression_run=True,
            regression_passed=True,
            hypotheses=[
                HypothesisSpec(
                    hyp_id="hyp_a",
                    description="Timeout wrapper drops float seconds",
                    contradicts=("hyp_b",),
                    supported_by_repro=True,
                ),
                HypothesisSpec(
                    hyp_id="hyp_b",
                    description="Pool closes connection prematurely",
                    contradicts=("hyp_a",),
                    supported_by_repro=True,
                ),
            ],
            submit_requested=False,
        ),
        "5_valid_patch_passes": CodingSessionSpec(
            task_id="scen_5_valid_patch",
            repo="encode/httpx",
            diff_summary=base_diff,
            localized_files=["httpx/_transports/default.py"],
            repro_observed=True,
            regression_run=True,
            regression_passed=True,
            hypotheses=[
                HypothesisSpec(
                    hyp_id="hyp_timeout_fmt",
                    description="Verified repro points to timeout formatting",
                    supported_by_repro=True,
                )
            ],
            submit_requested=True,
        ),
        "6_syntax_broken": CodingSessionSpec(
            task_id="scen_6_syntax_broken",
            repo="encode/httpx",
            diff_summary=broken_diff,
            localized_files=["broken.py"],
            repro_observed=True,
            regression_run=True,
            regression_passed=False,
            submit_requested=True,
        ),
        "7_ambiguous_spec": CodingSessionSpec(
            task_id="scen_7_ambiguous_spec",
            repo="Textualize/rich",
            diff_summary=base_diff,
            localized_files=["rich/text.py"],
            repro_observed=True,
            regression_run=True,
            regression_passed=True,
            ambiguous_spec=True,
        ),
        "8_searu_immunity_lock": CodingSessionSpec(
            task_id="scen_8_searu_kanlock",
            repo="encode/httpx",
            diff_summary=base_diff,
            localized_files=["httpx/_transports/default.py"],
            repro_observed=True,
            regression_run=True,
            regression_passed=True,
            hypotheses=[
                HypothesisSpec(
                    hyp_id="hyp_a",
                    description="Core KanLock protected hypothesis",
                    contradicts=("hyp_b",),
                    supported_by_repro=True,
                ),
                HypothesisSpec(
                    hyp_id="hyp_b",
                    description="Alternative unpinned hypothesis",
                    contradicts=("hyp_a",),
                    supported_by_repro=True,
                ),
            ],
            immunity_locked_nodes=("hyp_a",),
        ),
        "9_voidable_rescission": CodingSessionSpec(
            task_id="scen_9_voidable_rescission",
            repo="fastapi/fastapi",
            diff_summary=base_diff,
            localized_files=["fastapi/routing.py"],
            repro_observed=True,
            regression_run=True,
            regression_passed=True,
            voidable_nodes=("submit_ready",),
            submit_requested=True,
        ),
    }


def run_stress_suite(
    *,
    data_dir: Optional[Path] = None,
    sound_bin: Path = DEFAULT_SOUND_BIN,
    max_tasks: int = 25,
    synthetic_only: bool = False,
) -> Dict[str, Any]:
    """Execute 9 adversarial scenarios, graph scaling, parity checks, and dataset stress."""
    gate = SoundCodingGate(sound_bin=sound_bin)
    scenarios = build_adversarial_scenarios()
    ffi_timings: List[float] = []
    cli_timings: List[float] = []
    scenario_report: Dict[str, Any] = {}

    for name, spec in scenarios.items():
        eval_res, ffi_ms, cli_ms = assert_three_way_parity(gate, spec)
        ffi_timings.append(ffi_ms)
        cli_timings.append(cli_ms)
        dec = gate.evaluate(spec, backend="ffi")
        item: Dict[str, Any] = {
            "action": dec.action,
            "submit_ready_status": dec.submit_ready_status,
            "submit_ready_reasons": dec.submit_ready_reasons,
            "ffi_ms": round(ffi_ms, 3),
            "cli_ms": round(cli_ms, 3),
        }

        if name in ("4_competing_hypotheses", "8_searu_immunity_lock"):
            _, rep, fir = gate.resolve_hypothesis_conflict(spec)
            item["searu_converged"] = rep.converged
            item["searu_energy"] = rep.energy
            item["searu_retract"] = hex(rep.retract)
            item["searu_pinned"] = hex(rep.pinned)
            item["fir_len"] = len(fir.fir_bytes)
            assert rep.converged and rep.energy == 0
            assert len(fir.fir_bytes) == SOUND_FIR_BYTES and fir.fir_bytes.startswith(b"FIR1")
        elif name == "5_valid_patch_passes":
            _, fir = gate.lift_submit_authority_fir(spec)
            item["fir_len"] = len(fir.fir_bytes)
            assert dec.action == "ALLOW_SUBMIT"
            assert len(fir.fir_bytes) == SOUND_FIR_BYTES and fir.fir_bytes.startswith(b"FIR1")
        elif name == "9_voidable_rescission":
            unlocked = gate.provisional_admit_and_rescind_demo(spec, lock_target=False)
            locked = gate.provisional_admit_and_rescind_demo(spec, lock_target=True)
            item["unlocked_decl_rc"] = unlocked["decl_rc"]
            item["unlocked_resc_rc"] = unlocked["resc_rc"]
            item["locked_resc_rc"] = locked["resc_rc"]
            assert unlocked["decl_rc"] == 0 and unlocked["resc_rc"] == 0
            assert locked["resc_rc"] == 5

        scenario_report[name] = item

    # Large graph blast-radius projection stress (1,500 nodes -> <= 64 nodes)
    large_graph = make_synthetic_large_graph(1500)
    blast = analyze_networkx_graph(large_graph, ["httpx/_transports/default.py"], max_nodes=32)
    assert blast.total_nodes == 1500
    assert len(blast.projected_nodes) <= SOUND_MAX_NODES

    resolved_data = None if synthetic_only else discover_data_dir(data_dir)
    task_results: List[Dict[str, Any]] = []
    data_source = "synthetic"
    tasks_to_run: List[Dict[str, Any]] = []
    has_tasks_jsonl = resolved_data is not None and (resolved_data / "tasks.jsonl").is_file()
    matched_real_task_graphs = 0

    if has_tasks_jsonl:
        assert resolved_data is not None
        data_source = str(resolved_data)
        with (resolved_data / "tasks.jsonl").open("r", encoding="utf-8") as fh:
            for line in fh:
                if line.strip():
                    tasks_to_run.append(json.loads(line))
                    if len(tasks_to_run) >= max_tasks:
                        break
    else:
        tasks_to_run = make_synthetic_tasks()

    for task in tasks_to_run:
        tid = str(task.get("instance_id") or task.get("task_id") or "task")
        repo = str(task.get("repo", "unknown/repo"))
        repo_short = repo.split("/")[-1]
        base_commit = str(task.get("base_commit", ""))
        patch_text = str(task.get("patch", ""))
        diff_sum = parse_unified_diff(patch_text)
        loc_files = diff_sum.files or ["unknown.py"]

        g_report = None
        if resolved_data is not None:
            cand_graphs: List[Path] = []
            if base_commit:
                cand_graphs.append(resolved_data / "graphs" / f"{repo_short}_{base_commit}.json")
            cand_graphs.append(resolved_data / "graphs" / f"{tid}.json")
            for g_path in cand_graphs:
                if g_path.is_file():
                    g_report = analyze_networkx_graph(load_graph_file(g_path), loc_files, max_nodes=32)
                    matched_real_task_graphs += 1
                    break
        if g_report is None:
            g_report = analyze_networkx_graph(large_graph, loc_files, max_nodes=24)

        assert len(g_report.projected_nodes) <= SOUND_MAX_NODES

        good_spec = CodingSessionSpec(
            task_id=tid,
            repo=repo,
            diff_summary=diff_sum,
            localized_files=loc_files,
            repro_observed=True,
            regression_run=True,
            regression_passed=True,
            graph_report=g_report,
            blast_radius_checked=True,
            submit_requested=True,
        )
        _, good_ffi_ms, good_cli_ms = assert_three_way_parity(gate, good_spec)
        ffi_timings.append(good_ffi_ms)
        cli_timings.append(good_cli_ms)
        good_dec, good_fir = gate.lift_submit_authority_fir(good_spec)
        if not diff_sum.patch_empty:
            assert good_dec.action == "ALLOW_SUBMIT"
        assert len(good_fir.fir_bytes) == SOUND_FIR_BYTES

        skip_spec = CodingSessionSpec(
            task_id=f"{tid}_noreg",
            repo=repo,
            diff_summary=diff_sum,
            localized_files=loc_files,
            repro_observed=True,
            regression_run=False,
            regression_passed=None,
            graph_report=g_report,
            blast_radius_checked=True,
            submit_requested=True,
        )
        assert_three_way_parity(gate, skip_spec)
        skip_dec = gate.evaluate(skip_spec, backend="ffi")
        assert skip_dec.action in ("DEFER_PENUMBRA", "BLOCK_SUBMIT")
        assert skip_dec.eval_result.nodes[good_dec.node_index["fact_reg_pass"]].status == "penumbraOpen"

        task_results.append(
            {
                "task_id": tid,
                "repo": repo,
                "diff_files": len(diff_sum.files),
                "graph_total_nodes": g_report.total_nodes,
                "graph_projected_nodes": len(g_report.projected_nodes),
                "good_action": good_dec.action,
                "skip_reg_action": skip_dec.action,
            }
        )

    real_graph_results: List[Dict[str, Any]] = []
    if (not has_tasks_jsonl) and resolved_data is not None and (resolved_data / "graphs").is_dir():
        graph_paths = sorted((resolved_data / "graphs").glob("*.json"))[:max_tasks]
        if graph_paths and data_source == "synthetic":
            data_source = str(resolved_data)
        for gp in graph_paths:
            gdata = load_graph_file(gp)
            # Pick a real target symbol/file from the graph so BFS exercises multi-hop neighborhoods
            seed_queries: List[str] = []
            raw_nodes = gdata.get("nodes")
            if isinstance(raw_nodes, list) and raw_nodes:
                mid = raw_nodes[len(raw_nodes) // 2]
                if isinstance(mid, dict):
                    cand_q = str(mid.get("file") or mid.get("id") or mid.get("name") or "")
                    if cand_q:
                        seed_queries.append(cand_q)
            g_rep = analyze_networkx_graph(gdata, seed_queries, max_nodes=32)
            assert len(g_rep.projected_nodes) <= SOUND_MAX_NODES
            g_spec = CodingSessionSpec(
                task_id=f"rg_{gp.stem[:18]}",
                repo="kaggle/graph",
                diff_summary=parse_unified_diff(make_synthetic_tasks()[0]["patch"]),
                localized_files=seed_queries or ["module.py"],
                repro_observed=True,
                regression_run=True,
                regression_passed=True,
                graph_report=g_rep,
                blast_radius_checked=True,
                submit_requested=True,
            )
            _, r_ffi_ms, r_cli_ms = assert_three_way_parity(gate, g_spec)
            ffi_timings.append(r_ffi_ms)
            cli_timings.append(r_cli_ms)
            real_graph_results.append(
                {
                    "graph": gp.name,
                    "total_nodes": g_rep.total_nodes,
                    "total_edges": g_rep.total_edges,
                    "projected_nodes": len(g_rep.projected_nodes),
                    "truncated": g_rep.truncated,
                }
            )

    ffi_sorted = sorted(ffi_timings)
    p95_idx = min(len(ffi_sorted) - 1, int(0.95 * len(ffi_sorted)))
    real_graph_stats: Dict[str, Any] = {}
    if real_graph_results:
        tot_nodes_list = [int(r["total_nodes"]) for r in real_graph_results]
        proj_list = [int(r["projected_nodes"]) for r in real_graph_results]
        real_graph_stats = {
            "count": len(real_graph_results),
            "min_nodes": min(tot_nodes_list),
            "max_nodes": max(tot_nodes_list),
            "max_projected_nodes": max(proj_list),
            "samples": real_graph_results[:5],
        }
    return {
        "data_source": data_source,
        "parity_checked": True,
        "adversarial_scenarios": scenario_report,
        "large_graph_blast": {
            "total_nodes": blast.total_nodes,
            "projected_nodes": len(blast.projected_nodes),
            "truncated": blast.truncated,
        },
        "tasks_stressed": len(task_results),
        "matched_real_task_graphs": matched_real_task_graphs,
        "task_samples": task_results[:5],
        "real_graphs_stressed": len(real_graph_results),
        "real_graph_stats": real_graph_stats,
        "latency": {
            "ffi_mean_ms": round(statistics.mean(ffi_timings), 3),
            "ffi_p95_ms": round(ffi_sorted[p95_idx], 3),
            "cli_mean_ms": round(statistics.mean(cli_timings), 3),
        },
    }


def inspect_dataset(data_dir: Optional[Path] = None) -> Dict[str, Any]:
    """Inspect local Kaggle competition data files if unpacked, without failing when absent."""
    resolved = discover_data_dir(data_dir)
    if resolved is None:
        return {
            "found": False,
            "expected_default_dir": str(DEFAULT_DATA_DIR),
            "downloads_checked": str(Path.home() / "Downloads"),
            "instructions": (
                "Unpack gemma-4-developer-agent dataset under kaggle/data/ (containing tasks.jsonl, "
                "snapshots/, graphs/, embeddings/) or pass --data-dir PATH."
            ),
        }

    tasks_file = resolved / "tasks.jsonl"
    repo_counts: Counter[str] = Counter()
    task_count = 0
    sample_keys: List[str] = []
    if tasks_file.is_file():
        with tasks_file.open("r", encoding="utf-8") as fh:
            for line in fh:
                if not line.strip():
                    continue
                obj = json.loads(line)
                if not sample_keys:
                    sample_keys = sorted(obj.keys())
                repo_counts[str(obj.get("repo", "unknown"))] += 1
                task_count += 1

    graphs_dir = resolved / "graphs"
    graph_files = sorted(graphs_dir.glob("*.json")) if graphs_dir.is_dir() else []
    graph_node_counts: List[int] = []
    for gf in graph_files[:20]:
        try:
            gdata = load_graph_file(gf)
            graph_node_counts.append(len(gdata.get("nodes", [])))
        except Exception:
            continue

    snap_dir = resolved / "snapshots"
    snap_count = len(list(snap_dir.glob("*.tgz"))) if snap_dir.is_dir() else 0

    return {
        "found": True,
        "data_dir": str(resolved),
        "task_count": task_count,
        "repos": dict(repo_counts),
        "sample_task_keys": sample_keys,
        "snapshot_count": snap_count,
        "graph_file_count": len(graph_files),
        "graph_sample_nodes_min_max": (
            [min(graph_node_counts), max(graph_node_counts)] if graph_node_counts else []
        ),
        "harness_readme_present": (resolved / "HARNESS_README.md").is_file(),
    }


def eval_workspace(
    workspace: Path,
    *,
    repro_observed: bool = False,
    regression_run: bool = False,
    regression_passed: Optional[bool] = None,
    sound_bin: Path = DEFAULT_SOUND_BIN,
) -> Dict[str, Any]:
    """Evaluate a live git repository workspace through `SoundCodingGate`."""
    ws_info = inspect_git_workspace(workspace)
    diff_sum = ws_info["diff_summary"]
    spec = CodingSessionSpec(
        task_id=workspace.resolve().name,
        repo=workspace.resolve().name,
        diff_summary=diff_sum,
        localized_files=diff_sum.files,
        repro_observed=repro_observed,
        regression_run=regression_run,
        regression_passed=regression_passed,
        submit_requested=True,
    )
    gate = SoundCodingGate(sound_bin=sound_bin)
    dec = gate.evaluate(spec, backend="ffi")
    return {
        "workspace": str(workspace.resolve()),
        "action": dec.action,
        "submit_ready_status": dec.submit_ready_status,
        "submit_ready_reasons": dec.submit_ready_reasons,
        "blocked_reasons": dec.blocked_reasons,
        "diff_files": diff_sum.files,
        "syntax_errors": diff_sum.syntax_errors,
        "eval_raw": dec.eval_result.raw_output,
    }


def pack_adk_submission(
    adk_dir: Path = REPO_ROOT / "kaggle" / "agent" / "adk_pack",
    out_zip: Path = REPO_ROOT / "kaggle" / "agent" / "build" / "submission.zip",
) -> Dict[str, Any]:
    """Package `adk_pack/` into a valid Kaggle ADK `submission.zip` with `agent.yaml` at root."""
    import zipfile

    adk_root = Path(adk_dir).resolve()
    zip_path = Path(out_zip).resolve()
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    files = sorted(
        p
        for p in adk_root.rglob("*")
        if p.is_file() and "__pycache__" not in p.parts and p.suffix != ".pyc"
    )
    archived: List[str] = []
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for fpath in files:
            rel = fpath.relative_to(adk_root).as_posix()
            if ".." in rel.split("/") or rel.startswith("/"):
                raise ValueError(f"Unsafe zip member path: {rel!r}")
            zf.write(fpath, arcname=rel)
            archived.append(rel)
    return {"zip_path": str(zip_path), "file_count": len(archived), "files": archived}


def pack_colab_bundle(
    out_tgz: Path = REPO_ROOT / "kaggle" / "agent" / "build" / "sound_colab_bundle.tgz",
) -> Dict[str, Any]:
    """Build a compact (< 5 MB) tarball with C17 headers, modules, kaggle/agent, and sample tasks for Colab."""
    import io
    import tarfile

    tgz_path = Path(out_tgz).resolve()
    tgz_path.parent.mkdir(parents=True, exist_ok=True)
    flow_root = REPO_ROOT.parent / "FLOW"

    explicit_files: List[Tuple[Path, str]] = [
        (REPO_ROOT / "sound.h", "sound.h"),
        (REPO_ROOT / "sound.c", "sound.c"),
        (REPO_ROOT / "VERSION", "VERSION"),
        (REPO_ROOT / "build" / "sound_schema_abi.h", "build/sound_schema_abi.h"),
        (REPO_ROOT / "kaggle" / "__init__.py", "kaggle/__init__.py"),
        (flow_root / "core" / "flow.h", "vendor_flow/core/flow.h"),
        (flow_root / "core" / "flow_opt_params.h", "vendor_flow/core/flow_opt_params.h"),
        (flow_root / "core" / "searu.h", "vendor_flow/core/searu.h"),
        (flow_root / "sdk" / "flow_ir.h", "vendor_flow/sdk/flow_ir.h"),
    ]
    for src, _arc in explicit_files:
        if not src.is_file():
            raise FileNotFoundError(f"Required Colab bundle file missing: {src}")

    tree_roots: List[Tuple[Path, str, Tuple[str, ...]]] = [
        (REPO_ROOT / "modules", "modules", ("__pycache__",)),
        (REPO_ROOT / "kaggle" / "agent", "kaggle/agent", ("__pycache__", "build")),
    ]

    archived: List[str] = []
    with tarfile.open(tgz_path, "w:gz") as tf:
        for src, arc in explicit_files:
            tf.add(src, arcname=arc)
            archived.append(arc)

        for root_dir, arc_prefix, skip_parts in tree_roots:
            for p in sorted(root_dir.rglob("*")):
                if not p.is_file() or p.suffix == ".pyc":
                    continue
                rel_parts = p.relative_to(root_dir).parts
                if any(sp in rel_parts for sp in skip_parts):
                    continue
                arc = f"{arc_prefix}/{p.relative_to(root_dir).as_posix()}"
                tf.add(p, arcname=arc)
                archived.append(arc)

        tasks_jsonl = REPO_ROOT / "kaggle" / "data" / "tasks.jsonl"
        if tasks_jsonl.is_file():
            head_lines = []
            with tasks_jsonl.open("r", encoding="utf-8") as fh:
                for ln in fh:
                    if ln.strip():
                        head_lines.append(ln)
                        if len(head_lines) >= 20:
                            break
            payload = "".join(head_lines).encode("utf-8")
            ti = tarfile.TarInfo(name="kaggle/data/sample_tasks_20.jsonl")
            ti.size = len(payload)
            tf.addfile(ti, io.BytesIO(payload))
            archived.append(ti.name)

    return {
        "tgz_path": str(tgz_path),
        "bytes": tgz_path.stat().st_size,
        "file_count": len(archived),
    }


_DEFAULT_SALVAGE_PREFIXES: Tuple[str, ...] = (
    "HARNESS_README.md",
    "docker",
    "sample_submission",
    "sandbox",
    "graphs",
    "tasks.jsonl",
)


def salvage_truncated_zip(
    zip_path: Path,
    out_dir: Path = DEFAULT_DATA_DIR,
    *,
    prefixes: Optional[Sequence[str]] = _DEFAULT_SALVAGE_PREFIXES,
) -> Dict[str, Any]:
    """Stream-extract intact entries from a truncated ZIP64 archive with strict CRC32 verification."""
    src = Path(zip_path)
    if not src.is_file():
        raise FileNotFoundError(f"Zip archive not found: {src}")
    dest_root = Path(out_dir).resolve()
    dest_root.mkdir(parents=True, exist_ok=True)
    total_size = src.stat().st_size

    wanted = set(prefixes) if prefixes is not None else None
    by_prefix: Counter[str] = Counter()
    intact_total = 0
    extracted_count = 0
    skipped_count = 0
    crc_verified = 0
    crc_errors: List[str] = []
    bytes_written = 0
    truncated_entry: Optional[str] = None
    missing_bytes = 0
    chunk_size = 1 << 20

    with src.open("rb") as fh:
        pos = 0
        while pos + 30 <= total_size:
            fh.seek(pos)
            hdr = fh.read(30)
            if len(hdr) < 30 or hdr[:4] != b"PK\x03\x04":
                break
            (
                _ver,
                flag,
                method,
                _mtime,
                _mdate,
                exp_crc,
                csize,
                usize,
                nlen,
                elen,
            ) = struct.unpack("<HHHHHIIIHH", hdr[4:30])
            name = fh.read(nlen).decode("utf-8", errors="replace")
            extra = fh.read(elen)

            if flag & 0x0008:
                truncated_entry = f"{name} (data descriptor unsupported at offset {pos})"
                break

            if csize == 0xFFFFFFFF or usize == 0xFFFFFFFF:
                ep = 0
                while ep + 4 <= len(extra):
                    eid, esz = struct.unpack_from("<HH", extra, ep)
                    ep += 4
                    if eid == 0x0001:
                        off = ep
                        if usize == 0xFFFFFFFF:
                            usize = struct.unpack_from("<Q", extra, off)[0]
                            off += 8
                        if csize == 0xFFFFFFFF:
                            csize = struct.unpack_from("<Q", extra, off)[0]
                        break
                    ep += esz

            data_start = pos + 30 + nlen + elen
            if data_start + csize > total_size:
                truncated_entry = name
                missing_bytes = (data_start + csize) - total_size
                break

            intact_total += 1
            next_pos = data_start + csize
            posix_rel = PurePosixPath(name)
            if not name or name.endswith("/"):
                pos = next_pos
                continue
            if name.startswith("/") or ".." in posix_rel.parts:
                raise ValueError(f"Unsafe zip entry path rejected: {name!r}")

            top = posix_rel.parts[0]
            if wanted is not None and top not in wanted and name not in wanted:
                skipped_count += 1
                pos = next_pos
                continue

            target = (dest_root / Path(*posix_rel.parts)).resolve()
            if dest_root not in target.parents and target != dest_root:
                raise ValueError(f"Resolved path escaped destination directory: {target}")
            target.parent.mkdir(parents=True, exist_ok=True)
            part = target.with_name(target.name + ".part")

            if method not in (0, 8):
                crc_errors.append(f"{name}: unsupported compression method {method}")
                pos = next_pos
                continue

            fh.seek(data_start)
            decomp = zlib.decompressobj(-zlib.MAX_WBITS) if method == 8 else None
            remaining = csize
            calc_crc = 0
            written = 0
            try:
                with part.open("wb") as out_f:
                    while remaining > 0:
                        raw_chunk = fh.read(min(chunk_size, remaining))
                        if not raw_chunk:
                            raise EOFError(f"Unexpected EOF reading {name}")
                        remaining -= len(raw_chunk)
                        plain = decomp.decompress(raw_chunk) if decomp is not None else raw_chunk
                        if plain:
                            calc_crc = zlib.crc32(plain, calc_crc)
                            out_f.write(plain)
                            written += len(plain)
                    if decomp is not None:
                        tail = decomp.flush()
                        if tail:
                            calc_crc = zlib.crc32(tail, calc_crc)
                            out_f.write(tail)
                            written += len(tail)
                if (calc_crc & 0xFFFFFFFF) != exp_crc or written != usize:
                    part.unlink(missing_ok=True)
                    crc_errors.append(
                        f"{name}: crc=0x{calc_crc & 0xFFFFFFFF:08x}/0x{exp_crc:08x} size={written}/{usize}"
                    )
                else:
                    os.replace(part, target)
                    crc_verified += 1
                    extracted_count += 1
                    bytes_written += written
                    by_prefix[top] += 1
            except Exception as exc:
                part.unlink(missing_ok=True)
                crc_errors.append(f"{name}: {exc}")

            pos = next_pos

    return {
        "zip_path": str(src),
        "out_dir": str(dest_root),
        "file_size_bytes": total_size,
        "intact_total": intact_total,
        "extracted_count": extracted_count,
        "skipped_count": skipped_count,
        "crc_verified": crc_verified,
        "crc_errors": crc_errors,
        "bytes_written": bytes_written,
        "truncated_entry": truncated_entry,
        "missing_bytes": missing_bytes,
        "by_prefix": dict(sorted(by_prefix.items())),
    }


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="SOUND x Kaggle Gemma 4 stress-test harness")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_ins = sub.add_parser("inspect", help="Inspect Kaggle dataset directory if unpacked")
    p_ins.add_argument("--data-dir", type=Path, default=None)
    p_ins.add_argument("--require-data", action="store_true", help="Exit 2 if dataset is not unpacked yet")

    p_str = sub.add_parser("stress", help="Run adversarial + 3-way parity + graph/task stress suite")
    p_str.add_argument("--data-dir", type=Path, default=None)
    p_str.add_argument("--max-tasks", type=int, default=25)
    p_str.add_argument("--synthetic", action="store_true", help="Run only synthetic tasks even if data exists")
    p_str.add_argument("--require-data", action="store_true", help="Exit 2 if Kaggle dataset is missing")

    p_ws = sub.add_parser("eval-workspace", help="Audit a live git working directory")
    p_ws.add_argument("--workspace", type=Path, default=Path("."))
    p_ws.add_argument("--repro-observed", action="store_true")
    p_ws.add_argument("--regression-run", action="store_true")
    p_ws.add_argument("--regression-passed", action="store_true")

    p_pk = sub.add_parser("pack-submission", help="Package adk_pack into submission.zip")
    p_pk.add_argument("--out", type=Path, default=REPO_ROOT / "kaggle" / "agent" / "build" / "submission.zip")

    p_cb = sub.add_parser("pack-colab", help="Package compact Colab runtime tarball (< 5 MB)")
    p_cb.add_argument("--out", type=Path, default=REPO_ROOT / "kaggle" / "agent" / "build" / "sound_colab_bundle.tgz")

    p_sv = sub.add_parser("salvage-zip", help="Salvage intact entries from truncated ZIP64 archive with CRC32")
    p_sv.add_argument("zip_path", type=Path, help="Path to truncated Kaggle .zip archive")
    p_sv.add_argument("--out-dir", type=Path, default=DEFAULT_DATA_DIR, help="Output directory (default: kaggle/data)")
    p_sv.add_argument("--all", action="store_true", help="Extract all intact entries including embeddings & snapshots")

    args = parser.parse_args(argv)

    if args.cmd == "inspect":
        report = inspect_dataset(args.data_dir)
        print(json.dumps(report, indent=2))
        if args.require_data and not report["found"]:
            return 2
        return 0

    if args.cmd == "salvage-zip":
        report = salvage_truncated_zip(
            args.zip_path,
            args.out_dir,
            prefixes=(None if args.all else _DEFAULT_SALVAGE_PREFIXES),
        )
        print(json.dumps(report, indent=2))
        return 0 if not report["crc_errors"] else 2

    if args.cmd == "stress":
        if args.require_data and discover_data_dir(args.data_dir) is None:
            print(json.dumps({"error": "Kaggle dataset not found locally; pass --data-dir or unpack kaggle/data/"}, indent=2))
            return 2
        report = run_stress_suite(
            data_dir=args.data_dir,
            max_tasks=args.max_tasks,
            synthetic_only=args.synthetic,
        )
        print(json.dumps(report, indent=2))
        return 0

    if args.cmd == "pack-submission":
        report = pack_adk_submission(out_zip=args.out)
        print(json.dumps(report, indent=2))
        return 0

    if args.cmd == "pack-colab":
        report = pack_colab_bundle(out_tgz=args.out)
        print(json.dumps(report, indent=2))
        return 0

    report = eval_workspace(
        args.workspace,
        repro_observed=args.repro_observed,
        regression_run=args.regression_run,
        regression_passed=(True if args.regression_passed else None),
    )
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
