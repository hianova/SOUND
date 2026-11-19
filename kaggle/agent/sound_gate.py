"""Deterministic SOUND coding-agent gate bridging SWE repo facts to Ring-0 bilattice evaluation.

Maps localized coding-session observations (diff stats, syntax checks, repro logs,
blast-radius graph checks, regression runs, competing root-cause hypotheses, spec ambiguity)
into a sealed 16,384B `"SND2"` SoundArena (<= 64 nodes) and drives:
  - 4-valued Belnap/grounding evaluation (`established` / `defeated` / `conflictUndecided` / `penumbraOpen`)
  - Hume's Guillotine (`HUME_IS_TO_OUGHT`, `HUME_OUGHT_TO_IS`) & `EVIDENTIAL_LEAF` enforcement
  - SEARU non-monotonic hypothesis conflict repair + `KanLock` (`immunity_mask`) protection
  - `Voidable / Rescission` provisional patch admission & retroactive revocation
  - `2,144B "FIR1"` QTT authority gate synthesis
"""

from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Set, Tuple

from modules.common.arena_codec import (
    ASPECT_MAP,
    DEFAULT_SEARU_SEED,
    EVID_MAP,
    NODE_FLAG_CONSTITUTIVE,
    SOUND_MAX_CLAUSES,
    SOUND_MAX_NODES,
    SORT_MAP,
    STATUS_CODE_MAP,
    ArenaEvalResult,
    ArenaFirResult,
    ArenaNodeSpec,
    ArenaRepairResult,
    NodeEvalResult,
    NormativeStateSnapshot,
    apply_declarative_arena_bytes,
    decode_reasons,
    encode_arena,
    exercise_rescission_arena_bytes,
    lift_fir_arena_bytes,
    load_schema_hash,
    repair_arena_bytes,
    verify_arena_bytes,
)

from .repo_analyzer import DiffSummary, GraphBlastReport, parse_unified_diff

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOUND_BIN = REPO_ROOT / "build" / "sound"

_REASON_PENUMBRA = 0x01
_REASON_REVOKED = 0x02
_REASON_EVIDENTIAL_LEAF = 0x04
_REASON_HUME_IS_TO_OUGHT = 0x08
_REASON_HUME_OUGHT_TO_IS = 0x10
_REASON_UNVERIFIED_PRESUP = 0x20
_REASON_ATTACKED = 0x40
_REASON_NO_ACTIVE_CLAUSE = 0x80


def _sanitize_label(raw: str, max_len: int = 24) -> str:
    """Normalize a node identifier into <= `max_len` ASCII `[A-Za-z0-9_]` characters."""
    cleaned = re.sub(r"[^A-Za-z0-9_]", "_", raw.strip())
    if not cleaned:
        cleaned = "node"
    return cleaned[:max_len]


def _clause_valid_portable(
    nd: ArenaNodeSpec,
    clause_mask: int,
    node_mask: int,
    factual_mask: int,
) -> Tuple[bool, int]:
    """Mirror C17 `sound_clause_valid` in `sound.h` (lines 280-312) bit-for-bit."""
    scoped = clause_mask & node_mask
    if clause_mask != scoped:
        return False, 0

    evid_code = EVID_MAP[nd.evid]
    sort_code = SORT_MAP[nd.sort]
    flags = NODE_FLAG_CONSTITUTIVE if nd.constitutive else 0

    if evid_code >= EVID_MAP["inferential"] and clause_mask == 0:
        return False, _REASON_EVIDENTIAL_LEAF

    if sort_code == SORT_MAP["factual"]:
        if (clause_mask & ~factual_mask) != 0:
            return False, _REASON_HUME_OUGHT_TO_IS
        return True, 0

    if (
        evid_code == EVID_MAP["enacted_norm"]
        or (flags & NODE_FLAG_CONSTITUTIVE) != 0
        or (clause_mask & (node_mask & ~factual_mask)) != 0
    ):
        return True, 0

    return False, _REASON_HUME_IS_TO_OUGHT


