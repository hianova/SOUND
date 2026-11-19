"""SOUND deterministic coding-agent gate, repository analyzer, and dataset harness."""

from .repo_analyzer import (
    DiffSummary,
    GraphBlastReport,
    analyze_networkx_graph,
    check_python_syntax,
    extract_modified_symbols,
    inspect_git_workspace,
    parse_unified_diff,
)
from .sound_gate import (
    CodingSessionSpec,
    GateDecision,
    HypothesisSpec,
    SoundCodingGate,
    compile_coding_session,
    eval_arena_nodes_portable,
)

__all__ = [
    "CodingSessionSpec",
    "DiffSummary",
    "GateDecision",
    "GraphBlastReport",
    "HypothesisSpec",
    "SoundCodingGate",
    "analyze_networkx_graph",
    "check_python_syntax",
    "compile_coding_session",
    "eval_arena_nodes_portable",
    "extract_modified_symbols",
    "inspect_git_workspace",
    "parse_unified_diff",
]
