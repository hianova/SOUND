#ifndef SOUND_H
#define SOUND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flow.h"
#include "searu.h"
#include "flow_ir_wire.h"
#include "sound_schema_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * SOUND v3.0.0 — Pure Epistemic, Normative & Linguistic Theory Engine (Ring-0)
 * ============================================================================
 * Pure-GPR C17 implementation over sealed 16,384B (256 x 64B) `SoundArena`:
 *   - Zero heap allocation, zero floating-point, zero indirect calls (`blr`/`br`),
 *     zero integer division (`udiv`/`sdiv`), zero jump tables, `-mgeneral-regs-only`
 *     and freestanding Ring-0 clean.
 *   - Pillar 1: 4-Valued Belnap Bilattice, Alternating Fixpoint & Odd-Cycle No-Go
 *   - Pillar 2: Discrete Cech Sheaf Cohomology (H^1) & 64-Bit Gluing Obstructions
 *   - Pillar 3: Complete 8-Position Hohfeld V4 Group, Kinetic Freeze & Chisholm
 *   - Pillar 4: Vendler Aktionsart, 4-Valued Presupposition & Force Endofunctors
 * ============================================================================ */

#define SOUND_MAGIC                      UINT32_C(0x32444E53) /* "SND2" */
#define SOUND_VERSION                    UINT32_C(2)
#define SOUND_THEORY_VERSION_STR         "3.0.0"
#define SOUND_ARENA_CELLS                256U
#define SOUND_ARENA_BYTES                16384U
#define SOUND_MAX_CLAUSES                8U
#define SOUND_LABEL_BYTES                32U
#define SOUND_NODE_FLAG_CONSTITUTIVE     UINT32_C(1)
#define SOUND_FNV1A_OFFSET               UINT64_C(0xCBF29CE484222325)
#define SOUND_FNV1A_PRIME                UINT64_C(0x00000100000001B3)
#define SOUND_EVAL_FLAG_PARTITION_OK     (UINT32_C(1) << 0U)
#define SOUND_EVAL_FLAG_CONFLICT_FREE_OK (UINT32_C(1) << 1U)
#define SOUND_EVAL_FLAG_HOHFELD_OK       (UINT32_C(1) << 2U)
#define SOUND_EVAL_FLAG_FIXPOINT_OK      (UINT32_C(1) << 3U)
#define SOUND_FIR_BYTES                  FLOW_IR_WIRE_BYTES(23U, 5U, 4U, 18U, 0U)
#define SOUND_FIR_FLAG_USE_REPAIRED      (UINT32_C(1) << 0U)

FLOW_SCHEMA_STATIC_ASSERT(SOUND_FIR_BYTES == 2144U, "SOUND FIR1 size mismatch");
FLOW_SCHEMA_STATIC_ASSERT(sizeof(SoundArena) == SOUND_ARENA_BYTES,
                          "SoundArena must occupy exactly 16,384 bytes");
FLOW_SCHEMA_STATIC_ASSERT(SOUND_MAX_NODES == 64U, "SOUND_MAX_NODES must be 64");
FLOW_SCHEMA_STATIC_ASSERT(SOUND_MAX_EVENTS == 64U, "SOUND_MAX_EVENTS must be 64");
FLOW_SCHEMA_STATIC_ASSERT(SOUND_MAX_CANDIDATES == 16U, "SOUND_MAX_CANDIDATES must be 16");

typedef enum {
    SOUND_SORT_FACTUAL = 0U,
    SOUND_SORT_NORMATIVE = 1U,
    SOUND_SORT_INSTITUTIONAL = 2U
} sound_sort_t;

typedef enum {
    SOUND_EVID_DIRECT = 0U,
    SOUND_EVID_SENSORY = 1U,
    SOUND_EVID_ENACTED_NORM = 2U,
    SOUND_EVID_INFERENTIAL = 3U,
    SOUND_EVID_REPORTATIVE = 4U,
    SOUND_EVID_ASSUMED = 5U
} sound_evid_t;

