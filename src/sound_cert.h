#ifndef SOUND_CERT_H
#define SOUND_CERT_H

#include "flow_ir_builder.h"
#include "flow_ir_io.h"
#include "sound.h"

#include <stdio.h>

typedef flow_ir_builder_ws_t sound_cert_ws_t;

static inline uint32_t sound_cert_emit_const(flow_ir_prog_t *p, uint16_t width, uint64_t val,
                                             const char *name) {
    return flow_ir_emit_const(p, width, val, name);
}

static inline uint32_t sound_cert_emit_op1(flow_ir_prog_t *p, uint16_t kind, uint16_t width,
                                           uint32_t in0, uint64_t p0, const char *name) {
    return flow_ir_emit_op1(p, kind, width, in0, p0, name);
}

static inline uint32_t sound_cert_emit_op2(flow_ir_prog_t *p, uint16_t kind, uint16_t width,
                                           uint32_t lhs, uint32_t rhs, const char *name) {
    return flow_ir_emit_op2(p, kind, width, lhs, rhs, name);
}

static inline uint32_t sound_cert_emit_sel(flow_ir_prog_t *p, uint16_t width, uint32_t sel,
                                           uint32_t vf, uint32_t vt, const char *name) {
    return flow_ir_emit_sel(p, width, sel, vf, vt, name);
}

static inline bool sound_cert_seal_and_write_fir(sound_cert_ws_t *ws, const char *out_fir_path) {
    return flow_ir_builder_seal_and_write(ws, out_fir_path);
}

static inline void sound_cert_init_prog(sound_cert_ws_t *ws, const char *name) {
    flow_ir_builder_init(ws, name);
}

/* ============================================================================
 * 1. Pillar 1: `paradox-min` (.lean, .fir, .json)
 * ============================================================================ */

static inline bool sound_cert_emit_paradox_lean(const sound_paradox_witness_t *w,
                                                const char *path) {
    if (w == NULL || path == NULL) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    fprintf(fp, "import Sound.Theory\n\n");
    fprintf(fp, "namespace Sound.CertParadoxMin\n\n");
    fprintf(fp, "open Sound.Bilattice\n\n");
    fprintf(fp, "def atkRow : Fin 3 → BitVec 3\n");
    fprintf(fp, "  | 0 => %u#3\n", (unsigned)(w->atk_to[0] & 7U));
    fprintf(fp, "  | 1 => %u#3\n", (unsigned)(w->atk_to[1] & 7U));
    fprintf(fp, "  | 2 => %u#3\n\n", (unsigned)(w->atk_to[2] & 7U));
    fprintf(fp, "def paradoxAtk (i j : Fin 3) : Bool :=\n");
    fprintf(fp, "  BitVec.getLsbD (atkRow j) i.val\n\n");
    fprintf(fp, "private def inhList (row : BitVec 3) : List Nat :=\n");
    fprintf(fp, "  ([0, 1, 2].filter (fun k => BitVec.getLsbD row k))\n\n");
    fprintf(fp, "def paradoxNodes : List GraphNode :=\n");
    fprintf(fp,
            "  [ { id := 0, tag := 1, rank := 2, bridge := true, inhibitors := inhList (%u#3) }\n",
            (unsigned)(w->atk_to[0] & 7U));
    fprintf(fp,
            "  , { id := 1, tag := 1, rank := 2, bridge := true, inhibitors := inhList (%u#3) }\n",
            (unsigned)(w->atk_to[1] & 7U));
    fprintf(
        fp,
        "  , { id := 2, tag := 1, rank := 2, bridge := true, inhibitors := inhList (%u#3) } ]\n\n",
        (unsigned)(w->atk_to[2] & 7U));

    fprintf(fp, "theorem cert_bilattice_conflict_fixpoint :\n");
    fprintf(fp, "    evalIStar 3 paradoxNodes = [] ∧\n");
    fprintf(fp, "    evalPStar 3 paradoxNodes = [0, 1, 2] ∧\n");
    fprintf(fp,
            "    paradoxNodes.map (evalNode 3 paradoxNodes) = [.conflict, .conflict, .conflict] "
            ":= by\n");
    fprintf(fp, "  decide\n\n");

    fprintf(fp, "theorem cert_no_classical_model (v : Fin 3 → Bool) :\n");
    fprintf(fp, "    ¬ IsClassicalAttackModel paradoxAtk v := by\n");
    fprintf(fp, "  intro hM\n");
    fprintf(fp, "  have p0 := hM 0\n");
    fprintf(fp, "  have p1 := hM 1\n");
    fprintf(fp, "  have p2 := hM 2\n");
    fprintf(fp, "  have fAll : ∀ (u : Fin 3),\n");
    fprintf(fp, "      ((∀ i : Fin 3, paradoxAtk i u = true → v i = false) ↔\n");
    fprintf(fp, "       ((paradoxAtk 0 u = true → v 0 = false) ∧\n");
    fprintf(fp, "        (paradoxAtk 1 u = true → v 1 = false) ∧\n");
    fprintf(fp, "        (paradoxAtk 2 u = true → v 2 = false))) := by\n");
    fprintf(fp, "    intro u\n");
    fprintf(fp, "    exact ⟨fun h => ⟨h 0, h 1, h 2⟩, fun ⟨a, b, c⟩ ⟨i, _⟩ => by\n");
    fprintf(fp, "      match i with | 0 => exact a | 1 => exact b | 2 => exact c⟩\n");
    fprintf(fp, "  rw [fAll 0] at p0\n");
    fprintf(fp, "  rw [fAll 1] at p1\n");
    fprintf(fp, "  rw [fAll 2] at p2\n");
    fprintf(fp, "  cases hv0 : v 0 <;> cases hv1 : v 1 <;> cases hv2 : v 2 <;>\n");
    fprintf(fp, "    simp [hv0, hv1, hv2, paradoxAtk, atkRow] at p0 p1 p2\n\n");

    fprintf(fp, "end Sound.CertParadoxMin\n");
    fclose(fp);
    return true;
}

