#include "flow_ir.h"
#include "flow_wire_pack.h"
#include "sound.h"
#include "sound_cert.h"

#include <stdio.h>

typedef struct {
    uint64_t s;
} sound_rng_t;

static uint64_t sound_splitmix64(sound_rng_t *rng) {
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
    uint32_t req = (uint32_t)(SOUND_EVAL_FLAG_PARTITION_OK | SOUND_EVAL_FLAG_CONFLICT_FREE_OK |
                              SOUND_EVAL_FLAG_HOHFELD_OK | SOUND_EVAL_FLAG_FIXPOINT_OK);
    return (arena->map.eval_out.eval_flags & req) == req;
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

/* ============================================================================
 * T1..T10 Legacy Kernel & Security Gate Suites (Preserved 100%)
 * ============================================================================ */

static uint32_t sound_test_t1_kernel(void) {
    uint32_t fails = UINT32_C(0);
    SoundArena arena;

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

    arena.map.candidate[0].target_node = 0U;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FIBER_FALLBACK_POLICY_VIOLATION ||
        admitted != UINT32_MAX ||
        (arena.map.forest_ctrl.last_reject_reason & (uint32_t)SOUND_INJECT_POLICY_SLOT) ==
            UINT32_C(0) ||
        arena.map.node_meta[0].sort != (uint8_t)SOUND_SORT_INSTITUTIONAL) {
        fails++;
    }

    arena.map.candidate[0].target_node = 3U;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FIBER_FALLBACK_POLICY_VIOLATION ||
        admitted != UINT32_MAX ||
        arena.map.forest_ctrl.last_reject_reason != (uint32_t)SOUND_INJECT_UNBOUND_SLOT ||
        arena.map.node_meta[3].evid != (uint8_t)SOUND_EVID_ASSUMED) {
        fails++;
    }

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

    arena.map.candidate[0].attack_mask = UINT64_C(0);
    arena.map.candidate[0].evid = (uint8_t)SOUND_EVID_DIRECT;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FIBER_FALLBACK_POLICY_VIOLATION ||
        admitted != UINT32_MAX ||
        arena.map.forest_ctrl.last_reject_reason != (uint32_t)SOUND_INJECT_DIRECT_CLAIM_FORBID ||
        arena.map.node_meta[1].evid != (uint8_t)SOUND_EVID_ASSUMED) {
        fails++;
    }

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

    dst.cells[1].bytes[0] ^= UINT8_C(0x01);
    if (sound_arena_verify(&dst) != SOUND_FIBER_FALLBACK_MAC) {
        fails++;
    }
    dst.cells[1].bytes[0] ^= UINT8_C(0x01);

    dst.map.img_hdr.magic ^= UINT32_C(1);
    if (sound_arena_verify(&dst) != SOUND_FIBER_FALLBACK_FORMAT) {
        fails++;
    }
    dst.map.img_hdr.magic = SOUND_MAGIC;
    dst.map.img_hdr.schema_hash ^= UINT64_C(1);
    if (sound_arena_verify(&dst) != SOUND_FIBER_FALLBACK_FORMAT) {
        fails++;
    }

    /* "FLWA" sparse 64B arena pack <-> unpack bit-exact roundtrip + tamper rejection */
    static uint8_t flwa_buf[18432];
    static uint8_t raw_rt[SOUND_ARENA_BYTES];
    size_t flwa_len = 0U;
    size_t rt_len = 0U;
    if (!flow_arena_pack_bytes(wire, SOUND_ARENA_BYTES, flwa_buf, sizeof(flwa_buf), &flwa_len) ||
        flwa_len >= SOUND_ARENA_BYTES ||
        !flow_arena_unpack_bytes(flwa_buf, flwa_len, raw_rt, sizeof(raw_rt), &rt_len) ||
        rt_len != SOUND_ARENA_BYTES) {
        fails++;
    } else {
        for (size_t i = 0U; i < SOUND_ARENA_BYTES; i++) {
            if (raw_rt[i] != wire[i]) {
                fails++;
                break;
            }
        }
        flwa_buf[flwa_len - 1U] ^= UINT8_C(1);
        if (flow_arena_unpack_bytes(flwa_buf, flwa_len, raw_rt, sizeof(raw_rt), &rt_len)) {
            fails++;
        }
    }

    return fails;
}

static uint32_t sound_test_t5_searu_repair(void) {
    uint32_t fails = UINT32_C(0);
    SoundArena arena;

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

    arena.map.norm.immunity_mask = UINT64_C(0x2);
    if (sound_searu_repair_conflict(&arena, UINT64_C(0x53454152555F3032), 1024U) !=
            SOUND_FIBER_COMMITTED ||
        arena.map.searu_repair.best_energy != UINT64_C(0) ||
        arena.map.searu_repair.candidate_mask != UINT64_C(0x2) ||
        arena.map.searu_repair.retracted_mask != UINT64_C(0x1)) {
        fails++;
    }

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

    {
        uint64_t inv[5] = {UINT64_C(0x2), UINT64_C(0), UINT64_C(0x1), UINT64_C(0), UINT64_C(0x2)};
        uint64_t outv[4] = {0};
        if (!flow_ir_eval_u64(&prog, inv, outv) || outv[0] != UINT64_C(0x2) ||
            outv[1] != UINT64_C(0) || outv[2] != UINT64_C(0x4) || outv[3] != UINT64_C(0x7)) {
            fails++;
        }
    }
    {
        uint64_t inv[5] = {UINT64_C(0x2), UINT64_C(0), UINT64_C(0x6), UINT64_C(0x4), UINT64_C(0)};
        uint64_t outv[4] = {0};
        if (!flow_ir_eval_u64(&prog, inv, outv) || outv[0] != UINT64_C(0x2) ||
            outv[1] != UINT64_C(0)) {
            fails++;
        }
    }
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
        static uint8_t fir2_buf[SOUND_FIR_BYTES];
        size_t fir2_len = 0U;
        flow_ir_prog_t prog2;
        uint64_t outv2[4] = {0};
        if (!flow_ir_encode_packed_bytes(&prog, fir2_buf, sizeof(fir2_buf), &fir2_len) ||
            fir2_len >= fir_len || !flow_ir_load_bytes(&prog2, fir2_buf, fir2_len) ||
            !flow_ir_validate(&prog2) || !flow_ir_admits_unitary(&prog2) ||
            !flow_ir_eval_u64(&prog2, inv, outv2) || outv2[3] != outv[3]) {
            fails++;
        }
    }

    return fails;
}

