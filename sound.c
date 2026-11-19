#include "flow_ir.h"
#include "sound.h"

#include <stdio.h>

typedef struct {
    uint64_t s;
} sound_rng_t;

static inline uint64_t sound_splitmix64(sound_rng_t *rng) {
    uint64_t z = (rng->s += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30U)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27U)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31U);
}

static bool sound_cstr_eq(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return false;
    }
    size_t i = 0U;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return false;
        }
        i++;
    }
    return a[i] == b[i];
}

static void sound_label_copy(uint8_t dst[SOUND_LABEL_BYTES], const char *src) {
    size_t i = 0U;
    if (src != NULL) {
        while ((i + 1U) < SOUND_LABEL_BYTES && src[i] != '\0') {
            dst[i] = (uint8_t)src[i];
            i++;
        }
    }
    while (i < SOUND_LABEL_BYTES) {
        dst[i] = 0U;
        i++;
    }
}

static void sound_label_read(char dst[SOUND_LABEL_BYTES], const uint8_t src[SOUND_LABEL_BYTES]) {
    size_t i = 0U;
    while ((i + 1U) < SOUND_LABEL_BYTES && src[i] != 0U) {
        dst[i] = (char)src[i];
        i++;
    }
    while (i < SOUND_LABEL_BYTES) {
        dst[i] = '\0';
        i++;
    }
}