typedef enum {
    SOUND_ASPECT_STATE = 0U,
    SOUND_ASPECT_ACTIVITY = 1U,
    SOUND_ASPECT_ACCOMPLISHMENT = 2U,
    SOUND_ASPECT_ACHIEVEMENT = 3U
} sound_aspect_t;

typedef enum {
    SOUND_FORCE_ASSERTION = 0U,
    SOUND_FORCE_QUERY = 1U,
    SOUND_FORCE_DIRECTIVE = 2U,
    SOUND_FORCE_DECLARATIVE = 3U,
    SOUND_FORCE_IRREALIS = 4U
} sound_force_t;

typedef enum {
    SOUND_STATUS_ESTABLISHED = 0U,
    SOUND_STATUS_DEFEATED = 1U,
    SOUND_STATUS_CONFLICT_UNDECIDED = 2U,
    SOUND_STATUS_PENUMBRA_OPEN = 3U
} sound_status_t;

typedef enum {
    SOUND_B4_BOT = 0U,
    SOUND_B4_FALSE = 1U,
    SOUND_B4_TRUE = 2U,
    SOUND_B4_TOP = 3U
} sound_b4_t;

typedef enum {
    SOUND_HOHFELD_CLAIM = 0U,
    SOUND_HOHFELD_PRIVILEGE = 1U,
    SOUND_HOHFELD_DUTY = 2U,
    SOUND_HOHFELD_NORIGHT = 3U,
    SOUND_HOHFELD_POWER = 4U,
    SOUND_HOHFELD_IMMUNITY = 5U,
    SOUND_HOHFELD_LIABILITY = 6U,
    SOUND_HOHFELD_DISABILITY = 7U
} sound_hohfeld_pos_t;

typedef enum {
    SOUND_REASON_NONE = 0U,
    SOUND_REASON_PENUMBRA = (1U << 0U),
    SOUND_REASON_REVOKED = (1U << 1U),
    SOUND_REASON_EVIDENTIAL_LEAF = (1U << 2U),
    SOUND_REASON_HUME_IS_TO_OUGHT = (1U << 3U),
    SOUND_REASON_HUME_OUGHT_TO_IS = (1U << 4U),
    SOUND_REASON_UNVERIFIED_PRESUP = (1U << 5U),
    SOUND_REASON_ATTACKED = (1U << 6U),
    SOUND_REASON_NO_ACTIVE_CLAUSE = (1U << 7U)
} sound_reason_t;

#define SOUND_FOREST_CTRL_PREPARED_SEALED   (UINT32_C(1) << 0U)
#define SOUND_FOREST_CTRL_NO_DIRECT_CLAIM   (UINT32_C(1) << 1U)
#define SOUND_FOREST_CTRL_KEYED_SIPHASH     (UINT32_C(1) << 2U)
#define SOUND_FOREST_CTRL_PROPOSER_LOW_EVID (UINT32_C(1) << 3U)
#define SOUND_FLOW_AUX_PRIORITY_CELL        UINT32_C(36)
#define SOUND_FLOW_AUX_GLUE_CELL            UINT32_C(37)
#define SOUND_GLUE_MAGIC_VER                UINT64_C(0x00010000314C4753)

typedef enum {
    SOUND_INJECT_NONE = 0U,
    SOUND_INJECT_IMMUNITY_SLOT = (1U << 0U),
    SOUND_INJECT_POLICY_SLOT = (1U << 1U),
    SOUND_INJECT_UNBOUND_SLOT = (1U << 2U),
    SOUND_INJECT_CROSS_PLANE_SORT = (1U << 3U),
    SOUND_INJECT_PRIV_ESCALATION = (1U << 4U),
    SOUND_INJECT_TOPOLOGY_MUTATION = (1U << 5U),
    SOUND_INJECT_POLICY_TAMPER = (1U << 6U),
    SOUND_INJECT_DIRECT_CLAIM_FORBID = (1U << 7U),
    SOUND_INJECT_EVID_CEILING = (1U << 8U),
    SOUND_INJECT_SPAN_HASH_MISMATCH = (1U << 9U),
    SOUND_INJECT_DUPLICATE_TARGET = (1U << 10U)
} sound_inject_reason_t;

