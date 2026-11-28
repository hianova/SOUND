#ifndef SOUND_CERT_H
#define SOUND_CERT_H

#include "flow_ir_builder.h"
#include "flow_ir_io.h"
#include "flow_wire_pack.h"
#include "sound.h"

#include <stdio.h>

typedef flow_ir_builder_ws_t sound_cert_ws_t;

#define SOUND_CERT_PACK_MAGIC   UINT32_C(0x43444E53) /* "SNDC" */
#define SOUND_CERT_PACK_VERSION UINT8_C(1)
#define SOUND_CERT_MAX_BYTES    16384U

typedef enum {
    SOUND_WIRE_KIND_CONTRACT_SPEC = 1U,
    SOUND_WIRE_KIND_CONTRACT_CERT = 2U,
    SOUND_WIRE_KIND_HUNT_WITNESS = 3U
} sound_wire_kind_t;

typedef enum {
    SOUND_HUNT_MODE_PARADOX_MIN = 1U,
    SOUND_HUNT_MODE_SHEAF_H1 = 2U,
    SOUND_HUNT_MODE_CHISHOLM = 3U,
    SOUND_HUNT_MODE_NORM_TRAP = 4U,
    SOUND_HUNT_MODE_ASPECT_FIREWALL = 5U,
    SOUND_HUNT_MODE_PRESUP_FILTER = 6U,
    SOUND_HUNT_MODE_CONTRACT_DRIFT = 7U,
    SOUND_HUNT_MODE_CONTRACT_LOOPHOLE = 8U
} sound_hunt_mode_t;

