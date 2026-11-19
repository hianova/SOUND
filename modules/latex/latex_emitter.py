"""Auditable LaTeX report emitter turning SOUND verdicts & SEARU repairs back into `.tex`."""

from __future__ import annotations

import re
from typing import Any, Dict, List, Optional, Sequence

from .latex_compiler import CompiledLatexDoc
from .qa_verifier import QAVerdict

_TEX_ESCAPE_MAP: Dict[str, str] = {
    "\\": r"\textbackslash{}",
    "{": r"\{",
    "}": r"\}",
    "$": r"\$",
    "&": r"\&",
    "%": r"\%",
    "#": r"\#",
    "_": r"\_",
    "^": r"\textasciicircum{}",
    "~": r"\textasciitilde{}",
}

_ENV_TOKEN_RE = re.compile(r"\\(begin|end)\{([A-Za-z0-9_*]+)\}")


def escape_latex(text: str) -> str:
    """Escape LaTeX special characters in a single pass."""
    return "".join(_TEX_ESCAPE_MAP.get(ch, ch) for ch in str(text))


def _status_badge(status: Optional[str], fiber_name_str: str) -> str:
    if status == "established":
        return r"\textbf{established}"
    if status == "defeated":
        return r"\textbf{defeated}"
    if status == "conflictUndecided":
        return r"\textbf{conflictUndecided}"
    if status == "penumbraOpen":
        return r"\textbf{penumbraOpen}"
    return rf"\texttt{{{escape_latex(fiber_name_str)}}}"