def _node_fires_portable(
    u: int,
    nd: ArenaNodeSpec,
    blocked_by: int,
    active_set: int,
    node_mask: int,
    factual_mask: int,
    revoked_mask: int,
    penumbra_mask: int,
) -> bool:
    """Mirror C17 `sound_node_fires` in `sound.h` (lines 314-340)."""
    ub = 1 << u
    if ((revoked_mask | penumbra_mask) & ub) != 0:
        return False
    if (nd.attack_mask & blocked_by) != 0:
        return False
    if (nd.presup_mask & active_set) != nd.presup_mask:
        return False

    nc = min(len(nd.clauses), SOUND_MAX_CLAUSES)
    for c in range(nc):
        cm = nd.clauses[c] & node_mask
        valid, _ = _clause_valid_portable(nd, cm, node_mask, factual_mask)
        if valid and (cm & active_set) == cm:
            return True
    return False


def _gamma_portable(
    nodes: Sequence[ArenaNodeSpec],
    blocked_by: int,
    node_mask: int,
    factual_mask: int,
    revoked_mask: int,
    penumbra_mask: int,
) -> int:
    """Mirror C17 `sound_gamma` in `sound.h` (lines 342-360)."""
    n = min(len(nodes), SOUND_MAX_NODES)
    cur = 0
    for _ in range(n):
        nxt = 0
        for u in range(n):
            if _node_fires_portable(
                u,
                nodes[u],
                blocked_by,
                cur,
                node_mask,
                factual_mask,
                revoked_mask,
                penumbra_mask,
            ):
                nxt |= 1 << u
        if nxt == cur:
            break
        cur = nxt
    return cur


def eval_arena_nodes_portable(nodes: Sequence[ArenaNodeSpec]) -> ArenaEvalResult:
    """Pure-Python C17-parity grounded bilattice evaluator matching `sound_eval` in `sound.h`."""
    n = len(nodes)
    if n > SOUND_MAX_NODES:
        raise ValueError(f"SoundArena allows at most {SOUND_MAX_NODES} nodes, got {n}")

    node_mask = ((1 << n) - 1) if n < 64 else 0xFFFFFFFFFFFFFFFF
    factual_mask = 0
    revoked_mask = 0
    penumbra_mask = 0

    for u, nd in enumerate(nodes):
        if nd.sort not in SORT_MAP:
            raise ValueError(f"Invalid sort {nd.sort!r} on node {nd.label!r}")
        if nd.evid not in EVID_MAP:
            raise ValueError(f"Invalid evid {nd.evid!r} on node {nd.label!r}")
        if nd.aspect not in ASPECT_MAP:
            raise ValueError(f"Invalid aspect {nd.aspect!r} on node {nd.label!r}")
        ub = 1 << u
        if SORT_MAP[nd.sort] == SORT_MAP["factual"]:
            factual_mask |= ub
        if nd.revoked:
            revoked_mask |= ub
        if nd.penumbra:
            penumbra_mask |= ub

    cur_i = 0
    for _ in range(n):
        p_tmp = _gamma_portable(nodes, cur_i, node_mask, factual_mask, revoked_mask, penumbra_mask)
        nxt_i = _gamma_portable(nodes, p_tmp, node_mask, factual_mask, revoked_mask, penumbra_mask)
        if nxt_i == cur_i:
            break
        cur_i = nxt_i

    p_star = _gamma_portable(nodes, cur_i, node_mask, factual_mask, revoked_mask, penumbra_mask)
    i_star = _gamma_portable(nodes, p_star, node_mask, factual_mask, revoked_mask, penumbra_mask)

    pen = penumbra_mask & node_mask
    non_pen = node_mask & ~pen
    est = i_star & non_pen
    conf = (p_star & ~i_star) & non_pen
    defm = non_pen & ~p_star

    eval_nodes: List[NodeEvalResult] = []
    for u in range(n):
        nd = nodes[u]
        ub = 1 << u
        if (pen & ub) != 0:
            st_str = STATUS_CODE_MAP[3]
            rs_val = _REASON_PENUMBRA
        elif (est & ub) != 0:
            st_str = STATUS_CODE_MAP[0]
            rs_val = 0
        elif (conf & ub) != 0:
            st_str = STATUS_CODE_MAP[2]
            rs_val = _REASON_ATTACKED
        else:
            st_str = STATUS_CODE_MAP[1]
            rs_val = 0
            if (revoked_mask & ub) != 0:
                rs_val |= _REASON_REVOKED
            if (nd.attack_mask & p_star) != 0:
                rs_val |= _REASON_ATTACKED
            if (nd.presup_mask & i_star) != nd.presup_mask:
                rs_val |= _REASON_UNVERIFIED_PRESUP
            nc = min(len(nd.clauses), SOUND_MAX_CLAUSES)
            any_valid = False
            for c in range(nc):
                ok, bit_rs = _clause_valid_portable(nd, nd.clauses[c] & node_mask, node_mask, factual_mask)
                rs_val |= bit_rs
                if ok:
                    any_valid = True
            if (not any_valid) or (rs_val == 0):
                rs_val |= _REASON_NO_ACTIVE_CLAUSE

        eval_nodes.append(
            NodeEvalResult(
                index=u,
                label=nd.label,
                status=st_str,
                reasons=rs_val,
            )
        )

    return ArenaEvalResult(
        num_nodes=n,
        i_star=i_star,
        p_star=p_star,
        established_mask=est,
        defeated_mask=defm,
        conflict_mask=conf,
        penumbra_mask=pen,
        nodes=eval_nodes,
        raw_output=(
            f"[portable] nodes={n} I*=0x{i_star:016x} P*=0x{p_star:016x} "
            f"est=0x{est:016x} def=0x{defm:016x} conf=0x{conf:016x} pen=0x{pen:016x}"
        ),
    )