static bool sound_parse_u64(const char *s, uint64_t *out) {
    if (s == NULL || out == NULL || s[0] == '\0') {
        return false;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        uint64_t v = UINT64_C(0);
        size_t d = 0U;
        while (s[d] != '\0') {
            char ch = s[d];
            uint64_t nib = UINT64_C(0);
            if (ch >= '0' && ch <= '9') {
                nib = (uint64_t)(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                nib = (uint64_t)(ch - 'a') + UINT64_C(10);
            } else if (ch >= 'A' && ch <= 'F') {
                nib = (uint64_t)(ch - 'A') + UINT64_C(10);
            } else {
                return false;
            }
            if (d >= 16U) {
                return false;
            }
            v = (v << 4U) | nib;
            d++;
        }
        if (d == 0U) {
            return false;
        }
        *out = v;
        return true;
    }
    uint64_t v = UINT64_C(0);
    size_t d = 0U;
    while (s[d] != '\0') {
        char ch = s[d];
        if (ch < '0' || ch > '9' || d >= 19U) {
            return false;
        }
        v = (v * UINT64_C(10)) + (uint64_t)(ch - '0');
        d++;
    }
    *out = v;
    return true;
}

static void sound_set_node(SoundArena *arena, uint32_t id, const char *label, uint8_t sort,
                           uint8_t evid, uint8_t aspect, bool is_const, bool revoked, bool penumbra,
                           uint64_t presup_mask, uint64_t attack_mask, uint8_t num_clauses,
                           uint64_t c0, uint64_t c1) {
    SoundNodeMeta64 *nd = &arena->map.node_meta[id];
    nd->sort = sort;
    nd->evid = evid;
    nd->aspect = aspect;
    nd->num_clauses = num_clauses;
    nd->flags = is_const ? SOUND_NODE_FLAG_CONSTITUTIVE : UINT32_C(0);
    nd->presup_mask = presup_mask;
    nd->attack_mask = attack_mask;
    nd->span_start = 0U;
    nd->span_end = 0U;
    arena->map.node_clauses[id].clauses[0] = c0;
    arena->map.node_clauses[id].clauses[1] = c1;
    sound_label_copy(nd->label_bytes, label);
    if (revoked) {
        arena->map.norm.revoked_mask |= sound_bit64(id);
    }
    if (penumbra) {
        arena->map.norm.penumbra_mask |= sound_bit64(id);
    }
}

static const char *sound_status_name(uint8_t st) {
    if (st == (uint8_t)SOUND_STATUS_ESTABLISHED) {
        return "established";
    }
    if (st == (uint8_t)SOUND_STATUS_DEFEATED) {
        return "defeated";
    }
    if (st == (uint8_t)SOUND_STATUS_CONFLICT_UNDECIDED) {
        return "conflictUndecided";
    }
    if (st == (uint8_t)SOUND_STATUS_PENUMBRA_OPEN) {
        return "penumbraOpen";
    }
    return "unknown";
}

static bool sound_eval_flags_ok(const SoundArena *arena) {
    uint32_t req = SOUND_EVAL_FLAG_PARTITION_OK | SOUND_EVAL_FLAG_CONFLICT_FREE_OK |
                   SOUND_EVAL_FLAG_HOHFELD_OK | SOUND_EVAL_FLAG_FIXPOINT_OK;
    return (arena->map.eval_out.eval_flags & req) == req;
}

static uint32_t sound_test_t1_kernel(void) {
    uint32_t fails = UINT32_C(0);
    SoundArena arena;

    /* Case 1: uncorroborated hearsay + unverified presup */
    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 2U;
    sound_set_node(&arena, 0U, "wang_stopped", SOUND_SORT_FACTUAL, SOUND_EVID_REPORTATIVE,
                   SOUND_ASPECT_ACHIEVEMENT, false, false, false, UINT64_C(2), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 1U, "wang_prev_dump", SOUND_SORT_FACTUAL, SOUND_EVID_REPORTATIVE,
                   SOUND_ASPECT_ACTIVITY, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    if (sound_eval(&arena) != SOUND_FIBER_COMMITTED || !sound_eval_flags_ok(&arena) ||
        arena.map.eval_status.status[0] != SOUND_STATUS_DEFEATED ||
        arena.map.eval_status.status[1] != SOUND_STATUS_DEFEATED) {
        fails++;
    }

    /* Case 3: Park rule — Sedan established, Ambulance defeated, E-Scooter penumbraOpen */
    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 5U;
    sound_set_node(&arena, 0U, "park_rule", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 1U, "sedan_ban", SOUND_SORT_NORMATIVE, SOUND_EVID_DIRECT,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(1), UINT64_C(0));
    sound_set_node(&arena, 2U, "emergency_exc", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 3U, "ambulance_ban", SOUND_SORT_NORMATIVE, SOUND_EVID_DIRECT,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(4), 1U,
                   UINT64_C(1), UINT64_C(0));
    sound_set_node(&arena, 4U, "escooter_ban", SOUND_SORT_NORMATIVE, SOUND_EVID_DIRECT,
                   SOUND_ASPECT_STATE, false, false, true, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(1), UINT64_C(0));
    if (sound_eval(&arena) != SOUND_FIBER_COMMITTED || !sound_eval_flags_ok(&arena) ||
        arena.map.eval_status.status[1] != SOUND_STATUS_ESTABLISHED ||
        arena.map.eval_status.status[3] != SOUND_STATUS_DEFEATED ||
        arena.map.eval_status.status[4] != SOUND_STATUS_PENUMBRA_OPEN ||
        (arena.map.eval_reasons[0].reasons[3] & SOUND_REASON_ATTACKED) == UINT32_C(0)) {
        fails++;
    }

    /* Case 4: Two-Sided Hume Guillotine */
    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 6U;
    sound_set_node(&arena, 0U, "bloodstain", SOUND_SORT_FACTUAL, SOUND_EVID_DIRECT,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 1U, "presence", SOUND_SORT_FACTUAL, SOUND_EVID_DIRECT,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 2U, "injury_inf", SOUND_SORT_FACTUAL, SOUND_EVID_INFERENTIAL,
                   SOUND_ASPECT_ACHIEVEMENT, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(3), UINT64_C(0));
    sound_set_node(&arena, 3U, "suspect_guilty", SOUND_SORT_NORMATIVE, SOUND_EVID_INFERENTIAL,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(3), UINT64_C(0));
    sound_set_node(&arena, 4U, "enacted_statute", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 5U, "contra_factual", SOUND_SORT_FACTUAL, SOUND_EVID_DIRECT,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(16), UINT64_C(0));
    if (sound_eval(&arena) != SOUND_FIBER_COMMITTED || !sound_eval_flags_ok(&arena) ||
        arena.map.eval_out.established_mask != UINT64_C(0x17) ||
        arena.map.eval_out.defeated_mask != UINT64_C(0x28) ||
        (arena.map.eval_reasons[0].reasons[3] & SOUND_REASON_HUME_IS_TO_OUGHT) == UINT32_C(0) ||
        (arena.map.eval_reasons[0].reasons[5] & SOUND_REASON_HUME_OUGHT_TO_IS) == UINT32_C(0)) {
        fails++;
    }

    /* Nixon Diamond + 3-Stage Burden Shifting */
    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 2U;
    sound_set_node(&arena, 0U, "quaker", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(2), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 1U, "republican", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(1), 1U,
                   UINT64_C(0), UINT64_C(0));
    if (sound_eval(&arena) != SOUND_FIBER_COMMITTED || !sound_eval_flags_ok(&arena) ||
        arena.map.eval_out.conflict_mask != UINT64_C(0x3) ||
        arena.map.eval_out.i_star != UINT64_C(0) || arena.map.eval_out.p_star != UINT64_C(0x3)) {
        fails++;
    }

    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 3U;
    sound_set_node(&arena, 0U, "prima_facie", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(2), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 1U, "self_defense", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(4), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 2U, "excess_force", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    if (sound_eval(&arena) != SOUND_FIBER_COMMITTED || !sound_eval_flags_ok(&arena) ||
        arena.map.eval_out.established_mask != UINT64_C(0x5) ||
        arena.map.eval_out.defeated_mask != UINT64_C(0x2)) {
        fails++;
    }

    return fails;
}

static uint32_t sound_test_t2_hohfeld_and_rollback(void) {
    uint32_t fails = UINT32_C(0);
    SoundArena arena;

    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 4U;
    arena.map.norm.power_mask = UINT64_C(0xC);
    arena.map.norm.immunity_mask = UINT64_C(0x8);
    arena.map.norm.duty_pos_mask = UINT64_C(0x8);
    sound_set_node(&arena, 0U, "alice_bid", SOUND_SORT_FACTUAL, SOUND_EVID_DIRECT,
                   SOUND_ASPECT_ACHIEVEMENT, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 1U, "contract_formed", SOUND_SORT_INSTITUTIONAL, SOUND_EVID_DIRECT,
                   SOUND_ASPECT_ACHIEVEMENT, true, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(1), UINT64_C(0));
    sound_set_node(&arena, 2U, "alice_pay_duty", SOUND_SORT_NORMATIVE, SOUND_EVID_DIRECT,
                   SOUND_ASPECT_STATE, true, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(2), UINT64_C(0));
    sound_set_node(&arena, 3U, "venue_cleanup", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 2U,
                   UINT64_C(2), UINT64_C(0));

    uint32_t ep0 = arena.map.forest_ctrl.fiber_epoch;
    if (sound_apply_potestative(&arena, 3U, false, true, true, false) !=
            SOUND_FIBER_HOLD_IMMUNITY ||
        arena.map.forest_ctrl.last_fiber != (uint32_t)SOUND_FIBER_HOLD_IMMUNITY ||
        arena.map.forest_ctrl.fiber_epoch <= ep0 || arena.map.norm.duty_pos_mask != UINT64_C(0x8) ||
        !sound_hohfeld_coherent(&arena)) {
        fails++;
    }
    uint32_t ep1 = arena.map.forest_ctrl.fiber_epoch;
    if (sound_apply_potestative(&arena, 1U, true, false, false, false) !=
            SOUND_FIBER_HOLD_NO_POWER ||
        arena.map.forest_ctrl.last_fiber != (uint32_t)SOUND_FIBER_HOLD_NO_POWER ||
        arena.map.forest_ctrl.fiber_epoch <= ep1 || arena.map.norm.duty_pos_mask != UINT64_C(0x8)) {
        fails++;
    }
    uint32_t ep2 = arena.map.forest_ctrl.fiber_epoch;
    if (sound_apply_declarative(&arena, 0U, 2U, false, false, UINT64_C(0x1111)) !=
            SOUND_FIBER_HOLD_MISSING_CONTEXT ||
        arena.map.forest_ctrl.last_fiber != (uint32_t)SOUND_FIBER_HOLD_MISSING_CONTEXT ||
        arena.map.forest_ctrl.fiber_epoch <= ep2 || arena.map.img_hdr.phys_len != 1U) {
        fails++;
    }
    uint32_t ep3 = arena.map.forest_ctrl.fiber_epoch;
    if (sound_apply_declarative(&arena, 0U, 2U, true, false, UINT64_C(0x2222)) !=
            SOUND_FIBER_COMMITTED ||
        arena.map.forest_ctrl.last_fiber != (uint32_t)SOUND_FIBER_COMMITTED ||
        arena.map.forest_ctrl.fiber_epoch <= ep3 || arena.map.img_hdr.phys_len != 2U ||
        arena.map.norm.duty_pos_mask != UINT64_C(0xC) ||
        arena.map.norm.voidable_mask != UINT64_C(0x4) ||
        arena.map.norm.rescission_power_mask != UINT64_C(0x4)) {
        fails++;
    }
    uint32_t ep4 = arena.map.forest_ctrl.fiber_epoch;
    if (sound_exercise_rescission(&arena, 0U, 2U, UINT64_C(0x3333)) != SOUND_FIBER_COMMITTED ||
        arena.map.forest_ctrl.last_fiber != (uint32_t)SOUND_FIBER_COMMITTED ||
        arena.map.forest_ctrl.fiber_epoch <= ep4 || arena.map.img_hdr.phys_len != 3U ||
        arena.map.eval_out.established_mask != UINT64_C(0x8) ||
        arena.map.eval_out.defeated_mask != UINT64_C(0x7) ||
        arena.map.norm.duty_pos_mask != UINT64_C(0x8) ||
        arena.map.norm.voidable_mask != UINT64_C(0x0) ||
        arena.map.norm.rescission_power_mask != UINT64_C(0x0)) {
        fails++;
    }
    uint32_t ep5 = arena.map.forest_ctrl.fiber_epoch;
    if (sound_apply_potestative(&arena, 2U, true, false, false, false) != SOUND_FIBER_COMMITTED ||
        sound_apply_potestative(&arena, 2U, false, false, true, false) !=
            SOUND_FIBER_HOLD_INCOHERENT ||
        arena.map.forest_ctrl.last_fiber != (uint32_t)SOUND_FIBER_HOLD_INCOHERENT ||
        arena.map.forest_ctrl.fiber_epoch <= ep5 || arena.map.norm.duty_pos_mask != UINT64_C(0xC) ||
        arena.map.norm.duty_neg_mask != UINT64_C(0x0) || !sound_hohfeld_coherent(&arena)) {
        fails++;
    }

    return fails;
}

static uint32_t sound_test_t3_frontend_gate(void) {
    uint32_t fails = UINT32_C(0);
    SoundArena arena;
    uint32_t admitted = UINT32_MAX;

    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 2U;
    sound_set_node(&arena, 0U, "slot0", SOUND_SORT_FACTUAL, SOUND_EVID_ASSUMED, SOUND_ASPECT_STATE,
                   false, false, false, UINT64_C(0), UINT64_C(0), 0U, UINT64_C(0), UINT64_C(0));

    arena.map.forest_ctrl.utterance_bytes = 10U;
    arena.map.forest_ctrl.num_candidates = 1U;
    arena.map.candidate[0].span_start = 4U;
    arena.map.candidate[0].span_end = 12U;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FIBER_FALLBACK_SPAN ||
        arena.map.forest_ctrl.last_fiber != (uint32_t)SOUND_FIBER_FALLBACK_SPAN) {
        fails++;
    }

    arena.map.forest_ctrl.utterance_bytes = 24U;
    arena.map.forest_ctrl.ambiguity_flags = 1U;
    arena.map.candidate[0].span_end = 10U;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FIBER_FALLBACK_CLARIFY ||
        arena.map.forest_ctrl.last_fiber != (uint32_t)SOUND_FIBER_FALLBACK_CLARIFY) {
        fails++;
    }

    arena.map.forest_ctrl.ambiguity_flags = 0U;
    arena.map.forest_ctrl.unresolved_anaphora = 1U;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FIBER_FALLBACK_CLARIFY ||
        arena.map.forest_ctrl.last_fiber != (uint32_t)SOUND_FIBER_FALLBACK_CLARIFY) {
        fails++;
    }

    arena.map.forest_ctrl.unresolved_anaphora = 0U;
    arena.map.forest_ctrl.num_candidates = 2U;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FIBER_FALLBACK_CLARIFY ||
        arena.map.forest_ctrl.last_fiber != (uint32_t)SOUND_FIBER_FALLBACK_CLARIFY) {
        fails++;
    }

    arena.map.forest_ctrl.num_candidates = 1U;
    arena.map.candidate[0].force = (uint8_t)SOUND_FORCE_IRREALIS;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FIBER_FALLBACK_IRREALIS ||
        arena.map.forest_ctrl.last_fiber != (uint32_t)SOUND_FIBER_FALLBACK_IRREALIS) {
        fails++;
    }

    arena.map.candidate[0].target_node = 0U;
    arena.map.candidate[0].force = (uint8_t)SOUND_FORCE_ASSERTION;
    arena.map.candidate[0].sort = (uint8_t)SOUND_SORT_FACTUAL;
    arena.map.candidate[0].evid = (uint8_t)SOUND_EVID_DIRECT;
    arena.map.candidate[0].aspect = (uint8_t)SOUND_ASPECT_ACHIEVEMENT;
    arena.map.candidate[0].span_start = 0U;
    arena.map.candidate[0].span_end = 16U;
    arena.map.candidate[0].attack_mask = UINT64_C(0x2);
    arena.map.candidate[0].payload_hash = UINT64_C(0xBEEF);
    if (sound_admit_forest(&arena, &admitted) != SOUND_FIBER_COMMITTED ||
        arena.map.forest_ctrl.last_fiber != (uint32_t)SOUND_FIBER_COMMITTED || admitted != 0U ||
        arena.map.node_meta[0].attack_mask != UINT64_C(0x2) || arena.map.img_hdr.phys_len != 1U) {
        fails++;
    }

    /* Zero-Trust Prepared-Statement Policy/Input Isolation (9 checks) */
    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 4U;
    arena.map.norm.immunity_mask = UINT64_C(0x4);
    arena.map.node_meta[0].sort = (uint8_t)SOUND_SORT_INSTITUTIONAL;
    arena.map.node_meta[0].evid = (uint8_t)SOUND_EVID_ENACTED_NORM;
    arena.map.node_meta[0].flags = SOUND_NODE_FLAG_CONSTITUTIVE;
    arena.map.node_meta[0].num_clauses = 1U;
    arena.map.node_clauses[0].clauses[0] = UINT64_C(0x2);

    arena.map.node_meta[1].sort = (uint8_t)SOUND_SORT_FACTUAL;
    arena.map.node_meta[1].evid = (uint8_t)SOUND_EVID_ASSUMED;
    arena.map.node_meta[1].num_clauses = 1U;
    arena.map.node_clauses[1].clauses[0] = UINT64_C(0);

    arena.map.node_meta[2].sort = (uint8_t)SOUND_SORT_FACTUAL;
    arena.map.node_meta[2].evid = (uint8_t)SOUND_EVID_DIRECT;
    arena.map.node_meta[2].num_clauses = 1U;
    arena.map.node_clauses[2].clauses[0] = UINT64_C(0);

    arena.map.node_meta[3].sort = (uint8_t)SOUND_SORT_FACTUAL;
    arena.map.node_meta[3].evid = (uint8_t)SOUND_EVID_ASSUMED;
    arena.map.node_meta[3].num_clauses = 1U;
    arena.map.node_clauses[3].clauses[0] = UINT64_C(0);

    arena.map.forest_ctrl.utterance_bytes = 32U;
    arena.map.forest_ctrl.num_candidates = 1U;

    /* 1. Misconfigured seal rejections */
    if (sound_policy_seal(&arena, UINT64_C(0x3), UINT64_C(0x2), UINT32_C(0)) !=
            SOUND_FIBER_HOLD_INCOHERENT ||
        sound_policy_seal(&arena, UINT64_C(0x1), UINT64_C(0x4), UINT32_C(0)) !=
            SOUND_FIBER_HOLD_IMMUNITY ||
        sound_policy_seal(&arena, UINT64_C(0x0), UINT64_C(0x1), UINT32_C(0)) !=
            SOUND_FIBER_HOLD_INCOHERENT ||
        sound_policy_seal(&arena, UINT64_C(0x1), UINT64_C(0x2),
                          SOUND_FOREST_CTRL_NO_DIRECT_CLAIM) != SOUND_FIBER_COMMITTED ||
        sound_policy_verify(&arena) != SOUND_FIBER_COMMITTED ||
        sound_eval(&arena) != SOUND_FIBER_COMMITTED ||
        (arena.map.eval_out.established_mask & UINT64_C(0x3)) != UINT64_C(0)) {
        fails++;
    }

    /* 2. Injection block: Immunity slot (node 2) */
    arena.map.candidate[0].target_node = 2U;
    arena.map.candidate[0].force = (uint8_t)SOUND_FORCE_ASSERTION;
    arena.map.candidate[0].sort = (uint8_t)SOUND_SORT_FACTUAL;
    arena.map.candidate[0].evid = (uint8_t)SOUND_EVID_SENSORY;
    arena.map.candidate[0].aspect = (uint8_t)SOUND_ASPECT_STATE;
    arena.map.candidate[0].span_start = 0U;
    arena.map.candidate[0].span_end = 8U;
    arena.map.candidate[0].node_flags = UINT32_C(0);
    arena.map.candidate[0].presup_mask = UINT64_C(0);
    arena.map.candidate[0].premise_mask = UINT64_C(0);
    arena.map.candidate[0].attack_mask = UINT64_C(0);
    if (sound_admit_forest(&arena, &admitted) != SOUND_FIBER_FALLBACK_POLICY_VIOLATION ||
        arena.map.forest_ctrl.last_fiber != (uint32_t)SOUND_FIBER_FALLBACK_POLICY_VIOLATION ||
        admitted != UINT32_MAX ||
        (arena.map.forest_ctrl.last_reject_reason & (uint32_t)SOUND_INJECT_IMMUNITY_SLOT) ==
            UINT32_C(0) ||
        arena.map.node_meta[2].evid != (uint8_t)SOUND_EVID_DIRECT ||
        arena.map.phys_log[0].kind != (uint32_t)SOUND_PHYS_INJECT_BLOCKED) {
        fails++;
    }

    /* 3. Injection block: Policy slot (node 0) */
    arena.map.candidate[0].target_node = 0U;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FIBER_FALLBACK_POLICY_VIOLATION ||
        admitted != UINT32_MAX ||
        (arena.map.forest_ctrl.last_reject_reason & (uint32_t)SOUND_INJECT_POLICY_SLOT) ==
            UINT32_C(0) ||
        arena.map.node_meta[0].sort != (uint8_t)SOUND_SORT_INSTITUTIONAL) {
        fails++;
    }

    /* 4. Injection block: Unbound slot (node 3) */
    arena.map.candidate[0].target_node = 3U;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FIBER_FALLBACK_POLICY_VIOLATION ||
        admitted != UINT32_MAX ||
        arena.map.forest_ctrl.last_reject_reason != (uint32_t)SOUND_INJECT_UNBOUND_SLOT ||
        arena.map.node_meta[3].evid != (uint8_t)SOUND_EVID_ASSUMED) {
        fails++;
    }

    /* 5. Injection block: Cross-plane sort + privilege escalation on bind slot (node 1) */
    arena.map.candidate[0].target_node = 1U;
    arena.map.candidate[0].sort = (uint8_t)SOUND_SORT_INSTITUTIONAL;
    arena.map.candidate[0].evid = (uint8_t)SOUND_EVID_ENACTED_NORM;
    arena.map.candidate[0].force = (uint8_t)SOUND_FORCE_DECLARATIVE;
    arena.map.candidate[0].node_flags = SOUND_NODE_FLAG_CONSTITUTIVE;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FIBER_FALLBACK_POLICY_VIOLATION ||
        admitted != UINT32_MAX ||
        (arena.map.forest_ctrl.last_reject_reason &
         ((uint32_t)SOUND_INJECT_CROSS_PLANE_SORT | (uint32_t)SOUND_INJECT_PRIV_ESCALATION)) !=
            ((uint32_t)SOUND_INJECT_CROSS_PLANE_SORT | (uint32_t)SOUND_INJECT_PRIV_ESCALATION) ||
        arena.map.node_meta[1].sort != (uint8_t)SOUND_SORT_FACTUAL) {
        fails++;
    }

    /* 6. Injection block: Topology mutation on bind slot (node 1) */
    arena.map.candidate[0].sort = (uint8_t)SOUND_SORT_FACTUAL;
    arena.map.candidate[0].evid = (uint8_t)SOUND_EVID_SENSORY;
    arena.map.candidate[0].force = (uint8_t)SOUND_FORCE_ASSERTION;
    arena.map.candidate[0].node_flags = UINT32_C(0);
    arena.map.candidate[0].attack_mask = UINT64_C(0x1);
    if (sound_admit_forest(&arena, &admitted) != SOUND_FIBER_FALLBACK_POLICY_VIOLATION ||
        admitted != UINT32_MAX ||
        arena.map.forest_ctrl.last_reject_reason != (uint32_t)SOUND_INJECT_TOPOLOGY_MUTATION ||
        arena.map.node_meta[1].attack_mask != UINT64_C(0)) {
        fails++;
    }

    /* 7. Injection block: NO_DIRECT_CLAIM enforcement on bind slot (node 1) */
    arena.map.candidate[0].attack_mask = UINT64_C(0);
    arena.map.candidate[0].evid = (uint8_t)SOUND_EVID_DIRECT;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FIBER_FALLBACK_POLICY_VIOLATION ||
        admitted != UINT32_MAX ||
        arena.map.forest_ctrl.last_reject_reason != (uint32_t)SOUND_INJECT_DIRECT_CLAIM_FORBID ||
        arena.map.node_meta[1].evid != (uint8_t)SOUND_EVID_ASSUMED) {
        fails++;
    }

    /* 8. Valid parameter binding & eval propagation + seal verification */
    arena.map.candidate[0].evid = (uint8_t)SOUND_EVID_SENSORY;
    arena.map.candidate[0].aspect = (uint8_t)SOUND_ASPECT_ACHIEVEMENT;
    arena.map.candidate[0].span_start = 2U;
    arena.map.candidate[0].span_end = 18U;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FIBER_COMMITTED ||
        arena.map.forest_ctrl.last_fiber != (uint32_t)SOUND_FIBER_COMMITTED || admitted != 1U ||
        arena.map.forest_ctrl.last_reject_reason != (uint32_t)SOUND_INJECT_NONE ||
        arena.map.node_meta[1].evid != (uint8_t)SOUND_EVID_SENSORY ||
        sound_policy_verify(&arena) != SOUND_FIBER_COMMITTED ||
        sound_eval(&arena) != SOUND_FIBER_COMMITTED ||
        (arena.map.eval_out.established_mask & UINT64_C(0x3)) != UINT64_C(0x3)) {
        fails++;
    }
    sound_arena_seal(&arena);
    if (sound_arena_verify(&arena) != SOUND_FIBER_COMMITTED) {
        fails++;
    }

    /* 9. Post-seal policy tamper fail-closed */
    arena.map.node_clauses[0].clauses[0] = UINT64_C(0);
    sound_arena_seal(&arena);
    if (sound_policy_verify(&arena) != SOUND_FIBER_FALLBACK_POLICY_TAMPER ||
        sound_arena_verify(&arena) != SOUND_FIBER_FALLBACK_POLICY_TAMPER ||
        sound_admit_forest(&arena, &admitted) != SOUND_FIBER_FALLBACK_POLICY_TAMPER ||
        arena.map.forest_ctrl.last_fiber != (uint32_t)SOUND_FIBER_FALLBACK_POLICY_TAMPER ||
        admitted != UINT32_MAX ||
        arena.map.forest_ctrl.last_reject_reason != (uint32_t)SOUND_INJECT_POLICY_TAMPER) {
        fails++;
    }

    return fails;
}