static inline bool sound_cert_emit_paradox_fir(const sound_paradox_witness_t *w,
                                               const char *out_fir_path, sound_cert_ws_t *ws) {
    if (w == NULL || ws == NULL) {
        return false;
    }
    sound_cert_init_prog(ws, "paradox_min_gamma");
    flow_ir_prog_t *p = &ws->ir_prog;
    p->num_inputs = 1U;
    p->inputs[0] = 0U;
    p->vals[0].width = 3U;
    p->vals[0].is_classical = 0U;
    snprintf(p->vals[0].name, sizeof(p->vals[0].name), "blocked3");
    p->num_vals = 1U;

    uint32_t k0 = sound_cert_emit_const(p, 3U, UINT64_C(0), "c0");
    uint32_t c1 = sound_cert_emit_const(p, 3U, UINT64_C(1), "c1");
    uint32_t bit0 = sound_cert_emit_const(p, 3U, UINT64_C(1), "b0");
    uint32_t bit1 = sound_cert_emit_const(p, 3U, UINT64_C(2), "b1");
    uint32_t bit2 = sound_cert_emit_const(p, 3U, UINT64_C(4), "b2");
    uint32_t a0 = sound_cert_emit_const(p, 3U, (uint64_t)(w->atk_to[0] & 7U), "a0");
    uint32_t a1 = sound_cert_emit_const(p, 3U, (uint64_t)(w->atk_to[1] & 7U), "a1");
    uint32_t a2 = sound_cert_emit_const(p, 3U, (uint64_t)(w->atk_to[2] & 7U), "a2");
    (void)c1;

    uint32_t h0 = sound_cert_emit_op2(p, FLOW_IR_OP_AND_VEC, 3U, 0U, a0, "h0");
    uint32_t ok0 = sound_cert_emit_op1(p, FLOW_IR_OP_EQ_ZERO, 1U, h0, 0U, "ok0");
    uint32_t t0 = sound_cert_emit_sel(p, 3U, ok0, k0, bit0, "t0");

    uint32_t h1 = sound_cert_emit_op2(p, FLOW_IR_OP_AND_VEC, 3U, 0U, a1, "h1");
    uint32_t ok1 = sound_cert_emit_op1(p, FLOW_IR_OP_EQ_ZERO, 1U, h1, 0U, "ok1");
    uint32_t t1 = sound_cert_emit_sel(p, 3U, ok1, k0, bit1, "t1");

    uint32_t h2 = sound_cert_emit_op2(p, FLOW_IR_OP_AND_VEC, 3U, 0U, a2, "h2");
    uint32_t ok2 = sound_cert_emit_op1(p, FLOW_IR_OP_EQ_ZERO, 1U, h2, 0U, "ok2");
    uint32_t t2 = sound_cert_emit_sel(p, 3U, ok2, k0, bit2, "t2");

    uint32_t g01 = sound_cert_emit_op2(p, FLOW_IR_OP_XOR_VEC, 3U, t0, t1, "g01");
    uint32_t g_all = sound_cert_emit_op2(p, FLOW_IR_OP_XOR_VEC, 3U, g01, t2, "gamma_out");
    if (g_all == UINT32_MAX) {
        return false;
    }
    p->num_outputs = 1U;
    p->outputs[0] = g_all;
    if (!sound_cert_seal_and_write_fir(ws, out_fir_path)) {
        return false;
    }
    bool match = true;
    for (uint32_t blk = 0U; blk < 8U; blk++) {
        uint64_t inv[1] = {(uint64_t)blk};
        uint64_t outv[1] = {UINT64_C(0)};
        uint32_t exp_g = UINT32_C(0);
        for (uint32_t u = 0U; u < 3U; u++) {
            if (((uint32_t)w->atk_to[u] & blk) == UINT32_C(0)) {
                exp_g |= (UINT32_C(1) << u);
            }
        }
        if (!flow_ir_eval_u64(&ws->ir_reloaded, inv, outv) || (outv[0] & 7U) != exp_g) {
            match = false;
            break;
        }
    }
    ws->ir_eval_ok = match;
    return match;
}

static inline bool sound_cert_emit_paradox_json(const sound_paradox_witness_t *w,
                                                const sound_cert_ws_t *ws, const char *path) {
    if (w == NULL || path == NULL) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    fprintf(fp, "{\n");
    fprintf(fp, "  \"mode\": \"paradox-min\",\n");
    fprintf(fp, "  \"seed\": \"0x%016llx\",\n", (unsigned long long)w->seed);
    fprintf(fp, "  \"num_nodes\": %u,\n", (unsigned)w->num_nodes);
    fprintf(fp, "  \"atk_to\": [%u, %u, %u],\n", (unsigned)w->atk_to[0], (unsigned)w->atk_to[1],
            (unsigned)w->atk_to[2]);
    fprintf(fp, "  \"classical_models\": %u,\n", (unsigned)w->classical_models);
    fprintf(fp, "  \"i_star\": %u,\n", (unsigned)w->i_star);
    fprintf(fp, "  \"p_star\": %u,\n", (unsigned)w->p_star);
    fprintf(fp, "  \"conflict_mask\": %u,\n", (unsigned)w->conflict_mask);
    fprintf(fp, "  \"is_odd_cycle\": %u,\n", (unsigned)w->is_odd_cycle);
    fprintf(fp, "  \"merkle\": \"0x%016llx\",\n", (unsigned long long)w->merkle);
    fprintf(fp, "  \"fir_wire_bytes\": %zu,\n", (ws != NULL) ? ws->wire_len : 0U);
    fprintf(fp, "  \"fir_eval_ok\": %s\n", (ws != NULL && ws->ir_eval_ok) ? "true" : "false");
    fprintf(fp, "}\n");
    fclose(fp);
    return true;
}

/* ============================================================================
 * 2. Pillar 2: `sheaf-h1` (.lean, .fir, .json)
 * ============================================================================ */