@dataclass(frozen=True)
class HypothesisSpec:
    """Candidate root-cause or fix hypothesis tracked inside a coding session."""

    hyp_id: str
    description: str = ""
    target_symbol: str = ""
    contradicts: Tuple[str, ...] = ()
    supported_by_repro: bool = False
    is_unverified_guess: bool = False


@dataclass(frozen=True)
class CodingSessionSpec:
    """Structured coding-agent state compiled into a SOUND Arena DAG."""

    task_id: str
    repo: str = ""
    diff_text: str = ""
    diff_summary: Optional[DiffSummary] = None
    localized_files: Sequence[str] = ()
    repro_observed: bool = False
    regression_passed: Optional[bool] = None
    regression_run: bool = False
    hypotheses: Sequence[HypothesisSpec] = ()
    ambiguous_spec: bool = False
    submit_requested: bool = False
    hume_shortcut_claim: bool = False
    immunity_locked_nodes: Sequence[str] = ()
    voidable_nodes: Sequence[str] = ()
    graph_report: Optional[GraphBlastReport] = None
    blast_radius_checked: bool = True
    revoked_nodes: Sequence[str] = ()


@dataclass(frozen=True)
class CompiledCodingArena:
    """Arena node specifications, index lookup, and normative bitmasks."""

    nodes: List[ArenaNodeSpec]
    node_index: Dict[str, int]
    power_mask: int
    immunity_mask: int
    duty_pos_mask: int
    duty_neg_mask: int
    voidable_mask: int
    rescission_power_mask: int


@dataclass(frozen=True)
class GateDecision:
    """Structured verdict produced by `SoundCodingGate`."""

    action: str
    submit_ready_status: str
    submit_ready_reasons: List[str]
    eval_result: ArenaEvalResult
    blocked_reasons: List[str]
    nodes: List[ArenaNodeSpec]
    node_index: Dict[str, int]
    power_mask: int
    immunity_mask: int
    normative_snapshot: Optional[NormativeStateSnapshot] = None
    fir_result: Optional[ArenaFirResult] = None
    repair_result: Optional[ArenaRepairResult] = None


