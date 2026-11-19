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
 * SOUND v2.1.0 — Formal Epistemic & Normative Soundness Kernel (Pure GPR C17)
 * ============================================================================
 * FLOW v8.0.0 SSOT Substrate over a sealed 16,384B (256 x 64B) `SoundArena`:
 *   - Zero heap allocation, zero floating-point, zero indirect calls (`blr`/`br`),
 *     zero integer division (`udiv`/`sdiv`), zero jump tables, `-mgeneral-regs-only`
 *     and freestanding Ring-0 clean.
 *   - Layer 0: Zero-Trust Proof-Carrying Candidate Forest Gate + Irrealis Hold
 *   - Layer 1: Monotone Physical Log (`PhysLog`) + Hohfeld `Immunity` KanLock
 *   - Layer 2: 64-Node 4-Valued Bilattice Alternating-Fixpoint Kernel (Γ / Γ²)
 *   - SEARU  : KanLock-pinned Pure-GPR maximal conflict-free extension repair
 *   - FIR1   : Pure-GPR `.snd -> .fir` QTT linear authority gate lifter
 * ============================================================================ */

#define SOUND_MAGIC                      UINT32_C(0x32444E53) /* "SND2" */
#define SOUND_VERSION                    UINT32_C(2)
#define SOUND_ARENA_CELLS                256U
#define SOUND_ARENA_BYTES                16384U
#define SOUND_MAX_CLAUSES                8U
#define SOUND_LABEL_BYTES                32U
#define SOUND_NODE_FLAG_CONSTITUTIVE     UINT32_C(1)
#define SOUND_FNV1A_OFFSET               UINT64_C(0xCBF29CE484222325)
#define SOUND_FNV1A_PRIME                UINT64_C(0x00000100000001B3)
#define SOUND_EVAL_FLAG_PARTITION_OK     (UINT32_C(1) << 0)
#define SOUND_EVAL_FLAG_CONFLICT_FREE_OK (UINT32_C(1) << 1)
#define SOUND_EVAL_FLAG_HOHFELD_OK       (UINT32_C(1) << 2)
#define SOUND_EVAL_FLAG_FIXPOINT_OK      (UINT32_C(1) << 3)
#define SOUND_FIR_BYTES                  FLOW_IR_WIRE_BYTES(23U, 5U, 4U, 18U, 0U)
#define SOUND_FIR_FLAG_USE_REPAIRED      (UINT32_C(1) << 0)

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
    SOUND_REASON_NONE = 0U,
    SOUND_REASON_PENUMBRA = (1U << 0),
    SOUND_REASON_REVOKED = (1U << 1),
    SOUND_REASON_EVIDENTIAL_LEAF = (1U << 2),
    SOUND_REASON_HUME_IS_TO_OUGHT = (1U << 3),
    SOUND_REASON_HUME_OUGHT_TO_IS = (1U << 4),
    SOUND_REASON_UNVERIFIED_PRESUP = (1U << 5),
    SOUND_REASON_ATTACKED = (1U << 6),
    SOUND_REASON_NO_ACTIVE_CLAUSE = (1U << 7)
} sound_reason_t;

#define SOUND_FOREST_CTRL_PREPARED_SEALED   (UINT32_C(1) << 0)
#define SOUND_FOREST_CTRL_NO_DIRECT_CLAIM   (UINT32_C(1) << 1)
#define SOUND_FOREST_CTRL_KEYED_SIPHASH     (UINT32_C(1) << 2)
#define SOUND_FOREST_CTRL_PROPOSER_LOW_EVID (UINT32_C(1) << 3)
#define SOUND_FLOW_AUX_PRIORITY_CELL        UINT32_C(36)
#define SOUND_FLOW_AUX_GLUE_CELL            UINT32_C(37)
#define SOUND_GLUE_MAGIC_VER                UINT64_C(0x00010000314C4753)