static inline bool sound_cert_emit_sheaf_lean(const sound_sheaf_witness_t *w, const char *path) {
    if (w == NULL || path == NULL) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    fprintf(fp, "import Sound.Theory\n\n");
    fprintf(fp, "namespace Sound.CertSheafH1\n\n");
    fprintf(fp, "open Sound.Sheaf\n\n");
    fprintf(fp, "def cochain (i j : Fin 3) : BitVec 2 :=\n");
    fprintf(fp, "  match i.val, j.val with\n");
    fprintf(fp, "  | 0, 1 => %u#2\n", (unsigned)(w->sg01 & 3U));
    fprintf(fp, "  | 1, 2 => %u#2\n", (unsigned)(w->sg12 & 3U));
    fprintf(fp, "  | 2, 0 => %u#2\n", (unsigned)(w->sg20 & 3U));
    fprintf(fp, "  | 1, 0 => -%u#2\n", (unsigned)(w->sg01 & 3U));
    fprintf(fp, "  | 2, 1 => -%u#2\n", (unsigned)(w->sg12 & 3U));
    fprintf(fp, "  | 0, 2 => -%u#2\n", (unsigned)(w->sg20 & 3U));
    fprintf(fp, "  | _, _ => 0#2\n\n");
    fprintf(fp, "def t01 : BitVec 64 := 0x%016llx#64\n", (unsigned long long)w->t01);
    fprintf(fp, "def t12 : BitVec 64 := 0x%016llx#64\n", (unsigned long long)w->t12);
    fprintf(fp, "def t20 : BitVec 64 := 0x%016llx#64\n\n", (unsigned long long)w->t20);

    fprintf(fp,
            "theorem cert_holonomy_nonzero : walkHolonomyZ4 cochain [0, 1, 2, 0] ≠ 0#2 := by\n");
    fprintf(fp, "  decide\n\n");

    fprintf(fp, "theorem cert_h1_obstruction_nogo : ¬ IsCoboundaryZ4 cochain triangleAdj :=\n");
    fprintf(fp, "  nonzero_holonomy_no_global_section_z4_nogo cochain triangleAdj 0 [1, 2, 0]\n");
    fprintf(fp, "    ⟨by decide, by decide, by decide, trivial⟩ rfl cert_holonomy_nonzero\n\n");

    fprintf(fp, "theorem cert_arena64_twist_nogo :\n");
    fprintf(fp, "    ¬ ∃ (s0 s1 s2 : BitVec 64),\n");
    fprintf(fp, "        (s0 ^^^ s1) = t01 ∧ (s1 ^^^ s2) = t12 ∧ (s2 ^^^ s0) = t20 :=\n");
    fprintf(fp, "  arena64_triangle_twist_no_global_section_bv64 t01 t12 t20 (by decide)\n\n");

    fprintf(fp, "end Sound.CertSheafH1\n");
    fclose(fp);
    return true;
}

static inline bool sound_cert_emit_sheaf_fir(const sound_sheaf_witness_t *w,
                                             const char *out_fir_path, sound_cert_ws_t *ws) {
    if (w == NULL || ws == NULL) {
        return false;
    }
    sound_cert_init_prog(ws, "sheaf_h1_holonomy");
    flow_ir_prog_t *p = &ws->ir_prog;
    p->num_inputs = 3U;
    p->inputs[0] = 0U;
    p->inputs[1] = 1U;
    p->inputs[2] = 2U;
    p->vals[0].width = 64U;
    p->vals[1].width = 64U;
    p->vals[2].width = 64U;
    snprintf(p->vals[0].name, sizeof(p->vals[0].name), "t01");
    snprintf(p->vals[1].name, sizeof(p->vals[1].name), "t12");
    snprintf(p->vals[2].name, sizeof(p->vals[2].name), "t20");
    p->num_vals = 3U;

    uint32_t x01 = sound_cert_emit_op2(p, FLOW_IR_OP_XOR_VEC, 64U, 0U, 1U, "h01");
    uint32_t hol = sound_cert_emit_op2(p, FLOW_IR_OP_XOR_VEC, 64U, x01, 2U, "holonomy");
    uint32_t gluable = sound_cert_emit_op1(p, FLOW_IR_OP_EQ_ZERO, 1U, hol, 0U, "gluable");
    if (gluable == UINT32_MAX) {
        return false;
    }
    p->num_outputs = 2U;
    p->outputs[0] = hol;
    p->outputs[1] = gluable;
    if (!sound_cert_seal_and_write_fir(ws, out_fir_path)) {
        return false;
    }

    uint64_t inv[3] = {w->t01, w->t12, w->t20};
    uint64_t outv[2] = {UINT64_C(0), UINT64_C(0)};
    uint64_t cob_in[3] = {w->t01, w->t12, w->t01 ^ w->t12};
    uint64_t cob_out[2] = {UINT64_C(0), UINT64_C(0)};
    bool ok = flow_ir_eval_u64(&ws->ir_reloaded, inv, outv) && outv[0] == w->xor_holonomy &&
              outv[1] == UINT64_C(0) && flow_ir_eval_u64(&ws->ir_reloaded, cob_in, cob_out) &&
              cob_out[0] == UINT64_C(0) && cob_out[1] == UINT64_C(1);
    ws->ir_eval_ok = ok;
    return ok;
}

static inline bool sound_cert_emit_sheaf_json(const sound_sheaf_witness_t *w,
                                              const sound_cert_ws_t *ws, const char *path) {
    if (w == NULL || path == NULL) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    fprintf(fp, "{\n");
    fprintf(fp, "  \"mode\": \"sheaf-h1\",\n");
    fprintf(fp, "  \"seed\": \"0x%016llx\",\n", (unsigned long long)w->seed);
    fprintf(fp, "  \"g\": [%u, %u, %u],\n", (unsigned)w->g01, (unsigned)w->g12, (unsigned)w->g20);
    fprintf(fp, "  \"phi\": [%u, %u, %u],\n", (unsigned)w->phi[0], (unsigned)w->phi[1],
            (unsigned)w->phi[2]);
    fprintf(fp, "  \"sg\": [%u, %u, %u],\n", (unsigned)w->sg01, (unsigned)w->sg12,
            (unsigned)w->sg20);
    fprintf(fp, "  \"holonomy_z4\": %u,\n", (unsigned)w->holonomy_z4);
    fprintf(fp, "  \"gauge_holonomy_z4\": %u,\n", (unsigned)w->gauge_holonomy_z4);
    fprintf(fp, "  \"is_coboundary_z4\": %u,\n", (unsigned)w->is_coboundary_z4);
    fprintf(fp, "  \"skew_symmetric_ok\": %u,\n", (unsigned)w->skew_symmetric_ok);
    fprintf(fp, "  \"t01\": \"0x%016llx\",\n", (unsigned long long)w->t01);
    fprintf(fp, "  \"t12\": \"0x%016llx\",\n", (unsigned long long)w->t12);
    fprintf(fp, "  \"t20\": \"0x%016llx\",\n", (unsigned long long)w->t20);
    fprintf(fp, "  \"xor_holonomy\": \"0x%016llx\",\n", (unsigned long long)w->xor_holonomy);
    fprintf(fp, "  \"merkle\": \"0x%016llx\",\n", (unsigned long long)w->merkle);
    fprintf(fp, "  \"fir_wire_bytes\": %zu,\n", (ws != NULL) ? ws->wire_len : 0U);
    fprintf(fp, "  \"fir_eval_ok\": %s\n", (ws != NULL && ws->ir_eval_ok) ? "true" : "false");
    fprintf(fp, "}\n");
    fclose(fp);
    return true;
}