static uint32_t sound_test_t8_siphash_and_atomic_batch(void) {
    uint32_t fails = UINT32_C(0);
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

    sound_zero_bytes(arena.map.candidate, sizeof(arena.map.candidate));
    arena.map.forest_ctrl.utterance_bytes = (uint32_t)(sizeof(utt) - 1U);
    arena.map.forest_ctrl.num_candidates = 1U;
    arena.map.candidate[0].target_node = 1U;
    arena.map.candidate[0].force = SOUND_FORCE_ASSERTION;
    arena.map.candidate[0].sort = SOUND_SORT_FACTUAL;
    arena.map.candidate[0].evid = SOUND_EVID_SENSORY;
    arena.map.candidate[0].span_start = 0U;
    arena.map.candidate[0].span_end = 5U;
    arena.map.candidate[0].payload_hash = UINT64_C(0x123456789ABCDEF0);
    uint32_t n_adm = 0U;
    if (sound_admit_forest_batch_keyed(&arena, utt, sizeof(utt) - 1U, k0, k1, &n_adm) !=
            SOUND_FIBER_FALLBACK_SPAN ||
        arena.map.forest_ctrl.last_reject_reason != (uint32_t)SOUND_INJECT_SPAN_HASH_MISMATCH ||
        arena.map.node_meta[1].evid != (uint8_t)SOUND_EVID_ASSUMED) {
        fails++;
    }

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
    arena.map.candidate[2].evid = SOUND_EVID_SENSORY;
    if (sound_admit_forest_batch_keyed(&arena, utt, sizeof(utt) - 1U, k0, k1, &n_adm) !=
            SOUND_FIBER_FALLBACK_POLICY_VIOLATION ||
        (arena.map.forest_ctrl.last_reject_reason & (uint32_t)SOUND_INJECT_EVID_CEILING) == 0U ||
        arena.map.node_meta[1].evid != (uint8_t)SOUND_EVID_ASSUMED ||
        arena.map.node_meta[2].evid != (uint8_t)SOUND_EVID_ASSUMED) {
        fails++;
    }
    arena.map.candidate[2].evid = SOUND_EVID_REPORTATIVE;
    arena.map.candidate[2].target_node = 1U;
    if (sound_admit_forest_batch_keyed(&arena, utt, sizeof(utt) - 1U, k0, k1, &n_adm) !=
            SOUND_FIBER_FALLBACK_POLICY_VIOLATION ||
        arena.map.forest_ctrl.last_reject_reason != (uint32_t)SOUND_INJECT_DUPLICATE_TARGET ||
        arena.map.node_meta[1].evid != (uint8_t)SOUND_EVID_ASSUMED) {
        fails++;
    }
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
    w1[0] = UINT8_C(120);
    w1[1] = UINT8_C(15);
    if (sound_searu_repair_conflict_weighted(&a, UINT64_C(0xCAFEBABE11223344), 1024U, w1) !=
            SOUND_FIBER_COMMITTED ||
        a.map.searu_repair.converged != UINT32_C(1) ||
        a.map.searu_repair.candidate_mask != UINT64_C(1) ||
        a.map.searu_repair.best_energy != UINT64_C(15)) {
        fails++;
    }
    a.map.norm.immunity_mask = UINT64_C(2);
    w1[0] = UINT8_C(255);
    w1[1] = UINT8_C(1);
    if (sound_searu_repair_conflict_weighted(&a, UINT64_C(0x778899AABBCCDDEE), 1024U, w1) !=
            SOUND_FIBER_COMMITTED ||
        a.map.searu_repair.converged != UINT32_C(1) ||
        a.map.searu_repair.candidate_mask != UINT64_C(2)) {
        fails++;
    }

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
    if (sound_chart_glue_import(&b, &a, UINT64_C(2), slot_map) != SOUND_FIBER_COMMITTED ||
        sound_chart_glue_verify(&b, &a) != SOUND_FIBER_COMMITTED ||
        sound_eval(&b) != SOUND_FIBER_COMMITTED ||
        (b.map.eval_out.established_mask & UINT64_C(0x7)) != UINT64_C(0x7)) {
        fails++;
    }
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
    {
        static uint8_t fir2_buf[65536];
        size_t fir2_len = 0U;
        static flow_ir_prog_t prog2;
        if (!flow_ir_encode_packed_bytes(&prog, fir2_buf, sizeof(fir2_buf), &fir2_len) ||
            fir2_len >= fir_len || !flow_ir_load_bytes(&prog2, fir2_buf, fir2_len) ||
            !flow_ir_admits_unitary(&prog2)) {
            fails++;
        } else {
            uint64_t inv[4] = {UINT64_C(0x6), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
            uint64_t out1[4] = {0};
            uint64_t out2[4] = {0};
            if (!flow_ir_eval_u64(&prog, inv, out1) || !flow_ir_eval_u64(&prog2, inv, out2) ||
                out1[0] != out2[0] || out1[1] != out2[1] || out1[2] != out2[2] ||
                out1[3] != out2[3]) {
                fails++;
            }
            fir2_buf[fir2_len - 1U] ^= UINT8_C(1);
            if (flow_ir_load_bytes(&prog2, fir2_buf, fir2_len)) {
                fails++;
            }
        }
    }
    return fails;
}

/* ============================================================================
 * T11..T18 Pure-Theory Pillars 1-4 Self-Tests
 * ============================================================================ */

static sound_cert_ws_t g_cert_ws;

static uint32_t sound_test_t11_bilattice_classify(void) {
    uint32_t fails = UINT32_C(0);
    sound_bilattice_atlas_t atlas;
    sound_classify_bilattice(&atlas);
    if (atlas.b4_v4_ok != 1U || atlas.b4_demorgan_ok != 1U || atlas.b4_interlaced_ok != 1U ||
        atlas.b4_distrib_ok != 1U || atlas.n3_total != 64U || atlas.n4_total != 4096U ||
        atlas.n3_grounded_unique != 34U || atlas.n3_unique_nongrounded != 6U ||
        atlas.n3_multi_stable != 22U || atlas.n3_classical_unsolvable != 2U ||
        atlas.n3_conflict_nonempty != 30U || atlas.n4_classical_unsolvable != 214U ||
        (atlas.n3_grounded_unique + atlas.n3_unique_nongrounded + atlas.n3_multi_stable +
         atlas.n3_classical_unsolvable) != 64U ||
        (atlas.n4_grounded_unique + atlas.n4_unique_nongrounded + atlas.n4_multi_stable +
         atlas.n4_classical_unsolvable) != 4096U ||
        (atlas.n4_grounded_unique + atlas.n4_conflict_nonempty) != 4096U) {
        fails++;
    }
    return fails;
}

static uint32_t sound_test_t12_paradox_min(void) {
    uint32_t fails = UINT32_C(0);
    for (uint64_t sd = 0U; sd < 2U; sd++) {
        sound_paradox_witness_t w;
        sound_hunt_paradox_min(sd, &w);
        if (w.num_nodes != 3U || w.classical_models != 0U || w.i_star != 0U || w.p_star != 7U ||
            w.conflict_mask != 7U || w.is_odd_cycle != 1U || w.merkle == UINT64_C(0)) {
            fails++;
        }
        if (!sound_cert_emit_paradox_fir(&w, NULL, &g_cert_ws) || !g_cert_ws.ir_eval_ok) {
            fails++;
        }
    }
    return fails;
}

static uint32_t sound_test_t13_sheaf_classify(void) {
    uint32_t fails = UINT32_C(0);
    sound_sheaf_atlas_t atlas;
    sound_classify_sheaf(&atlas);
    if (atlas.c3_z4_total != 64U || atlas.c3_z4_coboundary != 16U ||
        atlas.c3_z4_obstructed != 48U || atlas.c4_z4_total != 256U ||
        atlas.c4_z4_coboundary != 64U || atlas.c4_z4_obstructed != 192U ||
        atlas.p3_z4_total != 16U || atlas.p3_z4_coboundary != 16U || atlas.c3_v4_total != 64U ||
        atlas.c3_v4_coboundary != 16U || atlas.gauge_invariance_ok != 1U) {
        fails++;
    }
    return fails;
}

static uint32_t sound_test_t14_sheaf_h1_hunt(void) {
    uint32_t fails = UINT32_C(0);
    for (uint64_t sd = 1U; sd <= 4U; sd++) {
        sound_sheaf_witness_t w;
        sound_hunt_sheaf_h1(sd, &w);
        if (w.holonomy_z4 == 0U || w.gauge_holonomy_z4 != w.holonomy_z4 ||
            w.is_coboundary_z4 != 0U || w.skew_symmetric_ok != 1U ||
            w.xor_holonomy == UINT64_C(0) || w.merkle == UINT64_C(0)) {
            fails++;
        }
        if (!sound_cert_emit_sheaf_fir(&w, NULL, &g_cert_ws) || !g_cert_ws.ir_eval_ok) {
            fails++;
        }
    }
    return fails;
}

static uint32_t sound_test_t15_hohfeld_v4_classify(void) {
    uint32_t fails = UINT32_C(0);
    sound_hohfeld_atlas_t atlas;
    sound_classify_hohfeld(&atlas);
    if (atlas.v4_group_ok != 1U || atlas.v4_orbits != 2U || atlas.transitions_checked == 0U ||
        atlas.coherence_violations != 0U || atlas.immunity_leaks != 0U ||
        atlas.disability_leaks != 0U || atlas.reachable_imm0 != 27U || atlas.reachable_imm1 != 9U ||
        atlas.reachable_imm2 != 3U || atlas.reachable_imm3 != 1U) {
        fails++;
    }
    return fails;
}

static uint32_t sound_test_t16_chisholm_norm_trap(void) {
    uint32_t fails = UINT32_C(0);
    sound_chisholm_witness_t cw;
    sound_hunt_chisholm(&cw);
    if (cw.num_nodes != 4U || cw.established_mask != UINT64_C(0xC) ||
        cw.defeated_mask != UINT64_C(0x3) || cw.conflict_mask != UINT64_C(0) ||
        cw.penumbra_mask != UINT64_C(0) || cw.hohfeld_ok != 1U ||
        cw.statuses[0] != (uint8_t)SOUND_STATUS_DEFEATED ||
        cw.statuses[1] != (uint8_t)SOUND_STATUS_DEFEATED ||
        cw.statuses[2] != (uint8_t)SOUND_STATUS_ESTABLISHED ||
        cw.statuses[3] != (uint8_t)SOUND_STATUS_ESTABLISHED) {
        fails++;
    }
    if (!sound_cert_emit_chisholm_fir(NULL, &g_cert_ws) || !g_cert_ws.ir_eval_ok) {
        fails++;
    }

    sound_norm_trap_witness_t nw;
    sound_hunt_norm_trap(UINT64_C(0x77), &nw);
    if (nw.freeze_ok != 1U || nw.final_pos != UINT64_C(0x4) || nw.final_neg != UINT64_C(0x2) ||
        nw.step_fiber[0] != (uint8_t)SOUND_FIBER_COMMITTED ||
        nw.step_fiber[1] != (uint8_t)SOUND_FIBER_HOLD_IMMUNITY ||
        nw.step_fiber[2] != (uint8_t)SOUND_FIBER_HOLD_NO_POWER ||
        nw.step_fiber[3] != (uint8_t)SOUND_FIBER_HOLD_INCOHERENT ||
        nw.energy_keep_a != nw.energy_keep_b || nw.energy_keep_a >= nw.energy_both ||
        nw.energy_keep_a >= nw.energy_neither || nw.kanlock_win_energy >= nw.kanlock_both_energy) {
        fails++;
    }
    if (!sound_cert_emit_norm_trap_fir(&nw, NULL, &g_cert_ws) || !g_cert_ws.ir_eval_ok) {
        fails++;
    }
    return fails;
}

static uint32_t sound_test_t17_vendler_aspect(void) {
    uint32_t fails = UINT32_C(0);
    sound_aspect_witness_t aw;
    sound_hunt_aspect_firewall(UINT64_C(0x42), &aw);
    if (aw.aspect_bijection_ok != 1U || aw.telic_blocked_mask != UINT32_C(3) ||
        aw.state_gate_telic != 0U || aw.activity_gate_telic != 0U ||
        aw.accomplishment_gate_telic != 1U || aw.achievement_gate_telic != 1U ||
        aw.declarative_atelic_fiber != (uint32_t)SOUND_FIBER_FALLBACK_POLICY_VIOLATION ||
        !(aw.j_start + UINT32_C(2) <= aw.j_end) || !(aw.i_end < aw.j_end)) {
        fails++;
    }
    if (!sound_cert_emit_aspect_fir(NULL, &g_cert_ws) || !g_cert_ws.ir_eval_ok) {
        fails++;
    }
    return fails;
}

static uint32_t sound_test_t18_projection_and_forces(void) {
    uint32_t fails = UINT32_C(0);
    sound_projection_atlas_t pa;
    sound_classify_projection(&pa);
    if (pa.total_unary_maps != 256U || pa.involutive_maps != 10U || pa.classical_neg_maps != 1U ||
        pa.hole_presup_pairs != 16U || pa.strawson_gap_pairs != 12U || pa.cond_filter_ok != 1U ||
        pa.plug_irrealis_ok != 1U || pa.force_invariants_ok != 1U) {
        fails++;
    }

    sound_presup_witness_t pw;
    sound_hunt_presup_filter(UINT64_C(0x55), &pw);
    if (pw.eval_fail_b != (uint8_t)SOUND_B4_BOT || pw.eval_neg_fail_b != (uint8_t)SOUND_B4_BOT ||
        pw.plug_presup != (uint8_t)SOUND_B4_TRUE || pw.plug_eval != (uint8_t)SOUND_B4_BOT ||
        pw.cond_false_presup != (uint8_t)SOUND_B4_TRUE || pw.cond_true_presup != pw.b_presup ||
        pw.force_hume_ok != 1U || pw.force_query_ok != 1U || pw.force_irrealis_ok != 1U ||
        pw.force_directive_ok != 1U) {
        fails++;
    }
    if (!sound_cert_emit_presup_fir(NULL, &g_cert_ws) || !g_cert_ws.ir_eval_ok) {
        fails++;
    }
    return fails;
}

static uint32_t sound_test_t19_contract_drift(void) {
    uint32_t fails = UINT32_C(0);
    sound_contract_spec_t spec;
    sound_contract_audit_t audit;

    /* 1. Clean fair contract certifies */
    sound_zero_bytes(&spec, sizeof(spec));
    spec.max_loss_cap = UINT64_C(10000);
    spec.max_exit_fee = UINT64_C(1500);
    spec.protected_mask = UINT64_C(0xF);
    spec.max_asym_num = UINT64_C(2);
    spec.max_asym_den = UINT64_C(1);
    spec.num_clauses = 2U;
    spec.clauses[0].clause_id = 1U;
    spec.clauses[0].kind = (uint8_t)SOUND_CLAUSE_BASE_FEE;
    spec.clauses[0].disclosed = UINT8_C(1);
    spec.clauses[0].b4_truth = (uint8_t)SOUND_B4_TRUE;
    spec.clauses[0].base_loss = UINT64_C(6000);
    spec.clauses[0].exit_fee = UINT64_C(500);
    spec.clauses[0].cons_penalty = UINT64_C(200);
    spec.clauses[0].inst_penalty = UINT64_C(100);
    spec.clauses[1].clause_id = 2U;
    spec.clauses[1].kind = (uint8_t)SOUND_CLAUSE_RECIPROCITY;
    spec.clauses[1].disclosed = UINT8_C(1);
    spec.clauses[1].b4_truth = (uint8_t)SOUND_B4_TRUE;
    spec.clauses[1].base_loss = UINT64_C(3000);
    spec.clauses[1].exit_fee = UINT64_C(1000);
    spec.clauses[1].cons_penalty = UINT64_C(400);
    spec.clauses[1].inst_penalty = UINT64_C(200);
    sound_audit_contract(&spec, &audit);
    if (audit.certified != UINT32_C(1) || audit.viol_flags != UINT32_C(0) ||
        audit.num_predatory_clauses != UINT32_C(0) || audit.benefit_drift != (int64_t)(-1000) ||
        !sound_cert_emit_contract_fir(&audit, NULL, &g_cert_ws) || !g_cert_ws.ir_eval_ok) {
        fails++;
    }

    /* 2. Single-fault isolation across every violation bit */
    sound_contract_spec_t s;
    sound_zero_bytes(&s, sizeof(s));
    s = spec;
    s.num_clauses = 3U;

    /* IMMUNITY_BREACH */
    sound_zero_bytes(&s.clauses[2], sizeof(s.clauses[2]));
    s.clauses[2].clause_id = 3U;
    s.clauses[2].kind = (uint8_t)SOUND_CLAUSE_UNILATERAL_POWER;
    s.clauses[2].disclosed = UINT8_C(1);
    s.clauses[2].b4_truth = (uint8_t)SOUND_B4_TRUE;
    s.clauses[2].target_mask = UINT64_C(0x2);
    sound_audit_contract(&s, &audit);
    if (audit.certified != UINT32_C(0) ||
        (audit.viol_flags & (uint32_t)SOUND_CVIOL_IMMUNITY_BREACH) == UINT32_C(0) ||
        audit.repaired_certified != UINT32_C(1)) {
        fails++;
    }

    /* UNINFORMED_AMBIG via B4_BOT and B4_TOP */
    sound_zero_bytes(&s.clauses[2], sizeof(s.clauses[2]));
    s.clauses[2].clause_id = 3U;
    s.clauses[2].kind = (uint8_t)SOUND_CLAUSE_BASE_FEE;
    s.clauses[2].disclosed = UINT8_C(1);
    s.clauses[2].b4_truth = (uint8_t)SOUND_B4_BOT;
    sound_audit_contract(&s, &audit);
    if (audit.certified != UINT32_C(0) ||
        (audit.viol_flags & (uint32_t)SOUND_CVIOL_UNINFORMED_AMBIG) == UINT32_C(0)) {
        fails++;
    }
    s.clauses[2].b4_truth = (uint8_t)SOUND_B4_TOP;
    sound_audit_contract(&s, &audit);
    if (audit.certified != UINT32_C(0) ||
        (audit.viol_flags & (uint32_t)SOUND_CVIOL_UNINFORMED_AMBIG) == UINT32_C(0)) {
        fails++;
    }

    /* PRESUP_SMUGGLE */
    sound_zero_bytes(&s.clauses[2], sizeof(s.clauses[2]));
    s.clauses[2].clause_id = 3U;
    s.clauses[2].kind = (uint8_t)SOUND_CLAUSE_BASE_FEE;
    s.clauses[2].disclosed = UINT8_C(0);
    s.clauses[2].b4_truth = (uint8_t)SOUND_B4_TRUE;
    s.clauses[2].base_loss = UINT64_C(500);
    sound_audit_contract(&s, &audit);
    if (audit.certified != UINT32_C(0) ||
        (audit.viol_flags & (uint32_t)SOUND_CVIOL_PRESUP_SMUGGLE) == UINT32_C(0)) {
        fails++;
    }

    /* CHISHOLM_CASCADE */
    sound_zero_bytes(&s.clauses[2], sizeof(s.clauses[2]));
    s.clauses[2].clause_id = 3U;
    s.clauses[2].kind = (uint8_t)SOUND_CLAUSE_CHISHOLM_DEFAULT;
    s.clauses[2].disclosed = UINT8_C(1);
    s.clauses[2].b4_truth = (uint8_t)SOUND_B4_TRUE;
    s.clauses[2].extra_drift = UINT64_C(250);
    sound_audit_contract(&s, &audit);
    if (audit.certified != UINT32_C(0) ||
        (audit.viol_flags & (uint32_t)SOUND_CVIOL_CHISHOLM_CASCADE) == UINT32_C(0)) {
        fails++;
    }

    /* UNCONSCIONABLE */
    sound_zero_bytes(&s.clauses[2], sizeof(s.clauses[2]));
    s.clauses[2].clause_id = 3U;
    s.clauses[2].kind = (uint8_t)SOUND_CLAUSE_RECIPROCITY;
    s.clauses[2].disclosed = UINT8_C(1);
    s.clauses[2].b4_truth = (uint8_t)SOUND_B4_TRUE;
    s.clauses[2].cons_penalty = UINT64_C(201);
    s.clauses[2].inst_penalty = UINT64_C(100);
    sound_audit_contract(&s, &audit);
    if (audit.certified != UINT32_C(0) ||
        (audit.viol_flags & (uint32_t)SOUND_CVIOL_UNCONSCIONABLE) == UINT32_C(0)) {
        fails++;
    }

    /* SHEAF_CONFLICT */
    sound_zero_bytes(&s.clauses[2], sizeof(s.clauses[2]));
    s.clauses[2].clause_id = 3U;
    s.clauses[2].kind = (uint8_t)SOUND_CLAUSE_BASE_FEE;
    s.clauses[2].disclosed = UINT8_C(1);
    s.clauses[2].b4_truth = (uint8_t)SOUND_B4_TRUE;
    s.clauses[2].sheaf_cycle = UINT8_C(1);
    sound_audit_contract(&s, &audit);
    if (audit.certified != UINT32_C(0) ||
        (audit.viol_flags & (uint32_t)SOUND_CVIOL_SHEAF_CONFLICT) == UINT32_C(0)) {
        fails++;
    }

    /* EXIT_TRAP */
    sound_zero_bytes(&s.clauses[2], sizeof(s.clauses[2]));
    s.clauses[2].clause_id = 3U;
    s.clauses[2].kind = (uint8_t)SOUND_CLAUSE_BASE_FEE;
    s.clauses[2].disclosed = UINT8_C(1);
    s.clauses[2].b4_truth = (uint8_t)SOUND_B4_TRUE;
    s.clauses[2].exit_fee = UINT64_C(1501);
    sound_audit_contract(&s, &audit);
    if (audit.certified != UINT32_C(0) ||
        (audit.viol_flags & (uint32_t)SOUND_CVIOL_EXIT_TRAP) == UINT32_C(0)) {
        fails++;
    }

    /* BENEFIT_DRIFT via cap overflow + max_asym_den==0 fail-closed */
    sound_zero_bytes(&s.clauses[2], sizeof(s.clauses[2]));
    s.clauses[2].clause_id = 3U;
    s.clauses[2].kind = (uint8_t)SOUND_CLAUSE_BASE_FEE;
    s.clauses[2].disclosed = UINT8_C(1);
    s.clauses[2].b4_truth = (uint8_t)SOUND_B4_TRUE;
    s.clauses[2].base_loss = UINT64_C(2000);
    sound_audit_contract(&s, &audit);
    if (audit.certified != UINT32_C(0) ||
        (audit.viol_flags & (uint32_t)SOUND_CVIOL_BENEFIT_DRIFT) == UINT32_C(0) ||
        audit.benefit_drift != (int64_t)1000) {
        fails++;
    }
    s = spec;
    s.max_asym_den = UINT64_C(0);
    sound_audit_contract(&s, &audit);
    if (audit.certified != UINT32_C(0) || audit.repaired_certified != UINT32_C(0) ||
        (audit.viol_flags & (uint32_t)SOUND_CVIOL_UNCONSCIONABLE) == UINT32_C(0)) {
        fails++;
    }

    /* 3. Hunt witness + FIR gate */
    sound_hunt_contract_drift(UINT64_C(0x534F554E445F3330), &spec, &audit);
    if (audit.certified != UINT32_C(0) || audit.repaired_certified != UINT32_C(1) ||
        audit.num_predatory_clauses != UINT32_C(4) || audit.benefit_drift <= (int64_t)0 ||
        (audit.viol_flags & UINT32_C(0xBF)) != UINT32_C(0xBF) ||
        !sound_cert_emit_contract_fir(&audit, NULL, &g_cert_ws) || !g_cert_ws.ir_eval_ok) {
        fails++;
    }
    return fails;
}

static uint32_t sound_test_t20_compositional_loophole(void) {
    uint32_t fails = UINT32_C(0);
    static sound_contract_spec_t spec;
    static sound_contract_audit_t audit;

    /* 1. Gauge-trivial Z4 cycle (1 + 2 + 1 = 4 = 0 mod 4) + benign redirects -> CERTIFIED */
    sound_zero_bytes(&spec, sizeof(spec));
    spec.max_loss_cap = UINT64_C(10000);
    spec.max_exit_fee = UINT64_C(2000);
    spec.protected_mask = UINT64_C(0x1);
    spec.max_asym_num = UINT64_C(2);
    spec.max_asym_den = UINT64_C(1);
    spec.num_clauses = 3U;
    for (uint32_t i = 0U; i < 3U; i++) {
        spec.clauses[i].clause_id = i + 1U;
        spec.clauses[i].kind = (uint8_t)SOUND_CLAUSE_BASE_FEE;
        spec.clauses[i].disclosed = UINT8_C(1);
        spec.clauses[i].b4_truth = (uint8_t)SOUND_B4_TRUE;
        spec.clauses[i].base_loss = UINT64_C(2000);
        spec.clauses[i].exit_fee = UINT64_C(500);
        spec.clauses[i].cons_penalty = UINT64_C(100);
        spec.clauses[i].inst_penalty = UINT64_C(100);
    }
    spec.clauses[0].target_mask = UINT64_C(0x1);
    spec.num_edges = 3U;
    spec.edges[0].src = 0U;
    spec.edges[0].dst = 1U;
    spec.edges[0].edge_kind = (uint8_t)SOUND_EDGE_REDIRECT;
    spec.edges[0].cocycle_shift = UINT8_C(1);
    spec.edges[1].src = 1U;
    spec.edges[1].dst = 2U;
    spec.edges[1].edge_kind = (uint8_t)SOUND_EDGE_REDIRECT;
    spec.edges[1].cocycle_shift = UINT8_C(2);
    spec.edges[2].src = 2U;
    spec.edges[2].dst = 0U;
    spec.edges[2].edge_kind = (uint8_t)SOUND_EDGE_REDIRECT;
    spec.edges[2].cocycle_shift = UINT8_C(1);
    sound_audit_contract(&spec, &audit);
    if (audit.certified != UINT32_C(1) || audit.graph_sheaf_h1_obstructed != UINT32_C(0) ||
        audit.graph_bypass_mask != UINT32_C(0) || audit.viol_flags != UINT32_C(0)) {
        fails++;
    }

    /* 2. Mobius Z4 loop (1+1+1 = 3 mod 4) + 2-hop power_delegate -> redirect bypass */
    spec.edges[1].edge_kind = (uint8_t)SOUND_EDGE_POWER_DELEGATE;
    spec.edges[1].cocycle_shift = UINT8_C(1);
    sound_audit_contract(&spec, &audit);
    if (audit.certified != UINT32_C(0) || audit.num_predatory_clauses != UINT32_C(0) ||
        audit.graph_sheaf_h1_obstructed != UINT32_C(1) ||
        (audit.viol_flags & (uint32_t)SOUND_CVIOL_GRAPH_SHEAF_H1) == UINT32_C(0) ||
        (audit.viol_flags & (uint32_t)SOUND_CVIOL_GRAPH_BYPASS) == UINT32_C(0) ||
        (audit.graph_bypass_mask & UINT32_C(1)) == UINT32_C(0) ||
        audit.repaired_certified != UINT32_C(1)) {
        fails++;
    }

    /* 3. Default trigger edge + coverage gaps fail closed */
    spec.edges[0].edge_kind = (uint8_t)SOUND_EDGE_DEFAULT_TRIGGER;
    spec.edges[0].cocycle_shift = UINT8_C(0);
    spec.edges[0].extra_exposure = UINT64_C(6000);
    spec.coverage_gaps = 2U;
    sound_audit_contract(&spec, &audit);
    if (audit.certified != UINT32_C(0) ||
        (audit.viol_flags & (uint32_t)SOUND_CVIOL_GRAPH_CASCADE) == UINT32_C(0) ||
        (audit.viol_flags & (uint32_t)SOUND_CVIOL_COVERAGE_GAP) == UINT32_C(0) ||
        audit.edge_exposure_sum != UINT64_C(6000) || audit.benefit_drift <= (int64_t)0 ||
        audit.repaired_certified != UINT32_C(1)) {
        fails++;
    }

    /* 4. Compositional loophole hunt witness + FIR gate */
    sound_hunt_contract_loophole(UINT64_C(0x42), &spec, &audit);
    if (audit.certified != UINT32_C(0) || audit.num_predatory_clauses != UINT32_C(0) ||
        audit.repaired_certified != UINT32_C(1) || audit.graph_sheaf_h1_obstructed != UINT32_C(1) ||
        audit.graph_bypass_mask == UINT32_C(0) || audit.edge_exposure_sum != UINT64_C(5000) ||
        audit.benefit_drift <= (int64_t)0 ||
        !sound_cert_emit_contract_fir(&audit, NULL, &g_cert_ws) || !g_cert_ws.ir_eval_ok) {
        fails++;
    }
    return fails;
}

static uint32_t sound_test_t21_packed_wire_roundtrip(void) {
    uint32_t fails = UINT32_C(0);
    static sound_contract_spec_t spec_in, spec_out;
    static sound_contract_audit_t audit_in, audit_out;
    static uint8_t pkt[SOUND_CERT_MAX_BYTES];
    size_t pkt_len = 0U;

    /* 1. Contract spec & contract cert "SNDC" pack/unpack bit-exact roundtrip */
    sound_hunt_contract_loophole(UINT64_C(0x42), &spec_in, &audit_in);
    if (!sound_cert_emit_contract_fir(&audit_in, NULL, &g_cert_ws) || !g_cert_ws.ir_eval_ok) {
        return UINT32_C(1);
    }
    if (!sound_cert_pack_contract_spec(&spec_in, pkt, sizeof(pkt), &pkt_len) ||
        !sound_cert_unpack_contract_spec(pkt, pkt_len, &spec_out)) {
        fails++;
    } else {
        const uint8_t *pa = (const uint8_t *)&spec_in;
        const uint8_t *pb = (const uint8_t *)&spec_out;
        for (size_t i = 0U; i < sizeof(spec_in); i++) {
            if (pa[i] != pb[i]) {
                fails++;
                break;
            }
        }
        pkt[pkt_len - 1U] ^= UINT8_C(1);
        if (sound_cert_unpack_contract_spec(pkt, pkt_len, &spec_out)) {
            fails++;
        }
    }

    if (!sound_cert_pack_contract_cert(&spec_in, &audit_in, &g_cert_ws, pkt, sizeof(pkt),
                                       &pkt_len)) {
        fails++;
    } else {
        size_t fbytes = 0U;
        bool feval = false;
        if (!sound_cert_unpack_contract_cert(pkt, pkt_len, &spec_out, &audit_out, &fbytes,
                                             &feval) ||
            fbytes != g_cert_ws.wire_len || feval != g_cert_ws.ir_eval_ok ||
            audit_out.certified != audit_in.certified ||
            audit_out.viol_flags != audit_in.viol_flags ||
            audit_out.benefit_drift != audit_in.benefit_drift ||
            audit_out.merkle != audit_in.merkle) {
            fails++;
        }
        pkt[pkt_len - 1U] ^= UINT8_C(1);
        if (sound_cert_unpack_contract_cert(pkt, pkt_len, &spec_out, &audit_out, &fbytes, &feval)) {
            fails++;
        }
    }

    /* 2. All 6 witness hunt modes "SNDC" + "FIR2" roundtrip */
    {
        sound_paradox_witness_t w_in, w_out;
        sound_hunt_paradox_min(UINT64_C(0x1337), &w_in);
        if (!sound_cert_emit_paradox_fir(&w_in, NULL, &g_cert_ws) ||
            !sound_cert_pack_hunt_witness(SOUND_HUNT_MODE_PARADOX_MIN, &w_in, &g_cert_ws, pkt,
                                          sizeof(pkt), &pkt_len) ||
            !sound_cert_unpack_hunt_witness(pkt, pkt_len, NULL, &w_out, sizeof(w_out), NULL,
                                            NULL) ||
            w_out.merkle != w_in.merkle || w_out.atk_to[0] != w_in.atk_to[0]) {
            fails++;
        }
    }
    {
        sound_sheaf_witness_t w_in, w_out;
        sound_hunt_sheaf_h1(UINT64_C(0x1337), &w_in);
        if (!sound_cert_emit_sheaf_fir(&w_in, NULL, &g_cert_ws) ||
            !sound_cert_pack_hunt_witness(SOUND_HUNT_MODE_SHEAF_H1, &w_in, &g_cert_ws, pkt,
                                          sizeof(pkt), &pkt_len) ||
            !sound_cert_unpack_hunt_witness(pkt, pkt_len, NULL, &w_out, sizeof(w_out), NULL,
                                            NULL) ||
            w_out.merkle != w_in.merkle || w_out.xor_holonomy != w_in.xor_holonomy) {
            fails++;
        }
    }
    {
        sound_chisholm_witness_t w_in, w_out;
        sound_hunt_chisholm(&w_in);
        if (!sound_cert_emit_chisholm_fir(NULL, &g_cert_ws) ||
            !sound_cert_pack_hunt_witness(SOUND_HUNT_MODE_CHISHOLM, &w_in, &g_cert_ws, pkt,
                                          sizeof(pkt), &pkt_len) ||
            !sound_cert_unpack_hunt_witness(pkt, pkt_len, NULL, &w_out, sizeof(w_out), NULL,
                                            NULL) ||
            w_out.merkle != w_in.merkle || w_out.established_mask != w_in.established_mask) {
            fails++;
        }
    }
    {
        sound_norm_trap_witness_t w_in, w_out;
        sound_hunt_norm_trap(UINT64_C(0x77), &w_in);
        if (!sound_cert_emit_norm_trap_fir(&w_in, NULL, &g_cert_ws) ||
            !sound_cert_pack_hunt_witness(SOUND_HUNT_MODE_NORM_TRAP, &w_in, &g_cert_ws, pkt,
                                          sizeof(pkt), &pkt_len) ||
            !sound_cert_unpack_hunt_witness(pkt, pkt_len, NULL, &w_out, sizeof(w_out), NULL,
                                            NULL) ||
            w_out.merkle != w_in.merkle || w_out.final_pos != w_in.final_pos) {
            fails++;
        }
    }
    {
        sound_aspect_witness_t w_in, w_out;
        sound_hunt_aspect_firewall(UINT64_C(0x42), &w_in);
        if (!sound_cert_emit_aspect_fir(NULL, &g_cert_ws) ||
            !sound_cert_pack_hunt_witness(SOUND_HUNT_MODE_ASPECT_FIREWALL, &w_in, &g_cert_ws, pkt,
                                          sizeof(pkt), &pkt_len) ||
            !sound_cert_unpack_hunt_witness(pkt, pkt_len, NULL, &w_out, sizeof(w_out), NULL,
                                            NULL) ||
            w_out.merkle != w_in.merkle || w_out.j_end != w_in.j_end) {
            fails++;
        }
    }
    {
        sound_presup_witness_t w_in, w_out;
        sound_hunt_presup_filter(UINT64_C(0x55), &w_in);
        if (!sound_cert_emit_presup_fir(NULL, &g_cert_ws) ||
            !sound_cert_pack_hunt_witness(SOUND_HUNT_MODE_PRESUP_FILTER, &w_in, &g_cert_ws, pkt,
                                          sizeof(pkt), &pkt_len) ||
            !sound_cert_unpack_hunt_witness(pkt, pkt_len, NULL, &w_out, sizeof(w_out), NULL,
                                            NULL) ||
            w_out.merkle != w_in.merkle || w_out.cond_false_presup != w_in.cond_false_presup) {
            fails++;
        }
    }
    return fails;
}

/* ============================================================================
 * File I/O & CLI Commands
 * ============================================================================ */

static bool sound_read_bin_file_buf(const char *path, uint8_t *buf, size_t cap, size_t *out_len) {
    if (path == NULL || buf == NULL || out_len == NULL || cap == 0U) {
        return false;
    }
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return false;
    }
    size_t total = 0U;
    while (total < cap) {
        size_t got = fread(buf + total, 1U, cap - total, fp);
        total += got;
        if (got == 0U || feof(fp) != 0 || ferror(fp) != 0) {
            break;
        }
    }
    int extra = fgetc(fp);
    int err = ferror(fp);
    fclose(fp);
    if (err != 0 || extra != EOF) {
        return false;
    }
    *out_len = total;
    return true;
}