typedef enum {
    SOUND_FIBER_COMMITTED = 0U,
    SOUND_FIBER_FALLBACK_NULL = 1U,
    SOUND_FIBER_FALLBACK_BOUNDS = 2U,
    SOUND_FIBER_FALLBACK_FORMAT = 3U,
    SOUND_FIBER_FALLBACK_MAC = 4U,
    SOUND_FIBER_HOLD_IMMUNITY = 5U,
    SOUND_FIBER_HOLD_NO_POWER = 6U,
    SOUND_FIBER_HOLD_MISSING_CONTEXT = 7U,
    SOUND_FIBER_HOLD_INCOHERENT = 8U,
    SOUND_FIBER_FALLBACK_POLICY_TAMPER = 9U,
    SOUND_FIBER_FALLBACK_CLARIFY = 10U,
    SOUND_FIBER_FALLBACK_IRREALIS = 11U,
    SOUND_FIBER_FALLBACK_SPAN = 12U,
    SOUND_FIBER_FALLBACK_POLICY_VIOLATION = 13U
} sound_fiber_t;

typedef enum {
    SOUND_PHYS_UTTERANCE = 1U,
    SOUND_PHYS_DECLARATIVE = 2U,
    SOUND_PHYS_RESCISSION = 3U,
    SOUND_PHYS_FOREST_ADMIT = 4U,
    SOUND_PHYS_INJECT_BLOCKED = 5U,
    SOUND_PHYS_CHART_GLUE = 6U
} sound_phys_kind_t;

typedef struct {
    uint64_t magic_ver;
    uint64_t src_merkle_root;
    uint64_t src_policy_hash;
    uint64_t src_export_mask;
    uint64_t dst_import_mask;
    uint64_t glue_hash;
    uint64_t epoch_flags;
    uint64_t reserved;
} SoundChartGlue64;

FLOW_SCHEMA_STATIC_ASSERT(sizeof(SoundChartGlue64) == 64U,
                          "SoundChartGlue64 must fit in one 64B flow_aux cell");

/* ---- Pillar 1-4 Classification Atlases & Discovery Witnesses ---- */

typedef struct {
    uint32_t b4_v4_ok;
    uint32_t b4_demorgan_ok;
    uint32_t b4_interlaced_ok;
    uint32_t b4_distrib_ok;
    uint32_t n3_total;
    uint32_t n3_grounded_unique;
    uint32_t n3_unique_nongrounded;
    uint32_t n3_multi_stable;
    uint32_t n3_classical_unsolvable;
    uint32_t n3_conflict_nonempty;
    uint32_t n4_total;
    uint32_t n4_grounded_unique;
    uint32_t n4_unique_nongrounded;
    uint32_t n4_multi_stable;
    uint32_t n4_classical_unsolvable;
    uint32_t n4_conflict_nonempty;
} sound_bilattice_atlas_t;

typedef struct {
    uint32_t num_nodes;
    uint8_t atk_to[8];
    uint8_t i_star;
    uint8_t p_star;
    uint8_t conflict_mask;
    uint8_t is_odd_cycle;
    uint32_t classical_models;
    uint64_t seed;
    uint64_t merkle;
} sound_paradox_witness_t;

typedef struct {
    uint32_t c3_z4_total;
    uint32_t c3_z4_coboundary;
    uint32_t c3_z4_obstructed;
    uint32_t c4_z4_total;
    uint32_t c4_z4_coboundary;
    uint32_t c4_z4_obstructed;
    uint32_t p3_z4_total;
    uint32_t p3_z4_coboundary;
    uint32_t c3_v4_total;
    uint32_t c3_v4_coboundary;
    uint32_t gauge_invariance_ok;
} sound_sheaf_atlas_t;

typedef struct {
    uint8_t g01;
    uint8_t g12;
    uint8_t g20;
    uint8_t holonomy_z4;
    uint8_t phi[3];
    uint8_t sg01;
    uint8_t sg12;
    uint8_t sg20;
    uint8_t gauge_holonomy_z4;
    uint8_t is_coboundary_z4;
    uint8_t skew_symmetric_ok;
    uint64_t t01;
    uint64_t t12;
    uint64_t t20;
    uint64_t xor_holonomy;
    uint64_t seed;
    uint64_t merkle;
} sound_sheaf_witness_t;

