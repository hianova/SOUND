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
    if (sound_eval(&arena) != SOUND_OK || !sound_eval_flags_ok(&arena) ||
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
    if (sound_eval(&arena) != SOUND_OK || !sound_eval_flags_ok(&arena) ||
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
    if (sound_eval(&arena) != SOUND_OK || !sound_eval_flags_ok(&arena) ||
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
    if (sound_eval(&arena) != SOUND_OK || !sound_eval_flags_ok(&arena) ||
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
    if (sound_eval(&arena) != SOUND_OK || !sound_eval_flags_ok(&arena) ||
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

    if (sound_apply_potestative(&arena, 3U, false, true, true, false) !=
            SOUND_ERR_IMMUNITY_LOCKED ||
        arena.map.norm.duty_pos_mask != UINT64_C(0x8) || !sound_hohfeld_coherent(&arena)) {
        fails++;
    }
    if (sound_apply_potestative(&arena, 1U, true, false, false, false) != SOUND_ERR_NO_POWER ||
        arena.map.norm.duty_pos_mask != UINT64_C(0x8)) {
        fails++;
    }
    if (sound_apply_declarative(&arena, 0U, 2U, false, false, UINT64_C(0x1111)) !=
            SOUND_ERR_MISSING_CONTEXT ||
        arena.map.img_hdr.phys_len != 1U) {
        fails++;
    }
    if (sound_apply_declarative(&arena, 0U, 2U, true, false, UINT64_C(0x2222)) != SOUND_OK ||
        arena.map.img_hdr.phys_len != 2U || arena.map.norm.duty_pos_mask != UINT64_C(0xC) ||
        arena.map.norm.voidable_mask != UINT64_C(0x4) ||
        arena.map.norm.rescission_power_mask != UINT64_C(0x4)) {
        fails++;
    }
    if (sound_exercise_rescission(&arena, 0U, 2U, UINT64_C(0x3333)) != SOUND_OK ||
        arena.map.img_hdr.phys_len != 3U || arena.map.eval_out.established_mask != UINT64_C(0x8) ||
        arena.map.eval_out.defeated_mask != UINT64_C(0x7) ||
        arena.map.norm.duty_pos_mask != UINT64_C(0x8) ||
        arena.map.norm.voidable_mask != UINT64_C(0x0) ||
        arena.map.norm.rescission_power_mask != UINT64_C(0x0)) {
        fails++;
    }
    if (sound_apply_potestative(&arena, 2U, true, false, false, false) != SOUND_OK ||
        sound_apply_potestative(&arena, 2U, false, false, true, false) != SOUND_ERR_INCOHERENT ||
        !sound_hohfeld_coherent(&arena)) {
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
    if (sound_admit_forest(&arena, &admitted) != SOUND_FOREST_INVALID_SPAN) {
        fails++;
    }

    arena.map.forest_ctrl.utterance_bytes = 24U;
    arena.map.forest_ctrl.ambiguity_flags = 1U;
    arena.map.candidate[0].span_end = 10U;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FOREST_NEEDS_CLARIFICATION) {
        fails++;
    }

    arena.map.forest_ctrl.ambiguity_flags = 0U;
    arena.map.forest_ctrl.unresolved_anaphora = 1U;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FOREST_NEEDS_CLARIFICATION) {
        fails++;
    }

    arena.map.forest_ctrl.unresolved_anaphora = 0U;
    arena.map.forest_ctrl.num_candidates = 2U;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FOREST_NEEDS_CLARIFICATION) {
        fails++;
    }

    arena.map.forest_ctrl.num_candidates = 1U;
    arena.map.candidate[0].force = (uint8_t)SOUND_FORCE_IRREALIS;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FOREST_IRREALIS_QUARANTINED) {
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
    if (sound_admit_forest(&arena, &admitted) != SOUND_FOREST_ADMITTED || admitted != 0U ||
        arena.map.node_meta[0].attack_mask != UINT64_C(0x2) || arena.map.img_hdr.phys_len != 1U) {
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
    if (sound_arena_verify(&dst) != SOUND_OK || sound_eval(&dst) != SOUND_OK ||
        dst.map.norm.immunity_mask != UINT64_C(0x4) ||
        dst.map.norm.voidable_mask != UINT64_C(0x2) ||
        dst.map.norm.rescission_power_mask != UINT64_C(0x2) ||
        dst.map.eval_out.established_mask != UINT64_C(0x7)) {
        fails++;
    }

    /* Bit flip in Arena tail must trigger SOUND_ERR_MAC */
    dst.cells[1].bytes[0] ^= UINT8_C(0x01);
    if (sound_arena_verify(&dst) != SOUND_ERR_MAC) {
        fails++;
    }
    dst.cells[1].bytes[0] ^= UINT8_C(0x01);

    /* Bad magic and schema_hash must trigger SOUND_ERR_FORMAT */
    dst.map.img_hdr.magic ^= UINT32_C(1);
    if (sound_arena_verify(&dst) != SOUND_ERR_FORMAT) {
        fails++;
    }
    dst.map.img_hdr.magic = SOUND_MAGIC;
    dst.map.img_hdr.schema_hash ^= UINT64_C(1);
    if (sound_arena_verify(&dst) != SOUND_ERR_FORMAT) {
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
    if (sound_searu_repair_conflict(&arena, UINT64_C(0x53454152555F3031), 1024U) != SOUND_OK ||
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
    if (sound_searu_repair_conflict(&arena, UINT64_C(0x53454152555F3032), 1024U) != SOUND_OK ||
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
    if (sound_searu_repair_conflict(&arena, UINT64_C(0x53454152555F3033), 1024U) != SOUND_OK ||
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

        if (sound_eval(&arena) != SOUND_OK || !sound_eval_flags_ok(&arena)) {
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
    if (sound_eval(&arena) != SOUND_OK ||
        sound_lift_to_fir(&arena, "case3_park", UINT32_C(0), fir_buf, sizeof(fir_buf), &fir_len) !=
            SOUND_OK ||
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
    if (sound_searu_repair_conflict(&arena, UINT64_C(0x53454152555F3031), 1024U) != SOUND_OK ||
        sound_lift_to_fir(&arena, "nixon_repaired", SOUND_FIR_FLAG_USE_REPAIRED, fir_buf,
                          sizeof(fir_buf), &fir_len) != SOUND_OK ||
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
    uint32_t tot = f1 + f2 + f3 + f4 + f5 + f6 + f7;
    printf(
        "[selftest] T1(kernel)=%s T2(hohfeld)=%s T3(gate)=%s T4(arena)=%s T5(searu)=%s T6(fuzz4096)=%s T7(fir)=%s | fails=%u\n",
        f1 == 0U ? "PASS" : "FAIL", f2 == 0U ? "PASS" : "FAIL", f3 == 0U ? "PASS" : "FAIL",
        f4 == 0U ? "PASS" : "FAIL", f5 == 0U ? "PASS" : "FAIL", f6 == 0U ? "PASS" : "FAIL",
        f7 == 0U ? "PASS" : "FAIL", tot);
    return (tot == 0U) ? 0 : 1;
}

static int sound_cmd_verify(const char *path) {
    if (!sound_load_arena_file(path, &g_cli_arena)) {
        fprintf(stderr, "ERROR: unable to read 16,384B SoundArena '%s'\n", path);
        return 1;
    }
    sound_err_t rc = sound_arena_verify(&g_cli_arena);
    if (rc != SOUND_OK) {
        fprintf(stderr, "ERROR: SoundArena verify failed on '%s' (err=%u)\n", path, (unsigned)rc);
        return 1;
    }
    rc = sound_eval(&g_cli_arena);
    if (rc != SOUND_OK || !sound_eval_flags_ok(&g_cli_arena)) {
        fprintf(stderr, "ERROR: Kernel invariant violation on '%s'\n", path);
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
    sound_err_t rc = sound_arena_verify(&g_cli_arena);
    if (rc != SOUND_OK) {
        fprintf(stderr, "ERROR: SoundArena verify failed on '%s' (err=%u)\n", path, (unsigned)rc);
        return 1;
    }
    rc = sound_searu_repair_conflict(&g_cli_arena, seed, 2048U);
    if (rc != SOUND_OK) {
        fprintf(stderr, "ERROR: SEARU repair failed on '%s' (err=%u)\n", path, (unsigned)rc);
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
    sound_err_t rc = sound_arena_verify(&g_cli_arena);
    if (rc != SOUND_OK) {
        fprintf(stderr, "ERROR: SoundArena verify failed on '%s' (err=%u)\n", in_path,
                (unsigned)rc);
        return 1;
    }
    rc = use_repaired ? sound_searu_repair_conflict(&g_cli_arena, seed, 2048U)
                      : sound_eval(&g_cli_arena);
    if (rc != SOUND_OK || !sound_eval_flags_ok(&g_cli_arena)) {
        fprintf(stderr, "ERROR: Kernel invariant violation on '%s'\n", in_path);
        return 1;
    }

    char prog_name[24];
    sound_derive_prog_name(prog_name, in_path);
    uint8_t fir_buf[SOUND_FIR_BYTES];
    size_t fir_len = 0U;
    uint32_t fir_flags = use_repaired ? SOUND_FIR_FLAG_USE_REPAIRED : UINT32_C(0);
    rc = sound_lift_to_fir(&g_cli_arena, prog_name, fir_flags, fir_buf, sizeof(fir_buf), &fir_len);
    if (rc != SOUND_OK) {
        fprintf(stderr, "ERROR: lift-fir failed on '%s' (err=%u)\n", in_path, (unsigned)rc);
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

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage:\n  sound selftest\n  sound verify <arena.snd>\n"
                        "  sound repair <arena.snd> [seed]\n"
                        "  sound lift-fir <arena.snd> <out.fir> [--repaired [seed]]\n");
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
    fprintf(stderr, "ERROR: invalid arguments\n");
    return 1;
}
