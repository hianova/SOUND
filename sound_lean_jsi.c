#define _POSIX_C_SOURCE 200809L

#include <lean/lean.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sound.h"

/*
 * ============================================================================
 * SOUND v2.0.0 — Zero-Copy Lean 4 Dynamic Plugin JSI Bridge (0 `lean -c`)
 * ============================================================================
 * Linked ONLY into `build/libsound_jsi.{dylib,so}` for elaboration-time
 * `#eval Sound.compileTimeBoundaryGate`. Never linked into `libsound.a` or
 * the standalone `build/sound` CLI.
 * ============================================================================
 */

LEAN_EXPORT lean_obj_res sound_jsi_verify_arena_bytes(b_lean_obj_arg raw, uint64_t exp_nm,
                                                      uint64_t exp_i, uint64_t exp_p,
                                                      uint64_t exp_est, uint64_t exp_def,
                                                      uint64_t exp_conf, uint64_t exp_pen);
LEAN_EXPORT lean_obj_res sound_jsi_verify_searu_repair(b_lean_obj_arg raw, uint64_t seed);
LEAN_EXPORT lean_obj_res sound_jsi_verify_core_suites(uint64_t seed);
LEAN_EXPORT lean_obj_res l_Sound_verifyArenaBytes___boxed(lean_obj_arg raw, lean_obj_arg exp_nm,
                                                          lean_obj_arg exp_i, lean_obj_arg exp_p,
                                                          lean_obj_arg exp_est,
                                                          lean_obj_arg exp_def,
                                                          lean_obj_arg exp_conf,
                                                          lean_obj_arg exp_pen, lean_obj_arg world);
LEAN_EXPORT lean_obj_res l_Sound_verifySearuRepair___boxed(lean_obj_arg raw, lean_obj_arg seed,
                                                           lean_obj_arg world);
LEAN_EXPORT lean_obj_res l_Sound_verifyCoreSuites___boxed(lean_obj_arg seed, lean_obj_arg world);

typedef struct {
    uint64_t s;
} sound_jsi_rng_t;

static uint64_t sound_jsi_splitmix64(sound_jsi_rng_t *rng) {
    uint64_t z = (rng->s += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30U)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27U)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31U);
}

static bool sound_jsi_load_arena(b_lean_obj_arg raw, SoundArena *out) {
    if (out == NULL || lean_sarray_size(raw) != SOUND_ARENA_BYTES) {
        return false;
    }
    const uint8_t *src = lean_sarray_cptr((lean_object *)raw);
    if (src == NULL) {
        return false;
    }
    for (size_t i = 0U; i < SOUND_ARENA_CELLS; i++) {
        flow_cell_from_bytes(out->cells[i].u64, src + (i * 64U));
    }
    return true;
}

static void sound_jsi_set_node(SoundArena *arena, uint32_t id, const char *label, uint8_t sort,
                               uint8_t evid, uint8_t aspect, bool is_const, bool revoked,
                               bool penumbra, uint64_t presup_mask, uint64_t attack_mask,
                               uint8_t num_clauses, uint64_t c0, uint64_t c1) {
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
    size_t i = 0U;
    if (label != NULL) {
        while ((i + 1U) < SOUND_LABEL_BYTES && label[i] != '\0') {
            nd->label_bytes[i] = (uint8_t)label[i];
            i++;
        }
    }
    while (i < SOUND_LABEL_BYTES) {
        nd->label_bytes[i] = 0U;
        i++;
    }
    if (revoked) {
        arena->map.norm.revoked_mask |= sound_bit64(id);
    }
    if (penumbra) {
        arena->map.norm.penumbra_mask |= sound_bit64(id);
    }
}