typedef struct {
    uint32_t v4_group_ok;
    uint32_t v4_orbits;
    uint32_t transitions_checked;
    uint32_t coherence_violations;
    uint32_t immunity_leaks;
    uint32_t disability_leaks;
    uint32_t reachable_imm0;
    uint32_t reachable_imm1;
    uint32_t reachable_imm2;
    uint32_t reachable_imm3;
} sound_hohfeld_atlas_t;

typedef struct {
    uint32_t num_nodes;
    uint64_t established_mask;
    uint64_t defeated_mask;
    uint64_t conflict_mask;
    uint64_t penumbra_mask;
    uint8_t statuses[4];
    uint32_t hohfeld_ok;
    uint64_t merkle;
} sound_chisholm_witness_t;

typedef struct {
    uint64_t init_pos;
    uint64_t init_neg;
    uint64_t power;
    uint64_t immunity;
    uint64_t final_pos;
    uint64_t final_neg;
    uint8_t step_slot[4];
    uint8_t step_fiber[4];
    uint16_t tie_weight;
    uint32_t energy_keep_a;
    uint32_t energy_keep_b;
    uint32_t energy_both;
    uint32_t energy_neither;
    uint32_t kanlock_win_energy;
    uint32_t kanlock_both_energy;
    uint32_t freeze_ok;
    uint64_t seed;
    uint64_t merkle;
} sound_norm_trap_witness_t;

typedef struct {
    uint64_t epistemic;
    uint64_t duty_pos;
    uint64_t duty_neg;
    uint64_t power;
    uint64_t immunity;
    uint64_t modal;
} sound_world_state_t;

typedef struct {
    uint32_t total_unary_maps;
    uint32_t involutive_maps;
    uint32_t t_antitone_maps;
    uint32_t k_monotone_maps;
    uint32_t classical_neg_maps;
    uint32_t hole_presup_pairs;
    uint32_t strawson_gap_pairs;
    uint32_t cond_filter_ok;
    uint32_t plug_irrealis_ok;
    uint32_t force_invariants_ok;
} sound_projection_atlas_t;

typedef struct {
    uint32_t aspect_bijection_ok;
    uint32_t telic_blocked_mask;
    uint32_t j_start;
    uint32_t j_end;
    uint32_t i_start;
    uint32_t i_end;
    uint32_t state_gate_telic;
    uint32_t activity_gate_telic;
    uint32_t accomplishment_gate_telic;
    uint32_t achievement_gate_telic;
    uint32_t declarative_atelic_fiber;
    uint64_t seed;
    uint64_t merkle;
} sound_aspect_witness_t;

typedef struct {
    uint8_t a_assert;
    uint8_t a_presup;
    uint8_t b_assert;
    uint8_t b_presup;
    uint8_t eval_fail_b;
    uint8_t eval_neg_fail_b;
    uint8_t plug_presup;
    uint8_t plug_eval;
    uint8_t cond_false_presup;
    uint8_t cond_true_presup;
    uint8_t force_hume_ok;
    uint8_t force_query_ok;
    uint8_t force_irrealis_ok;
    uint8_t force_directive_ok;
    uint64_t seed;
    uint64_t merkle;
} sound_presup_witness_t;

/* ---- Inline Tiny Bit & Arena Primitives (Freestanding Clean) ---- */

static inline uint64_t sound_bit64(uint32_t i) {
    return UINT64_C(1) << (i & 63U);
}

static inline uint64_t sound_mask_n(uint32_t n) {
    if (n >= SOUND_MAX_NODES) {
        return UINT64_MAX;
    }
    if (n == 0U) {
        return UINT64_C(0);
    }
    return (UINT64_C(1) << n) - UINT64_C(1);
}

static inline uint64_t sound_mask_bool(bool cond) {
    return UINT64_C(0) - (uint64_t)cond;
}

