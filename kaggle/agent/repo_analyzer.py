"""Pure-stdlib diff, AST syntax, NetworkX JSON graph, and git workspace analyzer.

Designed for Python 3.9.6+ without third-party dependencies (`networkx`, `pyyaml`, etc.)
so the same logic can inspect SWE workspaces locally and inside isolated sandboxes.
"""

from __future__ import annotations

import ast
import json
import re
import subprocess
from collections import defaultdict, deque
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Deque, Dict, Iterable, List, Optional, Sequence, Set, Tuple

_DIFF_GIT_RE = re.compile(r"^diff --git a/(.+?) b/(.+)$")
_HUNK_HDR_RE = re.compile(r"^@@\s+-\d+(?:,\d+)?\s+\+\d+(?:,\d+)?\s+@@(?:\s+(.*))?$")
_PY_DEF_RE = re.compile(r"^[+-]\s*(?:async\s+)?(?:def|class)\s+([A-Za-z_][A-Za-z0-9_]*)")
_BROAD_EXCEPT_RE = re.compile(r"^\+\s*except(?:\s*:|\s+Exception\b)")
_DEBUG_PRINT_RE = re.compile(r"^\+\s*print\s*\(")
_TEST_PATH_RE = re.compile(r"(^|/)(?:tests?|testing)/|(^|/)test_[^/]+\.py$|_test\.py$")


@dataclass(frozen=True)
class DiffSummary:
    """Structured facts extracted deterministically from a unified git diff."""

    files: List[str]
    added_lines: int
    removed_lines: int
    hunk_count: int
    py_files: List[str]
    non_py_files: List[str]
    touches_tests: bool
    touches_source: bool
    patch_empty: bool
    is_multifile: bool
    syntax_errors: List[str]
    symbols_touched: List[str]
    has_broad_except: bool
    has_debug_print: bool


@dataclass(frozen=True)
class GraphBlastReport:
    """Bounded caller/callee blast-radius projection from a serialized NetworkX graph."""

    total_nodes: int
    total_edges: int
    target_nodes: List[str]
    top_neighbors: List[Tuple[str, int, int]]  # (node_id, hop_distance, degree)
    projected_nodes: List[str]
    hop_distances: Dict[str, int]
    truncated: bool


def is_test_path(rel_path: str) -> bool:
    """Return True if `rel_path` matches common Python test directory or file naming."""
    return bool(_TEST_PATH_RE.search(rel_path.replace("\\", "/")))


def check_python_syntax(rel_path: str, source_text: str) -> Optional[str]:
    """Parse Python source with `ast.parse`; return formatted error string or None."""
    try:
        ast.parse(source_text, filename=rel_path)
    except SyntaxError as exc:
        line = exc.lineno or 0
        col = exc.offset or 0
        return f"{rel_path}:{line}:{col}: {exc.msg}"
    return None


def extract_modified_symbols(diff_text: str) -> List[str]:
    """Extract ordered unique symbol names mentioned in hunk headers or def/class lines."""
    seen: Set[str] = set()
    ordered: List[str] = []

    def _push(sym: str) -> None:
        cleaned = sym.strip()
        if cleaned and cleaned not in seen:
            seen.add(cleaned)
            ordered.append(cleaned)

    for line in diff_text.splitlines():
        m_hunk = _HUNK_HDR_RE.match(line)
        if m_hunk and m_hunk.group(1):
            hdr = m_hunk.group(1).strip()
            m_hdr_def = re.search(r"(?:class|def)\s+([A-Za-z_][A-Za-z0-9_]*)", hdr)
            if m_hdr_def:
                _push(m_hdr_def.group(1))
            continue
        m_def = _PY_DEF_RE.match(line)
        if m_def:
            _push(m_def.group(1))
    return ordered