LEAN_EXPORT lean_obj_res sound_jsi_verify_arena_bytes(b_lean_obj_arg raw, uint64_t exp_nm,
                                                      uint64_t exp_i, uint64_t exp_p,
                                                      uint64_t exp_est, uint64_t exp_def,
                                                      uint64_t exp_conf, uint64_t exp_pen) {
    SoundArena arena;
    if (!sound_jsi_load_arena(raw, &arena)) {
        return lean_io_result_mk_ok(lean_box(0));
    }
    if (sound_arena_verify(&arena) != SOUND_OK || sound_eval(&arena) != SOUND_OK) {
        return lean_io_result_mk_ok(lean_box(0));
    }
    uint32_t req_flags = SOUND_EVAL_FLAG_PARTITION_OK | SOUND_EVAL_FLAG_CONFLICT_FREE_OK |
                         SOUND_EVAL_FLAG_HOHFELD_OK | SOUND_EVAL_FLAG_FIXPOINT_OK;
    if ((arena.map.eval_out.eval_flags & req_flags) != req_flags) {
        return lean_io_result_mk_ok(lean_box(0));
    }
    bool match = (arena.map.eval_out.node_mask == exp_nm) && (arena.map.eval_out.i_star == exp_i) &&
                 (arena.map.eval_out.p_star == exp_p) &&
                 (arena.map.eval_out.established_mask == exp_est) &&
                 (arena.map.eval_out.defeated_mask == exp_def) &&
                 (arena.map.eval_out.conflict_mask == exp_conf) &&
                 (arena.map.eval_out.penumbra_mask == exp_pen);
    return lean_io_result_mk_ok(lean_box(match ? 1 : 0));
}

LEAN_EXPORT lean_obj_res sound_jsi_verify_searu_repair(b_lean_obj_arg raw, uint64_t seed) {
    SoundArena arena;
    if (!sound_jsi_load_arena(raw, &arena)) {
        return lean_io_result_mk_ok(lean_box(0));
    }
    if (sound_arena_verify(&arena) != SOUND_OK || sound_eval(&arena) != SOUND_OK) {
        return lean_io_result_mk_ok(lean_box(0));
    }
    uint32_t n = arena.map.img_hdr.num_nodes;
    uint64_t nm = arena.map.eval_out.node_mask;
    uint64_t est = arena.map.eval_out.established_mask;
    uint64_t conf = arena.map.eval_out.conflict_mask;
    uint64_t imm = arena.map.norm.immunity_mask & nm;
    uint64_t factual_mask = sound_arena_factual_mask(&arena);

    if (sound_searu_repair_conflict(&arena, seed, 2048U) != SOUND_OK) {
        return lean_io_result_mk_ok(lean_box(0));
    }
    const SoundSearuRepair64 *rep = &arena.map.searu_repair;
    if (rep->best_energy != UINT64_C(0) || rep->converged != UINT32_C(1)) {
        return lean_io_result_mk_ok(lean_box(0));
    }
    uint64_t s = rep->candidate_mask;
    if ((s & est) != est || (s & (imm & (est | conf))) != (imm & (est | conf))) {
        return lean_io_result_mk_ok(lean_box(0));
    }
    if (conf != UINT64_C(0) &&
        (rep->retracted_mask == UINT64_C(0) || (s & rep->retracted_mask) != UINT64_C(0) ||
         (s | rep->retracted_mask) != (est | conf))) {
        return lean_io_result_mk_ok(lean_box(0));
    }

    uint64_t incoming_to_s = UINT64_C(0);
    for (uint32_t v = 0U; v < n; v++) {
        if ((s & sound_bit64(v)) != UINT64_C(0)) {
            incoming_to_s |= arena.map.node_meta[v].attack_mask;
        }
    }
    for (uint32_t u = 0U; u < n; u++) {
        uint64_t ub = sound_bit64(u);
        if ((s & ub) != UINT64_C(0)) {
            if ((arena.map.node_meta[u].attack_mask & s) != UINT64_C(0) ||
                !sound_node_fires(&arena, u, s, s, nm, factual_mask)) {
                return lean_io_result_mk_ok(lean_box(0));
            }
        } else if ((conf & ub) != UINT64_C(0)) {
            if ((incoming_to_s & ub) == UINT64_C(0) &&
                sound_node_fires(&arena, u, s, s, nm, factual_mask)) {
                return lean_io_result_mk_ok(lean_box(0));
            }
        }
    }
    return lean_io_result_mk_ok(lean_box(1));
}

