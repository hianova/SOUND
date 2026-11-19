"""Ring-0 Prepared-Statement Q&A verifier and Socratic premise necessity prober for LaTeX docs."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence

from ..common.arena_codec import (
    ArenaEvalResult,
    ArenaRepairResult,
    ForestCtrlSnapshot,
    SoundFiber,
    admit_candidate_bytes,
    decode_inject_reasons,
    decode_reasons,
    fiber_name,
    policy_verify_bytes,
    repair_arena_bytes,
    try_verify_arena_bytes,
)
from .latex_compiler import CompiledLatexDoc, LatexNodeSpec, LatexQAProbe


@dataclass(frozen=True)
class ProvenanceRef:
    """Traceable link back to an exact environment, `\\label`, and byte/line span in `.tex`."""

    node_id: str
    role_in_query: str  # target | active_premise | missing_premise | unsatisfied_presup | active_attacker
    title: str
    tex_label: str
    line_start: int
    line_end: int
    span_start: int
    span_end: int
    node_status: str
    reason_names: List[str]


@dataclass(frozen=True)
class QAVerdict:
    """Formal comprehension verdict returned from Ring-0 `SoundArena` execution."""

    qa_id: str
    kind: str
    title: str
    target_node: Optional[str]
    bind_facts: List[str]
    tamper_target: Optional[str]
    landing_fiber: SoundFiber
    landing_fiber_name: str
    actual_status: Optional[str]
    reason_names: List[str]
    inject_reasons: List[str]
    comprehension_pass: bool
    expected_fiber: SoundFiber
    expected_status: Optional[str]
    expected_reasons: List[str]
    diagnosis_zh: str
    provenance_refs: List[ProvenanceRef]
    repair_result: Optional[ArenaRepairResult]
    retracted_nodes: List[str]
    kept_nodes: List[str]
    forest_ctrl: ForestCtrlSnapshot
    eval_result: Optional[ArenaEvalResult]


def _nodes_from_mask(doc: CompiledLatexDoc, mask: int) -> List[str]:
    return [nd.node_id for nd in doc.nodes if (mask & (1 << nd.index)) != 0]


def _build_provenance_refs(
    doc: CompiledLatexDoc,
    target_node: Optional[str],
    eval_result: Optional[ArenaEvalResult],
) -> List[ProvenanceRef]:
    if target_node is None or eval_result is None or target_node not in doc.node_index:
        return []

    status_map = {nd.label: nd for nd in eval_result.nodes}
    refs: List[ProvenanceRef] = []
    seen_roles: set[tuple[str, str]] = set()

    def _add(nid: str, role_in_query: str) -> None:
        key = (nid, role_in_query)
        if key in seen_roles or nid not in doc.node_index:
            return
        seen_roles.add(key)
        meta: LatexNodeSpec = doc.node_index[nid]
        ev = status_map.get(nid)
        st = ev.status if ev is not None else "unknown"
        rs = ev.reason_names if ev is not None else []
        refs.append(
            ProvenanceRef(
                node_id=nid,
                role_in_query=role_in_query,
                title=meta.title,
                tex_label=meta.tex_label,
                line_start=meta.line_start,
                line_end=meta.line_end,
                span_start=meta.span_start,
                span_end=meta.span_end,
                node_status=st,
                reason_names=rs,
            )
        )

    _add(target_node, "target")
    tmeta = doc.node_index[target_node]

    for p_id in tmeta.presup:
        ev = status_map.get(p_id)
        if ev is not None and ev.status == "established":
            _add(p_id, "active_premise")
        else:
            _add(p_id, "unsatisfied_presup")

    for clause in tmeta.clauses:
        for c_id in clause:
            ev = status_map.get(c_id)
            if ev is not None and ev.status == "established":
                _add(c_id, "active_premise")
            else:
                _add(c_id, "missing_premise")

    for a_id in tmeta.attacked_by:
        ev = status_map.get(a_id)
        if ev is not None and ev.status in ("established", "conflictUndecided"):
            _add(a_id, "active_attacker")

    return refs


def _compose_diagnosis_zh(
    doc: CompiledLatexDoc,
    target_node: Optional[str],
    actual_status: Optional[str],
    reason_names: Sequence[str],
    refs: Sequence[ProvenanceRef],
    retracted_nodes: Sequence[str],
) -> str:
    if target_node is None or actual_status is None:
        return "已完成全域雙格檢查。"
    tmeta = doc.node_index[target_node]
    loc = f"（第 {tmeta.line_start}–{tmeta.line_end} 行 \\ref{{{tmeta.tex_label}}}）"

    if actual_status == "established":
        premises = [
            f"{r.node_id}({r.title})"
            for r in refs
            if r.role_in_query == "active_premise"
        ]
        prem_str = "、".join(premises) if premises else "無額外前提"
        return f"結論「{tmeta.title}」成立{loc}；已滿足前提：{prem_str}。"

    if actual_status == "penumbraOpen":
        return (
            f"「{tmeta.title}」落在灰色半影區 penumbraOpen{loc}："
            "題設條件尚不足以判定，不可武斷回答成立或不成立（需補齊條件或認定規則）。"
        )

    if actual_status == "conflictUndecided":
        retract_str = "、".join(retracted_nodes) if retracted_nodes else "無"
        return (
            f"「{tmeta.title}」發生規範／假設衝突 conflictUndecided{loc}；"
            f"SEARU 求解極大無衝突子集後建議撤回：{retract_str}。"
        )

    # defeated
    attackers = [
        f"{r.node_id}「{r.title}」(L{r.line_start})"
        for r in refs
        if r.role_in_query == "active_attacker"
    ]
    missing_p = list(
        dict.fromkeys(
            f"{r.node_id}「{r.title}」(L{r.line_start})"
            for r in refs
            if r.role_in_query in ("unsatisfied_presup", "missing_premise")
        )
    )
    parts: List[str] = [f"「{tmeta.title}」被擊敗 defeated{loc}"]
    if "ATTACKED" in reason_names and attackers:
        parts.append(f"遭到反例／但書攻破：{'、'.join(attackers)}")
    if (
        "UNVERIFIED_PRESUP" in reason_names or "NO_ACTIVE_CLAUSE" in reason_names
    ) and missing_p:
        parts.append(f"尚缺必要前提／先決條件：{'、'.join(missing_p)}")
    if "HUME_IS_TO_OUGHT" in reason_names:
        parts.append("觸發休謨防火牆 HUME_IS_TO_OUGHT：純事實不得未經法條／公理跳步推導規範")
    if "HUME_OUGHT_TO_IS" in reason_names:
        parts.append("觸發休謨防火牆 HUME_OUGHT_TO_IS：規範條文不得反推事實前提")
    if "REVOKED" in reason_names:
        parts.append("引用規則或權利已被撤銷 (REVOKED)")
    if len(parts) == 1:
        parts.append(f"原因：{', '.join(reason_names) or '無有效推導子句'}")
    return "；".join(parts) + "。"


def verify_scenario(
    doc: CompiledLatexDoc,
    sound_bin: Path,
    *,
    bind_facts: Sequence[str],
    target_node: Optional[str],
    claim_status: Optional[str] = None,
    expected_fiber: SoundFiber = SoundFiber.COMMITTED,
    expected_reasons: Sequence[str] = (),
    force: str = "assertion",
    evid: str = "sensory",
    tamper_target: Optional[str] = None,
    tamper_evid: Optional[str] = None,
    qa_id: str = "interactive",
    kind: str = "scenario",
    title: Optional[str] = None,
) -> QAVerdict:
    """Bind untrusted parameters into the sealed `SoundArena` and return a Ring-0 diagnosis."""
    if target_node is not None and target_node not in doc.node_index:
        raise KeyError(f"Unknown target_node {target_node!r} in {doc.pack_id!r}")
    for fid in bind_facts:
        if fid not in doc.node_index:
            raise KeyError(f"Unknown bind fact {fid!r} in {doc.pack_id!r}")
    if tamper_target is not None and tamper_target not in doc.node_index:
        raise KeyError(f"Unknown tamper_target {tamper_target!r} in {doc.pack_id!r}")

    cur_arena = doc.arena_bytes
    utterance_len = len(doc.source_bytes)
    ctrl_snap = doc.forest_ctrl

    pv_fiber = policy_verify_bytes(cur_arena, sound_bin)
    if pv_fiber != SoundFiber.COMMITTED:
        return QAVerdict(
            qa_id=qa_id,
            kind=kind,
            title=title or (doc.node_index[target_node].title if target_node else doc.title),
            target_node=target_node,
            bind_facts=list(bind_facts),
            tamper_target=tamper_target,
            landing_fiber=pv_fiber,
            landing_fiber_name=fiber_name(int(pv_fiber)),
            actual_status=None,
            reason_names=[],
            inject_reasons=[],
            comprehension_pass=(pv_fiber == expected_fiber),
            expected_fiber=expected_fiber,
            expected_status=claim_status,
            expected_reasons=list(expected_reasons),
            diagnosis_zh=f"政策封印驗證失敗：{fiber_name(int(pv_fiber))}。",
            provenance_refs=[],
            repair_result=None,
            retracted_nodes=[],
            kept_nodes=[],
            forest_ctrl=ctrl_snap,
            eval_result=None,
        )

    # Normal bindings (using probe force/evid when there is no separate tamper_target)
    bind_force = "assertion" if tamper_target is not None else force
    bind_evid = "sensory" if tamper_target is not None else evid

    for fid in bind_facts:
        nd = doc.node_index[fid]
        cur_arena, step_fiber, _admitted, ctrl_snap = admit_candidate_bytes(
            cur_arena,
            sound_bin,
            target_node=nd.index,
            force=bind_force,
            sort="factual",
            evid=bind_evid,
            aspect="state",
            span_start=nd.span_start,
            span_end=nd.span_end,
            utterance_bytes=utterance_len,
            payload_hash=nd.payload_hash,
        )
        if step_fiber != SoundFiber.COMMITTED:
            inj = decode_inject_reasons(ctrl_snap.last_reject_reason)
            pass_flag = (step_fiber == expected_fiber) and all(
                r in inj for r in expected_reasons
            )
            diag = (
                f"Prepared-Statement 入口閘門退回綁定「{fid}」"
                f"（第 {nd.line_start}–{nd.line_end} 行）：{fiber_name(int(step_fiber))}"
            )
            if inj:
                diag += f"，攔截原因：{', '.join(inj)}"
            diag += "，原 Arena 保持精確恒等零寫入。"
            return QAVerdict(
                qa_id=qa_id,
                kind=kind,
                title=title or (doc.node_index[target_node].title if target_node else fid),
                target_node=target_node,
                bind_facts=list(bind_facts),
                tamper_target=tamper_target,
                landing_fiber=step_fiber,
                landing_fiber_name=fiber_name(int(step_fiber)),
                actual_status=None,
                reason_names=[],
                inject_reasons=inj,
                comprehension_pass=pass_flag,
                expected_fiber=expected_fiber,
                expected_status=claim_status,
                expected_reasons=list(expected_reasons),
                diagnosis_zh=diag,
                provenance_refs=[],
                repair_result=None,
                retracted_nodes=[],
                kept_nodes=[],
                forest_ctrl=ctrl_snap,
                eval_result=None,
            )

    if tamper_target is not None:
        tnd = doc.node_index[tamper_target]
        cur_arena, tamp_fiber, _admitted, ctrl_snap = admit_candidate_bytes(
            cur_arena,
            sound_bin,
            target_node=tnd.index,
            force=force,
            sort="factual",
            evid=tamper_evid or evid,
            aspect="state",
            span_start=tnd.span_start,
            span_end=tnd.span_end,
            utterance_bytes=utterance_len,
            payload_hash=tnd.payload_hash,
        )
        inj = decode_inject_reasons(ctrl_snap.last_reject_reason)
        if tamp_fiber != SoundFiber.COMMITTED:
            pass_flag = (tamp_fiber == expected_fiber) and all(
                r in inj for r in expected_reasons
            )
            diag = (
                f"越權／注入測試於槽位「{tamper_target}」（第 {tnd.line_start}–{tnd.line_end} 行）"
                f"觸發 Ring-0 保護退縮：{fiber_name(int(tamp_fiber))}"
                f"（攔截遮罩：{', '.join(inj) or '無'}），已記入不可變拒絕日誌且未污染規則本體。"
            )
            return QAVerdict(
                qa_id=qa_id,
                kind=kind,
                title=title or f"Tamper Probe: {tamper_target}",
                target_node=target_node,
                bind_facts=list(bind_facts),
                tamper_target=tamper_target,
                landing_fiber=tamp_fiber,
                landing_fiber_name=fiber_name(int(tamp_fiber)),
                actual_status=None,
                reason_names=[],
                inject_reasons=inj,
                comprehension_pass=pass_flag,
                expected_fiber=expected_fiber,
                expected_status=claim_status,
                expected_reasons=list(expected_reasons),
                diagnosis_zh=diag,
                provenance_refs=[],
                repair_result=None,
                retracted_nodes=[],
                kept_nodes=[],
                forest_ctrl=ctrl_snap,
                eval_result=None,
            )

    outcome = try_verify_arena_bytes(cur_arena, sound_bin, backend="ffi")
    if outcome.fiber != SoundFiber.COMMITTED or outcome.eval_result is None:
        return QAVerdict(
            qa_id=qa_id,
            kind=kind,
            title=title or (doc.node_index[target_node].title if target_node else doc.title),
            target_node=target_node,
            bind_facts=list(bind_facts),
            tamper_target=tamper_target,
            landing_fiber=outcome.fiber,
            landing_fiber_name=fiber_name(int(outcome.fiber)),
            actual_status=None,
            reason_names=[],
            inject_reasons=[],
            comprehension_pass=(outcome.fiber == expected_fiber),
            expected_fiber=expected_fiber,
            expected_status=claim_status,
            expected_reasons=list(expected_reasons),
            diagnosis_zh=f"Ring-0 核心雙格求值回落至纖維：{fiber_name(int(outcome.fiber))}。",
            provenance_refs=[],
            repair_result=None,
            retracted_nodes=[],
            kept_nodes=[],
            forest_ctrl=outcome.forest_ctrl or ctrl_snap,
            eval_result=outcome.eval_result,
        )

    ev_res = outcome.eval_result
    repair_res: Optional[ArenaRepairResult] = None
    retracted_nodes: List[str] = []
    kept_nodes: List[str] = []
    if ev_res.conflict_mask != 0:
        repair_res = repair_arena_bytes(cur_arena, sound_bin, backend="ffi")
        retracted_nodes = _nodes_from_mask(doc, repair_res.retract)
        kept_nodes = _nodes_from_mask(doc, repair_res.cand)

    actual_status: Optional[str] = None
    reason_names: List[str] = []
    if target_node is not None:
        nd_eval = ev_res.nodes[doc.node_index[target_node].index]
        actual_status = nd_eval.status
        reason_names = decode_reasons(nd_eval.reasons)

    refs = _build_provenance_refs(doc, target_node, ev_res)
    diag = _compose_diagnosis_zh(doc, target_node, actual_status, reason_names, refs, retracted_nodes)

    pass_flag = expected_fiber == SoundFiber.COMMITTED
    if pass_flag and claim_status is not None:
        pass_flag = actual_status == claim_status
    if pass_flag and expected_reasons:
        pass_flag = all(r in reason_names for r in expected_reasons)

    eff_title = title or (
        doc.node_index[target_node].title if target_node is not None else doc.title
    )
    return QAVerdict(
        qa_id=qa_id,
        kind=kind,
        title=eff_title,
        target_node=target_node,
        bind_facts=list(bind_facts),
        tamper_target=tamper_target,
        landing_fiber=outcome.fiber,
        landing_fiber_name=fiber_name(int(outcome.fiber)),
        actual_status=actual_status,
        reason_names=reason_names,
        inject_reasons=[],
        comprehension_pass=pass_flag,
        expected_fiber=expected_fiber,
        expected_status=claim_status,
        expected_reasons=list(expected_reasons),
        diagnosis_zh=diag,
        provenance_refs=refs,
        repair_result=repair_res,
        retracted_nodes=retracted_nodes,
        kept_nodes=kept_nodes,
        forest_ctrl=outcome.forest_ctrl or ctrl_snap,
        eval_result=ev_res,
    )


def run_embedded_qa_suite(doc: CompiledLatexDoc, sound_bin: Path) -> List[QAVerdict]:
    """Execute every `\\begin{soundqa}` probe declared in a semantic LaTeX document."""
    verdicts: List[QAVerdict] = []
    for probe in doc.qa_probes:
        verdicts.append(
            verify_scenario(
                doc,
                sound_bin,
                bind_facts=probe.bind_facts,
                target_node=probe.target_node,
                claim_status=probe.expect_status,
                expected_fiber=probe.expect_fiber,
                expected_reasons=probe.expect_reasons,
                force=probe.force,
                tamper_target=probe.tamper_target,
                tamper_evid=probe.tamper_evid,
                qa_id=probe.qa_id,
                kind=probe.kind,
                title=probe.title,
            )
        )
    return verdicts


def probe_premise_necessity(
    doc: CompiledLatexDoc,
    sound_bin: Path,
    *,
    base_facts: Sequence[str],
    target_node: str,
) -> List[Dict[str, Any]]:
    """Socratic leave-one-out ablation testing which bound facts are necessary for `target_node`."""
    if target_node not in doc.node_index:
        raise KeyError(f"Unknown target_node {target_node!r}")
    base_verdict = verify_scenario(
        doc,
        sound_bin,
        bind_facts=base_facts,
        target_node=target_node,
        qa_id=f"ablate_base_{target_node}",
        kind="ablation_baseline",
    )
    base_established = base_verdict.actual_status == "established"

    rows: List[Dict[str, Any]] = []
    for dropped in base_facts:
        meta = doc.node_index[dropped]
        subset = [f for f in base_facts if f != dropped]
        sub_verdict = verify_scenario(
            doc,
            sound_bin,
            bind_facts=subset,
            target_node=target_node,
            qa_id=f"ablate_drop_{dropped}",
            kind="ablation",
        )
        necessary = base_established and (sub_verdict.actual_status != "established")
        rows.append(
            {
                "dropped_fact": dropped,
                "title": meta.title,
                "tex_label": meta.tex_label,
                "line_start": meta.line_start,
                "line_end": meta.line_end,
                "baseline_status": base_verdict.actual_status,
                "resulting_status": sub_verdict.actual_status,
                "necessary": necessary,
                "reason_names": sub_verdict.reason_names,
                "diagnosis_zh": sub_verdict.diagnosis_zh,
            }
        )
    return rows