def parse_unified_diff(
    diff_text: str,
    *,
    file_contents: Optional[Dict[str, str]] = None,
) -> DiffSummary:
    """Parse a unified diff into deterministic file, hunk, symbol, and smell counts."""
    files: List[str] = []
    file_set: Set[str] = set()
    added_lines = 0
    removed_lines = 0
    hunk_count = 0
    has_broad_except = False
    has_debug_print = False
    pending_minus_file: Optional[str] = None

    for line in diff_text.splitlines():
        m_git = _DIFF_GIT_RE.match(line)
        if m_git:
            pending_minus_file = None
            cand = m_git.group(2)
            if cand not in file_set:
                file_set.add(cand)
                files.append(cand)
            continue
        if line.startswith("--- "):
            raw_minus = line[4:].split("\t", 1)[0].strip()
            if raw_minus != "/dev/null":
                pending_minus_file = raw_minus[2:] if raw_minus.startswith("a/") else raw_minus
            else:
                pending_minus_file = None
            continue
        if line.startswith("+++ "):
            raw_plus = line[4:].split("\t", 1)[0].strip()
            if raw_plus != "/dev/null":
                cand = raw_plus[2:] if raw_plus.startswith("b/") else raw_plus
            else:
                cand = pending_minus_file or ""
            pending_minus_file = None
            if cand and cand not in file_set:
                file_set.add(cand)
                files.append(cand)
            continue
        if line.startswith("@@ "):
            hunk_count += 1
            continue
        if line.startswith("+"):
            added_lines += 1
            if _BROAD_EXCEPT_RE.match(line):
                has_broad_except = True
            if _DEBUG_PRINT_RE.match(line):
                has_debug_print = True
        elif line.startswith("-"):
            removed_lines += 1

    py_files = [p for p in files if p.endswith(".py")]
    non_py_files = [p for p in files if not p.endswith(".py")]
    touches_tests = any(is_test_path(p) for p in files)
    touches_source = any(not is_test_path(p) for p in files)

    syntax_errors: List[str] = []
    if file_contents:
        for rel_path in py_files:
            src = file_contents.get(rel_path)
            if src is not None:
                err = check_python_syntax(rel_path, src)
                if err is not None:
                    syntax_errors.append(err)

    symbols = extract_modified_symbols(diff_text)
    patch_empty = len(files) == 0 or (added_lines == 0 and removed_lines == 0)
    return DiffSummary(
        files=files,
        added_lines=added_lines,
        removed_lines=removed_lines,
        hunk_count=hunk_count,
        py_files=py_files,
        non_py_files=non_py_files,
        touches_tests=touches_tests,
        touches_source=touches_source,
        patch_empty=patch_empty,
        is_multifile=len(files) > 1,
        syntax_errors=syntax_errors,
        symbols_touched=symbols,
        has_broad_except=has_broad_except,
        has_debug_print=has_debug_print,
    )


def _node_label_candidates(raw_node: Any) -> Tuple[str, str]:
    """Extract canonical node ID and searchable lowercase metadata text."""
    if isinstance(raw_node, dict):
        nid = str(
            raw_node.get("id")
            or raw_node.get("name")
            or raw_node.get("node")
            or raw_node.get("path")
            or ""
        )
        meta_parts = [
            nid,
            str(raw_node.get("file", "")),
            str(raw_node.get("path", "")),
            str(raw_node.get("qualname", "")),
            str(raw_node.get("name", "")),
        ]
        return nid, " ".join(p for p in meta_parts if p).lower()
    nid = str(raw_node)
    return nid, nid.lower()


def _extract_graph_topology(
    graph_data: Dict[str, Any],
) -> Tuple[List[str], Dict[str, str], Dict[str, Set[str]]]:
    """Normalize common NetworkX JSON encodings into node list, search blobs, and undirected adjacency."""
    node_ids: List[str] = []
    seen_ids: Set[str] = set()
    search_blob: Dict[str, str] = {}
    adj: Dict[str, Set[str]] = defaultdict(set)

    def _register(nid: str, blob: Optional[str] = None) -> None:
        if not nid:
            return
        if nid not in seen_ids:
            seen_ids.add(nid)
            node_ids.append(nid)
            search_blob[nid] = (blob if blob is not None else nid).lower()
        elif blob and len(blob) > len(search_blob.get(nid, "")):
            search_blob[nid] = blob.lower()

    raw_nodes = graph_data.get("nodes")
    if isinstance(raw_nodes, list):
        for item in raw_nodes:
            nid, blob = _node_label_candidates(item)
            _register(nid, blob)
    elif isinstance(raw_nodes, dict):
        for nid, attrs in raw_nodes.items():
            blob = f"{nid} {attrs}" if isinstance(attrs, dict) else str(nid)
            _register(str(nid), blob)

    raw_links = graph_data.get("links")
    if raw_links is None:
        raw_links = graph_data.get("edges")

    if isinstance(raw_links, list):
        for link in raw_links:
            if isinstance(link, dict):
                src = str(link.get("source", ""))
                dst = str(link.get("target", ""))
            elif isinstance(link, (list, tuple)) and len(link) >= 2:
                src = str(link[0])
                dst = str(link[1])
            else:
                continue
            if src and dst:
                _register(src)
                _register(dst)
                adj[src].add(dst)
                adj[dst].add(src)
    elif isinstance(graph_data.get("adjacency"), list):
        adj_list = graph_data["adjacency"]
        for idx, nbrs in enumerate(adj_list):
            if idx < len(node_ids):
                src = node_ids[idx]
            else:
                src = str(idx)
                _register(src)
            if isinstance(nbrs, list):
                for item in nbrs:
                    dst_raw = item.get("id") if isinstance(item, dict) else item
                    if dst_raw is not None:
                        dst = str(dst_raw)
                        _register(dst)
                        adj[src].add(dst)
                        adj[dst].add(src)

    return node_ids, search_blob, adj


