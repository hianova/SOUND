"""Lore Pack validator, offline continuity auditor, and fragmented item lore compiler."""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Set, Tuple

from modules.common.arena_codec import (
    FALLBACK_SCHEMA_HASH,
    SOUND_MAX_CLAUSES,
    ArenaEvalResult,
    ArenaNodeSpec,
    ArenaRepairResult,
    encode_arena,
    repair_arena_bytes,
    verify_arena_bytes,
)

NODE_ID_RE = re.compile(r"^[A-Za-z0-9_]{1,24}$")
ALLOWED_ROLES: Set[str] = {"statute_root", "fact_slot", "rule", "effect", "penumbra"}
REQUIRED_LORE_FIELDS: Tuple[str, ...] = (
    "pack_id",
    "domain",
    "version",
    "title",
    "nodes",
    "public_facts",
    "full_truth_facts",
    "intentional_rashomon",
    "items",
    "npcs",
)


class LorePackError(ValueError):
    """Raised when a Narrative Lore Pack violates schema or SOUND arena constraints."""


@dataclass(frozen=True)
class LoreAuditReport:
    """Offline continuity & contradiction audit report for a Narrative Lore Pack."""

    pack_id: str
    title: str
    node_count: int
    intentional_rashomon_active: List[Tuple[str, str]]
    accidental_contradictions: List[str]
    full_truth_conflict_free: bool
    active_penumbra_hooks: List[str]
    public_eval: ArenaEvalResult
    full_truth_eval: ArenaEvalResult
    searu_repair: Optional[ArenaRepairResult] = None

    @property
    def clean(self) -> bool:
        return len(self.accidental_contradictions) == 0 and self.full_truth_conflict_free


@dataclass(frozen=True)
class ItemLoreCard:
    """Graph-projected Souls-like Item Lore Card backed by SOUND differential proof."""

    item_id: str
    name: str
    slot_kind: str
    unlocks_facts: List[str]
    combo_facts: List[str]
    newly_established: List[str]
    defeated_beliefs: List[str]
    resolved_rashomon: List[str]
    projected_facts: List[str]
    unverified_rumors: List[str]
    style_constraint: str
    forbidden_spoilers: List[str]
    rendered_card_text: str


def load_lore_pack(path: Path) -> Dict[str, Any]:
    """Load and validate a Narrative Lore Pack JSON."""
    data = json.loads(path.read_text(encoding="utf-8"))
    validate_lore_pack(data)
    return data