static inline bool sound_cert_bytes_equal(const uint8_t *a, const uint8_t *b, size_t n) {
    if (a == NULL || b == NULL) {
        return false;
    }
    for (size_t i = 0U; i < n; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
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
    flow_ir_builder_init(ws, "paradox_min_gamma");
    flow_ir_prog_t *p = &ws->ir_prog;
    p->num_inputs = 1U;
    p->inputs[0] = 0U;
    p->vals[0].width = 3U;
    p->vals[0].is_classical = 0U;
    snprintf(p->vals[0].name, sizeof(p->vals[0].name), "blocked3");
    p->num_vals = 1U;

    uint32_t k0 = flow_ir_emit_const(p, 3U, UINT64_C(0), "c0");
    uint32_t c1 = flow_ir_emit_const(p, 3U, UINT64_C(1), "c1");
    uint32_t bit0 = flow_ir_emit_const(p, 3U, UINT64_C(1), "b0");
    uint32_t bit1 = flow_ir_emit_const(p, 3U, UINT64_C(2), "b1");
    uint32_t bit2 = flow_ir_emit_const(p, 3U, UINT64_C(4), "b2");
    uint32_t a0 = flow_ir_emit_const(p, 3U, (uint64_t)(w->atk_to[0] & 7U), "a0");
    uint32_t a1 = flow_ir_emit_const(p, 3U, (uint64_t)(w->atk_to[1] & 7U), "a1");
    uint32_t a2 = flow_ir_emit_const(p, 3U, (uint64_t)(w->atk_to[2] & 7U), "a2");
    (void)c1;

    uint32_t h0 = flow_ir_emit_op2(p, FLOW_IR_OP_AND_VEC, 3U, 0U, a0, "h0");
    uint32_t ok0 = flow_ir_emit_op1(p, FLOW_IR_OP_EQ_ZERO, 1U, h0, 0U, "ok0");
    uint32_t t0 = flow_ir_emit_sel(p, 3U, ok0, k0, bit0, "t0");

    uint32_t h1 = flow_ir_emit_op2(p, FLOW_IR_OP_AND_VEC, 3U, 0U, a1, "h1");
    uint32_t ok1 = flow_ir_emit_op1(p, FLOW_IR_OP_EQ_ZERO, 1U, h1, 0U, "ok1");
    uint32_t t1 = flow_ir_emit_sel(p, 3U, ok1, k0, bit1, "t1");

    uint32_t h2 = flow_ir_emit_op2(p, FLOW_IR_OP_AND_VEC, 3U, 0U, a2, "h2");
    uint32_t ok2 = flow_ir_emit_op1(p, FLOW_IR_OP_EQ_ZERO, 1U, h2, 0U, "ok2");
    uint32_t t2 = flow_ir_emit_sel(p, 3U, ok2, k0, bit2, "t2");

    uint32_t g01 = flow_ir_emit_op2(p, FLOW_IR_OP_XOR_VEC, 3U, t0, t1, "g01");
    uint32_t g_all = flow_ir_emit_op2(p, FLOW_IR_OP_XOR_VEC, 3U, g01, t2, "gamma_out");
    if (g_all == UINT32_MAX) {
        return false;
    }
    p->num_outputs = 1U;
    p->outputs[0] = g_all;
    if (!flow_ir_builder_seal_packed_and_write(ws, out_fir_path)) {
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

static inline bool sound_cert_write_paradox_json_fp(const sound_paradox_witness_t *w,
                                                    size_t fir_wire_bytes, bool fir_eval_ok,
                                                    FILE *fp) {
    if (w == NULL || fp == NULL) {
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
    fprintf(fp, "  \"fir_wire_bytes\": %zu,\n", fir_wire_bytes);
    fprintf(fp, "  \"fir_eval_ok\": %s\n", fir_eval_ok ? "true" : "false");
    fprintf(fp, "}\n");
    return ferror(fp) == 0;
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
    bool ok = sound_cert_write_paradox_json_fp(w, (ws != NULL) ? ws->wire_len : 0U,
                                               (ws != NULL && ws->ir_eval_ok), fp);
    int cls = fclose(fp);
    return ok && (cls == 0);
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
    flow_ir_builder_init(ws, "sheaf_h1_holonomy");
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

    uint32_t x01 = flow_ir_emit_op2(p, FLOW_IR_OP_XOR_VEC, 64U, 0U, 1U, "h01");
    uint32_t hol = flow_ir_emit_op2(p, FLOW_IR_OP_XOR_VEC, 64U, x01, 2U, "holonomy");
    uint32_t gluable = flow_ir_emit_op1(p, FLOW_IR_OP_EQ_ZERO, 1U, hol, 0U, "gluable");
    if (gluable == UINT32_MAX) {
        return false;
    }
    p->num_outputs = 2U;
    p->outputs[0] = hol;
    p->outputs[1] = gluable;
    if (!flow_ir_builder_seal_packed_and_write(ws, out_fir_path)) {
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

static inline bool sound_cert_write_sheaf_json_fp(const sound_sheaf_witness_t *w,
                                                  size_t fir_wire_bytes, bool fir_eval_ok,
                                                  FILE *fp) {
    if (w == NULL || fp == NULL) {
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
    fprintf(fp, "  \"fir_wire_bytes\": %zu,\n", fir_wire_bytes);
    fprintf(fp, "  \"fir_eval_ok\": %s\n", fir_eval_ok ? "true" : "false");
    fprintf(fp, "}\n");
    return ferror(fp) == 0;
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
    bool ok = sound_cert_write_sheaf_json_fp(w, (ws != NULL) ? ws->wire_len : 0U,
                                             (ws != NULL && ws->ir_eval_ok), fp);
    int cls = fclose(fp);
    return ok && (cls == 0);
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
    if (flow_ir_load_bytes_fiber(&ws->ir_prog, ws->wire_buf, ws->wire_len) !=
        FLOW_IR_FIBER_COMMITTED) {
        return false;
    }
    if (!flow_ir_encode_packed_bytes(&ws->ir_prog, ws->wire_buf, sizeof(ws->wire_buf),
                                     &ws->wire_len) ||
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
    uint64_t inv[5] = {UINT64_C(0x4), UINT64_C(0), UINT64_C(0x4), UINT64_C(0), UINT64_C(0)};
    uint64_t outv[4] = {UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    ws->ir_eval_ok = flow_ir_eval_u64(&ws->ir_reloaded, inv, outv) && outv[0] == UINT64_C(0x4) &&
                     outv[3] == UINT64_C(0xC);
    if (!ws->ir_eval_ok) {
        return false;
    }
    if (out_fir_path != NULL && out_fir_path[0] != '\0') {
        return flow_ir_write_bytes_file(out_fir_path, ws->wire_buf, ws->wire_len);
    }
    return true;
}

static inline bool sound_cert_write_chisholm_json_fp(const sound_chisholm_witness_t *w,
                                                     size_t fir_wire_bytes, bool fir_eval_ok,
                                                     FILE *fp) {
    if (w == NULL || fp == NULL) {
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
    fprintf(fp, "  \"fir_wire_bytes\": %zu,\n", fir_wire_bytes);
    fprintf(fp, "  \"fir_eval_ok\": %s\n", fir_eval_ok ? "true" : "false");
    fprintf(fp, "}\n");
    return ferror(fp) == 0;
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
    bool ok = sound_cert_write_chisholm_json_fp(w, (ws != NULL) ? ws->wire_len : 0U,
                                                (ws != NULL && ws->ir_eval_ok), fp);
    int cls = fclose(fp);
    return ok && (cls == 0);
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
        flow_ir_load_bytes_fiber(&ws->ir_prog, ws->wire_buf, ws->wire_len) !=
            FLOW_IR_FIBER_COMMITTED ||
        !flow_ir_encode_packed_bytes(&ws->ir_prog, ws->wire_buf, sizeof(ws->wire_buf),
                                     &ws->wire_len) ||
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
        return flow_ir_write_bytes_file(out_fir_path, ws->wire_buf, ws->wire_len);
    }
    return true;
}

static inline bool sound_cert_write_norm_trap_json_fp(const sound_norm_trap_witness_t *w,
                                                      size_t fir_wire_bytes, bool fir_eval_ok,
                                                      FILE *fp) {
    if (w == NULL || fp == NULL) {
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
    fprintf(fp, "  \"fir_wire_bytes\": %zu,\n", fir_wire_bytes);
    fprintf(fp, "  \"fir_eval_ok\": %s\n", fir_eval_ok ? "true" : "false");
    fprintf(fp, "}\n");
    return ferror(fp) == 0;
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
    bool ok = sound_cert_write_norm_trap_json_fp(w, (ws != NULL) ? ws->wire_len : 0U,
                                                 (ws != NULL && ws->ir_eval_ok), fp);
    int cls = fclose(fp);
    return ok && (cls == 0);
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
    flow_ir_builder_init(ws, "aspect_telic_gate");
    flow_ir_prog_t *p = &ws->ir_prog;
    /* Input: 2-bit aspect code (0=state, 1=activity, 2=accomplishment, 3=achievement)
       telic bit == bit 1 of aspect code! */
    p->num_inputs = 1U;
    p->inputs[0] = 0U;
    p->vals[0].width = 2U;
    snprintf(p->vals[0].name, sizeof(p->vals[0].name), "aspect2");
    p->num_vals = 1U;

    uint32_t telic = flow_ir_emit_op1(p, FLOW_IR_OP_SLICE, 1U, 0U, 1U, "is_telic");
    uint32_t dyn = flow_ir_emit_op1(p, FLOW_IR_OP_SLICE, 1U, 0U, 0U, "is_dyn");
    if (telic == UINT32_MAX || dyn == UINT32_MAX) {
        return false;
    }
    p->num_outputs = 2U;
    p->outputs[0] = telic;
    p->outputs[1] = dyn;
    if (!flow_ir_builder_seal_packed_and_write(ws, out_fir_path)) {
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

static inline bool sound_cert_write_aspect_json_fp(const sound_aspect_witness_t *w,
                                                   size_t fir_wire_bytes, bool fir_eval_ok,
                                                   FILE *fp) {
    if (w == NULL || fp == NULL) {
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
    fprintf(fp, "  \"fir_wire_bytes\": %zu,\n", fir_wire_bytes);
    fprintf(fp, "  \"fir_eval_ok\": %s\n", fir_eval_ok ? "true" : "false");
    fprintf(fp, "}\n");
    return ferror(fp) == 0;
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
    bool ok = sound_cert_write_aspect_json_fp(w, (ws != NULL) ? ws->wire_len : 0U,
                                              (ws != NULL && ws->ir_eval_ok), fp);
    int cls = fclose(fp);
    return ok && (cls == 0);
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
    flow_ir_builder_init(ws, "presup_strawson_eval");
    flow_ir_prog_t *p = &ws->ir_prog;
    p->num_inputs = 2U;
    p->inputs[0] = 0U;
    p->inputs[1] = 1U;
    p->vals[0].width = 2U;
    p->vals[1].width = 2U;
    snprintf(p->vals[0].name, sizeof(p->vals[0].name), "assert2");
    snprintf(p->vals[1].name, sizeof(p->vals[1].name), "presup2");
    p->num_vals = 2U;

    uint32_t c0 = flow_ir_emit_const(p, 2U, UINT64_C(0), "bot");
    uint32_t c_true = flow_ir_emit_const(p, 2U, UINT64_C(2), "btrue");
    uint32_t diff = flow_ir_emit_op2(p, FLOW_IR_OP_XOR_VEC, 2U, 1U, c_true, "pdiff");
    uint32_t p_ok = flow_ir_emit_op1(p, FLOW_IR_OP_EQ_ZERO, 1U, diff, 0U, "p_ok");
    uint32_t res = flow_ir_emit_sel(p, 2U, p_ok, c0, 0U, "eval_out");
    if (res == UINT32_MAX) {
        return false;
    }
    p->num_outputs = 1U;
    p->outputs[0] = res;
    if (!flow_ir_builder_seal_packed_and_write(ws, out_fir_path)) {
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

static inline bool sound_cert_write_presup_json_fp(const sound_presup_witness_t *w,
                                                   size_t fir_wire_bytes, bool fir_eval_ok,
                                                   FILE *fp) {
    if (w == NULL || fp == NULL) {
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
    fprintf(fp, "  \"fir_wire_bytes\": %zu,\n", fir_wire_bytes);
    fprintf(fp, "  \"fir_eval_ok\": %s\n", fir_eval_ok ? "true" : "false");
    fprintf(fp, "}\n");
    return ferror(fp) == 0;
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
    bool ok = sound_cert_write_presup_json_fp(w, (ws != NULL) ? ws->wire_len : 0U,
                                              (ws != NULL && ws->ir_eval_ok), fp);
    int cls = fclose(fp);
    return ok && (cls == 0);
}

/* ============================================================================
 * 7. Pillar 5: `contract-drift` (.lean, .fir, .json)
 * ============================================================================ */

static inline const char *sound_cert_clause_kind_lean(uint8_t k) {
    switch (k) {
    case (uint8_t)SOUND_CLAUSE_BASE_FEE:
        return ".baseFee";
    case (uint8_t)SOUND_CLAUSE_UNILATERAL_POWER:
        return ".unilateralPower";
    case (uint8_t)SOUND_CLAUSE_AMBIGUOUS_OBLIGATION:
        return ".ambiguousObligation";
    case (uint8_t)SOUND_CLAUSE_PRESUP_SMUGGLE:
        return ".presupSmuggle";
    case (uint8_t)SOUND_CLAUSE_CHISHOLM_DEFAULT:
        return ".chisholmDefault";
    case (uint8_t)SOUND_CLAUSE_RECIPROCITY:
        return ".reciprocity";
    default:
        return ".baseFee";
    }
}

static inline const char *sound_cert_edge_kind_lean(uint8_t k) {
    switch (k) {
    case (uint8_t)SOUND_EDGE_OVERRIDE:
        return ".override";
    case (uint8_t)SOUND_EDGE_REDIRECT:
        return ".redirect";
    case (uint8_t)SOUND_EDGE_POWER_DELEGATE:
        return ".powerDelegate";
    case (uint8_t)SOUND_EDGE_DEFAULT_TRIGGER:
        return ".defaultTrigger";
    default:
        return ".redirect";
    }
}

static inline bool sound_cert_emit_contract_lean(const sound_contract_spec_t *spec,
                                                 const sound_contract_audit_t *audit,
                                                 const char *path) {
    if (spec == NULL || audit == NULL || path == NULL) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    uint32_t n = spec->num_clauses;
    if (n > SOUND_CONTRACT_MAX_CLAUSES) {
        n = SOUND_CONTRACT_MAX_CLAUSES;
    }
    uint32_t ne = spec->num_edges;
    if (ne > SOUND_CONTRACT_MAX_EDGES) {
        ne = SOUND_CONTRACT_MAX_EDGES;
    }
    bool use_graph = (ne > UINT32_C(0)) || (spec->coverage_gaps > UINT32_C(0));

    fprintf(fp, "import Sound.Theory\n\n");
    fprintf(fp, "namespace %s\n\n",
            use_graph ? "Sound.CertContractLoophole" : "Sound.CertContractDrift");
    fprintf(fp, "open Sound.Contract Sound.Bilattice\n\n");
    fprintf(fp, "def spec : SummarySpec :=\n");
    fprintf(fp,
            "  { maxLossCap := %llu, maxExitFee := %llu, protectedMask := 0x%llx#64, "
            "maxAsymNum := %llu, maxAsymDen := %llu }\n\n",
            (unsigned long long)spec->max_loss_cap, (unsigned long long)spec->max_exit_fee,
            (unsigned long long)spec->protected_mask, (unsigned long long)spec->max_asym_num,
            (unsigned long long)spec->max_asym_den);

    fprintf(fp, "def clauses : List ContractClause :=\n");
    if (n == 0U) {
        fprintf(fp, "  []\n\n");
    } else {
        for (uint32_t i = 0U; i < n; i++) {
            const sound_contract_clause_t *c = &spec->clauses[i];
            fprintf(fp,
                    "  %c { clauseId := %u, kind := %s, disclosed := %s, targetMask := 0x%llx#64, "
                    "b4Truth := %u#2, baseLoss := %llu, extraDrift := %llu, exitFee := %llu, "
                    "consPenalty := %llu, instPenalty := %llu, sheafCycle := %s }\n",
                    (i == 0U) ? '[' : ',', (unsigned)c->clause_id,
                    sound_cert_clause_kind_lean(c->kind), (c->disclosed != 0U) ? "true" : "false",
                    (unsigned long long)c->target_mask, (unsigned)(c->b4_truth & 3U),
                    (unsigned long long)c->base_loss, (unsigned long long)c->extra_drift,
                    (unsigned long long)c->exit_fee, (unsigned long long)c->cons_penalty,
                    (unsigned long long)c->inst_penalty, (c->sheaf_cycle != 0U) ? "true" : "false");
        }
        fprintf(fp, "  ]\n\n");
    }

    if (use_graph) {
        fprintf(fp, "def edges : List ClauseEdge :=\n");
        if (ne == 0U) {
            fprintf(fp, "  []\n\n");
        } else {
            for (uint32_t i = 0U; i < ne; i++) {
                const sound_contract_edge_t *e = &spec->edges[i];
                fprintf(fp,
                        "  %c { src := %u, dst := %u, edgeKind := %s, cocycleShift := %u#2, "
                        "extraExposure := %llu }\n",
                        (i == 0U) ? '[' : ',', (unsigned)e->src, (unsigned)e->dst,
                        sound_cert_edge_kind_lean(e->edge_kind),
                        (unsigned)(e->cocycle_shift & UINT8_C(3)),
                        (unsigned long long)e->extra_exposure);
            }
            fprintf(fp, "  ]\n\n");
        }
        fprintf(fp, "def coverageGaps : Nat := %u\n\n", (unsigned)spec->coverage_gaps);

        if (audit->certified == UINT32_C(1)) {
            fprintf(fp, "theorem cert_graph_verdict :\n");
            fprintf(fp, "    auditContractGraph spec clauses edges 0 = .certified := by\n");
            fprintf(fp, "  decide\n\n");

            fprintf(fp, "theorem cert_graph_loss_bound (act : AdversarialAction) :\n");
            fprintf(
                fp,
                "    actualConsumerLoss act clauses + edgesExposure edges ≤ spec.maxLossCap :=\n");
            fprintf(
                fp,
                "  compositional_certified_bounds_loss spec clauses edges cert_graph_verdict act\n\n");

            fprintf(fp, "theorem cert_base_verdict :\n");
            fprintf(fp, "    auditContract spec clauses = .certified := by\n");
            fprintf(fp, "  decide\n\n");

            fprintf(fp, "theorem cert_zero_benefit_drift :\n");
            fprintf(fp, "    benefitDrift spec clauses ≤ 0 :=\n");
            fprintf(fp, "  certified_zero_benefit_drift spec clauses cert_base_verdict\n\n");
        } else {
            fprintf(fp, "theorem cert_graph_vetoed :\n");
            fprintf(fp, "    auditContractGraph spec clauses edges coverageGaps = .vetoed := by\n");
            fprintf(fp, "  decide\n\n");
            if (audit->repaired_certified == UINT32_C(1)) {
                fprintf(fp, "theorem cert_repaired_graph_certified :\n");
                fprintf(fp,
                        "    auditContractGraph spec (repairContractGraph spec clauses edges).1 "
                        "(repairContractGraph spec clauses edges).2 0 = .certified := by\n");
                fprintf(fp, "  decide\n\n");

                fprintf(fp, "theorem cert_repaired_graph_loss_bound (act : AdversarialAction) :\n");
                fprintf(
                    fp,
                    "    actualConsumerLoss act (repairContractGraph spec clauses edges).1 +\n");
                fprintf(
                    fp,
                    "      edgesExposure (repairContractGraph spec clauses edges).2 ≤ spec.maxLossCap :=\n");
                fprintf(fp, "  compositional_certified_bounds_loss spec "
                            "(repairContractGraph spec clauses edges).1 "
                            "(repairContractGraph spec clauses edges).2 "
                            "cert_repaired_graph_certified act\n\n");
            } else {
                fprintf(fp, "theorem cert_repaired_edges_clean :\n");
                fprintf(fp,
                        "    edgesExposure (repairContractGraph spec clauses edges).2 = 0 :=\n");
                fprintf(fp, "  (graph_repair_restores_certified spec clauses edges).1\n\n");
            }
        }
        fprintf(fp, "end Sound.CertContractLoophole\n");
    } else {
        if (audit->certified == UINT32_C(1)) {
            fprintf(fp, "theorem cert_contract_verdict :\n");
            fprintf(fp, "    auditContract spec clauses = .certified := by\n");
            fprintf(fp, "  decide\n\n");

            fprintf(fp, "theorem cert_adversarial_loss_bound (act : AdversarialAction) :\n");
            fprintf(fp, "    actualConsumerLoss act clauses ≤ spec.maxLossCap :=\n");
            fprintf(
                fp,
                "  certified_bounds_adversarial_loss spec clauses cert_contract_verdict act\n\n");

            fprintf(fp, "theorem cert_zero_benefit_drift :\n");
            fprintf(fp, "    benefitDrift spec clauses ≤ 0 :=\n");
            fprintf(fp, "  certified_zero_benefit_drift spec clauses cert_contract_verdict\n\n");
        } else {
            fprintf(fp, "theorem cert_contract_vetoed :\n");
            fprintf(fp, "    auditContract spec clauses = .vetoed := by\n");
            fprintf(fp, "  decide\n\n");
            if (audit->repaired_certified == UINT32_C(1)) {
                fprintf(fp, "theorem cert_repaired_certified :\n");
                fprintf(
                    fp,
                    "    auditContract spec (repairContract spec clauses) = .certified := by\n");
                fprintf(fp, "  decide\n\n");

                fprintf(fp,
                        "theorem cert_repaired_adversarial_bound (act : AdversarialAction) :\n");
                fprintf(
                    fp,
                    "    actualConsumerLoss act (repairContract spec clauses) ≤ spec.maxLossCap :=\n");
                fprintf(fp,
                        "  certified_bounds_adversarial_loss spec (repairContract spec clauses) "
                        "cert_repaired_certified act\n\n");
            } else {
                fprintf(fp, "theorem cert_repair_removes_predatory :\n");
                fprintf(
                    fp,
                    "    ∀ c ∈ repairContract spec clauses, clausePredatory spec c = false :=\n");
                fprintf(fp, "  (repair_eliminates_predatory_clauses spec clauses).1\n\n");
            }
        }
        fprintf(fp, "end Sound.CertContractDrift\n");
    }

    fclose(fp);
    return true;
}

static inline bool sound_cert_emit_contract_fir(const sound_contract_audit_t *audit,
                                                const char *out_fir_path, sound_cert_ws_t *ws) {
    if (audit == NULL || ws == NULL) {
        return false;
    }
    flow_ir_builder_init(ws, "contract_consent_gate");
    flow_ir_prog_t *p = &ws->ir_prog;
    p->num_inputs = 2U;
    p->inputs[0] = 0U;
    p->inputs[1] = 1U;
    p->vals[0].width = 1U;
    p->vals[1].width = 1U;
    snprintf(p->vals[0].name, sizeof(p->vals[0].name), "viol_nonzero");
    snprintf(p->vals[1].name, sizeof(p->vals[1].name), "over_cap");
    p->num_vals = 2U;

    uint32_t pair = flow_ir_emit_op2(p, FLOW_IR_OP_CONCAT, 2U, 0U, 1U, "pair2");
    uint32_t vetoed = flow_ir_emit_op1(p, FLOW_IR_OP_OR_REDUCE, 1U, pair, 0U, "vetoed");
    uint32_t cert = flow_ir_emit_op1(p, FLOW_IR_OP_NOT_VEC, 1U, vetoed, 0U, "certified");
    if (pair == UINT32_MAX || vetoed == UINT32_MAX || cert == UINT32_MAX) {
        return false;
    }
    p->num_outputs = 2U;
    p->outputs[0] = cert;
    p->outputs[1] = vetoed;
    if (!flow_ir_builder_seal_packed_and_write(ws, out_fir_path)) {
        return false;
    }

    bool ok = true;
    for (uint32_t a = 0U; a < 2U; a++) {
        for (uint32_t b = 0U; b < 2U; b++) {
            uint64_t inv[2] = {(uint64_t)a, (uint64_t)b};
            uint64_t outv[2] = {UINT64_C(0), UINT64_C(0)};
            uint64_t exp_veto = (a != 0U || b != 0U) ? UINT64_C(1) : UINT64_C(0);
            uint64_t exp_cert = UINT64_C(1) ^ exp_veto;
            if (!flow_ir_eval_u64(&ws->ir_reloaded, inv, outv) || (outv[0] & 1ULL) != exp_cert ||
                (outv[1] & 1ULL) != exp_veto) {
                ok = false;
                break;
            }
        }
    }
    {
        uint64_t inv[2] = {
            (audit->viol_flags != UINT32_C(0) || audit->num_predatory_clauses != UINT32_C(0))
                ? UINT64_C(1)
                : UINT64_C(0),
            (audit->benefit_drift > 0) ? UINT64_C(1) : UINT64_C(0)};
        uint64_t outv[2] = {UINT64_C(0), UINT64_C(0)};
        if (!flow_ir_eval_u64(&ws->ir_reloaded, inv, outv) ||
            (outv[0] & 1ULL) != (uint64_t)(audit->certified != UINT32_C(0) ? 1U : 0U)) {
            ok = false;
        }
    }
    ws->ir_eval_ok = ok;
    return ok;
}

static inline bool sound_cert_write_contract_json_fp(const sound_contract_spec_t *spec,
                                                     const sound_contract_audit_t *audit,
                                                     size_t fir_wire_bytes, bool fir_eval_ok,
                                                     FILE *fp) {
    if (spec == NULL || audit == NULL || fp == NULL) {
        return false;
    }
    uint32_t npred = audit->num_predatory_clauses;
    if (npred > SOUND_CONTRACT_MAX_CLAUSES) {
        npred = SOUND_CONTRACT_MAX_CLAUSES;
    }
    fprintf(fp, "{\n");
    fprintf(fp, "  \"mode\": \"contract-drift\",\n");
    fprintf(fp, "  \"seed\": \"0x%016llx\",\n", (unsigned long long)spec->seed);
    fprintf(fp, "  \"certified\": %u,\n", (unsigned)audit->certified);
    fprintf(fp, "  \"verdict\": \"%s\",\n",
            (audit->certified != UINT32_C(0)) ? "CERTIFIED" : "VETOED");
    fprintf(fp, "  \"viol_flags\": %u,\n", (unsigned)audit->viol_flags);
    fprintf(fp, "  \"num_clauses\": %u,\n", (unsigned)spec->num_clauses);
    fprintf(fp, "  \"num_edges\": %u,\n", (unsigned)spec->num_edges);
    fprintf(fp, "  \"coverage_gaps\": %u,\n", (unsigned)spec->coverage_gaps);
    fprintf(fp, "  \"num_predatory_clauses\": %u,\n", (unsigned)audit->num_predatory_clauses);
    fprintf(fp, "  \"num_malicious_edges\": %u,\n", (unsigned)audit->num_malicious_edges);
    fprintf(fp, "  \"graph_sheaf_h1_obstructed\": %u,\n",
            (unsigned)audit->graph_sheaf_h1_obstructed);
    fprintf(fp, "  \"graph_bypass_mask\": %u,\n", (unsigned)audit->graph_bypass_mask);
    fprintf(fp, "  \"repaired_certified\": %u,\n", (unsigned)audit->repaired_certified);
    fprintf(fp, "  \"max_loss_cap\": %llu,\n", (unsigned long long)spec->max_loss_cap);
    fprintf(fp, "  \"max_exit_fee\": %llu,\n", (unsigned long long)spec->max_exit_fee);
    fprintf(fp, "  \"base_loss_sum\": %llu,\n", (unsigned long long)audit->base_loss_sum);
    fprintf(fp, "  \"edge_exposure_sum\": %llu,\n", (unsigned long long)audit->edge_exposure_sum);
    fprintf(fp, "  \"worst_case_loss\": %llu,\n", (unsigned long long)audit->worst_case_loss);
    fprintf(fp, "  \"benefit_drift\": %lld,\n", (long long)audit->benefit_drift);
    fprintf(fp, "  \"max_exit_seen\": %llu,\n", (unsigned long long)audit->max_exit_seen);
    fprintf(fp, "  \"repaired_worst_loss\": %llu,\n",
            (unsigned long long)audit->repaired_worst_loss);
    fprintf(fp, "  \"predatory_clause_ids\": [");
    for (uint32_t i = 0U; i < npred; i++) {
        fprintf(fp, "%s%u", (i == 0U) ? "" : ", ", (unsigned)audit->predatory_clause_ids[i]);
    }
    fprintf(fp, "],\n");
    fprintf(fp, "  \"merkle\": \"0x%016llx\",\n", (unsigned long long)audit->merkle);
    fprintf(fp, "  \"fir_wire_bytes\": %zu,\n", fir_wire_bytes);
    fprintf(fp, "  \"fir_eval_ok\": %s\n", fir_eval_ok ? "true" : "false");
    fprintf(fp, "}\n");
    return ferror(fp) == 0;
}

static inline bool sound_cert_emit_contract_json(const sound_contract_spec_t *spec,
                                                 const sound_contract_audit_t *audit,
                                                 const sound_cert_ws_t *ws, const char *path) {
    if (spec == NULL || audit == NULL || path == NULL) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    bool ok = sound_cert_write_contract_json_fp(spec, audit, (ws != NULL) ? ws->wire_len : 0U,
                                                (ws != NULL && ws->ir_eval_ok), fp);
    int cls = fclose(fp);
    return ok && (cls == 0);
}

static inline bool sound_cert_emit_contract_receipt(const sound_contract_spec_t *spec,
                                                    const sound_contract_audit_t *audit,
                                                    const char *path) {
    if (spec == NULL || audit == NULL || path == NULL) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    uint32_t n = spec->num_clauses;
    if (n > SOUND_CONTRACT_MAX_CLAUSES) {
        n = SOUND_CONTRACT_MAX_CLAUSES;
    }
    uint32_t ne = spec->num_edges;
    if (ne > SOUND_CONTRACT_MAX_EDGES) {
        ne = SOUND_CONTRACT_MAX_EDGES;
    }

    fprintf(fp, "# SOUND Contract Informed-Consent & Risk Receipt\n\n");
    fprintf(fp, "- **Verdict**: `%s` (`merkle=0x%016llx`, `viol_flags=0x%03x`)\n",
            (audit->certified != UINT32_C(0)) ? "CERTIFIED — SAFE TO SIGN" : "VETOED — DO NOT SIGN",
            (unsigned long long)audit->merkle, (unsigned)audit->viol_flags);
    fprintf(fp,
            "- **Promised Summary Cap (`max_loss_cap`)**: `%llu` | **Max Exit Fee Cap**: `%llu`\n",
            (unsigned long long)spec->max_loss_cap, (unsigned long long)spec->max_exit_fee);
    fprintf(fp,
            "- **Adversarial Worst-Case Consumer Loss**: `%llu` "
            "(disclosed base `%llu` + cross-clause exposure `%llu`)\n",
            (unsigned long long)audit->worst_case_loss, (unsigned long long)audit->base_loss_sum,
            (unsigned long long)audit->edge_exposure_sum);
    fprintf(fp, "- **Benefit Drift (`worst - cap`)**: `%+lld`\n", (long long)audit->benefit_drift);
    if (spec->coverage_gaps != UINT32_C(0)) {
        fprintf(fp,
                "- **Coverage Gate (`0x%x`)**: **FAIL-CLOSED — %u raw sentence(s) with amounts "
                "or high-risk triggers omitted from summary/clauses**\n",
                (unsigned)SOUND_CVIOL_COVERAGE_GAP, (unsigned)spec->coverage_gaps);
    }
    fprintf(fp, "\n## Adversarial Counterexample Scenario & Redline Guidance\n\n");
    if (audit->certified != UINT32_C(0)) {
        fprintf(fp,
                "All `%u` clauses and `%u` cross-references satisfy consumer immunity, ambiguity "
                "elimination, reciprocity bounds, and `benefit_drift <= 0`.\n",
                (unsigned)spec->num_clauses, (unsigned)spec->num_edges);
    } else {
        fprintf(
            fp,
            "Under legal institutional play, the counterparty can combine `%u` predatory "
            "clause(s) and `%u` malicious cross-reference edge(s) to push exposure up to `%llu`:\n\n",
            (unsigned)audit->num_predatory_clauses, (unsigned)audit->num_malicious_edges,
            (unsigned long long)audit->worst_case_loss);
        for (uint32_t i = 0U; i < n; i++) {
            uint32_t vm = audit->clause_viol_masks[i];
            if (vm != UINT32_C(0)) {
                fprintf(fp, "- **REDLINE REMOVE Clause #%u**: flags=`0x%03x`",
                        (unsigned)spec->clauses[i].clause_id, (unsigned)vm);
                if ((vm & (uint32_t)SOUND_CVIOL_IMMUNITY_BREACH) != 0U) {
                    fprintf(fp, " [pierces protected consumer immunity]");
                }
                if ((vm & (uint32_t)SOUND_CVIOL_UNINFORMED_AMBIG) != 0U) {
                    fprintf(fp, " [undefined/conflicting B4 obligation]");
                }
                if ((vm & (uint32_t)SOUND_CVIOL_PRESUP_SMUGGLE) != 0U) {
                    fprintf(fp, " [undisclosed cost fine print]");
                }
                if ((vm & (uint32_t)SOUND_CVIOL_CHISHOLM_CASCADE) != 0U) {
                    fprintf(fp, " [default acceleration trap]");
                }
                if ((vm & (uint32_t)SOUND_CVIOL_UNCONSCIONABLE) != 0U) {
                    fprintf(fp, " [unconscionable penalty asymmetry]");
                }
                if ((vm & (uint32_t)SOUND_CVIOL_EXIT_TRAP) != 0U) {
                    fprintf(fp, " [excessive exit penalty]");
                }
                fprintf(fp, " (worst contribution `%llu`)\n",
                        (unsigned long long)(spec->clauses[i].base_loss +
                                             spec->clauses[i].extra_drift));
            }
        }
        if ((audit->viol_flags & (uint32_t)SOUND_CVIOL_GRAPH_BYPASS) != 0U) {
            fprintf(fp,
                    "- **REDLINE SEVER Bypass Path**: Cross-clause override/delegation bypass "
                    "reaches protected clause bitmask `0x%x`.\n",
                    (unsigned)audit->graph_bypass_mask);
        }
        if ((audit->viol_flags & (uint32_t)SOUND_CVIOL_GRAPH_SHEAF_H1) != 0U) {
            fprintf(fp,
                    "- **REDLINE RESOLVE Annex Contradiction (`H^1 != 0`)**: Global interpretation "
                    "obstructed across cross-clause overrides.\n");
        }
        if ((audit->viol_flags & (uint32_t)SOUND_CVIOL_GRAPH_CASCADE) != 0U) {
            fprintf(fp, "- **REDLINE SEVER Default Trigger Edge**: Cross-clause default trigger "
                        "amplifies consumer exposure.\n");
        }
        for (uint32_t ei = 0U; ei < ne; ei++) {
            const sound_contract_edge_t *e = &spec->edges[ei];
            bool benign = (e->edge_kind == (uint8_t)SOUND_EDGE_REDIRECT) &&
                          ((e->cocycle_shift & UINT8_C(3)) == UINT8_C(0)) &&
                          (e->extra_exposure == UINT64_C(0));
            if (!benign) {
                fprintf(fp,
                        "  - Malicious Edge `clause[%u] -> clause[%u]`: kind=`%s`, shift=`%u`, "
                        "extra_exposure=`%llu`\n",
                        (unsigned)e->src, (unsigned)e->dst, sound_cert_edge_kind_lean(e->edge_kind),
                        (unsigned)(e->cocycle_shift & UINT8_C(3)),
                        (unsigned long long)e->extra_exposure);
            }
        }
        fprintf(fp,
                "\n- **Minimal Repair Outcome**: Removing marked clauses/edges & gaps -> "
                "repaired worst-case loss `%llu` (`repaired_certified=%u`)\n",
                (unsigned long long)audit->repaired_worst_loss,
                (unsigned)audit->repaired_certified);
    }
    fclose(fp);
    return true;
}

/* ============================================================================
 * 8. Canonical "SNDC" Binary Contract & Witness Certificate Codec
 * ============================================================================
 * Uses FLOW's freestanding canonical ULEB128 varints & FNV-1a-64 trailer
 * (../FLOW/sdk/flow_wire_pack.h) with zero heap allocation and bit-exact
 * struct/JSON roundtrip.
 * ============================================================================ */

static inline bool sound_wire__put_zigzag64(uint8_t *buf, size_t cap, size_t *pos, int64_t v) {
    uint64_t zz =
        (v >= 0) ? (((uint64_t)v) << 1U) : ((((uint64_t)(-(v + (int64_t)1))) << 1U) | UINT64_C(1));
    return flow_wire_put_varu64(buf, cap, pos, zz);
}

static inline bool sound_wire__get_zigzag64(const uint8_t *buf, size_t end, size_t *pos,
                                            int64_t *out) {
    if (out == NULL) {
        return false;
    }
    uint64_t zz = UINT64_C(0);
    if (!flow_wire_get_varu64(buf, end, pos, &zz)) {
        return false;
    }
    uint64_t mag = zz >> 1U;
    *out = ((zz & UINT64_C(1)) != UINT64_C(0)) ? (-(int64_t)mag - (int64_t)1) : (int64_t)mag;
    return true;
}

static inline bool sound_wire__put_contract_spec(uint8_t *buf, size_t cap, size_t *pos,
                                                 const sound_contract_spec_t *spec) {
    if (spec == NULL || spec->num_clauses > SOUND_CONTRACT_MAX_CLAUSES ||
        spec->num_edges > SOUND_CONTRACT_MAX_EDGES) {
        return false;
    }
    if (!flow_wire_put_varu64(buf, cap, pos, spec->seed) ||
        !flow_wire_put_varu64(buf, cap, pos, spec->max_loss_cap) ||
        !flow_wire_put_varu64(buf, cap, pos, spec->max_exit_fee) ||
        !flow_wire_put_varu64(buf, cap, pos, spec->protected_mask) ||
        !flow_wire_put_varu64(buf, cap, pos, spec->max_asym_num) ||
        !flow_wire_put_varu64(buf, cap, pos, spec->max_asym_den) ||
        !flow_wire_put_varu32(buf, cap, pos, spec->num_clauses) ||
        !flow_wire_put_varu32(buf, cap, pos, spec->num_edges) ||
        !flow_wire_put_varu32(buf, cap, pos, spec->coverage_gaps)) {
        return false;
    }
    for (uint32_t i = 0U; i < spec->num_clauses; i++) {
        const sound_contract_clause_t *c = &spec->clauses[i];
        if (!flow_wire_put_varu32(buf, cap, pos, c->clause_id) ||
            !flow_wire_put_u8(buf, cap, pos, c->kind) ||
            !flow_wire_put_u8(buf, cap, pos, c->disclosed) ||
            !flow_wire_put_u8(buf, cap, pos, c->b4_truth) ||
            !flow_wire_put_u8(buf, cap, pos, c->sheaf_cycle) ||
            !flow_wire_put_varu64(buf, cap, pos, c->target_mask) ||
            !flow_wire_put_varu64(buf, cap, pos, c->base_loss) ||
            !flow_wire_put_varu64(buf, cap, pos, c->extra_drift) ||
            !flow_wire_put_varu64(buf, cap, pos, c->exit_fee) ||
            !flow_wire_put_varu64(buf, cap, pos, c->cons_penalty) ||
            !flow_wire_put_varu64(buf, cap, pos, c->inst_penalty)) {
            return false;
        }
    }
    for (uint32_t i = 0U; i < spec->num_edges; i++) {
        const sound_contract_edge_t *e = &spec->edges[i];
        if (!flow_wire_put_varu32(buf, cap, pos, e->src) ||
            !flow_wire_put_varu32(buf, cap, pos, e->dst) ||
            !flow_wire_put_u8(buf, cap, pos, e->edge_kind) ||
            !flow_wire_put_u8(buf, cap, pos, e->cocycle_shift) ||
            !flow_wire_put_varu64(buf, cap, pos, e->extra_exposure)) {
            return false;
        }
    }
    return true;
}

static inline bool sound_wire__get_contract_spec(const uint8_t *buf, size_t end, size_t *pos,
                                                 sound_contract_spec_t *spec) {
    if (spec == NULL) {
        return false;
    }
    sound_zero_bytes(spec, sizeof(*spec));
    if (!flow_wire_get_varu64(buf, end, pos, &spec->seed) ||
        !flow_wire_get_varu64(buf, end, pos, &spec->max_loss_cap) ||
        !flow_wire_get_varu64(buf, end, pos, &spec->max_exit_fee) ||
        !flow_wire_get_varu64(buf, end, pos, &spec->protected_mask) ||
        !flow_wire_get_varu64(buf, end, pos, &spec->max_asym_num) ||
        !flow_wire_get_varu64(buf, end, pos, &spec->max_asym_den) ||
        !flow_wire_get_varu32(buf, end, pos, &spec->num_clauses) ||
        !flow_wire_get_varu32(buf, end, pos, &spec->num_edges) ||
        !flow_wire_get_varu32(buf, end, pos, &spec->coverage_gaps)) {
        return false;
    }
    if (spec->num_clauses > SOUND_CONTRACT_MAX_CLAUSES ||
        spec->num_edges > SOUND_CONTRACT_MAX_EDGES) {
        return false;
    }
    for (uint32_t i = 0U; i < spec->num_clauses; i++) {
        sound_contract_clause_t *c = &spec->clauses[i];
        if (!flow_wire_get_varu32(buf, end, pos, &c->clause_id) ||
            !flow_wire_get_u8(buf, end, pos, &c->kind) ||
            !flow_wire_get_u8(buf, end, pos, &c->disclosed) ||
            !flow_wire_get_u8(buf, end, pos, &c->b4_truth) ||
            !flow_wire_get_u8(buf, end, pos, &c->sheaf_cycle) ||
            !flow_wire_get_varu64(buf, end, pos, &c->target_mask) ||
            !flow_wire_get_varu64(buf, end, pos, &c->base_loss) ||
            !flow_wire_get_varu64(buf, end, pos, &c->extra_drift) ||
            !flow_wire_get_varu64(buf, end, pos, &c->exit_fee) ||
            !flow_wire_get_varu64(buf, end, pos, &c->cons_penalty) ||
            !flow_wire_get_varu64(buf, end, pos, &c->inst_penalty)) {
            return false;
        }
    }
    for (uint32_t i = 0U; i < spec->num_edges; i++) {
        sound_contract_edge_t *e = &spec->edges[i];
        if (!flow_wire_get_varu32(buf, end, pos, &e->src) ||
            !flow_wire_get_varu32(buf, end, pos, &e->dst) ||
            !flow_wire_get_u8(buf, end, pos, &e->edge_kind) ||
            !flow_wire_get_u8(buf, end, pos, &e->cocycle_shift) ||
            !flow_wire_get_varu64(buf, end, pos, &e->extra_exposure)) {
            return false;
        }
    }
    return true;
}

static inline bool sound_wire__put_contract_audit(uint8_t *buf, size_t cap, size_t *pos,
                                                  uint32_t num_clauses,
                                                  const sound_contract_audit_t *audit) {
    if (audit == NULL || num_clauses > SOUND_CONTRACT_MAX_CLAUSES ||
        audit->num_predatory_clauses > SOUND_CONTRACT_MAX_CLAUSES) {
        return false;
    }
    if (!flow_wire_put_varu32(buf, cap, pos, audit->certified) ||
        !flow_wire_put_varu32(buf, cap, pos, audit->viol_flags) ||
        !flow_wire_put_varu32(buf, cap, pos, audit->num_predatory_clauses) ||
        !flow_wire_put_varu32(buf, cap, pos, audit->repaired_certified) ||
        !flow_wire_put_varu32(buf, cap, pos, audit->num_malicious_edges) ||
        !flow_wire_put_varu32(buf, cap, pos, audit->graph_sheaf_h1_obstructed) ||
        !flow_wire_put_varu32(buf, cap, pos, audit->graph_bypass_mask) ||
        !flow_wire_put_varu64(buf, cap, pos, audit->base_loss_sum) ||
        !flow_wire_put_varu64(buf, cap, pos, audit->edge_exposure_sum) ||
        !flow_wire_put_varu64(buf, cap, pos, audit->worst_case_loss) ||
        !sound_wire__put_zigzag64(buf, cap, pos, audit->benefit_drift) ||
        !flow_wire_put_varu64(buf, cap, pos, audit->max_exit_seen) ||
        !flow_wire_put_varu64(buf, cap, pos, audit->repaired_worst_loss) ||
        !flow_wire_put_varu64(buf, cap, pos, audit->merkle)) {
        return false;
    }
    for (uint32_t i = 0U; i < audit->num_predatory_clauses; i++) {
        if (!flow_wire_put_varu32(buf, cap, pos, audit->predatory_clause_ids[i])) {
            return false;
        }
    }
    for (uint32_t i = 0U; i < num_clauses; i++) {
        if (!flow_wire_put_varu32(buf, cap, pos, audit->clause_viol_masks[i])) {
            return false;
        }
    }
    return true;
}

static inline bool sound_wire__get_contract_audit(const uint8_t *buf, size_t end, size_t *pos,
                                                  uint32_t num_clauses,
                                                  sound_contract_audit_t *audit) {
    if (audit == NULL || num_clauses > SOUND_CONTRACT_MAX_CLAUSES) {
        return false;
    }
    sound_zero_bytes(audit, sizeof(*audit));
    if (!flow_wire_get_varu32(buf, end, pos, &audit->certified) ||
        !flow_wire_get_varu32(buf, end, pos, &audit->viol_flags) ||
        !flow_wire_get_varu32(buf, end, pos, &audit->num_predatory_clauses) ||
        !flow_wire_get_varu32(buf, end, pos, &audit->repaired_certified) ||
        !flow_wire_get_varu32(buf, end, pos, &audit->num_malicious_edges) ||
        !flow_wire_get_varu32(buf, end, pos, &audit->graph_sheaf_h1_obstructed) ||
        !flow_wire_get_varu32(buf, end, pos, &audit->graph_bypass_mask) ||
        !flow_wire_get_varu64(buf, end, pos, &audit->base_loss_sum) ||
        !flow_wire_get_varu64(buf, end, pos, &audit->edge_exposure_sum) ||
        !flow_wire_get_varu64(buf, end, pos, &audit->worst_case_loss) ||
        !sound_wire__get_zigzag64(buf, end, pos, &audit->benefit_drift) ||
        !flow_wire_get_varu64(buf, end, pos, &audit->max_exit_seen) ||
        !flow_wire_get_varu64(buf, end, pos, &audit->repaired_worst_loss) ||
        !flow_wire_get_varu64(buf, end, pos, &audit->merkle)) {
        return false;
    }
    if (audit->num_predatory_clauses > SOUND_CONTRACT_MAX_CLAUSES) {
        return false;
    }
    for (uint32_t i = 0U; i < audit->num_predatory_clauses; i++) {
        if (!flow_wire_get_varu32(buf, end, pos, &audit->predatory_clause_ids[i])) {
            return false;
        }
    }
    for (uint32_t i = 0U; i < num_clauses; i++) {
        if (!flow_wire_get_varu32(buf, end, pos, &audit->clause_viol_masks[i])) {
            return false;
        }
    }
    return true;
}

static inline bool sound_cert_pack_contract_spec(const sound_contract_spec_t *spec, uint8_t *buf,
                                                 size_t cap, size_t *out_len) {
    if (spec == NULL || buf == NULL || out_len == NULL || cap < 16U) {
        return false;
    }
    size_t body_cap = cap - 8U;
    flow_ir_wire_write_le32(buf + 0U, SOUND_CERT_PACK_MAGIC);
    buf[4] = SOUND_CERT_PACK_VERSION;
    buf[5] = (uint8_t)SOUND_WIRE_KIND_CONTRACT_SPEC;
    buf[6] = UINT8_C(0);
    buf[7] = UINT8_C(0);
    size_t pos = 8U;
    if (!sound_wire__put_contract_spec(buf, body_cap, &pos, spec)) {
        return false;
    }
    *out_len = flow_ir_wire_seal_trailer(buf, pos);
    return true;
}

static inline bool sound_cert_unpack_contract_spec(const uint8_t *buf, size_t len,
                                                   sound_contract_spec_t *spec_out) {
    if (buf == NULL || spec_out == NULL || len < 16U) {
        return false;
    }
    if (flow_ir_wire_read_le32(buf + 0U) != SOUND_CERT_PACK_MAGIC ||
        buf[4] != SOUND_CERT_PACK_VERSION || buf[5] != (uint8_t)SOUND_WIRE_KIND_CONTRACT_SPEC ||
        buf[6] != UINT8_C(0) || buf[7] != UINT8_C(0)) {
        return false;
    }
    size_t body_len = len - 8U;
    if (flow_ir_wire_read_le64(buf + body_len) != flow_ir_wire_fnv1a64(buf, body_len)) {
        return false;
    }
    size_t pos = 8U;
    return sound_wire__get_contract_spec(buf, body_len, &pos, spec_out) && (pos == body_len);
}

static inline bool sound_cert_pack_contract_cert(const sound_contract_spec_t *spec,
                                                 const sound_contract_audit_t *audit,
                                                 const sound_cert_ws_t *ws, uint8_t *buf,
                                                 size_t cap, size_t *out_len) {
    if (spec == NULL || audit == NULL || buf == NULL || out_len == NULL || cap < 16U) {
        return false;
    }
    size_t body_cap = cap - 8U;
    flow_ir_wire_write_le32(buf + 0U, SOUND_CERT_PACK_MAGIC);
    buf[4] = SOUND_CERT_PACK_VERSION;
    buf[5] = (uint8_t)SOUND_WIRE_KIND_CONTRACT_CERT;
    buf[6] = UINT8_C(0);
    buf[7] = (ws != NULL && ws->ir_eval_ok) ? UINT8_C(1) : UINT8_C(0);
    size_t pos = 8U;
    uint64_t fbytes = (ws != NULL) ? (uint64_t)ws->wire_len : UINT64_C(0);
    if (!flow_wire_put_varu64(buf, body_cap, &pos, fbytes) ||
        !sound_wire__put_contract_spec(buf, body_cap, &pos, spec) ||
        !sound_wire__put_contract_audit(buf, body_cap, &pos, spec->num_clauses, audit)) {
        return false;
    }
    *out_len = flow_ir_wire_seal_trailer(buf, pos);
    return true;
}

static inline bool sound_cert_unpack_contract_cert(const uint8_t *buf, size_t len,
                                                   sound_contract_spec_t *spec_out,
                                                   sound_contract_audit_t *audit_out,
                                                   size_t *fir_wire_bytes_out,
                                                   bool *fir_eval_ok_out) {
    if (buf == NULL || spec_out == NULL || audit_out == NULL || len < 16U) {
        return false;
    }
    if (flow_ir_wire_read_le32(buf + 0U) != SOUND_CERT_PACK_MAGIC ||
        buf[4] != SOUND_CERT_PACK_VERSION || buf[5] != (uint8_t)SOUND_WIRE_KIND_CONTRACT_CERT ||
        buf[6] != UINT8_C(0) || (buf[7] & UINT8_C(0xFE)) != UINT8_C(0)) {
        return false;
    }
    size_t body_len = len - 8U;
    if (flow_ir_wire_read_le64(buf + body_len) != flow_ir_wire_fnv1a64(buf, body_len)) {
        return false;
    }
    size_t pos = 8U;
    uint64_t fbytes = UINT64_C(0);
    if (!flow_wire_get_varu64(buf, body_len, &pos, &fbytes) ||
        !sound_wire__get_contract_spec(buf, body_len, &pos, spec_out) ||
        !sound_wire__get_contract_audit(buf, body_len, &pos, spec_out->num_clauses, audit_out) ||
        pos != body_len) {
        return false;
    }
    if (fir_wire_bytes_out != NULL) {
        *fir_wire_bytes_out = (size_t)fbytes;
    }
    if (fir_eval_ok_out != NULL) {
        *fir_eval_ok_out = (buf[7] & UINT8_C(1)) != UINT8_C(0);
    }
    return true;
}

static inline bool sound_cert_pack_hunt_witness(sound_hunt_mode_t mode, const void *witness,
                                                const sound_cert_ws_t *ws, uint8_t *buf, size_t cap,
                                                size_t *out_len) {
    if (witness == NULL || buf == NULL || out_len == NULL || cap < 16U) {
        return false;
    }
    size_t body_cap = cap - 8U;
    flow_ir_wire_write_le32(buf + 0U, SOUND_CERT_PACK_MAGIC);
    buf[4] = SOUND_CERT_PACK_VERSION;
    buf[5] = (uint8_t)SOUND_WIRE_KIND_HUNT_WITNESS;
    buf[6] = (uint8_t)mode;
    buf[7] = (ws != NULL && ws->ir_eval_ok) ? UINT8_C(1) : UINT8_C(0);
    size_t pos = 8U;
    uint64_t fbytes = (ws != NULL) ? (uint64_t)ws->wire_len : UINT64_C(0);
    if (!flow_wire_put_varu64(buf, body_cap, &pos, fbytes)) {
        return false;
    }
    if (mode == SOUND_HUNT_MODE_PARADOX_MIN) {
        const sound_paradox_witness_t *w = (const sound_paradox_witness_t *)witness;
        if (!flow_wire_put_varu32(buf, body_cap, &pos, w->num_nodes) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->atk_to[0]) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->atk_to[1]) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->atk_to[2]) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->i_star) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->p_star) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->conflict_mask) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->is_odd_cycle) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->classical_models) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->seed) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->merkle)) {
            return false;
        }
    } else if (mode == SOUND_HUNT_MODE_SHEAF_H1) {
        const sound_sheaf_witness_t *w = (const sound_sheaf_witness_t *)witness;
        if (!flow_wire_put_u8(buf, body_cap, &pos, w->g01) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->g12) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->g20) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->holonomy_z4) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->phi[0]) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->phi[1]) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->phi[2]) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->sg01) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->sg12) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->sg20) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->gauge_holonomy_z4) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->is_coboundary_z4) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->skew_symmetric_ok) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->t01) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->t12) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->t20) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->xor_holonomy) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->seed) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->merkle)) {
            return false;
        }
    } else if (mode == SOUND_HUNT_MODE_CHISHOLM) {
        const sound_chisholm_witness_t *w = (const sound_chisholm_witness_t *)witness;
        if (!flow_wire_put_varu32(buf, body_cap, &pos, w->num_nodes) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->established_mask) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->defeated_mask) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->conflict_mask) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->penumbra_mask) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->statuses[0]) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->statuses[1]) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->statuses[2]) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->statuses[3]) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->hohfeld_ok) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->merkle)) {
            return false;
        }
    } else if (mode == SOUND_HUNT_MODE_NORM_TRAP) {
        const sound_norm_trap_witness_t *w = (const sound_norm_trap_witness_t *)witness;
        if (!flow_wire_put_varu64(buf, body_cap, &pos, w->init_pos) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->init_neg) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->power) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->immunity) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->final_pos) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->final_neg) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->step_slot[0]) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->step_slot[1]) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->step_slot[2]) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->step_slot[3]) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->step_fiber[0]) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->step_fiber[1]) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->step_fiber[2]) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->step_fiber[3]) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, (uint32_t)w->tie_weight) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->energy_keep_a) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->energy_keep_b) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->energy_both) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->energy_neither) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->kanlock_win_energy) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->kanlock_both_energy) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->freeze_ok) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->seed) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->merkle)) {
            return false;
        }
    } else if (mode == SOUND_HUNT_MODE_ASPECT_FIREWALL) {
        const sound_aspect_witness_t *w = (const sound_aspect_witness_t *)witness;
        if (!flow_wire_put_varu32(buf, body_cap, &pos, w->aspect_bijection_ok) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->telic_blocked_mask) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->j_start) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->j_end) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->i_start) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->i_end) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->state_gate_telic) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->activity_gate_telic) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->accomplishment_gate_telic) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->achievement_gate_telic) ||
            !flow_wire_put_varu32(buf, body_cap, &pos, w->declarative_atelic_fiber) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->seed) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->merkle)) {
            return false;
        }
    } else if (mode == SOUND_HUNT_MODE_PRESUP_FILTER) {
        const sound_presup_witness_t *w = (const sound_presup_witness_t *)witness;
        if (!flow_wire_put_u8(buf, body_cap, &pos, w->a_assert) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->a_presup) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->b_assert) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->b_presup) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->eval_fail_b) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->eval_neg_fail_b) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->plug_presup) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->plug_eval) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->cond_false_presup) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->cond_true_presup) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->force_hume_ok) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->force_query_ok) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->force_irrealis_ok) ||
            !flow_wire_put_u8(buf, body_cap, &pos, w->force_directive_ok) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->seed) ||
            !flow_wire_put_varu64(buf, body_cap, &pos, w->merkle)) {
            return false;
        }
    } else {
        return false;
    }
    *out_len = flow_ir_wire_seal_trailer(buf, pos);
    return true;
}

