"""Sustainable fixed-footprint multi-NPC runtime driven by SOUND and FIR1 authority gates."""

from __future__ import annotations

import hashlib
import re
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Set, Tuple

from modules.common.arena_codec import (
    FALLBACK_SCHEMA_HASH,
    SOUND_ARENA_BYTES,
    SOUND_ERR_MAP,
    ArenaEvalResult,
    ArenaFirResult,
    apply_declarative_arena_bytes,
    decode_reasons,
    exercise_rescission_arena_bytes,
    fnv1a_64,
    lift_fir_arena_bytes,
    read_normative_state,
    repair_arena_bytes,
    verify_arena_bytes,
)
from modules.common.tripwires import scan_option_tripwires

from .lore_engine import build_world_arena, validate_lore_pack
from .world_ledger import WorldLedger

_COUNTERFACTUAL_RE = re.compile(
    r"(假設|如果|若非|假如|要是|若我|suppose|what\s+if|hypothetically)",
    re.IGNORECASE,
)


@dataclass
class NpcSessionState:
    """Bounded per-NPC epistemic & Hohfeld capability state (no unbounded chat log)."""

    npc_id: str
    name: str
    role_title: str
    known_facts: Set[str]
    rumor_facts: Set[str]
    power_nodes: Set[str]
    immunity_nodes: Set[str]
    searu_seed: str
    phys_history: List[Dict[str, Any]] = field(default_factory=list)
    arena_snapshot: Optional[bytes] = None


@dataclass(frozen=True)
class SpeechAct:
    """Grounded dialogue & authority decision returned by SOUND for one player turn."""

    npc_id: str
    action_kind: str
    target_node: str
    stance: str
    sound_status: str
    sound_reasons: int
    allowed_nodes: List[str]
    forbidden_spoilers: List[str]
    state_delta: Dict[str, Any]
    rendered_dialogue: str
    searu_leaning_node: Optional[str] = None


