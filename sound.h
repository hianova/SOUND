#ifndef SOUND_H
#define SOUND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flow.h"
#include "searu.h"
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
#define SOUND_FIR_MAGIC                  UINT32_C(0x31524946) /* "FIR1" */
#define SOUND_FIR_VERSION                UINT32_C(1)
#define SOUND_FIR_BYTES                  2144U
#define SOUND_FIR_FLAG_USE_REPAIRED      (UINT32_C(1) << 0)

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

typedef enum {
    SOUND_FOREST_ADMITTED = 0U,
    SOUND_FOREST_NEEDS_CLARIFICATION = 1U,
    SOUND_FOREST_IRREALIS_QUARANTINED = 2U,
    SOUND_FOREST_INVALID_SPAN = 3U
} sound_forest_verdict_t;

typedef enum {
    SOUND_OK = 0U,
    SOUND_ERR_NULL = 1U,
    SOUND_ERR_BOUNDS = 2U,
    SOUND_ERR_FORMAT = 3U,
    SOUND_ERR_MAC = 4U,
    SOUND_ERR_IMMUNITY_LOCKED = 5U,
    SOUND_ERR_NO_POWER = 6U,
    SOUND_ERR_MISSING_CONTEXT = 7U,
    SOUND_ERR_INCOHERENT = 8U
} sound_err_t;

typedef enum {
    SOUND_PHYS_UTTERANCE = 1U,
    SOUND_PHYS_DECLARATIVE = 2U,
    SOUND_PHYS_RESCISSION = 3U,
    SOUND_PHYS_FOREST_ADMIT = 4U
} sound_phys_kind_t;

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

static inline void sound_write_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & UINT16_C(0xFF));
    p[1] = (uint8_t)((v >> 8U) & UINT16_C(0xFF));
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

