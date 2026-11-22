#!/usr/bin/env python3
"""
tools/sound_sabotage_check.py — 42-Mutant Single-Fault Sabotage Gate (Gate 6)
================================================================================
Verifies 100.0% mutation-kill efficacy (42/42 killed, 0 compile errors) across 14
categories (3 mutants each) in `src/sound_ring0.c`:
   1. bilattice_hume_evid         (3)
   2. hohfeld_kanlock             (3)
   3. prepared_isolation          (3)
   4. siphash_atomic_batch        (3)
   5. weighted_searu              (3)
   6. chart_glue_fir_lift         (3)
   7. bilattice_odd_cycle_classify(3)
   8. bilattice_paradox_hunt      (3)
   9. sheaf_h1_holonomy           (3)
  10. sheaf_h1_obstruction_hunt   (3)
  11. hohfeld_v4_group            (3)
  12. hohfeld_chisholm_trap       (3)
  13. linguistics_vendler_aspect  (3)
  14. linguistics_presup_force    (3)
"""

from __future__ import annotations

import os
import sys
from pathlib import Path


FLOW_TOOLS_DIR = (
    Path(os.environ.get("FLOW_DIR", str(Path(__file__).resolve().parents[2] / "FLOW"))).resolve()
    / "sdk"
    / "tools"
)
if str(FLOW_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(FLOW_TOOLS_DIR))

from flow_sabotage_runner import FlowMutant, run_sabotage_suite

SoundMutant = FlowMutant