def validate_lore_pack(pack: Dict[str, Any]) -> None:
    """Validate structure, 62-node cap (leaves dynamic headroom), IDs, items, and NPCs."""
    for key in REQUIRED_LORE_FIELDS:
        if key not in pack:
            raise LorePackError(f"Lore pack missing required field {key!r}")
    if pack["domain"] != "narrative":
        raise LorePackError(f"Expected domain='narrative', got {pack['domain']!r}")

    nodes = pack["nodes"]
    if not isinstance(nodes, list) or not (1 <= len(nodes) <= 62):
        count = len(nodes) if isinstance(nodes, list) else -1
        raise LorePackError(f"Lore pack must contain 1..62 nodes, got {count}")

    id_to_node: Dict[str, Dict[str, Any]] = {}
    for nd in nodes:
        nid = nd.get("id")
        if not isinstance(nid, str) or not NODE_ID_RE.match(nid):
            raise LorePackError(f"Invalid node ID {nid!r} (must match ^[A-Za-z0-9_]{{1,24}}$)")
        if nid in id_to_node:
            raise LorePackError(f"Duplicate node ID {nid!r}")
        if nd.get("role") not in ALLOWED_ROLES:
            raise LorePackError(f"Node {nid!r} has invalid role {nd.get('role')!r}")
        clauses = nd.get("clauses", [])
        if not isinstance(clauses, list) or len(clauses) > SOUND_MAX_CLAUSES:
            raise LorePackError(f"Node {nid!r} exceeds SOUND_MAX_CLAUSES={SOUND_MAX_CLAUSES}")
        id_to_node[nid] = nd

    def _require_ref(ref: str, ctx: str) -> None:
        if ref not in id_to_node:
            raise LorePackError(f"Unknown node reference {ref!r} in {ctx}")

    for nd in nodes:
        nid = nd["id"]
        for c_idx, clause in enumerate(nd.get("clauses", [])):
            for ref in clause:
                _require_ref(ref, f"{nid}.clauses[{c_idx}]")
        for ref in nd.get("presup", []):
            _require_ref(ref, f"{nid}.presup")
        for ref in nd.get("attacked_by", []):
            _require_ref(ref, f"{nid}.attacked_by")

    for ref in pack["public_facts"]:
        _require_ref(ref, "public_facts")
    for ref in pack["full_truth_facts"]:
        _require_ref(ref, "full_truth_facts")
    for pair in pack["intentional_rashomon"]:
        if not isinstance(pair, list) or len(pair) != 2:
            raise LorePackError(f"intentional_rashomon entry must be 2-element list: {pair!r}")
        _require_ref(pair[0], "intentional_rashomon")
        _require_ref(pair[1], "intentional_rashomon")
    for _gflag, lfact in pack.get("ledger_imports", {}).items():
        _require_ref(str(lfact), f"ledger_imports[{_gflag!r}]")

    seen_items: Set[str] = set()
    for it in pack["items"]:
        iid = it["item_id"]
        if iid in seen_items:
            raise LorePackError(f"Duplicate item_id {iid!r}")
        seen_items.add(iid)
        for ref in it.get("unlocks_facts", []):
            _require_ref(ref, f"item[{iid}].unlocks_facts")
        for ref in it.get("combo_facts", []):
            _require_ref(ref, f"item[{iid}].combo_facts")
        for ref in it.get("rumor_nodes", []):
            _require_ref(ref, f"item[{iid}].rumor_nodes")
        for ref in it.get("penumbra_nodes", []):
            _require_ref(ref, f"item[{iid}].penumbra_nodes")

    seen_npcs: Set[str] = set()
    for npc in pack["npcs"]:
        nid = npc["npc_id"]
        if nid in seen_npcs:
            raise LorePackError(f"Duplicate npc_id {nid!r}")
        seen_npcs.add(nid)
        for ref in npc.get("known_facts", []):
            _require_ref(ref, f"npc[{nid}].known_facts")
        for ref in npc.get("rumor_facts", []):
            _require_ref(ref, f"npc[{nid}].rumor_facts")
        for ref in npc.get("power_nodes", []):
            _require_ref(ref, f"npc[{nid}].power_nodes")
        for ref in npc.get("immunity_nodes", []):
            _require_ref(ref, f"npc[{nid}].immunity_nodes")


def _mask_for_ids(refs: Iterable[str], id_to_idx: Dict[str, int]) -> int:
    mask = 0
    for ref in refs:
        mask |= 1 << id_to_idx[ref]
    return mask


def build_world_arena(
    pack: Dict[str, Any],
    active_facts: Iterable[str],
    *,
    rumor_facts: Iterable[str] = (),
    unverified_facts: Iterable[str] = (),
    power_nodes: Iterable[str] = (),
    immunity_nodes: Iterable[str] = (),
    schema_hash: int = FALLBACK_SCHEMA_HASH,
) -> Tuple[bytes, Dict[str, int]]:
    """Compile a Lore Pack + epistemic fact/authority masks into a sealed 16,384B SoundArena."""
    validate_lore_pack(pack)
    nodes = pack["nodes"]
    id_to_idx: Dict[str, int] = {nd["id"]: idx for idx, nd in enumerate(nodes)}

    active_set = set(active_facts)
    rumor_set = set(rumor_facts) - active_set
    unverified_set = set(unverified_facts)

    specs: List[ArenaNodeSpec] = []
    for nd in nodes:
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

        if role == "fact_slot":
            if nid in unverified_set:
                evid = "assumed"
                clauses = [0]
            elif nid in rumor_set:
                evid = "reportative"
                clauses = [0]
            elif nid in active_set:
                evid = "direct"
                clauses = [0]
            else:
                evid = "direct"
                clauses = []
        else:
            clauses = [_mask_for_ids(c, id_to_idx) for c in nd.get("clauses", [])]

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
                clauses=clauses,
            )
        )

    arena = encode_arena(
        specs,
        schema_hash=schema_hash,
        power_mask=_mask_for_ids(power_nodes, id_to_idx),
        immunity_mask=_mask_for_ids(immunity_nodes, id_to_idx),
    )
    return arena, id_to_idx


