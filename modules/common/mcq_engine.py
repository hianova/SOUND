"""Declarative Topic Pack validator, MCQ option compiler, and SOUND trap classifier."""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Set, Tuple

from .arena_codec import (
    FALLBACK_SCHEMA_HASH,
    SOUND_MAX_CLAUSES,
    ArenaEvalResult,
    ArenaNodeSpec,
    ArenaRepairResult,
    decode_reasons,
    encode_arena,
    repair_arena_bytes,
    verify_arena_bytes,
)
from .tripwires import detect_stem_polarity, scan_option_tripwires

NODE_ID_RE = re.compile(r"^[A-Za-z0-9_]{1,24}$")
ALLOWED_ROLES: Set[str] = {"statute_root", "fact_slot", "rule", "effect", "penumbra"}
ALLOWED_STATUSES: Set[str] = {
    "established",
    "defeated",
    "conflictUndecided",
    "penumbraOpen",
}
REQUIRED_PACK_FIELDS: Tuple[str, ...] = (
    "pack_id",
    "domain",
    "jurisdiction",
    "standard",
    "version",
    "title",
    "nodes",
    "universal_counterexamples",
    "confusable_pairs",
)


class PackValidationError(ValueError):
    """Raised when a declarative Topic Pack violates schema or 64-node budget rules."""


@dataclass(frozen=True)
class OptionDiagnosis:
    """Formal SOUND verification and trap classification for a single MCQ option."""

    label: str
    text: str
    target_node: str
    expect_status: str
    actual_status: str
    reasons: int
    reason_names: List[str]
    tripwires: Dict[str, List[str]]
    universal_counterexample_applied: bool
    counterpart_established: bool
    is_trap: bool
    trap_type: str
    repair_result: Optional[ArenaRepairResult] = None
    eval_result: Optional[ArenaEvalResult] = field(default=None, repr=False)


@dataclass(frozen=True)
class QuestionDiagnosis:
    """Formal diagnosis and answer selection across all options of one MCQ."""

    question_id: str
    pack_id: str
    stem: str
    stem_polarity: str
    inferred_polarity: str
    polarity_cues: List[str]
    polarity_consistent: bool
    predicted_answers: List[str]
    expected_answer: str
    options: List[OptionDiagnosis]

    @property
    def answer_exact_match(self) -> bool:
        return self.predicted_answers == [self.expected_answer]


def load_topic_pack(path: Path) -> Dict[str, Any]:
    """Load and validate a JSON declarative Topic Pack."""
    data = json.loads(path.read_text(encoding="utf-8"))
    validate_topic_pack(data)
    return data