static uint32_t sound_test_t4_arena_merkle(void) {
    uint32_t fails = UINT32_C(0);
    SoundArena src;
    SoundArena dst;

    sound_arena_init(&src);
    src.map.img_hdr.num_nodes = 3U;
    src.map.norm.immunity_mask = UINT64_C(0x4);
    src.map.norm.voidable_mask = UINT64_C(0x2);
    src.map.norm.rescission_power_mask = UINT64_C(0x2);
    sound_set_node(&src, 0U, "obs_rain", SOUND_SORT_FACTUAL, SOUND_EVID_SENSORY, SOUND_ASPECT_STATE,
                   false, false, false, UINT64_C(0), UINT64_C(0), 1U, UINT64_C(0), UINT64_C(0));
    sound_set_node(&src, 1U, "wet_ground", SOUND_SORT_FACTUAL, SOUND_EVID_INFERENTIAL,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(1), UINT64_C(0));
    sound_set_node(&src, 2U, "slip_warn", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(1), UINT64_C(0), 1U,
                   UINT64_C(2), UINT64_C(0));
    sound_arena_seal(&src);

    uint8_t wire[SOUND_ARENA_BYTES];
    for (size_t i = 0U; i < SOUND_ARENA_CELLS; i++) {
        flow_cell_to_bytes(src.cells[i].u64, wire + (i * 64U));
    }
    for (size_t i = 0U; i < SOUND_ARENA_CELLS; i++) {
        flow_cell_from_bytes(dst.cells[i].u64, wire + (i * 64U));
    }
    if (sound_arena_verify(&dst) != SOUND_FIBER_COMMITTED ||
        sound_eval(&dst) != SOUND_FIBER_COMMITTED || dst.map.norm.immunity_mask != UINT64_C(0x4) ||
        dst.map.norm.voidable_mask != UINT64_C(0x2) ||
        dst.map.norm.rescission_power_mask != UINT64_C(0x2) ||
        dst.map.eval_out.established_mask != UINT64_C(0x7)) {
        fails++;
    }

    /* Bit flip in Arena tail must trigger SOUND_FIBER_FALLBACK_MAC */
    dst.cells[1].bytes[0] ^= UINT8_C(0x01);
    if (sound_arena_verify(&dst) != SOUND_FIBER_FALLBACK_MAC) {
        fails++;
    }
    dst.cells[1].bytes[0] ^= UINT8_C(0x01);

    /* Bad magic and schema_hash must trigger SOUND_FIBER_FALLBACK_FORMAT */
    dst.map.img_hdr.magic ^= UINT32_C(1);
    if (sound_arena_verify(&dst) != SOUND_FIBER_FALLBACK_FORMAT) {
        fails++;
    }
    dst.map.img_hdr.magic = SOUND_MAGIC;
    dst.map.img_hdr.schema_hash ^= UINT64_C(1);
    if (sound_arena_verify(&dst) != SOUND_FIBER_FALLBACK_FORMAT) {
        fails++;
    }

    return fails;
}