static inline bool sound_cert_unpack_hunt_witness(const uint8_t *buf, size_t len,
                                                  sound_hunt_mode_t *mode_out, void *witness_out,
                                                  size_t witness_cap, size_t *fir_wire_bytes_out,
                                                  bool *fir_eval_ok_out) {
    if (buf == NULL || witness_out == NULL || len < 16U) {
        return false;
    }
    if (flow_ir_wire_read_le32(buf + 0U) != SOUND_CERT_PACK_MAGIC ||
        buf[4] != SOUND_CERT_PACK_VERSION || buf[5] != (uint8_t)SOUND_WIRE_KIND_HUNT_WITNESS ||
        (buf[7] & UINT8_C(0xFE)) != UINT8_C(0)) {
        return false;
    }
    size_t body_len = len - 8U;
    if (flow_ir_wire_read_le64(buf + body_len) != flow_ir_wire_fnv1a64(buf, body_len)) {
        return false;
    }
    sound_hunt_mode_t mode = (sound_hunt_mode_t)buf[6];
    size_t pos = 8U;
    uint64_t fbytes = UINT64_C(0);
    if (!flow_wire_get_varu64(buf, body_len, &pos, &fbytes)) {
        return false;
    }
    if (mode == SOUND_HUNT_MODE_PARADOX_MIN) {
        if (witness_cap < sizeof(sound_paradox_witness_t)) {
            return false;
        }
        sound_paradox_witness_t *w = (sound_paradox_witness_t *)witness_out;
        sound_zero_bytes(w, sizeof(*w));
        if (!flow_wire_get_varu32(buf, body_len, &pos, &w->num_nodes) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->atk_to[0]) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->atk_to[1]) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->atk_to[2]) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->i_star) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->p_star) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->conflict_mask) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->is_odd_cycle) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->classical_models) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->seed) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->merkle)) {
            return false;
        }
    } else if (mode == SOUND_HUNT_MODE_SHEAF_H1) {
        if (witness_cap < sizeof(sound_sheaf_witness_t)) {
            return false;
        }
        sound_sheaf_witness_t *w = (sound_sheaf_witness_t *)witness_out;
        sound_zero_bytes(w, sizeof(*w));
        if (!flow_wire_get_u8(buf, body_len, &pos, &w->g01) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->g12) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->g20) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->holonomy_z4) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->phi[0]) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->phi[1]) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->phi[2]) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->sg01) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->sg12) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->sg20) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->gauge_holonomy_z4) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->is_coboundary_z4) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->skew_symmetric_ok) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->t01) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->t12) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->t20) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->xor_holonomy) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->seed) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->merkle)) {
            return false;
        }
    } else if (mode == SOUND_HUNT_MODE_CHISHOLM) {
        if (witness_cap < sizeof(sound_chisholm_witness_t)) {
            return false;
        }
        sound_chisholm_witness_t *w = (sound_chisholm_witness_t *)witness_out;
        sound_zero_bytes(w, sizeof(*w));
        if (!flow_wire_get_varu32(buf, body_len, &pos, &w->num_nodes) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->established_mask) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->defeated_mask) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->conflict_mask) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->penumbra_mask) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->statuses[0]) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->statuses[1]) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->statuses[2]) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->statuses[3]) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->hohfeld_ok) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->merkle)) {
            return false;
        }
    } else if (mode == SOUND_HUNT_MODE_NORM_TRAP) {
        if (witness_cap < sizeof(sound_norm_trap_witness_t)) {
            return false;
        }
        sound_norm_trap_witness_t *w = (sound_norm_trap_witness_t *)witness_out;
        sound_zero_bytes(w, sizeof(*w));
        uint32_t tw = 0U;
        if (!flow_wire_get_varu64(buf, body_len, &pos, &w->init_pos) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->init_neg) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->power) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->immunity) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->final_pos) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->final_neg) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->step_slot[0]) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->step_slot[1]) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->step_slot[2]) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->step_slot[3]) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->step_fiber[0]) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->step_fiber[1]) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->step_fiber[2]) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->step_fiber[3]) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &tw) || tw > UINT16_MAX ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->energy_keep_a) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->energy_keep_b) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->energy_both) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->energy_neither) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->kanlock_win_energy) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->kanlock_both_energy) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->freeze_ok) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->seed) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->merkle)) {
            return false;
        }
        w->tie_weight = (uint16_t)tw;
    } else if (mode == SOUND_HUNT_MODE_ASPECT_FIREWALL) {
        if (witness_cap < sizeof(sound_aspect_witness_t)) {
            return false;
        }
        sound_aspect_witness_t *w = (sound_aspect_witness_t *)witness_out;
        sound_zero_bytes(w, sizeof(*w));
        if (!flow_wire_get_varu32(buf, body_len, &pos, &w->aspect_bijection_ok) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->telic_blocked_mask) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->j_start) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->j_end) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->i_start) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->i_end) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->state_gate_telic) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->activity_gate_telic) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->accomplishment_gate_telic) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->achievement_gate_telic) ||
            !flow_wire_get_varu32(buf, body_len, &pos, &w->declarative_atelic_fiber) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->seed) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->merkle)) {
            return false;
        }
    } else if (mode == SOUND_HUNT_MODE_PRESUP_FILTER) {
        if (witness_cap < sizeof(sound_presup_witness_t)) {
            return false;
        }
        sound_presup_witness_t *w = (sound_presup_witness_t *)witness_out;
        sound_zero_bytes(w, sizeof(*w));
        if (!flow_wire_get_u8(buf, body_len, &pos, &w->a_assert) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->a_presup) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->b_assert) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->b_presup) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->eval_fail_b) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->eval_neg_fail_b) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->plug_presup) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->plug_eval) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->cond_false_presup) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->cond_true_presup) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->force_hume_ok) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->force_query_ok) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->force_irrealis_ok) ||
            !flow_wire_get_u8(buf, body_len, &pos, &w->force_directive_ok) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->seed) ||
            !flow_wire_get_varu64(buf, body_len, &pos, &w->merkle)) {
            return false;
        }
    } else {
        return false;
    }
    if (pos != body_len) {
        return false;
    }
    if (mode_out != NULL) {
        *mode_out = mode;
    }
    if (fir_wire_bytes_out != NULL) {
        *fir_wire_bytes_out = (size_t)fbytes;
    }
    if (fir_eval_ok_out != NULL) {
        *fir_eval_ok_out = (buf[7] & UINT8_C(1)) != UINT8_C(0);
    }
    return true;
}