def analyze_networkx_graph(
    graph_data: Dict[str, Any],
    target_files_or_symbols: Sequence[str],
    *,
    max_nodes: int = 32,
) -> GraphBlastReport:
    """Project a large NetworkX dependency graph to a bounded <=64-node caller blast radius."""
    capped_max = max(1, min(int(max_nodes), 64))
    node_ids, search_blob, adj = _extract_graph_topology(graph_data)
    total_nodes = len(node_ids)
    total_edges = sum(len(nbrs) for nbrs in adj.values()) // 2

    queries = [q.strip().lower() for q in target_files_or_symbols if q and q.strip()]
    targets: List[str] = []
    if queries:
        for nid in node_ids:
            blob = search_blob.get(nid, nid.lower())
            if any(q in blob for q in queries):
                targets.append(nid)
    if not targets and node_ids:
        ranked_by_deg = sorted(node_ids, key=lambda n: (-len(adj.get(n, ())), n))
        targets = ranked_by_deg[: min(3, len(ranked_by_deg))]

    hop_distances: Dict[str, int] = {}
    queue: Deque[str] = deque()
    for t in targets:
        if t not in hop_distances:
            hop_distances[t] = 0
            queue.append(t)

    while queue:
        cur = queue.popleft()
        cur_hop = hop_distances[cur]
        for nbr in sorted(adj.get(cur, ())):
            if nbr not in hop_distances:
                hop_distances[nbr] = cur_hop + 1
                queue.append(nbr)

    # Include reachable neighbors ordered by (hop, -degree, id)
    reachable = sorted(
        hop_distances.keys(),
        key=lambda n: (hop_distances[n], -len(adj.get(n, ())), n),
    )
    projected = reachable[:capped_max]
    top_neighbors = [
        (nid, hop_distances[nid], len(adj.get(nid, ())))
        for nid in projected
        if hop_distances[nid] > 0
    ]
    truncated = len(reachable) > capped_max or total_nodes > capped_max

    return GraphBlastReport(
        total_nodes=total_nodes,
        total_edges=total_edges,
        target_nodes=targets[:capped_max],
        top_neighbors=top_neighbors,
        projected_nodes=projected,
        hop_distances={n: hop_distances[n] for n in projected},
        truncated=truncated,
    )


def load_graph_file(graph_path: Path) -> Dict[str, Any]:
    """Load a serialized NetworkX JSON graph from disk."""
    return json.loads(graph_path.read_text(encoding="utf-8"))


def inspect_git_workspace(repo_dir: Path, base_ref: str = "HEAD") -> Dict[str, Any]:
    """Inspect a git working tree for modified diff, untracked files, and Python syntax checks."""
    repo = Path(repo_dir)
    diff_proc = subprocess.run(
        ["git", "-C", str(repo), "diff", "--no-ext-diff", base_ref],
        capture_output=True,
        text=True,
        check=False,
    )
    status_proc = subprocess.run(
        ["git", "-C", str(repo), "status", "--porcelain"],
        capture_output=True,
        text=True,
        check=False,
    )
    diff_text = diff_proc.stdout if diff_proc.returncode == 0 else ""

    file_contents: Dict[str, str] = {}
    untracked_py: List[str] = []
    if status_proc.returncode == 0:
        for ln in status_proc.stdout.splitlines():
            if len(ln) < 4:
                continue
            code = ln[:2]
            rel = ln[3:].strip()
            if " -> " in rel:
                rel = rel.split(" -> ", 1)[1].strip()
            fpath = repo / rel
            if code == "??" and rel.endswith(".py") and fpath.is_file():
                untracked_py.append(rel)
            if rel.endswith(".py") and fpath.is_file():
                file_contents[rel] = fpath.read_text(encoding="utf-8", errors="replace")

    summary = parse_unified_diff(diff_text, file_contents=file_contents)
    extra_syntax_errors = list(summary.syntax_errors)
    for rel in untracked_py:
        if rel not in summary.py_files:
            err = check_python_syntax(rel, file_contents[rel])
            if err is not None:
                extra_syntax_errors.append(err)

    return {
        "repo_dir": str(repo),
        "git_diff_ok": diff_proc.returncode == 0,
        "git_status_lines": [ln for ln in status_proc.stdout.splitlines() if ln.strip()],
        "untracked_py": untracked_py,
        "diff_summary": DiffSummary(
            files=summary.files + [p for p in untracked_py if p not in summary.files],
            added_lines=summary.added_lines,
            removed_lines=summary.removed_lines,
            hunk_count=summary.hunk_count,
            py_files=summary.py_files + [p for p in untracked_py if p not in summary.py_files],
            non_py_files=summary.non_py_files,
            touches_tests=summary.touches_tests or any(is_test_path(p) for p in untracked_py),
            touches_source=summary.touches_source or any(not is_test_path(p) for p in untracked_py),
            patch_empty=summary.patch_empty and not untracked_py,
            is_multifile=(len(summary.files) + len(untracked_py)) > 1,
            syntax_errors=extra_syntax_errors,
            symbols_touched=summary.symbols_touched,
            has_broad_except=summary.has_broad_except,
            has_debug_print=summary.has_debug_print,
        ),
        "diff_text": diff_text,
    }