static uint32_t sound_test_t5_searu_repair(void) {
    uint32_t fails = UINT32_C(0);
    SoundArena arena;

    /* 1. Nixon Diamond without Immunity pin: zero-energy maximal conflict-free extension */
    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 2U;
    sound_set_node(&arena, 0U, "quaker", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(2), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 1U, "republican", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(1), 1U,
                   UINT64_C(0), UINT64_C(0));
    if (sound_searu_repair_conflict(&arena, UINT64_C(0x53454152555F3031), 1024U) !=
            SOUND_FIBER_COMMITTED ||
        arena.map.searu_repair.best_energy != UINT64_C(0) ||
        arena.map.searu_repair.converged != UINT32_C(1) ||
        (arena.map.searu_repair.candidate_mask != UINT64_C(0x1) &&
         arena.map.searu_repair.candidate_mask != UINT64_C(0x2)) ||
        (arena.map.searu_repair.candidate_mask | arena.map.searu_repair.retracted_mask) !=
            UINT64_C(0x3)) {
        fails++;
    }

    /* 2. Nixon Diamond with Immunity pin on node 1 -> MUST keep node 1 and retract node 0 */
    arena.map.norm.immunity_mask = UINT64_C(0x2);
    if (sound_searu_repair_conflict(&arena, UINT64_C(0x53454152555F3032), 1024U) !=
            SOUND_FIBER_COMMITTED ||
        arena.map.searu_repair.best_energy != UINT64_C(0) ||
        arena.map.searu_repair.candidate_mask != UINT64_C(0x2) ||
        arena.map.searu_repair.retracted_mask != UINT64_C(0x1)) {
        fails++;
    }

    /* 3. Conflict-free graph (Case 3) -> zero iterations, preserves established nodes */
    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 5U;
    sound_set_node(&arena, 0U, "park_rule", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 1U, "sedan_ban", SOUND_SORT_NORMATIVE, SOUND_EVID_DIRECT,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(1), UINT64_C(0));
    sound_set_node(&arena, 2U, "emergency_exc", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 3U, "ambulance_ban", SOUND_SORT_NORMATIVE, SOUND_EVID_DIRECT,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(4), 1U,
                   UINT64_C(1), UINT64_C(0));
    sound_set_node(&arena, 4U, "escooter_ban", SOUND_SORT_NORMATIVE, SOUND_EVID_DIRECT,
                   SOUND_ASPECT_STATE, false, false, true, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(1), UINT64_C(0));
    if (sound_searu_repair_conflict(&arena, UINT64_C(0x53454152555F3033), 1024U) !=
            SOUND_FIBER_COMMITTED ||
        arena.map.searu_repair.best_energy != UINT64_C(0) ||
        arena.map.searu_repair.iters_used != UINT32_C(0) ||
        arena.map.searu_repair.candidate_mask != UINT64_C(0x7) ||
        arena.map.searu_repair.retracted_mask != UINT64_C(0)) {
        fails++;
    }

    return fails;
}

static uint32_t sound_test_t6_fuzz_4096(void) {
    uint32_t fails = UINT32_C(0);
    sound_rng_t rng = {UINT64_C(0x534F554E445F4655)};

    for (uint32_t g = 0U; g < 4096U; g++) {
        SoundArena arena;
        uint32_t n = (uint32_t)((sound_splitmix64(&rng) % 12U) + 1U);
        uint64_t nm = sound_mask_n(n);
        sound_arena_init(&arena);
        arena.map.img_hdr.num_nodes = n;
        arena.map.norm.revoked_mask = sound_splitmix64(&rng) & nm & UINT64_C(0x555);
        arena.map.norm.penumbra_mask = sound_splitmix64(&rng) & nm & UINT64_C(0x222);

        bool no_norm_source = true;
        for (uint32_t u = 0U; u < n; u++) {
            uint8_t sort = (uint8_t)(sound_splitmix64(&rng) % 3U);
            uint8_t evid = (uint8_t)(sound_splitmix64(&rng) % 6U);
            bool ic = (sound_splitmix64(&rng) & UINT64_C(7)) == UINT64_C(0);
            if (evid == (uint8_t)SOUND_EVID_ENACTED_NORM || ic) {
                no_norm_source = false;
            }
            uint64_t att = sound_splitmix64(&rng) & nm & (~sound_bit64(u));
            uint64_t pre = sound_splitmix64(&rng) & sound_splitmix64(&rng) & nm;
            uint8_t nc = (uint8_t)((sound_splitmix64(&rng) % 3U) + 1U);
            uint64_t c0 = (sound_splitmix64(&rng) & 1U) != 0U
                              ? UINT64_C(0)
                              : (sound_splitmix64(&rng) & sound_splitmix64(&rng) & nm);
            uint64_t c1 = sound_splitmix64(&rng) & sound_splitmix64(&rng) & nm;
            sound_set_node(&arena, u, "rnd", sort, evid, SOUND_ASPECT_STATE, ic, false, false, pre,
                           att, nc, c0, c1);
            if (nc > 2U) {
                arena.map.node_clauses[u].clauses[2] = sound_splitmix64(&rng) & nm;
            }
        }

        if (sound_eval(&arena) != SOUND_FIBER_COMMITTED || !sound_eval_flags_ok(&arena)) {
            fails++;
            break;
        }
        if ((arena.map.eval_out.established_mask & arena.map.norm.revoked_mask) != UINT64_C(0) ||
            (arena.map.eval_out.i_star & arena.map.norm.penumbra_mask) != UINT64_C(0) ||
            (arena.map.eval_out.p_star & arena.map.norm.penumbra_mask) != UINT64_C(0)) {
            fails++;
            break;
        }
        for (uint32_t u = 0U; u < n; u++) {
            if (arena.map.eval_status.status[u] == (uint8_t)SOUND_STATUS_ESTABLISHED) {
                if (arena.map.node_meta[u].evid >= (uint8_t)SOUND_EVID_INFERENTIAL) {
                    bool has_non_empty = false;
                    for (uint32_t c = 0U; c < arena.map.node_meta[u].num_clauses; c++) {
                        if (arena.map.node_clauses[u].clauses[c] != UINT64_C(0)) {
                            has_non_empty = true;
                        }
                    }
                    if (!has_non_empty) {
                        fails++;
                    }
                }
                if (no_norm_source && arena.map.node_meta[u].sort != (uint8_t)SOUND_SORT_FACTUAL) {
                    fails++;
                }
            }
        }
    }
    return fails;
}