def audit_lore_pack(
    pack: Dict[str, Any],
    sound_bin: Path,
    schema_hash: int = FALLBACK_SCHEMA_HASH,
) -> LoreAuditReport:
    """Run SOUND verification on baseline public lore and full-truth resolution states."""
    validate_lore_pack(pack)

    pub_arena, _ = build_world_arena(
        pack, pack["public_facts"], schema_hash=schema_hash
    )
    pub_eval = verify_arena_bytes(pub_arena, sound_bin)

    truth_arena, _ = build_world_arena(
        pack, pack["full_truth_facts"], schema_hash=schema_hash
    )
    truth_eval = verify_arena_bytes(truth_arena, sound_bin)

    pub_conflicts = [nd.label for nd in pub_eval.nodes if nd.status == "conflictUndecided"]
    pub_conflict_set = set(pub_conflicts)

    declared_nodes: Set[str] = set()
    active_rashomon: List[Tuple[str, str]] = []
    for pair in pack.get("intentional_rashomon", []):
        if pair[0] in pub_conflict_set and pair[1] in pub_conflict_set:
            active_rashomon.append((pair[0], pair[1]))
            declared_nodes.update(pair)

    accidental_pub = [nid for nid in pub_conflicts if nid not in declared_nodes]
    truth_conflicts = [nd.label for nd in truth_eval.nodes if nd.status == "conflictUndecided"]

    accidental_combined: List[str] = list(dict.fromkeys(accidental_pub + truth_conflicts))
    searu_repair: Optional[ArenaRepairResult] = None
    if accidental_pub:
        searu_repair = repair_arena_bytes(pub_arena, sound_bin)
    elif truth_conflicts:
        searu_repair = repair_arena_bytes(truth_arena, sound_bin)

    penumbra_hooks = [nd.label for nd in truth_eval.nodes if nd.status == "penumbraOpen"]

    return LoreAuditReport(
        pack_id=str(pack["pack_id"]),
        title=str(pack["title"]),
        node_count=len(pack["nodes"]),
        intentional_rashomon_active=active_rashomon,
        accidental_contradictions=accidental_combined,
        full_truth_conflict_free=(len(truth_conflicts) == 0),
        active_penumbra_hooks=penumbra_hooks,
        public_eval=pub_eval,
        full_truth_eval=truth_eval,
        searu_repair=searu_repair,
    )


def _render_item_card_text(
    item: Dict[str, Any],
    id_to_meta: Dict[str, Dict[str, Any]],
    *,
    newly_established: List[str],
    defeated_beliefs: List[str],
    resolved_rashomon: List[str],
    unverified_rumors: List[str],
    forbidden_spoilers: List[str],
) -> str:
    def _title(nid: str) -> str:
        nd = id_to_meta.get(nid, {})
        return f"{nd.get('title', nid)} ({nid})"

    lines: List[str] = [
        f"【{item['name']}】[{item.get('slot_kind', '道具')}]",
        f"  {item['base_flavor']}",
    ]
    if newly_established:
        joined = "、".join(_title(n) for n in newly_established)
        lines.append(f"  [物證投影] 證實：{joined}")
    if defeated_beliefs:
        joined = "、".join(_title(n) for n in defeated_beliefs)
        lines.append(f"  [證詞反轉] 擊敗舊說（ATTACKED）：{joined}")
    if resolved_rashomon:
        joined = "、".join(_title(n) for n in resolved_rashomon)
        lines.append(f"  [羅生門破除] 勝出確立：{joined}")
    if item.get("combo_note"):
        lines.append(f"  [拼湊條件] {item['combo_note']}")
    if unverified_rumors:
        rumor_txt = item.get("rumor_flavor") or "、".join(_title(n) for n in unverified_rumors)
        lines.append(f"  [街巷傳聞（未證實）] {rumor_txt}")
    if forbidden_spoilers:
        hint = item.get("cryptic_hint") or "真相仍在未定之灰霧中，切忌言明結局。"
        lines.append(
            f"  [半影謎霧｜禁止劇透：{', '.join(forbidden_spoilers)}] {hint}"
        )
    return "\n".join(lines)