class NarrativeWorldRuntime:
    """Stateful narrative & NPC interaction kernel backed by `./build/sound`."""

    def __init__(
        self,
        pack: Dict[str, Any],
        sound_bin: Path,
        schema_hash: int = FALLBACK_SCHEMA_HASH,
    ) -> None:
        validate_lore_pack(pack)
        self.pack = pack
        self.sound_bin = sound_bin
        self.schema_hash = schema_hash
        self.nodes_by_id: Dict[str, Dict[str, Any]] = {
            nd["id"]: nd for nd in pack["nodes"]
        }
        self.items_by_id: Dict[str, Dict[str, Any]] = {
            it["item_id"]: it for it in pack["items"]
        }
        self.npcs: Dict[str, NpcSessionState] = {}
        for npc in pack["npcs"]:
            nid = str(npc["npc_id"])
            self.npcs[nid] = NpcSessionState(
                npc_id=nid,
                name=str(npc["name"]),
                role_title=str(npc["role_title"]),
                known_facts=set(npc.get("known_facts", [])),
                rumor_facts=set(npc.get("rumor_facts", [])),
                power_nodes=set(npc.get("power_nodes", [])),
                immunity_nodes=set(npc.get("immunity_nodes", [])),
                searu_seed=str(npc.get("searu_seed", "0x53454152555F3031")),
            )

    def _get_npc(self, npc_id: str) -> NpcSessionState:
        if npc_id not in self.npcs:
            raise KeyError(f"Unknown NPC {npc_id!r}")
        return self.npcs[npc_id]

    def _node_title(self, node_id: str) -> str:
        nd = self.nodes_by_id.get(node_id, {})
        return str(nd.get("title", node_id))

    def _build_fresh_npc_arena(
        self,
        npc: NpcSessionState,
        *,
        extra_facts: Iterable[str] = (),
    ) -> Tuple[bytes, Dict[str, int]]:
        facts = set(npc.known_facts) | set(extra_facts)
        return build_world_arena(
            self.pack,
            facts,
            rumor_facts=npc.rumor_facts - facts,
            power_nodes=npc.power_nodes,
            immunity_nodes=npc.immunity_nodes,
            schema_hash=self.schema_hash,
        )

    def _refresh_arena_snapshot_facts(self, npc: NpcSessionState) -> bytes:
        """Rebuild node fact slots after `known_facts` changes while keeping C17 PhysLog/norm state."""
        fresh, _ = self._build_fresh_npc_arena(npc)
        prev = npc.arena_snapshot
        if prev is None:
            return fresh
        buf = bytearray(fresh)
        # Preserve Cell 1 (SoundNormative64) masks merged with fresh penumbra/power/immunity
        p_rev, p_pen, p_dp, p_dn, p_pow, p_imm, p_void, p_resc = struct.unpack_from("<8Q", prev, 64)
        _, f_pen, _, _, f_pow, f_imm, _, _ = struct.unpack_from("<8Q", fresh, 64)
        struct.pack_into(
            "<8Q",
            buf,
            64,
            p_rev,
            p_pen | f_pen,
            p_dp,
            p_dn,
            p_pow | f_pow,
            p_imm | f_imm,
            p_void,
            p_resc,
        )
        # Preserve Cells 136..199 (PhysLog entries: 64 x 64B = 4096B at offset 8704)
        buf[8704 : 8704 + 4096] = prev[8704 : 8704 + 4096]
        # Preserve Cell 0 phys_len, phys_head, phys_hash and re-seal Merkle root
        _mag, _ver, _nn, _ff, phys_len, phys_head, _rh, phys_hash, shash, _mr, _rt = struct.unpack_from(
            "<IIIIIIQQQQQ", prev, 0
        )
        merkle = fnv1a_64(bytes(buf[64:SOUND_ARENA_BYTES]))
        struct.pack_into(
            "<IIIIIIQQQQQ",
            buf,
            0,
            _mag,
            _ver,
            len(self.pack["nodes"]),
            _ff,
            phys_len,
            phys_head,
            0,
            phys_hash,
            shash,
            merkle,
            0,
        )
        sealed = bytes(buf)
        npc.arena_snapshot = sealed
        return sealed

    def _eval_npc(
        self,
        npc: NpcSessionState,
        *,
        extra_facts: Iterable[str] = (),
    ) -> Tuple[bytes, ArenaEvalResult, Dict[str, int]]:
        id_to_idx = {nd["id"]: idx for idx, nd in enumerate(self.pack["nodes"])}
        if npc.arena_snapshot is not None and not list(extra_facts):
            arena_bytes = npc.arena_snapshot
        else:
            arena_bytes, id_to_idx = self._build_fresh_npc_arena(npc, extra_facts=extra_facts)
        eval_res = verify_arena_bytes(arena_bytes, self.sound_bin)
        return arena_bytes, eval_res, id_to_idx

    def _arena_digest(self, npc: NpcSessionState) -> str:
        arena_bytes, _, _ = self._eval_npc(npc)
        return hashlib.sha256(arena_bytes).hexdigest()

    def _collect_spoilers(self, npc: NpcSessionState, eval_res: ArenaEvalResult) -> List[str]:
        spoilers: List[str] = []
        for nd in eval_res.nodes:
            if nd.status == "penumbraOpen":
                spoilers.append(nd.label)
            elif (
                self.nodes_by_id[nd.label]["role"] == "fact_slot"
                and nd.label not in npc.known_facts
            ):
                spoilers.append(nd.label)
        return spoilers

    def _collect_allowed(self, eval_res: ArenaEvalResult) -> List[str]:
        return [nd.label for nd in eval_res.nodes if nd.status == "established"]

    def ask_topic(self, npc_id: str, topic_node: str) -> SpeechAct:
        """Query an NPC's grounded belief on `topic_node` under epistemic fog & SEARU."""
        if topic_node not in self.nodes_by_id:
            raise KeyError(f"Unknown topic_node {topic_node!r}")
        npc = self._get_npc(npc_id)
        arena_bytes, eval_res, id_to_idx = self._eval_npc(npc)
        node_res = eval_res.nodes[id_to_idx[topic_node]]
        spoilers = self._collect_spoilers(npc, eval_res)
        allowed = self._collect_allowed(eval_res)
        title = self._node_title(topic_node)

        leaning_node: Optional[str] = None
        if node_res.status == "penumbraOpen":
            stance = "CRYPTIC_EVADE"
            if topic_node not in spoilers:
                spoilers.append(topic_node)
            dialogue = (
                f"{npc.role_title}{npc.name}壓低聲音：「關於『{title}』，誰也別想讓我說死……"
                f"霧裡聽見的未必是神諭。」"
            )
        elif node_res.status == "conflictUndecided":
            stance = "RASHOMON_TORN"
            rep = repair_arena_bytes(arena_bytes, self.sound_bin, seed=npc.searu_seed)
            for nd in eval_res.nodes:
                bit = 1 << nd.index
                if (eval_res.conflict_mask & bit) and (rep.cand & bit):
                    leaning_node = nd.label
                    break
            lean_txt = (
                f"內心姑且偏向『{self._node_title(leaning_node)}』"
                if leaning_node
                else "仍無法決斷"
            )
            dialogue = (
                f"{npc.role_title}{npc.name}皺眉嘆息：「兩邊證詞互相咬死，我說不準；"
                f"真要選一邊站，{lean_txt}。」"
            )
        elif node_res.status == "established":
            stance = "CONVINCED"
            dialogue = f"{npc.role_title}{npc.name}點頭道：「這點毫無疑問——『{title}』確係如此。」"
        else:
            rumor_hit = bool(
                set(self.nodes_by_id[topic_node].get("presup", [])) & npc.rumor_facts
            )
            if rumor_hit or (node_res.reasons & 0x04):
                stance = "SKEPTICAL_RUMOR"
                dialogue = (
                    f"{npc.role_title}{npc.name}擺手：「『{title}』只是酒館裡的空穴來風，"
                    f"拿不出實證我可不信。」"
                )
            elif node_res.reasons & 0x40:
                stance = "BELIEF_SHATTERED"
                dialogue = (
                    f"{npc.role_title}{npc.name}臉色驟變：「『{title}』的舊說已經被新物證徹徹底底推翻了……」"
                )
            else:
                stance = "UNVERIFIED_CLAIM"
                dialogue = f"{npc.role_title}{npc.name}搖頭：「關於『{title}』，我手上還沒見到足夠證據。」"

        return SpeechAct(
            npc_id=npc_id,
            action_kind="ask_topic",
            target_node=topic_node,
            stance=stance,
            sound_status=node_res.status,
            sound_reasons=node_res.reasons,
            allowed_nodes=allowed,
            forbidden_spoilers=spoilers,
            state_delta={},
            rendered_dialogue=dialogue,
            searu_leaning_node=leaning_node,
        )

    def present_item(self, npc_id: str, item_id: str) -> SpeechAct:
        """Show an item's evidence facts to an NPC, logging PhysLog and computing belief shift."""
        if item_id not in self.items_by_id:
            raise KeyError(f"Unknown item_id {item_id!r}")
        npc = self._get_npc(npc_id)
        item = self.items_by_id[item_id]

        _, pre_eval, _ = self._eval_npc(npc)
        pre_status = {nd.label: nd.status for nd in pre_eval.nodes}

        unlocks = list(item.get("unlocks_facts", []))
        newly_learned = [f for f in unlocks if f not in npc.known_facts]
        npc.known_facts.update(unlocks)
        npc.rumor_facts -= npc.known_facts
        if npc.arena_snapshot is not None:
            self._refresh_arena_snapshot_facts(npc)

        npc.phys_history.append(
            {
                "event_id": len(npc.phys_history) + 1,
                "kind": "present_item",
                "item_id": item_id,
                "unlocks_facts": unlocks,
            }
        )

        _, post_eval, _ = self._eval_npc(npc)
        newly_established = [
            nd.label
            for nd in post_eval.nodes
            if nd.status == "established" and pre_status[nd.label] != "established"
        ]
        defeated_beliefs = [
            nd.label
            for nd in post_eval.nodes
            if nd.status == "defeated"
            and pre_status[nd.label] in ("established", "conflictUndecided")
            and (nd.reasons & 0x40)
        ]

        primary_target = (
            defeated_beliefs[0]
            if defeated_beliefs
            else (newly_established[0] if newly_established else (unlocks[0] if unlocks else item_id))
        )
        post_map = {nd.label: nd for nd in post_eval.nodes}
        primary_res = post_map.get(primary_target)
        spoilers = self._collect_spoilers(npc, post_eval)

        if defeated_beliefs:
            stance = "BELIEF_SHATTERED"
            shattered_txt = "、".join(self._node_title(n) for n in defeated_beliefs)
            est_txt = "、".join(self._node_title(n) for n in newly_established)
            dialogue = (
                f"{npc.role_title}{npc.name}盯著『{item['name']}』震驚失聲："
                f"「這竟是真的……如此一來『{shattered_txt}』完全站不住腳！"
                f"（新確立：{est_txt}）」"
            )
        elif newly_established:
            stance = "CONVINCED"
            est_txt = "、".join(self._node_title(n) for n in newly_established)
            dialogue = (
                f"{npc.role_title}{npc.name}端詳『{item['name']}』後點頭："
                f"「我記下了：{est_txt}。」"
            )
        else:
            stance = "EVIDENCE_ABSORBED"
            dialogue = (
                f"{npc.role_title}{npc.name}檢視『{item['name']}』："
                f"「單憑這件證物還不足以推翻結論，尚缺拼圖另一半。」"
            )

        return SpeechAct(
            npc_id=npc_id,
            action_kind="present_item",
            target_node=primary_target,
            stance=stance,
            sound_status=primary_res.status if primary_res else "established",
            sound_reasons=primary_res.reasons if primary_res else 0,
            allowed_nodes=self._collect_allowed(post_eval),
            forbidden_spoilers=spoilers,
            state_delta={
                "item_id": item_id,
                "newly_learned_facts": newly_learned,
                "newly_established": newly_established,
                "defeated_beliefs": defeated_beliefs,
            },
            rendered_dialogue=dialogue,
        )

    def report_rumor(
        self, npc_id: str, fact_id: str, target_claim: str
    ) -> SpeechAct:
        """Introduce an uncorroborated rumor fact (REPORTATIVE) and check epistemic quarantine."""
        if fact_id not in self.nodes_by_id or target_claim not in self.nodes_by_id:
            raise KeyError("Unknown fact_id or target_claim")
        npc = self._get_npc(npc_id)
        if fact_id not in npc.known_facts:
            npc.rumor_facts.add(fact_id)

        _, eval_res, id_to_idx = self._eval_npc(npc)
        claim_res = eval_res.nodes[id_to_idx[target_claim]]
        spoilers = self._collect_spoilers(npc, eval_res)

        if claim_res.status == "established":
            stance = "CONVINCED"
            dialogue = (
                f"{npc.role_title}{npc.name}道：「這已非單純流言，我手頭自有物證佐證『{self._node_title(target_claim)}』。」"
            )
        else:
            stance = "SKEPTICAL_RUMOR"
            dialogue = (
                f"{npc.role_title}{npc.name}冷哼一聲：「『{self._node_title(target_claim)}』只是道聽塗說，"
                f"在見到實證前我不會採信。」"
            )

        return SpeechAct(
            npc_id=npc_id,
            action_kind="report_rumor",
            target_node=target_claim,
            stance=stance,
            sound_status=claim_res.status,
            sound_reasons=claim_res.reasons,
            allowed_nodes=self._collect_allowed(eval_res),
            forbidden_spoilers=spoilers,
            state_delta={"rumor_fact_recorded": fact_id},
            rendered_dialogue=dialogue,
        )

    def hypothetical_bluff(
        self, npc_id: str, utterance: str, target_node: str
    ) -> SpeechAct:
        """Quarantine hypothetical/irrealis player utterances without mutating world or PhysLog."""
        if target_node not in self.nodes_by_id:
            raise KeyError(f"Unknown target_node {target_node!r}")
        npc = self._get_npc(npc_id)
        pre_digest = self._arena_digest(npc)
        pre_phys_len = len(npc.phys_history)

        tw = scan_option_tripwires(utterance)
        has_irrealis = bool(tw["irrealis_premature"]) or bool(_COUNTERFACTUAL_RE.search(utterance))

        _, eval_res, id_to_idx = self._eval_npc(npc)
        node_res = eval_res.nodes[id_to_idx[target_node]]
        post_digest = self._arena_digest(npc)

        assert pre_digest == post_digest and len(npc.phys_history) == pre_phys_len

        stance = "BLUFF_QUARANTINED" if has_irrealis else "UNVERIFIED_CLAIM"
        return SpeechAct(
            npc_id=npc_id,
            action_kind="hypothetical_bluff",
            target_node=target_node,
            stance=stance,
            sound_status=node_res.status,
            sound_reasons=node_res.reasons,
            allowed_nodes=self._collect_allowed(eval_res),
            forbidden_spoilers=self._collect_spoilers(npc, eval_res),
            state_delta={
                "quarantined": has_irrealis,
                "arena_digest_unchanged": True,
                "phys_len_unchanged": True,
            },
            rendered_dialogue=(
                f"{npc.role_title}{npc.name}警惕地盯著你：「『{utterance}』只是空口假設或未成真之事，"
                f"我可不會替沒影的事改變立場。」"
            ),
        )

    def request_action(
        self,
        npc_id: str,
        action_node: str,
        *,
        extra_facts: Iterable[str] = (),
    ) -> SpeechAct:
        """Verify Hohfeld KanLock immunity, power, and SOUND prerequisites for an NPC action."""
        if action_node not in self.nodes_by_id:
            raise KeyError(f"Unknown action_node {action_node!r}")
        npc = self._get_npc(npc_id)
        for fid in extra_facts:
            if fid not in self.nodes_by_id:
                raise KeyError(f"Unknown extra fact {fid!r}")

        if action_node in npc.immunity_nodes:
            _, eval_res, id_to_idx = self._eval_npc(npc)
            node_res = eval_res.nodes[id_to_idx[action_node]]
            return SpeechAct(
                npc_id=npc_id,
                action_kind="request_action",
                target_node=action_node,
                stance="ACTION_IMMUNITY_LOCKED",
                sound_status=node_res.status,
                sound_reasons=node_res.reasons,
                allowed_nodes=self._collect_allowed(eval_res),
                forbidden_spoilers=self._collect_spoilers(npc, eval_res),
                state_delta={"blocked_by": "IMMUNITY_KANLOCK"},
                rendered_dialogue=(
                    f"{npc.role_title}{npc.name}斷然拒絕：「涉及『{self._node_title(action_node)}』之鐵律已受封印保護，"
                    f"我絕無立場也不可能違逆。」"
                ),
            )

        if action_node not in npc.power_nodes:
            _, eval_res, id_to_idx = self._eval_npc(npc)
            node_res = eval_res.nodes[id_to_idx[action_node]]
            return SpeechAct(
                npc_id=npc_id,
                action_kind="request_action",
                target_node=action_node,
                stance="ACTION_NO_POWER",
                sound_status=node_res.status,
                sound_reasons=node_res.reasons,
                allowed_nodes=self._collect_allowed(eval_res),
                forbidden_spoilers=self._collect_spoilers(npc, eval_res),
                state_delta={"blocked_by": "HOHFELD_NO_POWER"},
                rendered_dialogue=(
                    f"{npc.role_title}{npc.name}攤手：「『{self._node_title(action_node)}』不在我這區區{npc.role_title}的職權之內。」"
                ),
            )

        _, eval_res, id_to_idx = self._eval_npc(npc, extra_facts=extra_facts)
        node_res = eval_res.nodes[id_to_idx[action_node]]
        eval_map = {nd.label: nd for nd in eval_res.nodes}

        if node_res.status == "established":
            npc.known_facts.update(extra_facts)
            npc.rumor_facts -= npc.known_facts
            npc.phys_history.append(
                {
                    "event_id": len(npc.phys_history) + 1,
                    "kind": "grant_action",
                    "action_node": action_node,
                    "extra_facts": list(extra_facts),
                }
            )
            return SpeechAct(
                npc_id=npc_id,
                action_kind="request_action",
                target_node=action_node,
                stance="ACTION_GRANTED",
                sound_status=node_res.status,
                sound_reasons=node_res.reasons,
                allowed_nodes=self._collect_allowed(eval_res),
                forbidden_spoilers=self._collect_spoilers(npc, eval_res),
                state_delta={
                    "granted_action": action_node,
                    "phys_len": len(npc.phys_history),
                },
                rendered_dialogue=(
                    f"{npc.role_title}{npc.name}鄭重交出信物，執行『{self._node_title(action_node)}』。"
                ),
            )

        missing = [
            p
            for p in self.nodes_by_id[action_node].get("presup", [])
            if eval_map[p].status != "established"
        ]
        missing_names = "、".join(self._node_title(m) for m in missing) or "未知要件"
        return SpeechAct(
            npc_id=npc_id,
            action_kind="request_action",
            target_node=action_node,
            stance="ACTION_PREREQ_MISSING",
            sound_status=node_res.status,
            sound_reasons=node_res.reasons,
            allowed_nodes=self._collect_allowed(eval_res),
            forbidden_spoilers=self._collect_spoilers(npc, eval_res),
            state_delta={
                "blocked_by": "UNVERIFIED_PRESUP",
                "missing_prereqs": missing,
                "reason_names": decode_reasons(node_res.reasons),
            },
            rendered_dialogue=(
                f"{npc.role_title}{npc.name}搖頭：「要我動手『{self._node_title(action_node)}』，"
                f"尚缺必要前提：{missing_names}。」"
            ),
        )

    def import_ledger(self, ledger: WorldLedger) -> List[str]:
        """Inject cross-scene `WorldLedger` flags into all NPCs' initial knowledge."""
        activated = ledger.project_scene_initial_facts(self.pack)
        if activated:
            for npc in self.npcs.values():
                npc.known_facts.update(activated)
                npc.rumor_facts -= npc.known_facts
                npc.arena_snapshot = None
        return activated

    def export_npc_to_ledger(
        self,
        npc_id: str,
        ledger: WorldLedger,
        export_map: Dict[str, str],
    ) -> List[str]:
        """Evaluate an NPC's epistemic arena and record established mapped flags into `WorldLedger`."""
        npc = self._get_npc(npc_id)
        _, eval_res, _ = self._eval_npc(npc)
        granted = [
            str(ev["action_node"])
            for ev in npc.phys_history
            if ev.get("kind") == "grant_action" and "action_node" in ev
        ]
        return ledger.record_scene_outcomes(
            str(self.pack["pack_id"]),
            eval_res,
            export_map,
            granted_actions=granted,
        )

    def sign_declarative_pact(
        self,
        npc_id: str,
        intent_node: str,
        duty_slot_node: str,
        *,
        in_constitutive_context: bool,
        internal_will_matches: bool,
        utterance_hash: int = 0x5041435400000001,
    ) -> SpeechAct:
        """Run C17 `sound_ring0_apply_declarative` on an NPC's persistent 16KB SoundArena."""
        if intent_node not in self.nodes_by_id or duty_slot_node not in self.nodes_by_id:
            raise KeyError("Unknown intent_node or duty_slot_node")
        npc = self._get_npc(npc_id)
        arena_bytes, _, id_to_idx = self._eval_npc(npc)
        intent_idx = id_to_idx[intent_node]
        duty_idx = id_to_idx[duty_slot_node]

        sealed, rc, snap = apply_declarative_arena_bytes(
            arena_bytes,
            self.sound_bin,
            intent_node=intent_idx,
            duty_slot=duty_idx,
            in_constitutive_context=in_constitutive_context,
            internal_will_matches=internal_will_matches,
            utterance_hash=utterance_hash,
        )
        npc.arena_snapshot = sealed
        eval_res = verify_arena_bytes(sealed, self.sound_bin)
        duty_res = eval_res.nodes[duty_idx]
        npc.phys_history.append(
            {
                "event_id": len(npc.phys_history) + 1,
                "kind": "declarative_pact",
                "intent_node": intent_node,
                "duty_slot_node": duty_slot_node,
                "rc": rc,
                "phys_len": snap.phys_len,
            }
        )

        duty_title = self._node_title(duty_slot_node)
        err_name = SOUND_ERR_MAP.get(rc, f"ERR_{rc}")
        if rc == 7:
            stance = "PACT_MISSING_CONTEXT"
            dialogue = (
                f"{npc.role_title}{npc.name}合上法典：「非正式聽證場合所提宣示不生公法效力（{err_name}），"
                f"但你的發言已記入編年史雜湊鏈（phys_len={snap.phys_len}）。」"
            )
        elif rc == 5:
            stance = "ACTION_IMMUNITY_LOCKED"
            dialogue = (
                f"{npc.role_title}{npc.name}嚴正拒斥：「『{duty_title}』受 KanLock 不可侵犯鎖封存（{err_name}），"
                f"任何人皆不得締約動搖！」"
            )
        elif rc == 6:
            stance = "ACTION_NO_POWER"
            dialogue = (
                f"{npc.role_title}{npc.name}擺手：「我沒有針對『{duty_title}』的宣告權限（{err_name}）。」"
            )
        elif rc != 0:
            stance = "ACTION_PREREQ_MISSING"
            dialogue = f"{npc.role_title}{npc.name}搖頭：「締約失敗（{err_name}）。」"
        elif not internal_will_matches:
            stance = "PACT_SIGNED_VOIDABLE"
            dialogue = (
                f"{npc.role_title}{npc.name}皺眉在『{duty_title}』簽章：「既已當庭請願且形式完備，姑且準予效力；"
                f"惟此處分已被標記為可撤銷（voidable=0x{snap.voidable_mask:x}），若有欺瞞必行追奪！」"
            )
        else:
            stance = "PACT_SIGNED_BINDING"
            dialogue = (
                f"{npc.role_title}{npc.name}鄭重用印頒布『{duty_title}』："
                f"「雙證確鑿、真意無瑕，本令終局生效且不可任意撤銷（voidable=0x{snap.voidable_mask:x}）。」"
            )

        return SpeechAct(
            npc_id=npc_id,
            action_kind="sign_declarative_pact",
            target_node=duty_slot_node,
            stance=stance,
            sound_status=duty_res.status,
            sound_reasons=duty_res.reasons,
            allowed_nodes=self._collect_allowed(eval_res),
            forbidden_spoilers=self._collect_spoilers(npc, eval_res),
            state_delta={
                "rc": rc,
                "err_name": err_name,
                "phys_len": snap.phys_len,
                "phys_hash": f"0x{snap.phys_hash:016x}",
                "duty_pos_mask": snap.duty_pos_mask,
                "voidable_mask": snap.voidable_mask,
                "rescission_power_mask": snap.rescission_power_mask,
            },
            rendered_dialogue=dialogue,
        )

    def rescind_deceptive_pact(
        self,
        npc_id: str,
        target_intent_node: str,
        duty_slot_node: str,
        *,
        rescission_hash: int = 0x5245534300000001,
    ) -> SpeechAct:
        """Run C17 `sound_ring0_exercise_rescission` to revoke a voidable deceptive pact."""
        if target_intent_node not in self.nodes_by_id or duty_slot_node not in self.nodes_by_id:
            raise KeyError("Unknown target_intent_node or duty_slot_node")
        npc = self._get_npc(npc_id)
        arena_bytes, _, id_to_idx = self._eval_npc(npc)
        intent_idx = id_to_idx[target_intent_node]
        duty_idx = id_to_idx[duty_slot_node]

        sealed, rc, eval_res, snap = exercise_rescission_arena_bytes(
            arena_bytes,
            self.sound_bin,
            target_intent_node=intent_idx,
            duty_slot=duty_idx,
            rescission_hash=rescission_hash,
        )
        npc.arena_snapshot = sealed
        intent_res = eval_res.nodes[intent_idx]
        duty_res = eval_res.nodes[duty_idx]
        err_name = SOUND_ERR_MAP.get(rc, f"ERR_{rc}")

        npc.phys_history.append(
            {
                "event_id": len(npc.phys_history) + 1,
                "kind": "exercise_rescission",
                "target_intent_node": target_intent_node,
                "duty_slot_node": duty_slot_node,
                "rc": rc,
                "phys_len": snap.phys_len,
            }
        )

        if rc == 0:
            stance = "PACT_RESCINDED_HOSTILE"
            dialogue = (
                f"{npc.role_title}{npc.name}拍案怒喝：「欺君偽造之『{self._node_title(target_intent_node)}』已依法撤銷（REVOKED）！"
                f"原簽發之『{self._node_title(duty_slot_node)}』即刻失效，衛兵給我拿下！」"
            )
        elif rc == 5:
            stance = "ACTION_IMMUNITY_LOCKED"
            dialogue = f"{npc.role_title}{npc.name}：「此權限受 KanLock 鎖定，不得撤銷（{err_name}）。」"
        else:
            stance = "ACTION_NO_POWER"
            dialogue = (
                f"{npc.role_title}{npc.name}冷靜道：「該處分並無可撤銷之瑕疵或撤銷權限（{err_name}）。」"
            )

        return SpeechAct(
            npc_id=npc_id,
            action_kind="rescind_deceptive_pact",
            target_node=target_intent_node,
            stance=stance,
            sound_status=intent_res.status,
            sound_reasons=intent_res.reasons,
            allowed_nodes=self._collect_allowed(eval_res),
            forbidden_spoilers=self._collect_spoilers(npc, eval_res),
            state_delta={
                "rc": rc,
                "err_name": err_name,
                "phys_len": snap.phys_len,
                "phys_hash": f"0x{snap.phys_hash:016x}",
                "revoked_mask": snap.revoked_mask,
                "duty_pos_mask": snap.duty_pos_mask,
                "voidable_mask": snap.voidable_mask,
                "rescission_power_mask": snap.rescission_power_mask,
                "duty_slot_status": duty_res.status,
            },
            rendered_dialogue=dialogue,
        )

    def export_npc_fir_gate(
        self,
        npc_id: str,
        *,
        repaired: bool = False,
    ) -> ArenaFirResult:
        """Lift an NPC's current epistemic & Hohfeld gate into a verified 2,144B `"FIR1"` circuit."""
        npc = self._get_npc(npc_id)
        arena_bytes, _, _ = self._eval_npc(npc)
        return lift_fir_arena_bytes(
            arena_bytes,
            self.sound_bin,
            prog_name=npc.npc_id,
            repaired=repaired,
            seed=npc.searu_seed,
        )
