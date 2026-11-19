"""Semantic LaTeX <-> SOUND Prepared-Statement Comprehension Verification MVP."""

from __future__ import annotations

from .latex_compiler import (
    CompiledLatexDoc,
    LatexCompileError,
    LatexNodeSpec,
    LatexQAProbe,
    compile_latex_file,
    compile_latex_text,
    export_topic_pack,
)
from .latex_emitter import (
    emit_latex_report,
    escape_latex,
    validate_emitted_latex_structure,
)
from .qa_verifier import (
    ProvenanceRef,
    QAVerdict,
    probe_premise_necessity,
    run_embedded_qa_suite,
    verify_scenario,
)

__all__ = [
    "CompiledLatexDoc",
    "LatexCompileError",
    "LatexNodeSpec",
    "LatexQAProbe",
    "ProvenanceRef",
    "QAVerdict",
    "compile_latex_file",
    "compile_latex_text",
    "emit_latex_report",
    "escape_latex",
    "export_topic_pack",
    "probe_premise_necessity",
    "run_embedded_qa_suite",
    "validate_emitted_latex_structure",
    "verify_scenario",
]