def compile_coding_session(spec: CodingSessionSpec) -> CompiledCodingArena:
    """Compile a `CodingSessionSpec` into <= 64 bounded `ArenaNodeSpec` entries and masks."""
    diff_sum = spec.diff_summary
    if diff_sum is None and spec.diff_text:
        diff_sum = parse_unified_diff(spec.diff_text)

    if diff_sum is not None:
        patch_nonempty = not diff_sum.patch_empty
        syntax_clean = len(diff_sum.syntax_errors) == 0
    else:
        patch_nonempty = bool(spec.localized_files) and (spec.regression_run or spec.submit_requested)
        syntax_clean = True

    localized_ok = len(spec.localized_files) > 0
    repro_ok = bool(spec.repro_observed)
    blast_ok = bool(spec.blast_radius_checked)
    if spec.graph_report is not None and len(spec.graph_report.projected_nodes) > SOUND_MAX_NODES:
        blast_ok = False

    reg_penumbra = not bool(spec.regression_run)
    reg_passed = bool(spec.regression_run and spec.regression_passed is True)

    revoked_set = set(spec.revoked_nodes)
    raw_nodes: List[Dict[str, object]] = []
    node_index: Dict[str, int] = {}

    def _add_node(
        label: str,
        *,
        sort: str,
        evid: str,
        aspect: str = "state",
        constitutive: bool = False,
        penumbra: bool = False,
        presup_mask: int = 0,
        attack_mask: int = 0,
        clauses: Optional[List[int]] = None,
        aliases: Sequence[str] = (),
    ) -> int:
        if len(raw_nodes) >= SOUND_MAX_NODES:
            raise ValueError(f"Exceeded SOUND_MAX_NODES ({SOUND_MAX_NODES}) compiling session {spec.task_id}")
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
                "aspect": aspect,
                "constitutive": constitutive,
                "revoked": revoked,
                "penumbra": penumbra,
                "presup_mask": presup_mask,
                "attack_mask": attack_mask,
                "clauses": list(clauses or []),
                "span_start": idx,
                "span_end": idx + 1,
            }
        )
        node_index[clean] = idx
        for alias in aliases:
            if alias not in node_index:
                node_index[alias] = idx
        return idx

    # Core factual & normative precondition DAG (indices 0..6)
    idx_patch = _add_node(
        "fact_patch_nonempty",
        sort="factual",
        evid="direct",
        clauses=[0] if patch_nonempty else [],
    )
    idx_syntax = _add_node(
        "fact_syntax_ok",
        sort="factual",
        evid="direct",
        clauses=[0] if syntax_clean else [],
    )
    idx_loc = _add_node(
        "fact_localized",
        sort="factual",
        evid="direct" if localized_ok else "assumed",
        clauses=[0],
    )
    idx_repro = _add_node(
        "fact_repro_obs",
        sort="factual",
        evid="sensory" if repro_ok else "assumed",
        clauses=[0],
    )
    idx_blast = _add_node(
        "fact_blast_ok",
        sort="factual",
        evid="direct" if blast_ok else "assumed",
        clauses=[0],
    )
    idx_reg = _add_node(
        "fact_reg_pass",
        sort="factual",
        evid="direct",
        penumbra=reg_penumbra,
        clauses=[0] if (reg_penumbra or reg_passed) else [],
    )
    idx_spec = _add_node(
        "norm_spec_clear",
        sort="normative",
        evid="enacted_norm",
        penumbra=bool(spec.ambiguous_spec),
        clauses=[0],
    )

    # Optional bounded hypothesis layer (capped cleanly inside 64 nodes)
    max_hyps = SOUND_MAX_NODES - 12  # reserve slots for preconditions, resolved, shortcut & gate nodes
    hyp_specs = list(spec.hypotheses[:max_hyps])
    hyp_indices: Dict[str, int] = {}
    for h_pos, hyp in enumerate(hyp_specs):
        supported = bool(hyp.supported_by_repro) and not bool(hyp.is_unverified_guess)
        evid = "direct" if supported else "assumed"
        clauses = [1 << idx_repro] if supported else [0]
        h_label = _sanitize_label(hyp.hyp_id if hyp.hyp_id.startswith("hyp_") else f"hyp_{hyp.hyp_id}")
        h_idx = _add_node(
            h_label,
            sort="factual",
            evid=evid,
            clauses=clauses,
            aliases=(hyp.hyp_id, f"hyp_{h_pos}"),
        )
        hyp_indices[hyp.hyp_id] = h_idx

    # Wire symmetric/directed attack masks among hypotheses
    for hyp in hyp_specs:
        src_idx = hyp_indices[hyp.hyp_id]
        src_bit = 1 << src_idx
        for other_id in hyp.contradicts:
            dst_idx = hyp_indices.get(other_id)
            if dst_idx is None or dst_idx == src_idx:
                continue
            dst_bit = 1 << dst_idx
            raw_nodes[src_idx]["attack_mask"] = int(raw_nodes[src_idx]["attack_mask"]) | dst_bit
            raw_nodes[dst_idx]["attack_mask"] = int(raw_nodes[dst_idx]["attack_mask"]) | src_bit

    idx_hyp_resolved: Optional[int] = None
    if hyp_specs:
        # Disjunctive constitutive bridge: any non-attacked established hypothesis settles diagnosis.
        # Note: if two competing hypotheses mutually attack, SEARU can retract one side so the remaining
        # hypothesis keeps up to SOUND_MAX_CLAUSES support paths available.
        clause_list = [1 << hyp_indices[h.hyp_id] for h in hyp_specs[:SOUND_MAX_CLAUSES]]
        idx_hyp_resolved = _add_node(
            "norm_hyp_resolved",
            sort="institutional",
            evid="direct",
            constitutive=True,
            clauses=clause_list,
        )

    idx_shortcut: Optional[int] = None
    if spec.hume_shortcut_claim:
        # Explicit Is->Ought leap test: non-constitutive inferential normative claim from pure facts
        idx_shortcut = _add_node(
            "norm_shortcut_claim",
            sort="normative",
            evid="inferential",
            constitutive=False,
            clauses=[(1 << idx_patch) | (1 << idx_reg)],
        )

    precond_mask = (
        (1 << idx_patch)
        | (1 << idx_syntax)
        | (1 << idx_loc)
        | (1 << idx_repro)
        | (1 << idx_blast)
        | (1 << idx_reg)
        | (1 << idx_spec)
    )
    if idx_hyp_resolved is not None:
        precond_mask |= 1 << idx_hyp_resolved
    if idx_shortcut is not None:
        precond_mask |= 1 << idx_shortcut

    idx_eligible = _add_node(
        "norm_gate_eligible",
        sort="institutional",
        evid="enacted_norm",
        constitutive=True,
        presup_mask=precond_mask,
        clauses=[0],
    )
    idx_submit = _add_node(
        "submit_ready",
        sort="normative",
        evid="enacted_norm",
        constitutive=False,
        presup_mask=1 << idx_eligible,
        clauses=[1 << idx_eligible],
    )

    nodes = [
        ArenaNodeSpec(
            label=str(item["label"]),
            sort=str(item["sort"]),
            evid=str(item["evid"]),
            aspect=str(item["aspect"]),
            constitutive=bool(item["constitutive"]),
            revoked=bool(item["revoked"]),
            penumbra=bool(item["penumbra"]),
            presup_mask=int(item["presup_mask"]),
            attack_mask=int(item["attack_mask"]),
            clauses=list(item["clauses"]),  # type: ignore[arg-type]
            span_start=int(item["span_start"]),
            span_end=int(item["span_end"]),
        )
        for item in raw_nodes
    ]

    immunity_mask = 0
    for name in spec.immunity_locked_nodes:
        idx = node_index.get(name)
        if idx is None:
            idx = node_index.get(_sanitize_label(name))
        if idx is not None:
            immunity_mask |= 1 << idx

    voidable_mask = 0
    for name in spec.voidable_nodes:
        idx = node_index.get(name)
        if idx is None:
            idx = node_index.get(_sanitize_label(name))
        if idx is not None:
            voidable_mask |= 1 << idx

    power_mask = (1 << idx_eligible) | (1 << idx_submit) | voidable_mask
    rescission_power_mask = voidable_mask

    return CompiledCodingArena(
        nodes=nodes,
        node_index=node_index,
        power_mask=power_mask,
        immunity_mask=immunity_mask,
        duty_pos_mask=0,
        duty_neg_mask=0,
        voidable_mask=voidable_mask,
        rescission_power_mask=rescission_power_mask,
    )