def validate_topic_pack(pack: Dict[str, Any]) -> None:
    """Validate structure, ID uniqueness, 64-node reserve budget, and reference integrity."""
    for key in REQUIRED_PACK_FIELDS:
        if key not in pack:
            raise PackValidationError(f"Topic pack missing required field {key!r}")

    nodes = pack["nodes"]
    if not isinstance(nodes, list) or not (1 <= len(nodes) <= 62):
        count = len(nodes) if isinstance(nodes, list) else -1
        raise PackValidationError(
            f"Pack {pack.get('pack_id')!r} must define 1..62 nodes (leaves >=2 dynamic slots), got {count}"
        )

    ids: List[str] = []
    seen: Set[str] = set()
    id_to_node: Dict[str, Dict[str, Any]] = {}
    for nd in nodes:
        nid = nd.get("id")
        if not isinstance(nid, str) or not NODE_ID_RE.match(nid):
            raise PackValidationError(f"Invalid node ID {nid!r} (must match ^[A-Za-z0-9_]{{1,24}}$)")
        if nid in seen:
            raise PackValidationError(f"Duplicate node ID {nid!r} in pack {pack['pack_id']!r}")
        role = nd.get("role")
        if role not in ALLOWED_ROLES:
            raise PackValidationError(f"Node {nid!r} has invalid role {role!r}")
        clauses = nd.get("clauses", [])
        if not isinstance(clauses, list) or len(clauses) > SOUND_MAX_CLAUSES:
            raise PackValidationError(f"Node {nid!r} exceeds SOUND_MAX_CLAUSES={SOUND_MAX_CLAUSES}")
        seen.add(nid)
        ids.append(nid)
        id_to_node[nid] = nd

    for nd in nodes:
        nid = nd["id"]
        for c_idx, clause in enumerate(nd.get("clauses", [])):
            if not isinstance(clause, list):
                raise PackValidationError(f"Node {nid!r} clause[{c_idx}] must be a list of IDs")
            for ref in clause:
                if ref not in id_to_node:
                    raise PackValidationError(f"Node {nid!r} clause[{c_idx}] unknown ref {ref!r}")
        for ref in nd.get("presup", []):
            if ref not in id_to_node:
                raise PackValidationError(f"Node {nid!r} presup unknown ref {ref!r}")
        for ref in nd.get("attacked_by", []):
            if ref not in id_to_node:
                raise PackValidationError(f"Node {nid!r} attacked_by unknown ref {ref!r}")

    uc = pack["universal_counterexamples"]
    if not isinstance(uc, dict):
        raise PackValidationError("universal_counterexamples must be an object")
    for target, extra_facts in uc.items():
        if target not in id_to_node:
            raise PackValidationError(f"universal_counterexamples unknown target {target!r}")
        if not isinstance(extra_facts, list) or any(f not in id_to_node for f in extra_facts):
            raise PackValidationError(
                f"universal_counterexamples[{target!r}] contains unknown node ref"
            )

    pairs = pack["confusable_pairs"]
    if not isinstance(pairs, list):
        raise PackValidationError("confusable_pairs must be a list of 2-element lists")
    for pair in pairs:
        if not isinstance(pair, list) or len(pair) != 2:
            raise PackValidationError(f"confusable_pairs entry must have length 2: {pair!r}")
        if pair[0] not in id_to_node or pair[1] not in id_to_node:
            raise PackValidationError(f"confusable_pairs contains unknown ref: {pair!r}")


def _mask_for_ids(refs: Sequence[str], id_to_idx: Dict[str, int]) -> int:
    mask = 0
    for ref in refs:
        if ref not in id_to_idx:
            raise KeyError(f"Unknown node ID in claim/pack reference: {ref!r}")
        mask |= 1 << id_to_idx[ref]
    return mask


def _confusable_counterparts(pack: Dict[str, Any], target_id: str) -> List[str]:
    out: List[str] = []
    for a, b in pack.get("confusable_pairs", []):
        if a == target_id:
            out.append(b)
        elif b == target_id:
            out.append(a)
    return out


def _classify_trap(
    *,
    actual_status: str,
    expect_status: str,
    reasons: int,
    option: Dict[str, Any],
    tripwires: Dict[str, List[str]],
    universal_counterexample_applied: bool,
    counterpart_established: bool,
) -> Tuple[bool, str]:
    if actual_status == expect_status:
        return False, "VALID_STATEMENT"

    if actual_status == "penumbraOpen":
        return True, "TRAP_PENUMBRA_OVERCLAIM"
    if actual_status == "conflictUndecided":
        return True, "TRAP_CONFLICT_UNRESOLVED"
    if (reasons & 0x08) or (reasons & 0x10):
        return True, "TRAP_HUME_IS_TO_OUGHT"
    if (reasons & 0x40) and (
        bool(option.get("universal_scope", False))
        or universal_counterexample_applied
        or (bool(tripwires["absolutizers"]) and not counterpart_established)
    ):
        return True, "TRAP_EXCEPTION_IGNORED"
    if (reasons & 0x40) and counterpart_established:
        return True, "TRAP_CONCEPT_OR_ROLE_SWAP"
    if (reasons & 0x20) or (reasons & 0x04):
        return True, "TRAP_UNVERIFIED_PRESUP"
    if reasons & 0x40:
        return True, "TRAP_EXCEPTION_IGNORED"
    if reasons & 0x02:
        return True, "TRAP_REVOKED_RULE"
    if reasons & 0x80:
        return True, (
            "TRAP_CONCEPT_OR_ROLE_SWAP" if counterpart_established else "TRAP_UNSUPPORTED_CLAIM"
        )
    return True, "TRAP_UNSUPPORTED_CLAIM"