static uint32_t sound_test_t7_fir_lift(void) {
    uint32_t fails = UINT32_C(0);
    SoundArena arena;
    uint8_t fir_buf[SOUND_FIR_BYTES];
    size_t fir_len = 0U;
    flow_ir_prog_t prog;
    flow_ir_dag_stats_t dag;

    /* 1. Lift Case 3 Park Rule (power=0x4, immunity=0x2, established=0x7) */
    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 5U;
    arena.map.norm.duty_pos_mask = UINT64_C(0x2);
    arena.map.norm.duty_neg_mask = UINT64_C(0x8);
    arena.map.norm.power_mask = UINT64_C(0x4);
    arena.map.norm.immunity_mask = UINT64_C(0x2);
    arena.map.norm.rescission_power_mask = UINT64_C(0x4);
    sound_set_node(&arena, 0U, "park_rule", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 1U, "sedan_ban", SOUND_SORT_NORMATIVE, SOUND_EVID_DIRECT,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(1), UINT64_C(0));
    sound_set_node(&arena, 2U, "emergency_exc", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 3U, "ambulance_ban", SOUND_SORT_NORMATIVE, SOUND_EVID_DIRECT,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(4), 1U,
                   UINT64_C(1), UINT64_C(0));
    sound_set_node(&arena, 4U, "escooter_ban", SOUND_SORT_NORMATIVE, SOUND_EVID_DIRECT,
                   SOUND_ASPECT_STATE, false, false, true, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(1), UINT64_C(0));
    if (sound_eval(&arena) != SOUND_FIBER_COMMITTED ||
        sound_lift_to_fir(&arena, "case3_park", UINT32_C(0), fir_buf, sizeof(fir_buf), &fir_len) !=
            SOUND_FIBER_COMMITTED ||
        fir_len != (size_t)SOUND_FIR_BYTES || !flow_ir_load_bytes(&prog, fir_buf, fir_len) ||
        !flow_ir_validate(&prog) || !flow_ir_admits_unitary(&prog) ||
        !flow_ir_analyze_dag(&prog, &dag)) {
        return UINT32_C(1);
    }

    /* Immunity lock (bit 1) & No-Power lock (bit 0) prevent unauthorized mutation */
    {
        uint64_t inv[5] = {UINT64_C(0x2), UINT64_C(0), UINT64_C(0x1), UINT64_C(0), UINT64_C(0x2)};
        uint64_t outv[4] = {0};
        if (!flow_ir_eval_u64(&prog, inv, outv) || outv[0] != UINT64_C(0x2) ||
            outv[1] != UINT64_C(0) || outv[2] != UINT64_C(0x4) || outv[3] != UINT64_C(0x7)) {
            fails++;
        }
    }
    /* Contradictory duty request on slot 2 rolls back */
    {
        uint64_t inv[5] = {UINT64_C(0x2), UINT64_C(0), UINT64_C(0x6), UINT64_C(0x4), UINT64_C(0)};
        uint64_t outv[4] = {0};
        if (!flow_ir_eval_u64(&prog, inv, outv) || outv[0] != UINT64_C(0x2) ||
            outv[1] != UINT64_C(0)) {
            fails++;
        }
    }
    /* Valid grant on slot 2 + linear rescission consumption on slot 2 */
    {
        uint64_t inv_a[5] = {UINT64_C(0x2), UINT64_C(0), UINT64_C(0x6), UINT64_C(0), UINT64_C(0)};
        uint64_t out_a[4] = {0};
        if (!flow_ir_eval_u64(&prog, inv_a, out_a) || out_a[0] != UINT64_C(0x6) ||
            out_a[2] != UINT64_C(0x4)) {
            fails++;
        }
        uint64_t inv_b[5] = {UINT64_C(0x6), UINT64_C(0), UINT64_C(0x6), UINT64_C(0), UINT64_C(0x4)};
        uint64_t out_b[4] = {0};
        if (!flow_ir_eval_u64(&prog, inv_b, out_b) || out_b[0] != UINT64_C(0x2) ||
            out_b[2] != UINT64_C(0)) {
            fails++;
        }
    }

    /* 2. Nixon Diamond repaired via SEARU lifted with SOUND_FIR_FLAG_USE_REPAIRED */
    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 2U;
    sound_set_node(&arena, 0U, "quaker", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(2), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 1U, "republican", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(1), 1U,
                   UINT64_C(0), UINT64_C(0));
    if (sound_searu_repair_conflict(&arena, UINT64_C(0x53454152555F3031), 1024U) !=
            SOUND_FIBER_COMMITTED ||
        sound_lift_to_fir(&arena, "nixon_repaired", SOUND_FIR_FLAG_USE_REPAIRED, fir_buf,
                          sizeof(fir_buf), &fir_len) != SOUND_FIBER_COMMITTED ||
        !flow_ir_load_bytes(&prog, fir_buf, fir_len) || !flow_ir_admits_unitary(&prog)) {
        fails++;
    } else {
        uint64_t inv[5] = {0};
        uint64_t outv[4] = {0};
        if (!flow_ir_eval_u64(&prog, inv, outv) ||
            outv[3] != arena.map.searu_repair.candidate_mask) {
            fails++;
        }
    }

    return fails;
}

static uint32_t sound_test_t8_siphash_and_atomic_batch(void) {
    uint32_t fails = UINT32_C(0);
    /* 1. Official SipHash-2-4 Aumasson-Bernstein 15-byte test vector:
     *    key = 00..0f, msg = 00..0e -> tag = 0xa129ca6149be45e5ULL */
    uint8_t msg15[15];
    for (uint32_t i = 0U; i < 15U; i++) {
        msg15[i] = (uint8_t)i;
    }
    uint64_t tv = sound_siphash24_bytes(UINT64_C(0x0706050403020100), UINT64_C(0x0F0E0D0C0B0A0908),
                                        msg15, 15U);
    if (tv != UINT64_C(0xA129CA6149BE45E5)) {
        fails++;
    }

    static SoundArena arena;
    const uint64_t k0 = UINT64_C(0x534F554E445F4B30);
    const uint64_t k1 = UINT64_C(0x5052455041524531);
    const uint64_t bad_k0 = UINT64_C(0xDEAD0000BEEF0000);
    static const uint8_t utt[] = "fact0;fact1;fact2;";

    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 5U;
    sound_set_node(&arena, 0U, "stat_root", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 1U, "bind_a", SOUND_SORT_FACTUAL, SOUND_EVID_ASSUMED, SOUND_ASPECT_STATE,
                   false, false, false, UINT64_C(0), UINT64_C(0), 1U, UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 2U, "bind_b", SOUND_SORT_FACTUAL, SOUND_EVID_ASSUMED, SOUND_ASPECT_STATE,
                   false, false, false, UINT64_C(0), UINT64_C(0), 1U, UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 3U, "bind_c", SOUND_SORT_FACTUAL, SOUND_EVID_ASSUMED, SOUND_ASPECT_STATE,
                   false, false, false, UINT64_C(0), UINT64_C(0), 1U, UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 4U, "rule_out", SOUND_SORT_NORMATIVE, SOUND_EVID_INFERENTIAL,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0xF), UINT64_C(0));

    uint64_t pol_m = UINT64_C(0x11);
    uint64_t bnd_m = UINT64_C(0x0E);
    uint32_t cfl = SOUND_FOREST_CTRL_NO_DIRECT_CLAIM | SOUND_FOREST_CTRL_PROPOSER_LOW_EVID;
    if (sound_policy_seal_keyed(&arena, pol_m, bnd_m, cfl, k0, k1) != SOUND_FIBER_COMMITTED ||
        sound_policy_verify_keyed(&arena, k0, k1) != SOUND_FIBER_COMMITTED ||
        sound_policy_verify_keyed(&arena, bad_k0, k1) != SOUND_FIBER_FALLBACK_POLICY_TAMPER ||
        sound_policy_verify(&arena) != SOUND_FIBER_FALLBACK_POLICY_TAMPER) {
        fails++;
    }

    /* 2. Utterance span hash check + evid ceiling check */
    sound_zero_bytes(arena.map.candidate, sizeof(arena.map.candidate));
    arena.map.forest_ctrl.utterance_bytes = (uint32_t)(sizeof(utt) - 1U);
    arena.map.forest_ctrl.num_candidates = 1U;
    arena.map.candidate[0].target_node = 1U;
    arena.map.candidate[0].force = SOUND_FORCE_ASSERTION;
    arena.map.candidate[0].sort = SOUND_SORT_FACTUAL;
    arena.map.candidate[0].evid = SOUND_EVID_SENSORY;
    arena.map.candidate[0].span_start = 0U;
    arena.map.candidate[0].span_end = 5U;
    arena.map.candidate[0].payload_hash = UINT64_C(0x123456789ABCDEF0); /* mismatch */
    uint32_t n_adm = 0U;
    if (sound_admit_forest_batch_keyed(&arena, utt, sizeof(utt) - 1U, k0, k1, &n_adm) !=
            SOUND_FIBER_FALLBACK_SPAN ||
        arena.map.forest_ctrl.last_reject_reason != (uint32_t)SOUND_INJECT_SPAN_HASH_MISMATCH ||
        arena.map.node_meta[1].evid != (uint8_t)SOUND_EVID_ASSUMED) {
        fails++;
    }

    /* 3. Atomic 3-candidate batch rollback when candidate[2] violates policy/evid/duplicate */
    arena.map.forest_ctrl.num_candidates = 3U;
    for (uint32_t ci = 0U; ci < 3U; ci++) {
        arena.map.candidate[ci].target_node = 1U + ci;
        arena.map.candidate[ci].force = SOUND_FORCE_ASSERTION;
        arena.map.candidate[ci].sort = SOUND_SORT_FACTUAL;
        arena.map.candidate[ci].evid = SOUND_EVID_REPORTATIVE;
        arena.map.candidate[ci].span_start = ci * 6U;
        arena.map.candidate[ci].span_end = ci * 6U + 5U;
        arena.map.candidate[ci].payload_hash =
            sound_fnv1a_bytes(SOUND_FNV1A_OFFSET, utt + (ci * 6U), 5U);
    }
    /* Violation a: candidate[2] tries to exceed low evid ceiling (sensory < reportative) */
    arena.map.candidate[2].evid = SOUND_EVID_SENSORY;
    if (sound_admit_forest_batch_keyed(&arena, utt, sizeof(utt) - 1U, k0, k1, &n_adm) !=
            SOUND_FIBER_FALLBACK_POLICY_VIOLATION ||
        (arena.map.forest_ctrl.last_reject_reason & (uint32_t)SOUND_INJECT_EVID_CEILING) == 0U ||
        arena.map.node_meta[1].evid != (uint8_t)SOUND_EVID_ASSUMED ||
        arena.map.node_meta[2].evid != (uint8_t)SOUND_EVID_ASSUMED) {
        fails++;
    }
    /* Violation b: duplicate target_node between candidate[0] and candidate[2] */
    arena.map.candidate[2].evid = SOUND_EVID_REPORTATIVE;
    arena.map.candidate[2].target_node = 1U;
    if (sound_admit_forest_batch_keyed(&arena, utt, sizeof(utt) - 1U, k0, k1, &n_adm) !=
            SOUND_FIBER_FALLBACK_POLICY_VIOLATION ||
        arena.map.forest_ctrl.last_reject_reason != (uint32_t)SOUND_INJECT_DUPLICATE_TARGET ||
        arena.map.node_meta[1].evid != (uint8_t)SOUND_EVID_ASSUMED) {
        fails++;
    }
    /* Clean commit across all 3 candidates under PROPOSER_LOW_EVID: reportative hearsay leaves stay
     * quarantined */
    arena.map.candidate[2].target_node = 3U;
    if (sound_admit_forest_batch_keyed(&arena, utt, sizeof(utt) - 1U, k0, k1, &n_adm) !=
            SOUND_FIBER_COMMITTED ||
        n_adm != 3U || arena.map.node_meta[1].evid != (uint8_t)SOUND_EVID_REPORTATIVE ||
        arena.map.node_meta[2].evid != (uint8_t)SOUND_EVID_REPORTATIVE ||
        arena.map.node_meta[3].evid != (uint8_t)SOUND_EVID_REPORTATIVE ||
        sound_eval(&arena) != SOUND_FIBER_COMMITTED ||
        arena.map.eval_out.established_mask != UINT64_C(0x1) ||
        (arena.map.eval_reasons[0].reasons[1] & (uint32_t)SOUND_REASON_EVIDENTIAL_LEAF) == 0U) {
        fails++;
    }

    /* 4. Keyed batch without PROPOSER_LOW_EVID: SOUND_EVID_SENSORY receipts establish all 5 nodes
     */
    if (sound_policy_seal_keyed(&arena, pol_m, bnd_m, SOUND_FOREST_CTRL_NO_DIRECT_CLAIM, k0, k1) !=
        SOUND_FIBER_COMMITTED) {
        fails++;
    } else {
        for (uint32_t ci = 0U; ci < 3U; ci++) {
            arena.map.candidate[ci].evid = SOUND_EVID_SENSORY;
        }
        if (sound_admit_forest_batch_keyed(&arena, utt, sizeof(utt) - 1U, k0, k1, &n_adm) !=
                SOUND_FIBER_COMMITTED ||
            n_adm != 3U || sound_eval(&arena) != SOUND_FIBER_COMMITTED ||
            (arena.map.eval_out.established_mask & UINT64_C(0x1F)) != UINT64_C(0x1F)) {
            fails++;
        }
    }
    return fails;
}