typedef enum {
    SOUND_INJECT_NONE = 0U,
    SOUND_INJECT_IMMUNITY_SLOT = (1U << 0),
    SOUND_INJECT_POLICY_SLOT = (1U << 1),
    SOUND_INJECT_UNBOUND_SLOT = (1U << 2),
    SOUND_INJECT_CROSS_PLANE_SORT = (1U << 3),
    SOUND_INJECT_PRIV_ESCALATION = (1U << 4),
    SOUND_INJECT_TOPOLOGY_MUTATION = (1U << 5),
    SOUND_INJECT_POLICY_TAMPER = (1U << 6),
    SOUND_INJECT_DIRECT_CLAIM_FORBID = (1U << 7),
    SOUND_INJECT_EVID_CEILING = (1U << 8),
    SOUND_INJECT_SPAN_HASH_MISMATCH = (1U << 9),
    SOUND_INJECT_DUPLICATE_TARGET = (1U << 10)
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

static inline uint64_t sound__rotl64(uint64_t x, uint32_t b) {
    return (x << b) | (x >> (64U - b));
}

typedef struct {
    uint64_t v0;
    uint64_t v1;
    uint64_t v2;
    uint64_t v3;
    uint64_t buf;
    uint32_t buflen;
    uint32_t total_len_low;
} sound_siphash24_ctx_t;

static inline void sound__sipround(sound_siphash24_ctx_t *c) {
    c->v0 += c->v1;
    c->v1 = sound__rotl64(c->v1, 13U);
    c->v1 ^= c->v0;
    c->v0 = sound__rotl64(c->v0, 32U);
    c->v2 += c->v3;
    c->v3 = sound__rotl64(c->v3, 16U);
    c->v3 ^= c->v2;
    c->v0 += c->v3;
    c->v3 = sound__rotl64(c->v3, 21U);
    c->v3 ^= c->v0;
    c->v2 += c->v1;
    c->v1 = sound__rotl64(c->v1, 17U);
    c->v1 ^= c->v2;
    c->v2 = sound__rotl64(c->v2, 32U);
}

static inline void sound_siphash24_init(sound_siphash24_ctx_t *c, uint64_t k0, uint64_t k1) {
    c->v0 = UINT64_C(0x736F6D6570736575) ^ k0;
    c->v1 = UINT64_C(0x646F72616E646F6D) ^ k1;
    c->v2 = UINT64_C(0x6C7967656E657261) ^ k0;
    c->v3 = UINT64_C(0x7465646279746573) ^ k1;
    c->buf = UINT64_C(0);
    c->buflen = UINT32_C(0);
    c->total_len_low = UINT32_C(0);
}

static inline void sound_siphash24_update(sound_siphash24_ctx_t *c, const uint8_t *data,
                                          size_t len) {
    for (size_t i = 0U; i < len; i++) {
        c->buf |= ((uint64_t)data[i]) << (c->buflen * 8U);
        c->buflen++;
        c->total_len_low++;
        if (c->buflen == UINT32_C(8)) {
            c->v3 ^= c->buf;
            sound__sipround(c);
            sound__sipround(c);
            c->v0 ^= c->buf;
            c->buf = UINT64_C(0);
            c->buflen = UINT32_C(0);
        }
    }
}

static inline uint64_t sound_siphash24_final(sound_siphash24_ctx_t *c) {
    uint64_t b = (((uint64_t)(c->total_len_low & UINT32_C(0xFF))) << 56U) | c->buf;
    c->v3 ^= b;
    sound__sipround(c);
    sound__sipround(c);
    c->v0 ^= b;
    c->v2 ^= UINT64_C(0xFF);
    sound__sipround(c);
    sound__sipround(c);
    sound__sipround(c);
    sound__sipround(c);
    return c->v0 ^ c->v1 ^ c->v2 ^ c->v3;
}

static inline uint64_t sound_siphash24_bytes(uint64_t k0, uint64_t k1, const uint8_t *data,
                                             size_t len) {
    sound_siphash24_ctx_t ctx;
    sound_siphash24_init(&ctx, k0, k1);
    if (data != NULL && len > 0U) {
        sound_siphash24_update(&ctx, data, len);
    }
    return sound_siphash24_final(&ctx);
}

static inline uint64_t sound_arena_siphash24(const SoundArena *arena, uint64_t k0, uint64_t k1) {
    if (arena == NULL) {
        return UINT64_C(0);
    }
    return sound_siphash24_bytes(k0, k1, arena->cells[1].bytes,
                                 (size_t)(SOUND_ARENA_CELLS - 1U) * 64ULL);
}

static inline uint64_t sound__policy_digest_core(const SoundArena *arena, bool keyed, uint64_t k0,
                                                 uint64_t k1) {
    if (arena == NULL) {
        return UINT64_C(0);
    }
    const SoundForestCtrl64 *ctrl = &arena->map.forest_ctrl;
    uint32_t n = (arena->map.img_hdr.num_nodes <= SOUND_MAX_NODES) ? arena->map.img_hdr.num_nodes
                                                                   : SOUND_MAX_NODES;
    uint8_t hdr_buf[24];
    sound_write_le32(hdr_buf + 0U, n);
    sound_write_le32(hdr_buf + 4U, ctrl->ctrl_flags);
    sound_write_le64(hdr_buf + 8U, ctrl->policy_mask);
    sound_write_le64(hdr_buf + 16U, ctrl->bind_mask);

    uint64_t h = SOUND_FNV1A_OFFSET;
    sound_siphash24_ctx_t sip;
    sound_siphash24_init(&sip, k0, k1);

    if (keyed) {
        sound_siphash24_update(&sip, hdr_buf, sizeof(hdr_buf));
        sound_siphash24_update(&sip, arena->cells[SOUND_SLOT_NORM].bytes, 64U);
    } else {
        h = sound_fnv1a_bytes(h, hdr_buf, sizeof(hdr_buf));
        h = sound_fnv1a_bytes(h, arena->cells[SOUND_SLOT_NORM].bytes, 64U);
    }

    for (uint32_t i = 0U; i < n; i++) {
        uint64_t bit = sound_bit64(i);
        const uint8_t *clause_bytes = arena->cells[SOUND_SLOT_NODE_CLAUSES_BASE + i].bytes;
        if (keyed) {
            sound_siphash24_update(&sip, clause_bytes, 64U);
        } else {
            h = sound_fnv1a_bytes(h, clause_bytes, 64U);
        }
        if ((ctrl->bind_mask & bit) == UINT64_C(0)) {
            const uint8_t *meta_bytes = arena->cells[SOUND_SLOT_NODE_META_BASE + i].bytes;
            if (keyed) {
                sound_siphash24_update(&sip, meta_bytes, 64U);
            } else {
                h = sound_fnv1a_bytes(h, meta_bytes, 64U);
            }
        } else {
            const SoundNodeMeta64 *nd = &arena->map.node_meta[i];
            uint8_t tmpl[54];
            tmpl[0] = nd->sort;
            tmpl[1] = nd->num_clauses;
            sound_write_le32(tmpl + 2U, nd->flags);
            sound_write_le64(tmpl + 6U, nd->presup_mask);
            sound_write_le64(tmpl + 14U, nd->attack_mask);
            for (size_t k = 0U; k < 32U; k++) {
                tmpl[22U + k] = nd->label_bytes[k];
            }
            if (keyed) {
                sound_siphash24_update(&sip, tmpl, sizeof(tmpl));
            } else {
                h = sound_fnv1a_bytes(h, tmpl, sizeof(tmpl));
            }
        }
    }
    return keyed ? sound_siphash24_final(&sip) : h;
}

static inline uint64_t sound_policy_digest(const SoundArena *arena) {
    return sound__policy_digest_core(arena, false, UINT64_C(0), UINT64_C(0));
}

static inline uint64_t sound_policy_digest_keyed(const SoundArena *arena, uint64_t k0,
                                                 uint64_t k1) {
    return sound__policy_digest_core(arena, true, k0, k1);
}

static inline sound_fiber_t sound__policy_verify_core(const SoundArena *arena, bool use_key,
                                                      uint64_t k0, uint64_t k1) {
    if (arena == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    const SoundForestCtrl64 *ctrl = &arena->map.forest_ctrl;
    if ((ctrl->ctrl_flags & SOUND_FOREST_CTRL_PREPARED_SEALED) == UINT32_C(0)) {
        return SOUND_FIBER_COMMITTED;
    }
    if (arena->map.img_hdr.num_nodes > SOUND_MAX_NODES) {
        return SOUND_FIBER_FALLBACK_BOUNDS;
    }
    uint32_t valid_ctrl = SOUND_FOREST_CTRL_PREPARED_SEALED | SOUND_FOREST_CTRL_NO_DIRECT_CLAIM |
                          SOUND_FOREST_CTRL_KEYED_SIPHASH | SOUND_FOREST_CTRL_PROPOSER_LOW_EVID;
    if ((ctrl->ctrl_flags & (~valid_ctrl)) != UINT32_C(0)) {
        return SOUND_FIBER_FALLBACK_POLICY_TAMPER;
    }
    bool is_keyed = (ctrl->ctrl_flags & SOUND_FOREST_CTRL_KEYED_SIPHASH) != UINT32_C(0);
    if (is_keyed && !use_key) {
        return SOUND_FIBER_FALLBACK_POLICY_TAMPER;
    }
    uint64_t nm = sound_mask_n(arena->map.img_hdr.num_nodes);
    if (((ctrl->policy_mask & (~nm)) != UINT64_C(0)) ||
        ((ctrl->bind_mask & (~nm)) != UINT64_C(0)) ||
        ((ctrl->policy_mask & ctrl->bind_mask) != UINT64_C(0)) ||
        ((ctrl->bind_mask & arena->map.norm.immunity_mask) != UINT64_C(0))) {
        return SOUND_FIBER_FALLBACK_POLICY_TAMPER;
    }
    for (uint32_t i = 0U; i < arena->map.img_hdr.num_nodes; i++) {
        if ((ctrl->bind_mask & sound_bit64(i)) != UINT64_C(0)) {
            const SoundNodeMeta64 *nd = &arena->map.node_meta[i];
            if (nd->sort != (uint8_t)SOUND_SORT_FACTUAL ||
                (nd->flags & SOUND_NODE_FLAG_CONSTITUTIVE) != UINT32_C(0)) {
                return SOUND_FIBER_FALLBACK_POLICY_TAMPER;
            }
        }
    }
    uint64_t expected =
        is_keyed ? sound_policy_digest_keyed(arena, k0, k1) : sound_policy_digest(arena);
    if (ctrl->policy_hash != expected) {
        return SOUND_FIBER_FALLBACK_POLICY_TAMPER;
    }
    return SOUND_FIBER_COMMITTED;
}

static inline sound_fiber_t sound_policy_verify(const SoundArena *arena) {
    return sound__policy_verify_core(arena, false, UINT64_C(0), UINT64_C(0));
}

static inline sound_fiber_t sound_policy_verify_keyed(const SoundArena *arena, uint64_t k0,
                                                      uint64_t k1) {
    return sound__policy_verify_core(arena, true, k0, k1);
}

static inline sound_fiber_t sound__policy_seal_core(SoundArena *arena, uint64_t policy_mask,
                                                    uint64_t bind_mask, uint32_t ctrl_flags,
                                                    bool use_key, uint64_t k0, uint64_t k1) {
    if (arena == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    if (arena->map.img_hdr.num_nodes > SOUND_MAX_NODES) {
        return sound_record_fiber(arena, SOUND_FIBER_FALLBACK_BOUNDS);
    }
    uint32_t valid_ctrl = SOUND_FOREST_CTRL_PREPARED_SEALED | SOUND_FOREST_CTRL_NO_DIRECT_CLAIM |
                          SOUND_FOREST_CTRL_KEYED_SIPHASH | SOUND_FOREST_CTRL_PROPOSER_LOW_EVID;
    if ((ctrl_flags & (~valid_ctrl)) != UINT32_C(0)) {
        return sound_record_fiber(arena, SOUND_FIBER_FALLBACK_FORMAT);
    }
    if (!use_key && (ctrl_flags & SOUND_FOREST_CTRL_KEYED_SIPHASH) != UINT32_C(0)) {
        return sound_record_fiber(arena, SOUND_FIBER_FALLBACK_FORMAT);
    }
    uint64_t nm = sound_mask_n(arena->map.img_hdr.num_nodes);
    if (((policy_mask & (~nm)) != UINT64_C(0)) || ((bind_mask & (~nm)) != UINT64_C(0))) {
        return sound_record_fiber(arena, SOUND_FIBER_FALLBACK_BOUNDS);
    }
    if ((policy_mask & bind_mask) != UINT64_C(0)) {
        return sound_record_fiber(arena, SOUND_FIBER_HOLD_INCOHERENT);
    }
    if ((bind_mask & arena->map.norm.immunity_mask) != UINT64_C(0)) {
        return sound_record_fiber(arena, SOUND_FIBER_HOLD_IMMUNITY);
    }
    for (uint32_t i = 0U; i < arena->map.img_hdr.num_nodes; i++) {
        if ((bind_mask & sound_bit64(i)) != UINT64_C(0)) {
            const SoundNodeMeta64 *nd = &arena->map.node_meta[i];
            if (nd->sort != (uint8_t)SOUND_SORT_FACTUAL ||
                (nd->flags & SOUND_NODE_FLAG_CONSTITUTIVE) != UINT32_C(0) ||
                nd->num_clauses > (uint8_t)SOUND_MAX_CLAUSES ||
                (nd->presup_mask & (~nm)) != UINT64_C(0) ||
                (nd->attack_mask & (~nm)) != UINT64_C(0)) {
                return sound_record_fiber(arena, SOUND_FIBER_HOLD_INCOHERENT);
            }
        }
    }
    SoundForestCtrl64 *ctrl = &arena->map.forest_ctrl;
    uint32_t final_flags = ctrl_flags | SOUND_FOREST_CTRL_PREPARED_SEALED;
    if (use_key) {
        final_flags |= SOUND_FOREST_CTRL_KEYED_SIPHASH;
    }
    ctrl->ctrl_flags = final_flags;
    ctrl->last_reject_reason = (uint32_t)SOUND_INJECT_NONE;
    ctrl->policy_mask = policy_mask;
    ctrl->bind_mask = bind_mask;
    ctrl->policy_hash =
        use_key ? sound_policy_digest_keyed(arena, k0, k1) : sound_policy_digest(arena);
    return sound_record_fiber(arena, SOUND_FIBER_COMMITTED);
}

static inline sound_fiber_t sound_policy_seal(SoundArena *arena, uint64_t policy_mask,
                                              uint64_t bind_mask, uint32_t ctrl_flags) {
    return sound__policy_seal_core(arena, policy_mask, bind_mask, ctrl_flags, false, UINT64_C(0),
                                   UINT64_C(0));
}

static inline sound_fiber_t sound_policy_seal_keyed(SoundArena *arena, uint64_t policy_mask,
                                                    uint64_t bind_mask, uint32_t ctrl_flags,
                                                    uint64_t k0, uint64_t k1) {
    return sound__policy_seal_core(arena, policy_mask, bind_mask, ctrl_flags, true, k0, k1);
}

static inline sound_fiber_t sound_arena_verify(const SoundArena *arena) {
    if (arena == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    const SoundImageHeader64 *hdr = &arena->map.img_hdr;
    if (hdr->magic != SOUND_MAGIC || hdr->version != SOUND_VERSION ||
        hdr->schema_hash != SOUND_SCHEMA_SCHEMA_HASH) {
        return SOUND_FIBER_FALLBACK_FORMAT;
    }
    if (hdr->merkle_root != sound_arena_merkle(arena)) {
        return SOUND_FIBER_FALLBACK_MAC;
    }
    if (hdr->num_nodes > SOUND_MAX_NODES || hdr->phys_len > SOUND_MAX_EVENTS ||
        arena->map.forest_ctrl.num_candidates > SOUND_MAX_CANDIDATES) {
        return SOUND_FIBER_FALLBACK_BOUNDS;
    }
    uint64_t node_mask = sound_mask_n(hdr->num_nodes);
    if (((arena->map.norm.revoked_mask & (~node_mask)) != UINT64_C(0)) ||
        ((arena->map.norm.penumbra_mask & (~node_mask)) != UINT64_C(0))) {
        return SOUND_FIBER_FALLBACK_FORMAT;
    }
    for (uint32_t u = 0U; u < hdr->num_nodes; u++) {
        const SoundNodeMeta64 *nd = &arena->map.node_meta[u];
        if (nd->sort > (uint8_t)SOUND_SORT_INSTITUTIONAL ||
            nd->evid > (uint8_t)SOUND_EVID_ASSUMED ||
            nd->aspect > (uint8_t)SOUND_ASPECT_ACHIEVEMENT ||
            nd->num_clauses > (uint8_t)SOUND_MAX_CLAUSES ||
            ((nd->presup_mask & (~node_mask)) != UINT64_C(0)) ||
            ((nd->attack_mask & (~node_mask)) != UINT64_C(0))) {
            return SOUND_FIBER_FALLBACK_FORMAT;
        }
        for (uint32_t c = 0U; c < SOUND_MAX_CLAUSES; c++) {
            if ((arena->map.node_clauses[u].clauses[c] & (~node_mask)) != UINT64_C(0)) {
                return SOUND_FIBER_FALLBACK_FORMAT;
            }
        }
    }
    return sound_policy_verify(arena);
}

static inline bool sound_hohfeld_coherent(const SoundArena *arena) {
    if (arena == NULL) {
        return false;
    }
    return (arena->map.norm.duty_pos_mask & arena->map.norm.duty_neg_mask) == UINT64_C(0);
}

static inline uint64_t sound_arena_factual_mask(const SoundArena *arena) {
    uint64_t m = UINT64_C(0);
    uint32_t n = (arena->map.img_hdr.num_nodes <= SOUND_MAX_NODES) ? arena->map.img_hdr.num_nodes
                                                                   : SOUND_MAX_NODES;
    for (uint32_t u = 0U; u < n; u++) {
        if (arena->map.node_meta[u].sort == (uint8_t)SOUND_SORT_FACTUAL) {
            m |= sound_bit64(u);
        }
    }
    return m;
}

static inline bool sound_clause_valid(const SoundArena *arena, uint32_t u, uint64_t clause_mask,
                                      uint64_t node_mask, uint64_t factual_mask,
                                      uint32_t *diag_reason_out) {
    const SoundNodeMeta64 *nd = &arena->map.node_meta[u];
    uint64_t scoped = clause_mask & node_mask;
    if (clause_mask != scoped) {
        return false;
    }
    if (nd->evid >= (uint8_t)SOUND_EVID_INFERENTIAL && clause_mask == UINT64_C(0)) {
        if (diag_reason_out != NULL) {
            *diag_reason_out |= (uint32_t)SOUND_REASON_EVIDENTIAL_LEAF;
        }
        return false;
    }
    if (nd->sort == (uint8_t)SOUND_SORT_FACTUAL) {
        if ((clause_mask & (~factual_mask)) != UINT64_C(0)) {
            if (diag_reason_out != NULL) {
                *diag_reason_out |= (uint32_t)SOUND_REASON_HUME_OUGHT_TO_IS;
            }
            return false;
        }
        return true;
    }
    if (nd->evid == (uint8_t)SOUND_EVID_ENACTED_NORM ||
        ((nd->flags & SOUND_NODE_FLAG_CONSTITUTIVE) != UINT32_C(0))) {
        return true;
    }
    if ((clause_mask & (node_mask & (~factual_mask))) != UINT64_C(0)) {
        return true;
    }
    if (diag_reason_out != NULL) {
        *diag_reason_out |= (uint32_t)SOUND_REASON_HUME_IS_TO_OUGHT;
    }
    return false;
}

static inline bool sound_node_fires(const SoundArena *arena, uint32_t u, uint64_t blocked_by,
                                    uint64_t active_set, uint64_t node_mask,
                                    uint64_t factual_mask) {
    const SoundNodeMeta64 *nd = &arena->map.node_meta[u];
    uint64_t ub = sound_bit64(u);
    if (((arena->map.norm.revoked_mask | arena->map.norm.penumbra_mask) & ub) != UINT64_C(0)) {
        return false;
    }
    if ((nd->attack_mask & blocked_by) != UINT64_C(0)) {
        return false;
    }
    if ((nd->presup_mask & active_set) != nd->presup_mask) {
        return false;
    }
    uint32_t nc = nd->num_clauses;
    if (nc > SOUND_MAX_CLAUSES) {
        nc = SOUND_MAX_CLAUSES;
    }
    for (uint32_t c = 0U; c < nc; c++) {
        uint64_t cm = arena->map.node_clauses[u].clauses[c];
        if (sound_clause_valid(arena, u, cm, node_mask, factual_mask, NULL) &&
            ((cm & active_set) == cm)) {
            return true;
        }
    }
    return false;
}

static inline uint64_t sound_gamma(const SoundArena *arena, uint64_t blocked_by, uint64_t node_mask,
                                   uint64_t factual_mask) {
    uint32_t n = (arena->map.img_hdr.num_nodes <= SOUND_MAX_NODES) ? arena->map.img_hdr.num_nodes
                                                                   : SOUND_MAX_NODES;
    uint64_t cur = UINT64_C(0);
    for (uint32_t step = 0U; step < n; step++) {
        uint64_t nxt = UINT64_C(0);
        for (uint32_t u = 0U; u < n; u++) {
            if (sound_node_fires(arena, u, blocked_by, cur, node_mask, factual_mask)) {
                nxt |= sound_bit64(u);
            }
        }
        if (nxt == cur) {
            break;
        }
        cur = nxt;
    }
    return cur;
}

static inline sound_fiber_t sound_eval(SoundArena *arena) {
    if (arena == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    if (arena->map.img_hdr.num_nodes > SOUND_MAX_NODES) {
        return sound_record_fiber(arena, SOUND_FIBER_FALLBACK_BOUNDS);
    }
    uint32_t prev_epoch = arena->map.eval_out.eval_epoch;
    sound_zero_bytes(&arena->map.eval_out, sizeof(arena->map.eval_out));
    sound_zero_bytes(&arena->map.eval_status, sizeof(arena->map.eval_status));
    sound_zero_bytes(arena->map.eval_reasons, sizeof(arena->map.eval_reasons));

    uint32_t n = arena->map.img_hdr.num_nodes;
    uint64_t node_mask = sound_mask_n(n);
    uint64_t factual_mask = sound_arena_factual_mask(arena);
    uint64_t cur_i = UINT64_C(0);

    for (uint32_t step = 0U; step < n; step++) {
        uint64_t p_tmp = sound_gamma(arena, cur_i, node_mask, factual_mask);
        uint64_t nxt_i = sound_gamma(arena, p_tmp, node_mask, factual_mask);
        if (nxt_i == cur_i) {
            break;
        }
        cur_i = nxt_i;
    }

    uint64_t p_star = sound_gamma(arena, cur_i, node_mask, factual_mask);
    uint64_t i_star = sound_gamma(arena, p_star, node_mask, factual_mask);
    bool fixpoint_ok = (sound_gamma(arena, i_star, node_mask, factual_mask) == p_star) &&
                       (sound_gamma(arena, p_star, node_mask, factual_mask) == i_star);

    uint64_t pen = arena->map.norm.penumbra_mask & node_mask;
    uint64_t non_pen = node_mask & (~pen);
    uint64_t est = i_star & non_pen;
    uint64_t conf = (p_star & (~i_star)) & non_pen;
    uint64_t defm = non_pen & (~p_star);

    arena->map.eval_out.node_mask = node_mask;
    arena->map.eval_out.i_star = i_star;
    arena->map.eval_out.p_star = p_star;
    arena->map.eval_out.penumbra_mask = pen;
    arena->map.eval_out.established_mask = est;
    arena->map.eval_out.conflict_mask = conf;
    arena->map.eval_out.defeated_mask = defm;

    bool sub_ok = ((i_star & (~p_star)) == UINT64_C(0));
    bool disj_ok = ((est & conf) == UINT64_C(0)) && ((est & defm) == UINT64_C(0)) &&
                   ((est & pen) == UINT64_C(0)) && ((conf & defm) == UINT64_C(0)) &&
                   ((conf & pen) == UINT64_C(0)) && ((defm & pen) == UINT64_C(0));
    bool cover_ok = ((est | conf | defm | pen) == node_mask);
    bool partition_ok = sub_ok && disj_ok && cover_ok;

    bool cfree = true;
    for (uint32_t u = 0U; u < n; u++) {
        uint64_t ub = sound_bit64(u);
        uint8_t st_val = (uint8_t)SOUND_STATUS_DEFEATED;
        uint32_t rs_val = UINT32_C(0);
        if ((pen & ub) != UINT64_C(0)) {
            st_val = (uint8_t)SOUND_STATUS_PENUMBRA_OPEN;
            rs_val = (uint32_t)SOUND_REASON_PENUMBRA;
        } else if ((est & ub) != UINT64_C(0)) {
            st_val = (uint8_t)SOUND_STATUS_ESTABLISHED;
            rs_val = (uint32_t)SOUND_REASON_NONE;
            if ((arena->map.node_meta[u].attack_mask & (est | p_star)) != UINT64_C(0)) {
                cfree = false;
            }
        } else if ((conf & ub) != UINT64_C(0)) {
            st_val = (uint8_t)SOUND_STATUS_CONFLICT_UNDECIDED;
            rs_val = (uint32_t)SOUND_REASON_ATTACKED;
        } else {
            const SoundNodeMeta64 *nd = &arena->map.node_meta[u];
            if ((arena->map.norm.revoked_mask & ub) != UINT64_C(0)) {
                rs_val |= (uint32_t)SOUND_REASON_REVOKED;
            }
            if ((nd->attack_mask & p_star) != UINT64_C(0)) {
                rs_val |= (uint32_t)SOUND_REASON_ATTACKED;
            }
            if ((nd->presup_mask & i_star) != nd->presup_mask) {
                rs_val |= (uint32_t)SOUND_REASON_UNVERIFIED_PRESUP;
            }
            uint32_t nc =
                (nd->num_clauses <= SOUND_MAX_CLAUSES) ? nd->num_clauses : SOUND_MAX_CLAUSES;
            bool any_valid = false;
            for (uint32_t c = 0U; c < nc; c++) {
                if (sound_clause_valid(arena, u, arena->map.node_clauses[u].clauses[c], node_mask,
                                       factual_mask, &rs_val)) {
                    any_valid = true;
                }
            }
            if (!any_valid || rs_val == UINT32_C(0)) {
                rs_val |= (uint32_t)SOUND_REASON_NO_ACTIVE_CLAUSE;
            }
        }
        arena->map.eval_status.status[u] = st_val;
        arena->map.eval_reasons[u >> 4U].reasons[u & 15U] = rs_val;
    }

    uint32_t eflags = UINT32_C(0);
    if (partition_ok) {
        eflags |= SOUND_EVAL_FLAG_PARTITION_OK;
    }
    if (cfree) {
        eflags |= SOUND_EVAL_FLAG_CONFLICT_FREE_OK;
    }
    if (sound_hohfeld_coherent(arena)) {
        eflags |= SOUND_EVAL_FLAG_HOHFELD_OK;
    }
    if (fixpoint_ok) {
        eflags |= SOUND_EVAL_FLAG_FIXPOINT_OK;
    }
    arena->map.eval_out.eval_flags = eflags;
    arena->map.eval_out.eval_epoch = prev_epoch + UINT32_C(1);
    return sound_record_fiber(arena, SOUND_FIBER_COMMITTED);
}

static inline sound_fiber_t sound_append_phys(SoundArena *arena, uint32_t kind,
                                              uint32_t target_node, uint32_t duty_slot,
                                              uint32_t flags, uint32_t span_start,
                                              uint32_t span_end, uint64_t payload_hash) {
    if (arena == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    if (arena->map.img_hdr.phys_len >= SOUND_MAX_EVENTS) {
        return SOUND_FIBER_FALLBACK_BOUNDS;
    }
    uint32_t idx = arena->map.img_hdr.phys_head & (SOUND_MAX_EVENTS - 1U);
    uint32_t eid = arena->map.img_hdr.phys_len + UINT32_C(1);
    uint64_t prev_h = arena->map.img_hdr.phys_hash;

    SoundPhysEvent64 *ev = &arena->map.phys_log[idx];
    ev->event_id = eid;
    ev->kind = kind;
    ev->target_node = target_node;
    ev->duty_slot = duty_slot;
    ev->flags = flags;
    ev->span_start = span_start;
    ev->span_end = span_end;
    ev->reserved0 = UINT32_C(0);
    ev->payload_hash = payload_hash;
    ev->prev_chain_hash = prev_h;

    uint8_t buf[44];
    sound_write_le32(buf + 0U, eid);
    sound_write_le32(buf + 4U, kind);
    sound_write_le32(buf + 8U, target_node);
    sound_write_le32(buf + 12U, duty_slot);
    sound_write_le32(buf + 16U, flags);
    sound_write_le32(buf + 20U, span_start);
    sound_write_le32(buf + 24U, span_end);
    sound_write_le64(buf + 28U, payload_hash);
    sound_write_le64(buf + 36U, prev_h);

    uint64_t chain_h = sound_fnv1a_bytes(prev_h, buf, sizeof(buf));
    ev->chain_hash = chain_h;
    ev->reserved1 = UINT64_C(0);

    arena->map.img_hdr.phys_hash = chain_h;
    arena->map.img_hdr.phys_len = eid;
    arena->map.img_hdr.phys_head = (idx + UINT32_C(1)) & (SOUND_MAX_EVENTS - 1U);
    return SOUND_FIBER_COMMITTED;
}

static inline sound_fiber_t sound_apply_potestative(SoundArena *arena, uint32_t slot,
                                                    bool grant_pos, bool revoke_pos, bool grant_neg,
                                                    bool revoke_neg) {
    if (arena == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    if (slot >= SOUND_MAX_NODES) {
        return sound_record_fiber(arena, SOUND_FIBER_FALLBACK_BOUNDS);
    }
    uint64_t bit = sound_bit64(slot);
    uint64_t cur_pos = arena->map.norm.duty_pos_mask;
    uint64_t cur_neg = arena->map.norm.duty_neg_mask;
    bool imm_hit = (arena->map.norm.immunity_mask & bit) != UINT64_C(0);
    bool pow_ok = (arena->map.norm.power_mask & bit) != UINT64_C(0);

    uint64_t dp0 = sound_mux_u64(sound_mask_bool(grant_pos), cur_pos | bit, cur_pos);
    uint64_t dp = sound_mux_u64(sound_mask_bool(revoke_pos), dp0 & (~bit), dp0);
    uint64_t dn0 = sound_mux_u64(sound_mask_bool(grant_neg), cur_neg | bit, cur_neg);
    uint64_t dn = sound_mux_u64(sound_mask_bool(revoke_neg), dn0 & (~bit), dn0);
    bool coh_ok = (dp & dn) == UINT64_C(0);

    bool commit = (!imm_hit) & pow_ok & coh_ok;
    uint64_t cm = sound_mask_bool(commit);
    arena->map.norm.duty_pos_mask = sound_mux_u64(cm, dp, cur_pos);
    arena->map.norm.duty_neg_mask = sound_mux_u64(cm, dn, cur_neg);

    uint32_t f = sound_mux_u32(sound_mask_bool_u32(!coh_ok), (uint32_t)SOUND_FIBER_HOLD_INCOHERENT,
                               (uint32_t)SOUND_FIBER_COMMITTED);
    f = sound_mux_u32(sound_mask_bool_u32(!pow_ok), (uint32_t)SOUND_FIBER_HOLD_NO_POWER, f);
    f = sound_mux_u32(sound_mask_bool_u32(imm_hit), (uint32_t)SOUND_FIBER_HOLD_IMMUNITY, f);
    return sound_record_fiber(arena, (sound_fiber_t)f);
}

static inline sound_fiber_t sound_apply_declarative(SoundArena *arena, uint32_t intent_node,
                                                    uint32_t duty_slot,
                                                    bool in_constitutive_context,
                                                    bool internal_will_matches,
                                                    uint64_t utterance_hash) {
    if (arena == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    if (intent_node >= arena->map.img_hdr.num_nodes || duty_slot >= SOUND_MAX_NODES) {
        return sound_record_fiber(arena, SOUND_FIBER_FALLBACK_BOUNDS);
    }
    const SoundNodeMeta64 *nd = &arena->map.node_meta[intent_node];
    sound_fiber_t log_f =
        sound_append_phys(arena, (uint32_t)SOUND_PHYS_DECLARATIVE, intent_node, duty_slot,
                          in_constitutive_context ? UINT32_C(1) : UINT32_C(0), nd->span_start,
                          nd->span_end, utterance_hash);
    if (log_f != SOUND_FIBER_COMMITTED) {
        return sound_record_fiber(arena, log_f);
    }
    if (!in_constitutive_context) {
        return sound_record_fiber(arena, SOUND_FIBER_HOLD_MISSING_CONTEXT);
    }
    sound_fiber_t pot_f = sound_apply_potestative(arena, duty_slot, true, false, false, false);
    if (pot_f != SOUND_FIBER_COMMITTED) {
        return pot_f;
    }
    uint64_t bit = sound_bit64(duty_slot);
    uint64_t vm = sound_mask_bool(!internal_will_matches);
    uint64_t v_cur = arena->map.norm.voidable_mask;
    uint64_t r_cur = arena->map.norm.rescission_power_mask;
    arena->map.norm.voidable_mask = sound_mux_u64(vm, v_cur | bit, v_cur & (~bit));
    arena->map.norm.rescission_power_mask = sound_mux_u64(vm, r_cur | bit, r_cur & (~bit));
    return SOUND_FIBER_COMMITTED;
}

static inline sound_fiber_t sound_exercise_rescission(SoundArena *arena,
                                                      uint32_t target_intent_node,
                                                      uint32_t duty_slot,
                                                      uint64_t rescission_hash) {
    if (arena == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    if (target_intent_node >= arena->map.img_hdr.num_nodes || duty_slot >= SOUND_MAX_NODES) {
        return sound_record_fiber(arena, SOUND_FIBER_FALLBACK_BOUNDS);
    }
    uint64_t dbit = sound_bit64(duty_slot);
    if ((arena->map.norm.immunity_mask & dbit) != UINT64_C(0)) {
        return sound_record_fiber(arena, SOUND_FIBER_HOLD_IMMUNITY);
    }
    if ((arena->map.norm.rescission_power_mask & dbit) == UINT64_C(0)) {
        return sound_record_fiber(arena, SOUND_FIBER_HOLD_NO_POWER);
    }
    const SoundNodeMeta64 *nd = &arena->map.node_meta[target_intent_node];
    sound_fiber_t log_f =
        sound_append_phys(arena, (uint32_t)SOUND_PHYS_RESCISSION, target_intent_node, duty_slot,
                          UINT32_C(1), nd->span_start, nd->span_end, rescission_hash);
    if (log_f != SOUND_FIBER_COMMITTED) {
        return sound_record_fiber(arena, log_f);
    }
    arena->map.norm.revoked_mask |= sound_bit64(target_intent_node);
    arena->map.norm.voidable_mask &= (~dbit);
    arena->map.norm.rescission_power_mask &= (~dbit);
    arena->map.norm.duty_pos_mask &= (~dbit);

    sound_fiber_t ev_f = sound_eval(arena);
    if (ev_f != SOUND_FIBER_COMMITTED) {
        return ev_f;
    }
    uint64_t keep_mask = arena->map.eval_out.established_mask | arena->map.norm.immunity_mask;
    arena->map.norm.duty_pos_mask &= keep_mask;
    arena->map.norm.duty_neg_mask &= keep_mask;
    arena->map.norm.voidable_mask &= keep_mask;
    arena->map.norm.rescission_power_mask &= keep_mask;
    return SOUND_FIBER_COMMITTED;
}

static inline bool sound__validate_candidate_at(const SoundArena *arena, uint32_t cand_idx,
                                                bool sealed, const uint8_t *utterance_ptr,
                                                size_t utterance_len, sound_fiber_t *fiber_out,
                                                uint32_t *inject_reasons_out) {
    const SoundForestCtrl64 *ctrl = &arena->map.forest_ctrl;
    const SoundCandidate64 *c = &arena->map.candidate[cand_idx];
    *fiber_out = SOUND_FIBER_COMMITTED;
    *inject_reasons_out = (uint32_t)SOUND_INJECT_NONE;

    if (!(c->span_start < c->span_end && c->span_end <= ctrl->utterance_bytes) ||
        c->target_node >= arena->map.img_hdr.num_nodes || c->target_node >= SOUND_MAX_NODES ||
        c->sort > (uint8_t)SOUND_SORT_INSTITUTIONAL || c->evid > (uint8_t)SOUND_EVID_ASSUMED ||
        c->aspect > (uint8_t)SOUND_ASPECT_ACHIEVEMENT) {
        *fiber_out = SOUND_FIBER_FALLBACK_SPAN;
        return false;
    }
    if (utterance_ptr != NULL) {
        if ((size_t)c->span_end > utterance_len) {
            *fiber_out = SOUND_FIBER_FALLBACK_SPAN;
            return false;
        }
        uint64_t actual_hash = sound_fnv1a_bytes(SOUND_FNV1A_OFFSET, utterance_ptr + c->span_start,
                                                 (size_t)(c->span_end - c->span_start));
        if (actual_hash != c->payload_hash) {
            *fiber_out = SOUND_FIBER_FALLBACK_SPAN;
            *inject_reasons_out = (uint32_t)SOUND_INJECT_SPAN_HASH_MISMATCH;
            return false;
        }
    }
    uint64_t nm = sound_mask_n(arena->map.img_hdr.num_nodes);
    if (((c->presup_mask & (~nm)) != UINT64_C(0)) || ((c->premise_mask & (~nm)) != UINT64_C(0)) ||
        ((c->attack_mask & (~nm)) != UINT64_C(0))) {
        *fiber_out = SOUND_FIBER_FALLBACK_SPAN;
        return false;
    }
    if (sealed) {
        const SoundNodeMeta64 *nd = &arena->map.node_meta[c->target_node];
        uint32_t inject_reasons = (uint32_t)SOUND_INJECT_NONE;
        uint64_t tbit = sound_bit64(c->target_node);
        if ((arena->map.norm.immunity_mask & tbit) != UINT64_C(0)) {
            inject_reasons |= (uint32_t)SOUND_INJECT_IMMUNITY_SLOT;
        }
        if ((ctrl->policy_mask & tbit) != UINT64_C(0)) {
            inject_reasons |= (uint32_t)SOUND_INJECT_POLICY_SLOT;
        }
        if ((tbit & (~ctrl->bind_mask)) != UINT64_C(0)) {
            inject_reasons |= (uint32_t)SOUND_INJECT_UNBOUND_SLOT;
        }
        if (nd->sort != (uint8_t)SOUND_SORT_FACTUAL || c->sort != (uint8_t)SOUND_SORT_FACTUAL) {
            inject_reasons |= (uint32_t)SOUND_INJECT_CROSS_PLANE_SORT;
        }
        if (c->node_flags != UINT32_C(0) || c->evid == (uint8_t)SOUND_EVID_ENACTED_NORM ||
            c->force == (uint8_t)SOUND_FORCE_DECLARATIVE ||
            (nd->flags & SOUND_NODE_FLAG_CONSTITUTIVE) != UINT32_C(0)) {
            inject_reasons |= (uint32_t)SOUND_INJECT_PRIV_ESCALATION;
        }
        if ((c->presup_mask | c->premise_mask | c->attack_mask) != UINT64_C(0)) {
            inject_reasons |= (uint32_t)SOUND_INJECT_TOPOLOGY_MUTATION;
        }
        if ((ctrl->ctrl_flags & SOUND_FOREST_CTRL_NO_DIRECT_CLAIM) != UINT32_C(0) &&
            c->evid == (uint8_t)SOUND_EVID_DIRECT) {
            inject_reasons |= (uint32_t)SOUND_INJECT_DIRECT_CLAIM_FORBID;
        }
        if ((ctrl->ctrl_flags & SOUND_FOREST_CTRL_PROPOSER_LOW_EVID) != UINT32_C(0) &&
            c->evid < (uint8_t)SOUND_EVID_REPORTATIVE) {
            inject_reasons |= (uint32_t)SOUND_INJECT_EVID_CEILING;
        }
        if (inject_reasons != (uint32_t)SOUND_INJECT_NONE) {
            *fiber_out = SOUND_FIBER_FALLBACK_POLICY_VIOLATION;
            *inject_reasons_out = inject_reasons;
            return false;
        }
    }
    if (c->force == (uint8_t)SOUND_FORCE_IRREALIS) {
        *fiber_out = SOUND_FIBER_FALLBACK_IRREALIS;
        return false;
    }
    return true;
}

static inline sound_fiber_t sound__admit_forest_core(SoundArena *arena, bool single_only,
                                                     const uint8_t *utterance_ptr,
                                                     size_t utterance_len, bool use_key,
                                                     uint64_t k0, uint64_t k1, uint32_t *out_val) {
    if (out_val != NULL) {
        *out_val = single_only ? UINT32_MAX : UINT32_C(0);
    }
    if (arena == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    SoundForestCtrl64 *ctrl = &arena->map.forest_ctrl;
    bool sealed = (ctrl->ctrl_flags & SOUND_FOREST_CTRL_PREPARED_SEALED) != UINT32_C(0);
    ctrl->last_reject_reason = (uint32_t)SOUND_INJECT_NONE;
    if (sealed && sound__policy_verify_core(arena, use_key, k0, k1) != SOUND_FIBER_COMMITTED) {
        const SoundCandidate64 *c0 = &arena->map.candidate[0];
        ctrl->last_reject_reason = (uint32_t)SOUND_INJECT_POLICY_TAMPER;
        ctrl->last_admitted_node = UINT32_MAX;
        (void)sound_append_phys(arena, (uint32_t)SOUND_PHYS_INJECT_BLOCKED, c0->target_node,
                                UINT32_MAX, ctrl->last_reject_reason, c0->span_start, c0->span_end,
                                c0->payload_hash);
        return sound_record_fiber(arena, SOUND_FIBER_FALLBACK_POLICY_TAMPER);
    }
    uint32_t nc = ctrl->num_candidates;
    bool bad_count =
        single_only ? (nc != UINT32_C(1)) : (nc == UINT32_C(0) || nc > SOUND_MAX_CANDIDATES);
    if (ctrl->ambiguity_flags != UINT32_C(0) || ctrl->unresolved_anaphora != UINT32_C(0) ||
        bad_count) {
        ctrl->last_admitted_node = UINT32_MAX;
        return sound_record_fiber(arena, SOUND_FIBER_FALLBACK_CLARIFY);
    }

    /* Phase 1: Pure read-only validation over all candidates (all-or-nothing transaction) */
    uint64_t seen_targets = UINT64_C(0);
    for (uint32_t idx = 0U; idx < nc; idx++) {
        sound_fiber_t cand_f = SOUND_FIBER_COMMITTED;
        uint32_t cand_inj = (uint32_t)SOUND_INJECT_NONE;
        if (!sound__validate_candidate_at(arena, idx, sealed, utterance_ptr, utterance_len, &cand_f,
                                          &cand_inj)) {
            const SoundCandidate64 *cb = &arena->map.candidate[idx];
            ctrl->last_reject_reason = cand_inj;
            ctrl->last_admitted_node = UINT32_MAX;
            if (cand_inj != (uint32_t)SOUND_INJECT_NONE) {
                (void)sound_append_phys(arena, (uint32_t)SOUND_PHYS_INJECT_BLOCKED, cb->target_node,
                                        cb->target_node, cand_inj, cb->span_start, cb->span_end,
                                        cb->payload_hash);
            }
            return sound_record_fiber(arena, cand_f);
        }
        uint64_t tbit = sound_bit64(arena->map.candidate[idx].target_node);
        if ((seen_targets & tbit) != UINT64_C(0)) {
            const SoundCandidate64 *cb = &arena->map.candidate[idx];
            ctrl->last_reject_reason = (uint32_t)SOUND_INJECT_DUPLICATE_TARGET;
            ctrl->last_admitted_node = UINT32_MAX;
            (void)sound_append_phys(arena, (uint32_t)SOUND_PHYS_INJECT_BLOCKED, cb->target_node,
                                    cb->target_node, ctrl->last_reject_reason, cb->span_start,
                                    cb->span_end, cb->payload_hash);
            return sound_record_fiber(arena, SOUND_FIBER_FALLBACK_POLICY_VIOLATION);
        }
        seen_targets |= tbit;
    }

    /* Phase 2: Atomic commit across all validated candidate slots */
    uint32_t last_node = UINT32_MAX;
    for (uint32_t idx = 0U; idx < nc; idx++) {
        const SoundCandidate64 *c = &arena->map.candidate[idx];
        SoundNodeMeta64 *nd = &arena->map.node_meta[c->target_node];
        if (sealed) {
            nd->evid = c->evid;
            nd->aspect = c->aspect;
            nd->span_start = c->span_start;
            nd->span_end = c->span_end;
        } else {
            nd->sort = c->sort;
            nd->evid = c->evid;
            nd->aspect = c->aspect;
            nd->flags |= c->node_flags;
            nd->presup_mask |= c->presup_mask;
            nd->attack_mask |= c->attack_mask;
            nd->span_start = c->span_start;
            nd->span_end = c->span_end;
            if (nd->num_clauses < (uint8_t)SOUND_MAX_CLAUSES) {
                arena->map.node_clauses[c->target_node].clauses[nd->num_clauses] = c->premise_mask;
                nd->num_clauses++;
            }
        }
        (void)sound_append_phys(arena, (uint32_t)SOUND_PHYS_FOREST_ADMIT, c->target_node,
                                c->target_node, (uint32_t)c->force, c->span_start, c->span_end,
                                c->payload_hash);
        last_node = c->target_node;
    }
    ctrl->last_admitted_node = last_node;
    if (out_val != NULL) {
        *out_val = single_only ? last_node : nc;
    }
    return sound_record_fiber(arena, SOUND_FIBER_COMMITTED);
}

static inline sound_fiber_t sound_admit_forest(SoundArena *arena, uint32_t *admitted_node_out) {
    return sound__admit_forest_core(arena, true, NULL, 0U, false, UINT64_C(0), UINT64_C(0),
                                    admitted_node_out);
}

static inline sound_fiber_t sound_admit_forest_with_utterance(SoundArena *arena,
                                                              const uint8_t *utterance_ptr,
                                                              size_t utterance_len,
                                                              uint32_t *admitted_node_out) {
    if (utterance_ptr == NULL) {
        if (admitted_node_out != NULL) {
            *admitted_node_out = UINT32_MAX;
        }
        return sound_record_fiber(arena, SOUND_FIBER_FALLBACK_NULL);
    }
    return sound__admit_forest_core(arena, true, utterance_ptr, utterance_len, false, UINT64_C(0),
                                    UINT64_C(0), admitted_node_out);
}

static inline sound_fiber_t sound_admit_forest_batch(SoundArena *arena,
                                                     const uint8_t *utterance_ptr,
                                                     size_t utterance_len,
                                                     uint32_t *admitted_count_out) {
    return sound__admit_forest_core(arena, false, utterance_ptr, utterance_len, false, UINT64_C(0),
                                    UINT64_C(0), admitted_count_out);
}

static inline sound_fiber_t sound_admit_forest_batch_keyed(SoundArena *arena,
                                                           const uint8_t *utterance_ptr,
                                                           size_t utterance_len, uint64_t k0,
                                                           uint64_t k1,
                                                           uint32_t *admitted_count_out) {
    return sound__admit_forest_core(arena, false, utterance_ptr, utterance_len, true, k0, k1,
                                    admitted_count_out);
}

static inline sound_fiber_t sound_searu_repair_conflict(SoundArena *arena, uint64_t seed,
                                                        uint32_t max_iters) {
    if (arena == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    sound_fiber_t ev_f = sound_eval(arena);
    if (ev_f != SOUND_FIBER_COMMITTED) {
        return ev_f;
    }
    uint32_t n = arena->map.img_hdr.num_nodes;
    uint64_t nm = arena->map.eval_out.node_mask;
    uint64_t est = arena->map.eval_out.established_mask;
    uint64_t conf = arena->map.eval_out.conflict_mask;
    uint64_t imm = arena->map.norm.immunity_mask & nm;
    uint64_t factual_mask = sound_arena_factual_mask(arena);
    uint64_t pinned = (~conf) | imm;

    SoundSearuRepair64 *rep = &arena->map.searu_repair;
    if (conf == UINT64_C(0)) {
        rep->pinned_mask = est | imm;
        rep->candidate_mask = est;
        rep->retracted_mask = UINT64_C(0);
        rep->best_energy = UINT64_C(0);
        rep->iters_used = UINT32_C(0);
        rep->converged = UINT32_C(1);
        rep->seed = seed;
        rep->reserved[0] = UINT64_C(0);
        rep->reserved[1] = UINT64_C(0);
        return sound_record_fiber(arena, SOUND_FIBER_COMMITTED);
    }

    uint64_t init_canvas[4] = {est | (conf & imm), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    uint64_t kan_pin[4] = {pinned, UINT64_MAX, UINT64_MAX, UINT64_MAX};
    searu_result res;
    sound_zero_bytes(&res, sizeof(res));

    SEARU_SOLVE_INLINE(&res, n, init_canvas, kan_pin, seed, max_iters, cand, frust, energy, {
        uint64_t s = cand[0] & nm;
        uint64_t incoming_to_s = UINT64_C(0);
        for (uint32_t v = 0U; v < n; v++) {
            if ((s & sound_bit64(v)) != UINT64_C(0)) {
                incoming_to_s |= arena->map.node_meta[v].attack_mask;
            }
        }
        for (uint32_t u = 0U; u < n; u++) {
            uint64_t ub = sound_bit64(u);
            if ((s & ub) != UINT64_C(0)) {
                uint64_t hit = arena->map.node_meta[u].attack_mask & s;
                if (hit != UINT64_C(0)) {
                    energy += UINT64_C(1024);
                    frust[0] |= ub | hit;
                }
                if (!sound_node_fires(arena, u, s, s, nm, factual_mask)) {
                    energy += UINT64_C(512);
                    frust[0] |= ub;
                }
            } else if ((conf & ub) != UINT64_C(0)) {
                if ((incoming_to_s & ub) == UINT64_C(0) &&
                    sound_node_fires(arena, u, s, s, nm, factual_mask)) {
                    energy += UINT64_C(1);
                    frust[0] |= ub;
                }
            }
        }
    });

    uint64_t cand_mask = res.canvas[0] & nm;
    rep->pinned_mask = est | imm;
    rep->candidate_mask = cand_mask;
    rep->retracted_mask = (est | conf) & (~cand_mask);
    rep->best_energy = res.best_energy;
    rep->iters_used = res.iterations_run;
    rep->converged = (res.best_energy == UINT64_C(0)) ? UINT32_C(1) : UINT32_C(0);
    rep->seed = seed;
    rep->reserved[0] = UINT64_C(0);
    rep->reserved[1] = UINT64_C(0);
    return sound_record_fiber(arena, SOUND_FIBER_COMMITTED);
}

static inline sound_fiber_t sound_set_priority_weights(SoundArena *arena,
                                                       const uint8_t weights[SOUND_MAX_NODES]) {
    if (arena == NULL || weights == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    for (uint32_t i = 0U; i < SOUND_MAX_NODES; i++) {
        arena->cells[SOUND_SLOT_FLOW_AUX_BASE + SOUND_FLOW_AUX_PRIORITY_CELL].bytes[i] = weights[i];
    }
    return SOUND_FIBER_COMMITTED;
}

static inline sound_fiber_t
sound_searu_repair_conflict_weighted(SoundArena *arena, uint64_t seed, uint32_t max_iters,
                                     const uint8_t priority_weights[SOUND_MAX_NODES]) {
    if (arena == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    if (priority_weights == NULL) {
        return sound_searu_repair_conflict(arena, seed, max_iters);
    }
    sound_fiber_t ev_f = sound_eval(arena);
    if (ev_f != SOUND_FIBER_COMMITTED) {
        return ev_f;
    }
    uint32_t n = arena->map.img_hdr.num_nodes;
    uint64_t nm = arena->map.eval_out.node_mask;
    uint64_t est = arena->map.eval_out.established_mask;
    uint64_t conf = arena->map.eval_out.conflict_mask;
    uint64_t imm = arena->map.norm.immunity_mask & nm;
    uint64_t factual_mask = sound_arena_factual_mask(arena);
    uint64_t pinned = (~conf) | imm;

    SoundSearuRepair64 *rep = &arena->map.searu_repair;
    if (conf == UINT64_C(0)) {
        rep->pinned_mask = est | imm;
        rep->candidate_mask = est;
        rep->retracted_mask = UINT64_C(0);
        rep->best_energy = UINT64_C(0);
        rep->iters_used = UINT32_C(0);
        rep->converged = UINT32_C(1);
        rep->seed = seed;
        rep->reserved[0] = UINT64_C(0);
        rep->reserved[1] = UINT64_C(0);
        return sound_record_fiber(arena, SOUND_FIBER_COMMITTED);
    }

    uint8_t lw[SOUND_MAX_NODES];
    for (uint32_t i = 0U; i < SOUND_MAX_NODES; i++) {
        lw[i] = (priority_weights[i] > UINT8_C(0)) ? priority_weights[i] : UINT8_C(1);
    }

    uint64_t init_canvas[4] = {est | (conf & imm), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    uint64_t kan_pin[4] = {pinned, UINT64_MAX, UINT64_MAX, UINT64_MAX};
    searu_result res;
    sound_zero_bytes(&res, sizeof(res));

    SEARU_SOLVE_INLINE(&res, n, init_canvas, kan_pin, seed, max_iters, cand, frust, energy, {
        uint64_t s = cand[0] & nm;
        uint64_t incoming_to_s = UINT64_C(0);
        for (uint32_t v = 0U; v < n; v++) {
            if ((s & sound_bit64(v)) != UINT64_C(0)) {
                incoming_to_s |= arena->map.node_meta[v].attack_mask;
            }
        }
        for (uint32_t u = 0U; u < n; u++) {
            uint64_t ub = sound_bit64(u);
            if ((s & ub) != UINT64_C(0)) {
                uint64_t hit = arena->map.node_meta[u].attack_mask & s;
                if (hit != UINT64_C(0)) {
                    energy += UINT64_C(65536);
                    frust[0] |= ub | hit;
                }
                if (!sound_node_fires(arena, u, s, s, nm, factual_mask)) {
                    energy += UINT64_C(32768);
                    frust[0] |= ub;
                }
            } else if ((conf & ub) != UINT64_C(0)) {
                energy += (uint64_t)lw[u];
                if ((incoming_to_s & ub) == UINT64_C(0) &&
                    sound_node_fires(arena, u, s, s, nm, factual_mask)) {
                    energy += UINT64_C(16384);
                    frust[0] |= ub;
                } else {
                    for (uint32_t b = 0U; b < n; b++) {
                        uint64_t bb = sound_bit64(b);
                        if (((s & bb) != UINT64_C(0)) && ((conf & bb) != UINT64_C(0)) &&
                            ((arena->map.node_meta[u].attack_mask & bb) != UINT64_C(0)) &&
                            (lw[u] > lw[b])) {
                            frust[0] |= ub | bb;
                        }
                    }
                }
            }
        }
    });

    uint64_t cand_mask = res.canvas[0] & nm;
    uint64_t inc_final = UINT64_C(0);
    for (uint32_t v = 0U; v < n; v++) {
        if ((cand_mask & sound_bit64(v)) != UINT64_C(0)) {
            inc_final |= arena->map.node_meta[v].attack_mask;
        }
    }
    bool hard_ok = true;
    for (uint32_t u = 0U; u < n; u++) {
        uint64_t ub = sound_bit64(u);
        if ((cand_mask & ub) != UINT64_C(0)) {
            if ((arena->map.node_meta[u].attack_mask & cand_mask) != UINT64_C(0) ||
                !sound_node_fires(arena, u, cand_mask, cand_mask, nm, factual_mask)) {
                hard_ok = false;
            }
        } else if ((conf & ub) != UINT64_C(0)) {
            if ((inc_final & ub) == UINT64_C(0) &&
                sound_node_fires(arena, u, cand_mask, cand_mask, nm, factual_mask)) {
                hard_ok = false;
            }
        }
    }

    rep->pinned_mask = est | imm;
    rep->candidate_mask = cand_mask;
    rep->retracted_mask = (est | conf) & (~cand_mask);
    rep->best_energy = res.best_energy;
    rep->iters_used = res.iterations_run;
    rep->converged = hard_ok ? UINT32_C(1) : UINT32_C(0);
    rep->seed = seed;
    rep->reserved[0] = UINT64_C(0);
    rep->reserved[1] = UINT64_C(0);
    return sound_record_fiber(arena, SOUND_FIBER_COMMITTED);
}

static inline uint64_t sound_chart_glue_digest(const SoundChartGlue64 *g) {
    if (g == NULL) {
        return UINT64_C(0);
    }
    uint8_t buf[48];
    sound_write_le64(buf + 0U, g->magic_ver);
    sound_write_le64(buf + 8U, g->src_merkle_root);
    sound_write_le64(buf + 16U, g->src_policy_hash);
    sound_write_le64(buf + 24U, g->src_export_mask);
    sound_write_le64(buf + 32U, g->dst_import_mask);
    sound_write_le64(buf + 40U, g->epoch_flags);
    return sound_fnv1a_bytes(SOUND_FNV1A_OFFSET, buf, sizeof(buf));
}

static inline sound_fiber_t sound_chart_export_port(const SoundArena *src, uint64_t export_mask,
                                                    SoundChartGlue64 *port_out) {
    if (src == NULL || port_out == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    sound_fiber_t ver_f = sound_arena_verify(src);
    if (ver_f != SOUND_FIBER_COMMITTED) {
        return ver_f;
    }
    uint32_t req_flags = SOUND_EVAL_FLAG_PARTITION_OK | SOUND_EVAL_FLAG_CONFLICT_FREE_OK |
                         SOUND_EVAL_FLAG_HOHFELD_OK | SOUND_EVAL_FLAG_FIXPOINT_OK;
    if ((src->map.eval_out.eval_flags & req_flags) != req_flags || !sound_hohfeld_coherent(src)) {
        return SOUND_FIBER_HOLD_INCOHERENT;
    }
    uint64_t nm = sound_mask_n(src->map.img_hdr.num_nodes);
    if (export_mask == UINT64_C(0) || (export_mask & (~nm)) != UINT64_C(0)) {
        return SOUND_FIBER_FALLBACK_BOUNDS;
    }
    if ((export_mask & src->map.eval_out.conflict_mask) != UINT64_C(0) ||
        (export_mask & (~src->map.eval_out.established_mask)) != UINT64_C(0)) {
        return SOUND_FIBER_HOLD_INCOHERENT;
    }
    sound_zero_bytes(port_out, sizeof(*port_out));
    port_out->magic_ver = SOUND_GLUE_MAGIC_VER;
    port_out->src_merkle_root = src->map.img_hdr.merkle_root;
    port_out->src_policy_hash = src->map.forest_ctrl.policy_hash;
    port_out->src_export_mask = export_mask;
    port_out->dst_import_mask = UINT64_C(0);
    port_out->epoch_flags =
        (((uint64_t)src->map.eval_out.eval_epoch) << 32U) | (uint64_t)src->map.img_hdr.phys_len;
    port_out->glue_hash = sound_chart_glue_digest(port_out);
    return SOUND_FIBER_COMMITTED;
}

static inline sound_fiber_t
sound__chart_glue_import_core(SoundArena *dst, const SoundArena *src, uint64_t src_export_mask,
                              const uint8_t dst_slot_for_src[SOUND_MAX_NODES], bool use_key,
                              uint64_t k0, uint64_t k1) {
    if (dst == NULL || src == NULL || dst_slot_for_src == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    SoundChartGlue64 glue;
    sound_fiber_t exp_f = sound_chart_export_port(src, src_export_mask, &glue);
    if (exp_f != SOUND_FIBER_COMMITTED) {
        return sound_record_fiber(dst, exp_f);
    }
    if (dst->map.img_hdr.num_nodes > SOUND_MAX_NODES) {
        return sound_record_fiber(dst, SOUND_FIBER_FALLBACK_BOUNDS);
    }
    SoundForestCtrl64 *ctrl = &dst->map.forest_ctrl;
    bool sealed = (ctrl->ctrl_flags & SOUND_FOREST_CTRL_PREPARED_SEALED) != UINT32_C(0);
    if (sealed && sound__policy_verify_core(dst, use_key, k0, k1) != SOUND_FIBER_COMMITTED) {
        ctrl->last_reject_reason = (uint32_t)SOUND_INJECT_POLICY_TAMPER;
        return sound_record_fiber(dst, SOUND_FIBER_FALLBACK_POLICY_TAMPER);
    }

    uint64_t dst_import_mask = UINT64_C(0);
    uint32_t src_n = src->map.img_hdr.num_nodes;
    uint32_t dst_n = dst->map.img_hdr.num_nodes;
    for (uint32_t s = 0U; s < src_n; s++) {
        if ((src_export_mask & sound_bit64(s)) == UINT64_C(0)) {
            continue;
        }
        uint32_t d = (uint32_t)dst_slot_for_src[s];
        if (d >= dst_n) {
            return sound_record_fiber(dst, SOUND_FIBER_FALLBACK_BOUNDS);
        }
        uint64_t dbit = sound_bit64(d);
        if ((dst_import_mask & dbit) != UINT64_C(0)) {
            ctrl->last_reject_reason = (uint32_t)SOUND_INJECT_DUPLICATE_TARGET;
            return sound_record_fiber(dst, SOUND_FIBER_FALLBACK_POLICY_VIOLATION);
        }
        uint32_t inj = (uint32_t)SOUND_INJECT_NONE;
        if ((dst->map.norm.immunity_mask & dbit) != UINT64_C(0)) {
            inj |= (uint32_t)SOUND_INJECT_IMMUNITY_SLOT;
        }
        if (sealed) {
            if ((ctrl->policy_mask & dbit) != UINT64_C(0)) {
                inj |= (uint32_t)SOUND_INJECT_POLICY_SLOT;
            }
            if ((dbit & (~ctrl->bind_mask)) != UINT64_C(0)) {
                inj |= (uint32_t)SOUND_INJECT_UNBOUND_SLOT;
            }
        }
        const SoundNodeMeta64 *dnd = &dst->map.node_meta[d];
        if (dnd->sort != (uint8_t)SOUND_SORT_FACTUAL) {
            inj |= (uint32_t)SOUND_INJECT_CROSS_PLANE_SORT;
        }
        if ((dnd->flags & SOUND_NODE_FLAG_CONSTITUTIVE) != UINT32_C(0)) {
            inj |= (uint32_t)SOUND_INJECT_PRIV_ESCALATION;
        }
        if (inj != (uint32_t)SOUND_INJECT_NONE) {
            ctrl->last_reject_reason = inj;
            return sound_record_fiber(dst, SOUND_FIBER_FALLBACK_POLICY_VIOLATION);
        }
        dst_import_mask |= dbit;
    }

    for (uint32_t s = 0U; s < src_n; s++) {
        if ((src_export_mask & sound_bit64(s)) != UINT64_C(0)) {
            uint32_t d = (uint32_t)dst_slot_for_src[s];
            dst->map.node_meta[d].evid = (uint8_t)SOUND_EVID_DIRECT;
        }
    }
    glue.dst_import_mask = dst_import_mask;
    glue.glue_hash = sound_chart_glue_digest(&glue);
    dst->map.flow_aux[SOUND_FLOW_AUX_GLUE_CELL][0] = glue.magic_ver;
    dst->map.flow_aux[SOUND_FLOW_AUX_GLUE_CELL][1] = glue.src_merkle_root;
    dst->map.flow_aux[SOUND_FLOW_AUX_GLUE_CELL][2] = glue.src_policy_hash;
    dst->map.flow_aux[SOUND_FLOW_AUX_GLUE_CELL][3] = glue.src_export_mask;
    dst->map.flow_aux[SOUND_FLOW_AUX_GLUE_CELL][4] = glue.dst_import_mask;
    dst->map.flow_aux[SOUND_FLOW_AUX_GLUE_CELL][5] = glue.glue_hash;
    dst->map.flow_aux[SOUND_FLOW_AUX_GLUE_CELL][6] = glue.epoch_flags;
    dst->map.flow_aux[SOUND_FLOW_AUX_GLUE_CELL][7] = glue.reserved;

    ctrl->last_reject_reason = (uint32_t)SOUND_INJECT_NONE;
    (void)sound_append_phys(dst, (uint32_t)SOUND_PHYS_CHART_GLUE, dst_n, src_n,
                            (uint32_t)glue.epoch_flags, UINT32_C(0), UINT32_C(0), glue.glue_hash);
    return sound_record_fiber(dst, SOUND_FIBER_COMMITTED);
}

static inline sound_fiber_t
sound_chart_glue_import(SoundArena *dst, const SoundArena *src, uint64_t src_export_mask,
                        const uint8_t dst_slot_for_src[SOUND_MAX_NODES]) {
    return sound__chart_glue_import_core(dst, src, src_export_mask, dst_slot_for_src, false,
                                         UINT64_C(0), UINT64_C(0));
}

static inline sound_fiber_t
sound_chart_glue_import_keyed(SoundArena *dst, const SoundArena *src, uint64_t src_export_mask,
                              const uint8_t dst_slot_for_src[SOUND_MAX_NODES], uint64_t k0,
                              uint64_t k1) {
    return sound__chart_glue_import_core(dst, src, src_export_mask, dst_slot_for_src, true, k0, k1);
}

static inline sound_fiber_t sound_chart_glue_verify(const SoundArena *dst, const SoundArena *src) {
    if (dst == NULL || src == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    sound_fiber_t src_ver = sound_arena_verify(src);
    if (src_ver != SOUND_FIBER_COMMITTED) {
        return src_ver;
    }
    SoundChartGlue64 g;
    g.magic_ver = dst->map.flow_aux[SOUND_FLOW_AUX_GLUE_CELL][0];
    g.src_merkle_root = dst->map.flow_aux[SOUND_FLOW_AUX_GLUE_CELL][1];
    g.src_policy_hash = dst->map.flow_aux[SOUND_FLOW_AUX_GLUE_CELL][2];
    g.src_export_mask = dst->map.flow_aux[SOUND_FLOW_AUX_GLUE_CELL][3];
    g.dst_import_mask = dst->map.flow_aux[SOUND_FLOW_AUX_GLUE_CELL][4];
    g.glue_hash = dst->map.flow_aux[SOUND_FLOW_AUX_GLUE_CELL][5];
    g.epoch_flags = dst->map.flow_aux[SOUND_FLOW_AUX_GLUE_CELL][6];
    g.reserved = dst->map.flow_aux[SOUND_FLOW_AUX_GLUE_CELL][7];

    if (g.magic_ver != SOUND_GLUE_MAGIC_VER) {
        return SOUND_FIBER_FALLBACK_FORMAT;
    }
    if (g.glue_hash != sound_chart_glue_digest(&g) ||
        g.src_merkle_root != src->map.img_hdr.merkle_root ||
        g.src_policy_hash != src->map.forest_ctrl.policy_hash) {
        return SOUND_FIBER_FALLBACK_MAC;
    }
    uint64_t src_nm = sound_mask_n(src->map.img_hdr.num_nodes);
    if (g.src_export_mask == UINT64_C(0) || g.dst_import_mask == UINT64_C(0) ||
        (g.src_export_mask & (~src_nm)) != UINT64_C(0)) {
        return SOUND_FIBER_FALLBACK_BOUNDS;
    }
    if ((g.src_export_mask & src->map.eval_out.conflict_mask) != UINT64_C(0) ||
        (g.src_export_mask & (~src->map.eval_out.established_mask)) != UINT64_C(0)) {
        return SOUND_FIBER_HOLD_INCOHERENT;
    }
    return SOUND_FIBER_COMMITTED;
}

typedef struct {
    uint16_t op_kind[FLOW_IR_MAX_OPS];
    uint8_t op_nins[FLOW_IR_MAX_OPS];
    uint8_t val_w1[FLOW_IR_MAX_VALS];
    uint16_t op_ins[FLOW_IR_MAX_OPS][FLOW_IR_MAX_INS];
    uint64_t op_p0[FLOW_IR_MAX_OPS];
    uint32_t num_ops;
    uint32_t num_consts;
    uint64_t const_vals[64];
    uint16_t const_ids[64];
    bool overflow;
} sound__pbld_t;

static inline uint16_t sound__pbld_op(sound__pbld_t *b, uint16_t kind, uint8_t nins, bool w1,
                                      uint16_t i0, uint16_t i1, uint16_t i2, uint64_t p0) {
    if (b->overflow || b->num_ops >= FLOW_IR_MAX_OPS) {
        b->overflow = true;
        return UINT16_C(0);
    }
    uint32_t idx = b->num_ops++;
    b->op_kind[idx] = kind;
    b->op_nins[idx] = nins;
    b->op_ins[idx][0] = i0;
    b->op_ins[idx][1] = i1;
    b->op_ins[idx][2] = i2;
    b->op_ins[idx][3] = UINT16_C(0);
    b->op_ins[idx][4] = UINT16_C(0);
    b->op_ins[idx][5] = UINT16_C(0);
    b->op_p0[idx] = p0;
    uint16_t vid = (uint16_t)(1U + idx);
    b->val_w1[vid] = w1 ? UINT8_C(1) : UINT8_C(0);
    return vid;
}

static inline uint16_t sound__pbld_concat_or(sound__pbld_t *b, const uint16_t *bits,
                                             uint32_t count) {
    if (count == 0U || b->overflow) {
        return UINT16_C(0);
    }
    if (count == 1U) {
        return bits[0];
    }
    if (b->num_ops + UINT32_C(2) > FLOW_IR_MAX_OPS) {
        b->overflow = true;
        return UINT16_C(0);
    }
    uint32_t use_n = (count <= FLOW_IR_MAX_INS) ? count : FLOW_IR_MAX_INS;
    uint32_t idx = b->num_ops++;
    b->op_kind[idx] = (uint16_t)FLOW_IR_OP_CONCAT;
    b->op_nins[idx] = (uint8_t)use_n;
    for (uint32_t j = 0U; j < FLOW_IR_MAX_INS; j++) {
        b->op_ins[idx][j] = (j < use_n) ? bits[j] : UINT16_C(0);
    }
    b->op_p0[idx] = UINT64_C(0);
    uint16_t cv = (uint16_t)(1U + idx);
    b->val_w1[cv] = UINT8_C(0);
    return sound__pbld_op(b, (uint16_t)FLOW_IR_OP_OR_REDUCE, UINT8_C(1), true, cv, UINT16_C(0),
                          UINT16_C(0), UINT64_C(0));
}

static inline uint16_t sound__pbld_const(sound__pbld_t *b, uint64_t val) {
    for (uint32_t i = 0U; i < b->num_consts; i++) {
        if (b->const_vals[i] == val) {
            return b->const_ids[i];
        }
    }
    uint16_t vid = sound__pbld_op(b, (uint16_t)FLOW_IR_OP_CONST_U64, UINT8_C(0), false, UINT16_C(0),
                                  UINT16_C(0), UINT16_C(0), val);
    if (!b->overflow && b->num_consts < 64U) {
        b->const_vals[b->num_consts] = val;
        b->const_ids[b->num_consts] = vid;
        b->num_consts++;
    }
    return vid;
}

static inline uint16_t sound__pbld_gamma(sound__pbld_t *b, const SoundArena *arena, uint32_t n,
                                         uint64_t node_mask, uint64_t factual_mask,
                                         uint64_t bind_mask, uint64_t root_mask,
                                         uint64_t plain_bind_mask, const uint8_t *der_order,
                                         uint32_t n_der, uint16_t zero_id, uint16_t facts_in_id,
                                         bool zero_blocked, uint16_t blocked_id) {
    uint16_t acc_id = (root_mask != UINT64_C(0)) ? sound__pbld_const(b, root_mask) : zero_id;
    if (plain_bind_mask != UINT64_C(0)) {
        uint16_t pbm_id = sound__pbld_const(b, plain_bind_mask);
        uint16_t b_on = sound__pbld_op(b, (uint16_t)FLOW_IR_OP_AND_VEC, UINT8_C(2), false,
                                       facts_in_id, pbm_id, UINT16_C(0), UINT64_C(0));
        acc_id = (root_mask != UINT64_C(0))
                     ? sound__pbld_op(b, (uint16_t)FLOW_IR_OP_XOR_VEC, UINT8_C(2), false, acc_id,
                                      b_on, UINT16_C(0), UINT64_C(0))
                     : b_on;
    }

    uint16_t not_acc_id = UINT16_C(0);
    bool not_valid = false;

    for (uint32_t di = 0U; di < n_der && !b->overflow; di++) {
        uint32_t u = (uint32_t)der_order[di];
        uint64_t ubit = sound_bit64(u);
        const SoundNodeMeta64 *nd = &arena->map.node_meta[u];
        bool is_bind = (bind_mask & ubit) != UINT64_C(0);

        uint16_t conds[4];
        uint32_t n_cond = 0U;

        if (is_bind) {
            uint16_t ubit_id = sound__pbld_const(b, ubit);
            uint16_t masked = sound__pbld_op(b, (uint16_t)FLOW_IR_OP_AND_VEC, UINT8_C(2), false,
                                             facts_in_id, ubit_id, UINT16_C(0), UINT64_C(0));
            conds[n_cond++] = sound__pbld_op(b, (uint16_t)FLOW_IR_OP_OR_REDUCE, UINT8_C(1), true,
                                             masked, UINT16_C(0), UINT16_C(0), UINT64_C(0));
        } else {
            uint16_t cl_bits[SOUND_MAX_CLAUSES];
            uint32_t n_cl = 0U;
            bool has_zero_cl = false;
            uint32_t nc =
                (nd->num_clauses <= SOUND_MAX_CLAUSES) ? nd->num_clauses : SOUND_MAX_CLAUSES;
            for (uint32_t c = 0U; c < nc; c++) {
                uint64_t cm = arena->map.node_clauses[u].clauses[c];
                if (sound_clause_valid(arena, u, cm, node_mask, factual_mask, NULL)) {
                    if (cm == UINT64_C(0)) {
                        has_zero_cl = true;
                        break;
                    }
                    if (!not_valid) {
                        not_acc_id =
                            sound__pbld_op(b, (uint16_t)FLOW_IR_OP_NOT_VEC, UINT8_C(1), false,
                                           acc_id, UINT16_C(0), UINT16_C(0), UINT64_C(0));
                        not_valid = true;
                    }
                    uint16_t cm_id = sound__pbld_const(b, cm);
                    uint16_t miss =
                        sound__pbld_op(b, (uint16_t)FLOW_IR_OP_AND_VEC, UINT8_C(2), false,
                                       not_acc_id, cm_id, UINT16_C(0), UINT64_C(0));
                    cl_bits[n_cl++] =
                        sound__pbld_op(b, (uint16_t)FLOW_IR_OP_EQ_ZERO, UINT8_C(1), true, miss,
                                       UINT16_C(0), UINT16_C(0), UINT64_C(0));
                }
            }
            if (!has_zero_cl) {
                if (n_cl == 0U) {
                    continue;
                }
                if (n_cl <= FLOW_IR_MAX_INS) {
                    conds[n_cond++] = sound__pbld_concat_or(b, cl_bits, n_cl);
                } else {
                    uint16_t lo = sound__pbld_concat_or(b, cl_bits, FLOW_IR_MAX_INS);
                    uint16_t hi =
                        sound__pbld_concat_or(b, cl_bits + FLOW_IR_MAX_INS, n_cl - FLOW_IR_MAX_INS);
                    uint16_t pair_bits[2] = {lo, hi};
                    conds[n_cond++] = sound__pbld_concat_or(b, pair_bits, 2U);
                }
            }
        }

        if (nd->presup_mask != UINT64_C(0)) {
            if (!not_valid) {
                not_acc_id = sound__pbld_op(b, (uint16_t)FLOW_IR_OP_NOT_VEC, UINT8_C(1), false,
                                            acc_id, UINT16_C(0), UINT16_C(0), UINT64_C(0));
            }
            uint16_t pm_id = sound__pbld_const(b, nd->presup_mask);
            uint16_t miss_p = sound__pbld_op(b, (uint16_t)FLOW_IR_OP_AND_VEC, UINT8_C(2), false,
                                             not_acc_id, pm_id, UINT16_C(0), UINT64_C(0));
            conds[n_cond++] = sound__pbld_op(b, (uint16_t)FLOW_IR_OP_EQ_ZERO, UINT8_C(1), true,
                                             miss_p, UINT16_C(0), UINT16_C(0), UINT64_C(0));
        }

        if (!zero_blocked && nd->attack_mask != UINT64_C(0)) {
            uint16_t am_id = sound__pbld_const(b, nd->attack_mask);
            uint16_t hit_a = sound__pbld_op(b, (uint16_t)FLOW_IR_OP_AND_VEC, UINT8_C(2), false,
                                            blocked_id, am_id, UINT16_C(0), UINT64_C(0));
            conds[n_cond++] = sound__pbld_op(b, (uint16_t)FLOW_IR_OP_EQ_ZERO, UINT8_C(1), true,
                                             hit_a, UINT16_C(0), UINT16_C(0), UINT64_C(0));
        }

        uint16_t ubit_id2 = sound__pbld_const(b, ubit);
        if (n_cond == 0U) {
            acc_id = sound__pbld_op(b, (uint16_t)FLOW_IR_OP_XOR_VEC, UINT8_C(2), false, acc_id,
                                    ubit_id2, UINT16_C(0), UINT64_C(0));
            not_valid = false;
        } else {
            uint16_t fire_c = conds[0];
            for (uint32_t ci = 1U; ci < n_cond; ci++) {
                fire_c = sound__pbld_op(b, (uint16_t)FLOW_IR_OP_AND_VEC, UINT8_C(2), true, fire_c,
                                        conds[ci], UINT16_C(0), UINT64_C(0));
            }
            uint16_t bit_term = sound__pbld_op(b, (uint16_t)FLOW_IR_OP_CTRL_SEL, UINT8_C(3), false,
                                               fire_c, zero_id, ubit_id2, UINT64_C(0));
            acc_id = sound__pbld_op(b, (uint16_t)FLOW_IR_OP_XOR_VEC, UINT8_C(2), false, acc_id,
                                    bit_term, UINT16_C(0), UINT64_C(0));
            not_valid = false;
        }
    }
    (void)n;
    return acc_id;
}

static inline sound_fiber_t sound_lift_policy_eval_to_fir(const SoundArena *arena,
                                                          const char *prog_name,
                                                          uint32_t unroll_alt_steps,
                                                          uint8_t *out_buf, size_t out_cap,
                                                          size_t *out_len) {
    if (arena == NULL || out_buf == NULL || out_len == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    *out_len = 0U;
    uint32_t n = arena->map.img_hdr.num_nodes;
    if (n == 0U || n > SOUND_MAX_NODES) {
        return SOUND_FIBER_FALLBACK_BOUNDS;
    }

    uint64_t node_mask = sound_mask_n(n);
    uint64_t factual_mask = sound_arena_factual_mask(arena);
    uint64_t rev_pen = (arena->map.norm.revoked_mask | arena->map.norm.penumbra_mask) & node_mask;
    uint64_t pen = arena->map.norm.penumbra_mask & node_mask;
    uint64_t non_pen = node_mask & (~pen);

    bool sealed =
        (arena->map.forest_ctrl.ctrl_flags & SOUND_FOREST_CTRL_PREPARED_SEALED) != UINT32_C(0);
    uint64_t bind_mask = UINT64_C(0);
    if (sealed) {
        bind_mask = arena->map.forest_ctrl.bind_mask & node_mask;
    } else {
        for (uint32_t u = 0U; u < n; u++) {
            const SoundNodeMeta64 *nd = &arena->map.node_meta[u];
            if (nd->sort == (uint8_t)SOUND_SORT_FACTUAL &&
                (nd->flags & SOUND_NODE_FLAG_CONSTITUTIVE) == UINT32_C(0) &&
                nd->num_clauses == UINT8_C(1) &&
                arena->map.node_clauses[u].clauses[0] == UINT64_C(0)) {
                bind_mask |= sound_bit64(u);
            }
        }
    }

    uint64_t root_mask = UINT64_C(0);
    uint64_t plain_bind_mask = UINT64_C(0);
    uint64_t der_cand_mask = UINT64_C(0);
    uint64_t all_attackers = UINT64_C(0);

    for (uint32_t u = 0U; u < n; u++) {
        uint64_t ubit = sound_bit64(u);
        if ((rev_pen & ubit) != UINT64_C(0)) {
            continue;
        }
        const SoundNodeMeta64 *nd = &arena->map.node_meta[u];
        all_attackers |= (nd->attack_mask & node_mask);
        bool is_bind = (bind_mask & ubit) != UINT64_C(0);
        if (is_bind) {
            if (nd->attack_mask == UINT64_C(0) && nd->presup_mask == UINT64_C(0)) {
                plain_bind_mask |= ubit;
            } else {
                der_cand_mask |= ubit;
            }
            continue;
        }
        bool has_any_valid = false;
        bool has_zero_valid = false;
        uint32_t nc = (nd->num_clauses <= SOUND_MAX_CLAUSES) ? nd->num_clauses : SOUND_MAX_CLAUSES;
        for (uint32_t c = 0U; c < nc; c++) {
            uint64_t cm = arena->map.node_clauses[u].clauses[c];
            if (sound_clause_valid(arena, u, cm, node_mask, factual_mask, NULL)) {
                has_any_valid = true;
                if (cm == UINT64_C(0)) {
                    has_zero_valid = true;
                }
            }
        }
        if (!has_any_valid) {
            continue;
        }
        if (has_zero_valid && nd->attack_mask == UINT64_C(0) && nd->presup_mask == UINT64_C(0)) {
            root_mask |= ubit;
        } else {
            der_cand_mask |= ubit;
        }
    }

    /* Topological order on positive dependencies inside der_cand_mask */
    uint8_t der_order[SOUND_MAX_NODES];
    uint32_t n_der = 0U;
    uint64_t satisfied = root_mask | plain_bind_mask;
    uint64_t placed = UINT64_C(0);
    for (uint32_t pass = 0U; pass < n; pass++) {
        bool progressed = false;
        for (uint32_t u = 0U; u < n; u++) {
            uint64_t ubit = sound_bit64(u);
            if ((der_cand_mask & ubit) == UINT64_C(0) || (placed & ubit) != UINT64_C(0)) {
                continue;
            }
            const SoundNodeMeta64 *nd = &arena->map.node_meta[u];
            uint64_t pos_deps = nd->presup_mask & der_cand_mask;
            uint32_t nc =
                (nd->num_clauses <= SOUND_MAX_CLAUSES) ? nd->num_clauses : SOUND_MAX_CLAUSES;
            for (uint32_t c = 0U; c < nc; c++) {
                uint64_t cm = arena->map.node_clauses[u].clauses[c];
                if ((bind_mask & ubit) == UINT64_C(0) &&
                    sound_clause_valid(arena, u, cm, node_mask, factual_mask, NULL)) {
                    pos_deps |= (cm & der_cand_mask);
                }
            }
            if ((pos_deps & (~satisfied)) == UINT64_C(0)) {
                der_order[n_der++] = (uint8_t)u;
                placed |= ubit;
                satisfied |= ubit;
                progressed = true;
            }
        }
        if (!progressed) {
            break;
        }
    }
    for (uint32_t u = 0U; u < n; u++) {
        uint64_t ubit = sound_bit64(u);
        if ((der_cand_mask & ubit) != UINT64_C(0) && (placed & ubit) == UINT64_C(0)) {
            der_order[n_der++] = (uint8_t)u;
        }
    }

    uint32_t alt_rounds = unroll_alt_steps;
    if (alt_rounds == 0U) {
        bool nested_attack = false;
        for (uint32_t u = 0U; u < n; u++) {
            uint64_t ubit = sound_bit64(u);
            if (arena->map.node_meta[u].attack_mask != UINT64_C(0) &&
                (all_attackers & ubit) != UINT64_C(0)) {
                nested_attack = true;
                break;
            }
        }
        alt_rounds = nested_attack ? UINT32_C(2) : UINT32_C(1);
    } else if (alt_rounds > UINT32_C(4)) {
        alt_rounds = UINT32_C(4);
    }

    sound__pbld_t b;
    b.num_ops = 0U;
    b.num_consts = 0U;
    b.overflow = false;
    b.val_w1[0] = UINT8_C(0);

    uint16_t zero_id = sound__pbld_const(&b, UINT64_C(0));
    uint16_t cur_i_id = zero_id;
    uint16_t p_star_id = zero_id;
    uint16_t i_star_id = zero_id;

    if (all_attackers == UINT64_C(0) && unroll_alt_steps == UINT32_C(0)) {
        p_star_id = sound__pbld_gamma(&b, arena, n, node_mask, factual_mask, bind_mask, root_mask,
                                      plain_bind_mask, der_order, n_der, zero_id, UINT16_C(0), true,
                                      zero_id);
        i_star_id = p_star_id;
    } else {
        for (uint32_t step = 0U; step < alt_rounds && !b.overflow; step++) {
            uint16_t p_tmp = sound__pbld_gamma(&b, arena, n, node_mask, factual_mask, bind_mask,
                                               root_mask, plain_bind_mask, der_order, n_der,
                                               zero_id, UINT16_C(0), (step == 0U), cur_i_id);
            cur_i_id = sound__pbld_gamma(&b, arena, n, node_mask, factual_mask, bind_mask,
                                         root_mask, plain_bind_mask, der_order, n_der, zero_id,
                                         UINT16_C(0), false, p_tmp);
        }
        if (!b.overflow) {
            p_star_id = sound__pbld_gamma(&b, arena, n, node_mask, factual_mask, bind_mask,
                                          root_mask, plain_bind_mask, der_order, n_der, zero_id,
                                          UINT16_C(0), false, cur_i_id);
            i_star_id = sound__pbld_gamma(&b, arena, n, node_mask, factual_mask, bind_mask,
                                          root_mask, plain_bind_mask, der_order, n_der, zero_id,
                                          UINT16_C(0), false, p_star_id);
        }
    }

    uint16_t non_pen_id = sound__pbld_const(&b, non_pen);
    uint16_t pen_id = sound__pbld_const(&b, pen);
    uint16_t est_id = sound__pbld_op(&b, (uint16_t)FLOW_IR_OP_AND_VEC, UINT8_C(2), false, i_star_id,
                                     non_pen_id, UINT16_C(0), UINT64_C(0));
    uint16_t not_i_id = sound__pbld_op(&b, (uint16_t)FLOW_IR_OP_NOT_VEC, UINT8_C(1), false,
                                       i_star_id, UINT16_C(0), UINT16_C(0), UINT64_C(0));
    uint16_t p_min_i = sound__pbld_op(&b, (uint16_t)FLOW_IR_OP_AND_VEC, UINT8_C(2), false,
                                      p_star_id, not_i_id, UINT16_C(0), UINT64_C(0));
    uint16_t conf_id = sound__pbld_op(&b, (uint16_t)FLOW_IR_OP_AND_VEC, UINT8_C(2), false, p_min_i,
                                      non_pen_id, UINT16_C(0), UINT64_C(0));
    uint16_t not_p_id = sound__pbld_op(&b, (uint16_t)FLOW_IR_OP_NOT_VEC, UINT8_C(1), false,
                                       p_star_id, UINT16_C(0), UINT16_C(0), UINT64_C(0));
    uint16_t def_id = sound__pbld_op(&b, (uint16_t)FLOW_IR_OP_AND_VEC, UINT8_C(2), false,
                                     non_pen_id, not_p_id, UINT16_C(0), UINT64_C(0));

    if (b.overflow) {
        return SOUND_FIBER_FALLBACK_BOUNDS;
    }
    uint32_t num_vals = UINT32_C(1) + b.num_ops;
    size_t need_bytes = FLOW_IR_WIRE_BYTES(num_vals, 1U, 4U, b.num_ops, 0U);
    if (out_cap < need_bytes) {
        return SOUND_FIBER_FALLBACK_BOUNDS;
    }

    const char *effective_name =
        (prog_name != NULL && prog_name[0] != '\0') ? prog_name : "sound_policy_eval";
    flow_ir_wire_zero(out_buf, need_bytes);
    flow_ir_wire_write_header(out_buf, FLOW_IR_MULT_LINEAR, FLOW_IR_FLAG_CLEAN, num_vals,
                              UINT32_C(1), UINT32_C(4), b.num_ops, UINT32_C(0), effective_name);

    size_t off = FLOW_IR_HEADER_BYTES;
    flow_ir_wire_write_val(out_buf + off, UINT16_C(64), UINT8_C(0), "facts_in");
    off += FLOW_IR_VAL_WIRE_B;
    for (uint32_t i = 0U; i < b.num_ops; i++) {
        uint16_t vw = (b.val_w1[1U + i] != UINT8_C(0)) ? UINT16_C(1) : UINT16_C(64);
        flow_ir_wire_write_val(out_buf + off, vw, UINT8_C(0), "v");
        off += FLOW_IR_VAL_WIRE_B;
    }

    flow_ir_wire_write_le32(out_buf + off, UINT32_C(0));
    off += FLOW_IR_WIRE_PAD8_U32_BYTES(1U);

    uint32_t outs[4] = {(uint32_t)est_id, (uint32_t)def_id, (uint32_t)conf_id, (uint32_t)pen_id};
    for (uint32_t i = 0U; i < 4U; i++) {
        flow_ir_wire_write_le32(out_buf + off + ((size_t)i * 4U), outs[i]);
    }
    off += FLOW_IR_WIRE_PAD8_U32_BYTES(4U);

    for (uint32_t i = 0U; i < b.num_ops; i++) {
        uint32_t ins32[FLOW_IR_MAX_INS];
        for (uint32_t j = 0U; j < FLOW_IR_MAX_INS; j++) {
            ins32[j] = (uint32_t)b.op_ins[i][j];
        }
        flow_ir_wire_write_op(out_buf + off, b.op_kind[i], (uint16_t)b.op_nins[i], UINT32_C(1) + i,
                              ins32, UINT32_C(0), UINT32_C(0), b.op_p0[i], UINT64_C(0), UINT64_C(0),
                              UINT64_C(0));
        off += FLOW_IR_OP_WIRE_B;
    }

    *out_len = flow_ir_wire_seal_trailer(out_buf, off);
    return SOUND_FIBER_COMMITTED;
}

typedef struct {
    uint16_t kind;
    uint16_t num_ins;
    uint16_t width;
    uint32_t ins[3];
    const char *name;
} sound__fir_op_desc_t;

static inline sound_fiber_t sound_lift_to_fir(const SoundArena *arena, const char *prog_name,
                                              uint32_t fir_flags, uint8_t *out_buf, size_t out_cap,
                                              size_t *out_len) {
    if (arena == NULL || out_buf == NULL || out_len == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    if (out_cap < (size_t)SOUND_FIR_BYTES) {
        return SOUND_FIBER_FALLBACK_BOUNDS;
    }
    uint32_t req_flags = SOUND_EVAL_FLAG_PARTITION_OK | SOUND_EVAL_FLAG_CONFLICT_FREE_OK |
                         SOUND_EVAL_FLAG_HOHFELD_OK | SOUND_EVAL_FLAG_FIXPOINT_OK;
    if ((arena->map.eval_out.eval_flags & req_flags) != req_flags ||
        !sound_hohfeld_coherent(arena)) {
        return SOUND_FIBER_HOLD_INCOHERENT;
    }
    uint64_t active_mask = arena->map.eval_out.established_mask;
    if ((fir_flags & SOUND_FIR_FLAG_USE_REPAIRED) != UINT32_C(0)) {
        if (arena->map.searu_repair.converged != UINT32_C(1) ||
            arena->map.searu_repair.best_energy != UINT64_C(0)) {
            return SOUND_FIBER_HOLD_INCOHERENT;
        }
        active_mask = arena->map.searu_repair.candidate_mask;
    }

    uint64_t mutable_mask = arena->map.norm.power_mask & (~arena->map.norm.immunity_mask);
    uint64_t resc_mask = arena->map.norm.rescission_power_mask & (~arena->map.norm.immunity_mask);
    const char *effective_name =
        (prog_name != NULL && prog_name[0] != '\0') ? prog_name : "sound_auth_gate";

    static const char *const k_in_names[5] = {"cur_pos", "cur_neg", "req_pos", "req_neg",
                                              "ex_resc"};
    static const uint32_t k_outputs[4] = {22U, 17U, 19U, 6U};
    static const sound__fir_op_desc_t k_ops[18] = {
        {(uint16_t)FLOW_IR_OP_CONST_U64, 0U, 64U, {0U, 0U, 0U}, "k_mut"},
        {(uint16_t)FLOW_IR_OP_CONST_U64, 0U, 64U, {0U, 0U, 0U}, "k_est"},
        {(uint16_t)FLOW_IR_OP_CONST_U64, 0U, 64U, {0U, 0U, 0U}, "k_resc"},
        {(uint16_t)FLOW_IR_OP_XOR_VEC, 2U, 64U, {2U, 0U, 0U}, "diff_pos"},
        {(uint16_t)FLOW_IR_OP_AND_VEC, 2U, 64U, {8U, 5U, 0U}, "step_pos"},
        {(uint16_t)FLOW_IR_OP_XOR_VEC, 2U, 64U, {0U, 9U, 0U}, "cand_pos"},
        {(uint16_t)FLOW_IR_OP_XOR_VEC, 2U, 64U, {3U, 1U, 0U}, "diff_neg"},
        {(uint16_t)FLOW_IR_OP_AND_VEC, 2U, 64U, {11U, 5U, 0U}, "step_neg"},
        {(uint16_t)FLOW_IR_OP_XOR_VEC, 2U, 64U, {1U, 12U, 0U}, "cand_neg"},
        {(uint16_t)FLOW_IR_OP_AND_VEC, 2U, 64U, {10U, 13U, 0U}, "clash"},
        {(uint16_t)FLOW_IR_OP_EQ_ZERO, 1U, 1U, {14U, 0U, 0U}, "coh_ok"},
        {(uint16_t)FLOW_IR_OP_CTRL_SEL, 3U, 64U, {15U, 0U, 10U}, "next_pos"},
        {(uint16_t)FLOW_IR_OP_CTRL_SEL, 3U, 64U, {15U, 1U, 13U}, "next_neg"},
        {(uint16_t)FLOW_IR_OP_NOT_VEC, 1U, 64U, {4U, 0U, 0U}, "not_ex"},
        {(uint16_t)FLOW_IR_OP_AND_VEC, 2U, 64U, {7U, 18U, 0U}, "next_resc"},
        {(uint16_t)FLOW_IR_OP_AND_VEC, 2U, 64U, {7U, 4U, 0U}, "fired_resc"},
        {(uint16_t)FLOW_IR_OP_NOT_VEC, 1U, 64U, {20U, 0U, 0U}, "not_fired"},
        {(uint16_t)FLOW_IR_OP_AND_VEC, 2U, 64U, {16U, 21U, 0U}, "final_pos"}};

    flow_ir_wire_zero(out_buf, (size_t)SOUND_FIR_BYTES);
    flow_ir_wire_write_header(out_buf, FLOW_IR_MULT_LINEAR, FLOW_IR_FLAG_CLEAN, UINT32_C(23),
                              UINT32_C(5), UINT32_C(4), UINT32_C(18), UINT32_C(0), effective_name);

    size_t off = FLOW_IR_HEADER_BYTES;
    for (uint32_t i = 0U; i < 5U; i++) {
        flow_ir_wire_write_val(out_buf + off, UINT16_C(64), UINT8_C(0), k_in_names[i]);
        off += FLOW_IR_VAL_WIRE_B;
    }
    for (uint32_t i = 0U; i < 18U; i++) {
        flow_ir_wire_write_val(out_buf + off, k_ops[i].width, UINT8_C(0), k_ops[i].name);
        off += FLOW_IR_VAL_WIRE_B;
    }

    for (uint32_t i = 0U; i < 5U; i++) {
        flow_ir_wire_write_le32(out_buf + off + ((size_t)i * 4U), i);
    }
    off += FLOW_IR_WIRE_PAD8_U32_BYTES(5U);

    for (uint32_t i = 0U; i < 4U; i++) {
        flow_ir_wire_write_le32(out_buf + off + ((size_t)i * 4U), k_outputs[i]);
    }
    off += FLOW_IR_WIRE_PAD8_U32_BYTES(4U);

    const uint64_t k_consts[3] = {mutable_mask, active_mask, resc_mask};
    for (uint32_t i = 0U; i < 18U; i++) {
        uint64_t p0 = (i < 3U) ? k_consts[i] : UINT64_C(0);
        flow_ir_wire_write_op(out_buf + off, k_ops[i].kind, k_ops[i].num_ins, 5U + i, k_ops[i].ins,
                              0U, 0U, p0, UINT64_C(0), UINT64_C(0), UINT64_C(0));
        off += FLOW_IR_OP_WIRE_B;
    }

    *out_len = flow_ir_wire_seal_trailer(out_buf, off);
    return SOUND_FIBER_COMMITTED;
}

#ifdef SOUND_EXPORT_RING0
sound_fiber_t sound_ring0_eval(SoundArena *arena);
sound_fiber_t sound_ring0_apply_potestative(SoundArena *arena, uint32_t slot, bool grant_pos,
                                            bool revoke_pos, bool grant_neg, bool revoke_neg);
sound_fiber_t sound_ring0_apply_declarative(SoundArena *arena, uint32_t intent_node,
                                            uint32_t duty_slot, bool in_constitutive_context,
                                            bool internal_will_matches, uint64_t utterance_hash);
sound_fiber_t sound_ring0_exercise_rescission(SoundArena *arena, uint32_t target_intent_node,
                                              uint32_t duty_slot, uint64_t rescission_hash);
sound_fiber_t sound_ring0_admit_forest(SoundArena *arena, uint32_t *admitted_node_out);
sound_fiber_t sound_ring0_searu_repair(SoundArena *arena, uint64_t seed, uint32_t max_iters);
void sound_ring0_arena_seal(SoundArena *arena);
sound_fiber_t sound_ring0_arena_verify(const SoundArena *arena);
sound_fiber_t sound_ring0_policy_seal(SoundArena *arena, uint64_t policy_mask, uint64_t bind_mask,
                                      uint32_t ctrl_flags);
sound_fiber_t sound_ring0_policy_verify(const SoundArena *arena);
sound_fiber_t sound_ring0_lift_to_fir(const SoundArena *arena, const char *prog_name,
                                      uint32_t fir_flags, uint8_t *out_buf, size_t out_cap,
                                      size_t *out_len);
uint64_t sound_ring0_siphash24(uint64_t k0, uint64_t k1, const uint8_t *data, size_t len);
sound_fiber_t sound_ring0_policy_seal_keyed(SoundArena *arena, uint64_t policy_mask,
                                            uint64_t bind_mask, uint32_t ctrl_flags, uint64_t k0,
                                            uint64_t k1);
sound_fiber_t sound_ring0_policy_verify_keyed(const SoundArena *arena, uint64_t k0, uint64_t k1);
sound_fiber_t sound_ring0_admit_forest_with_utterance(SoundArena *arena,
                                                      const uint8_t *utterance_ptr,
                                                      size_t utterance_len,
                                                      uint32_t *admitted_node_out);
sound_fiber_t sound_ring0_admit_forest_batch(SoundArena *arena, const uint8_t *utterance_ptr,
                                             size_t utterance_len, uint32_t *admitted_count_out);
sound_fiber_t sound_ring0_admit_forest_batch_keyed(SoundArena *arena, const uint8_t *utterance_ptr,
                                                   size_t utterance_len, uint64_t k0, uint64_t k1,
                                                   uint32_t *admitted_count_out);
sound_fiber_t sound_ring0_searu_repair_weighted(SoundArena *arena, uint64_t seed,
                                                uint32_t max_iters,
                                                const uint8_t priority_weights[SOUND_MAX_NODES]);
sound_fiber_t sound_ring0_chart_glue_import(SoundArena *dst, const SoundArena *src,
                                            uint64_t src_export_mask,
                                            const uint8_t dst_slot_for_src[SOUND_MAX_NODES]);
sound_fiber_t sound_ring0_chart_glue_import_keyed(SoundArena *dst, const SoundArena *src,
                                                  uint64_t src_export_mask,
                                                  const uint8_t dst_slot_for_src[SOUND_MAX_NODES],
                                                  uint64_t k0, uint64_t k1);
sound_fiber_t sound_ring0_chart_glue_verify(const SoundArena *dst, const SoundArena *src);
sound_fiber_t sound_ring0_lift_policy_eval_to_fir(const SoundArena *arena, const char *prog_name,
                                                  uint32_t unroll_alt_steps, uint8_t *out_buf,
                                                  size_t out_cap, size_t *out_len);

sound_fiber_t sound_ring0_eval(SoundArena *arena) {
    return sound_eval(arena);
}

sound_fiber_t sound_ring0_apply_potestative(SoundArena *arena, uint32_t slot, bool grant_pos,
                                            bool revoke_pos, bool grant_neg, bool revoke_neg) {
    return sound_apply_potestative(arena, slot, grant_pos, revoke_pos, grant_neg, revoke_neg);
}

sound_fiber_t sound_ring0_apply_declarative(SoundArena *arena, uint32_t intent_node,
                                            uint32_t duty_slot, bool in_constitutive_context,
                                            bool internal_will_matches, uint64_t utterance_hash) {
    return sound_apply_declarative(arena, intent_node, duty_slot, in_constitutive_context,
                                   internal_will_matches, utterance_hash);
}

sound_fiber_t sound_ring0_exercise_rescission(SoundArena *arena, uint32_t target_intent_node,
                                              uint32_t duty_slot, uint64_t rescission_hash) {
    return sound_exercise_rescission(arena, target_intent_node, duty_slot, rescission_hash);
}

sound_fiber_t sound_ring0_admit_forest(SoundArena *arena, uint32_t *admitted_node_out) {
    return sound_admit_forest(arena, admitted_node_out);
}

sound_fiber_t sound_ring0_searu_repair(SoundArena *arena, uint64_t seed, uint32_t max_iters) {
    return sound_searu_repair_conflict(arena, seed, max_iters);
}

void sound_ring0_arena_seal(SoundArena *arena) {
    sound_arena_seal(arena);
}

sound_fiber_t sound_ring0_arena_verify(const SoundArena *arena) {
    return sound_arena_verify(arena);
}

sound_fiber_t sound_ring0_policy_seal(SoundArena *arena, uint64_t policy_mask, uint64_t bind_mask,
                                      uint32_t ctrl_flags) {
    return sound_policy_seal(arena, policy_mask, bind_mask, ctrl_flags);
}

sound_fiber_t sound_ring0_policy_verify(const SoundArena *arena) {
    return sound_policy_verify(arena);
}

sound_fiber_t sound_ring0_lift_to_fir(const SoundArena *arena, const char *prog_name,
                                      uint32_t fir_flags, uint8_t *out_buf, size_t out_cap,
                                      size_t *out_len) {
    return sound_lift_to_fir(arena, prog_name, fir_flags, out_buf, out_cap, out_len);
}

uint64_t sound_ring0_siphash24(uint64_t k0, uint64_t k1, const uint8_t *data, size_t len) {
    return sound_siphash24_bytes(k0, k1, data, len);
}

sound_fiber_t sound_ring0_policy_seal_keyed(SoundArena *arena, uint64_t policy_mask,
                                            uint64_t bind_mask, uint32_t ctrl_flags, uint64_t k0,
                                            uint64_t k1) {
    return sound_policy_seal_keyed(arena, policy_mask, bind_mask, ctrl_flags, k0, k1);
}

sound_fiber_t sound_ring0_policy_verify_keyed(const SoundArena *arena, uint64_t k0, uint64_t k1) {
    return sound_policy_verify_keyed(arena, k0, k1);
}

sound_fiber_t sound_ring0_admit_forest_with_utterance(SoundArena *arena,
                                                      const uint8_t *utterance_ptr,
                                                      size_t utterance_len,
                                                      uint32_t *admitted_node_out) {
    return sound_admit_forest_with_utterance(arena, utterance_ptr, utterance_len,
                                             admitted_node_out);
}

sound_fiber_t sound_ring0_admit_forest_batch(SoundArena *arena, const uint8_t *utterance_ptr,
                                             size_t utterance_len, uint32_t *admitted_count_out) {
    return sound_admit_forest_batch(arena, utterance_ptr, utterance_len, admitted_count_out);
}

sound_fiber_t sound_ring0_admit_forest_batch_keyed(SoundArena *arena, const uint8_t *utterance_ptr,
                                                   size_t utterance_len, uint64_t k0, uint64_t k1,
                                                   uint32_t *admitted_count_out) {
    return sound_admit_forest_batch_keyed(arena, utterance_ptr, utterance_len, k0, k1,
                                          admitted_count_out);
}

sound_fiber_t sound_ring0_searu_repair_weighted(SoundArena *arena, uint64_t seed,
                                                uint32_t max_iters,
                                                const uint8_t priority_weights[SOUND_MAX_NODES]) {
    return sound_searu_repair_conflict_weighted(arena, seed, max_iters, priority_weights);
}

sound_fiber_t sound_ring0_chart_glue_import(SoundArena *dst, const SoundArena *src,
                                            uint64_t src_export_mask,
                                            const uint8_t dst_slot_for_src[SOUND_MAX_NODES]) {
    return sound_chart_glue_import(dst, src, src_export_mask, dst_slot_for_src);
}

sound_fiber_t sound_ring0_chart_glue_import_keyed(SoundArena *dst, const SoundArena *src,
                                                  uint64_t src_export_mask,
                                                  const uint8_t dst_slot_for_src[SOUND_MAX_NODES],
                                                  uint64_t k0, uint64_t k1) {
    return sound_chart_glue_import_keyed(dst, src, src_export_mask, dst_slot_for_src, k0, k1);
}

sound_fiber_t sound_ring0_chart_glue_verify(const SoundArena *dst, const SoundArena *src) {
    return sound_chart_glue_verify(dst, src);
}

sound_fiber_t sound_ring0_lift_policy_eval_to_fir(const SoundArena *arena, const char *prog_name,
                                                  uint32_t unroll_alt_steps, uint8_t *out_buf,
                                                  size_t out_cap, size_t *out_len) {
    return sound_lift_policy_eval_to_fir(arena, prog_name, unroll_alt_steps, out_buf, out_cap,
                                         out_len);
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* SOUND_H */