def evaluate_option(
    pack: Dict[str, Any],
    question: Dict[str, Any],
    option: Dict[str, Any],
    sound_bin: Path,
    schema_hash: int = FALLBACK_SCHEMA_HASH,
) -> OptionDiagnosis:
    """Compile an MCQ option against its Domain Pack, verify via `build/sound`, and diagnose traps."""
    validate_topic_pack(pack)
    base_nodes: List[Dict[str, Any]] = pack["nodes"]
    id_to_idx: Dict[str, int] = {nd["id"]: idx for idx, nd in enumerate(base_nodes)}

    opt_text = str(option["text"])
    tripwires = scan_option_tripwires(opt_text)

    active_facts: Set[str] = set(question.get("stem_facts", [])) | set(
        option.get("scenario_facts", [])
    )
    unverified_facts: Set[str] = set(option.get("unverified_facts", []))

    claim = option["claim"]
    claim_kind = claim["kind"]
    claim_target = claim.get("target")

    universal_counterexample_applied = False
    has_scoped_exc = bool(tripwires["exception_scoped"]) or bool(
        option.get("explicit_exception_scoped", False)
    )
    want_universal = bool(option.get("universal_scope", False)) or (
        claim_kind == "assert_node" and bool(tripwires["absolutizers"]) and not has_scoped_exc
    )
    uc_map = pack.get("universal_counterexamples", {})
    if want_universal and isinstance(claim_target, str) and claim_target in uc_map:
        active_facts.update(uc_map[claim_target])
        universal_counterexample_applied = True

    specs: List[ArenaNodeSpec] = []
    for nd in base_nodes:
        nid = nd["id"]
        role = nd["role"]
        sort = nd.get("sort", "factual" if role == "fact_slot" else "normative")
        evid = nd.get("evid", "enacted_norm" if role == "statute_root" else "direct")
        aspect = nd.get("aspect", "state")
        constitutive = bool(nd.get("constitutive", False))
        revoked = bool(nd.get("revoked", False))
        penumbra = bool(nd.get("penumbra", role == "penumbra"))
        presup_mask = _mask_for_ids(nd.get("presup", []), id_to_idx)
        attack_mask = _mask_for_ids(nd.get("attacked_by", []), id_to_idx)

        if nid in unverified_facts:
            evid = "assumed"
            clause_masks = [0]
        elif role == "fact_slot":
            clause_masks = [0] if nid in active_facts else []
        else:
            clause_masks = [_mask_for_ids(c, id_to_idx) for c in nd.get("clauses", [])]

        specs.append(
            ArenaNodeSpec(
                label=nid,
                sort=sort,
                evid=evid,
                aspect=aspect,
                constitutive=constitutive,
                revoked=revoked,
                penumbra=penumbra,
                presup_mask=presup_mask,
                attack_mask=attack_mask,
                clauses=clause_masks,
            )
        )

    counterpart_lookup_id = ""
    if claim_kind == "assert_node":
        if claim_target not in id_to_idx:
            raise KeyError(f"assert_node target {claim_target!r} not in pack {pack['pack_id']!r}")
        target_idx = id_to_idx[claim_target]
        target_label = claim_target
        counterpart_lookup_id = claim_target
        expect_status = claim.get("expect_status", "established")
    elif claim_kind == "hume_leap":
        from_facts = claim.get("from_facts", [])
        if not from_facts:
            raise ValueError("hume_leap claim requires non-empty 'from_facts'")
        for fid in from_facts:
            if fid not in id_to_idx:
                raise KeyError(f"hume_leap unknown fact {fid!r}")
            if specs[id_to_idx[fid]].sort != "factual":
                raise ValueError(f"hume_leap from_facts entry {fid!r} must have sort='factual'")
        target_idx = len(specs)
        target_label = "opt_claim"
        expect_status = claim.get("expect_status", "established")
        specs.append(
            ArenaNodeSpec(
                label=target_label,
                sort="normative",
                evid="inferential",
                aspect="state",
                constitutive=False,
                clauses=[_mask_for_ids(from_facts, id_to_idx)],
            )
        )
    elif claim_kind == "composite_claim":
        requires = claim.get("requires", [])
        attacked_by = claim.get("attacked_by", [])
        target_idx = len(specs)
        target_label = "opt_claim"
        counterpart_lookup_id = str(claim.get("counterpart_of", ""))
        expect_status = claim.get("expect_status", "established")
        req_mask = _mask_for_ids(requires, id_to_idx)
        specs.append(
            ArenaNodeSpec(
                label=target_label,
                sort="normative",
                evid="direct",
                aspect="state",
                constitutive=True,
                presup_mask=req_mask,
                attack_mask=_mask_for_ids(attacked_by, id_to_idx),
                clauses=[req_mask],
            )
        )
    else:
        raise ValueError(f"Unsupported claim kind: {claim_kind!r}")

    if expect_status not in ALLOWED_STATUSES:
        raise ValueError(f"Invalid expect_status: {expect_status!r}")

    arena_bytes = encode_arena(specs, schema_hash=schema_hash)
    eval_res = verify_arena_bytes(arena_bytes, sound_bin)
    target_res = eval_res.nodes[target_idx]

    repair_res: Optional[ArenaRepairResult] = None
    if target_res.status == "conflictUndecided":
        repair_res = repair_arena_bytes(arena_bytes, sound_bin)

    counterpart_established = False
    if counterpart_lookup_id:
        for cp_id in _confusable_counterparts(pack, counterpart_lookup_id):
            if cp_id in id_to_idx and eval_res.nodes[id_to_idx[cp_id]].status == "established":
                counterpart_established = True
                break

    is_trap, trap_type = _classify_trap(
        actual_status=target_res.status,
        expect_status=expect_status,
        reasons=target_res.reasons,
        option=option,
        tripwires=tripwires,
        universal_counterexample_applied=universal_counterexample_applied,
        counterpart_established=counterpart_established,
    )

    return OptionDiagnosis(
        label=str(option["label"]),
        text=opt_text,
        target_node=target_label,
        expect_status=expect_status,
        actual_status=target_res.status,
        reasons=target_res.reasons,
        reason_names=decode_reasons(target_res.reasons),
        tripwires=tripwires,
        universal_counterexample_applied=universal_counterexample_applied,
        counterpart_established=counterpart_established,
        is_trap=is_trap,
        trap_type=trap_type,
        repair_result=repair_res,
        eval_result=eval_res,
    )