static uint32_t sound_test_t9_lex_specialis_and_chart_glue(void) {
    uint32_t fails = UINT32_C(0);
    static SoundArena a, b;

    /* 1. Lex Specialis priority weights vs KanLock dominance */
    sound_arena_init(&a);
    a.map.img_hdr.num_nodes = 2U;
    sound_set_node(&a, 0U, "gen_rule", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(2), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&a, 1U, "spec_rule", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(1), 1U,
                   UINT64_C(0), UINT64_C(0));
    uint8_t w1[SOUND_MAX_NODES];
    sound_zero_bytes(w1, sizeof(w1));
    w1[0] = UINT8_C(10);
    w1[1] = UINT8_C(100);
    if (sound_set_priority_weights(&a, w1) != SOUND_FIBER_COMMITTED ||
        sound_searu_repair_conflict_weighted(&a, UINT64_C(0x123456789ABCDEF1), 1024U, w1) !=
            SOUND_FIBER_COMMITTED ||
        a.map.searu_repair.converged != UINT32_C(1) ||
        a.map.searu_repair.candidate_mask != UINT64_C(2) ||
        a.map.searu_repair.best_energy != UINT64_C(10)) {
        fails++;
    }
    /* Flip weights: keep node 0 instead */
    w1[0] = UINT8_C(120);
    w1[1] = UINT8_C(15);
    if (sound_searu_repair_conflict_weighted(&a, UINT64_C(0xCAFEBABE11223344), 1024U, w1) !=
            SOUND_FIBER_COMMITTED ||
        a.map.searu_repair.converged != UINT32_C(1) ||
        a.map.searu_repair.candidate_mask != UINT64_C(1) ||
        a.map.searu_repair.best_energy != UINT64_C(15)) {
        fails++;
    }
    /* Pin lower-weight node 1 via KanLock: immunity dominates weight 255 vs 1 */
    a.map.norm.immunity_mask = UINT64_C(2);
    w1[0] = UINT8_C(255);
    w1[1] = UINT8_C(1);
    if (sound_searu_repair_conflict_weighted(&a, UINT64_C(0x778899AABBCCDDEE), 1024U, w1) !=
            SOUND_FIBER_COMMITTED ||
        a.map.searu_repair.converged != UINT32_C(1) ||
        a.map.searu_repair.candidate_mask != UINT64_C(2)) {
        fails++;
    }

    /* 2. Cross-Chart Sheaf Gluing: src -> dst */
    sound_arena_init(&a);
    a.map.img_hdr.num_nodes = 3U;
    sound_set_node(&a, 0U, "src_root", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&a, 1U, "src_export", SOUND_SORT_NORMATIVE, SOUND_EVID_INFERENTIAL,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(1), UINT64_C(0), 1U,
                   UINT64_C(1), UINT64_C(0));
    sound_set_node(&a, 2U, "src_conf", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(4), 1U,
                   UINT64_C(0), UINT64_C(0));
    (void)sound_eval(&a);
    sound_arena_seal(&a);

    sound_arena_init(&b);
    b.map.img_hdr.num_nodes = 3U;
    sound_set_node(&b, 0U, "dst_stat", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&b, 1U, "dst_imp", SOUND_SORT_FACTUAL, SOUND_EVID_ASSUMED, SOUND_ASPECT_STATE,
                   false, false, false, UINT64_C(0), UINT64_C(0), 1U, UINT64_C(0), UINT64_C(0));
    sound_set_node(&b, 2U, "dst_eff", SOUND_SORT_NORMATIVE, SOUND_EVID_INFERENTIAL,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(2), UINT64_C(0), 1U,
                   UINT64_C(3), UINT64_C(0));
    if (sound_policy_seal(&b, UINT64_C(0x5), UINT64_C(0x2), UINT32_C(0)) != SOUND_FIBER_COMMITTED) {
        fails++;
    }
    uint8_t slot_map[SOUND_MAX_NODES];
    sound_zero_bytes(slot_map, sizeof(slot_map));
    slot_map[1] = UINT8_C(1);

    /* Reject exporting non-established / conflicted node or importing into policy slot */
    if (sound_chart_glue_import(&b, &a, UINT64_C(4), slot_map) != SOUND_FIBER_HOLD_INCOHERENT) {
        fails++;
    }
    uint8_t bad_map[SOUND_MAX_NODES];
    sound_zero_bytes(bad_map, sizeof(bad_map));
    bad_map[1] = UINT8_C(2);
    if (sound_chart_glue_import(&b, &a, UINT64_C(2), bad_map) !=
        SOUND_FIBER_FALLBACK_POLICY_VIOLATION) {
        fails++;
    }
    /* Clean import + verify + downstream firing */
    if (sound_chart_glue_import(&b, &a, UINT64_C(2), slot_map) != SOUND_FIBER_COMMITTED ||
        sound_chart_glue_verify(&b, &a) != SOUND_FIBER_COMMITTED ||
        sound_eval(&b) != SOUND_FIBER_COMMITTED ||
        (b.map.eval_out.established_mask & UINT64_C(0x7)) != UINT64_C(0x7)) {
        fails++;
    }
    /* Tampering src invalidates glue verification */
    a.map.node_meta[0].span_end ^= UINT32_C(1);
    if (sound_chart_glue_verify(&b, &a) != SOUND_FIBER_FALLBACK_MAC) {
        fails++;
    }
    return fails;
}

static uint32_t sound_test_t10_policy_eval_fir_lift(void) {
    uint32_t fails = UINT32_C(0);
    static SoundArena arena, sim;
    static uint8_t fir_buf[65536];
    size_t fir_len = 0U;
    static flow_ir_prog_t prog;

    /* 6-node stratified policy with root, 2 bind slots, derived rule, attacking proviso, penumbra
     */
    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 6U;
    sound_set_node(&arena, 0U, "statute", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 1U, "bind_main", SOUND_SORT_FACTUAL, SOUND_EVID_ASSUMED,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 2U, "bind_exc", SOUND_SORT_FACTUAL, SOUND_EVID_ASSUMED,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 3U, "proviso", SOUND_SORT_NORMATIVE, SOUND_EVID_INFERENTIAL,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0x5), UINT64_C(0));
    sound_set_node(&arena, 4U, "effect", SOUND_SORT_NORMATIVE, SOUND_EVID_INFERENTIAL,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(2), UINT64_C(0x8), 1U,
                   UINT64_C(3), UINT64_C(0));
    sound_set_node(&arena, 5U, "pen_slot", SOUND_SORT_NORMATIVE, SOUND_EVID_INFERENTIAL,
                   SOUND_ASPECT_STATE, false, false, true, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(1), UINT64_C(0));
    if (sound_policy_seal(&arena, UINT64_C(0x39), UINT64_C(0x06), UINT32_C(0)) !=
            SOUND_FIBER_COMMITTED ||
        sound_lift_policy_eval_to_fir(&arena, "vat_policy_eval", 0U, fir_buf, sizeof(fir_buf),
                                      &fir_len) != SOUND_FIBER_COMMITTED ||
        !flow_ir_load_bytes(&prog, fir_buf, fir_len) || !flow_ir_validate(&prog) ||
        !flow_ir_admits_unitary(&prog) || prog.num_inputs != 1U || prog.num_outputs != 4U) {
        return UINT32_C(1);
    }

    for (uint32_t m = 0U; m < 4U; m++) {
        uint64_t facts_vec = UINT64_C(0);
        if ((m & 1U) != 0U) {
            facts_vec |= UINT64_C(1) << 1U;
        }
        if ((m & 2U) != 0U) {
            facts_vec |= UINT64_C(1) << 2U;
        }
        sim = arena;
        sim.map.node_meta[1].evid = (m & 1U) ? SOUND_EVID_SENSORY : SOUND_EVID_ASSUMED;
        sim.map.node_meta[2].evid = (m & 2U) ? SOUND_EVID_SENSORY : SOUND_EVID_ASSUMED;
        if (sound_eval(&sim) != SOUND_FIBER_COMMITTED) {
            fails++;
            continue;
        }
        uint64_t inv[4] = {facts_vec, UINT64_C(0), UINT64_C(0), UINT64_C(0)};
        uint64_t outv[4] = {0};
        if (!flow_ir_eval_u64(&prog, inv, outv) || outv[0] != sim.map.eval_out.established_mask ||
            outv[1] != sim.map.eval_out.defeated_mask ||
            outv[2] != sim.map.eval_out.conflict_mask ||
            outv[3] != sim.map.eval_out.penumbra_mask) {
            fails++;
        }
    }

    /* Also test cyclic conflict policy gated by 2 bind slots across all 4 states */
    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 5U;
    sound_set_node(&arena, 0U, "law", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                   UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 1U, "f_a", SOUND_SORT_FACTUAL, SOUND_EVID_ASSUMED, SOUND_ASPECT_STATE,
                   false, false, false, UINT64_C(0), UINT64_C(0), 1U, UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 2U, "f_b", SOUND_SORT_FACTUAL, SOUND_EVID_ASSUMED, SOUND_ASPECT_STATE,
                   false, false, false, UINT64_C(0), UINT64_C(0), 1U, UINT64_C(0), UINT64_C(0));
    sound_set_node(&arena, 3U, "claim_a", SOUND_SORT_NORMATIVE, SOUND_EVID_INFERENTIAL,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0x10), 1U,
                   UINT64_C(0x3), UINT64_C(0));
    sound_set_node(&arena, 4U, "claim_b", SOUND_SORT_NORMATIVE, SOUND_EVID_INFERENTIAL,
                   SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0x08), 1U,
                   UINT64_C(0x5), UINT64_C(0));
    if (sound_policy_seal(&arena, UINT64_C(0x19), UINT64_C(0x06), UINT32_C(0)) !=
            SOUND_FIBER_COMMITTED ||
        sound_lift_policy_eval_to_fir(&arena, "cyc_policy_eval", 2U, fir_buf, sizeof(fir_buf),
                                      &fir_len) != SOUND_FIBER_COMMITTED ||
        !flow_ir_load_bytes(&prog, fir_buf, fir_len) || !flow_ir_admits_unitary(&prog) ||
        prog.num_inputs != 1U || prog.num_outputs != 4U) {
        return fails + UINT32_C(1);
    }
    for (uint32_t m = 0U; m < 4U; m++) {
        uint64_t facts_vec = UINT64_C(0);
        if ((m & 1U) != 0U) {
            facts_vec |= UINT64_C(1) << 1U;
        }
        if ((m & 2U) != 0U) {
            facts_vec |= UINT64_C(1) << 2U;
        }
        sim = arena;
        sim.map.node_meta[1].evid = (m & 1U) ? SOUND_EVID_SENSORY : SOUND_EVID_ASSUMED;
        sim.map.node_meta[2].evid = (m & 2U) ? SOUND_EVID_SENSORY : SOUND_EVID_ASSUMED;
        if (sound_eval(&sim) != SOUND_FIBER_COMMITTED) {
            fails++;
            continue;
        }
        uint64_t inv[4] = {facts_vec, UINT64_C(0), UINT64_C(0), UINT64_C(0)};
        uint64_t outv[4] = {0};
        if (!flow_ir_eval_u64(&prog, inv, outv) || outv[0] != sim.map.eval_out.established_mask ||
            outv[1] != sim.map.eval_out.defeated_mask ||
            outv[2] != sim.map.eval_out.conflict_mask ||
            outv[3] != sim.map.eval_out.penumbra_mask) {
            fails++;
        }
    }
    return fails;
}

