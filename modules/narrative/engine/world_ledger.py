"""Cross-scene persistence ledger bridging 16KB / 64-node narrative arenas."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, Iterable, List, Set

from modules.common.arena_codec import ArenaEvalResult


@dataclass
class WorldLedger:
    """Bounded cross-chapter flag & relic ledger connecting separate 64-node scenes."""

    global_flags: Set[str] = field(default_factory=set)
    carried_items: Set[str] = field(default_factory=set)
    scene_log: List[Dict[str, Any]] = field(default_factory=list)

    def record_scene_outcomes(
        self,
        scene_pack_id: str,
        eval_res: ArenaEvalResult,
        export_map: Dict[str, str],
        *,
        granted_actions: Iterable[str] = (),
        carried_items: Iterable[str] = (),
    ) -> List[str]:
        """Export established scene nodes and granted actions into `global_flags`."""
        status_map = {nd.label: nd.status for nd in eval_res.nodes}
        granted_set = set(granted_actions)
        newly_recorded: List[str] = []

        for local_node, global_flag in export_map.items():
            if status_map.get(local_node) == "established" or local_node in granted_set:
                if global_flag not in self.global_flags:
                    self.global_flags.add(global_flag)
                    newly_recorded.append(global_flag)

        self.carried_items.update(carried_items)
        self.scene_log.append(
            {
                "scene_pack_id": scene_pack_id,
                "newly_recorded_flags": list(newly_recorded),
                "total_flags": sorted(self.global_flags),
            }
        )
        return newly_recorded

    def project_scene_initial_facts(self, target_pack: Dict[str, Any]) -> List[str]:
        """Translate `global_flags` into `fact_slot` IDs via `target_pack['ledger_imports']`."""
        imports = target_pack.get("ledger_imports", {})
        activated: List[str] = []
        for global_flag, local_fact in imports.items():
            if global_flag in self.global_flags:
                activated.append(str(local_fact))
        return activated
