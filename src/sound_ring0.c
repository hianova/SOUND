#include "sound.h"

/* ============================================================================
 * Internal SplitMix64 PRNG & SipHash-2-4
 * ============================================================================ */

static uint64_t sound__splitmix64(uint64_t *state) {
    uint64_t z = (*state += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30U)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27U)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31U);
}

static uint64_t sound__rotl64(uint64_t x, uint32_t b) {
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

static void sound__sipround(sound_siphash24_ctx_t *c) {
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

static void sound__siphash24_init(sound_siphash24_ctx_t *c, uint64_t k0, uint64_t k1) {
    c->v0 = UINT64_C(0x736F6D6570736575) ^ k0;
    c->v1 = UINT64_C(0x646F72616E646F6D) ^ k1;
    c->v2 = UINT64_C(0x6C7967656E657261) ^ k0;
    c->v3 = UINT64_C(0x7465646279746573) ^ k1;
    c->buf = UINT64_C(0);
    c->buflen = UINT32_C(0);
    c->total_len_low = UINT32_C(0);
}

static void sound__siphash24_update(sound_siphash24_ctx_t *c, const uint8_t *data, size_t len) {
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

static uint64_t sound__siphash24_final(sound_siphash24_ctx_t *c) {
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

uint64_t sound_siphash24_bytes(uint64_t k0, uint64_t k1, const uint8_t *data, size_t len) {
    sound_siphash24_ctx_t ctx;
    sound__siphash24_init(&ctx, k0, k1);
    if (data != NULL && len > 0U) {
        sound__siphash24_update(&ctx, data, len);
    }
    return sound__siphash24_final(&ctx);
}

uint64_t sound_arena_siphash24(const SoundArena *arena, uint64_t k0, uint64_t k1) {
    if (arena == NULL) {
        return UINT64_C(0);
    }
    return sound_siphash24_bytes(k0, k1, arena->cells[1].bytes,
                                 (size_t)(SOUND_ARENA_CELLS - 1U) * 64ULL);
}

/* ============================================================================
 * Layer 0: Policy Digest, Prepared-Statement Admission & Arena Verification
 * ============================================================================ */

static uint64_t sound__policy_digest_core(const SoundArena *arena, bool keyed, uint64_t k0,
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
    sound__siphash24_init(&sip, k0, k1);

    if (keyed) {
        sound__siphash24_update(&sip, hdr_buf, sizeof(hdr_buf));
        sound__siphash24_update(&sip, arena->cells[SOUND_SLOT_NORM].bytes, 64U);
    } else {
        h = sound_fnv1a_bytes(h, hdr_buf, sizeof(hdr_buf));
        h = sound_fnv1a_bytes(h, arena->cells[SOUND_SLOT_NORM].bytes, 64U);
    }

    for (uint32_t i = 0U; i < n; i++) {
        uint64_t bit = sound_bit64(i);
        const uint8_t *clause_bytes = arena->cells[SOUND_SLOT_NODE_CLAUSES_BASE + i].bytes;
        if (keyed) {
            sound__siphash24_update(&sip, clause_bytes, 64U);
        } else {
            h = sound_fnv1a_bytes(h, clause_bytes, 64U);
        }
        if ((ctrl->bind_mask & bit) == UINT64_C(0)) {
            const uint8_t *meta_bytes = arena->cells[SOUND_SLOT_NODE_META_BASE + i].bytes;
            if (keyed) {
                sound__siphash24_update(&sip, meta_bytes, 64U);
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
                sound__siphash24_update(&sip, tmpl, sizeof(tmpl));
            } else {
                h = sound_fnv1a_bytes(h, tmpl, sizeof(tmpl));
            }
        }
    }
    return keyed ? sound__siphash24_final(&sip) : h;
}

uint64_t sound_policy_digest(const SoundArena *arena) {
    return sound__policy_digest_core(arena, false, UINT64_C(0), UINT64_C(0));
}

uint64_t sound_policy_digest_keyed(const SoundArena *arena, uint64_t k0, uint64_t k1) {
    return sound__policy_digest_core(arena, true, k0, k1);
}

static sound_fiber_t sound__policy_verify_core(const SoundArena *arena, bool use_key, uint64_t k0,
                                               uint64_t k1) {
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

sound_fiber_t sound_policy_verify(const SoundArena *arena) {
    return sound__policy_verify_core(arena, false, UINT64_C(0), UINT64_C(0));
}

sound_fiber_t sound_policy_verify_keyed(const SoundArena *arena, uint64_t k0, uint64_t k1) {
    return sound__policy_verify_core(arena, true, k0, k1);
}

static sound_fiber_t sound__policy_seal_core(SoundArena *arena, uint64_t policy_mask,
                                             uint64_t bind_mask, uint32_t ctrl_flags, bool use_key,
                                             uint64_t k0, uint64_t k1) {
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

sound_fiber_t sound_policy_seal(SoundArena *arena, uint64_t policy_mask, uint64_t bind_mask,
                                uint32_t ctrl_flags) {
    return sound__policy_seal_core(arena, policy_mask, bind_mask, ctrl_flags, false, UINT64_C(0),
                                   UINT64_C(0));
}

sound_fiber_t sound_policy_seal_keyed(SoundArena *arena, uint64_t policy_mask, uint64_t bind_mask,
                                      uint32_t ctrl_flags, uint64_t k0, uint64_t k1) {
    return sound__policy_seal_core(arena, policy_mask, bind_mask, ctrl_flags, true, k0, k1);
}

sound_fiber_t sound_arena_verify(const SoundArena *arena) {
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

/* ============================================================================
 * Layer 2: 4-Valued Alternating-Fixpoint Bilattice & Hume Firewall
 * ============================================================================ */

uint64_t sound_arena_factual_mask(const SoundArena *arena) {
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

bool sound_clause_valid(const SoundArena *arena, uint32_t u, uint64_t clause_mask,
                        uint64_t node_mask, uint64_t factual_mask, uint32_t *diag_reason_out) {
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

bool sound_node_fires(const SoundArena *arena, uint32_t u, uint64_t blocked_by, uint64_t active_set,
                      uint64_t node_mask, uint64_t factual_mask) {
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

uint64_t sound_gamma(const SoundArena *arena, uint64_t blocked_by, uint64_t node_mask,
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

sound_fiber_t sound_eval(SoundArena *arena) {
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

/* ============================================================================
 * Layer 1: Monotone Physical Log, Hohfeld Potestative & Declarative Transitions
 * ============================================================================ */

sound_fiber_t sound_append_phys(SoundArena *arena, uint32_t kind, uint32_t target_node,
                                uint32_t duty_slot, uint32_t flags, uint32_t span_start,
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

sound_fiber_t sound_apply_potestative(SoundArena *arena, uint32_t slot, bool grant_pos,
                                      bool revoke_pos, bool grant_neg, bool revoke_neg) {
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

    bool commit = (!imm_hit) && pow_ok && coh_ok;
    uint64_t cm = sound_mask_bool(commit);
    arena->map.norm.duty_pos_mask = sound_mux_u64(cm, dp, cur_pos);
    arena->map.norm.duty_neg_mask = sound_mux_u64(cm, dn, cur_neg);

    uint32_t f = sound_mux_u32(sound_mask_bool_u32(!coh_ok), (uint32_t)SOUND_FIBER_HOLD_INCOHERENT,
                               (uint32_t)SOUND_FIBER_COMMITTED);
    f = sound_mux_u32(sound_mask_bool_u32(!pow_ok), (uint32_t)SOUND_FIBER_HOLD_NO_POWER, f);
    f = sound_mux_u32(sound_mask_bool_u32(imm_hit), (uint32_t)SOUND_FIBER_HOLD_IMMUNITY, f);
    return sound_record_fiber(arena, (sound_fiber_t)f);
}

sound_fiber_t sound_apply_declarative(SoundArena *arena, uint32_t intent_node, uint32_t duty_slot,
                                      bool in_constitutive_context, bool internal_will_matches,
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

sound_fiber_t sound_exercise_rescission(SoundArena *arena, uint32_t target_intent_node,
                                        uint32_t duty_slot, uint64_t rescission_hash) {
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

/* ============================================================================
 * Layer 0: Candidate Forest Gate (Atomic 2-Phase Transaction)
 * ============================================================================ */

static bool sound__validate_candidate_at(const SoundArena *arena, uint32_t cand_idx, bool sealed,
                                         const uint8_t *utterance_ptr, size_t utterance_len,
                                         sound_fiber_t *fiber_out, uint32_t *inject_reasons_out) {
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

static sound_fiber_t sound__admit_forest_core(SoundArena *arena, bool single_only,
                                              const uint8_t *utterance_ptr, size_t utterance_len,
                                              bool use_key, uint64_t k0, uint64_t k1,
                                              uint32_t *out_val) {
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

sound_fiber_t sound_admit_forest(SoundArena *arena, uint32_t *admitted_node_out) {
    return sound__admit_forest_core(arena, true, NULL, 0U, false, UINT64_C(0), UINT64_C(0),
                                    admitted_node_out);
}

sound_fiber_t sound_admit_forest_with_utterance(SoundArena *arena, const uint8_t *utterance_ptr,
                                                size_t utterance_len, uint32_t *admitted_node_out) {
    if (utterance_ptr == NULL) {
        if (admitted_node_out != NULL) {
            *admitted_node_out = UINT32_MAX;
        }
        return sound_record_fiber(arena, SOUND_FIBER_FALLBACK_NULL);
    }
    return sound__admit_forest_core(arena, true, utterance_ptr, utterance_len, false, UINT64_C(0),
                                    UINT64_C(0), admitted_node_out);
}

sound_fiber_t sound_admit_forest_batch(SoundArena *arena, const uint8_t *utterance_ptr,
                                       size_t utterance_len, uint32_t *admitted_count_out) {
    return sound__admit_forest_core(arena, false, utterance_ptr, utterance_len, false, UINT64_C(0),
                                    UINT64_C(0), admitted_count_out);
}

sound_fiber_t sound_admit_forest_batch_keyed(SoundArena *arena, const uint8_t *utterance_ptr,
                                             size_t utterance_len, uint64_t k0, uint64_t k1,
                                             uint32_t *admitted_count_out) {
    return sound__admit_forest_core(arena, false, utterance_ptr, utterance_len, true, k0, k1,
                                    admitted_count_out);
}

/* ============================================================================
 * SEARU & Lex Specialis Conflict Repair + Cross-Chart Sheaf Gluing
 * ============================================================================ */

sound_fiber_t sound_searu_repair_conflict(SoundArena *arena, uint64_t seed, uint32_t max_iters) {
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

sound_fiber_t sound_set_priority_weights(SoundArena *arena,
                                         const uint8_t weights[SOUND_MAX_NODES]) {
    if (arena == NULL || weights == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    for (uint32_t i = 0U; i < SOUND_MAX_NODES; i++) {
        arena->cells[SOUND_SLOT_FLOW_AUX_BASE + SOUND_FLOW_AUX_PRIORITY_CELL].bytes[i] = weights[i];
    }
    return SOUND_FIBER_COMMITTED;
}

sound_fiber_t
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

uint64_t sound_chart_glue_digest(const SoundChartGlue64 *g) {
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

sound_fiber_t sound_chart_export_port(const SoundArena *src, uint64_t export_mask,
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

static sound_fiber_t sound__chart_glue_import_core(SoundArena *dst, const SoundArena *src,
                                                   uint64_t src_export_mask,
                                                   const uint8_t dst_slot_for_src[SOUND_MAX_NODES],
                                                   bool use_key, uint64_t k0, uint64_t k1) {
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

sound_fiber_t sound_chart_glue_import(SoundArena *dst, const SoundArena *src,
                                      uint64_t src_export_mask,
                                      const uint8_t dst_slot_for_src[SOUND_MAX_NODES]) {
    return sound__chart_glue_import_core(dst, src, src_export_mask, dst_slot_for_src, false,
                                         UINT64_C(0), UINT64_C(0));
}

sound_fiber_t sound_chart_glue_import_keyed(SoundArena *dst, const SoundArena *src,
                                            uint64_t src_export_mask,
                                            const uint8_t dst_slot_for_src[SOUND_MAX_NODES],
                                            uint64_t k0, uint64_t k1) {
    return sound__chart_glue_import_core(dst, src, src_export_mask, dst_slot_for_src, true, k0, k1);
}

sound_fiber_t sound_chart_glue_verify(const SoundArena *dst, const SoundArena *src) {
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

/* ============================================================================
 * FIR1 Lifters (Authority Gate 2,144B & Combinational Policy-Eval Unrolling)
 * ============================================================================ */

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

static uint16_t sound__pbld_op(sound__pbld_t *b, uint16_t kind, uint8_t nins, bool w1, uint16_t i0,
                               uint16_t i1, uint16_t i2, uint64_t p0) {
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

static uint16_t sound__pbld_concat_or(sound__pbld_t *b, const uint16_t *bits, uint32_t count) {
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

static uint16_t sound__pbld_const(sound__pbld_t *b, uint64_t val) {
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

static uint16_t sound__pbld_gamma(sound__pbld_t *b, const SoundArena *arena, uint32_t n,
                                  uint64_t node_mask, uint64_t factual_mask, uint64_t bind_mask,
                                  uint64_t root_mask, uint64_t plain_bind_mask,
                                  const uint8_t *der_order, uint32_t n_der, uint16_t zero_id,
                                  uint16_t facts_in_id, bool zero_blocked, uint16_t blocked_id) {
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

sound_fiber_t sound_lift_policy_eval_to_fir(const SoundArena *arena, const char *prog_name,
                                            uint32_t unroll_alt_steps, uint8_t *out_buf,
                                            size_t out_cap, size_t *out_len) {
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
    sound_zero_bytes(&b, sizeof(b));

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

sound_fiber_t sound_lift_to_fir(const SoundArena *arena, const char *prog_name, uint32_t fir_flags,
                                uint8_t *out_buf, size_t out_cap, size_t *out_len) {
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

/* ============================================================================
 * Pillar 1: Belnap Bilattice & Odd-Cycle Classification / Discovery Hunt
 * ============================================================================ */

uint8_t sound_b4_neg(uint8_t x) {
    uint32_t xm = (uint32_t)x & 3U;
    return (uint8_t)(((xm & 2U) >> 1U) | ((xm & 1U) << 1U));
}

uint8_t sound_b4_conf(uint8_t x) {
    return (uint8_t)((uint32_t)sound_b4_neg(x) ^ 3U);
}

uint8_t sound_b4_and_t(uint8_t x, uint8_t y) {
    uint32_t xu = (uint32_t)x;
    uint32_t yu = (uint32_t)y;
    return (uint8_t)(((xu & yu) & 2U) | ((xu | yu) & 1U));
}

uint8_t sound_b4_or_t(uint8_t x, uint8_t y) {
    uint32_t xu = (uint32_t)x;
    uint32_t yu = (uint32_t)y;
    return (uint8_t)(((xu | yu) & 2U) | ((xu & yu) & 1U));
}

uint8_t sound_b4_and_k(uint8_t x, uint8_t y) {
    return (uint8_t)(((uint32_t)x & (uint32_t)y) & 3U);
}

uint8_t sound_b4_or_k(uint8_t x, uint8_t y) {
    return (uint8_t)(((uint32_t)x | (uint32_t)y) & 3U);
}

bool sound_b4_le_t(uint8_t x, uint8_t y) {
    uint32_t xt = (uint32_t)x & 2U;
    uint32_t yt = (uint32_t)y & 2U;
    uint32_t xf = (uint32_t)x & 1U;
    uint32_t yf = (uint32_t)y & 1U;
    return (xt == 0U || yt != 0U) && (yf == 0U || xf != 0U);
}

bool sound_b4_le_k(uint8_t x, uint8_t y) {
    return (((uint32_t)x & 3U) & (~((uint32_t)y & 3U))) == 0U;
}

uint32_t sound_graph_count_classical_models(uint32_t n, const uint8_t atk_to[8]) {
    if (n == 0U || n > 8U || atk_to == NULL) {
        return UINT32_C(0);
    }
    uint32_t count = UINT32_C(0);
    uint32_t limit = UINT32_C(1) << n;
    for (uint32_t m = 0U; m < limit; m++) {
        bool ok = true;
        for (uint32_t u = 0U; u < n; u++) {
            bool u_in = ((m >> u) & UINT32_C(1)) != UINT32_C(0);
            bool u_unatk = (((uint32_t)atk_to[u]) & m) == UINT32_C(0);
            if (u_in != u_unatk) {
                ok = false;
                break;
            }
        }
        if (ok) {
            count++;
        }
    }
    return count;
}

static uint8_t sound__small_gamma(uint32_t n, const uint8_t atk_to[8], uint8_t blocked_by) {
    uint32_t acc = UINT32_C(0);
    for (uint32_t u = 0U; u < n; u++) {
        if ((((uint32_t)atk_to[u]) & (uint32_t)blocked_by) == UINT32_C(0)) {
            acc |= (UINT32_C(1) << u);
        }
    }
    return (uint8_t)acc;
}

void sound_graph_eval_bilattice_small(uint32_t n, const uint8_t atk_to[8], uint8_t *i_star_out,
                                      uint8_t *p_star_out) {
    if (n == 0U || n > 8U || atk_to == NULL) {
        if (i_star_out != NULL) {
            *i_star_out = UINT8_C(0);
        }
        if (p_star_out != NULL) {
            *p_star_out = UINT8_C(0);
        }
        return;
    }
    uint8_t cur_i = UINT8_C(0);
    for (uint32_t step = 0U; step < n; step++) {
        uint8_t p_tmp = sound__small_gamma(n, atk_to, cur_i);
        uint8_t nxt_i = sound__small_gamma(n, atk_to, p_tmp);
        if (nxt_i == cur_i) {
            break;
        }
        cur_i = nxt_i;
    }
    uint8_t p_star = sound__small_gamma(n, atk_to, cur_i);
    uint8_t i_star = sound__small_gamma(n, atk_to, p_star);
    if (i_star_out != NULL) {
        *i_star_out = i_star;
    }
    if (p_star_out != NULL) {
        *p_star_out = p_star;
    }
}

static void sound__classify_digraph_space(uint32_t n, uint32_t *total_out, uint32_t *grounded_out,
                                          uint32_t *uniq_nongrnd_out, uint32_t *multi_out,
                                          uint32_t *unsolvable_out, uint32_t *conflict_out) {
    uint8_t edges_src[16];
    uint8_t edges_dst[16];
    uint32_t num_edges = UINT32_C(0);
    for (uint32_t dst = 0U; dst < n; dst++) {
        for (uint32_t src = 0U; src < n; src++) {
            if (src != dst) {
                edges_src[num_edges] = (uint8_t)src;
                edges_dst[num_edges] = (uint8_t)dst;
                num_edges++;
            }
        }
    }
    uint32_t total = UINT32_C(1) << num_edges;
    uint32_t grounded = UINT32_C(0);
    uint32_t uniq_nongrnd = UINT32_C(0);
    uint32_t multi = UINT32_C(0);
    uint32_t unsolvable = UINT32_C(0);
    uint32_t conflict = UINT32_C(0);
    uint8_t nm = (uint8_t)((UINT32_C(1) << n) - UINT32_C(1));

    for (uint32_t g = 0U; g < total; g++) {
        uint8_t atk_to[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for (uint32_t e = 0U; e < num_edges; e++) {
            if ((g & (UINT32_C(1) << e)) != UINT32_C(0)) {
                atk_to[edges_dst[e]] |= (uint8_t)(UINT32_C(1) << edges_src[e]);
            }
        }
        uint32_t models = sound_graph_count_classical_models(n, atk_to);
        uint8_t ist = UINT8_C(0);
        uint8_t pst = UINT8_C(0);
        sound_graph_eval_bilattice_small(n, atk_to, &ist, &pst);
        if (models == UINT32_C(1) && ist == pst) {
            grounded++;
        } else if (models == UINT32_C(1)) {
            uniq_nongrnd++;
        } else if (models >= UINT32_C(2)) {
            multi++;
        } else {
            unsolvable++;
        }
        if ((((uint32_t)pst & (~(uint32_t)ist)) & (uint32_t)nm) != 0U) {
            conflict++;
        }
    }
    *total_out = total;
    *grounded_out = grounded;
    *uniq_nongrnd_out = uniq_nongrnd;
    *multi_out = multi;
    *unsolvable_out = unsolvable;
    *conflict_out = conflict;
}

void sound_classify_bilattice(sound_bilattice_atlas_t *out) {
    if (out == NULL) {
        return;
    }
    sound_zero_bytes(out, sizeof(*out));
    bool v4_ok = true;
    bool dm_ok = true;
    bool inter_ok = true;
    bool dist_ok = true;
    for (uint8_t x = 0U; x < 4U; x++) {
        if (sound_b4_neg(sound_b4_neg(x)) != x || sound_b4_conf(sound_b4_conf(x)) != x ||
            sound_b4_neg(sound_b4_conf(x)) != sound_b4_conf(sound_b4_neg(x))) {
            v4_ok = false;
        }
        for (uint8_t y = 0U; y < 4U; y++) {
            if (sound_b4_neg(sound_b4_and_t(x, y)) !=
                    sound_b4_or_t(sound_b4_neg(x), sound_b4_neg(y)) ||
                sound_b4_neg(sound_b4_or_t(x, y)) !=
                    sound_b4_and_t(sound_b4_neg(x), sound_b4_neg(y)) ||
                sound_b4_conf(sound_b4_and_k(x, y)) !=
                    sound_b4_or_k(sound_b4_conf(x), sound_b4_conf(y))) {
                dm_ok = false;
            }
            for (uint8_t x2 = 0U; x2 < 4U; x2++) {
                for (uint8_t y2 = 0U; y2 < 4U; y2++) {
                    if (sound_b4_le_k(x, x2) && sound_b4_le_k(y, y2)) {
                        if (!sound_b4_le_k(sound_b4_and_t(x, y), sound_b4_and_t(x2, y2)) ||
                            !sound_b4_le_k(sound_b4_or_t(x, y), sound_b4_or_t(x2, y2))) {
                            inter_ok = false;
                        }
                    }
                    if (sound_b4_le_t(x, x2) && sound_b4_le_t(y, y2)) {
                        if (!sound_b4_le_t(sound_b4_and_k(x, y), sound_b4_and_k(x2, y2)) ||
                            !sound_b4_le_t(sound_b4_or_k(x, y), sound_b4_or_k(x2, y2))) {
                            inter_ok = false;
                        }
                    }
                }
                if (x2 < 4U && sound_b4_and_t(x, sound_b4_or_k(y, x2)) !=
                                   sound_b4_or_k(sound_b4_and_t(x, y), sound_b4_and_t(x, x2))) {
                    dist_ok = false;
                }
            }
        }
    }
    out->b4_v4_ok = v4_ok ? UINT32_C(1) : UINT32_C(0);
    out->b4_demorgan_ok = dm_ok ? UINT32_C(1) : UINT32_C(0);
    out->b4_interlaced_ok = inter_ok ? UINT32_C(1) : UINT32_C(0);
    out->b4_distrib_ok = dist_ok ? UINT32_C(1) : UINT32_C(0);

    sound__classify_digraph_space(3U, &out->n3_total, &out->n3_grounded_unique,
                                  &out->n3_unique_nongrounded, &out->n3_multi_stable,
                                  &out->n3_classical_unsolvable, &out->n3_conflict_nonempty);
    sound__classify_digraph_space(4U, &out->n4_total, &out->n4_grounded_unique,
                                  &out->n4_unique_nongrounded, &out->n4_multi_stable,
                                  &out->n4_classical_unsolvable, &out->n4_conflict_nonempty);
}

void sound_hunt_paradox_min(uint64_t seed, sound_paradox_witness_t *out) {
    if (out == NULL) {
        return;
    }
    sound_zero_bytes(out, sizeof(*out));
    uint64_t r = seed ^ UINT64_C(0x50415241444F5831);
    uint64_t d = sound__splitmix64(&r);
    out->num_nodes = 3U;
    out->seed = seed;
    if ((d & UINT64_C(1)) == UINT64_C(0)) {
        /* 0 -> 1 -> 2 -> 0: 2 attacks 0, 0 attacks 1, 1 attacks 2 */
        out->atk_to[0] = UINT8_C(4);
        out->atk_to[1] = UINT8_C(1);
        out->atk_to[2] = UINT8_C(2);
    } else {
        /* 0 -> 2 -> 1 -> 0: 1 attacks 0, 2 attacks 1, 0 attacks 2 */
        out->atk_to[0] = UINT8_C(2);
        out->atk_to[1] = UINT8_C(4);
        out->atk_to[2] = UINT8_C(1);
    }
    out->classical_models = sound_graph_count_classical_models(3U, out->atk_to);
    sound_graph_eval_bilattice_small(3U, out->atk_to, &out->i_star, &out->p_star);
    out->conflict_mask = (uint8_t)(((uint32_t)out->p_star & (~(uint32_t)out->i_star)) & 7U);
    out->is_odd_cycle = (out->classical_models == UINT32_C(0) && out->conflict_mask == UINT8_C(7))
                            ? UINT8_C(1)
                            : UINT8_C(0);
    uint64_t mh = SOUND_FNV1A_OFFSET;
    mh = sound_fnv1a_u64(mh, (uint64_t)out->num_nodes);
    mh = sound_fnv1a_bytes(mh, out->atk_to, 3U);
    mh = sound_fnv1a_u64(mh, (uint64_t)out->classical_models);
    mh = sound_fnv1a_u64(mh, (uint64_t)out->conflict_mask);
    out->merkle = mh;
}

/* ============================================================================
 * Pillar 2: Discrete Cech Sheaf Cohomology (H^1)
 * ============================================================================ */

uint8_t sound_sheaf_holonomy_z4_3(uint8_t g01, uint8_t g12, uint8_t g20) {
    return (uint8_t)(((uint32_t)g01 + (uint32_t)g12 + (uint32_t)g20) & 3U);
}

uint8_t sound_sheaf_holonomy_v4_3(uint8_t g01, uint8_t g12, uint8_t g20) {
    return (uint8_t)(((uint32_t)g01 ^ (uint32_t)g12 ^ (uint32_t)g20) & 3U);
}

bool sound_sheaf_is_coboundary_z4_3(uint8_t g01, uint8_t g12, uint8_t g20) {
    uint32_t e01 = (uint32_t)g01 & 3U;
    uint32_t e12 = (uint32_t)g12 & 3U;
    uint32_t e20 = (uint32_t)g20 & 3U;
    for (uint32_t f0 = 0U; f0 < 4U; f0++) {
        for (uint32_t f1 = 0U; f1 < 4U; f1++) {
            for (uint32_t f2 = 0U; f2 < 4U; f2++) {
                if ((((f1 - f0) & 3U) == e01) && (((f2 - f1) & 3U) == e12) &&
                    (((f0 - f2) & 3U) == e20)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool sound_sheaf_is_coboundary_v4_3(uint8_t g01, uint8_t g12, uint8_t g20) {
    uint32_t e01 = (uint32_t)g01 & 3U;
    uint32_t e12 = (uint32_t)g12 & 3U;
    uint32_t e20 = (uint32_t)g20 & 3U;
    for (uint32_t f0 = 0U; f0 < 4U; f0++) {
        for (uint32_t f1 = 0U; f1 < 4U; f1++) {
            for (uint32_t f2 = 0U; f2 < 4U; f2++) {
                if (((f0 ^ f1) == e01) && ((f1 ^ f2) == e12) && ((f2 ^ f0) == e20)) {
                    return true;
                }
            }
        }
    }
    return false;
}

static bool sound__sheaf_is_coboundary_z4_4(uint8_t g01, uint8_t g12, uint8_t g23, uint8_t g30) {
    uint32_t e01 = (uint32_t)g01 & 3U;
    uint32_t e12 = (uint32_t)g12 & 3U;
    uint32_t e23 = (uint32_t)g23 & 3U;
    uint32_t e30 = (uint32_t)g30 & 3U;
    for (uint32_t f1 = 0U; f1 < 4U; f1++) {
        for (uint32_t f2 = 0U; f2 < 4U; f2++) {
            for (uint32_t f3 = 0U; f3 < 4U; f3++) {
                if (((f1 & 3U) == e01) && (((f2 - f1) & 3U) == e12) && (((f3 - f2) & 3U) == e23) &&
                    (((UINT32_C(0) - f3) & 3U) == e30)) {
                    return true;
                }
            }
        }
    }
    return false;
}

static bool sound__sheaf_is_coboundary_z4_p3(uint8_t g01, uint8_t g12) {
    uint32_t e01 = (uint32_t)g01 & 3U;
    uint32_t e12 = (uint32_t)g12 & 3U;
    for (uint32_t f1 = 0U; f1 < 4U; f1++) {
        for (uint32_t f2 = 0U; f2 < 4U; f2++) {
            if (((f1 & 3U) == e01) && (((f2 - f1) & 3U) == e12)) {
                return true;
            }
        }
    }
    return false;
}

void sound_classify_sheaf(sound_sheaf_atlas_t *out) {
    if (out == NULL) {
        return;
    }
    sound_zero_bytes(out, sizeof(*out));
    bool gauge_ok = true;

    for (uint8_t g01 = 0U; g01 < 4U; g01++) {
        for (uint8_t g12 = 0U; g12 < 4U; g12++) {
            for (uint8_t g20 = 0U; g20 < 4U; g20++) {
                out->c3_z4_total++;
                uint8_t hol_z = sound_sheaf_holonomy_z4_3(g01, g12, g20);
                bool cob_z = sound_sheaf_is_coboundary_z4_3(g01, g12, g20);
                if (cob_z != (hol_z == UINT8_C(0))) {
                    gauge_ok = false;
                }
                if (cob_z) {
                    out->c3_z4_coboundary++;
                } else {
                    out->c3_z4_obstructed++;
                }

                out->c3_v4_total++;
                uint8_t hol_v = sound_sheaf_holonomy_v4_3(g01, g12, g20);
                bool cob_v = sound_sheaf_is_coboundary_v4_3(g01, g12, g20);
                if (cob_v != (hol_v == UINT8_C(0))) {
                    gauge_ok = false;
                }
                if (cob_v) {
                    out->c3_v4_coboundary++;
                }

                for (uint32_t p1 = 0U; p1 < 4U; p1++) {
                    uint8_t sg01 = (uint8_t)(((uint32_t)g01 + p1) & 3U);
                    uint8_t sg12 = (uint8_t)(((uint32_t)g12 + 2U - p1) & 3U);
                    uint8_t sg20 = (uint8_t)(((uint32_t)g20 - 2U) & 3U);
                    if (sound_sheaf_holonomy_z4_3(sg01, sg12, sg20) != hol_z) {
                        gauge_ok = false;
                    }
                }
            }
        }
    }

    for (uint8_t g01 = 0U; g01 < 4U; g01++) {
        for (uint8_t g12 = 0U; g12 < 4U; g12++) {
            out->p3_z4_total++;
            if (sound__sheaf_is_coboundary_z4_p3(g01, g12)) {
                out->p3_z4_coboundary++;
            }
            for (uint8_t g23 = 0U; g23 < 4U; g23++) {
                for (uint8_t g30 = 0U; g30 < 4U; g30++) {
                    out->c4_z4_total++;
                    if (sound__sheaf_is_coboundary_z4_4(g01, g12, g23, g30)) {
                        out->c4_z4_coboundary++;
                    } else {
                        out->c4_z4_obstructed++;
                    }
                }
            }
        }
    }
    out->gauge_invariance_ok = gauge_ok ? UINT32_C(1) : UINT32_C(0);
}

void sound_hunt_sheaf_h1(uint64_t seed, sound_sheaf_witness_t *out) {
    if (out == NULL) {
        return;
    }
    sound_zero_bytes(out, sizeof(*out));
    uint64_t rng = seed ^ UINT64_C(0x53484541464831);
    uint64_t d0 = sound__splitmix64(&rng);
    uint64_t d1 = sound__splitmix64(&rng);
    uint64_t d2 = sound__splitmix64(&rng);

    out->seed = seed;
    /* Canonical non-trivial H^1 in Z4 + seeded gauge shift */
    uint8_t base_twist = ((d0 & UINT64_C(1)) == UINT64_C(0)) ? UINT8_C(1) : UINT8_C(3);
    out->phi[0] = (uint8_t)((d0 >> 2U) & UINT64_C(3));
    out->phi[1] = (uint8_t)((d0 >> 4U) & UINT64_C(3));
    out->phi[2] = (uint8_t)((d0 >> 6U) & UINT64_C(3));

    out->g01 = base_twist;
    out->g12 = base_twist;
    out->g20 = base_twist;
    out->holonomy_z4 = sound_sheaf_holonomy_z4_3(out->g01, out->g12, out->g20);

    out->sg01 =
        (uint8_t)(((uint32_t)out->g01 + (uint32_t)out->phi[1] - (uint32_t)out->phi[0]) & 3U);
    out->sg12 =
        (uint8_t)(((uint32_t)out->g12 + (uint32_t)out->phi[2] - (uint32_t)out->phi[1]) & 3U);
    out->sg20 =
        (uint8_t)(((uint32_t)out->g20 + (uint32_t)out->phi[0] - (uint32_t)out->phi[2]) & 3U);
    out->gauge_holonomy_z4 = sound_sheaf_holonomy_z4_3(out->sg01, out->sg12, out->sg20);

    out->is_coboundary_z4 =
        sound_sheaf_is_coboundary_z4_3(out->sg01, out->sg12, out->sg20) ? UINT8_C(1) : UINT8_C(0);
    out->skew_symmetric_ok = (out->g01 != UINT8_C(0) && out->holonomy_z4 == out->gauge_holonomy_z4)
                                 ? UINT8_C(1)
                                 : UINT8_C(0);

    uint64_t twist64 = UINT64_C(1) << (uint32_t)(d2 & UINT64_C(63));
    out->t01 = d1;
    out->t12 = d2;
    out->t20 = d1 ^ d2 ^ twist64;
    out->xor_holonomy = out->t01 ^ out->t12 ^ out->t20;

    uint64_t mh = SOUND_FNV1A_OFFSET;
    mh = sound_fnv1a_u64(mh, (uint64_t)out->holonomy_z4);
    mh = sound_fnv1a_u64(mh, (uint64_t)out->sg01);
    mh = sound_fnv1a_u64(mh, (uint64_t)out->sg12);
    mh = sound_fnv1a_u64(mh, (uint64_t)out->sg20);
    mh = sound_fnv1a_u64(mh, out->xor_holonomy);
    out->merkle = mh;
}

/* ============================================================================
 * Pillar 3: Complete 8-Position Hohfeld V4 Group, Chisholm & Norm Trap
 * ============================================================================ */

sound_hohfeld_pos_t sound_hohfeld_correlative(sound_hohfeld_pos_t p) {
    if (p == SOUND_HOHFELD_CLAIM) {
        return SOUND_HOHFELD_DUTY;
    }
    if (p == SOUND_HOHFELD_DUTY) {
        return SOUND_HOHFELD_CLAIM;
    }
    if (p == SOUND_HOHFELD_PRIVILEGE) {
        return SOUND_HOHFELD_NORIGHT;
    }
    if (p == SOUND_HOHFELD_NORIGHT) {
        return SOUND_HOHFELD_PRIVILEGE;
    }
    if (p == SOUND_HOHFELD_POWER) {
        return SOUND_HOHFELD_LIABILITY;
    }
    if (p == SOUND_HOHFELD_LIABILITY) {
        return SOUND_HOHFELD_POWER;
    }
    if (p == SOUND_HOHFELD_IMMUNITY) {
        return SOUND_HOHFELD_DISABILITY;
    }
    return SOUND_HOHFELD_IMMUNITY;
}

sound_hohfeld_pos_t sound_hohfeld_opposite(sound_hohfeld_pos_t p) {
    if (p == SOUND_HOHFELD_CLAIM) {
        return SOUND_HOHFELD_NORIGHT;
    }
    if (p == SOUND_HOHFELD_NORIGHT) {
        return SOUND_HOHFELD_CLAIM;
    }
    if (p == SOUND_HOHFELD_PRIVILEGE) {
        return SOUND_HOHFELD_DUTY;
    }
    if (p == SOUND_HOHFELD_DUTY) {
        return SOUND_HOHFELD_PRIVILEGE;
    }
    if (p == SOUND_HOHFELD_POWER) {
        return SOUND_HOHFELD_DISABILITY;
    }
    if (p == SOUND_HOHFELD_DISABILITY) {
        return SOUND_HOHFELD_POWER;
    }
    if (p == SOUND_HOHFELD_IMMUNITY) {
        return SOUND_HOHFELD_LIABILITY;
    }
    return SOUND_HOHFELD_IMMUNITY;
}

sound_hohfeld_pos_t sound_hohfeld_dual(sound_hohfeld_pos_t p) {
    return sound_hohfeld_opposite(sound_hohfeld_correlative(p));
}

bool sound_hohfeld_is_first_order(sound_hohfeld_pos_t p) {
    return (uint32_t)p <= (uint32_t)SOUND_HOHFELD_NORIGHT;
}

uint32_t sound_lex_specialis_pair_energy(bool keep_a, bool keep_b, uint16_t wa, uint16_t wb) {
    uint32_t e = UINT32_C(0);
    if (keep_a && keep_b) {
        e += UINT32_C(65536);
    }
    if (!keep_a && !keep_b) {
        e += UINT32_C(16384);
    }
    if (!keep_a) {
        e += (uint32_t)wa;
    }
    if (!keep_b) {
        e += (uint32_t)wb;
    }
    return e;
}

static uint32_t sound__bfs_reachable_3slot(uint8_t power, uint8_t immunity) {
    uint8_t vis[8][8];
    sound_zero_bytes(vis, sizeof(vis));
    uint8_t q_pos[64];
    uint8_t q_neg[64];
    uint32_t head = UINT32_C(0);
    uint32_t tail = UINT32_C(0);
    vis[0][0] = UINT8_C(1);
    q_pos[tail] = UINT8_C(0);
    q_neg[tail] = UINT8_C(0);
    tail++;
    while (head < tail) {
        uint8_t cp = q_pos[head];
        uint8_t cn = q_neg[head];
        head++;
        for (uint32_t slot = 0U; slot < 3U; slot++) {
            uint32_t bit = (UINT32_C(1) << slot);
            for (uint32_t act = 0U; act < 16U; act++) {
                bool gp = (act & UINT32_C(1)) != UINT32_C(0);
                bool rp = (act & UINT32_C(2)) != UINT32_C(0);
                bool gn = (act & UINT32_C(4)) != UINT32_C(0);
                bool rn = (act & UINT32_C(8)) != UINT32_C(0);
                if ((((uint32_t)immunity & bit) != 0U) || (((uint32_t)power & bit) == 0U)) {
                    continue;
                }
                uint32_t dp = gp ? ((uint32_t)cp | bit) : (uint32_t)cp;
                dp = rp ? (dp & (~bit)) : dp;
                uint32_t dn = gn ? ((uint32_t)cn | bit) : (uint32_t)cn;
                dn = rn ? (dn & (~bit)) : dn;
                if ((dp & dn) == 0U && vis[dp][dn] == UINT8_C(0)) {
                    vis[dp][dn] = UINT8_C(1);
                    q_pos[tail] = (uint8_t)dp;
                    q_neg[tail] = (uint8_t)dn;
                    tail++;
                }
            }
        }
    }
    return tail;
}

void sound_classify_hohfeld(sound_hohfeld_atlas_t *out) {
    if (out == NULL) {
        return;
    }
    sound_zero_bytes(out, sizeof(*out));
    bool v4_ok = true;
    for (uint32_t i = 0U; i < 8U; i++) {
        sound_hohfeld_pos_t p = (sound_hohfeld_pos_t)i;
        sound_hohfeld_pos_t c = sound_hohfeld_correlative(p);
        sound_hohfeld_pos_t o = sound_hohfeld_opposite(p);
        sound_hohfeld_pos_t d = sound_hohfeld_dual(p);
        if (sound_hohfeld_correlative(c) != p || sound_hohfeld_opposite(o) != p ||
            sound_hohfeld_dual(d) != p ||
            sound_hohfeld_opposite(c) != sound_hohfeld_correlative(o) || c == p || o == p ||
            d == p || sound_hohfeld_is_first_order(c) != sound_hohfeld_is_first_order(p) ||
            sound_hohfeld_is_first_order(o) != sound_hohfeld_is_first_order(p)) {
            v4_ok = false;
        }
    }
    out->v4_group_ok = v4_ok ? UINT32_C(1) : UINT32_C(0);
    out->v4_orbits = UINT32_C(2);

    SoundArena tmp;
    for (uint8_t pos = 0U; pos < 8U; pos++) {
        for (uint8_t neg = 0U; neg < 8U; neg++) {
            if ((pos & neg) != UINT8_C(0)) {
                continue;
            }
            for (uint8_t pow = 0U; pow < 8U; pow++) {
                for (uint8_t imm = 0U; imm < 8U; imm++) {
                    for (uint32_t slot = 0U; slot < 3U; slot++) {
                        for (uint32_t act = 0U; act < 16U; act++) {
                            out->transitions_checked++;
                            sound_arena_init(&tmp);
                            tmp.map.img_hdr.num_nodes = 3U;
                            tmp.map.norm.duty_pos_mask = (uint64_t)pos;
                            tmp.map.norm.duty_neg_mask = (uint64_t)neg;
                            tmp.map.norm.power_mask = (uint64_t)pow;
                            tmp.map.norm.immunity_mask = (uint64_t)imm;
                            (void)sound_apply_potestative(&tmp, slot, (act & 1U) != 0U,
                                                          (act & 2U) != 0U, (act & 4U) != 0U,
                                                          (act & 8U) != 0U);
                            uint64_t np = tmp.map.norm.duty_pos_mask;
                            uint64_t nn = tmp.map.norm.duty_neg_mask;
                            if ((np & nn) != UINT64_C(0)) {
                                out->coherence_violations++;
                            }
                            if ((np & (uint64_t)imm) != ((uint64_t)pos & (uint64_t)imm) ||
                                (nn & (uint64_t)imm) != ((uint64_t)neg & (uint64_t)imm)) {
                                out->immunity_leaks++;
                            }
                            uint64_t dis_mask = (~(uint64_t)pow) & UINT64_C(7);
                            if ((np & dis_mask) != ((uint64_t)pos & dis_mask) ||
                                (nn & dis_mask) != ((uint64_t)neg & dis_mask)) {
                                out->disability_leaks++;
                            }
                        }
                    }
                }
            }
        }
    }
    out->reachable_imm0 = sound__bfs_reachable_3slot(UINT8_C(7), UINT8_C(0));
    out->reachable_imm1 = sound__bfs_reachable_3slot(UINT8_C(7), UINT8_C(1));
    out->reachable_imm2 = sound__bfs_reachable_3slot(UINT8_C(7), UINT8_C(3));
    out->reachable_imm3 = sound__bfs_reachable_3slot(UINT8_C(7), UINT8_C(7));
}

void sound_hunt_chisholm(sound_chisholm_witness_t *out) {
    if (out == NULL) {
        return;
    }
    sound_zero_bytes(out, sizeof(*out));
    SoundArena arena;
    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 4U;

    /* Node 0: go_help (revoked factual failure) */
    arena.map.node_meta[0].sort = (uint8_t)SOUND_SORT_FACTUAL;
    arena.map.node_meta[0].evid = (uint8_t)SOUND_EVID_DIRECT;
    arena.map.node_meta[0].num_clauses = 1U;
    arena.map.norm.revoked_mask = UINT64_C(1);

    /* Node 1: primary tell_going (conditional on node 0, attacked by node 2) */
    arena.map.node_meta[1].sort = (uint8_t)SOUND_SORT_NORMATIVE;
    arena.map.node_meta[1].evid = (uint8_t)SOUND_EVID_INFERENTIAL;
    arena.map.node_meta[1].flags = SOUND_NODE_FLAG_CONSTITUTIVE;
    arena.map.node_meta[1].num_clauses = 1U;
    arena.map.node_meta[1].attack_mask = UINT64_C(4);
    arena.map.node_clauses[1].clauses[0] = UINT64_C(1);

    /* Node 2: CTD tell_not_going (enacted constitutive norm triggered by node 3) */
    arena.map.node_meta[2].sort = (uint8_t)SOUND_SORT_NORMATIVE;
    arena.map.node_meta[2].evid = (uint8_t)SOUND_EVID_INFERENTIAL;
    arena.map.node_meta[2].flags = SOUND_NODE_FLAG_CONSTITUTIVE;
    arena.map.node_meta[2].num_clauses = 1U;
    arena.map.node_clauses[2].clauses[0] = UINT64_C(8);

    /* Node 3: factual_no_help (direct observation) */
    arena.map.node_meta[3].sort = (uint8_t)SOUND_SORT_FACTUAL;
    arena.map.node_meta[3].evid = (uint8_t)SOUND_EVID_DIRECT;
    arena.map.node_meta[3].num_clauses = 1U;

    (void)sound_eval(&arena);

    out->num_nodes = 4U;
    out->established_mask = arena.map.eval_out.established_mask;
    out->defeated_mask = arena.map.eval_out.defeated_mask;
    out->conflict_mask = arena.map.eval_out.conflict_mask;
    out->penumbra_mask = arena.map.eval_out.penumbra_mask;
    for (uint32_t i = 0U; i < 4U; i++) {
        out->statuses[i] = arena.map.eval_status.status[i];
    }
    out->hohfeld_ok = sound_hohfeld_coherent(&arena) ? UINT32_C(1) : UINT32_C(0);

    uint64_t mh = SOUND_FNV1A_OFFSET;
    mh = sound_fnv1a_u64(mh, out->established_mask);
    mh = sound_fnv1a_u64(mh, out->defeated_mask);
    mh = sound_fnv1a_u64(mh, out->conflict_mask);
    out->merkle = mh;
}

void sound_hunt_norm_trap(uint64_t seed, sound_norm_trap_witness_t *out) {
    if (out == NULL) {
        return;
    }
    sound_zero_bytes(out, sizeof(*out));
    SoundArena arena;
    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 4U;
    arena.map.norm.duty_pos_mask = UINT64_C(0x5); /* bits 0, 2 */
    arena.map.norm.duty_neg_mask = UINT64_C(0x2); /* bit 1 */
    arena.map.norm.power_mask = UINT64_C(0x7);    /* bits 0, 1, 2 (bit 3 disability) */
    arena.map.norm.immunity_mask = UINT64_C(0x4); /* bit 2 KanLock immune */

    out->init_pos = arena.map.norm.duty_pos_mask;
    out->init_neg = arena.map.norm.duty_neg_mask;
    out->power = arena.map.norm.power_mask;
    out->immunity = arena.map.norm.immunity_mask;
    out->seed = seed;

    /* 4-step trace: (slot 0 valid revoke), (slot 2 immunity hold), (slot 3 no-power hold),
       (slot 1 incoherent clash hold) */
    out->step_slot[0] = UINT8_C(0);
    out->step_fiber[0] =
        (uint8_t)sound_apply_potestative(&arena, 0U, false, true, false, false); /* committed */
    out->step_slot[1] = UINT8_C(2);
    out->step_fiber[1] =
        (uint8_t)sound_apply_potestative(&arena, 2U, false, true, false, false); /* hold_immunity */
    out->step_slot[2] = UINT8_C(3);
    out->step_fiber[2] =
        (uint8_t)sound_apply_potestative(&arena, 3U, true, false, false, false); /* hold_no_power */
    out->step_slot[3] = UINT8_C(1);
    out->step_fiber[3] =
        (uint8_t)sound_apply_potestative(&arena, 1U, true, false, false, false); /* hold_incoh */

    out->final_pos = arena.map.norm.duty_pos_mask;
    out->final_neg = arena.map.norm.duty_neg_mask;

    bool imm_ok = ((out->final_pos & out->immunity) == (out->init_pos & out->immunity)) &&
                  ((out->final_neg & out->immunity) == (out->init_neg & out->immunity));
    bool dis_ok = ((out->final_pos & (~out->power)) == (out->init_pos & (~out->power))) &&
                  ((out->final_neg & (~out->power)) == (out->init_neg & (~out->power)));
    bool coh_ok = (out->final_pos & out->final_neg) == UINT64_C(0);
    out->freeze_ok = (imm_ok && dis_ok && coh_ok) ? UINT32_C(1) : UINT32_C(0);

    uint64_t r = seed ^ UINT64_C(0x4E4F524D54524150);
    uint64_t d = sound__splitmix64(&r);
    uint16_t w = (uint16_t)(UINT32_C(1) + (uint32_t)(d & UINT64_C(0x3F)));
    out->tie_weight = w;
    out->energy_keep_a = sound_lex_specialis_pair_energy(true, false, w, w);
    out->energy_keep_b = sound_lex_specialis_pair_energy(false, true, w, w);
    out->energy_both = sound_lex_specialis_pair_energy(true, true, w, w);
    out->energy_neither = sound_lex_specialis_pair_energy(false, false, w, w);
    out->kanlock_win_energy =
        sound_lex_specialis_pair_energy(false, true, UINT16_C(255), UINT16_C(1));
    out->kanlock_both_energy =
        sound_lex_specialis_pair_energy(true, true, UINT16_C(255), UINT16_C(1));

    uint64_t mh = SOUND_FNV1A_OFFSET;
    mh = sound_fnv1a_u64(mh, out->final_pos);
    mh = sound_fnv1a_u64(mh, out->final_neg);
    mh = sound_fnv1a_u64(mh, (uint64_t)out->tie_weight);
    mh = sound_fnv1a_u64(mh, (uint64_t)out->kanlock_win_energy);
    out->merkle = mh;
}

/* ============================================================================
 * Pillar 4: Vendler Aspect, 4-Valued Presupposition & Illocutionary Force
 * ============================================================================ */

bool sound_aspect_is_dynamic(sound_aspect_t a) {
    return (a == SOUND_ASPECT_ACTIVITY || a == SOUND_ASPECT_ACCOMPLISHMENT);
}

bool sound_aspect_is_telic(sound_aspect_t a) {
    return (a == SOUND_ASPECT_ACCOMPLISHMENT || a == SOUND_ASPECT_ACHIEVEMENT);
}

sound_aspect_t sound_aspect_of_features(bool dyn, bool telic) {
    if (!dyn && !telic) {
        return SOUND_ASPECT_STATE;
    }
    if (dyn && !telic) {
        return SOUND_ASPECT_ACTIVITY;
    }
    if (dyn && telic) {
        return SOUND_ASPECT_ACCOMPLISHMENT;
    }
    return SOUND_ASPECT_ACHIEVEMENT;
}

bool sound_aspect_constitutive_ok(sound_aspect_t a, bool requires_telic) {
    return (!requires_telic) || sound_aspect_is_telic(a);
}

uint8_t sound_presup_eval(uint8_t assert_val, uint8_t presup_val) {
    uint32_t pv = (uint32_t)presup_val & 3U;
    uint32_t av = (uint32_t)assert_val & 3U;
    return (pv == (uint32_t)SOUND_B4_TRUE) ? (uint8_t)av : (uint8_t)SOUND_B4_BOT;
}

void sound_presup_neg(uint8_t assert_val, uint8_t presup_val, uint8_t *out_assert,
                      uint8_t *out_presup) {
    if (out_assert != NULL) {
        *out_assert = sound_b4_neg(assert_val);
    }
    if (out_presup != NULL) {
        *out_presup = (uint8_t)((uint32_t)presup_val & 3U);
    }
}

void sound_presup_irrealis(uint8_t assert_val, uint8_t presup_val, uint8_t *out_assert,
                           uint8_t *out_presup) {
    (void)assert_val;
    (void)presup_val;
    if (out_assert != NULL) {
        *out_assert = (uint8_t)SOUND_B4_BOT;
    }
    if (out_presup != NULL) {
        *out_presup = (uint8_t)SOUND_B4_TRUE;
    }
}

void sound_presup_cond(uint8_t a_assert, uint8_t a_presup, uint8_t b_assert, uint8_t b_presup,
                       uint8_t *out_assert, uint8_t *out_presup) {
    uint8_t aa = (uint8_t)((uint32_t)a_assert & 3U);
    uint8_t ap = (uint8_t)((uint32_t)a_presup & 3U);
    uint8_t ba = (uint8_t)((uint32_t)b_assert & 3U);
    uint8_t bp = (uint8_t)((uint32_t)b_presup & 3U);
    if (out_assert != NULL) {
        *out_assert = sound_b4_or_t(sound_b4_neg(aa), ba);
    }
    if (out_presup != NULL) {
        *out_presup = (aa == (uint8_t)SOUND_B4_TRUE) ? sound_b4_and_t(ap, bp) : ap;
    }
}

sound_fiber_t sound_force_apply(sound_world_state_t *st, sound_force_t force, uint64_t slot_bit,
                                sound_aspect_t aspect, bool gp, bool rp, bool gn, bool rn) {
    if (st == NULL) {
        return SOUND_FIBER_FALLBACK_NULL;
    }
    if (force == SOUND_FORCE_ASSERTION) {
        st->epistemic |= slot_bit;
        return SOUND_FIBER_COMMITTED;
    }
    if (force == SOUND_FORCE_QUERY) {
        return SOUND_FIBER_COMMITTED;
    }
    if (force == SOUND_FORCE_IRREALIS) {
        st->modal |= slot_bit;
        return SOUND_FIBER_FALLBACK_IRREALIS;
    }
    if (force == SOUND_FORCE_DECLARATIVE && !sound_aspect_is_telic(aspect)) {
        return SOUND_FIBER_FALLBACK_POLICY_VIOLATION;
    }

    bool imm_hit = (st->immunity & slot_bit) != UINT64_C(0);
    bool pow_ok = (st->power & slot_bit) != UINT64_C(0);
    uint64_t dp0 = gp ? (st->duty_pos | slot_bit) : st->duty_pos;
    uint64_t dp = rp ? (dp0 & (~slot_bit)) : dp0;
    uint64_t dn0 = gn ? (st->duty_neg | slot_bit) : st->duty_neg;
    uint64_t dn = rn ? (dn0 & (~slot_bit)) : dn0;
    bool coh_ok = (dp & dn) == UINT64_C(0);

    if (imm_hit) {
        return SOUND_FIBER_HOLD_IMMUNITY;
    }
    if (!pow_ok) {
        return SOUND_FIBER_HOLD_NO_POWER;
    }
    if (!coh_ok) {
        return SOUND_FIBER_HOLD_INCOHERENT;
    }
    st->duty_pos = dp;
    st->duty_neg = dn;
    if (force == SOUND_FORCE_DECLARATIVE) {
        st->epistemic |= slot_bit;
    }
    return SOUND_FIBER_COMMITTED;
}

void sound_classify_projection(sound_projection_atlas_t *out) {
    if (out == NULL) {
        return;
    }
    sound_zero_bytes(out, sizeof(*out));
    for (uint32_t code = 0U; code < 256U; code++) {
        uint8_t f[4];
        f[0] = (uint8_t)(code & UINT32_C(3));
        f[1] = (uint8_t)((code >> 2U) & UINT32_C(3));
        f[2] = (uint8_t)((code >> 4U) & UINT32_C(3));
        f[3] = (uint8_t)((code >> 6U) & UINT32_C(3));
        out->total_unary_maps++;

        bool inv = true;
        bool t_anti = true;
        bool k_mono = true;
        for (uint8_t x = 0U; x < 4U; x++) {
            if (f[f[x]] != x) {
                inv = false;
            }
            for (uint8_t y = 0U; y < 4U; y++) {
                if (sound_b4_le_t(x, y) && !sound_b4_le_t(f[y], f[x])) {
                    t_anti = false;
                }
                if (sound_b4_le_k(x, y) && !sound_b4_le_k(f[x], f[y])) {
                    k_mono = false;
                }
            }
        }
        if (inv) {
            out->involutive_maps++;
        }
        if (t_anti) {
            out->t_antitone_maps++;
        }
        if (k_mono) {
            out->k_monotone_maps++;
        }
        if (inv && t_anti && k_mono && f[SOUND_B4_TRUE] == (uint8_t)SOUND_B4_FALSE &&
            f[SOUND_B4_FALSE] == (uint8_t)SOUND_B4_TRUE) {
            out->classical_neg_maps++;
        }
    }

    bool filt_ok = true;
    bool plug_ok = true;
    for (uint8_t a = 0U; a < 4U; a++) {
        for (uint8_t p = 0U; p < 4U; p++) {
            uint8_t na = UINT8_C(0);
            uint8_t np = UINT8_C(0);
            sound_presup_neg(a, p, &na, &np);
            if (np == p) {
                out->hole_presup_pairs++;
            }
            uint8_t ev0 = sound_presup_eval(a, p);
            uint8_t ev1 = sound_presup_eval(na, np);
            if (p != (uint8_t)SOUND_B4_TRUE && ev0 == (uint8_t)SOUND_B4_BOT &&
                ev1 == (uint8_t)SOUND_B4_BOT) {
                out->strawson_gap_pairs++;
            }
            uint8_t pa = UINT8_C(0);
            uint8_t pp = UINT8_C(0);
            sound_presup_irrealis(a, p, &pa, &pp);
            if (pp != (uint8_t)SOUND_B4_TRUE ||
                sound_presup_eval(pa, pp) != (uint8_t)SOUND_B4_BOT) {
                plug_ok = false;
            }
            for (uint8_t b_a = 0U; b_a < 4U; b_a++) {
                for (uint8_t b_p = 0U; b_p < 4U; b_p++) {
                    uint8_t ca = UINT8_C(0);
                    uint8_t cp = UINT8_C(0);
                    sound_presup_cond(a, p, b_a, b_p, &ca, &cp);
                    if (p == (uint8_t)SOUND_B4_TRUE && a == (uint8_t)SOUND_B4_FALSE &&
                        cp != (uint8_t)SOUND_B4_TRUE) {
                        filt_ok = false;
                    }
                    if (p == (uint8_t)SOUND_B4_TRUE && a == (uint8_t)SOUND_B4_TRUE && cp != b_p) {
                        filt_ok = false;
                    }
                    if (p != (uint8_t)SOUND_B4_TRUE && cp == (uint8_t)SOUND_B4_TRUE) {
                        filt_ok = false;
                    }
                }
            }
        }
    }
    out->cond_filter_ok = filt_ok ? UINT32_C(1) : UINT32_C(0);
    out->plug_irrealis_ok = plug_ok ? UINT32_C(1) : UINT32_C(0);

    bool force_ok = true;
    sound_world_state_t base = {UINT64_C(0x1), UINT64_C(0x2), UINT64_C(0x4),
                                UINT64_C(0x9), UINT64_C(0x2), UINT64_C(0x0)};
    sound_world_state_t st = base;
    if (sound_force_apply(&st, SOUND_FORCE_ASSERTION, UINT64_C(8), SOUND_ASPECT_STATE, true, false,
                          false, false) != SOUND_FIBER_COMMITTED ||
        st.duty_pos != base.duty_pos || st.duty_neg != base.duty_neg ||
        st.epistemic != (base.epistemic | UINT64_C(8))) {
        force_ok = false;
    }
    st = base;
    if (sound_force_apply(&st, SOUND_FORCE_QUERY, UINT64_C(8), SOUND_ASPECT_STATE, true, false,
                          false, false) != SOUND_FIBER_COMMITTED ||
        st.epistemic != base.epistemic || st.duty_pos != base.duty_pos || st.modal != base.modal) {
        force_ok = false;
    }
    st = base;
    if (sound_force_apply(&st, SOUND_FORCE_IRREALIS, UINT64_C(8), SOUND_ASPECT_ACTIVITY, true,
                          false, false, false) != SOUND_FIBER_FALLBACK_IRREALIS ||
        st.epistemic != base.epistemic || st.duty_pos != base.duty_pos || st.modal != UINT64_C(8)) {
        force_ok = false;
    }
    st = base;
    if (sound_force_apply(&st, SOUND_FORCE_DECLARATIVE, UINT64_C(8), SOUND_ASPECT_ACTIVITY, true,
                          false, false, false) != SOUND_FIBER_FALLBACK_POLICY_VIOLATION ||
        st.duty_pos != base.duty_pos || st.epistemic != base.epistemic) {
        force_ok = false;
    }
    st = base;
    if (sound_force_apply(&st, SOUND_FORCE_DIRECTIVE, UINT64_C(8), SOUND_ASPECT_STATE, true, false,
                          false, false) != SOUND_FIBER_COMMITTED ||
        st.epistemic != base.epistemic || st.duty_pos != (base.duty_pos | UINT64_C(8))) {
        force_ok = false;
    }
    out->force_invariants_ok = force_ok ? UINT32_C(1) : UINT32_C(0);
}

void sound_hunt_aspect_firewall(uint64_t seed, sound_aspect_witness_t *out) {
    if (out == NULL) {
        return;
    }
    sound_zero_bytes(out, sizeof(*out));
    out->seed = seed;
    bool bij_ok = true;
    uint32_t blocked_mask = UINT32_C(0);
    for (uint32_t a = 0U; a < 4U; a++) {
        sound_aspect_t asp = (sound_aspect_t)a;
        if (sound_aspect_of_features(sound_aspect_is_dynamic(asp), sound_aspect_is_telic(asp)) !=
            asp) {
            bij_ok = false;
        }
        if (!sound_aspect_constitutive_ok(asp, true)) {
            blocked_mask |= (UINT32_C(1) << a);
        }
    }
    out->aspect_bijection_ok = bij_ok ? UINT32_C(1) : UINT32_C(0);
    out->telic_blocked_mask = blocked_mask;
    uint64_t r = seed ^ UINT64_C(0x415350454354);
    uint64_t d = sound__splitmix64(&r);
    out->j_start = (uint32_t)(d & UINT64_C(7));
    out->j_end = out->j_start + UINT32_C(3);
    out->i_start = out->j_start;
    out->i_end = out->j_start + UINT32_C(1);

    out->state_gate_telic = sound_aspect_constitutive_ok(SOUND_ASPECT_STATE, true) ? 1U : 0U;
    out->activity_gate_telic = sound_aspect_constitutive_ok(SOUND_ASPECT_ACTIVITY, true) ? 1U : 0U;
    out->accomplishment_gate_telic =
        sound_aspect_constitutive_ok(SOUND_ASPECT_ACCOMPLISHMENT, true) ? 1U : 0U;
    out->achievement_gate_telic =
        sound_aspect_constitutive_ok(SOUND_ASPECT_ACHIEVEMENT, true) ? 1U : 0U;

    sound_world_state_t st = {UINT64_C(0), UINT64_C(0), UINT64_C(0),
                              UINT64_C(1), UINT64_C(0), UINT64_C(0)};
    out->declarative_atelic_fiber =
        (uint32_t)sound_force_apply(&st, SOUND_FORCE_DECLARATIVE, UINT64_C(1),
                                    SOUND_ASPECT_ACTIVITY, true, false, false, false);

    uint64_t mh = SOUND_FNV1A_OFFSET;
    mh = sound_fnv1a_u64(mh, (uint64_t)out->telic_blocked_mask);
    mh = sound_fnv1a_u64(mh, (uint64_t)out->j_start);
    mh = sound_fnv1a_u64(mh, (uint64_t)out->declarative_atelic_fiber);
    out->merkle = mh;
}

void sound_hunt_presup_filter(uint64_t seed, sound_presup_witness_t *out) {
    if (out == NULL) {
        return;
    }
    sound_zero_bytes(out, sizeof(*out));
    out->seed = seed;
    uint64_t r = seed ^ UINT64_C(0x505245535550);
    uint64_t d = sound__splitmix64(&r);

    out->a_assert = (uint8_t)SOUND_B4_FALSE;
    out->a_presup = (uint8_t)SOUND_B4_TRUE;
    out->b_assert =
        ((d & UINT64_C(1)) == UINT64_C(0)) ? (uint8_t)SOUND_B4_TRUE : (uint8_t)SOUND_B4_FALSE;
    out->b_presup =
        ((d & UINT64_C(2)) == UINT64_C(0)) ? (uint8_t)SOUND_B4_BOT : (uint8_t)SOUND_B4_FALSE;

    uint8_t neg_ba = UINT8_C(0);
    uint8_t neg_bp = UINT8_C(0);
    sound_presup_neg(out->b_assert, out->b_presup, &neg_ba, &neg_bp);
    out->eval_fail_b = sound_presup_eval(out->b_assert, out->b_presup);
    out->eval_neg_fail_b = sound_presup_eval(neg_ba, neg_bp);

    uint8_t pa = UINT8_C(0);
    sound_presup_irrealis(out->b_assert, out->b_presup, &pa, &out->plug_presup);
    out->plug_eval = sound_presup_eval(pa, out->plug_presup);

    uint8_t ca = UINT8_C(0);
    sound_presup_cond(out->a_assert, out->a_presup, out->b_assert, out->b_presup, &ca,
                      &out->cond_false_presup);
    sound_presup_cond((uint8_t)SOUND_B4_TRUE, (uint8_t)SOUND_B4_TRUE, out->b_assert, out->b_presup,
                      &ca, &out->cond_true_presup);

    sound_world_state_t st = {UINT64_C(0x1), UINT64_C(0x2), UINT64_C(0x0),
                              UINT64_C(0x4), UINT64_C(0x2), UINT64_C(0x0)};
    sound_world_state_t orig = st;
    out->force_hume_ok =
        (sound_force_apply(&st, SOUND_FORCE_ASSERTION, UINT64_C(8), SOUND_ASPECT_STATE, true, false,
                           false, false) == SOUND_FIBER_COMMITTED &&
         st.duty_pos == orig.duty_pos && st.duty_neg == orig.duty_neg)
            ? UINT8_C(1)
            : UINT8_C(0);
    st = orig;
    out->force_query_ok =
        (sound_force_apply(&st, SOUND_FORCE_QUERY, UINT64_C(8), SOUND_ASPECT_STATE, true, false,
                           false, false) == SOUND_FIBER_COMMITTED &&
         st.epistemic == orig.epistemic && st.duty_pos == orig.duty_pos)
            ? UINT8_C(1)
            : UINT8_C(0);
    st = orig;
    out->force_irrealis_ok =
        (sound_force_apply(&st, SOUND_FORCE_IRREALIS, UINT64_C(8), SOUND_ASPECT_STATE, true, false,
                           false, false) == SOUND_FIBER_FALLBACK_IRREALIS &&
         st.epistemic == orig.epistemic && st.duty_pos == orig.duty_pos && st.modal == UINT64_C(8))
            ? UINT8_C(1)
            : UINT8_C(0);
    st = orig;
    out->force_directive_ok =
        (sound_force_apply(&st, SOUND_FORCE_DIRECTIVE, UINT64_C(4), SOUND_ASPECT_STATE, true, false,
                           false, false) == SOUND_FIBER_COMMITTED &&
         st.epistemic == orig.epistemic && st.duty_pos == (orig.duty_pos | UINT64_C(4)))
            ? UINT8_C(1)
            : UINT8_C(0);

    uint64_t mh = SOUND_FNV1A_OFFSET;
    mh = sound_fnv1a_u64(mh, (uint64_t)out->b_assert);
    mh = sound_fnv1a_u64(mh, (uint64_t)out->b_presup);
    mh = sound_fnv1a_u64(mh, (uint64_t)out->cond_false_presup);
    mh = sound_fnv1a_u64(mh, (uint64_t)out->cond_true_presup);
    out->merkle = mh;
}

static uint64_t sound__sat_add_u64(uint64_t a, uint64_t b) {
    return (a > (UINT64_MAX - b)) ? UINT64_MAX : (a + b);
}

void sound_audit_contract(const sound_contract_spec_t *spec, sound_contract_audit_t *out) {
    if (out == NULL) {
        return;
    }
    sound_zero_bytes(out, sizeof(*out));
    if (spec == NULL) {
        out->viol_flags = (uint32_t)SOUND_CVIOL_UNCONSCIONABLE;
        return;
    }

    uint32_t n = spec->num_clauses;
    if (n > SOUND_CONTRACT_MAX_CLAUSES) {
        n = SOUND_CONTRACT_MAX_CLAUSES;
        out->viol_flags |= (uint32_t)SOUND_CVIOL_BENEFIT_DRIFT;
    }

    uint64_t base_sum = UINT64_C(0);
    uint64_t worst_sum = UINT64_C(0);
    uint64_t rep_worst_sum = UINT64_C(0);

    for (uint32_t i = 0U; i < n; i++) {
        const sound_contract_clause_t *c = &spec->clauses[i];
        base_sum = sound__sat_add_u64(base_sum, c->base_loss);
        worst_sum = sound__sat_add_u64(worst_sum, sound__sat_add_u64(c->base_loss, c->extra_drift));
        if (c->exit_fee > out->max_exit_seen) {
            out->max_exit_seen = c->exit_fee;
        }

        uint32_t c_viol = UINT32_C(0);
        if (c->kind == (uint8_t)SOUND_CLAUSE_UNILATERAL_POWER &&
            (c->target_mask & spec->protected_mask) != UINT64_C(0)) {
            c_viol |= (uint32_t)SOUND_CVIOL_IMMUNITY_BREACH;
        }
        if (c->b4_truth == (uint8_t)SOUND_B4_BOT || c->b4_truth == (uint8_t)SOUND_B4_TOP ||
            c->kind == (uint8_t)SOUND_CLAUSE_AMBIGUOUS_OBLIGATION) {
            c_viol |= (uint32_t)SOUND_CVIOL_UNINFORMED_AMBIG;
        }
        if (c->disclosed == UINT8_C(0) &&
            (c->base_loss > UINT64_C(0) || c->extra_drift > UINT64_C(0) ||
             c->exit_fee > UINT64_C(0))) {
            c_viol |= (uint32_t)SOUND_CVIOL_PRESUP_SMUGGLE;
        }
        if (((unsigned __int128)c->cons_penalty * (unsigned __int128)spec->max_asym_den) >
            ((unsigned __int128)c->inst_penalty * (unsigned __int128)spec->max_asym_num)) {
            c_viol |= (uint32_t)SOUND_CVIOL_UNCONSCIONABLE;
        }
        if (c->kind == (uint8_t)SOUND_CLAUSE_CHISHOLM_DEFAULT && c->extra_drift > UINT64_C(0)) {
            c_viol |= (uint32_t)SOUND_CVIOL_CHISHOLM_CASCADE;
        }
        if (c->sheaf_cycle != UINT8_C(0)) {
            c_viol |= (uint32_t)SOUND_CVIOL_SHEAF_CONFLICT;
        }
        if (c->exit_fee > spec->max_exit_fee) {
            c_viol |= (uint32_t)SOUND_CVIOL_EXIT_TRAP;
        }
        if (c->extra_drift > UINT64_C(0)) {
            c_viol |= (uint32_t)SOUND_CVIOL_BENEFIT_DRIFT;
        }

        out->clause_viol_masks[i] = c_viol;
        if (c_viol != UINT32_C(0)) {
            out->viol_flags |= c_viol;
            out->predatory_clause_ids[out->num_predatory_clauses] = c->clause_id;
            out->num_predatory_clauses++;
        } else {
            rep_worst_sum = sound__sat_add_u64(rep_worst_sum, c->base_loss);
        }
    }

    uint32_t ne = spec->num_edges;
    if (ne > SOUND_CONTRACT_MAX_EDGES) {
        ne = SOUND_CONTRACT_MAX_EDGES;
        out->viol_flags |= (uint32_t)SOUND_CVIOL_BENEFIT_DRIFT;
    }

    uint64_t edge_exp_sum = UINT64_C(0);
    uint32_t bypass_reach = UINT32_C(0);
    for (uint32_t i = 0U; i < n; i++) {
        if (spec->clauses[i].kind == (uint8_t)SOUND_CLAUSE_UNILATERAL_POWER) {
            bypass_reach |= (UINT32_C(1) << i);
        }
    }
    for (uint32_t ei = 0U; ei < ne; ei++) {
        const sound_contract_edge_t *e = &spec->edges[ei];
        edge_exp_sum = sound__sat_add_u64(edge_exp_sum, e->extra_exposure);
        bool is_benign_redirect = (e->edge_kind == (uint8_t)SOUND_EDGE_REDIRECT) &&
                                  ((e->cocycle_shift & UINT8_C(3)) == UINT8_C(0)) &&
                                  (e->extra_exposure == UINT64_C(0));
        if (!is_benign_redirect) {
            out->num_malicious_edges++;
        }
        if (e->edge_kind == (uint8_t)SOUND_EDGE_DEFAULT_TRIGGER &&
            e->extra_exposure > UINT64_C(0)) {
            out->viol_flags |= (uint32_t)SOUND_CVIOL_GRAPH_CASCADE;
        }
        if (e->dst < n && (e->edge_kind == (uint8_t)SOUND_EDGE_OVERRIDE ||
                           e->edge_kind == (uint8_t)SOUND_EDGE_POWER_DELEGATE)) {
            bypass_reach |= (UINT32_C(1) << e->dst);
        }
    }
    for (uint32_t step = 0U; step < n; step++) {
        uint32_t next_reach = bypass_reach;
        for (uint32_t ei = 0U; ei < ne; ei++) {
            const sound_contract_edge_t *e = &spec->edges[ei];
            if (e->src < n && e->dst < n &&
                (e->edge_kind == (uint8_t)SOUND_EDGE_OVERRIDE ||
                 e->edge_kind == (uint8_t)SOUND_EDGE_POWER_DELEGATE ||
                 e->edge_kind == (uint8_t)SOUND_EDGE_REDIRECT)) {
                if ((bypass_reach & (UINT32_C(1) << e->src)) != UINT32_C(0)) {
                    next_reach |= (UINT32_C(1) << e->dst);
                }
            }
        }
        bypass_reach = next_reach;
    }
    for (uint32_t i = 0U; i < n; i++) {
        if ((bypass_reach & (UINT32_C(1) << i)) != UINT32_C(0) &&
            (spec->clauses[i].target_mask & spec->protected_mask) != UINT64_C(0)) {
            out->graph_bypass_mask |= (UINT32_C(1) << i);
        }
    }
    if (out->graph_bypass_mask != UINT32_C(0)) {
        out->viol_flags |= (uint32_t)SOUND_CVIOL_GRAPH_BYPASS;
    }

    uint8_t pot[SOUND_CONTRACT_MAX_CLAUSES];
    uint8_t vis[SOUND_CONTRACT_MAX_CLAUSES];
    sound_zero_bytes(pot, sizeof(pot));
    sound_zero_bytes(vis, sizeof(vis));
    for (uint32_t root = 0U; root < n; root++) {
        if (vis[root] != UINT8_C(0)) {
            continue;
        }
        vis[root] = UINT8_C(1);
        pot[root] = UINT8_C(0);
        for (uint32_t step = 0U; step < n; step++) {
            for (uint32_t ei = 0U; ei < ne; ei++) {
                const sound_contract_edge_t *e = &spec->edges[ei];
                if (e->src < n && e->dst < n) {
                    uint32_t sh = (uint32_t)e->cocycle_shift & UINT32_C(3);
                    if (vis[e->src] != UINT8_C(0) && vis[e->dst] == UINT8_C(0)) {
                        vis[e->dst] = UINT8_C(1);
                        pot[e->dst] = (uint8_t)(((uint32_t)pot[e->src] + sh) & UINT32_C(3));
                    } else if (vis[e->src] == UINT8_C(0) && vis[e->dst] != UINT8_C(0)) {
                        vis[e->src] = UINT8_C(1);
                        pot[e->src] =
                            (uint8_t)(((uint32_t)pot[e->dst] + UINT32_C(4) - sh) & UINT32_C(3));
                    }
                }
            }
        }
    }
    for (uint32_t ei = 0U; ei < ne; ei++) {
        const sound_contract_edge_t *e = &spec->edges[ei];
        if (e->src < n && e->dst < n) {
            uint32_t sh = (uint32_t)e->cocycle_shift & UINT32_C(3);
            uint32_t diff =
                ((uint32_t)pot[e->dst] + UINT32_C(4) - (uint32_t)pot[e->src]) & UINT32_C(3);
            if (vis[e->src] == UINT8_C(0) || vis[e->dst] == UINT8_C(0) || diff != sh) {
                out->graph_sheaf_h1_obstructed = UINT32_C(1);
                out->viol_flags |= (uint32_t)SOUND_CVIOL_GRAPH_SHEAF_H1;
            }
        }
    }

    if (spec->coverage_gaps != UINT32_C(0)) {
        out->viol_flags |= (uint32_t)SOUND_CVIOL_COVERAGE_GAP;
    }

    worst_sum = sound__sat_add_u64(worst_sum, edge_exp_sum);
    if (worst_sum > spec->max_loss_cap) {
        out->viol_flags |= (uint32_t)SOUND_CVIOL_BENEFIT_DRIFT;
    }
    if (spec->max_asym_den == UINT64_C(0)) {
        out->viol_flags |= (uint32_t)SOUND_CVIOL_UNCONSCIONABLE;
    }

    int64_t w_i64 = (worst_sum <= (uint64_t)INT64_MAX) ? (int64_t)worst_sum : INT64_MAX;
    int64_t cap_i64 =
        (spec->max_loss_cap <= (uint64_t)INT64_MAX) ? (int64_t)spec->max_loss_cap : INT64_MAX;
    out->base_loss_sum = base_sum;
    out->edge_exposure_sum = edge_exp_sum;
    out->worst_case_loss = worst_sum;
    out->benefit_drift = w_i64 - cap_i64;
    out->repaired_worst_loss = rep_worst_sum;
    out->certified = (spec->num_clauses <= SOUND_CONTRACT_MAX_CLAUSES &&
                      spec->num_edges <= SOUND_CONTRACT_MAX_EDGES &&
                      out->viol_flags == UINT32_C(0) && out->num_predatory_clauses == UINT32_C(0))
                         ? UINT32_C(1)
                         : UINT32_C(0);
    out->repaired_certified =
        (spec->num_clauses <= SOUND_CONTRACT_MAX_CLAUSES &&
         spec->num_edges <= SOUND_CONTRACT_MAX_EDGES && spec->max_asym_den != UINT64_C(0) &&
         rep_worst_sum <= spec->max_loss_cap)
            ? UINT32_C(1)
            : UINT32_C(0);

    uint64_t mh = SOUND_FNV1A_OFFSET;
    mh = sound_fnv1a_u64(mh, (uint64_t)out->certified);
    mh = sound_fnv1a_u64(mh, (uint64_t)out->viol_flags);
    mh = sound_fnv1a_u64(mh, out->worst_case_loss);
    mh = sound_fnv1a_u64(mh, out->repaired_worst_loss);
    mh = sound_fnv1a_u64(mh, (uint64_t)out->num_predatory_clauses);
    mh = sound_fnv1a_u64(mh, (uint64_t)out->num_malicious_edges);
    mh = sound_fnv1a_u64(mh, (uint64_t)out->graph_sheaf_h1_obstructed);
    mh = sound_fnv1a_u64(mh, (uint64_t)out->graph_bypass_mask);
    mh = sound_fnv1a_u64(mh, out->edge_exposure_sum);
    out->merkle = mh;
}

void sound_hunt_contract_drift(uint64_t seed, sound_contract_spec_t *spec_out,
                               sound_contract_audit_t *audit_out) {
    if (spec_out == NULL || audit_out == NULL) {
        return;
    }
    sound_zero_bytes(spec_out, sizeof(*spec_out));
    uint64_t r = seed ^ UINT64_C(0x434F4E5452414354);
    uint64_t d = sound__splitmix64(&r);

    spec_out->seed = seed;
    spec_out->max_loss_cap = UINT64_C(12000);
    spec_out->max_exit_fee = UINT64_C(2000);
    spec_out->protected_mask = UINT64_C(0xF);
    spec_out->max_asym_num = UINT64_C(2);
    spec_out->max_asym_den = UINT64_C(1);
    spec_out->num_clauses = 5U;

    /* Clause 1: fair disclosed base fee within cap */
    spec_out->clauses[0].clause_id = 1U;
    spec_out->clauses[0].kind = (uint8_t)SOUND_CLAUSE_BASE_FEE;
    spec_out->clauses[0].disclosed = UINT8_C(1);
    spec_out->clauses[0].b4_truth = (uint8_t)SOUND_B4_TRUE;
    spec_out->clauses[0].base_loss = UINT64_C(6000) + (d & UINT64_C(2047));
    spec_out->clauses[0].cons_penalty = UINT64_C(100);
    spec_out->clauses[0].inst_penalty = UINT64_C(100);

    /* Clause 2: unilateral power piercing protected consumer core immunity */
    spec_out->clauses[1].clause_id = 2U;
    spec_out->clauses[1].kind = (uint8_t)SOUND_CLAUSE_UNILATERAL_POWER;
    spec_out->clauses[1].disclosed = UINT8_C(1);
    spec_out->clauses[1].b4_truth = (uint8_t)SOUND_B4_TRUE;
    spec_out->clauses[1].target_mask = UINT64_C(0x4);
    spec_out->clauses[1].extra_drift = UINT64_C(25000);

    /* Clause 3: undefined B4_BOT unilateral reasonableness fee clause */
    spec_out->clauses[2].clause_id = 3U;
    spec_out->clauses[2].kind = (uint8_t)SOUND_CLAUSE_AMBIGUOUS_OBLIGATION;
    spec_out->clauses[2].disclosed = UINT8_C(1);
    spec_out->clauses[2].b4_truth = (uint8_t)SOUND_B4_BOT;
    spec_out->clauses[2].extra_drift = UINT64_C(15000);

    /* Clause 4: undisclosed presupposition-smuggled admin surcharge */
    spec_out->clauses[3].clause_id = 4U;
    spec_out->clauses[3].kind = (uint8_t)SOUND_CLAUSE_PRESUP_SMUGGLE;
    spec_out->clauses[3].disclosed = UINT8_C(0);
    spec_out->clauses[3].b4_truth = (uint8_t)SOUND_B4_TRUE;
    spec_out->clauses[3].base_loss = UINT64_C(4000);
    spec_out->clauses[3].extra_drift = UINT64_C(8000);

    /* Clause 5: Chisholm default acceleration + unconscionable exit penalty */
    spec_out->clauses[4].clause_id = 5U;
    spec_out->clauses[4].kind = (uint8_t)SOUND_CLAUSE_CHISHOLM_DEFAULT;
    spec_out->clauses[4].disclosed = UINT8_C(1);
    spec_out->clauses[4].b4_truth = (uint8_t)SOUND_B4_TRUE;
    spec_out->clauses[4].extra_drift = UINT64_C(30000);
    spec_out->clauses[4].exit_fee = UINT64_C(12000);
    spec_out->clauses[4].cons_penalty = UINT64_C(50000);
    spec_out->clauses[4].inst_penalty = UINT64_C(0);

    sound_audit_contract(spec_out, audit_out);
}

void sound_hunt_contract_loophole(uint64_t seed, sound_contract_spec_t *spec_out,
                                  sound_contract_audit_t *audit_out) {
    if (spec_out == NULL || audit_out == NULL) {
        return;
    }
    sound_zero_bytes(spec_out, sizeof(*spec_out));
    uint64_t r = seed ^ UINT64_C(0x4C4F4F50484F4C45);
    uint64_t d = sound__splitmix64(&r);

    spec_out->seed = seed;
    spec_out->max_loss_cap = UINT64_C(10000);
    spec_out->max_exit_fee = UINT64_C(2000);
    spec_out->protected_mask = UINT64_C(0x1);
    spec_out->max_asym_num = UINT64_C(2);
    spec_out->max_asym_den = UINT64_C(1);
    spec_out->num_clauses = 3U;

    /* Individually all 3 clauses look clean and within cap (5000 + 3000 + 1000 <= 10000) */
    spec_out->clauses[0].clause_id = 1U;
    spec_out->clauses[0].kind = (uint8_t)SOUND_CLAUSE_BASE_FEE;
    spec_out->clauses[0].disclosed = UINT8_C(1);
    spec_out->clauses[0].b4_truth = (uint8_t)SOUND_B4_TRUE;
    spec_out->clauses[0].target_mask = UINT64_C(0x1);
    spec_out->clauses[0].base_loss = UINT64_C(5000) + (d & UINT64_C(255));
    spec_out->clauses[0].exit_fee = UINT64_C(500);
    spec_out->clauses[0].cons_penalty = UINT64_C(100);
    spec_out->clauses[0].inst_penalty = UINT64_C(100);

    spec_out->clauses[1].clause_id = 2U;
    spec_out->clauses[1].kind = (uint8_t)SOUND_CLAUSE_RECIPROCITY;
    spec_out->clauses[1].disclosed = UINT8_C(1);
    spec_out->clauses[1].b4_truth = (uint8_t)SOUND_B4_TRUE;
    spec_out->clauses[1].base_loss = UINT64_C(3000);
    spec_out->clauses[1].exit_fee = UINT64_C(500);
    spec_out->clauses[1].cons_penalty = UINT64_C(100);
    spec_out->clauses[1].inst_penalty = UINT64_C(100);

    spec_out->clauses[2].clause_id = 3U;
    spec_out->clauses[2].kind = (uint8_t)SOUND_CLAUSE_RECIPROCITY;
    spec_out->clauses[2].disclosed = UINT8_C(1);
    spec_out->clauses[2].b4_truth = (uint8_t)SOUND_B4_TRUE;
    spec_out->clauses[2].base_loss = UINT64_C(1000);
    spec_out->clauses[2].exit_fee = UINT64_C(500);
    spec_out->clauses[2].cons_penalty = UINT64_C(100);
    spec_out->clauses[2].inst_penalty = UINT64_C(100);

    /* Compositional cross-clause Mobius H^1 = 3 mod 4 loop + immunity bypass + default trigger */
    spec_out->num_edges = 3U;
    spec_out->edges[0].src = 0U;
    spec_out->edges[0].dst = 1U;
    spec_out->edges[0].edge_kind = (uint8_t)SOUND_EDGE_REDIRECT;
    spec_out->edges[0].cocycle_shift = UINT8_C(1);
    spec_out->edges[0].extra_exposure = UINT64_C(0);

    spec_out->edges[1].src = 1U;
    spec_out->edges[1].dst = 2U;
    spec_out->edges[1].edge_kind = (uint8_t)SOUND_EDGE_POWER_DELEGATE;
    spec_out->edges[1].cocycle_shift = UINT8_C(1);
    spec_out->edges[1].extra_exposure = UINT64_C(0);

    spec_out->edges[2].src = 2U;
    spec_out->edges[2].dst = 0U;
    spec_out->edges[2].edge_kind = (uint8_t)SOUND_EDGE_OVERRIDE;
    spec_out->edges[2].cocycle_shift = UINT8_C(1);
    spec_out->edges[2].extra_exposure = UINT64_C(5000);

    sound_audit_contract(spec_out, audit_out);
}