static bool sound_load_arena_file(const char *path, SoundArena *arena) {
    static uint8_t raw[SOUND_ARENA_BYTES + 1U];
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return false;
    }
    size_t total = 0U;
    while (total < sizeof(raw)) {
        size_t got = fread(raw + total, 1U, sizeof(raw) - total, fp);
        total += got;
        if (got == 0U || feof(fp) != 0 || ferror(fp) != 0) {
            break;
        }
    }
    int err = ferror(fp);
    fclose(fp);
    if (err != 0 || total != SOUND_ARENA_BYTES) {
        return false;
    }
    for (size_t i = 0U; i < SOUND_ARENA_CELLS; i++) {
        flow_cell_from_bytes(arena->cells[i].u64, raw + (i * 64U));
    }
    return true;
}

static bool sound_save_bin_file(const char *path, const uint8_t *buf, size_t len) {
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    size_t wr = fwrite(buf, 1U, len, fp);
    int err = ferror(fp);
    int cls = fclose(fp);
    return (err == 0) && (cls == 0) && (wr == len);
}

static void sound_derive_prog_name(char dst[24], const char *path) {
    const char *base = path;
    if (path != NULL) {
        for (size_t i = 0U; path[i] != '\0'; i++) {
            if (path[i] == '/' || path[i] == '\\') {
                base = &path[i + 1U];
            }
        }
    }
    size_t len = 0U;
    if (base != NULL) {
        while (base[len] != '\0') {
            len++;
        }
    }
    if (len > 4U && base[len - 4U] == '.' && base[len - 3U] == 's' && base[len - 2U] == 'n' &&
        base[len - 1U] == 'd') {
        len -= 4U;
    }
    size_t out_i = 0U;
    while (out_i < 23U && out_i < len) {
        dst[out_i] = base[out_i];
        out_i++;
    }
    dst[out_i] = '\0';
    if (out_i == 0U) {
        static const char k_fallback[] = "sound_auth_gate";
        for (size_t i = 0U; i < sizeof(k_fallback); i++) {
            dst[i] = k_fallback[i];
        }
    }
}

static SoundArena g_cli_arena;

static int sound_cmd_selftest(void) {
    uint32_t f1 = sound_test_t1_kernel();
    uint32_t f2 = sound_test_t2_hohfeld_and_rollback();
    uint32_t f3 = sound_test_t3_frontend_gate();
    uint32_t f4 = sound_test_t4_arena_merkle();
    uint32_t f5 = sound_test_t5_searu_repair();
    uint32_t f6 = sound_test_t6_fuzz_4096();
    uint32_t f7 = sound_test_t7_fir_lift();
    uint32_t f8 = sound_test_t8_siphash_and_atomic_batch();
    uint32_t f9 = sound_test_t9_lex_specialis_and_chart_glue();
    uint32_t f10 = sound_test_t10_policy_eval_fir_lift();
    uint32_t tot = f1 + f2 + f3 + f4 + f5 + f6 + f7 + f8 + f9 + f10;
    printf(
        "[selftest] T1(kernel)=%s T2(hohfeld)=%s T3(gate)=%s T4(arena)=%s T5(searu)=%s T6(fuzz4096)=%s T7(fir)=%s T8(siphash_batch)=%s T9(lex_glue)=%s T10(policy_fir)=%s | fails=%u\n",
        f1 == 0U ? "PASS" : "FAIL", f2 == 0U ? "PASS" : "FAIL", f3 == 0U ? "PASS" : "FAIL",
        f4 == 0U ? "PASS" : "FAIL", f5 == 0U ? "PASS" : "FAIL", f6 == 0U ? "PASS" : "FAIL",
        f7 == 0U ? "PASS" : "FAIL", f8 == 0U ? "PASS" : "FAIL", f9 == 0U ? "PASS" : "FAIL",
        f10 == 0U ? "PASS" : "FAIL", tot);
    return (tot == 0U) ? 0 : 1;
}

static const char *sound_fiber_name(sound_fiber_t f) {
    switch (f) {
    case SOUND_FIBER_COMMITTED:
        return "SOUND_FIBER_COMMITTED";
    case SOUND_FIBER_FALLBACK_NULL:
        return "SOUND_FIBER_FALLBACK_NULL";
    case SOUND_FIBER_FALLBACK_BOUNDS:
        return "SOUND_FIBER_FALLBACK_BOUNDS";
    case SOUND_FIBER_FALLBACK_FORMAT:
        return "SOUND_FIBER_FALLBACK_FORMAT";
    case SOUND_FIBER_FALLBACK_MAC:
        return "SOUND_FIBER_FALLBACK_MAC";
    case SOUND_FIBER_HOLD_IMMUNITY:
        return "SOUND_FIBER_HOLD_IMMUNITY";
    case SOUND_FIBER_HOLD_NO_POWER:
        return "SOUND_FIBER_HOLD_NO_POWER";
    case SOUND_FIBER_HOLD_MISSING_CONTEXT:
        return "SOUND_FIBER_HOLD_MISSING_CONTEXT";
    case SOUND_FIBER_HOLD_INCOHERENT:
        return "SOUND_FIBER_HOLD_INCOHERENT";
    case SOUND_FIBER_FALLBACK_POLICY_TAMPER:
        return "SOUND_FIBER_FALLBACK_POLICY_TAMPER";
    case SOUND_FIBER_FALLBACK_CLARIFY:
        return "SOUND_FIBER_FALLBACK_CLARIFY";
    case SOUND_FIBER_FALLBACK_IRREALIS:
        return "SOUND_FIBER_FALLBACK_IRREALIS";
    case SOUND_FIBER_FALLBACK_SPAN:
        return "SOUND_FIBER_FALLBACK_SPAN";
    case SOUND_FIBER_FALLBACK_POLICY_VIOLATION:
        return "SOUND_FIBER_FALLBACK_POLICY_VIOLATION";
    default:
        return "SOUND_FIBER_UNKNOWN";
    }
}