static bool sound_load_arena_file(const char *path, SoundArena *arena) {
    static uint8_t wire[18432];
    static uint8_t raw[SOUND_ARENA_BYTES];
    size_t total = 0U;
    if (!sound_read_bin_file_buf(path, wire, sizeof(wire), &total)) {
        return false;
    }
    if (total >= (size_t)FLOW_ARENA_PACK_HDR_B + 8U &&
        flow_ir_wire_read_le32(wire) == FLOW_ARENA_PACK_MAGIC) {
        size_t raw_len = 0U;
        if (!flow_arena_unpack_bytes(wire, total, raw, sizeof(raw), &raw_len) ||
            raw_len != SOUND_ARENA_BYTES) {
            return false;
        }
    } else if (total == SOUND_ARENA_BYTES) {
        for (size_t i = 0U; i < SOUND_ARENA_BYTES; i++) {
            raw[i] = wire[i];
        }
    } else {
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

static bool sound_save_packed_arena_file(const char *path, const SoundArena *arena,
                                         size_t *out_len) {
    static uint8_t raw[SOUND_ARENA_BYTES];
    static uint8_t packed[18432];
    static uint8_t check[SOUND_ARENA_BYTES];
    if (path == NULL || arena == NULL) {
        return false;
    }
    for (size_t i = 0U; i < SOUND_ARENA_CELLS; i++) {
        flow_cell_to_bytes(arena->cells[i].u64, raw + (i * 64U));
    }
    size_t plen = 0U;
    size_t clen = 0U;
    if (!flow_arena_pack_bytes(raw, sizeof(raw), packed, sizeof(packed), &plen) ||
        !flow_arena_unpack_bytes(packed, plen, check, sizeof(check), &clen) ||
        clen != sizeof(raw) || !sound_cert_bytes_equal(raw, check, sizeof(raw))) {
        return false;
    }
    if (!sound_save_bin_file(path, packed, plen)) {
        return false;
    }
    if (out_len != NULL) {
        *out_len = plen;
    }
    return true;
}

static bool sound_save_raw_arena_file(const char *path, const SoundArena *arena) {
    static uint8_t raw[SOUND_ARENA_BYTES];
    if (path == NULL || arena == NULL) {
        return false;
    }
    for (size_t i = 0U; i < SOUND_ARENA_CELLS; i++) {
        flow_cell_to_bytes(arena->cells[i].u64, raw + (i * 64U));
    }
    return sound_save_bin_file(path, raw, sizeof(raw));
}

static void sound_derive_prog_name(char dst[24], const char *path) {
    if (path == NULL) {
        static const char k_fallback[] = "sound_auth_gate";
        for (size_t i = 0U; i < sizeof(k_fallback); i++) {
            dst[i] = k_fallback[i];
        }
        return;
    }
    const char *base = path;
    for (size_t i = 0U; path[i] != '\0'; i++) {
        if (path[i] == '/' || path[i] == '\\') {
            base = &path[i + 1U];
        }
    }
    size_t len = 0U;
    while (base[len] != '\0') {
        len++;
    }
    if (len > 4U && base[len - 4U] == '.' && base[len - 3U] == 's' && base[len - 2U] == 'n' &&
        base[len - 1U] == 'd') {
        len -= 4U;
    } else if (len > 5U && base[len - 5U] == '.' && base[len - 4U] == 'f' &&
               base[len - 3U] == 'l' && base[len - 2U] == 'w' && base[len - 1U] == 'a') {
        len -= 5U;
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
    uint32_t f[21];
    f[0] = sound_test_t1_kernel();
    f[1] = sound_test_t2_hohfeld_and_rollback();
    f[2] = sound_test_t3_frontend_gate();
    f[3] = sound_test_t4_arena_merkle();
    f[4] = sound_test_t5_searu_repair();
    f[5] = sound_test_t6_fuzz_4096();
    f[6] = sound_test_t7_fir_lift();
    f[7] = sound_test_t8_siphash_and_atomic_batch();
    f[8] = sound_test_t9_lex_specialis_and_chart_glue();
    f[9] = sound_test_t10_policy_eval_fir_lift();
    f[10] = sound_test_t11_bilattice_classify();
    f[11] = sound_test_t12_paradox_min();
    f[12] = sound_test_t13_sheaf_classify();
    f[13] = sound_test_t14_sheaf_h1_hunt();
    f[14] = sound_test_t15_hohfeld_v4_classify();
    f[15] = sound_test_t16_chisholm_norm_trap();
    f[16] = sound_test_t17_vendler_aspect();
    f[17] = sound_test_t18_projection_and_forces();
    f[18] = sound_test_t19_contract_drift();
    f[19] = sound_test_t20_compositional_loophole();
    f[20] = sound_test_t21_packed_wire_roundtrip();

    uint32_t tot = UINT32_C(0);
    for (uint32_t i = 0U; i < 21U; i++) {
        tot += f[i];
    }
    printf(
        "[selftest] T1..T10(kernel)=%s T11(bilat)=%s T12(paradox)=%s T13(sheaf_cls)=%s "
        "T14(sheaf_h1)=%s T15(hohfeld_v4)=%s T16(chisholm_trap)=%s T17(aspect)=%s "
        "T18(presup_force)=%s T19(contract)=%s T20(loophole)=%s T21(wire_pack)=%s | fails=%u\n",
        (f[0] + f[1] + f[2] + f[3] + f[4] + f[5] + f[6] + f[7] + f[8] + f[9]) == 0U ? "PASS"
                                                                                    : "FAIL",
        f[10] == 0U ? "PASS" : "FAIL", f[11] == 0U ? "PASS" : "FAIL", f[12] == 0U ? "PASS" : "FAIL",
        f[13] == 0U ? "PASS" : "FAIL", f[14] == 0U ? "PASS" : "FAIL", f[15] == 0U ? "PASS" : "FAIL",
        f[16] == 0U ? "PASS" : "FAIL", f[17] == 0U ? "PASS" : "FAIL", f[18] == 0U ? "PASS" : "FAIL",
        f[19] == 0U ? "PASS" : "FAIL", f[20] == 0U ? "PASS" : "FAIL", tot);
    return (tot == 0U) ? 0 : 1;
}

static int sound_cmd_verify(const char *path, bool json_out) {
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
    if (json_out) {
        printf("{\n");
        printf("  \"path\": \"%s\",\n", path);
        printf("  \"num_nodes\": %u,\n", g_cli_arena.map.img_hdr.num_nodes);
        printf("  \"node_mask\": %llu,\n", (unsigned long long)g_cli_arena.map.eval_out.node_mask);
        printf("  \"i_star\": %llu,\n", (unsigned long long)g_cli_arena.map.eval_out.i_star);
        printf("  \"p_star\": %llu,\n", (unsigned long long)g_cli_arena.map.eval_out.p_star);
        printf("  \"established_mask\": %llu,\n",
               (unsigned long long)g_cli_arena.map.eval_out.established_mask);
        printf("  \"defeated_mask\": %llu,\n",
               (unsigned long long)g_cli_arena.map.eval_out.defeated_mask);
        printf("  \"conflict_mask\": %llu,\n",
               (unsigned long long)g_cli_arena.map.eval_out.conflict_mask);
        printf("  \"penumbra_mask\": %llu\n",
               (unsigned long long)g_cli_arena.map.eval_out.penumbra_mask);
        printf("}\n");
        return 0;
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
        (unsigned)g_cli_arena.map.searu_repair.iters_used,
        (unsigned)g_cli_arena.map.searu_repair.converged);
    return (g_cli_arena.map.searu_repair.converged == UINT32_C(1)) ? 0 : 1;
}

static int sound_cmd_lift_fir(int argc, char **argv) {
    const char *in_path = argv[2];
    const char *out_path = argv[3];
    bool use_repaired = false;
    bool raw_fir = false;
    uint64_t seed = UINT64_C(0x53454152555F3031);
    for (int i = 4; i < argc; i++) {
        if (sound_cstr_eq(argv[i], "--repaired")) {
            use_repaired = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                if (!sound_parse_u64(argv[i + 1], &seed)) {
                    fprintf(stderr, "ERROR: invalid seed '%s'\n", argv[i + 1]);
                    return 1;
                }
                i++;
            }
        } else if (sound_cstr_eq(argv[i], "--raw-fir")) {
            raw_fir = true;
        } else {
            fprintf(stderr, "ERROR: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }
    if (!sound_load_arena_file(in_path, &g_cli_arena)) {
        fprintf(stderr, "ERROR: unable to read SoundArena (.flwa/.snd) '%s'\n", in_path);
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
    if (!flow_ir_load_bytes(&prog, fir_buf, fir_len)) {
        fprintf(stderr, "ERROR: lifted FIR1 failed decode\n");
        return 1;
    }
    uint8_t out_wire[SOUND_FIR_BYTES];
    size_t out_len = 0U;
    if (raw_fir) {
        for (size_t i = 0U; i < fir_len; i++) {
            out_wire[i] = fir_buf[i];
        }
        out_len = fir_len;
    } else {
        if (!flow_ir_encode_packed_bytes(&prog, out_wire, sizeof(out_wire), &out_len) ||
            !flow_ir_load_bytes(&prog, out_wire, out_len)) {
            fprintf(stderr, "ERROR: FIR2 pack/reload roundtrip failed\n");
            return 1;
        }
    }
    if (!flow_ir_validate(&prog) || !flow_ir_admits_unitary(&prog) ||
        !flow_ir_analyze_dag(&prog, &dag)) {
        fprintf(stderr, "ERROR: lifted %s failed FLOW_IR unitary/DAG audit\n",
                raw_fir ? "FIR1" : "FIR2");
        return 1;
    }
    if (!sound_save_bin_file(out_path, out_wire, out_len)) {
        fprintf(stderr, "ERROR: unable to write '%s'\n", out_path);
        return 1;
    }

    uint64_t active_mask = use_repaired ? g_cli_arena.map.searu_repair.candidate_mask
                                        : g_cli_arena.map.eval_out.established_mask;
    printf("[lift-fir] %s -> %s (%s %zuB, raw=%zuB, name=%s, active=0x%016llx) | mult_bits=%u "
           "mult_depth=%u unitary=OK\n",
           in_path, out_path, raw_fir ? "FIR1" : "FIR2", out_len, fir_len, prog.name,
           (unsigned long long)active_mask, dag.mult_bits, dag.mult_depth);
    return 0;
}

static int sound_cmd_lift_policy_fir(int argc, char **argv) {
    const char *in_path = argv[2];
    const char *out_path = argv[3];
    bool raw_fir = (argc >= 5 && sound_cstr_eq(argv[4], "--raw-fir"));
    if (argc >= 5 && !raw_fir) {
        fprintf(stderr, "ERROR: unknown option '%s'\n", argv[4]);
        return 1;
    }
    if (!sound_load_arena_file(in_path, &g_cli_arena)) {
        fprintf(stderr, "ERROR: unable to read SoundArena (.flwa/.snd) '%s'\n", in_path);
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
    static uint8_t packed_buf[65536];
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
    if (!flow_ir_load_bytes(&prog, fir_buf, fir_len)) {
        fprintf(stderr, "ERROR: lifted policy FIR1 failed decode\n");
        return 1;
    }
    const uint8_t *out_ptr = fir_buf;
    size_t out_len = fir_len;
    if (!raw_fir) {
        if (!flow_ir_encode_packed_bytes(&prog, packed_buf, sizeof(packed_buf), &out_len) ||
            !flow_ir_load_bytes(&prog, packed_buf, out_len)) {
            fprintf(stderr, "ERROR: policy FIR2 pack/reload roundtrip failed\n");
            return 1;
        }
        out_ptr = packed_buf;
    }
    if (!flow_ir_validate(&prog) || !flow_ir_admits_unitary(&prog) ||
        !flow_ir_analyze_dag(&prog, &dag)) {
        fprintf(stderr, "ERROR: lifted policy %s failed FLOW_IR unitary/DAG audit\n",
                raw_fir ? "FIR1" : "FIR2");
        return 1;
    }
    if (!sound_save_bin_file(out_path, out_ptr, out_len)) {
        fprintf(stderr, "ERROR: unable to write '%s'\n", out_path);
        return 1;
    }
    printf("[lift-policy-fir] %s -> %s (%s %zuB, raw=%zuB, name=%s, ops=%u) | mult_bits=%u "
           "mult_depth=%u unitary=OK\n",
           in_path, out_path, raw_fir ? "FIR1" : "FIR2", out_len, fir_len, prog.name, prog.num_ops,
           dag.mult_bits, dag.mult_depth);
    return 0;
}

static const char *sound_parse_opt(int argc, char **argv, int start, const char *flag) {
    for (int i = start; i + 1 < argc; i++) {
        if (sound_cstr_eq(argv[i], flag)) {
            return argv[i + 1];
        }
    }
    return NULL;
}

static int sound_cmd_classify_bilattice(const char *out_json) {
    sound_bilattice_atlas_t a;
    sound_classify_bilattice(&a);
    FILE *fp = (out_json != NULL) ? fopen(out_json, "wb") : stdout;
    if (fp == NULL) {
        return 1;
    }
    fprintf(fp, "{\n");
    fprintf(fp, "  \"b4_v4_ok\": %u,\n", a.b4_v4_ok);
    fprintf(fp, "  \"b4_demorgan_ok\": %u,\n", a.b4_demorgan_ok);
    fprintf(fp, "  \"b4_interlaced_ok\": %u,\n", a.b4_interlaced_ok);
    fprintf(fp, "  \"b4_distrib_ok\": %u,\n", a.b4_distrib_ok);
    fprintf(fp, "  \"n3_total\": %u,\n", a.n3_total);
    fprintf(fp, "  \"n3_grounded_unique\": %u,\n", a.n3_grounded_unique);
    fprintf(fp, "  \"n3_unique_nongrounded\": %u,\n", a.n3_unique_nongrounded);
    fprintf(fp, "  \"n3_multi_stable\": %u,\n", a.n3_multi_stable);
    fprintf(fp, "  \"n3_classical_unsolvable\": %u,\n", a.n3_classical_unsolvable);
    fprintf(fp, "  \"n3_conflict_nonempty\": %u,\n", a.n3_conflict_nonempty);
    fprintf(fp, "  \"n4_total\": %u,\n", a.n4_total);
    fprintf(fp, "  \"n4_grounded_unique\": %u,\n", a.n4_grounded_unique);
    fprintf(fp, "  \"n4_unique_nongrounded\": %u,\n", a.n4_unique_nongrounded);
    fprintf(fp, "  \"n4_multi_stable\": %u,\n", a.n4_multi_stable);
    fprintf(fp, "  \"n4_classical_unsolvable\": %u,\n", a.n4_classical_unsolvable);
    fprintf(fp, "  \"n4_conflict_nonempty\": %u\n", a.n4_conflict_nonempty);
    fprintf(fp, "}\n");
    if (out_json != NULL) {
        fclose(fp);
    }
    return 0;
}

static int sound_cmd_classify_sheaf(const char *out_json) {
    sound_sheaf_atlas_t a;
    sound_classify_sheaf(&a);
    FILE *fp = (out_json != NULL) ? fopen(out_json, "wb") : stdout;
    if (fp == NULL) {
        return 1;
    }
    fprintf(fp, "{\n");
    fprintf(fp, "  \"c3_z4_total\": %u,\n", a.c3_z4_total);
    fprintf(fp, "  \"c3_z4_coboundary\": %u,\n", a.c3_z4_coboundary);
    fprintf(fp, "  \"c3_z4_obstructed\": %u,\n", a.c3_z4_obstructed);
    fprintf(fp, "  \"c4_z4_total\": %u,\n", a.c4_z4_total);
    fprintf(fp, "  \"c4_z4_coboundary\": %u,\n", a.c4_z4_coboundary);
    fprintf(fp, "  \"c4_z4_obstructed\": %u,\n", a.c4_z4_obstructed);
    fprintf(fp, "  \"p3_z4_total\": %u,\n", a.p3_z4_total);
    fprintf(fp, "  \"p3_z4_coboundary\": %u,\n", a.p3_z4_coboundary);
    fprintf(fp, "  \"c3_v4_total\": %u,\n", a.c3_v4_total);
    fprintf(fp, "  \"c3_v4_coboundary\": %u,\n", a.c3_v4_coboundary);
    fprintf(fp, "  \"gauge_invariance_ok\": %u\n", a.gauge_invariance_ok);
    fprintf(fp, "}\n");
    if (out_json != NULL) {
        fclose(fp);
    }
    return 0;
}

static int sound_cmd_classify_hohfeld(const char *out_json) {
    sound_hohfeld_atlas_t a;
    sound_classify_hohfeld(&a);
    FILE *fp = (out_json != NULL) ? fopen(out_json, "wb") : stdout;
    if (fp == NULL) {
        return 1;
    }
    fprintf(fp, "{\n");
    fprintf(fp, "  \"v4_group_ok\": %u,\n", a.v4_group_ok);
    fprintf(fp, "  \"v4_orbits\": %u,\n", a.v4_orbits);
    fprintf(fp, "  \"transitions_checked\": %u,\n", a.transitions_checked);
    fprintf(fp, "  \"coherence_violations\": %u,\n", a.coherence_violations);
    fprintf(fp, "  \"immunity_leaks\": %u,\n", a.immunity_leaks);
    fprintf(fp, "  \"disability_leaks\": %u,\n", a.disability_leaks);
    fprintf(fp, "  \"reachable_imm0\": %u,\n", a.reachable_imm0);
    fprintf(fp, "  \"reachable_imm1\": %u,\n", a.reachable_imm1);
    fprintf(fp, "  \"reachable_imm2\": %u,\n", a.reachable_imm2);
    fprintf(fp, "  \"reachable_imm3\": %u\n", a.reachable_imm3);
    fprintf(fp, "}\n");
    if (out_json != NULL) {
        fclose(fp);
    }
    return 0;
}

static int sound_cmd_classify_projection(const char *out_json) {
    sound_projection_atlas_t a;
    sound_classify_projection(&a);
    FILE *fp = (out_json != NULL) ? fopen(out_json, "wb") : stdout;
    if (fp == NULL) {
        return 1;
    }
    fprintf(fp, "{\n");
    fprintf(fp, "  \"total_unary_maps\": %u,\n", a.total_unary_maps);
    fprintf(fp, "  \"involutive_maps\": %u,\n", a.involutive_maps);
    fprintf(fp, "  \"t_antitone_maps\": %u,\n", a.t_antitone_maps);
    fprintf(fp, "  \"k_monotone_maps\": %u,\n", a.k_monotone_maps);
    fprintf(fp, "  \"classical_neg_maps\": %u,\n", a.classical_neg_maps);
    fprintf(fp, "  \"hole_presup_pairs\": %u,\n", a.hole_presup_pairs);
    fprintf(fp, "  \"strawson_gap_pairs\": %u,\n", a.strawson_gap_pairs);
    fprintf(fp, "  \"cond_filter_ok\": %u,\n", a.cond_filter_ok);
    fprintf(fp, "  \"plug_irrealis_ok\": %u,\n", a.plug_irrealis_ok);
    fprintf(fp, "  \"force_invariants_ok\": %u\n", a.force_invariants_ok);
    fprintf(fp, "}\n");
    if (out_json != NULL) {
        fclose(fp);
    }
    return 0;
}

static int sound_cmd_hunt(int argc, char **argv) {
    const char *mode = argv[2];
    const char *seed_str = sound_parse_opt(argc, argv, 3, "--seed");
    const char *out_lean = sound_parse_opt(argc, argv, 3, "--out-lean");
    const char *out_fir = sound_parse_opt(argc, argv, 3, "--out-fir");
    const char *out_json = sound_parse_opt(argc, argv, 3, "--out-json");
    const char *out_cert = sound_parse_opt(argc, argv, 3, "--out-cert");
    uint64_t seed = UINT64_C(0x534F554E445F3330);
    if (seed_str != NULL && !sound_parse_u64(seed_str, &seed)) {
        fprintf(stderr, "ERROR: invalid seed '%s'\n", seed_str);
        return 1;
    }

    if (sound_cstr_eq(mode, "paradox-min")) {
        sound_paradox_witness_t w;
        sound_hunt_paradox_min(seed, &w);
        if (!sound_cert_emit_paradox_fir(&w, out_fir, &g_cert_ws)) {
            return 1;
        }
        if (out_lean != NULL && !sound_cert_emit_paradox_lean(&w, out_lean)) {
            return 1;
        }
        if (out_json != NULL && !sound_cert_emit_paradox_json(&w, &g_cert_ws, out_json)) {
            return 1;
        }
        if (out_cert != NULL && !sound_cert_emit_hunt_cert_file(SOUND_HUNT_MODE_PARADOX_MIN, &w,
                                                                &g_cert_ws, out_cert)) {
            return 1;
        }
        printf("[hunt paradox-min] models=%u conf=0x%x merkle=0x%016llx fir2=%zuB\n",
               w.classical_models, (unsigned)w.conflict_mask, (unsigned long long)w.merkle,
               g_cert_ws.wire_len);
        return 0;
    }
    if (sound_cstr_eq(mode, "sheaf-h1")) {
        sound_sheaf_witness_t w;
        sound_hunt_sheaf_h1(seed, &w);
        if (!sound_cert_emit_sheaf_fir(&w, out_fir, &g_cert_ws)) {
            return 1;
        }
        if (out_lean != NULL && !sound_cert_emit_sheaf_lean(&w, out_lean)) {
            return 1;
        }
        if (out_json != NULL && !sound_cert_emit_sheaf_json(&w, &g_cert_ws, out_json)) {
            return 1;
        }
        if (out_cert != NULL &&
            !sound_cert_emit_hunt_cert_file(SOUND_HUNT_MODE_SHEAF_H1, &w, &g_cert_ws, out_cert)) {
            return 1;
        }
        printf("[hunt sheaf-h1] hol_z4=%u cob=%u xor_hol=0x%016llx merkle=0x%016llx fir2=%zuB\n",
               (unsigned)w.holonomy_z4, (unsigned)w.is_coboundary_z4,
               (unsigned long long)w.xor_holonomy, (unsigned long long)w.merkle,
               g_cert_ws.wire_len);
        return 0;
    }
    if (sound_cstr_eq(mode, "chisholm")) {
        sound_chisholm_witness_t w;
        sound_hunt_chisholm(&w);
        if (!sound_cert_emit_chisholm_fir(out_fir, &g_cert_ws)) {
            return 1;
        }
        if (out_lean != NULL && !sound_cert_emit_chisholm_lean(&w, out_lean)) {
            return 1;
        }
        if (out_json != NULL && !sound_cert_emit_chisholm_json(&w, &g_cert_ws, out_json)) {
            return 1;
        }
        if (out_cert != NULL &&
            !sound_cert_emit_hunt_cert_file(SOUND_HUNT_MODE_CHISHOLM, &w, &g_cert_ws, out_cert)) {
            return 1;
        }
        printf("[hunt chisholm] est=0x%llx def=0x%llx conf=0x%llx merkle=0x%016llx fir2=%zuB\n",
               (unsigned long long)w.established_mask, (unsigned long long)w.defeated_mask,
               (unsigned long long)w.conflict_mask, (unsigned long long)w.merkle,
               g_cert_ws.wire_len);
        return 0;
    }
    if (sound_cstr_eq(mode, "norm-trap")) {
        sound_norm_trap_witness_t w;
        sound_hunt_norm_trap(seed, &w);
        if (!sound_cert_emit_norm_trap_fir(&w, out_fir, &g_cert_ws)) {
            return 1;
        }
        if (out_lean != NULL && !sound_cert_emit_norm_trap_lean(&w, out_lean)) {
            return 1;
        }
        if (out_json != NULL && !sound_cert_emit_norm_trap_json(&w, &g_cert_ws, out_json)) {
            return 1;
        }
        if (out_cert != NULL &&
            !sound_cert_emit_hunt_cert_file(SOUND_HUNT_MODE_NORM_TRAP, &w, &g_cert_ws, out_cert)) {
            return 1;
        }
        printf("[hunt norm-trap] freeze_ok=%u w=%u eA=%u eB=%u merkle=0x%016llx fir2=%zuB\n",
               w.freeze_ok, (unsigned)w.tie_weight, w.energy_keep_a, w.energy_keep_b,
               (unsigned long long)w.merkle, g_cert_ws.wire_len);
        return 0;
    }
    if (sound_cstr_eq(mode, "aspect-firewall")) {
        sound_aspect_witness_t w;
        sound_hunt_aspect_firewall(seed, &w);
        if (!sound_cert_emit_aspect_fir(out_fir, &g_cert_ws)) {
            return 1;
        }
        if (out_lean != NULL && !sound_cert_emit_aspect_lean(&w, out_lean)) {
            return 1;
        }
        if (out_json != NULL && !sound_cert_emit_aspect_json(&w, &g_cert_ws, out_json)) {
            return 1;
        }
        if (out_cert != NULL && !sound_cert_emit_hunt_cert_file(SOUND_HUNT_MODE_ASPECT_FIREWALL, &w,
                                                                &g_cert_ws, out_cert)) {
            return 1;
        }
        printf("[hunt aspect-firewall] bij=%u blocked=0x%x fiber=%u merkle=0x%016llx fir2=%zuB\n",
               w.aspect_bijection_ok, w.telic_blocked_mask, w.declarative_atelic_fiber,
               (unsigned long long)w.merkle, g_cert_ws.wire_len);
        return 0;
    }
    if (sound_cstr_eq(mode, "presup-filter")) {
        sound_presup_witness_t w;
        sound_hunt_presup_filter(seed, &w);
        if (!sound_cert_emit_presup_fir(out_fir, &g_cert_ws)) {
            return 1;
        }
        if (out_lean != NULL && !sound_cert_emit_presup_lean(&w, out_lean)) {
            return 1;
        }
        if (out_json != NULL && !sound_cert_emit_presup_json(&w, &g_cert_ws, out_json)) {
            return 1;
        }
        if (out_cert != NULL && !sound_cert_emit_hunt_cert_file(SOUND_HUNT_MODE_PRESUP_FILTER, &w,
                                                                &g_cert_ws, out_cert)) {
            return 1;
        }
        printf("[hunt presup-filter] gap=(%u,%u) cond_f=%u cond_t=%u merkle=0x%016llx fir2=%zuB\n",
               (unsigned)w.eval_fail_b, (unsigned)w.eval_neg_fail_b, (unsigned)w.cond_false_presup,
               (unsigned)w.cond_true_presup, (unsigned long long)w.merkle, g_cert_ws.wire_len);
        return 0;
    }
    if (sound_cstr_eq(mode, "contract-drift")) {
        static sound_contract_spec_t spec;
        static sound_contract_audit_t audit;
        sound_hunt_contract_drift(seed, &spec, &audit);
        if (!sound_cert_emit_contract_fir(&audit, out_fir, &g_cert_ws)) {
            return 1;
        }
        if (out_lean != NULL && !sound_cert_emit_contract_lean(&spec, &audit, out_lean)) {
            return 1;
        }
        if (out_json != NULL &&
            !sound_cert_emit_contract_json(&spec, &audit, &g_cert_ws, out_json)) {
            return 1;
        }
        if (out_cert != NULL &&
            !sound_cert_emit_contract_cert_file(&spec, &audit, &g_cert_ws, out_cert)) {
            return 1;
        }
        printf("[hunt contract-drift] cert=%u drift=%lld pred=%u rep_cert=%u merkle=0x%016llx "
               "fir2=%zuB\n",
               audit.certified, (long long)audit.benefit_drift, audit.num_predatory_clauses,
               audit.repaired_certified, (unsigned long long)audit.merkle, g_cert_ws.wire_len);
        return 0;
    }
    if (sound_cstr_eq(mode, "contract-loophole")) {
        static sound_contract_spec_t spec;
        static sound_contract_audit_t audit;
        sound_hunt_contract_loophole(seed, &spec, &audit);
        if (!sound_cert_emit_contract_fir(&audit, out_fir, &g_cert_ws)) {
            return 1;
        }
        if (out_lean != NULL && !sound_cert_emit_contract_lean(&spec, &audit, out_lean)) {
            return 1;
        }
        if (out_json != NULL &&
            !sound_cert_emit_contract_json(&spec, &audit, &g_cert_ws, out_json)) {
            return 1;
        }
        if (out_cert != NULL &&
            !sound_cert_emit_contract_cert_file(&spec, &audit, &g_cert_ws, out_cert)) {
            return 1;
        }
        printf("[hunt contract-loophole] cert=%u pred=%u edges=%u sheaf_h1=%u bypass=0x%x "
               "drift=%lld rep_cert=%u fir2=%zuB\n",
               audit.certified, audit.num_predatory_clauses, spec.num_edges,
               audit.graph_sheaf_h1_obstructed, audit.graph_bypass_mask,
               (long long)audit.benefit_drift, audit.repaired_certified, g_cert_ws.wire_len);
        return 0;
    }
    fprintf(stderr, "ERROR: unknown hunt mode '%s'\n", mode);
    return 1;
}

static bool sound_json_is_ws(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static const char *sound_json_skip_ws(const char *p, const char *end) {
    while (p < end && sound_json_is_ws(*p)) {
        p++;
    }
    return p;
}

static const char *sound_json_skip_value(const char *p, const char *end) {
    p = sound_json_skip_ws(p, end);
    if (p >= end) {
        return end;
    }
    if (*p == '"') {
        p++;
        while (p < end) {
            if (*p == '\\' && (p + 1) < end) {
                p += 2;
                continue;
            }
            if (*p == '"') {
                return p + 1;
            }
            p++;
        }
        return end;
    }
    if (*p == '{' || *p == '[') {
        int depth = 0;
        bool in_str = false;
        while (p < end) {
            if (in_str) {
                if (*p == '\\' && (p + 1) < end) {
                    p += 2;
                    continue;
                }
                if (*p == '"') {
                    in_str = false;
                }
            } else {
                if (*p == '"') {
                    in_str = true;
                } else if (*p == '{' || *p == '[') {
                    depth++;
                } else if (*p == '}' || *p == ']') {
                    depth--;
                    if (depth == 0) {
                        return p + 1;
                    }
                }
            }
            p++;
        }
        return end;
    }
    while (p < end && *p != ',' && *p != '}' && *p != ']' && !sound_json_is_ws(*p)) {
        p++;
    }
    return p;
}

static const char *sound_json_obj_get(const char *obj_start, const char *obj_end, const char *key) {
    const char *p = sound_json_skip_ws(obj_start, obj_end);
    if (p >= obj_end || *p != '{') {
        return NULL;
    }
    p++;
    size_t klen = 0U;
    while (key[klen] != '\0') {
        klen++;
    }
    while (p < obj_end) {
        p = sound_json_skip_ws(p, obj_end);
        if (p >= obj_end || *p == '}') {
            return NULL;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p != '"') {
            return NULL;
        }
        p++;
        const char *kstart = p;
        while (p < obj_end && *p != '"') {
            if (*p == '\\' && (p + 1) < obj_end) {
                p += 2;
                continue;
            }
            p++;
        }
        if (p >= obj_end) {
            return NULL;
        }
        size_t tok_len = (size_t)(p - kstart);
        bool match = (tok_len == klen);
        if (match) {
            for (size_t i = 0U; i < klen; i++) {
                if (kstart[i] != key[i]) {
                    match = false;
                    break;
                }
            }
        }
        p++; /* skip '"' */
        p = sound_json_skip_ws(p, obj_end);
        if (p >= obj_end || *p != ':') {
            return NULL;
        }
        p++;
        p = sound_json_skip_ws(p, obj_end);
        if (match) {
            return p;
        }
        p = sound_json_skip_value(p, obj_end);
    }
    return NULL;
}

static bool sound_json_parse_u64_val(const char *p, const char *end, uint64_t *out) {
    if (p == NULL || p >= end) {
        return false;
    }
    p = sound_json_skip_ws(p, end);
    if (*p == '"') {
        p++;
        const char *q = p;
        char buf[32];
        size_t i = 0U;
        while (q < end && *q != '"' && i < 31U) {
            buf[i++] = *q++;
        }
        if (q >= end || *q != '"') {
            return false;
        }
        buf[i] = '\0';
        return sound_parse_u64(buf, out);
    }
    char buf[32];
    size_t i = 0U;
    while (p < end && i < 31U && *p != ',' && *p != '}' && *p != ']' && !sound_json_is_ws(*p)) {
        buf[i++] = *p++;
    }
    if (i == 0U) {
        return false;
    }
    buf[i] = '\0';
    return sound_parse_u64(buf, out);
}

static bool sound_json_parse_bool_val(const char *p, const char *end, uint8_t *out) {
    if (p == NULL || p >= end) {
        return false;
    }
    p = sound_json_skip_ws(p, end);
    if ((p + 4) <= end && p[0] == 't' && p[1] == 'r' && p[2] == 'u' && p[3] == 'e') {
        *out = UINT8_C(1);
        return true;
    }
    if ((p + 5) <= end && p[0] == 'f' && p[1] == 'a' && p[2] == 'l' && p[3] == 's' && p[4] == 'e') {
        *out = UINT8_C(0);
        return true;
    }
    uint64_t v = UINT64_C(0);
    if (sound_json_parse_u64_val(p, end, &v)) {
        *out = (v != UINT64_C(0)) ? UINT8_C(1) : UINT8_C(0);
        return true;
    }
    return false;
}

static bool sound_json_read_str_val(const char *p, const char *end, char *dst, size_t cap) {
    if (p == NULL || p >= end || cap == 0U) {
        return false;
    }
    p = sound_json_skip_ws(p, end);
    if (p >= end || *p != '"') {
        return false;
    }
    p++;
    size_t i = 0U;
    while (p < end && *p != '"') {
        if (i + 1U < cap) {
            dst[i++] = *p;
        }
        p++;
    }
    dst[i] = '\0';
    return (p < end && *p == '"');
}

static uint8_t sound_parse_clause_kind_token(const char *p, const char *end) {
    char s[40];
    if (sound_json_read_str_val(p, end, s, sizeof(s))) {
        if (sound_cstr_eq(s, "unilateral_power") || sound_cstr_eq(s, "unilateralPower")) {
            return (uint8_t)SOUND_CLAUSE_UNILATERAL_POWER;
        }
        if (sound_cstr_eq(s, "ambiguous_obligation") || sound_cstr_eq(s, "ambiguousObligation")) {
            return (uint8_t)SOUND_CLAUSE_AMBIGUOUS_OBLIGATION;
        }
        if (sound_cstr_eq(s, "presup_smuggle") || sound_cstr_eq(s, "presupSmuggle")) {
            return (uint8_t)SOUND_CLAUSE_PRESUP_SMUGGLE;
        }
        if (sound_cstr_eq(s, "chisholm_default") || sound_cstr_eq(s, "chisholmDefault")) {
            return (uint8_t)SOUND_CLAUSE_CHISHOLM_DEFAULT;
        }
        if (sound_cstr_eq(s, "reciprocity")) {
            return (uint8_t)SOUND_CLAUSE_RECIPROCITY;
        }
        return (uint8_t)SOUND_CLAUSE_BASE_FEE;
    }
    uint64_t v = UINT64_C(0);
    if (sound_json_parse_u64_val(p, end, &v) && v <= UINT64_C(5)) {
        return (uint8_t)v;
    }
    return (uint8_t)SOUND_CLAUSE_BASE_FEE;
}

static uint8_t sound_parse_b4_token(const char *p, const char *end) {
    char s[24];
    if (sound_json_read_str_val(p, end, s, sizeof(s))) {
        if (sound_cstr_eq(s, "BOT") || sound_cstr_eq(s, "bot")) {
            return (uint8_t)SOUND_B4_BOT;
        }
        if (sound_cstr_eq(s, "FALSE") || sound_cstr_eq(s, "false")) {
            return (uint8_t)SOUND_B4_FALSE;
        }
        if (sound_cstr_eq(s, "TOP") || sound_cstr_eq(s, "top")) {
            return (uint8_t)SOUND_B4_TOP;
        }
        return (uint8_t)SOUND_B4_TRUE;
    }
    uint64_t v = UINT64_C(2);
    if (sound_json_parse_u64_val(p, end, &v)) {
        return (uint8_t)(v & UINT64_C(3));
    }
    return (uint8_t)SOUND_B4_TRUE;
}

static uint8_t sound_parse_edge_kind_token(const char *p, const char *end) {
    char s[40];
    if (sound_json_read_str_val(p, end, s, sizeof(s))) {
        if (sound_cstr_eq(s, "override")) {
            return (uint8_t)SOUND_EDGE_OVERRIDE;
        }
        if (sound_cstr_eq(s, "power_delegate") || sound_cstr_eq(s, "powerDelegate")) {
            return (uint8_t)SOUND_EDGE_POWER_DELEGATE;
        }
        if (sound_cstr_eq(s, "default_trigger") || sound_cstr_eq(s, "defaultTrigger")) {
            return (uint8_t)SOUND_EDGE_DEFAULT_TRIGGER;
        }
        return (uint8_t)SOUND_EDGE_REDIRECT;
    }
    uint64_t v = UINT64_C(1);
    if (sound_json_parse_u64_val(p, end, &v) && v <= UINT64_C(3)) {
        return (uint8_t)v;
    }
    return (uint8_t)SOUND_EDGE_REDIRECT;
}

static bool sound_parse_contract_json_file(const char *path, sound_contract_spec_t *spec) {
    static char json_buf[32768];
    if (path == NULL || spec == NULL) {
        return false;
    }
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return false;
    }
    size_t len = fread(json_buf, 1U, sizeof(json_buf) - 1U, fp);
    int err = ferror(fp);
    fclose(fp);
    if (err != 0 || len == 0U || len >= sizeof(json_buf) - 1U) {
        return false;
    }
    json_buf[len] = '\0';

    const char *root = json_buf;
    const char *end = json_buf + len;
    root = sound_json_skip_ws(root, end);
    if (root >= end || *root != '{') {
        return false;
    }

    sound_zero_bytes(spec, sizeof(*spec));
    spec->max_asym_num = UINT64_C(2);
    spec->max_asym_den = UINT64_C(1);

    uint64_t tmp = UINT64_C(0);
    if (sound_json_parse_u64_val(sound_json_obj_get(root, end, "seed"), end, &tmp)) {
        spec->seed = tmp;
    }
    if (sound_json_parse_u64_val(sound_json_obj_get(root, end, "max_loss_cap"), end, &tmp)) {
        spec->max_loss_cap = tmp;
    }
    if (sound_json_parse_u64_val(sound_json_obj_get(root, end, "max_exit_fee"), end, &tmp)) {
        spec->max_exit_fee = tmp;
    }
    if (sound_json_parse_u64_val(sound_json_obj_get(root, end, "protected_mask"), end, &tmp)) {
        spec->protected_mask = tmp;
    }
    if (sound_json_parse_u64_val(sound_json_obj_get(root, end, "max_asym_num"), end, &tmp)) {
        spec->max_asym_num = tmp;
    }
    if (sound_json_parse_u64_val(sound_json_obj_get(root, end, "max_asym_den"), end, &tmp)) {
        spec->max_asym_den = tmp;
    }
    if (sound_json_parse_u64_val(sound_json_obj_get(root, end, "coverage_gaps"), end, &tmp)) {
        spec->coverage_gaps = (uint32_t)tmp;
    }

    const char *arr = sound_json_obj_get(root, end, "clauses");
    if (arr == NULL || *arr != '[') {
        return false;
    }
    const char *arr_end = sound_json_skip_value(arr, end);
    const char *cur = arr + 1;
    while (cur < arr_end) {
        cur = sound_json_skip_ws(cur, arr_end);
        if (cur >= arr_end || *cur == ']') {
            break;
        }
        if (*cur == ',') {
            cur++;
            continue;
        }
        if (*cur != '{') {
            return false;
        }
        const char *c_end = sound_json_skip_value(cur, arr_end);
        if (spec->num_clauses >= SOUND_CONTRACT_MAX_CLAUSES) {
            spec->num_clauses++;
            break;
        }
        sound_contract_clause_t *cl = &spec->clauses[spec->num_clauses];
        cl->clause_id = spec->num_clauses + UINT32_C(1);
        cl->disclosed = UINT8_C(1);
        cl->b4_truth = (uint8_t)SOUND_B4_TRUE;
        cl->sheaf_cycle = UINT8_C(0);

        if (sound_json_parse_u64_val(sound_json_obj_get(cur, c_end, "clause_id"), c_end, &tmp)) {
            cl->clause_id = (uint32_t)tmp;
        }
        const char *kp = sound_json_obj_get(cur, c_end, "kind");
        if (kp != NULL) {
            cl->kind = sound_parse_clause_kind_token(kp, c_end);
        }
        uint8_t btmp = UINT8_C(0);
        if (sound_json_parse_bool_val(sound_json_obj_get(cur, c_end, "disclosed"), c_end, &btmp)) {
            cl->disclosed = btmp;
        }
        const char *b4p = sound_json_obj_get(cur, c_end, "b4_truth");
        if (b4p != NULL) {
            cl->b4_truth = sound_parse_b4_token(b4p, c_end);
        }
        if (sound_json_parse_bool_val(sound_json_obj_get(cur, c_end, "sheaf_cycle"), c_end,
                                      &btmp)) {
            cl->sheaf_cycle = btmp;
        }
        if (sound_json_parse_u64_val(sound_json_obj_get(cur, c_end, "target_mask"), c_end, &tmp)) {
            cl->target_mask = tmp;
        }
        if (sound_json_parse_u64_val(sound_json_obj_get(cur, c_end, "base_loss"), c_end, &tmp)) {
            cl->base_loss = tmp;
        }
        if (sound_json_parse_u64_val(sound_json_obj_get(cur, c_end, "extra_drift"), c_end, &tmp)) {
            cl->extra_drift = tmp;
        }
        if (sound_json_parse_u64_val(sound_json_obj_get(cur, c_end, "exit_fee"), c_end, &tmp)) {
            cl->exit_fee = tmp;
        }
        if (sound_json_parse_u64_val(sound_json_obj_get(cur, c_end, "cons_penalty"), c_end, &tmp)) {
            cl->cons_penalty = tmp;
        }
        if (sound_json_parse_u64_val(sound_json_obj_get(cur, c_end, "inst_penalty"), c_end, &tmp)) {
            cl->inst_penalty = tmp;
        }
        spec->num_clauses++;
        cur = c_end;
    }

    const char *earr = sound_json_obj_get(root, end, "edges");
    if (earr != NULL && *earr == '[') {
        const char *earr_end = sound_json_skip_value(earr, end);
        const char *ecur = earr + 1;
        while (ecur < earr_end) {
            ecur = sound_json_skip_ws(ecur, earr_end);
            if (ecur >= earr_end || *ecur == ']') {
                break;
            }
            if (*ecur == ',') {
                ecur++;
                continue;
            }
            if (*ecur != '{') {
                return false;
            }
            const char *e_end = sound_json_skip_value(ecur, earr_end);
            if (spec->num_edges >= SOUND_CONTRACT_MAX_EDGES) {
                spec->num_edges++;
                break;
            }
            sound_contract_edge_t *ed = &spec->edges[spec->num_edges];
            ed->edge_kind = (uint8_t)SOUND_EDGE_REDIRECT;
            if (sound_json_parse_u64_val(sound_json_obj_get(ecur, e_end, "src"), e_end, &tmp)) {
                ed->src = (uint32_t)tmp;
            }
            if (sound_json_parse_u64_val(sound_json_obj_get(ecur, e_end, "dst"), e_end, &tmp)) {
                ed->dst = (uint32_t)tmp;
            }
            const char *ek = sound_json_obj_get(ecur, e_end, "edge_kind");
            if (ek != NULL) {
                ed->edge_kind = sound_parse_edge_kind_token(ek, e_end);
            }
            if (sound_json_parse_u64_val(sound_json_obj_get(ecur, e_end, "cocycle_shift"), e_end,
                                         &tmp)) {
                ed->cocycle_shift = (uint8_t)(tmp & UINT64_C(3));
            }
            if (sound_json_parse_u64_val(sound_json_obj_get(ecur, e_end, "extra_exposure"), e_end,
                                         &tmp)) {
                ed->extra_exposure = tmp;
            }
            spec->num_edges++;
            ecur = e_end;
        }
    }
    return spec->num_clauses > UINT32_C(0);
}

static bool sound_parse_contract_file(const char *path, sound_contract_spec_t *spec) {
    static uint8_t bin_buf[SOUND_CERT_MAX_BYTES];
    size_t blen = 0U;
    if (path != NULL && spec != NULL &&
        sound_read_bin_file_buf(path, bin_buf, sizeof(bin_buf), &blen) && blen >= 16U &&
        flow_ir_wire_read_le32(bin_buf) == SOUND_CERT_PACK_MAGIC) {
        return sound_cert_unpack_contract_spec(bin_buf, blen, spec);
    }
    return sound_parse_contract_json_file(path, spec);
}

static const char *sound_clause_kind_json_str(uint8_t k) {
    switch (k) {
    case (uint8_t)SOUND_CLAUSE_UNILATERAL_POWER:
        return "unilateral_power";
    case (uint8_t)SOUND_CLAUSE_AMBIGUOUS_OBLIGATION:
        return "ambiguous_obligation";
    case (uint8_t)SOUND_CLAUSE_PRESUP_SMUGGLE:
        return "presup_smuggle";
    case (uint8_t)SOUND_CLAUSE_CHISHOLM_DEFAULT:
        return "chisholm_default";
    case (uint8_t)SOUND_CLAUSE_RECIPROCITY:
        return "reciprocity";
    default:
        return "base_fee";
    }
}

static const char *sound_edge_kind_json_str(uint8_t k) {
    switch (k) {
    case (uint8_t)SOUND_EDGE_OVERRIDE:
        return "override";
    case (uint8_t)SOUND_EDGE_POWER_DELEGATE:
        return "power_delegate";
    case (uint8_t)SOUND_EDGE_DEFAULT_TRIGGER:
        return "default_trigger";
    default:
        return "redirect";
    }
}

static const char *sound_b4_json_str(uint8_t b4) {
    switch (b4 & 3U) {
    case (uint8_t)SOUND_B4_BOT:
        return "BOT";
    case (uint8_t)SOUND_B4_FALSE:
        return "FALSE";
    case (uint8_t)SOUND_B4_TOP:
        return "TOP";
    default:
        return "TRUE";
    }
}

static bool sound_write_contract_spec_json_file(const sound_contract_spec_t *spec,
                                                const char *path) {
    if (spec == NULL || path == NULL) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    fprintf(fp, "{\n");
    fprintf(fp, "  \"seed\": \"0x%016llx\",\n", (unsigned long long)spec->seed);
    fprintf(fp, "  \"max_loss_cap\": %llu,\n", (unsigned long long)spec->max_loss_cap);
    fprintf(fp, "  \"max_exit_fee\": %llu,\n", (unsigned long long)spec->max_exit_fee);
    fprintf(fp, "  \"protected_mask\": %llu,\n", (unsigned long long)spec->protected_mask);
    fprintf(fp, "  \"max_asym_num\": %llu,\n", (unsigned long long)spec->max_asym_num);
    fprintf(fp, "  \"max_asym_den\": %llu,\n", (unsigned long long)spec->max_asym_den);
    fprintf(fp, "  \"coverage_gaps\": %u,\n", (unsigned)spec->coverage_gaps);
    fprintf(fp, "  \"clauses\": [\n");
    for (uint32_t i = 0U; i < spec->num_clauses; i++) {
        const sound_contract_clause_t *c = &spec->clauses[i];
        fprintf(fp,
                "    {\"clause_id\": %u, \"kind\": \"%s\", \"disclosed\": %s, "
                "\"b4_truth\": \"%s\", \"sheaf_cycle\": %s, \"target_mask\": %llu, "
                "\"base_loss\": %llu, \"extra_drift\": %llu, \"exit_fee\": %llu, "
                "\"cons_penalty\": %llu, \"inst_penalty\": %llu}%s\n",
                (unsigned)c->clause_id, sound_clause_kind_json_str(c->kind),
                (c->disclosed != 0U) ? "true" : "false", sound_b4_json_str(c->b4_truth),
                (c->sheaf_cycle != 0U) ? "true" : "false", (unsigned long long)c->target_mask,
                (unsigned long long)c->base_loss, (unsigned long long)c->extra_drift,
                (unsigned long long)c->exit_fee, (unsigned long long)c->cons_penalty,
                (unsigned long long)c->inst_penalty, (i + 1U < spec->num_clauses) ? "," : "");
    }
    fprintf(fp, "  ],\n");
    fprintf(fp, "  \"edges\": [\n");
    for (uint32_t i = 0U; i < spec->num_edges; i++) {
        const sound_contract_edge_t *e = &spec->edges[i];
        fprintf(fp,
                "    {\"src\": %u, \"dst\": %u, \"edge_kind\": \"%s\", "
                "\"cocycle_shift\": %u, \"extra_exposure\": %llu}%s\n",
                (unsigned)e->src, (unsigned)e->dst, sound_edge_kind_json_str(e->edge_kind),
                (unsigned)e->cocycle_shift, (unsigned long long)e->extra_exposure,
                (i + 1U < spec->num_edges) ? "," : "");
    }
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");
    int err = ferror(fp);
    int cls = fclose(fp);
    return (err == 0) && (cls == 0);
}

static int sound_cmd_audit_contract(int argc, char **argv) {
    const char *in_path = argv[2];
    const char *out_lean = sound_parse_opt(argc, argv, 3, "--out-lean");
    const char *out_fir = sound_parse_opt(argc, argv, 3, "--out-fir");
    const char *out_json = sound_parse_opt(argc, argv, 3, "--out-json");
    const char *out_cert = sound_parse_opt(argc, argv, 3, "--out-cert");
    const char *out_receipt = sound_parse_opt(argc, argv, 3, "--out-receipt");

    static sound_contract_spec_t spec;
    static sound_contract_audit_t audit;
    if (!sound_parse_contract_file(in_path, &spec)) {
        fprintf(stderr, "ERROR: unable to parse contract (.sndc/.json) '%s'\n", in_path);
        return 1;
    }
    sound_audit_contract(&spec, &audit);
    if (!sound_cert_emit_contract_fir(&audit, out_fir, &g_cert_ws)) {
        fprintf(stderr, "ERROR: contract FIR2 generation/eval failed\n");
        return 1;
    }
    if (out_lean != NULL && !sound_cert_emit_contract_lean(&spec, &audit, out_lean)) {
        fprintf(stderr, "ERROR: unable to write '%s'\n", out_lean);
        return 1;
    }
    if (out_json != NULL && !sound_cert_emit_contract_json(&spec, &audit, &g_cert_ws, out_json)) {
        fprintf(stderr, "ERROR: unable to write '%s'\n", out_json);
        return 1;
    }
    if (out_cert != NULL &&
        !sound_cert_emit_contract_cert_file(&spec, &audit, &g_cert_ws, out_cert)) {
        fprintf(stderr, "ERROR: unable to write '%s'\n", out_cert);
        return 1;
    }
    if (out_receipt != NULL && !sound_cert_emit_contract_receipt(&spec, &audit, out_receipt)) {
        fprintf(stderr, "ERROR: unable to write '%s'\n", out_receipt);
        return 1;
    }
    printf("[audit-contract] %s | verdict=%s viol=0x%x base=%llu edge_exp=%llu worst=%llu "
           "drift=%lld pred=%u mal_edges=%u sheaf_h1=%u bypass=0x%x rep_cert=%u fir2=%zuB\n",
           in_path, (audit.certified != UINT32_C(0)) ? "CERTIFIED" : "VETOED", audit.viol_flags,
           (unsigned long long)audit.base_loss_sum, (unsigned long long)audit.edge_exposure_sum,
           (unsigned long long)audit.worst_case_loss, (long long)audit.benefit_drift,
           audit.num_predatory_clauses, audit.num_malicious_edges, audit.graph_sheaf_h1_obstructed,
           audit.graph_bypass_mask, audit.repaired_certified, g_cert_ws.wire_len);
    return 0;
}

static int sound_cmd_pack_arena(const char *in_path, const char *out_path) {
    if (!sound_load_arena_file(in_path, &g_cli_arena)) {
        fprintf(stderr, "ERROR: unable to read SoundArena '%s'\n", in_path);
        return 1;
    }
    size_t packed_len = 0U;
    if (!sound_save_packed_arena_file(out_path, &g_cli_arena, &packed_len)) {
        fprintf(stderr, "ERROR: unable to pack FLWA '%s'\n", out_path);
        return 1;
    }
    printf("[pack-arena] %s (%uB raw) -> %s (%zuB FLWA)\n", in_path, SOUND_ARENA_BYTES, out_path,
           packed_len);
    return 0;
}

static int sound_cmd_unpack_arena(const char *in_path, const char *out_path) {
    if (!sound_load_arena_file(in_path, &g_cli_arena)) {
        fprintf(stderr, "ERROR: unable to read SoundArena '%s'\n", in_path);
        return 1;
    }
    if (!sound_save_raw_arena_file(out_path, &g_cli_arena)) {
        fprintf(stderr, "ERROR: unable to write raw SoundArena '%s'\n", out_path);
        return 1;
    }
    printf("[unpack-arena] %s -> %s (%uB raw)\n", in_path, out_path, SOUND_ARENA_BYTES);
    return 0;
}

static int sound_cmd_pack_fir(const char *in_path, const char *out_path) {
    static uint8_t buf[FLOW_IR_MAX_FILE_BYTES];
    static flow_ir_prog_t prog;
    size_t in_len = 0U;
    size_t out_len = 0U;
    if (!sound_read_bin_file_buf(in_path, buf, sizeof(buf), &in_len) ||
        !flow_ir_load_bytes(&prog, buf, in_len) ||
        !flow_ir_encode_packed_bytes(&prog, buf, sizeof(buf), &out_len) ||
        !sound_save_bin_file(out_path, buf, out_len)) {
        fprintf(stderr, "ERROR: pack-fir failed '%s' -> '%s'\n", in_path, out_path);
        return 1;
    }
    printf("[pack-fir] %s (%zuB) -> %s (%zuB FIR2)\n", in_path, in_len, out_path, out_len);
    return 0;
}

static int sound_cmd_unpack_fir(const char *in_path, const char *out_path) {
    static uint8_t buf[FLOW_IR_MAX_FILE_BYTES];
    static flow_ir_prog_t prog;
    size_t in_len = 0U;
    size_t out_len = 0U;
    if (!sound_read_bin_file_buf(in_path, buf, sizeof(buf), &in_len) ||
        !flow_ir_load_bytes(&prog, buf, in_len) ||
        !flow_ir_encode_bytes(&prog, buf, sizeof(buf), &out_len) ||
        !sound_save_bin_file(out_path, buf, out_len)) {
        fprintf(stderr, "ERROR: unpack-fir failed '%s' -> '%s'\n", in_path, out_path);
        return 1;
    }
    printf("[unpack-fir] %s (%zuB) -> %s (%zuB FIR1)\n", in_path, in_len, out_path, out_len);
    return 0;
}

static int sound_cmd_pack_contract(const char *in_path, const char *out_path) {
    static sound_contract_spec_t spec;
    static sound_contract_spec_t rt;
    static uint8_t buf[SOUND_CERT_MAX_BYTES];
    size_t out_len = 0U;
    if (!sound_parse_contract_file(in_path, &spec) ||
        !sound_cert_pack_contract_spec(&spec, buf, sizeof(buf), &out_len) ||
        !sound_cert_unpack_contract_spec(buf, out_len, &rt) ||
        !sound_cert_bytes_equal((const uint8_t *)&spec, (const uint8_t *)&rt, sizeof(spec)) ||
        !sound_save_bin_file(out_path, buf, out_len)) {
        fprintf(stderr, "ERROR: pack-contract failed '%s' -> '%s'\n", in_path, out_path);
        return 1;
    }
    printf("[pack-contract] %s -> %s (%zuB SNDC)\n", in_path, out_path, out_len);
    return 0;
}

static int sound_cmd_unpack_contract(const char *in_path, const char *out_path) {
    static uint8_t buf[SOUND_CERT_MAX_BYTES];
    static sound_contract_spec_t spec;
    size_t in_len = 0U;
    if (!sound_read_bin_file_buf(in_path, buf, sizeof(buf), &in_len) ||
        !sound_cert_unpack_contract_spec(buf, in_len, &spec) ||
        !sound_write_contract_spec_json_file(&spec, out_path)) {
        fprintf(stderr, "ERROR: unpack-contract failed '%s' -> '%s'\n", in_path, out_path);
        return 1;
    }
    printf("[unpack-contract] %s (%zuB SNDC) -> %s\n", in_path, in_len, out_path);
    return 0;
}

static int sound_cmd_verify_cert(const char *in_path) {
    static uint8_t buf[SOUND_CERT_MAX_BYTES];
    size_t in_len = 0U;
    uint8_t kind = 0U;
    uint8_t sub = 0U;
    if (!sound_read_bin_file_buf(in_path, buf, sizeof(buf), &in_len) ||
        !sound_cert_verify_bytes(buf, in_len, &kind, &sub)) {
        fprintf(stderr, "ERROR: SNDC cert verification failed on '%s'\n", in_path);
        return 1;
    }
    printf("[verify-cert] %s (%zuB SNDC, kind=%u, submode=%u) | integrity=OK\n", in_path, in_len,
           (unsigned)kind, (unsigned)sub);
    return 0;
}

static int sound_cmd_unpack_cert(const char *in_path, const char *out_json) {
    static uint8_t buf[SOUND_CERT_MAX_BYTES];
    size_t in_len = 0U;
    if (!sound_read_bin_file_buf(in_path, buf, sizeof(buf), &in_len)) {
        fprintf(stderr, "ERROR: unable to read '%s'\n", in_path);
        return 1;
    }
    FILE *fp = fopen(out_json, "wb");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: unable to open '%s'\n", out_json);
        return 1;
    }
    bool ok = sound_cert_unpack_to_json_fp(buf, in_len, fp);
    int cls = fclose(fp);
    if (!ok || cls != 0) {
        fprintf(stderr, "ERROR: unpack-cert failed on '%s'\n", in_path);
        return 1;
    }
    printf("[unpack-cert] %s (%zuB SNDC) -> %s\n", in_path, in_len, out_json);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage:\n  sound selftest\n  sound verify <arena.flwa|arena.snd> [--json]\n"
                        "  sound repair <arena.flwa|arena.snd> [seed]\n"
                        "  sound lift-fir <arena.flwa|arena.snd> <out.fir2> [--repaired [seed]] "
                        "[--raw-fir]\n"
                        "  sound lift-policy-fir <arena.flwa|arena.snd> <out.fir2> [--raw-fir]\n"
                        "  sound pack-arena <in.snd|in.flwa> <out.flwa>\n"
                        "  sound unpack-arena <in.flwa|in.snd> <out.snd>\n"
                        "  sound pack-fir <in.fir|in.fir2> <out.fir2>\n"
                        "  sound unpack-fir <in.fir2|in.fir> <out.fir>\n"
                        "  sound pack-contract <in.json|in.sndc> <out.sndc>\n"
                        "  sound unpack-contract <in.sndc> <out.json>\n"
                        "  sound verify-cert <cert.bin>\n"
                        "  sound unpack-cert <cert.bin> <out.json>\n"
                        "  sound classify-<bilattice|sheaf|hohfeld|projection> [--out-json <p>]\n"
                        "  sound hunt <paradox-min|sheaf-h1|chisholm|norm-trap|aspect-firewall|"
                        "presup-filter|contract-drift|contract-loophole> [--seed <u64>] "
                        "[--out-lean <p>] [--out-fir <p>] [--out-json <p>] [--out-cert <p>]\n"
                        "  sound audit-contract <contract.sndc|contract.json> [--out-lean <p>] "
                        "[--out-fir <p>] [--out-json <p>] [--out-cert <p>] [--out-receipt <p>]\n");
        return 1;
    }
    if (sound_cstr_eq(argv[1], "selftest")) {
        return sound_cmd_selftest();
    }
    if (sound_cstr_eq(argv[1], "verify") && argc >= 3) {
        bool json_out = (argc >= 4 && sound_cstr_eq(argv[3], "--json"));
        return sound_cmd_verify(argv[2], json_out);
    }
    if (sound_cstr_eq(argv[1], "repair") && argc >= 3) {
        return sound_cmd_repair(argv[2], (argc >= 4) ? argv[3] : NULL);
    }
    if (sound_cstr_eq(argv[1], "lift-fir") && argc >= 4) {
        return sound_cmd_lift_fir(argc, argv);
    }
    if (sound_cstr_eq(argv[1], "lift-policy-fir") && argc >= 4) {
        return sound_cmd_lift_policy_fir(argc, argv);
    }
    if (sound_cstr_eq(argv[1], "pack-arena") && argc == 4) {
        return sound_cmd_pack_arena(argv[2], argv[3]);
    }
    if (sound_cstr_eq(argv[1], "unpack-arena") && argc == 4) {
        return sound_cmd_unpack_arena(argv[2], argv[3]);
    }
    if (sound_cstr_eq(argv[1], "pack-fir") && argc == 4) {
        return sound_cmd_pack_fir(argv[2], argv[3]);
    }
    if (sound_cstr_eq(argv[1], "unpack-fir") && argc == 4) {
        return sound_cmd_unpack_fir(argv[2], argv[3]);
    }
    if (sound_cstr_eq(argv[1], "pack-contract") && argc == 4) {
        return sound_cmd_pack_contract(argv[2], argv[3]);
    }
    if (sound_cstr_eq(argv[1], "unpack-contract") && argc == 4) {
        return sound_cmd_unpack_contract(argv[2], argv[3]);
    }
    if (sound_cstr_eq(argv[1], "verify-cert") && argc == 3) {
        return sound_cmd_verify_cert(argv[2]);
    }
    if (sound_cstr_eq(argv[1], "unpack-cert") && argc == 4) {
        return sound_cmd_unpack_cert(argv[2], argv[3]);
    }
    if (sound_cstr_eq(argv[1], "classify-bilattice")) {
        return sound_cmd_classify_bilattice(sound_parse_opt(argc, argv, 2, "--out-json"));
    }
    if (sound_cstr_eq(argv[1], "classify-sheaf")) {
        return sound_cmd_classify_sheaf(sound_parse_opt(argc, argv, 2, "--out-json"));
    }
    if (sound_cstr_eq(argv[1], "classify-hohfeld")) {
        return sound_cmd_classify_hohfeld(sound_parse_opt(argc, argv, 2, "--out-json"));
    }
    if (sound_cstr_eq(argv[1], "classify-projection")) {
        return sound_cmd_classify_projection(sound_parse_opt(argc, argv, 2, "--out-json"));
    }
    if (sound_cstr_eq(argv[1], "hunt") && argc >= 3) {
        return sound_cmd_hunt(argc, argv);
    }
    if (sound_cstr_eq(argv[1], "audit-contract") && argc >= 3) {
        return sound_cmd_audit_contract(argc, argv);
    }
    fprintf(stderr, "ERROR: invalid arguments\n");
    return 1;
}