def _derive_gate_decision(
    compiled: CompiledCodingArena,
    eval_res: ArenaEvalResult,
    *,
    spec: CodingSessionSpec,
    repair_res: Optional[ArenaRepairResult] = None,
    fir_res: Optional[ArenaFirResult] = None,
    norm_snap: Optional[NormativeStateSnapshot] = None,
) -> GateDecision:
    submit_idx = compiled.node_index["submit_ready"]
    submit_node = eval_res.nodes[submit_idx]
    submit_reasons = decode_reasons(submit_node.reasons)

    blocked_reasons: List[str] = []
    has_penumbra = False
    has_conflict = False
    has_defeated = False
    for nd_res in eval_res.nodes:
        if nd_res.label in ("submit_ready", "norm_gate_eligible", "norm_hyp_resolved"):
            continue
        if nd_res.status == "penumbraOpen":
            has_penumbra = True
            blocked_reasons.append(f"{nd_res.label}:penumbraOpen(PENUMBRA)")
        elif nd_res.status == "conflictUndecided":
            has_conflict = True
            blocked_reasons.append(f"{nd_res.label}:conflictUndecided(ATTACKED)")
        elif nd_res.status == "defeated":
            has_defeated = True
            r_names = ",".join(decode_reasons(nd_res.reasons))
            blocked_reasons.append(f"{nd_res.label}:defeated({r_names})")

    if submit_node.status == "established":
        action = "ALLOW_SUBMIT"
    elif has_conflict:
        action = "REPAIR_HYPOTHESES"
    elif has_defeated:
        action = "BLOCK_SUBMIT"
    elif spec.ambiguous_spec or eval_res.nodes[compiled.node_index["norm_spec_clear"]].status == "penumbraOpen":
        action = "REQUEST_CLARIFICATION"
    elif has_penumbra:
        action = "DEFER_PENUMBRA"
    else:
        action = "BLOCK_SUBMIT"

    return GateDecision(
        action=action,
        submit_ready_status=submit_node.status,
        submit_ready_reasons=submit_reasons,
        eval_result=eval_res,
        blocked_reasons=blocked_reasons,
        nodes=compiled.nodes,
        node_index=compiled.node_index,
        power_mask=compiled.power_mask,
        immunity_mask=compiled.immunity_mask,
        normative_snapshot=norm_snap,
        fir_result=fir_res,
        repair_result=repair_res,
    )