static inline uint32_t sound_mask_bool_u32(bool cond) {
    return UINT32_C(0) - (uint32_t)cond;
}

static inline uint64_t sound_mux_u64(uint64_t mask, uint64_t tr, uint64_t fa) {
    return (tr & mask) | (fa & (~mask));
}

static inline uint32_t sound_mux_u32(uint32_t mask, uint32_t tr, uint32_t fa) {
    return (tr & mask) | (fa & (~mask));
}

static inline sound_fiber_t sound_record_fiber(SoundArena *arena, sound_fiber_t fiber) {
    if (arena != NULL) {
        arena->map.forest_ctrl.last_fiber = (uint32_t)fiber;
        arena->map.forest_ctrl.fiber_epoch++;
    }
    return fiber;
}

static inline void sound_zero_bytes(void *dst, size_t n) {
    volatile uint8_t *p = (volatile uint8_t *)dst;
    for (size_t i = 0U; i < n; i++) {
        p[i] = 0U;
    }
}

static inline uint64_t sound_fnv1a_bytes(uint64_t seed, const uint8_t *data, size_t len) {
    uint64_t h = seed;
    for (size_t i = 0U; i < len; i++) {
        h ^= (uint64_t)data[i];
        h *= SOUND_FNV1A_PRIME;
    }
    return h;
}

static inline uint64_t sound_fnv1a_u64(uint64_t h, uint64_t v) {
    for (uint32_t b = 0U; b < 8U; b++) {
        h ^= (v >> (b * 8U)) & UINT64_C(0xFF);
        h *= SOUND_FNV1A_PRIME;
    }
    return h;
}

static inline void sound_write_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & UINT32_C(0xFF));
    p[1] = (uint8_t)((v >> 8U) & UINT32_C(0xFF));
    p[2] = (uint8_t)((v >> 16U) & UINT32_C(0xFF));
    p[3] = (uint8_t)((v >> 24U) & UINT32_C(0xFF));
}

static inline void sound_write_le64(uint8_t *p, uint64_t v) {
    sound_write_le32(p, (uint32_t)(v & UINT64_C(0xFFFFFFFF)));
    sound_write_le32(p + 4U, (uint32_t)((v >> 32U) & UINT64_C(0xFFFFFFFF)));
}

static inline void sound_arena_init(SoundArena *arena) {
    if (arena == NULL) {
        return;
    }
    sound_zero_bytes(arena, sizeof(*arena));
    arena->map.img_hdr.magic = SOUND_MAGIC;
    arena->map.img_hdr.version = SOUND_VERSION;
    arena->map.img_hdr.schema_hash = SOUND_SCHEMA_SCHEMA_HASH;
    arena->map.img_hdr.phys_hash = SOUND_FNV1A_OFFSET;
}

static inline uint64_t sound_arena_merkle(const SoundArena *arena) {
    if (arena == NULL) {
        return UINT64_C(0);
    }
    return sound_fnv1a_bytes(SOUND_FNV1A_OFFSET, arena->cells[1].bytes,
                             (size_t)(SOUND_ARENA_CELLS - 1U) * 64ULL);
}

static inline void sound_arena_seal(SoundArena *arena) {
    if (arena == NULL) {
        return;
    }
    arena->map.img_hdr.magic = SOUND_MAGIC;
    arena->map.img_hdr.version = SOUND_VERSION;
    arena->map.img_hdr.schema_hash = SOUND_SCHEMA_SCHEMA_HASH;
    arena->map.img_hdr.merkle_root = sound_arena_merkle(arena);
}

static inline bool sound_hohfeld_coherent(const SoundArena *arena) {
    if (arena == NULL) {
        return false;
    }
    return (arena->map.norm.duty_pos_mask & arena->map.norm.duty_neg_mask) == UINT64_C(0);
}

/* ---- Ring-0 External Kernel Prototypes (`src/sound_ring0.c`) ---- */