def emit_latex_report(
    doc: CompiledLatexDoc,
    verdicts: Sequence[QAVerdict],
    *,
    ablations: Optional[Sequence[Dict[str, Any]]] = None,
    standalone: bool = True,
) -> str:
    """Emit a deterministic, compilable LaTeX audit & comprehension diagnostic report."""
    lines: List[str] = []
    if standalone:
        lines.extend(
            [
                r"\documentclass[11pt]{article}",
                r"\usepackage[utf8]{inputenc}",
                r"\usepackage{amsmath,amssymb,amsthm,booktabs}",
                rf"\title{{SOUND Formal Comprehension Report: {escape_latex(doc.title)}}}",
                r"\date{}",
                r"\begin{document}",
                r"\maketitle",
                "",
            ]
        )

    lines.extend(
        [
            r"\section{Prepared-Statement Policy Seal \& Arena Metadata}",
            r"\begin{itemize}",
            rf"  \item \textbf{{Pack ID}}: \texttt{{{escape_latex(doc.pack_id)}}} ({escape_latex(doc.domain)} / {escape_latex(doc.jurisdiction)})",
            rf"  \item \textbf{{Standard}}: {escape_latex(doc.standard)}",
            rf"  \item \textbf{{Sealed Arena}}: 16,384~B (\texttt{{nodes={len(doc.nodes)}/64}}, \texttt{{policy\_mask=0x{doc.policy_mask:016x}}}, \texttt{{bind\_mask=0x{doc.bind_mask:016x}}}, \texttt{{immunity\_mask=0x{doc.immunity_mask:016x}}})",
            rf"  \item \textbf{{Digests}}: \texttt{{policy\_hash=0x{doc.forest_ctrl.policy_hash:016x}}}, \texttt{{merkle\_root=0x{doc.norm_state.merkle_root:016x}}}, \texttt{{source\_bytes={len(doc.source_bytes)}}}",
            r"\end{itemize}",
            "",
            r"\section{Semantic Node \& Source-Span Provenance Map}",
            r"\begin{tabular}{lllll}",
            r"\toprule",
            r"ID & Env & Sort / Plane & Source Lines & Byte Span \\",
            r"\midrule",
        ]
    )

    for nd in doc.nodes:
        plane = "bind" if nd.is_bind_slot else ("kanlock" if nd.kanlock else "policy")
        lines.append(
            rf"\texttt{{{escape_latex(nd.node_id)}}} & "
            rf"\texttt{{{escape_latex(nd.env_name)}}} & "
            rf"\texttt{{{escape_latex(nd.sort)}/{plane}}} & "
            rf"{nd.line_start}--{nd.line_end} & "
            rf"\texttt{{{nd.span_start}..{nd.span_end}}} \\"
        )

    lines.extend(
        [
            r"\bottomrule",
            r"\end{tabular}",
            "",
            r"\section{SOUND Comprehension \& Adversarial Probe Verdicts}",
        ]
    )

    for v in verdicts:
        badge = _status_badge(v.actual_status, v.landing_fiber_name)
        pass_lbl = "PASS" if v.comprehension_pass else "FAIL"
        lines.append(
            rf"\subsection{{{escape_latex(v.qa_id)}: {escape_latex(v.title)} [{pass_lbl}]}}"
        )
        lines.append(r"\begin{itemize}")
        lines.append(
            rf"  \item \textbf{{Landing Fiber}}: \texttt{{{escape_latex(v.landing_fiber_name)}}} | \textbf{{Status}}: {badge}"
        )
        lines.append(
            rf"  \item \textbf{{Bound Facts}}: \texttt{{{escape_latex(', '.join(v.bind_facts) if v.bind_facts else '(none)')}}}"
        )
        if v.tamper_target:
            lines.append(
                rf"  \item \textbf{{Tamper Slot}}: \texttt{{{escape_latex(v.tamper_target)}}} | \textbf{{Inject Reasons}}: \texttt{{{escape_latex(', '.join(v.inject_reasons) if v.inject_reasons else 'NONE')}}}"
            )
        if v.reason_names:
            lines.append(
                rf"  \item \textbf{{Kernel Reasons}}: \texttt{{{escape_latex(', '.join(v.reason_names))}}}"
            )
        if v.repair_result is not None:
            lines.append(
                rf"  \item \textbf{{SEARU Conflict Repair}}: energy={v.repair_result.energy}, conv={int(v.repair_result.converged)}, retract=\texttt{{{escape_latex(', '.join(v.retracted_nodes) or '(none)')}}}"
            )
        if v.provenance_refs:
            ref_strs = [
                rf"\texttt{{{escape_latex(r.node_id)}}} ({escape_latex(r.role_in_query)}, L{r.line_start}, ref~\ref{{{r.tex_label}}})"
                for r in v.provenance_refs
            ]
            lines.append(r"  \item \textbf{Provenance}: " + "; ".join(ref_strs))
        lines.append(rf"  \item \textbf{{Diagnosis}}: {escape_latex(v.diagnosis_zh)}")
        lines.append(r"\end{itemize}")
        lines.append("")

    if ablations:
        lines.extend(
            [
                r"\section{Socratic Premise Necessity Ablation}",
                r"\begin{tabular}{lllll}",
                r"\toprule",
                r"Dropped Fact & Source Line & Resulting Status & Necessary & Reasons \\",
                r"\midrule",
            ]
        )
        for row in ablations:
            nec = "YES" if row["necessary"] else "NO"
            rs = ", ".join(row["reason_names"]) if row["reason_names"] else "NONE"
            lines.append(
                rf"\texttt{{{escape_latex(row['dropped_fact'])}}} & "
                rf"{row['line_start']}--{row['line_end']} & "
                rf"\texttt{{{escape_latex(str(row['resulting_status']))}}} & "
                rf"\textbf{{{nec}}} & "
                rf"\texttt{{{escape_latex(rs)}}} \\"
            )
        lines.extend([r"\bottomrule", r"\end{tabular}", ""])

    if standalone:
        lines.append(r"\end{document}")

    return "\n".join(lines) + "\n"


def validate_emitted_latex_structure(tex_str: str) -> List[str]:
    """Verify structural TeX validity: documentclass, balanced braces, and matched environments."""
    errors: List[str] = []
    if r"\documentclass" not in tex_str:
        errors.append("Missing \\documentclass")
    if r"\begin{document}" not in tex_str:
        errors.append("Missing \\begin{document}")
    if r"\end{document}" not in tex_str:
        errors.append("Missing \\end{document}")

    # Check balanced braces outside escaped \{ and \}
    depth = 0
    i = 0
    n = len(tex_str)
    while i < n:
        ch = tex_str[i]
        if ch == "\\":
            i += 2
            continue
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth < 0:
                errors.append(f"Unmatched '}}' at index {i}")
                break
        i += 1
    if depth > 0:
        errors.append(f"Unclosed '{{' brace depth={depth}")

    # Check balanced \begin{env} / \end{env}
    stack: List[str] = []
    for m in _ENV_TOKEN_RE.finditer(tex_str):
        kind, env = m.group(1), m.group(2)
        if kind == "begin":
            stack.append(env)
        else:
            if not stack or stack[-1] != env:
                top = stack[-1] if stack else "(empty)"
                errors.append(f"Mismatched \\end{{{env}}} (stack top={top})")
                break
            stack.pop()
    if stack:
        errors.append(f"Unclosed environment(s): {stack}")

    return errors