static inline bool sound_cert_verify_bytes(const uint8_t *buf, size_t len, uint8_t *kind_out,
                                           uint8_t *submode_out) {
    if (buf == NULL || len < 16U) {
        return false;
    }
    if (flow_ir_wire_read_le32(buf + 0U) != SOUND_CERT_PACK_MAGIC ||
        buf[4] != SOUND_CERT_PACK_VERSION) {
        return false;
    }
    uint8_t kind = buf[5];
    uint8_t sub = buf[6];
    if (kind == (uint8_t)SOUND_WIRE_KIND_CONTRACT_SPEC) {
        static sound_contract_spec_t spec;
        if (!sound_cert_unpack_contract_spec(buf, len, &spec)) {
            return false;
        }
    } else if (kind == (uint8_t)SOUND_WIRE_KIND_CONTRACT_CERT) {
        static sound_contract_spec_t spec;
        static sound_contract_audit_t audit;
        if (!sound_cert_unpack_contract_cert(buf, len, &spec, &audit, NULL, NULL)) {
            return false;
        }
    } else if (kind == (uint8_t)SOUND_WIRE_KIND_HUNT_WITNESS) {
        uint8_t wbuf[256];
        sound_hunt_mode_t m = SOUND_HUNT_MODE_PARADOX_MIN;
        if (!sound_cert_unpack_hunt_witness(buf, len, &m, wbuf, sizeof(wbuf), NULL, NULL)) {
            return false;
        }
    } else {
        return false;
    }
    if (kind_out != NULL) {
        *kind_out = kind;
    }
    if (submode_out != NULL) {
        *submode_out = sub;
    }
    return true;
}