def compile_item_cards(
    pack: Dict[str, Any],
    sound_bin: Path,
    schema_hash: int = FALLBACK_SCHEMA_HASH,
) -> List[ItemLoreCard]:
    """Project each collectible item against public lore baseline into certified ItemLoreCards."""
    validate_lore_pack(pack)
    id_to_meta: Dict[str, Dict[str, Any]] = {nd["id"]: nd for nd in pack["nodes"]}

    base_facts = list(pack["public_facts"])
    base_arena, _ = build_world_arena(pack, base_facts, schema_hash=schema_hash)
    base_eval = verify_arena_bytes(base_arena, sound_bin)
    base_status: Dict[str, str] = {nd.label: nd.status for nd in base_eval.nodes}

    cards: List[ItemLoreCard] = []
    for item in pack["items"]:
        unlocks = list(item.get("unlocks_facts", []))
        combo = list(item.get("combo_facts", []))
        merged_facts = list(dict.fromkeys(base_facts + unlocks + combo))

        item_arena, _ = build_world_arena(pack, merged_facts, schema_hash=schema_hash)
        item_eval = verify_arena_bytes(item_arena, sound_bin)
        item_node_map = {nd.label: nd for nd in item_eval.nodes}

        newly_established: List[str] = []
        defeated_beliefs: List[str] = []
        resolved_rashomon: List[str] = []
        projected_facts: List[str] = []

        for nd in item_eval.nodes:
            prev_st = base_status[nd.label]
            cur_st = nd.status
            if cur_st == "established":
                if nd.label in unlocks:
                    projected_facts.append(nd.label)
                if prev_st != "established":
                    newly_established.append(nd.label)
                    if prev_st == "conflictUndecided":
                        resolved_rashomon.append(nd.label)
            elif cur_st == "defeated" and prev_st in ("established", "conflictUndecided"):
                if nd.reasons & 0x40:
                    defeated_beliefs.append(nd.label)

        unverified_rumors = [
            rid
            for rid in item.get("rumor_nodes", [])
            if rid in item_node_map and item_node_map[rid].status != "established"
        ]
        forbidden_spoilers = [
            pid
            for pid in item.get("penumbra_nodes", [])
            if pid in item_node_map and item_node_map[pid].status == "penumbraOpen"
        ]
        style_constraint = "CRYPTIC_HINT_ONLY" if forbidden_spoilers else "CONCRETE_ALLOWED"

        rendered = _render_item_card_text(
            item,
            id_to_meta,
            newly_established=newly_established,
            defeated_beliefs=defeated_beliefs,
            resolved_rashomon=resolved_rashomon,
            unverified_rumors=unverified_rumors,
            forbidden_spoilers=forbidden_spoilers,
        )

        cards.append(
            ItemLoreCard(
                item_id=str(item["item_id"]),
                name=str(item["name"]),
                slot_kind=str(item.get("slot_kind", "道具")),
                unlocks_facts=unlocks,
                combo_facts=combo,
                newly_established=newly_established,
                defeated_beliefs=defeated_beliefs,
                resolved_rashomon=resolved_rashomon,
                projected_facts=projected_facts,
                unverified_rumors=unverified_rumors,
                style_constraint=style_constraint,
                forbidden_spoilers=forbidden_spoilers,
                rendered_card_text=rendered,
            )
        )

    return cards