def build_sound_mutants() -> list[SoundMutant]:
    return [
        # --- 1. bilattice_hume_evid (3) ---
        SoundMutant(
            "M01_hume_is_to_ought_bypass",
            "bilattice_hume_evid",
            "    if ((clause_mask & (node_mask & (~factual_mask))) != UINT64_C(0)) {\n"
            "        return true;\n"
            "    }\n"
            "    if (diag_reason_out != NULL) {\n"
            "        *diag_reason_out |= (uint32_t)SOUND_REASON_HUME_IS_TO_OUGHT;\n"
            "    }\n"
            "    return false;",
            "    if ((clause_mask & (node_mask & (~factual_mask))) != UINT64_C(0)) {\n"
            "        return true;\n"
            "    }\n"
            "    if (diag_reason_out != NULL) {\n"
            "        *diag_reason_out |= (uint32_t)SOUND_REASON_HUME_IS_TO_OUGHT;\n"
            "    }\n"
            "    return true;",
        ),
        SoundMutant(
            "M02_hume_ought_to_is_bypass",
            "bilattice_hume_evid",
            "    if (nd->sort == (uint8_t)SOUND_SORT_FACTUAL) {\n"
            "        if ((clause_mask & (~factual_mask)) != UINT64_C(0)) {\n"
            "            if (diag_reason_out != NULL) {\n"
            "                *diag_reason_out |= (uint32_t)SOUND_REASON_HUME_OUGHT_TO_IS;\n"
            "            }\n"
            "            return false;\n"
            "        }\n"
            "        return true;\n"
            "    }",
            "    if (nd->sort == (uint8_t)SOUND_SORT_FACTUAL) {\n"
            "        if ((clause_mask & (~factual_mask)) != UINT64_C(0)) {\n"
            "            if (diag_reason_out != NULL) {\n"
            "                *diag_reason_out |= (uint32_t)SOUND_REASON_HUME_OUGHT_TO_IS;\n"
            "            }\n"
            "            return true;\n"
            "        }\n"
            "        return true;\n"
            "    }",
        ),
        SoundMutant(
            "M03_evidential_leaf_bypass",
            "bilattice_hume_evid",
            "    if (nd->evid >= (uint8_t)SOUND_EVID_INFERENTIAL && clause_mask == UINT64_C(0)) {\n"
            "        if (diag_reason_out != NULL) {\n"
            "            *diag_reason_out |= (uint32_t)SOUND_REASON_EVIDENTIAL_LEAF;\n"
            "        }\n"
            "        return false;\n"
            "    }",
            "    if (nd->evid >= (uint8_t)SOUND_EVID_INFERENTIAL && clause_mask == UINT64_C(0)) {\n"
            "        if (diag_reason_out != NULL) {\n"
            "            *diag_reason_out |= (uint32_t)SOUND_REASON_EVIDENTIAL_LEAF;\n"
            "        }\n"
            "        return true;\n"
            "    }",
        ),
        # --- 2. hohfeld_kanlock (3) ---
        SoundMutant(
            "M04_kanlock_immunity_bypass",
            "hohfeld_kanlock",
            "    bool commit = (!imm_hit) && pow_ok && coh_ok;",
            "    bool commit = pow_ok && coh_ok;",
        ),
        SoundMutant(
            "M05_no_power_bypass",
            "hohfeld_kanlock",
            "    bool pow_ok = (arena->map.norm.power_mask & bit) != UINT64_C(0);",
            "    bool pow_ok = true;",
        ),
        SoundMutant(
            "M06_coherence_clash_allow",
            "hohfeld_kanlock",
            "    bool coh_ok = (dp & dn) == UINT64_C(0);\n\n    bool commit = (!imm_hit) && pow_ok && coh_ok;",
            "    bool coh_ok = true;\n\n    bool commit = (!imm_hit) && pow_ok && coh_ok;",
        ),
        # --- 3. prepared_isolation (3) ---
        SoundMutant(
            "M07_policy_slot_bypass",
            "prepared_isolation",
            "        if ((ctrl->policy_mask & tbit) != UINT64_C(0)) {\n"
            "            inject_reasons |= (uint32_t)SOUND_INJECT_POLICY_SLOT;\n"
            "        }",
            "        if (false && (ctrl->policy_mask & tbit) != UINT64_C(0)) {\n"
            "            inject_reasons |= (uint32_t)SOUND_INJECT_POLICY_SLOT;\n"
            "        }",
        ),
        SoundMutant(
            "M08_cross_plane_sort_bypass",
            "prepared_isolation",
            "        if (nd->sort != (uint8_t)SOUND_SORT_FACTUAL || c->sort != (uint8_t)SOUND_SORT_FACTUAL) {\n"
            "            inject_reasons |= (uint32_t)SOUND_INJECT_CROSS_PLANE_SORT;\n"
            "        }",
            "        if (false && nd->sort != (uint8_t)SOUND_SORT_FACTUAL &&\n"
            "            c->sort != (uint8_t)SOUND_SORT_FACTUAL) {\n"
            "            inject_reasons |= (uint32_t)SOUND_INJECT_CROSS_PLANE_SORT;\n"
            "        }",
        ),
        SoundMutant(
            "M09_priv_escalation_bypass",
            "prepared_isolation",
            "        if (c->node_flags != UINT32_C(0) || c->evid == (uint8_t)SOUND_EVID_ENACTED_NORM ||\n"
            "            c->force == (uint8_t)SOUND_FORCE_DECLARATIVE ||\n"
            "            (nd->flags & SOUND_NODE_FLAG_CONSTITUTIVE) != UINT32_C(0)) {\n"
            "            inject_reasons |= (uint32_t)SOUND_INJECT_PRIV_ESCALATION;\n"
            "        }",
            "        if (false && c->node_flags != UINT32_C(0) && c->evid == (uint8_t)SOUND_EVID_ENACTED_NORM &&\n"
            "            c->force == (uint8_t)SOUND_FORCE_DECLARATIVE &&\n"
            "            (nd->flags & SOUND_NODE_FLAG_CONSTITUTIVE) != UINT32_C(0)) {\n"
            "            inject_reasons |= (uint32_t)SOUND_INJECT_PRIV_ESCALATION;\n"
            "        }",
        ),
        # --- 4. siphash_atomic_batch (3) ---
        SoundMutant(
            "M10_siphash_xff_corrupt",
            "siphash_atomic_batch",
            "    c->v2 ^= UINT64_C(0xFF);",
            "    c->v2 ^= UINT64_C(0xFE);",
        ),
        SoundMutant(
            "M11_utterance_span_hash_bypass",
            "siphash_atomic_batch",
            "        if (actual_hash != c->payload_hash) {\n"
            "            *fiber_out = SOUND_FIBER_FALLBACK_SPAN;\n"
            "            *inject_reasons_out = (uint32_t)SOUND_INJECT_SPAN_HASH_MISMATCH;\n"
            "            return false;\n"
            "        }",
            "        if (actual_hash != c->payload_hash && false) {\n"
            "            *fiber_out = SOUND_FIBER_FALLBACK_SPAN;\n"
            "            *inject_reasons_out = (uint32_t)SOUND_INJECT_SPAN_HASH_MISMATCH;\n"
            "            return false;\n"
            "        }",
        ),
        SoundMutant(
            "M12_batch_evid_ceiling_bypass",
            "siphash_atomic_batch",
            "        if ((ctrl->ctrl_flags & SOUND_FOREST_CTRL_PROPOSER_LOW_EVID) != UINT32_C(0) &&\n"
            "            c->evid < (uint8_t)SOUND_EVID_REPORTATIVE) {\n"
            "            inject_reasons |= (uint32_t)SOUND_INJECT_EVID_CEILING;\n"
            "        }",
            "        if ((ctrl->ctrl_flags & SOUND_FOREST_CTRL_PROPOSER_LOW_EVID) != UINT32_C(0) &&\n"
            "            c->evid < (uint8_t)SOUND_EVID_REPORTATIVE && false) {\n"
            "            inject_reasons |= (uint32_t)SOUND_INJECT_EVID_CEILING;\n"
            "        }",
        ),
        # --- 5. weighted_searu (3) ---
        SoundMutant(
            "M13_weighted_searu_ignore_priority",
            "weighted_searu",
            "    uint8_t lw[SOUND_MAX_NODES];\n"
            "    for (uint32_t i = 0U; i < SOUND_MAX_NODES; i++) {\n"
            "        lw[i] = (priority_weights[i] > UINT8_C(0)) ? priority_weights[i] : UINT8_C(1);\n"
            "    }",
            "    uint8_t lw[SOUND_MAX_NODES];\n"
            "    for (uint32_t i = 0U; i < SOUND_MAX_NODES; i++) {\n"
            "        lw[i] = UINT8_C(1);\n"
            "    }",
        ),
        SoundMutant(
            "M14_weighted_searu_kanlock_pin_drop",
            "weighted_searu",
            "    uint8_t lw[SOUND_MAX_NODES];\n"
            "    for (uint32_t i = 0U; i < SOUND_MAX_NODES; i++) {\n"
            "        lw[i] = (priority_weights[i] > UINT8_C(0)) ? priority_weights[i] : UINT8_C(1);\n"
            "    }\n\n"
            "    uint64_t init_canvas[4] = {est | (conf & imm), UINT64_C(0), UINT64_C(0), UINT64_C(0)};\n"
            "    uint64_t kan_pin[4] = {pinned, UINT64_MAX, UINT64_MAX, UINT64_MAX};",
            "    uint8_t lw[SOUND_MAX_NODES];\n"
            "    for (uint32_t i = 0U; i < SOUND_MAX_NODES; i++) {\n"
            "        lw[i] = (priority_weights[i] > UINT8_C(0)) ? priority_weights[i] : UINT8_C(1);\n"
            "    }\n\n"
            "    uint64_t init_canvas[4] = {est | (conf & imm), UINT64_C(0), UINT64_C(0), UINT64_C(0)};\n"
            "    uint64_t kan_pin[4] = {pinned & (~imm), UINT64_MAX, UINT64_MAX, UINT64_MAX};",
        ),
        SoundMutant(
            "M15_weighted_searu_conflict_energy_drop",
            "weighted_searu",
            "                if (hit != UINT64_C(0)) {\n"
            "                    energy += UINT64_C(65536);\n"
            "                    frust[0] |= ub | hit;\n"
            "                }\n"
            "                if (!sound_node_fires(arena, u, s, s, nm, factual_mask)) {\n"
            "                    energy += UINT64_C(32768);\n"
            "                    frust[0] |= ub;\n"
            "                }",
            "                if (hit != UINT64_C(0)) {\n"
            "                    frust[0] |= ub | hit;\n"
            "                }\n"
            "                if (!sound_node_fires(arena, u, s, s, nm, factual_mask)) {\n"
            "                    frust[0] |= ub;\n"
            "                }",
        ),
        # --- 6. chart_glue_fir_lift (3) ---
        SoundMutant(
            "M16_chart_glue_allow_conflict",
            "chart_glue_fir_lift",
            "    if ((export_mask & src->map.eval_out.conflict_mask) != UINT64_C(0) ||\n"
            "        (export_mask & (~src->map.eval_out.established_mask)) != UINT64_C(0)) {\n"
            "        return SOUND_FIBER_HOLD_INCOHERENT;\n"
            "    }\n"
            "    sound_zero_bytes(port_out, sizeof(*port_out));",
            "    if (((export_mask & src->map.eval_out.conflict_mask) != UINT64_C(0) ||\n"
            "         (export_mask & (~src->map.eval_out.established_mask)) != UINT64_C(0)) &&\n"
            "        false) {\n"
            "        return SOUND_FIBER_HOLD_INCOHERENT;\n"
            "    }\n"
            "    sound_zero_bytes(port_out, sizeof(*port_out));",
        ),
        SoundMutant(
            "M17_chart_glue_allow_policy_import",
            "chart_glue_fir_lift",
            "        if (inj != (uint32_t)SOUND_INJECT_NONE) {\n"
            "            ctrl->last_reject_reason = inj;\n"
            "            return sound_record_fiber(dst, SOUND_FIBER_FALLBACK_POLICY_VIOLATION);\n"
            "        }\n"
            "        dst_import_mask |= dbit;",
            "        if (inj != (uint32_t)SOUND_INJECT_NONE && false) {\n"
            "            ctrl->last_reject_reason = inj;\n"
            "            return sound_record_fiber(dst, SOUND_FIBER_FALLBACK_POLICY_VIOLATION);\n"
            "        }\n"
            "        dst_import_mask |= dbit;",
        ),
        SoundMutant(
            "M18_policy_fir_lift_drop_postloop",
            "chart_glue_fir_lift",
            "        if (!b.overflow) {\n"
            "            p_star_id = sound__pbld_gamma(&b, arena, n, node_mask, factual_mask, bind_mask,\n"
            "                                          root_mask, plain_bind_mask, der_order, n_der, zero_id,\n"
            "                                          UINT16_C(0), false, cur_i_id);\n"
            "            i_star_id = sound__pbld_gamma(&b, arena, n, node_mask, factual_mask, bind_mask,\n"
            "                                          root_mask, plain_bind_mask, der_order, n_der, zero_id,\n"
            "                                          UINT16_C(0), false, p_star_id);\n"
            "        }",
            "        if (!b.overflow) {\n"
            "            p_star_id = zero_id;\n"
            "            i_star_id = cur_i_id;\n"
            "        }",
        ),
        # --- 7. bilattice_odd_cycle_classify (3) ---
        SoundMutant(
            "M19_b4_neg_no_swap",
            "bilattice_odd_cycle_classify",
            "    return (uint8_t)(((xm & 2U) >> 1U) | ((xm & 1U) << 1U));",
            "    return (uint8_t)xm;",
        ),
        SoundMutant(
            "M20_b4_conf_drop_xor",
            "bilattice_odd_cycle_classify",
            "    return (uint8_t)((uint32_t)sound_b4_neg(x) ^ 3U);",
            "    return sound_b4_neg(x);",
        ),
        SoundMutant(
            "M21_classical_model_invert_check",
            "bilattice_odd_cycle_classify",
            "            if (u_in != u_unatk) {\n"
            "                ok = false;\n"
            "                break;\n"
            "            }",
            "            if (u_in && !u_unatk && false) {\n"
            "                ok = false;\n"
            "                break;\n"
            "            }",
        ),
        # --- 8. bilattice_paradox_hunt (3) ---
        SoundMutant(
            "M22_paradox_min_break_cycle",
            "bilattice_paradox_hunt",
            "        out->atk_to[0] = UINT8_C(4);\n"
            "        out->atk_to[1] = UINT8_C(1);\n"
            "        out->atk_to[2] = UINT8_C(2);",
            "        out->atk_to[0] = UINT8_C(0);\n"
            "        out->atk_to[1] = UINT8_C(1);\n"
            "        out->atk_to[2] = UINT8_C(2);",
        ),
        SoundMutant(
            "M23_small_gamma_drop_block",
            "bilattice_paradox_hunt",
            "static uint8_t sound__small_gamma(uint32_t n, const uint8_t atk_to[8], uint8_t blocked_by) {\n"
            "    uint32_t acc = UINT32_C(0);\n"
            "    for (uint32_t u = 0U; u < n; u++) {\n"
            "        if ((((uint32_t)atk_to[u]) & (uint32_t)blocked_by) == UINT32_C(0)) {\n"
            "            acc |= (UINT32_C(1) << u);\n"
            "        }\n"
            "    }\n"
            "    return (uint8_t)acc;\n"
            "}",
            "static uint8_t sound__small_gamma(uint32_t n, const uint8_t atk_to[8], uint8_t blocked_by) {\n"
            "    (void)atk_to;\n"
            "    (void)blocked_by;\n"
            "    return (uint8_t)((UINT32_C(1) << n) - UINT32_C(1));\n"
            "}",
        ),
        SoundMutant(
            "M24_paradox_min_zero_merkle",
            "bilattice_paradox_hunt",
            "    mh = sound_fnv1a_u64(mh, (uint64_t)out->conflict_mask);\n"
            "    out->merkle = mh;",
            "    mh = sound_fnv1a_u64(mh, (uint64_t)out->conflict_mask);\n"
            "    (void)mh;\n"
            "    out->merkle = UINT64_C(0);",
        ),
        # --- 9. sheaf_h1_holonomy (3) ---
        SoundMutant(
            "M25_sheaf_holonomy_z4_drop_g20",
            "sheaf_h1_holonomy",
            "uint8_t sound_sheaf_holonomy_z4_3(uint8_t g01, uint8_t g12, uint8_t g20) {\n"
            "    return (uint8_t)(((uint32_t)g01 + (uint32_t)g12 + (uint32_t)g20) & 3U);\n"
            "}",
            "uint8_t sound_sheaf_holonomy_z4_3(uint8_t g01, uint8_t g12, uint8_t g20) {\n"
            "    (void)g20;\n"
            "    return (uint8_t)(((uint32_t)g01 + (uint32_t)g12) & 3U);\n"
            "}",
        ),
        SoundMutant(
            "M26_sheaf_holonomy_v4_drop_g12",
            "sheaf_h1_holonomy",
            "uint8_t sound_sheaf_holonomy_v4_3(uint8_t g01, uint8_t g12, uint8_t g20) {\n"
            "    return (uint8_t)(((uint32_t)g01 ^ (uint32_t)g12 ^ (uint32_t)g20) & 3U);\n"
            "}",
            "uint8_t sound_sheaf_holonomy_v4_3(uint8_t g01, uint8_t g12, uint8_t g20) {\n"
            "    (void)g12;\n"
            "    return (uint8_t)(((uint32_t)g01 ^ (uint32_t)g20) & 3U);\n"
            "}",
        ),
        SoundMutant(
            "M27_sheaf_coboundary_z4_ignore_e20",
            "sheaf_h1_holonomy",
            "                if ((((f1 - f0) & 3U) == e01) && (((f2 - f1) & 3U) == e12) &&\n"
            "                    (((f0 - f2) & 3U) == e20)) {\n"
            "                    return true;\n"
            "                }",
            "                if ((((f1 - f0) & 3U) == e01) && (((f2 - f1) & 3U) == e12)) {\n"
            "                    (void)e20;\n"
            "                    return true;\n"
            "                }",
        ),
        # --- 10. sheaf_h1_obstruction_hunt (3) ---
        SoundMutant(
            "M28_sheaf_h1_hunt_trivialize",
            "sheaf_h1_obstruction_hunt",
            "    out->g01 = base_twist;\n"
            "    out->g12 = base_twist;\n"
            "    out->g20 = base_twist;",
            "    out->g01 = base_twist;\n"
            "    out->g12 = (uint8_t)(4U - (uint32_t)base_twist);\n"
            "    out->g20 = UINT8_C(0);",
        ),
        SoundMutant(
            "M29_sheaf_h1_gauge_corrupt",
            "sheaf_h1_obstruction_hunt",
            "    out->sg20 =\n"
            "        (uint8_t)(((uint32_t)out->g20 + (uint32_t)out->phi[0] - (uint32_t)out->phi[2]) & 3U);",
            "    out->sg20 = out->g20;",
        ),
        SoundMutant(
            "M30_sheaf_h1_zero_xor_twist",
            "sheaf_h1_obstruction_hunt",
            "    out->t20 = d1 ^ d2 ^ twist64;",
            "    (void)twist64;\n    out->t20 = d1 ^ d2;",
        ),
        # --- 11. hohfeld_v4_group (3) ---
        SoundMutant(
            "M31_hohfeld_corr_break_claim",
            "hohfeld_v4_group",
            "    if (p == SOUND_HOHFELD_CLAIM) {\n"
            "        return SOUND_HOHFELD_DUTY;\n"
            "    }",
            "    if (p == SOUND_HOHFELD_CLAIM) {\n"
            "        return SOUND_HOHFELD_PRIVILEGE;\n"
            "    }",
        ),
        SoundMutant(
            "M32_hohfeld_opp_break_power",
            "hohfeld_v4_group",
            "    if (p == SOUND_HOHFELD_POWER) {\n"
            "        return SOUND_HOHFELD_DISABILITY;\n"
            "    }",
            "    if (p == SOUND_HOHFELD_POWER) {\n"
            "        return SOUND_HOHFELD_LIABILITY;\n"
            "    }",
        ),
        SoundMutant(
            "M33_hohfeld_bfs_ignore_immunity",
            "hohfeld_v4_group",
            "                if ((((uint32_t)immunity & bit) != 0U) || (((uint32_t)power & bit) == 0U)) {\n"
            "                    continue;\n"
            "                }",
            "                (void)immunity;\n"
            "                if (((uint32_t)power & bit) == 0U) {\n"
            "                    continue;\n"
            "                }",
        ),
        # --- 12. hohfeld_chisholm_trap (3) ---
        SoundMutant(
            "M34_chisholm_drop_ctd_attack",
            "hohfeld_chisholm_trap",
            "    arena.map.node_meta[2].num_clauses = 1U;\n"
            "    arena.map.node_clauses[2].clauses[0] = UINT64_C(8);",
            "    arena.map.node_meta[2].num_clauses = 1U;\n"
            "    arena.map.node_clauses[2].clauses[0] = UINT64_C(0);",
        ),
        SoundMutant(
            "M35_norm_trap_break_tie",
            "hohfeld_chisholm_trap",
            "    out->energy_keep_a = sound_lex_specialis_pair_energy(true, false, w, w);\n"
            "    out->energy_keep_b = sound_lex_specialis_pair_energy(false, true, w, w);",
            "    out->energy_keep_a = sound_lex_specialis_pair_energy(true, false, w, w);\n"
            "    out->energy_keep_b = sound_lex_specialis_pair_energy(true, true, w, w);",
        ),
        SoundMutant(
            "M36_lex_energy_drop_conflict",
            "hohfeld_chisholm_trap",
            "    if (keep_a && keep_b) {\n"
            "        e += UINT32_C(65536);\n"
            "    }",
            "    if (keep_a && keep_b && false) {\n"
            "        e += UINT32_C(65536);\n"
            "    }",
        ),
        # --- 13. linguistics_vendler_aspect (3) ---
        SoundMutant(
            "M37_aspect_allow_atelic_constitutive",
            "linguistics_vendler_aspect",
            "bool sound_aspect_constitutive_ok(sound_aspect_t a, bool requires_telic) {\n"
            "    return (!requires_telic) || sound_aspect_is_telic(a);\n"
            "}",
            "bool sound_aspect_constitutive_ok(sound_aspect_t a, bool requires_telic) {\n"
            "    (void)a;\n"
            "    (void)requires_telic;\n"
            "    return true;\n"
            "}",
        ),
        SoundMutant(
            "M38_aspect_features_swap",
            "linguistics_vendler_aspect",
            "    if (dyn && !telic) {\n"
            "        return SOUND_ASPECT_ACTIVITY;\n"
            "    }\n"
            "    if (dyn && telic) {\n"
            "        return SOUND_ASPECT_ACCOMPLISHMENT;\n"
            "    }",
            "    if (dyn && !telic) {\n"
            "        return SOUND_ASPECT_ACCOMPLISHMENT;\n"
            "    }\n"
            "    if (dyn && telic) {\n"
            "        return SOUND_ASPECT_ACTIVITY;\n"
            "    }",
        ),
        SoundMutant(
            "M39_force_declarative_skip_telic",
            "linguistics_vendler_aspect",
            "    if (force == SOUND_FORCE_DECLARATIVE && !sound_aspect_is_telic(aspect)) {\n"
            "        return SOUND_FIBER_FALLBACK_POLICY_VIOLATION;\n"
            "    }",
            "    if (force == SOUND_FORCE_DECLARATIVE && !sound_aspect_is_telic(aspect) && false) {\n"
            "        return SOUND_FIBER_FALLBACK_POLICY_VIOLATION;\n"
            "    }",
        ),
        # --- 14. linguistics_presup_force (3) ---
        SoundMutant(
            "M40_presup_eval_bypass_gap",
            "linguistics_presup_force",
            "uint8_t sound_presup_eval(uint8_t assert_val, uint8_t presup_val) {\n"
            "    uint32_t pv = (uint32_t)presup_val & 3U;\n"
            "    uint32_t av = (uint32_t)assert_val & 3U;\n"
            "    return (pv == (uint32_t)SOUND_B4_TRUE) ? (uint8_t)av : (uint8_t)SOUND_B4_BOT;\n"
            "}",
            "uint8_t sound_presup_eval(uint8_t assert_val, uint8_t presup_val) {\n"
            "    (void)presup_val;\n"
            "    return (uint8_t)((uint32_t)assert_val & 3U);\n"
            "}",
        ),
        SoundMutant(
            "M41_presup_cond_no_filter",
            "linguistics_presup_force",
            "    if (out_presup != NULL) {\n"
            "        *out_presup = (aa == (uint8_t)SOUND_B4_TRUE) ? sound_b4_and_t(ap, bp) : ap;\n"
            "    }",
            "    if (out_presup != NULL) {\n"
            "        *out_presup = sound_b4_and_t(ap, bp);\n"
            "    }",
        ),
        SoundMutant(
            "M42_force_assertion_leak_duty",
            "linguistics_presup_force",
            "    if (force == SOUND_FORCE_ASSERTION) {\n"
            "        st->epistemic |= slot_bit;\n"
            "        return SOUND_FIBER_COMMITTED;\n"
            "    }",
            "    if (force == SOUND_FORCE_ASSERTION) {\n"
            "        st->epistemic |= slot_bit;\n"
            "        st->duty_pos |= slot_bit;\n"
            "        return SOUND_FIBER_COMMITTED;\n"
            "    }",
        ),
        # --- 15. contract_drift_immunity (3) ---
        SoundMutant(
            "M43_contract_drop_immunity_breach",
            "contract_drift_immunity",
            "        if (c->kind == (uint8_t)SOUND_CLAUSE_UNILATERAL_POWER &&\n"
            "            (c->target_mask & spec->protected_mask) != UINT64_C(0)) {\n"
            "            c_viol |= (uint32_t)SOUND_CVIOL_IMMUNITY_BREACH;\n"
            "        }",
            "        if (c->kind == (uint8_t)SOUND_CLAUSE_UNILATERAL_POWER &&\n"
            "            (c->target_mask & spec->protected_mask) != UINT64_C(0) && false) {\n"
            "            c_viol |= (uint32_t)SOUND_CVIOL_IMMUNITY_BREACH;\n"
            "        }",
        ),
        SoundMutant(
            "M44_contract_drop_cap_drift_veto",
            "contract_drift_immunity",
            "    if (worst_sum > spec->max_loss_cap) {\n"
            "        out->viol_flags |= (uint32_t)SOUND_CVIOL_BENEFIT_DRIFT;\n"
            "    }",
            "    if (worst_sum > spec->max_loss_cap && false) {\n"
            "        out->viol_flags |= (uint32_t)SOUND_CVIOL_BENEFIT_DRIFT;\n"
            "    }",
        ),
        SoundMutant(
            "M45_contract_drop_presup_smuggle",
            "contract_drift_immunity",
            "        if (c->disclosed == UINT8_C(0) &&\n"
            "            (c->base_loss > UINT64_C(0) || c->extra_drift > UINT64_C(0) ||\n"
            "             c->exit_fee > UINT64_C(0))) {\n"
            "            c_viol |= (uint32_t)SOUND_CVIOL_PRESUP_SMUGGLE;\n"
            "        }",
            "        if (c->disclosed == UINT8_C(0) &&\n"
            "            (c->base_loss > UINT64_C(0) || c->extra_drift > UINT64_C(0) ||\n"
            "             c->exit_fee > UINT64_C(0)) &&\n"
            "            false) {\n"
            "            c_viol |= (uint32_t)SOUND_CVIOL_PRESUP_SMUGGLE;\n"
            "        }",
        ),
        # --- 16. contract_informed_asym (3) ---
        SoundMutant(
            "M46_contract_drop_ambiguity_veto",
            "contract_informed_asym",
            "        if (c->b4_truth == (uint8_t)SOUND_B4_BOT || c->b4_truth == (uint8_t)SOUND_B4_TOP ||\n"
            "            c->kind == (uint8_t)SOUND_CLAUSE_AMBIGUOUS_OBLIGATION) {\n"
            "            c_viol |= (uint32_t)SOUND_CVIOL_UNINFORMED_AMBIG;\n"
            "        }",
            "        if ((c->b4_truth == (uint8_t)SOUND_B4_BOT || c->b4_truth == (uint8_t)SOUND_B4_TOP ||\n"
            "             c->kind == (uint8_t)SOUND_CLAUSE_AMBIGUOUS_OBLIGATION) &&\n"
            "            false) {\n"
            "            c_viol |= (uint32_t)SOUND_CVIOL_UNINFORMED_AMBIG;\n"
            "        }",
        ),
        SoundMutant(
            "M47_contract_drop_unconscionable",
            "contract_informed_asym",
            "        if (((unsigned __int128)c->cons_penalty * (unsigned __int128)spec->max_asym_den) >\n"
            "            ((unsigned __int128)c->inst_penalty * (unsigned __int128)spec->max_asym_num)) {\n"
            "            c_viol |= (uint32_t)SOUND_CVIOL_UNCONSCIONABLE;\n"
            "        }",
            "        if ((((unsigned __int128)c->cons_penalty * (unsigned __int128)spec->max_asym_den) >\n"
            "             ((unsigned __int128)c->inst_penalty * (unsigned __int128)spec->max_asym_num)) &&\n"
            "            false) {\n"
            "            c_viol |= (uint32_t)SOUND_CVIOL_UNCONSCIONABLE;\n"
            "        }",
        ),
        SoundMutant(
            "M48_contract_drop_chisholm_cascade",
            "contract_informed_asym",
            "        if (c->kind == (uint8_t)SOUND_CLAUSE_CHISHOLM_DEFAULT && c->extra_drift > UINT64_C(0)) {\n"
            "            c_viol |= (uint32_t)SOUND_CVIOL_CHISHOLM_CASCADE;\n"
            "        }",
            "        if (c->kind == (uint8_t)SOUND_CLAUSE_CHISHOLM_DEFAULT && c->extra_drift > UINT64_C(0) &&\n"
            "            false) {\n"
            "            c_viol |= (uint32_t)SOUND_CVIOL_CHISHOLM_CASCADE;\n"
            "        }",
        ),
        # --- 17. contract_graph_coverage (3) ---
        SoundMutant(
            "M49_contract_drop_graph_bypass",
            "contract_graph_coverage",
            "    if (out->graph_bypass_mask != UINT32_C(0)) {\n"
            "        out->viol_flags |= (uint32_t)SOUND_CVIOL_GRAPH_BYPASS;\n"
            "    }",
            "    if (out->graph_bypass_mask != UINT32_C(0) && false) {\n"
            "        out->viol_flags |= (uint32_t)SOUND_CVIOL_GRAPH_BYPASS;\n"
            "    }",
        ),
        SoundMutant(
            "M50_contract_drop_graph_sheaf_h1",
            "contract_graph_coverage",
            "            if (vis[e->src] == UINT8_C(0) || vis[e->dst] == UINT8_C(0) || diff != sh) {\n"
            "                out->graph_sheaf_h1_obstructed = UINT32_C(1);\n"
            "                out->viol_flags |= (uint32_t)SOUND_CVIOL_GRAPH_SHEAF_H1;\n"
            "            }",
            "            if ((vis[e->src] == UINT8_C(0) || vis[e->dst] == UINT8_C(0) || diff != sh) &&\n"
            "                false) {\n"
            "                out->graph_sheaf_h1_obstructed = UINT32_C(1);\n"
            "                out->viol_flags |= (uint32_t)SOUND_CVIOL_GRAPH_SHEAF_H1;\n"
            "            }",
        ),
        SoundMutant(
            "M51_contract_drop_coverage_gap",
            "contract_graph_coverage",
            "    if (spec->coverage_gaps != UINT32_C(0)) {\n"
            "        out->viol_flags |= (uint32_t)SOUND_CVIOL_COVERAGE_GAP;\n"
            "    }",
            "    if (spec->coverage_gaps != UINT32_C(0) && false) {\n"
            "        out->viol_flags |= (uint32_t)SOUND_CVIOL_COVERAGE_GAP;\n"
            "    }",
        ),
    ]


def main() -> int:
    repo_root = Path(__file__).resolve().parents[1]
    return run_sabotage_suite(
        ring0_path=repo_root / "src" / "sound_ring0.c",
        cli_path=repo_root / "src" / "sound_cli.c",
        schema_hdr=repo_root / "build" / "sound_schema_abi.h",
        mutants=build_sound_mutants(),
        repo_root=repo_root,
        prefix="snd_mut_",
    )


if __name__ == "__main__":
    raise SystemExit(main())