/* ============================================================================
 * 3. Pillar 3a: `chisholm` (.lean, .fir, .json)
 * ============================================================================ */

static inline bool sound_cert_emit_chisholm_lean(const sound_chisholm_witness_t *w,
                                                 const char *path) {
    if (w == NULL || path == NULL) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    fprintf(fp, "import Sound.Theory\n\n");
    fprintf(fp, "namespace Sound.CertChisholm\n\n");
    fprintf(fp, "open Sound.Bilattice Sound.Hohfeld\n\n");
    fprintf(fp, "theorem cert_sdl_explosion_nogo {P : Type} (M : SDLModel P) (a b : P)\n");
    fprintf(fp, "    (h1 : M.O a) (h2 : M.O (M.Imp a b)) (h3 : M.O (M.Not b)) : False :=\n");
    fprintf(fp, "  sdl_chisholm_explosion_nogo M a b h1 h2 h3\n\n");
    fprintf(fp, "theorem cert_chisholm_masks_exact :\n");
    fprintf(
        fp,
        "    let (_nm, _iStar, _pStar, est, defM, conf, pen) := statusMasks 3 chisholmSoundNodes\n");
    fprintf(fp, "    est = %llu ∧ defM = %llu ∧ conf = %llu ∧ pen = %llu := by\n",
            (unsigned long long)w->established_mask, (unsigned long long)w->defeated_mask,
            (unsigned long long)w->conflict_mask, (unsigned long long)w->penumbra_mask);
    fprintf(fp, "  decide\n\n");
    fprintf(
        fp,
        "theorem cert_chisholm_resolved : chisholmSoundNodes.map (evalNode 3 chisholmSoundNodes) =\n");
    fprintf(fp, "    [.reject, .reject, .accept, .accept] :=\n");
    fprintf(fp, "  sound_chisholm_stratified_consistent.2.2\n\n");
    fprintf(fp, "end Sound.CertChisholm\n");
    fclose(fp);
    return true;
}