uint64_t sound_siphash24_bytes(uint64_t k0, uint64_t k1, const uint8_t *data, size_t len);
uint64_t sound_arena_siphash24(const SoundArena *arena, uint64_t k0, uint64_t k1);
uint64_t sound_policy_digest(const SoundArena *arena);
uint64_t sound_policy_digest_keyed(const SoundArena *arena, uint64_t k0, uint64_t k1);

sound_fiber_t sound_policy_verify(const SoundArena *arena);
sound_fiber_t sound_policy_verify_keyed(const SoundArena *arena, uint64_t k0, uint64_t k1);
sound_fiber_t sound_policy_seal(SoundArena *arena, uint64_t policy_mask, uint64_t bind_mask,
                                uint32_t ctrl_flags);
sound_fiber_t sound_policy_seal_keyed(SoundArena *arena, uint64_t policy_mask, uint64_t bind_mask,
                                      uint32_t ctrl_flags, uint64_t k0, uint64_t k1);
sound_fiber_t sound_arena_verify(const SoundArena *arena);

uint64_t sound_arena_factual_mask(const SoundArena *arena);
bool sound_clause_valid(const SoundArena *arena, uint32_t u, uint64_t clause_mask,
                        uint64_t node_mask, uint64_t factual_mask, uint32_t *diag_reason_out);
bool sound_node_fires(const SoundArena *arena, uint32_t u, uint64_t blocked_by, uint64_t active_set,
                      uint64_t node_mask, uint64_t factual_mask);
uint64_t sound_gamma(const SoundArena *arena, uint64_t blocked_by, uint64_t node_mask,
                     uint64_t factual_mask);
sound_fiber_t sound_eval(SoundArena *arena);

sound_fiber_t sound_append_phys(SoundArena *arena, uint32_t kind, uint32_t target_node,
                                uint32_t duty_slot, uint32_t flags, uint32_t span_start,
                                uint32_t span_end, uint64_t payload_hash);
sound_fiber_t sound_apply_potestative(SoundArena *arena, uint32_t slot, bool grant_pos,
                                      bool revoke_pos, bool grant_neg, bool revoke_neg);
sound_fiber_t sound_apply_declarative(SoundArena *arena, uint32_t intent_node, uint32_t duty_slot,
                                      bool in_constitutive_context, bool internal_will_matches,
                                      uint64_t utterance_hash);
sound_fiber_t sound_exercise_rescission(SoundArena *arena, uint32_t target_intent_node,
                                        uint32_t duty_slot, uint64_t rescission_hash);

sound_fiber_t sound_admit_forest(SoundArena *arena, uint32_t *admitted_node_out);
sound_fiber_t sound_admit_forest_with_utterance(SoundArena *arena, const uint8_t *utterance_ptr,
                                                size_t utterance_len, uint32_t *admitted_node_out);
sound_fiber_t sound_admit_forest_batch(SoundArena *arena, const uint8_t *utterance_ptr,
                                       size_t utterance_len, uint32_t *admitted_count_out);
sound_fiber_t sound_admit_forest_batch_keyed(SoundArena *arena, const uint8_t *utterance_ptr,
                                             size_t utterance_len, uint64_t k0, uint64_t k1,
                                             uint32_t *admitted_count_out);

sound_fiber_t sound_searu_repair_conflict(SoundArena *arena, uint64_t seed, uint32_t max_iters);
sound_fiber_t sound_set_priority_weights(SoundArena *arena, const uint8_t weights[SOUND_MAX_NODES]);
sound_fiber_t sound_searu_repair_conflict_weighted(SoundArena *arena, uint64_t seed,
                                                   uint32_t max_iters,
                                                   const uint8_t priority_weights[SOUND_MAX_NODES]);

uint64_t sound_chart_glue_digest(const SoundChartGlue64 *g);
sound_fiber_t sound_chart_export_port(const SoundArena *src, uint64_t export_mask,
                                      SoundChartGlue64 *port_out);
sound_fiber_t sound_chart_glue_import(SoundArena *dst, const SoundArena *src,
                                      uint64_t src_export_mask,
                                      const uint8_t dst_slot_for_src[SOUND_MAX_NODES]);