class SoundCodingGate:
    """High-level verifier coupling coding sessions to C17 `libsound`, SEARU, KanLock, and FIR1."""

    def __init__(
        self,
        *,
        sound_bin: Path = DEFAULT_SOUND_BIN,
        repo_root: Path = REPO_ROOT,
        backend: str = "auto",
    ) -> None:
        self.sound_bin = Path(sound_bin)
        self.repo_root = Path(repo_root)
        self.backend = backend
        self.schema_hash = load_schema_hash(self.repo_root)

    def encode_session(self, spec: CodingSessionSpec) -> Tuple[CompiledCodingArena, bytes]:
        """Compile and FNV-1a Merkle-seal a 16,384B `SoundArena` image for `spec`."""
        compiled = compile_coding_session(spec)
        arena_bytes = encode_arena(
            compiled.nodes,
            schema_hash=self.schema_hash,
            power_mask=compiled.power_mask,
            immunity_mask=compiled.immunity_mask,
            duty_pos_mask=compiled.duty_pos_mask,
            duty_neg_mask=compiled.duty_neg_mask,
            voidable_mask=compiled.voidable_mask,
            rescission_power_mask=compiled.rescission_power_mask,
        )
        return compiled, arena_bytes

    def evaluate(
        self,
        spec: CodingSessionSpec,
        *,
        backend: Optional[str] = None,
    ) -> GateDecision:
        """Evaluate `spec` using `"ffi"`, `"cli"`, `"auto"`, or `"portable"` backend."""
        eff_backend = backend or self.backend
        compiled = compile_coding_session(spec)
        if eff_backend == "portable":
            eval_res = eval_arena_nodes_portable(compiled.nodes)
            return _derive_gate_decision(compiled, eval_res, spec=spec)

        _, arena_bytes = self.encode_session(spec)
        eval_res = verify_arena_bytes(arena_bytes, self.sound_bin, backend=eff_backend)
        return _derive_gate_decision(compiled, eval_res, spec=spec)

    def resolve_hypothesis_conflict(
        self,
        spec: CodingSessionSpec,
        *,
        seed: Optional[str] = None,
        prog_name: str = "swe_repaired_gate",
    ) -> Tuple[GateDecision, ArenaRepairResult, ArenaFirResult]:
        """Run C17 SEARU conflict repair + `FIR1` authority lift under `KanLock` immunity."""
        compiled, arena_bytes = self.encode_session(spec)
        eval_before = verify_arena_bytes(arena_bytes, self.sound_bin, backend="ffi")
        seed_arg = seed if seed is not None else hex(DEFAULT_SEARU_SEED)
        repair_res = repair_arena_bytes(arena_bytes, self.sound_bin, seed=seed_arg, backend="ffi")
        fir_res = lift_fir_arena_bytes(
            arena_bytes,
            self.sound_bin,
            prog_name=prog_name,
            repaired=True,
            seed=seed_arg,
            backend="ffi",
        )
        decision = _derive_gate_decision(
            compiled,
            eval_before,
            spec=spec,
            repair_res=repair_res,
            fir_res=fir_res,
        )
        return decision, repair_res, fir_res

    def lift_submit_authority_fir(
        self,
        spec: CodingSessionSpec,
        *,
        prog_name: str = "swe_submit_gate",
    ) -> Tuple[GateDecision, ArenaFirResult]:
        """Lift a verified coding-session arena to a 2,144B `FIR1` quantum-ready gate."""
        compiled, arena_bytes = self.encode_session(spec)
        eval_res = verify_arena_bytes(arena_bytes, self.sound_bin, backend="ffi")
        fir_res = lift_fir_arena_bytes(
            arena_bytes,
            self.sound_bin,
            prog_name=prog_name,
            repaired=False,
            backend="ffi",
        )
        decision = _derive_gate_decision(compiled, eval_res, spec=spec, fir_res=fir_res)
        return decision, fir_res

    def provisional_admit_and_rescind_demo(
        self,
        spec: CodingSessionSpec,
        *,
        target_label: str = "submit_ready",
        lock_target: bool = False,
    ) -> Dict[str, object]:
        """Exercise Ring-0 `apply_declarative` (voidable) and `exercise_rescission` (with/without KanLock)."""
        compiled = compile_coding_session(spec)
        target_idx = compiled.node_index[target_label]
        target_bit = 1 << target_idx
        imm_mask = compiled.immunity_mask | (target_bit if lock_target else 0)
        arena_bytes = encode_arena(
            compiled.nodes,
            schema_hash=self.schema_hash,
            power_mask=compiled.power_mask | target_bit,
            immunity_mask=imm_mask,
            voidable_mask=compiled.voidable_mask,
            rescission_power_mask=compiled.rescission_power_mask,
        )

        u_hash = int.from_bytes(hashlib.blake2b(spec.task_id.encode("utf-8"), digest_size=8).digest(), "little")
        sealed_decl, decl_rc, snap_decl = apply_declarative_arena_bytes(
            arena_bytes,
            self.sound_bin,
            intent_node=target_idx,
            duty_slot=target_idx,
            in_constitutive_context=True,
            internal_will_matches=False,
            utterance_hash=u_hash,
        )
        r_hash = u_hash ^ 0xA5A55A5A0F0FF0F0
        sealed_resc, resc_rc, eval_after, snap_resc = exercise_rescission_arena_bytes(
            sealed_decl,
            self.sound_bin,
            target_intent_node=target_idx,
            duty_slot=target_idx,
            rescission_hash=r_hash,
        )
        return {
            "target_node": target_label,
            "target_index": target_idx,
            "lock_target": lock_target,
            "decl_rc": decl_rc,
            "resc_rc": resc_rc,
            "snap_after_declarative": snap_decl,
            "snap_after_rescission": snap_resc,
            "eval_after_rescission": eval_after,
            "sealed_bytes": sealed_resc,
        }