LEAN_EXPORT lean_obj_res sound_jsi_verify_core_suites(uint64_t seed) {
    SoundArena arena;

    /* 1. Layer 1: Hohfeld KanLock, PhysLog, Declarative, Rescission & Multi-Support */
    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 4U;
    arena.map.norm.power_mask = UINT64_C(0xC);
    arena.map.norm.immunity_mask = UINT64_C(0x8);
    arena.map.norm.duty_pos_mask = UINT64_C(0x8);
    sound_jsi_set_node(&arena, 0U, "alice_bid", SOUND_SORT_FACTUAL, SOUND_EVID_DIRECT,
                       SOUND_ASPECT_ACHIEVEMENT, false, false, false, UINT64_C(0), UINT64_C(0), 1U,
                       UINT64_C(0), UINT64_C(0));
    sound_jsi_set_node(&arena, 1U, "contract_formed", SOUND_SORT_INSTITUTIONAL, SOUND_EVID_DIRECT,
                       SOUND_ASPECT_ACHIEVEMENT, true, false, false, UINT64_C(0), UINT64_C(0), 1U,
                       UINT64_C(1), UINT64_C(0));
    sound_jsi_set_node(&arena, 2U, "alice_pay_duty", SOUND_SORT_NORMATIVE, SOUND_EVID_DIRECT,
                       SOUND_ASPECT_STATE, true, false, false, UINT64_C(0), UINT64_C(0), 1U,
                       UINT64_C(2), UINT64_C(0));
    sound_jsi_set_node(&arena, 3U, "venue_cleanup", SOUND_SORT_NORMATIVE, SOUND_EVID_ENACTED_NORM,
                       SOUND_ASPECT_STATE, false, false, false, UINT64_C(0), UINT64_C(0), 2U,
                       UINT64_C(2), UINT64_C(0));
    arena.map.node_meta[0].span_start = 2U;
    arena.map.node_meta[0].span_end = 10U;

    if (sound_apply_potestative(&arena, 3U, false, true, true, false) !=
            SOUND_ERR_IMMUNITY_LOCKED ||
        sound_apply_potestative(&arena, 1U, true, false, false, false) != SOUND_ERR_NO_POWER ||
        sound_apply_declarative(&arena, 0U, 2U, false, false, UINT64_C(0x1111)) !=
            SOUND_ERR_MISSING_CONTEXT ||
        arena.map.img_hdr.phys_len != 1U ||
        sound_apply_declarative(&arena, 0U, 2U, true, false, UINT64_C(0x2222)) != SOUND_OK ||
        arena.map.img_hdr.phys_len != 2U || arena.map.norm.duty_pos_mask != UINT64_C(0xC) ||
        arena.map.norm.voidable_mask != UINT64_C(0x4) ||
        arena.map.norm.rescission_power_mask != UINT64_C(0x4) ||
        sound_exercise_rescission(&arena, 0U, 2U, UINT64_C(0x3333)) != SOUND_OK ||
        arena.map.img_hdr.phys_len != 3U || arena.map.eval_out.established_mask != UINT64_C(0x8) ||
        arena.map.eval_out.defeated_mask != UINT64_C(0x7) ||
        arena.map.norm.duty_pos_mask != UINT64_C(0x8) ||
        arena.map.norm.voidable_mask != UINT64_C(0x0) ||
        arena.map.norm.rescission_power_mask != UINT64_C(0x0) ||
        sound_apply_potestative(&arena, 2U, true, false, false, false) != SOUND_OK ||
        sound_apply_potestative(&arena, 2U, false, false, true, false) != SOUND_ERR_INCOHERENT ||
        !sound_hohfeld_coherent(&arena)) {
        return lean_io_result_mk_ok(lean_box(0));
    }

    /* 2. Layer 0: Zero-Trust Candidate Forest Gate */
    uint32_t admitted = UINT32_MAX;
    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 2U;
    arena.map.forest_ctrl.utterance_bytes = 20U;
    arena.map.forest_ctrl.num_candidates = 1U;
    arena.map.candidate[0].span_start = 4U;
    arena.map.candidate[0].span_end = 25U;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FOREST_INVALID_SPAN) {
        return lean_io_result_mk_ok(lean_box(0));
    }

    arena.map.forest_ctrl.ambiguity_flags = 1U;
    arena.map.candidate[0].span_end = 12U;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FOREST_NEEDS_CLARIFICATION) {
        return lean_io_result_mk_ok(lean_box(0));
    }

    arena.map.forest_ctrl.ambiguity_flags = 0U;
    arena.map.candidate[0].force = (uint8_t)SOUND_FORCE_IRREALIS;
    if (sound_admit_forest(&arena, &admitted) != SOUND_FOREST_IRREALIS_QUARANTINED) {
        return lean_io_result_mk_ok(lean_box(0));
    }

    arena.map.candidate[0].target_node = 0U;
    arena.map.candidate[0].force = (uint8_t)SOUND_FORCE_ASSERTION;
    arena.map.candidate[0].sort = (uint8_t)SOUND_SORT_FACTUAL;
    arena.map.candidate[0].evid = (uint8_t)SOUND_EVID_DIRECT;
    arena.map.candidate[0].aspect = (uint8_t)SOUND_ASPECT_ACHIEVEMENT;
    arena.map.candidate[0].attack_mask = UINT64_C(0x2);
    arena.map.candidate[0].payload_hash = UINT64_C(0xCAFE);
    if (sound_admit_forest(&arena, &admitted) != SOUND_FOREST_ADMITTED || admitted != 0U ||
        arena.map.node_meta[0].attack_mask != UINT64_C(0x2) ||
        arena.map.node_meta[0].span_start != 4U || arena.map.node_meta[0].span_end != 12U ||
        arena.map.img_hdr.phys_len != 1U ||
        arena.map.phys_log[0].kind != (uint32_t)SOUND_PHYS_FOREST_ADMIT) {
        return lean_io_result_mk_ok(lean_box(0));
    }

    /* 3. Sealed 16KB Arena Merkle + Tamper Detection */
    sound_arena_seal(&arena);
    if (sound_arena_verify(&arena) != SOUND_OK) {
        return lean_io_result_mk_ok(lean_box(0));
    }
    arena.cells[1].bytes[0] ^= UINT8_C(0x01);
    if (sound_arena_verify(&arena) != SOUND_ERR_MAC) {
        return lean_io_result_mk_ok(lean_box(0));
    }
    arena.cells[1].bytes[0] ^= UINT8_C(0x01);
    if (sound_arena_verify(&arena) != SOUND_OK) {
        return lean_io_result_mk_ok(lean_box(0));
    }
    arena.map.img_hdr.schema_hash ^= UINT64_C(1);
    if (sound_arena_verify(&arena) != SOUND_ERR_FORMAT) {
        return lean_io_result_mk_ok(lean_box(0));
    }

    /* 4. 4,096-Graph Fuzz Invariant Gate */
    sound_jsi_rng_t rng = {seed | UINT64_C(1)};
    for (uint32_t g = 0U; g < 4096U; g++) {
        uint32_t n = (uint32_t)((sound_jsi_splitmix64(&rng) % 12U) + 1U);
        uint64_t nm = sound_mask_n(n);
        sound_arena_init(&arena);
        arena.map.img_hdr.num_nodes = n;
        arena.map.norm.revoked_mask = sound_jsi_splitmix64(&rng) & nm & UINT64_C(0x555);
        arena.map.norm.penumbra_mask = sound_jsi_splitmix64(&rng) & nm & UINT64_C(0x222);

        bool no_norm_source = true;
        for (uint32_t u = 0U; u < n; u++) {
            uint8_t sort = (uint8_t)(sound_jsi_splitmix64(&rng) % 3U);
            uint8_t evid = (uint8_t)(sound_jsi_splitmix64(&rng) % 6U);
            bool ic = (sound_jsi_splitmix64(&rng) & UINT64_C(7)) == UINT64_C(0);
            if (evid == (uint8_t)SOUND_EVID_ENACTED_NORM || ic) {
                no_norm_source = false;
            }
            uint64_t att = sound_jsi_splitmix64(&rng) & nm & (~sound_bit64(u));
            uint64_t pre = sound_jsi_splitmix64(&rng) & sound_jsi_splitmix64(&rng) & nm;
            uint8_t nc = (uint8_t)((sound_jsi_splitmix64(&rng) % 3U) + 1U);
            uint64_t c0 = (sound_jsi_splitmix64(&rng) & 1U) != 0U
                              ? UINT64_C(0)
                              : (sound_jsi_splitmix64(&rng) & sound_jsi_splitmix64(&rng) & nm);
            uint64_t c1 = sound_jsi_splitmix64(&rng) & sound_jsi_splitmix64(&rng) & nm;
            sound_jsi_set_node(&arena, u, "rnd", sort, evid, SOUND_ASPECT_STATE, ic, false, false,
                               pre, att, nc, c0, c1);
            if (nc > 2U) {
                arena.map.node_clauses[u].clauses[2] = sound_jsi_splitmix64(&rng) & nm;
            }
        }
        uint32_t req_flags = SOUND_EVAL_FLAG_PARTITION_OK | SOUND_EVAL_FLAG_CONFLICT_FREE_OK |
                             SOUND_EVAL_FLAG_HOHFELD_OK | SOUND_EVAL_FLAG_FIXPOINT_OK;
        if (sound_eval(&arena) != SOUND_OK ||
            (arena.map.eval_out.eval_flags & req_flags) != req_flags ||
            (arena.map.eval_out.established_mask & arena.map.norm.revoked_mask) != UINT64_C(0) ||
            (arena.map.eval_out.i_star & arena.map.norm.penumbra_mask) != UINT64_C(0) ||
            (arena.map.eval_out.p_star & arena.map.norm.penumbra_mask) != UINT64_C(0)) {
            return lean_io_result_mk_ok(lean_box(0));
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
                        return lean_io_result_mk_ok(lean_box(0));
                    }
                }
                if (no_norm_source && arena.map.node_meta[u].sort != (uint8_t)SOUND_SORT_FACTUAL) {
                    return lean_io_result_mk_ok(lean_box(0));
                }
            }
        }
    }
    return lean_io_result_mk_ok(lean_box(1));
}