static int sound_cmd_verify(const char *path) {
    if (!sound_load_arena_file(path, &g_cli_arena)) {
        fprintf(stderr, "ERROR: unable to read 16,384B SoundArena '%s'\n", path);
        return 1;
    }
    sound_fiber_t rc = sound_arena_verify(&g_cli_arena);
    if (rc != SOUND_FIBER_COMMITTED) {
        fprintf(stderr, "ERROR: SoundArena verify failed on '%s' (fiber=%u %s)\n", path,
                (unsigned)rc, sound_fiber_name(rc));
        return 1;
    }
    rc = sound_eval(&g_cli_arena);
    if (rc != SOUND_FIBER_COMMITTED || !sound_eval_flags_ok(&g_cli_arena)) {
        fprintf(stderr, "ERROR: Kernel invariant violation on '%s' (fiber=%u %s)\n", path,
                (unsigned)rc, sound_fiber_name(rc));
        return 1;
    }
    printf(
        "[verify] %s (16384B, nodes=%u) | I*=0x%016llx P*=0x%016llx est=0x%016llx def=0x%016llx conf=0x%016llx pen=0x%016llx | invariants=OK\n",
        path, g_cli_arena.map.img_hdr.num_nodes,
        (unsigned long long)g_cli_arena.map.eval_out.i_star,
        (unsigned long long)g_cli_arena.map.eval_out.p_star,
        (unsigned long long)g_cli_arena.map.eval_out.established_mask,
        (unsigned long long)g_cli_arena.map.eval_out.defeated_mask,
        (unsigned long long)g_cli_arena.map.eval_out.conflict_mask,
        (unsigned long long)g_cli_arena.map.eval_out.penumbra_mask);
    for (uint32_t u = 0U; u < g_cli_arena.map.img_hdr.num_nodes; u++) {
        char lbl[SOUND_LABEL_BYTES];
        sound_label_read(lbl, g_cli_arena.map.node_meta[u].label_bytes);
        printf("  node[%02u] %-24s -> %-17s (reasons=0x%02x)\n", u, lbl,
               sound_status_name(g_cli_arena.map.eval_status.status[u]),
               g_cli_arena.map.eval_reasons[u >> 4U].reasons[u & 15U]);
    }
    return 0;
}

static int sound_cmd_repair(const char *path, const char *seed_str) {
    uint64_t seed = UINT64_C(0x53454152555F3031);
    if (seed_str != NULL && !sound_parse_u64(seed_str, &seed)) {
        fprintf(stderr, "ERROR: invalid seed '%s'\n", seed_str);
        return 1;
    }
    if (!sound_load_arena_file(path, &g_cli_arena)) {
        fprintf(stderr, "ERROR: unable to read 16,384B SoundArena '%s'\n", path);
        return 1;
    }
    sound_fiber_t rc = sound_arena_verify(&g_cli_arena);
    if (rc != SOUND_FIBER_COMMITTED) {
        fprintf(stderr, "ERROR: SoundArena verify failed on '%s' (fiber=%u %s)\n", path,
                (unsigned)rc, sound_fiber_name(rc));
        return 1;
    }
    rc = sound_searu_repair_conflict(&g_cli_arena, seed, 2048U);
    if (rc != SOUND_FIBER_COMMITTED) {
        fprintf(stderr, "ERROR: SEARU repair failed on '%s' (fiber=%u %s)\n", path, (unsigned)rc,
                sound_fiber_name(rc));
        return 1;
    }
    printf(
        "[repair] %s | conf_in=0x%016llx -> cand=0x%016llx retract=0x%016llx pinned=0x%016llx energy=%llu iters=%u conv=%u\n",
        path, (unsigned long long)g_cli_arena.map.eval_out.conflict_mask,
        (unsigned long long)g_cli_arena.map.searu_repair.candidate_mask,
        (unsigned long long)g_cli_arena.map.searu_repair.retracted_mask,
        (unsigned long long)g_cli_arena.map.searu_repair.pinned_mask,
        (unsigned long long)g_cli_arena.map.searu_repair.best_energy,
        g_cli_arena.map.searu_repair.iters_used, g_cli_arena.map.searu_repair.converged);
    return (g_cli_arena.map.searu_repair.converged == UINT32_C(1)) ? 0 : 1;
}

static int sound_cmd_lift_fir(int argc, char **argv) {
    const char *in_path = argv[2];
    const char *out_path = argv[3];
    bool use_repaired = false;
    uint64_t seed = UINT64_C(0x53454152555F3031);
    if (argc >= 5) {
        if (!sound_cstr_eq(argv[4], "--repaired")) {
            fprintf(stderr, "ERROR: unknown option '%s'\n", argv[4]);
            return 1;
        }
        use_repaired = true;
        if (argc >= 6 && !sound_parse_u64(argv[5], &seed)) {
            fprintf(stderr, "ERROR: invalid seed '%s'\n", argv[5]);
            return 1;
        }
    }
    if (!sound_load_arena_file(in_path, &g_cli_arena)) {
        fprintf(stderr, "ERROR: unable to read 16,384B SoundArena '%s'\n", in_path);
        return 1;
    }
    sound_fiber_t rc = sound_arena_verify(&g_cli_arena);
    if (rc != SOUND_FIBER_COMMITTED) {
        fprintf(stderr, "ERROR: SoundArena verify failed on '%s' (fiber=%u %s)\n", in_path,
                (unsigned)rc, sound_fiber_name(rc));
        return 1;
    }
    rc = use_repaired ? sound_searu_repair_conflict(&g_cli_arena, seed, 2048U)
                      : sound_eval(&g_cli_arena);
    if (rc != SOUND_FIBER_COMMITTED || !sound_eval_flags_ok(&g_cli_arena)) {
        fprintf(stderr, "ERROR: Kernel invariant violation on '%s' (fiber=%u %s)\n", in_path,
                (unsigned)rc, sound_fiber_name(rc));
        return 1;
    }

    char prog_name[24];
    sound_derive_prog_name(prog_name, in_path);
    uint8_t fir_buf[SOUND_FIR_BYTES];
    size_t fir_len = 0U;
    uint32_t fir_flags = use_repaired ? SOUND_FIR_FLAG_USE_REPAIRED : UINT32_C(0);
    rc = sound_lift_to_fir(&g_cli_arena, prog_name, fir_flags, fir_buf, sizeof(fir_buf), &fir_len);
    if (rc != SOUND_FIBER_COMMITTED) {
        fprintf(stderr, "ERROR: lift-fir failed on '%s' (fiber=%u %s)\n", in_path, (unsigned)rc,
                sound_fiber_name(rc));
        return 1;
    }

    flow_ir_prog_t prog;
    flow_ir_dag_stats_t dag;
    if (!flow_ir_load_bytes(&prog, fir_buf, fir_len) || !flow_ir_validate(&prog) ||
        !flow_ir_admits_unitary(&prog) || !flow_ir_analyze_dag(&prog, &dag)) {
        fprintf(stderr, "ERROR: lifted FIR1 failed FLOW_IR unitary/DAG audit\n");
        return 1;
    }
    if (!sound_save_bin_file(out_path, fir_buf, fir_len)) {
        fprintf(stderr, "ERROR: unable to write '%s'\n", out_path);
        return 1;
    }

    uint64_t active_mask = use_repaired ? g_cli_arena.map.searu_repair.candidate_mask
                                        : g_cli_arena.map.eval_out.established_mask;
    printf(
        "[lift-fir] %s -> %s (%zuB, name=%s, active=0x%016llx) | mult_bits=%u mult_depth=%u unitary=OK\n",
        in_path, out_path, fir_len, prog.name, (unsigned long long)active_mask, dag.mult_bits,
        dag.mult_depth);
    return 0;
}

static int sound_cmd_lift_policy_fir(const char *in_path, const char *out_path) {
    if (!sound_load_arena_file(in_path, &g_cli_arena)) {
        fprintf(stderr, "ERROR: unable to read 16,384B SoundArena '%s'\n", in_path);
        return 1;
    }
    sound_fiber_t rc = sound_arena_verify(&g_cli_arena);
    if (rc != SOUND_FIBER_COMMITTED) {
        fprintf(stderr, "ERROR: SoundArena verify failed on '%s' (fiber=%u %s)\n", in_path,
                (unsigned)rc, sound_fiber_name(rc));
        return 1;
    }
    char prog_name[24];
    sound_derive_prog_name(prog_name, in_path);
    static uint8_t fir_buf[65536];
    size_t fir_len = 0U;
    rc = sound_lift_policy_eval_to_fir(&g_cli_arena, prog_name, 0U, fir_buf, sizeof(fir_buf),
                                       &fir_len);
    if (rc != SOUND_FIBER_COMMITTED) {
        fprintf(stderr, "ERROR: lift-policy-fir failed on '%s' (fiber=%u %s)\n", in_path,
                (unsigned)rc, sound_fiber_name(rc));
        return 1;
    }
    static flow_ir_prog_t prog;
    flow_ir_dag_stats_t dag;
    if (!flow_ir_load_bytes(&prog, fir_buf, fir_len) || !flow_ir_validate(&prog) ||
        !flow_ir_admits_unitary(&prog) || !flow_ir_analyze_dag(&prog, &dag)) {
        fprintf(stderr, "ERROR: lifted policy FIR1 failed FLOW_IR unitary/DAG audit\n");
        return 1;
    }
    if (!sound_save_bin_file(out_path, fir_buf, fir_len)) {
        fprintf(stderr, "ERROR: unable to write '%s'\n", out_path);
        return 1;
    }
    printf(
        "[lift-policy-fir] %s -> %s (%zuB, name=%s, ops=%u) | mult_bits=%u mult_depth=%u unitary=OK\n",
        in_path, out_path, fir_len, prog.name, prog.num_ops, dag.mult_bits, dag.mult_depth);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage:\n  sound selftest\n  sound verify <arena.snd>\n"
                        "  sound repair <arena.snd> [seed]\n"
                        "  sound lift-fir <arena.snd> <out.fir> [--repaired [seed]]\n"
                        "  sound lift-policy-fir <arena.snd> <out.fir>\n");
        return 1;
    }
    if (sound_cstr_eq(argv[1], "selftest")) {
        return sound_cmd_selftest();
    }
    if (sound_cstr_eq(argv[1], "verify") && argc >= 3) {
        return sound_cmd_verify(argv[2]);
    }
    if (sound_cstr_eq(argv[1], "repair") && argc >= 3) {
        return sound_cmd_repair(argv[2], (argc >= 4) ? argv[3] : NULL);
    }
    if (sound_cstr_eq(argv[1], "lift-fir") && argc >= 4) {
        return sound_cmd_lift_fir(argc, argv);
    }
    if (sound_cstr_eq(argv[1], "lift-policy-fir") && argc >= 4) {
        return sound_cmd_lift_policy_fir(argv[2], argv[3]);
    }
    fprintf(stderr, "ERROR: invalid arguments\n");
    return 1;
}