sound_fiber_t sound_chart_glue_import_keyed(SoundArena *dst, const SoundArena *src,
                                            uint64_t src_export_mask,
                                            const uint8_t dst_slot_for_src[SOUND_MAX_NODES],
                                            uint64_t k0, uint64_t k1);
sound_fiber_t sound_chart_glue_verify(const SoundArena *dst, const SoundArena *src);

sound_fiber_t sound_lift_policy_eval_to_fir(const SoundArena *arena, const char *prog_name,
                                            uint32_t unroll_alt_steps, uint8_t *out_buf,
                                            size_t out_cap, size_t *out_len);
sound_fiber_t sound_lift_to_fir(const SoundArena *arena, const char *prog_name, uint32_t fir_flags,
                                uint8_t *out_buf, size_t out_cap, size_t *out_len);

/* ---- Pillar 1: Belnap Bilattice & Odd-Cycle Classification / Hunt ---- */

uint8_t sound_b4_neg(uint8_t x);
uint8_t sound_b4_conf(uint8_t x);
uint8_t sound_b4_and_t(uint8_t x, uint8_t y);
uint8_t sound_b4_or_t(uint8_t x, uint8_t y);
uint8_t sound_b4_and_k(uint8_t x, uint8_t y);
uint8_t sound_b4_or_k(uint8_t x, uint8_t y);
bool sound_b4_le_t(uint8_t x, uint8_t y);
bool sound_b4_le_k(uint8_t x, uint8_t y);
uint32_t sound_graph_count_classical_models(uint32_t n, const uint8_t atk_to[8]);
void sound_graph_eval_bilattice_small(uint32_t n, const uint8_t atk_to[8], uint8_t *i_star_out,
                                      uint8_t *p_star_out);
void sound_classify_bilattice(sound_bilattice_atlas_t *out);
void sound_hunt_paradox_min(uint64_t seed, sound_paradox_witness_t *out);

/* ---- Pillar 2: Discrete Cech Sheaf Cohomology (H^1) ---- */

uint8_t sound_sheaf_holonomy_z4_3(uint8_t g01, uint8_t g12, uint8_t g20);
uint8_t sound_sheaf_holonomy_v4_3(uint8_t g01, uint8_t g12, uint8_t g20);
bool sound_sheaf_is_coboundary_z4_3(uint8_t g01, uint8_t g12, uint8_t g20);
bool sound_sheaf_is_coboundary_v4_3(uint8_t g01, uint8_t g12, uint8_t g20);
void sound_classify_sheaf(sound_sheaf_atlas_t *out);
void sound_hunt_sheaf_h1(uint64_t seed, sound_sheaf_witness_t *out);

/* ---- Pillar 3: Complete 8-Position Hohfeld V4 Group, Chisholm & Norm Trap ---- */

sound_hohfeld_pos_t sound_hohfeld_correlative(sound_hohfeld_pos_t p);
sound_hohfeld_pos_t sound_hohfeld_opposite(sound_hohfeld_pos_t p);
sound_hohfeld_pos_t sound_hohfeld_dual(sound_hohfeld_pos_t p);
bool sound_hohfeld_is_first_order(sound_hohfeld_pos_t p);
uint32_t sound_lex_specialis_pair_energy(bool keep_a, bool keep_b, uint16_t wa, uint16_t wb);
void sound_classify_hohfeld(sound_hohfeld_atlas_t *out);
void sound_hunt_chisholm(sound_chisholm_witness_t *out);
void sound_hunt_norm_trap(uint64_t seed, sound_norm_trap_witness_t *out);

/* ---- Pillar 4: Vendler Aspect, 4-Valued Presupposition & Illocutionary Force ---- */

bool sound_aspect_is_dynamic(sound_aspect_t a);
bool sound_aspect_is_telic(sound_aspect_t a);
sound_aspect_t sound_aspect_of_features(bool dyn, bool telic);
bool sound_aspect_constitutive_ok(sound_aspect_t a, bool requires_telic);
uint8_t sound_presup_eval(uint8_t assert_val, uint8_t presup_val);
void sound_presup_neg(uint8_t assert_val, uint8_t presup_val, uint8_t *out_assert,
                      uint8_t *out_presup);