LEAN_EXPORT lean_obj_res
l_Sound_verifyArenaBytes___boxed(lean_obj_arg raw, lean_obj_arg exp_nm, lean_obj_arg exp_i,
                                 lean_obj_arg exp_p, lean_obj_arg exp_est, lean_obj_arg exp_def,
                                 lean_obj_arg exp_conf, lean_obj_arg exp_pen, lean_obj_arg world) {
    (void)world;
    uint64_t u_nm = lean_unbox_uint64(exp_nm);
    uint64_t u_i = lean_unbox_uint64(exp_i);
    uint64_t u_p = lean_unbox_uint64(exp_p);
    uint64_t u_est = lean_unbox_uint64(exp_est);
    uint64_t u_def = lean_unbox_uint64(exp_def);
    uint64_t u_conf = lean_unbox_uint64(exp_conf);
    uint64_t u_pen = lean_unbox_uint64(exp_pen);
    lean_obj_res r = sound_jsi_verify_arena_bytes(raw, u_nm, u_i, u_p, u_est, u_def, u_conf, u_pen);
    lean_dec(raw);
    lean_dec(exp_nm);
    lean_dec(exp_i);
    lean_dec(exp_p);
    lean_dec(exp_est);
    lean_dec(exp_def);
    lean_dec(exp_conf);
    lean_dec(exp_pen);
    return r;
}

LEAN_EXPORT lean_obj_res l_Sound_verifySearuRepair___boxed(lean_obj_arg raw, lean_obj_arg seed,
                                                           lean_obj_arg world) {
    (void)world;
    uint64_t useed = lean_unbox_uint64(seed);
    lean_obj_res r = sound_jsi_verify_searu_repair(raw, useed);
    lean_dec(raw);
    lean_dec(seed);
    return r;
}

LEAN_EXPORT lean_obj_res l_Sound_verifyCoreSuites___boxed(lean_obj_arg seed, lean_obj_arg world) {
    (void)world;
    uint64_t useed = lean_unbox_uint64(seed);
    lean_dec(seed);
    return sound_jsi_verify_core_suites(useed);
}