static inline bool sound_cert_unpack_to_json_fp(const uint8_t *buf, size_t len, FILE *fp) {
    if (buf == NULL || fp == NULL || len < 16U) {
        return false;
    }
    uint8_t kind = buf[5];
    if (kind == (uint8_t)SOUND_WIRE_KIND_CONTRACT_CERT) {
        static sound_contract_spec_t spec;
        static sound_contract_audit_t audit;
        size_t fbytes = 0U;
        bool feval = false;
        if (!sound_cert_unpack_contract_cert(buf, len, &spec, &audit, &fbytes, &feval)) {
            return false;
        }
        return sound_cert_write_contract_json_fp(&spec, &audit, fbytes, feval, fp);
    }
    if (kind == (uint8_t)SOUND_WIRE_KIND_HUNT_WITNESS) {
        sound_hunt_mode_t mode = (sound_hunt_mode_t)buf[6];
        size_t fbytes = 0U;
        bool feval = false;
        if (mode == SOUND_HUNT_MODE_PARADOX_MIN) {
            sound_paradox_witness_t w;
            return sound_cert_unpack_hunt_witness(buf, len, &mode, &w, sizeof(w), &fbytes,
                                                  &feval) &&
                   sound_cert_write_paradox_json_fp(&w, fbytes, feval, fp);
        }
        if (mode == SOUND_HUNT_MODE_SHEAF_H1) {
            sound_sheaf_witness_t w;
            return sound_cert_unpack_hunt_witness(buf, len, &mode, &w, sizeof(w), &fbytes,
                                                  &feval) &&
                   sound_cert_write_sheaf_json_fp(&w, fbytes, feval, fp);
        }
        if (mode == SOUND_HUNT_MODE_CHISHOLM) {
            sound_chisholm_witness_t w;
            return sound_cert_unpack_hunt_witness(buf, len, &mode, &w, sizeof(w), &fbytes,
                                                  &feval) &&
                   sound_cert_write_chisholm_json_fp(&w, fbytes, feval, fp);
        }
        if (mode == SOUND_HUNT_MODE_NORM_TRAP) {
            sound_norm_trap_witness_t w;
            return sound_cert_unpack_hunt_witness(buf, len, &mode, &w, sizeof(w), &fbytes,
                                                  &feval) &&
                   sound_cert_write_norm_trap_json_fp(&w, fbytes, feval, fp);
        }
        if (mode == SOUND_HUNT_MODE_ASPECT_FIREWALL) {
            sound_aspect_witness_t w;
            return sound_cert_unpack_hunt_witness(buf, len, &mode, &w, sizeof(w), &fbytes,
                                                  &feval) &&
                   sound_cert_write_aspect_json_fp(&w, fbytes, feval, fp);
        }
        if (mode == SOUND_HUNT_MODE_PRESUP_FILTER) {
            sound_presup_witness_t w;
            return sound_cert_unpack_hunt_witness(buf, len, &mode, &w, sizeof(w), &fbytes,
                                                  &feval) &&
                   sound_cert_write_presup_json_fp(&w, fbytes, feval, fp);
        }
    }
    return false;
}

static inline bool sound_cert_emit_contract_cert_file(const sound_contract_spec_t *spec,
                                                      const sound_contract_audit_t *audit,
                                                      const sound_cert_ws_t *ws, const char *path) {
    static uint8_t buf[SOUND_CERT_MAX_BYTES];
    size_t len = 0U;
    if (path == NULL || !sound_cert_pack_contract_cert(spec, audit, ws, buf, sizeof(buf), &len)) {
        return false;
    }
    return flow_ir_write_bytes_file(path, buf, len);
}

static inline bool sound_cert_emit_hunt_cert_file(sound_hunt_mode_t mode, const void *witness,
                                                  const sound_cert_ws_t *ws, const char *path) {
    static uint8_t buf[SOUND_CERT_MAX_BYTES];
    size_t len = 0U;
    if (path == NULL || !sound_cert_pack_hunt_witness(mode, witness, ws, buf, sizeof(buf), &len)) {
        return false;
    }
    return flow_ir_write_bytes_file(path, buf, len);
}

#endif /* SOUND_CERT_H */