void sound_presup_irrealis(uint8_t assert_val, uint8_t presup_val, uint8_t *out_assert,
                           uint8_t *out_presup);
void sound_presup_cond(uint8_t a_assert, uint8_t a_presup, uint8_t b_assert, uint8_t b_presup,
                       uint8_t *out_assert, uint8_t *out_presup);
sound_fiber_t sound_force_apply(sound_world_state_t *st, sound_force_t force, uint64_t slot_bit,
                                sound_aspect_t aspect, bool gp, bool rp, bool gn, bool rn);
void sound_classify_projection(sound_projection_atlas_t *out);
void sound_hunt_aspect_firewall(uint64_t seed, sound_aspect_witness_t *out);
void sound_hunt_presup_filter(uint64_t seed, sound_presup_witness_t *out);

/* ---- Pillar 5: Asymmetric Contract Informed-Consent & Benefit-Drift Verifier ---- */

#define SOUND_CONTRACT_MAX_CLAUSES 32U

typedef enum {
    SOUND_CLAUSE_BASE_FEE = 0U,
    SOUND_CLAUSE_UNILATERAL_POWER = 1U,
    SOUND_CLAUSE_AMBIGUOUS_OBLIGATION = 2U,
    SOUND_CLAUSE_PRESUP_SMUGGLE = 3U,
    SOUND_CLAUSE_CHISHOLM_DEFAULT = 4U,
    SOUND_CLAUSE_RECIPROCITY = 5U
} sound_clause_kind_t;

typedef enum {
    SOUND_CVIOL_NONE = 0U,
    SOUND_CVIOL_IMMUNITY_BREACH = (1U << 0U),
    SOUND_CVIOL_BENEFIT_DRIFT = (1U << 1U),
    SOUND_CVIOL_UNINFORMED_AMBIG = (1U << 2U),
    SOUND_CVIOL_PRESUP_SMUGGLE = (1U << 3U),
    SOUND_CVIOL_CHISHOLM_CASCADE = (1U << 4U),
    SOUND_CVIOL_UNCONSCIONABLE = (1U << 5U),
    SOUND_CVIOL_SHEAF_CONFLICT = (1U << 6U),
    SOUND_CVIOL_EXIT_TRAP = (1U << 7U)
} sound_contract_viol_t;

typedef struct {
    uint32_t clause_id;
    uint8_t kind;
    uint8_t disclosed;
    uint8_t b4_truth;
    uint8_t sheaf_cycle;
    uint64_t target_mask;
    uint64_t base_loss;
    uint64_t extra_drift;
    uint64_t exit_fee;
    uint64_t cons_penalty;
    uint64_t inst_penalty;
} sound_contract_clause_t;

typedef struct {
    uint64_t seed;
    uint64_t max_loss_cap;
    uint64_t max_exit_fee;
    uint64_t protected_mask;
    uint64_t max_asym_num;
    uint64_t max_asym_den;
    uint32_t num_clauses;
    uint32_t reserved;
    sound_contract_clause_t clauses[SOUND_CONTRACT_MAX_CLAUSES];
} sound_contract_spec_t;

typedef struct {
    uint32_t certified;
    uint32_t viol_flags;
    uint32_t num_predatory_clauses;
    uint32_t repaired_certified;
    uint64_t base_loss_sum;
    uint64_t worst_case_loss;
    int64_t benefit_drift;
    uint64_t max_exit_seen;
    uint64_t repaired_worst_loss;
    uint32_t predatory_clause_ids[SOUND_CONTRACT_MAX_CLAUSES];
    uint32_t clause_viol_masks[SOUND_CONTRACT_MAX_CLAUSES];
    uint64_t merkle;
} sound_contract_audit_t;

void sound_audit_contract(const sound_contract_spec_t *spec, sound_contract_audit_t *out);
void sound_hunt_contract_drift(uint64_t seed, sound_contract_spec_t *spec_out,
                               sound_contract_audit_t *audit_out);

#ifdef __cplusplus
}
#endif

#endif /* SOUND_H */