def solve_question(
    pack: Dict[str, Any],
    question: Dict[str, Any],
    sound_bin: Path,
    schema_hash: int = FALLBACK_SCHEMA_HASH,
) -> QuestionDiagnosis:
    """Verify every option in an MCQ via `build/sound` and pick answers by stem polarity."""
    stem = str(question["stem"])
    declared_polarity = str(question["stem_polarity"])
    if declared_polarity not in ("select_correct", "select_incorrect"):
        raise ValueError(f"Invalid stem_polarity: {declared_polarity!r}")

    pol_info = detect_stem_polarity(stem)
    inferred_polarity = str(pol_info["inferred_polarity"])
    polarity_consistent = inferred_polarity == declared_polarity

    opt_diags = [
        evaluate_option(pack, question, opt, sound_bin, schema_hash)
        for opt in question["options"]
    ]

    if declared_polarity == "select_correct":
        predicted_answers = [o.label for o in opt_diags if not o.is_trap]
    else:
        predicted_answers = [o.label for o in opt_diags if o.is_trap]

    return QuestionDiagnosis(
        question_id=str(question["id"]),
        pack_id=str(pack["pack_id"]),
        stem=stem,
        stem_polarity=declared_polarity,
        inferred_polarity=inferred_polarity,
        polarity_cues=list(pol_info["matched_cues"]),
        polarity_consistent=polarity_consistent,
        predicted_answers=predicted_answers,
        expected_answer=str(question["answer"]),
        options=opt_diags,
    )


def discover_packs(modules_root: Path) -> Dict[str, Tuple[Path, Dict[str, Any]]]:
    """Discover and validate all Topic Packs under `modules/*/packs/*.json`."""
    packs: Dict[str, Tuple[Path, Dict[str, Any]]] = {}
    for p in sorted(modules_root.glob("*/packs/*.json")):
        pack = load_topic_pack(p)
        pid = str(pack["pack_id"])
        if pid in packs:
            raise PackValidationError(f"Duplicate pack_id {pid!r} across {packs[pid][0]} and {p}")
        packs[pid] = (p, pack)
    return packs


def discover_benchmarks(modules_root: Path, domain: str = "all") -> List[Path]:
    """Discover benchmark JSON files for all domains or a single domain."""
    if domain == "all":
        return sorted(modules_root.glob("*/benchmarks/*.json"))
    return sorted((modules_root / domain / "benchmarks").glob("*.json"))