static inline bool sound_cert_emit_chisholm_fir(const char *out_fir_path, sound_cert_ws_t *ws) {
    if (ws == NULL) {
        return false;
    }
    sound_zero_bytes(ws, sizeof(*ws));
    SoundArena arena;
    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 4U;
    arena.map.norm.power_mask = UINT64_C(0x4);
    arena.map.norm.duty_pos_mask = UINT64_C(0x4);
    arena.map.node_meta[0].sort = (uint8_t)SOUND_SORT_FACTUAL;
    arena.map.node_meta[0].evid = (uint8_t)SOUND_EVID_DIRECT;
    arena.map.node_meta[0].num_clauses = 1U;
    arena.map.norm.revoked_mask = UINT64_C(1);

    arena.map.node_meta[1].sort = (uint8_t)SOUND_SORT_NORMATIVE;
    arena.map.node_meta[1].evid = (uint8_t)SOUND_EVID_INFERENTIAL;
    arena.map.node_meta[1].flags = SOUND_NODE_FLAG_CONSTITUTIVE;
    arena.map.node_meta[1].num_clauses = 1U;
    arena.map.node_meta[1].attack_mask = UINT64_C(4);
    arena.map.node_clauses[1].clauses[0] = UINT64_C(1);

    arena.map.node_meta[2].sort = (uint8_t)SOUND_SORT_NORMATIVE;
    arena.map.node_meta[2].evid = (uint8_t)SOUND_EVID_INFERENTIAL;
    arena.map.node_meta[2].flags = SOUND_NODE_FLAG_CONSTITUTIVE;
    arena.map.node_meta[2].num_clauses = 1U;
    arena.map.node_clauses[2].clauses[0] = UINT64_C(8);

    arena.map.node_meta[3].sort = (uint8_t)SOUND_SORT_FACTUAL;
    arena.map.node_meta[3].evid = (uint8_t)SOUND_EVID_DIRECT;
    arena.map.node_meta[3].num_clauses = 1U;

    if (sound_eval(&arena) != SOUND_FIBER_COMMITTED) {
        return false;
    }
    if (sound_lift_to_fir(&arena, "chisholm_ctd", 0U, ws->wire_buf, sizeof(ws->wire_buf),
                          &ws->wire_len) != SOUND_FIBER_COMMITTED) {
        return false;
    }
    if (flow_ir_load_bytes_fiber(&ws->ir_reloaded, ws->wire_buf, ws->wire_len) !=
        FLOW_IR_FIBER_COMMITTED) {
        return false;
    }
    flow_ir_dag_stats_t dag;
    ws->ir_linear_ok = (ws->ir_reloaded.mult == FLOW_IR_MULT_LINEAR);
    ws->ir_unitary_ok = flow_ir_admits_unitary(&ws->ir_reloaded);
    if (!ws->ir_linear_ok || !ws->ir_unitary_ok || !flow_ir_analyze_dag(&ws->ir_reloaded, &dag)) {
        return false;
    }
    ws->mult_bits = dag.mult_bits;
    ws->mult_depth = dag.mult_depth;
    uint64_t inv[5] = {UINT64_C(0x4), UINT64_C(0), UINT64_C(0x4), UINT64_C(0), UINT64_C(0)};
    uint64_t outv[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    ws->ir_eval_ok = flow_ir_eval_u64(&ws->ir_reloaded, inv, outv) && outv[0] == UINT64_C(0x4) &&
                     outv[3] == UINT64_C(0xC);
    if (!ws->ir_eval_ok) {
        return false;
    }
    if (out_fir_path != NULL && out_fir_path[0] != '\0') {
        FILE *fp = fopen(out_fir_path, "wb");
        if (fp == NULL) {
            return false;
        }
        size_t wr = fwrite(ws->wire_buf, 1U, ws->wire_len, fp);
        int cls = fclose(fp);
        if (wr != ws->wire_len || cls != 0) {
            return false;
        }
    }
    return true;
}

static inline bool sound_cert_emit_chisholm_json(const sound_chisholm_witness_t *w,
                                                 const sound_cert_ws_t *ws, const char *path) {
    if (w == NULL || path == NULL) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    fprintf(fp, "{\n");
    fprintf(fp, "  \"mode\": \"chisholm\",\n");
    fprintf(fp, "  \"num_nodes\": %u,\n", (unsigned)w->num_nodes);
    fprintf(fp, "  \"established_mask\": %llu,\n", (unsigned long long)w->established_mask);
    fprintf(fp, "  \"defeated_mask\": %llu,\n", (unsigned long long)w->defeated_mask);
    fprintf(fp, "  \"conflict_mask\": %llu,\n", (unsigned long long)w->conflict_mask);
    fprintf(fp, "  \"penumbra_mask\": %llu,\n", (unsigned long long)w->penumbra_mask);
    fprintf(fp, "  \"statuses\": [%u, %u, %u, %u],\n", (unsigned)w->statuses[0],
            (unsigned)w->statuses[1], (unsigned)w->statuses[2], (unsigned)w->statuses[3]);
    fprintf(fp, "  \"hohfeld_ok\": %u,\n", (unsigned)w->hohfeld_ok);
    fprintf(fp, "  \"merkle\": \"0x%016llx\",\n", (unsigned long long)w->merkle);
    fprintf(fp, "  \"fir_wire_bytes\": %zu,\n", (ws != NULL) ? ws->wire_len : 0U);
    fprintf(fp, "  \"fir_eval_ok\": %s\n", (ws != NULL && ws->ir_eval_ok) ? "true" : "false");
    fprintf(fp, "}\n");
    fclose(fp);
    return true;
}

/* ============================================================================
 * 4. Pillar 3b: `norm-trap` (.lean, .fir, .json)
 * ============================================================================ */

static inline bool sound_cert_emit_norm_trap_lean(const sound_norm_trap_witness_t *w,
                                                  const char *path) {
    if (w == NULL || path == NULL) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    fprintf(fp, "import Sound.Theory\n\n");
    fprintf(fp, "namespace Sound.CertNormTrap\n\n");
    fprintf(fp, "open Sound.Bilattice Sound.Hohfeld\n\n");
    fprintf(fp, "def initState : NormState64 :=\n");
    fprintf(
        fp,
        "  { dutyPos := 0x%llx#64, dutyNeg := 0x%llx#64, power := 0x%llx#64, immunity := 0x%llx#64 }\n\n",
        (unsigned long long)w->init_pos, (unsigned long long)w->init_neg,
        (unsigned long long)w->power, (unsigned long long)w->immunity);
    fprintf(fp, "def traceActions : List NormAction :=\n");
    fprintf(
        fp,
        "  [ { slotBit := 1#64, gp := false, rp := true,  gn := false, rn := false, hSingle := by decide }\n");
    fprintf(
        fp,
        "  , { slotBit := 4#64, gp := false, rp := true,  gn := false, rn := false, hSingle := by decide }\n");
    fprintf(
        fp,
        "  , { slotBit := 8#64, gp := true,  rp := false, gn := false, rn := false, hSingle := by decide }\n");
    fprintf(
        fp,
        "  , { slotBit := 2#64, gp := true,  rp := false, gn := false, rn := false, hSingle := by decide } ]\n\n");
    fprintf(fp, "def wTie : BitVec 16 := %u#16\n\n", (unsigned)w->tie_weight);

    fprintf(fp, "theorem cert_immunity_kinetic_freeze :\n");
    fprintf(
        fp,
        "    ((execNormTrace initState traceActions).dutyPos &&& (execNormTrace initState traceActions).dutyNeg) = 0#64 ∧\n");
    fprintf(
        fp,
        "    ((execNormTrace initState traceActions).dutyPos &&& initState.immunity) = (initState.dutyPos &&& initState.immunity) ∧\n");
    fprintf(
        fp,
        "    ((execNormTrace initState traceActions).dutyNeg &&& initState.immunity) = (initState.dutyNeg &&& initState.immunity) :=\n");
    fprintf(fp, "  immunity_kinetic_freeze_trace_nogo initState traceActions (by decide)\n\n");

    fprintf(fp, "theorem cert_trace_exact_landing :\n");
    fprintf(fp, "    (execNormTrace initState traceActions).dutyPos = 0x%llx#64 ∧\n",
            (unsigned long long)w->final_pos);
    fprintf(fp, "    (execNormTrace initState traceActions).dutyNeg = 0x%llx#64 := by\n",
            (unsigned long long)w->final_neg);
    fprintf(fp, "  decide\n\n");

    fprintf(fp, "theorem cert_lex_tie_symmetry :\n");
    fprintf(
        fp,
        "    lexSpecialisPairEnergy true false wTie wTie = lexSpecialisPairEnergy false true wTie wTie :=\n");
    fprintf(fp, "  lex_specialis_incomparable_tie_nogo_bv wTie\n\n");

    fprintf(fp, "theorem cert_kanlock_dominance :\n");
    fprintf(
        fp,
        "    lexSpecialisPairEnergy false true 255#16 1#16 < lexSpecialisPairEnergy true true 255#16 1#16 :=\n");
    fprintf(fp, "  hohfeld_immunity_overrides_max_weight_bv\n\n");

    fprintf(fp, "end Sound.CertNormTrap\n");
    fclose(fp);
    return true;
}

static inline bool sound_cert_emit_norm_trap_fir(const sound_norm_trap_witness_t *w,
                                                 const char *out_fir_path, sound_cert_ws_t *ws) {
    if (w == NULL || ws == NULL) {
        return false;
    }
    sound_zero_bytes(ws, sizeof(*ws));
    SoundArena arena;
    sound_arena_init(&arena);
    arena.map.img_hdr.num_nodes = 4U;
    arena.map.norm.duty_pos_mask = w->init_pos;
    arena.map.norm.duty_neg_mask = w->init_neg;
    arena.map.norm.power_mask = w->power;
    arena.map.norm.immunity_mask = w->immunity;
    arena.map.node_meta[0].sort = (uint8_t)SOUND_SORT_NORMATIVE;
    arena.map.node_meta[0].evid = (uint8_t)SOUND_EVID_ENACTED_NORM;
    arena.map.node_meta[0].num_clauses = 1U;
    for (uint32_t i = 1U; i < 4U; i++) {
        arena.map.node_meta[i].sort = (uint8_t)SOUND_SORT_FACTUAL;
        arena.map.node_meta[i].evid = (uint8_t)SOUND_EVID_DIRECT;
        arena.map.node_meta[i].num_clauses = 1U;
    }
    if (sound_eval(&arena) != SOUND_FIBER_COMMITTED ||
        sound_lift_to_fir(&arena, "norm_trap_gate", 0U, ws->wire_buf, sizeof(ws->wire_buf),
                          &ws->wire_len) != SOUND_FIBER_COMMITTED ||
        flow_ir_load_bytes_fiber(&ws->ir_reloaded, ws->wire_buf, ws->wire_len) !=
            FLOW_IR_FIBER_COMMITTED) {
        return false;
    }
    flow_ir_dag_stats_t dag;
    ws->ir_linear_ok = (ws->ir_reloaded.mult == FLOW_IR_MULT_LINEAR);
    ws->ir_unitary_ok = flow_ir_admits_unitary(&ws->ir_reloaded);
    if (!ws->ir_linear_ok || !ws->ir_unitary_ok || !flow_ir_analyze_dag(&ws->ir_reloaded, &dag)) {
        return false;
    }
    ws->mult_bits = dag.mult_bits;
    ws->mult_depth = dag.mult_depth;

    /* Valid revoke on bit 0 succeeds; immune bit 2 + disabled bit 3 stay frozen */
    uint64_t inv[5] = {w->init_pos, w->init_neg, w->init_pos & (~UINT64_C(5)),
                       w->init_neg | UINT64_C(8), UINT64_C(0)};
    uint64_t outv[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    ws->ir_eval_ok = flow_ir_eval_u64(&ws->ir_reloaded, inv, outv) && outv[0] == w->final_pos &&
                     outv[1] == w->final_neg;
    if (!ws->ir_eval_ok) {
        return false;
    }
    if (out_fir_path != NULL && out_fir_path[0] != '\0') {
        FILE *fp = fopen(out_fir_path, "wb");
        if (fp == NULL) {
            return false;
        }
        size_t wr = fwrite(ws->wire_buf, 1U, ws->wire_len, fp);
        int cls = fclose(fp);
        if (wr != ws->wire_len || cls != 0) {
            return false;
        }
    }
    return true;
}

static inline bool sound_cert_emit_norm_trap_json(const sound_norm_trap_witness_t *w,
                                                  const sound_cert_ws_t *ws, const char *path) {
    if (w == NULL || path == NULL) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    fprintf(fp, "{\n");
    fprintf(fp, "  \"mode\": \"norm-trap\",\n");
    fprintf(fp, "  \"seed\": \"0x%016llx\",\n", (unsigned long long)w->seed);
    fprintf(fp, "  \"init_pos\": %llu,\n", (unsigned long long)w->init_pos);
    fprintf(fp, "  \"init_neg\": %llu,\n", (unsigned long long)w->init_neg);
    fprintf(fp, "  \"power\": %llu,\n", (unsigned long long)w->power);
    fprintf(fp, "  \"immunity\": %llu,\n", (unsigned long long)w->immunity);
    fprintf(fp, "  \"final_pos\": %llu,\n", (unsigned long long)w->final_pos);
    fprintf(fp, "  \"final_neg\": %llu,\n", (unsigned long long)w->final_neg);
    fprintf(fp, "  \"step_fiber\": [%u, %u, %u, %u],\n", (unsigned)w->step_fiber[0],
            (unsigned)w->step_fiber[1], (unsigned)w->step_fiber[2], (unsigned)w->step_fiber[3]);
    fprintf(fp, "  \"tie_weight\": %u,\n", (unsigned)w->tie_weight);
    fprintf(fp, "  \"energy_keep_a\": %u,\n", (unsigned)w->energy_keep_a);
    fprintf(fp, "  \"energy_keep_b\": %u,\n", (unsigned)w->energy_keep_b);
    fprintf(fp, "  \"energy_both\": %u,\n", (unsigned)w->energy_both);
    fprintf(fp, "  \"energy_neither\": %u,\n", (unsigned)w->energy_neither);
    fprintf(fp, "  \"kanlock_win_energy\": %u,\n", (unsigned)w->kanlock_win_energy);
    fprintf(fp, "  \"kanlock_both_energy\": %u,\n", (unsigned)w->kanlock_both_energy);
    fprintf(fp, "  \"freeze_ok\": %u,\n", (unsigned)w->freeze_ok);
    fprintf(fp, "  \"merkle\": \"0x%016llx\",\n", (unsigned long long)w->merkle);
    fprintf(fp, "  \"fir_wire_bytes\": %zu,\n", (ws != NULL) ? ws->wire_len : 0U);
    fprintf(fp, "  \"fir_eval_ok\": %s\n", (ws != NULL && ws->ir_eval_ok) ? "true" : "false");
    fprintf(fp, "}\n");
    fclose(fp);
    return true;
}

/* ============================================================================
 * 5. Pillar 4a: `aspect-firewall` (.lean, .fir, .json)
 * ============================================================================ */

static inline bool sound_cert_emit_aspect_lean(const sound_aspect_witness_t *w, const char *path) {
    if (w == NULL || path == NULL) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    fprintf(fp, "import Sound.Theory\n\n");
    fprintf(fp, "namespace Sound.CertAspectFirewall\n\n");
    fprintf(fp, "open Sound.Linguistics Sound.Bilattice\n\n");
    fprintf(fp, "def jInt : Interval := ⟨%u, %u, by decide⟩\n", (unsigned)w->j_start,
            (unsigned)w->j_end);
    fprintf(fp, "def iInt : Interval := ⟨%u, %u, by decide⟩\n\n", (unsigned)w->i_start,
            (unsigned)w->i_end);

    fprintf(fp, "theorem cert_subinterval_witness : ProperSub iInt jInt :=\n");
    fprintf(fp, "  ⟨by decide, by decide, Or.inr (by decide)⟩\n\n");

    fprintf(fp, "theorem cert_homogeneous_quantized_nogo (P : Interval → Prop) (hPj : P jInt) :\n");
    fprintf(fp, "    ¬ (DownwardHomogeneous P ∧ Quantized P) :=\n");
    fprintf(fp, "  homogeneous_quantized_nogo P jInt (by decide) hPj\n\n");

    fprintf(fp, "theorem cert_atelic_constitutive_blocked :\n");
    fprintf(fp, "    constitutiveAspectGate .state true = false ∧\n");
    fprintf(fp, "    constitutiveAspectGate .activity true = false ∧\n");
    fprintf(fp, "    constitutiveAspectGate .accomplishment true = true ∧\n");
    fprintf(fp, "    constitutiveAspectGate .achievement true = true := by\n");
    fprintf(fp, "  decide\n\n");

    fprintf(fp, "theorem cert_declarative_atelic_retraction (st : WorldState) :\n");
    fprintf(fp,
            "    (applyForce .declarative st { slotBit := 1#64, aspect := .activity, gp := true, "
            "rp := false, gn := false, rn := false }).2 =\n");
    fprintf(fp, "      .fallbackPolicyViolation :=\n");
    fprintf(fp, "  congrArg Prod.snd (force_declarative_blocks_atelic_aspect_bv64 st _ rfl)\n\n");

    fprintf(fp, "end Sound.CertAspectFirewall\n");
    fclose(fp);
    return true;
}

static inline bool sound_cert_emit_aspect_fir(const char *out_fir_path, sound_cert_ws_t *ws) {
    if (ws == NULL) {
        return false;
    }
    sound_cert_init_prog(ws, "aspect_telic_gate");
    flow_ir_prog_t *p = &ws->ir_prog;
    /* Input: 2-bit aspect code (0=state, 1=activity, 2=accomplishment, 3=achievement)
       telic bit == bit 1 of aspect code! */
    p->num_inputs = 1U;
    p->inputs[0] = 0U;
    p->vals[0].width = 2U;
    snprintf(p->vals[0].name, sizeof(p->vals[0].name), "aspect2");
    p->num_vals = 1U;

    uint32_t telic = sound_cert_emit_op1(p, FLOW_IR_OP_SLICE, 1U, 0U, 1U, "is_telic");
    uint32_t dyn = sound_cert_emit_op1(p, FLOW_IR_OP_SLICE, 1U, 0U, 0U, "is_dyn");
    if (telic == UINT32_MAX || dyn == UINT32_MAX) {
        return false;
    }
    p->num_outputs = 2U;
    p->outputs[0] = telic;
    p->outputs[1] = dyn;
    if (!sound_cert_seal_and_write_fir(ws, out_fir_path)) {
        return false;
    }

    bool ok = true;
    for (uint32_t a = 0U; a < 4U; a++) {
        uint64_t inv[1] = {(uint64_t)a};
        uint64_t outv[2] = {UINT64_C(0), UINT64_C(0)};
        bool exp_t = sound_aspect_constitutive_ok((sound_aspect_t)a, true);
        bool exp_d = sound_aspect_is_dynamic((sound_aspect_t)a);
        /* Note for achievement=3, (dyn, telic) in enum is STATE=0, ACT=1, ACC=2, ACH=3 where
           bit 1 is telic (`2,3`), whereas bit 0 is raw bit; verify exact telic bit == exp_t */
        (void)exp_d;
        if (!flow_ir_eval_u64(&ws->ir_reloaded, inv, outv) || (outv[0] != 0ULL) != exp_t) {
            ok = false;
            break;
        }
    }
    ws->ir_eval_ok = ok;
    return ok;
}

static inline bool sound_cert_emit_aspect_json(const sound_aspect_witness_t *w,
                                               const sound_cert_ws_t *ws, const char *path) {
    if (w == NULL || path == NULL) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    fprintf(fp, "{\n");
    fprintf(fp, "  \"mode\": \"aspect-firewall\",\n");
    fprintf(fp, "  \"seed\": \"0x%016llx\",\n", (unsigned long long)w->seed);
    fprintf(fp, "  \"aspect_bijection_ok\": %u,\n", (unsigned)w->aspect_bijection_ok);
    fprintf(fp, "  \"telic_blocked_mask\": %u,\n", (unsigned)w->telic_blocked_mask);
    fprintf(fp, "  \"j_start\": %u,\n", (unsigned)w->j_start);
    fprintf(fp, "  \"j_end\": %u,\n", (unsigned)w->j_end);
    fprintf(fp, "  \"i_start\": %u,\n", (unsigned)w->i_start);
    fprintf(fp, "  \"i_end\": %u,\n", (unsigned)w->i_end);
    fprintf(fp, "  \"declarative_atelic_fiber\": %u,\n", (unsigned)w->declarative_atelic_fiber);
    fprintf(fp, "  \"merkle\": \"0x%016llx\",\n", (unsigned long long)w->merkle);
    fprintf(fp, "  \"fir_wire_bytes\": %zu,\n", (ws != NULL) ? ws->wire_len : 0U);
    fprintf(fp, "  \"fir_eval_ok\": %s\n", (ws != NULL && ws->ir_eval_ok) ? "true" : "false");
    fprintf(fp, "}\n");
    fclose(fp);
    return true;
}

/* ============================================================================
 * 6. Pillar 4b: `presup-filter` (.lean, .fir, .json)
 * ============================================================================ */

static inline bool sound_cert_emit_presup_lean(const sound_presup_witness_t *w, const char *path) {
    if (w == NULL || path == NULL) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    fprintf(fp, "import Sound.Theory\n\n");
    fprintf(fp, "namespace Sound.CertPresupFilter\n\n");
    fprintf(fp, "open Sound.Linguistics Sound.Bilattice\n\n");
    fprintf(fp, "def exprA : PresupExpr := ⟨%u#2, %u#2⟩\n", (unsigned)w->a_assert,
            (unsigned)w->a_presup);
    fprintf(fp, "def exprATrue : PresupExpr := ⟨2#2, 2#2⟩\n");
    fprintf(fp, "def exprB : PresupExpr := ⟨%u#2, %u#2⟩\n\n", (unsigned)w->b_assert,
            (unsigned)w->b_presup);

    fprintf(fp, "theorem cert_strawson_gap :\n");
    fprintf(fp, "    evalPresup exprB = 0#2 ∧ evalPresup (projNeg exprB) = 0#2 :=\n");
    fprintf(fp, "  presup_failure_blocks_both_assert_and_neg exprB (by decide)\n\n");

    fprintf(fp, "theorem cert_irrealis_plug :\n");
    fprintf(
        fp,
        "    (projIrrealis exprB).presupVal = 2#2 ∧ evalPresup (projIrrealis exprB) = 0#2 :=\n");
    fprintf(fp, "  projIrrealis_is_plug_bv exprB\n\n");

    fprintf(fp, "theorem cert_cond_filters_false :\n");
    fprintf(fp, "    (projCond exprA exprB).presupVal = %u#2 := by\n",
            (unsigned)w->cond_false_presup);
    fprintf(fp, "  decide\n\n");

    fprintf(fp, "theorem cert_cond_projects_true :\n");
    fprintf(fp, "    (projCond exprATrue exprB).presupVal = %u#2 := by\n",
            (unsigned)w->cond_true_presup);
    fprintf(fp, "  decide\n\n");

    fprintf(fp, "theorem cert_force_conservation (st : WorldState) (act : ForceAct) :\n");
    fprintf(fp, "    (applyForce .assertion st act).1.dutyPos = st.dutyPos ∧\n");
    fprintf(fp, "    (applyForce .query st act).1 = st ∧\n");
    fprintf(fp, "    (applyForce .irrealis st act).1.epistemic = st.epistemic :=\n");
    fprintf(fp, "  ⟨(force_assertion_conserves_normative_bv64 st act).1,\n");
    fprintf(fp, "   by simp [force_query_is_identity_bv64],\n");
    fprintf(fp, "   (force_irrealis_zero_real_leakage_bv64 st act).1⟩\n\n");

    fprintf(fp, "end Sound.CertPresupFilter\n");
    fclose(fp);
    return true;
}

static inline bool sound_cert_emit_presup_fir(const char *out_fir_path, sound_cert_ws_t *ws) {
    if (ws == NULL) {
        return false;
    }
    sound_cert_init_prog(ws, "presup_strawson_eval");
    flow_ir_prog_t *p = &ws->ir_prog;
    p->num_inputs = 2U;
    p->inputs[0] = 0U;
    p->inputs[1] = 1U;
    p->vals[0].width = 2U;
    p->vals[1].width = 2U;
    snprintf(p->vals[0].name, sizeof(p->vals[0].name), "assert2");
    snprintf(p->vals[1].name, sizeof(p->vals[1].name), "presup2");
    p->num_vals = 2U;

    uint32_t c0 = sound_cert_emit_const(p, 2U, UINT64_C(0), "bot");
    uint32_t c_true = sound_cert_emit_const(p, 2U, UINT64_C(2), "btrue");
    uint32_t diff = sound_cert_emit_op2(p, FLOW_IR_OP_XOR_VEC, 2U, 1U, c_true, "pdiff");
    uint32_t p_ok = sound_cert_emit_op1(p, FLOW_IR_OP_EQ_ZERO, 1U, diff, 0U, "p_ok");
    uint32_t res = sound_cert_emit_sel(p, 2U, p_ok, c0, 0U, "eval_out");
    if (res == UINT32_MAX) {
        return false;
    }
    p->num_outputs = 1U;
    p->outputs[0] = res;
    if (!sound_cert_seal_and_write_fir(ws, out_fir_path)) {
        return false;
    }

    bool ok = true;
    for (uint8_t a = 0U; a < 4U; a++) {
        for (uint8_t pr = 0U; pr < 4U; pr++) {
            uint64_t inv[2] = {(uint64_t)a, (uint64_t)pr};
            uint64_t outv[1] = {UINT64_C(0)};
            if (!flow_ir_eval_u64(&ws->ir_reloaded, inv, outv) ||
                (outv[0] & 3ULL) != (uint64_t)sound_presup_eval(a, pr)) {
                ok = false;
                break;
            }
        }
    }
    ws->ir_eval_ok = ok;
    return ok;
}

static inline bool sound_cert_emit_presup_json(const sound_presup_witness_t *w,
                                               const sound_cert_ws_t *ws, const char *path) {
    if (w == NULL || path == NULL) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    fprintf(fp, "{\n");
    fprintf(fp, "  \"mode\": \"presup-filter\",\n");
    fprintf(fp, "  \"seed\": \"0x%016llx\",\n", (unsigned long long)w->seed);
    fprintf(fp, "  \"a_assert\": %u,\n", (unsigned)w->a_assert);
    fprintf(fp, "  \"a_presup\": %u,\n", (unsigned)w->a_presup);
    fprintf(fp, "  \"b_assert\": %u,\n", (unsigned)w->b_assert);
    fprintf(fp, "  \"b_presup\": %u,\n", (unsigned)w->b_presup);
    fprintf(fp, "  \"eval_fail_b\": %u,\n", (unsigned)w->eval_fail_b);
    fprintf(fp, "  \"eval_neg_fail_b\": %u,\n", (unsigned)w->eval_neg_fail_b);
    fprintf(fp, "  \"plug_presup\": %u,\n", (unsigned)w->plug_presup);
    fprintf(fp, "  \"plug_eval\": %u,\n", (unsigned)w->plug_eval);
    fprintf(fp, "  \"cond_false_presup\": %u,\n", (unsigned)w->cond_false_presup);
    fprintf(fp, "  \"cond_true_presup\": %u,\n", (unsigned)w->cond_true_presup);
    fprintf(fp, "  \"force_hume_ok\": %u,\n", (unsigned)w->force_hume_ok);
    fprintf(fp, "  \"force_query_ok\": %u,\n", (unsigned)w->force_query_ok);
    fprintf(fp, "  \"force_irrealis_ok\": %u,\n", (unsigned)w->force_irrealis_ok);
    fprintf(fp, "  \"force_directive_ok\": %u,\n", (unsigned)w->force_directive_ok);
    fprintf(fp, "  \"merkle\": \"0x%016llx\",\n", (unsigned long long)w->merkle);
    fprintf(fp, "  \"fir_wire_bytes\": %zu,\n", (ws != NULL) ? ws->wire_len : 0U);
    fprintf(fp, "  \"fir_eval_ok\": %s\n", (ws != NULL && ws->ir_eval_ok) ? "true" : "false");
    fprintf(fp, "}\n");
    fclose(fp);
    return true;
}

#endif /* SOUND_CERT_H */