static inline void sound_write_fixed_ascii24(uint8_t dst[24], const char *src) {
    size_t i = 0U;
    if (src != NULL) {
        while (i < 23U && src[i] != '\0') {
            dst[i] = (uint8_t)src[i];
            i++;
        }
    }
    while (i < 24U) {
        dst[i] = 0U;
        i++;
    }
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

static inline sound_err_t sound_arena_verify(const SoundArena *arena) {
    if (arena == NULL) {
        return SOUND_ERR_NULL;
    }
    const SoundImageHeader64 *hdr = &arena->map.img_hdr;
    if (hdr->magic != SOUND_MAGIC || hdr->version != SOUND_VERSION ||
        hdr->schema_hash != SOUND_SCHEMA_SCHEMA_HASH) {
        return SOUND_ERR_FORMAT;
    }
    if (hdr->merkle_root != sound_arena_merkle(arena)) {
        return SOUND_ERR_MAC;
    }
    if (hdr->num_nodes > SOUND_MAX_NODES || hdr->phys_len > SOUND_MAX_EVENTS ||
        arena->map.forest_ctrl.num_candidates > SOUND_MAX_CANDIDATES) {
        return SOUND_ERR_BOUNDS;
    }
    uint64_t node_mask = sound_mask_n(hdr->num_nodes);
    if (((arena->map.norm.revoked_mask & (~node_mask)) != UINT64_C(0)) ||
        ((arena->map.norm.penumbra_mask & (~node_mask)) != UINT64_C(0))) {
        return SOUND_ERR_FORMAT;
    }
    for (uint32_t u = 0U; u < hdr->num_nodes; u++) {
        const SoundNodeMeta64 *nd = &arena->map.node_meta[u];
        if (nd->sort > (uint8_t)SOUND_SORT_INSTITUTIONAL ||
            nd->evid > (uint8_t)SOUND_EVID_ASSUMED ||
            nd->aspect > (uint8_t)SOUND_ASPECT_ACHIEVEMENT ||
            nd->num_clauses > (uint8_t)SOUND_MAX_CLAUSES ||
            ((nd->presup_mask & (~node_mask)) != UINT64_C(0)) ||
            ((nd->attack_mask & (~node_mask)) != UINT64_C(0))) {
            return SOUND_ERR_FORMAT;
        }
        for (uint32_t c = 0U; c < SOUND_MAX_CLAUSES; c++) {
            if ((arena->map.node_clauses[u].clauses[c] & (~node_mask)) != UINT64_C(0)) {
                return SOUND_ERR_FORMAT;
            }
        }
    }
    return SOUND_OK;
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

static inline sound_err_t sound_eval(SoundArena *arena) {
    if (arena == NULL) {
        return SOUND_ERR_NULL;
    }
    if (arena->map.img_hdr.num_nodes > SOUND_MAX_NODES) {
        return SOUND_ERR_BOUNDS;
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
    return SOUND_OK;
}

static inline sound_err_t sound_append_phys(SoundArena *arena, uint32_t kind, uint32_t target_node,
                                            uint32_t duty_slot, uint32_t flags, uint32_t span_start,
                                            uint32_t span_end, uint64_t payload_hash) {
    if (arena == NULL) {
        return SOUND_ERR_NULL;
    }
    if (arena->map.img_hdr.phys_len >= SOUND_MAX_EVENTS) {
        return SOUND_ERR_BOUNDS;
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
    return SOUND_OK;
}

static inline sound_err_t sound_apply_potestative(SoundArena *arena, uint32_t slot, bool grant_pos,
                                                  bool revoke_pos, bool grant_neg,
                                                  bool revoke_neg) {
    if (arena == NULL) {
        return SOUND_ERR_NULL;
    }
    if (slot >= SOUND_MAX_NODES) {
        return SOUND_ERR_BOUNDS;
    }
    uint64_t bit = sound_bit64(slot);
    if ((arena->map.norm.immunity_mask & bit) != UINT64_C(0)) {
        return SOUND_ERR_IMMUNITY_LOCKED;
    }
    if ((arena->map.norm.power_mask & bit) == UINT64_C(0)) {
        return SOUND_ERR_NO_POWER;
    }
    uint64_t dp = grant_pos ? (arena->map.norm.duty_pos_mask | bit) : arena->map.norm.duty_pos_mask;
    if (revoke_pos) {
        dp &= (~bit);
    }
    uint64_t dn = grant_neg ? (arena->map.norm.duty_neg_mask | bit) : arena->map.norm.duty_neg_mask;
    if (revoke_neg) {
        dn &= (~bit);
    }
    if ((dp & dn) != UINT64_C(0)) {
        return SOUND_ERR_INCOHERENT;
    }
    arena->map.norm.duty_pos_mask = dp;
    arena->map.norm.duty_neg_mask = dn;
    return SOUND_OK;
}

static inline sound_err_t sound_apply_declarative(SoundArena *arena, uint32_t intent_node,
                                                  uint32_t duty_slot, bool in_constitutive_context,
                                                  bool internal_will_matches,
                                                  uint64_t utterance_hash) {
    if (arena == NULL) {
        return SOUND_ERR_NULL;
    }
    if (intent_node >= arena->map.img_hdr.num_nodes || duty_slot >= SOUND_MAX_NODES) {
        return SOUND_ERR_BOUNDS;
    }
    const SoundNodeMeta64 *nd = &arena->map.node_meta[intent_node];
    sound_err_t log_rc =
        sound_append_phys(arena, (uint32_t)SOUND_PHYS_DECLARATIVE, intent_node, duty_slot,
                          in_constitutive_context ? UINT32_C(1) : UINT32_C(0), nd->span_start,
                          nd->span_end, utterance_hash);
    if (log_rc != SOUND_OK) {
        return log_rc;
    }
    if (!in_constitutive_context) {
        return SOUND_ERR_MISSING_CONTEXT;
    }
    sound_err_t pot_rc = sound_apply_potestative(arena, duty_slot, true, false, false, false);
    if (pot_rc != SOUND_OK) {
        return pot_rc;
    }
    uint64_t bit = sound_bit64(duty_slot);
    if (!internal_will_matches) {
        arena->map.norm.voidable_mask |= bit;
        arena->map.norm.rescission_power_mask |= bit;
    } else {
        arena->map.norm.voidable_mask &= (~bit);
        arena->map.norm.rescission_power_mask &= (~bit);
    }
    return SOUND_OK;
}

static inline sound_err_t sound_exercise_rescission(SoundArena *arena, uint32_t target_intent_node,
                                                    uint32_t duty_slot, uint64_t rescission_hash) {
    if (arena == NULL) {
        return SOUND_ERR_NULL;
    }
    if (target_intent_node >= arena->map.img_hdr.num_nodes || duty_slot >= SOUND_MAX_NODES) {
        return SOUND_ERR_BOUNDS;
    }
    uint64_t dbit = sound_bit64(duty_slot);
    if ((arena->map.norm.immunity_mask & dbit) != UINT64_C(0)) {
        return SOUND_ERR_IMMUNITY_LOCKED;
    }
    if ((arena->map.norm.rescission_power_mask & dbit) == UINT64_C(0)) {
        return SOUND_ERR_NO_POWER;
    }
    const SoundNodeMeta64 *nd = &arena->map.node_meta[target_intent_node];
    sound_err_t log_rc =
        sound_append_phys(arena, (uint32_t)SOUND_PHYS_RESCISSION, target_intent_node, duty_slot,
                          UINT32_C(1), nd->span_start, nd->span_end, rescission_hash);
    if (log_rc != SOUND_OK) {
        return log_rc;
    }
    arena->map.norm.revoked_mask |= sound_bit64(target_intent_node);
    arena->map.norm.voidable_mask &= (~dbit);
    arena->map.norm.rescission_power_mask &= (~dbit);
    arena->map.norm.duty_pos_mask &= (~dbit);

    sound_err_t ev_rc = sound_eval(arena);
    if (ev_rc != SOUND_OK) {
        return ev_rc;
    }
    uint64_t keep_mask = arena->map.eval_out.established_mask | arena->map.norm.immunity_mask;
    arena->map.norm.duty_pos_mask &= keep_mask;
    arena->map.norm.duty_neg_mask &= keep_mask;
    arena->map.norm.voidable_mask &= keep_mask;
    arena->map.norm.rescission_power_mask &= keep_mask;
    return SOUND_OK;
}

static inline sound_forest_verdict_t sound_admit_forest(SoundArena *arena,
                                                        uint32_t *admitted_node_out) {
    if (admitted_node_out != NULL) {
        *admitted_node_out = UINT32_MAX;
    }
    if (arena == NULL) {
        return SOUND_FOREST_INVALID_SPAN;
    }
    SoundForestCtrl64 *ctrl = &arena->map.forest_ctrl;
    if (ctrl->ambiguity_flags != UINT32_C(0) || ctrl->unresolved_anaphora != UINT32_C(0) ||
        ctrl->num_candidates != UINT32_C(1)) {
        ctrl->last_verdict = (uint32_t)SOUND_FOREST_NEEDS_CLARIFICATION;
        ctrl->last_admitted_node = UINT32_MAX;
        return SOUND_FOREST_NEEDS_CLARIFICATION;
    }
    const SoundCandidate64 *c = &arena->map.candidate[0];
    if (!(c->span_start < c->span_end && c->span_end <= ctrl->utterance_bytes) ||
        c->target_node >= arena->map.img_hdr.num_nodes || c->target_node >= SOUND_MAX_NODES ||
        c->sort > (uint8_t)SOUND_SORT_INSTITUTIONAL || c->evid > (uint8_t)SOUND_EVID_ASSUMED ||
        c->aspect > (uint8_t)SOUND_ASPECT_ACHIEVEMENT) {
        ctrl->last_verdict = (uint32_t)SOUND_FOREST_INVALID_SPAN;
        ctrl->last_admitted_node = UINT32_MAX;
        return SOUND_FOREST_INVALID_SPAN;
    }
    uint64_t nm = sound_mask_n(arena->map.img_hdr.num_nodes);
    if (((c->presup_mask & (~nm)) != UINT64_C(0)) || ((c->premise_mask & (~nm)) != UINT64_C(0)) ||
        ((c->attack_mask & (~nm)) != UINT64_C(0))) {
        ctrl->last_verdict = (uint32_t)SOUND_FOREST_INVALID_SPAN;
        ctrl->last_admitted_node = UINT32_MAX;
        return SOUND_FOREST_INVALID_SPAN;
    }
    if (c->force == (uint8_t)SOUND_FORCE_IRREALIS) {
        ctrl->last_verdict = (uint32_t)SOUND_FOREST_IRREALIS_QUARANTINED;
        ctrl->last_admitted_node = UINT32_MAX;
        return SOUND_FOREST_IRREALIS_QUARANTINED;
    }
    SoundNodeMeta64 *nd = &arena->map.node_meta[c->target_node];
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
    (void)sound_append_phys(arena, (uint32_t)SOUND_PHYS_FOREST_ADMIT, c->target_node,
                            c->target_node, (uint32_t)c->force, c->span_start, c->span_end,
                            c->payload_hash);
    ctrl->last_verdict = (uint32_t)SOUND_FOREST_ADMITTED;
    ctrl->last_admitted_node = c->target_node;
    if (admitted_node_out != NULL) {
        *admitted_node_out = c->target_node;
    }
    return SOUND_FOREST_ADMITTED;
}

static inline sound_err_t sound_searu_repair_conflict(SoundArena *arena, uint64_t seed,
                                                      uint32_t max_iters) {
    if (arena == NULL) {
        return SOUND_ERR_NULL;
    }
    sound_err_t ev_rc = sound_eval(arena);
    if (ev_rc != SOUND_OK) {
        return ev_rc;
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
        return SOUND_OK;
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
    return SOUND_OK;
}

typedef struct {
    uint16_t kind;
    uint16_t num_ins;
    uint16_t width;
    uint32_t ins[3];
    const char *name;
} sound__fir_op_desc_t;

static inline sound_err_t sound_lift_to_fir(const SoundArena *arena, const char *prog_name,
                                            uint32_t fir_flags, uint8_t *out_buf, size_t out_cap,
                                            size_t *out_len) {
    if (arena == NULL || out_buf == NULL || out_len == NULL) {
        return SOUND_ERR_NULL;
    }
    if (out_cap < (size_t)SOUND_FIR_BYTES) {
        return SOUND_ERR_BOUNDS;
    }
    uint32_t req_flags = SOUND_EVAL_FLAG_PARTITION_OK | SOUND_EVAL_FLAG_CONFLICT_FREE_OK |
                         SOUND_EVAL_FLAG_HOHFELD_OK | SOUND_EVAL_FLAG_FIXPOINT_OK;
    if ((arena->map.eval_out.eval_flags & req_flags) != req_flags ||
        !sound_hohfeld_coherent(arena)) {
        return SOUND_ERR_INCOHERENT;
    }
    uint64_t active_mask = arena->map.eval_out.established_mask;
    if ((fir_flags & SOUND_FIR_FLAG_USE_REPAIRED) != UINT32_C(0)) {
        if (arena->map.searu_repair.converged != UINT32_C(1) ||
            arena->map.searu_repair.best_energy != UINT64_C(0)) {
            return SOUND_ERR_INCOHERENT;
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
        {1U, 0U, 64U, {0U, 0U, 0U}, "k_mut"},       {1U, 0U, 64U, {0U, 0U, 0U}, "k_est"},
        {1U, 0U, 64U, {0U, 0U, 0U}, "k_resc"},      {4U, 2U, 64U, {2U, 0U, 0U}, "diff_pos"},
        {5U, 2U, 64U, {8U, 5U, 0U}, "step_pos"},    {4U, 2U, 64U, {0U, 9U, 0U}, "cand_pos"},
        {4U, 2U, 64U, {3U, 1U, 0U}, "diff_neg"},    {5U, 2U, 64U, {11U, 5U, 0U}, "step_neg"},
        {4U, 2U, 64U, {1U, 12U, 0U}, "cand_neg"},   {5U, 2U, 64U, {10U, 13U, 0U}, "clash"},
        {18U, 1U, 1U, {14U, 0U, 0U}, "coh_ok"},     {15U, 3U, 64U, {15U, 0U, 10U}, "next_pos"},
        {15U, 3U, 64U, {15U, 1U, 13U}, "next_neg"}, {6U, 1U, 64U, {4U, 0U, 0U}, "not_ex"},
        {5U, 2U, 64U, {7U, 18U, 0U}, "next_resc"},  {5U, 2U, 64U, {7U, 4U, 0U}, "fired_resc"},
        {6U, 1U, 64U, {20U, 0U, 0U}, "not_fired"},  {5U, 2U, 64U, {16U, 21U, 0U}, "final_pos"}};

    sound_zero_bytes(out_buf, (size_t)SOUND_FIR_BYTES);
    sound_write_le32(out_buf + 0U, SOUND_FIR_MAGIC);
    sound_write_le32(out_buf + 4U, SOUND_FIR_VERSION);
    sound_write_le32(out_buf + 8U, UINT32_C(1));  /* FLOW_IR_MULT_LINEAR */
    sound_write_le32(out_buf + 12U, UINT32_C(1)); /* cleanAnc = true */
    sound_write_le32(out_buf + 16U, UINT32_C(23));
    sound_write_le32(out_buf + 20U, UINT32_C(5));
    sound_write_le32(out_buf + 24U, UINT32_C(4));
    sound_write_le32(out_buf + 28U, UINT32_C(18));
    sound_write_le32(out_buf + 32U, UINT32_C(0));
    sound_write_le32(out_buf + 36U, UINT32_C(0));
    sound_write_fixed_ascii24(out_buf + 40U, effective_name);

    size_t off = 64U;
    for (uint32_t i = 0U; i < 5U; i++) {
        sound_write_le16(out_buf + off, UINT16_C(64));
        sound_write_fixed_ascii24(out_buf + off + 8U, k_in_names[i]);
        off += 32U;
    }
    for (uint32_t i = 0U; i < 18U; i++) {
        sound_write_le16(out_buf + off, k_ops[i].width);
        sound_write_fixed_ascii24(out_buf + off + 8U, k_ops[i].name);
        off += 32U;
    }

    for (uint32_t i = 0U; i < 5U; i++) {
        sound_write_le32(out_buf + off + ((size_t)i * 4U), i);
    }
    off += 24U; /* 5 inputs padded to 8-byte multiple */

    for (uint32_t i = 0U; i < 4U; i++) {
        sound_write_le32(out_buf + off + ((size_t)i * 4U), k_outputs[i]);
    }
    off += 16U;

    const uint64_t k_consts[3] = {mutable_mask, active_mask, resc_mask};
    for (uint32_t i = 0U; i < 18U; i++) {
        uint8_t *opr = out_buf + off;
        sound_write_le16(opr + 0U, k_ops[i].kind);
        sound_write_le16(opr + 2U, k_ops[i].num_ins);
        sound_write_le32(opr + 4U, 5U + i);
        for (uint16_t j = 0U; j < k_ops[i].num_ins; j++) {
            sound_write_le32(opr + 8U + ((size_t)j * 4U), k_ops[i].ins[j]);
        }
        uint64_t p0 = (i < 3U) ? k_consts[i] : UINT64_C(0);
        sound_write_le64(opr + 40U, p0);
        off += 72U;
    }

    uint64_t trailer = sound_fnv1a_bytes(SOUND_FNV1A_OFFSET, out_buf, off);
    sound_write_le64(out_buf + off, trailer);
    *out_len = off + 8U;
    return SOUND_OK;
}

#ifdef SOUND_EXPORT_RING0
sound_err_t sound_ring0_eval(SoundArena *arena);
sound_err_t sound_ring0_apply_potestative(SoundArena *arena, uint32_t slot, bool grant_pos,
                                          bool revoke_pos, bool grant_neg, bool revoke_neg);
sound_err_t sound_ring0_apply_declarative(SoundArena *arena, uint32_t intent_node,
                                          uint32_t duty_slot, bool in_constitutive_context,
                                          bool internal_will_matches, uint64_t utterance_hash);
sound_err_t sound_ring0_exercise_rescission(SoundArena *arena, uint32_t target_intent_node,
                                            uint32_t duty_slot, uint64_t rescission_hash);
sound_forest_verdict_t sound_ring0_admit_forest(SoundArena *arena, uint32_t *admitted_node_out);
sound_err_t sound_ring0_searu_repair(SoundArena *arena, uint64_t seed, uint32_t max_iters);
void sound_ring0_arena_seal(SoundArena *arena);
sound_err_t sound_ring0_arena_verify(const SoundArena *arena);
sound_err_t sound_ring0_lift_to_fir(const SoundArena *arena, const char *prog_name,
                                    uint32_t fir_flags, uint8_t *out_buf, size_t out_cap,
                                    size_t *out_len);

sound_err_t sound_ring0_eval(SoundArena *arena) {
    return sound_eval(arena);
}

sound_err_t sound_ring0_apply_potestative(SoundArena *arena, uint32_t slot, bool grant_pos,
                                          bool revoke_pos, bool grant_neg, bool revoke_neg) {
    return sound_apply_potestative(arena, slot, grant_pos, revoke_pos, grant_neg, revoke_neg);
}

sound_err_t sound_ring0_apply_declarative(SoundArena *arena, uint32_t intent_node,
                                          uint32_t duty_slot, bool in_constitutive_context,
                                          bool internal_will_matches, uint64_t utterance_hash) {
    return sound_apply_declarative(arena, intent_node, duty_slot, in_constitutive_context,
                                   internal_will_matches, utterance_hash);
}

sound_err_t sound_ring0_exercise_rescission(SoundArena *arena, uint32_t target_intent_node,
                                            uint32_t duty_slot, uint64_t rescission_hash) {
    return sound_exercise_rescission(arena, target_intent_node, duty_slot, rescission_hash);
}

sound_forest_verdict_t sound_ring0_admit_forest(SoundArena *arena, uint32_t *admitted_node_out) {
    return sound_admit_forest(arena, admitted_node_out);
}

sound_err_t sound_ring0_searu_repair(SoundArena *arena, uint64_t seed, uint32_t max_iters) {
    return sound_searu_repair_conflict(arena, seed, max_iters);
}

void sound_ring0_arena_seal(SoundArena *arena) {
    sound_arena_seal(arena);
}

sound_err_t sound_ring0_arena_verify(const SoundArena *arena) {
    return sound_arena_verify(arena);
}

sound_err_t sound_ring0_lift_to_fir(const SoundArena *arena, const char *prog_name,
                                    uint32_t fir_flags, uint8_t *out_buf, size_t out_cap,
                                    size_t *out_len) {
    return sound_lift_to_fir(arena, prog_name, fir_flags, out_buf, out_cap, out_len);
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* SOUND_H */
