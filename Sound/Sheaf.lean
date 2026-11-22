/-
  Sound.Sheaf — Discrete Cech Sheaf Cohomology H¹(U, G) & Global Gluing Obstructions
  =====================================================================================
  Formalizes 1-cochains, gauge transformations, coboundary vanishing, closed-loop holonomy,
  tree/chain triviality (H¹ = 0), explicit 3-chart normative Mobius obstructions (H¹ ≠ 0),
  and 64-bit Arena boundary twist gluing theorems (`bv_decide`).
  Zero external imports beyond `Std.Tactic.BVDecide`. Zero `sorryAx`.
-/
import Std.Tactic.BVDecide

namespace Sound.Sheaf

/-! ## Section 1: Z₄ (`BitVec 2`) 1-Cochains, Gauge Invariance & Holonomy No-Go -/

def IsCoboundaryZ4 {m : Nat} (g : Fin m → Fin m → BitVec 2) (adj : Fin m → Fin m → Bool) : Prop :=
  ∃ f : Fin m → BitVec 2, ∀ i j, adj i j = true → g i j = f j - f i

def walkHolonomyZ4 {m : Nat} (g : Fin m → Fin m → BitVec 2) : List (Fin m) → BitVec 2
  | []           => 0#2
  | [_]          => 0#2
  | a :: b :: ws => g a b + walkHolonomyZ4 g (b :: ws)

def AllAdj {m : Nat} (adj : Fin m → Fin m → Bool) : List (Fin m) → Prop
  | []           => True
  | [_]          => True
  | a :: b :: ws => adj a b = true ∧ AllAdj adj (b :: ws)

def gaugeShiftZ4 {m : Nat} (g : Fin m → Fin m → BitVec 2) (φ : Fin m → BitVec 2)
    (i j : Fin m) : BitVec 2 :=
  g i j + φ j - φ i

private theorem bv2_sub_cancel_z4 (a b c : BitVec 2) : (b - a) + (c - b) = c - a := by
  bv_decide

private theorem bv2_sub_self_z4 (a : BitVec 2) : (0#2 : BitVec 2) = a - a := by
  bv_decide

/-- 1. Any `Z₄` coboundary telescopes along an arbitrary path to the endpoint potential difference. -/
theorem coboundary_walk_telescopes_z4 {m : Nat} (g : Fin m → Fin m → BitVec 2)
    (adj : Fin m → Fin m → Bool) (f : Fin m → BitVec 2)
    (hCob : ∀ i j, adj i j = true → g i j = f j - f i)
    (a : Fin m) (ws : List (Fin m)) (hAdj : AllAdj adj (a :: ws)) :
    walkHolonomyZ4 g (a :: ws) = f ((a :: ws).getLast (by simp)) - f a := by
  induction ws generalizing a with
  | nil =>
      simp [walkHolonomyZ4]
  | cons b rest ih =>
      rcases hAdj with ⟨hStep, hRest⟩
      have hEdge := hCob a b hStep
      have hTail := ih b hRest
      simp only [walkHolonomyZ4]
      rw [hEdge, hTail]
      have hLast : (a :: b :: rest).getLast (by simp) = (b :: rest).getLast (by simp) := by
        simp [List.getLast_cons]
      rw [hLast]
      exact bv2_sub_cancel_z4 (f a) (f b) (f ((b :: rest).getLast (by simp)))

/-- 2. Every `Z₄` coboundary has vanishing holonomy around every closed walk. -/
theorem coboundary_closed_loop_vanishes_z4 {m : Nat} (g : Fin m → Fin m → BitVec 2)
    (adj : Fin m → Fin m → Bool) (a : Fin m) (ws : List (Fin m))
    (hCob : IsCoboundaryZ4 g adj) (hAdj : AllAdj adj (a :: ws))
    (hClosed : (a :: ws).getLast (by simp) = a) :
    walkHolonomyZ4 g (a :: ws) = 0#2 := by
  rcases hCob with ⟨f, hf⟩
  rw [coboundary_walk_telescopes_z4 g adj f hf a ws hAdj, hClosed]
  exact (bv2_sub_self_z4 (f a)).symm

/-- 3. Universal `Z₄` Cohomological Obstruction No-Go (`H¹ ≠ 0`):
    Any closed walk with non-zero holonomy unconditionally forbids a global consistent section. -/
theorem nonzero_holonomy_no_global_section_z4_nogo {m : Nat} (g : Fin m → Fin m → BitVec 2)
    (adj : Fin m → Fin m → Bool) (a : Fin m) (ws : List (Fin m))
    (hAdj : AllAdj adj (a :: ws)) (hClosed : (a :: ws).getLast (by simp) = a)
    (hHol : walkHolonomyZ4 g (a :: ws) ≠ 0#2) :
    ¬ IsCoboundaryZ4 g adj := by
  intro hCob
  exact hHol (coboundary_closed_loop_vanishes_z4 g adj a ws hCob hAdj hClosed)

/-- 4. Gauge transformation shifts arbitrary open-walk holonomy by the endpoint gauge difference. -/
theorem gauge_walk_relation_z4 {m : Nat} (g : Fin m → Fin m → BitVec 2) (φ : Fin m → BitVec 2)
    (a : Fin m) (ws : List (Fin m)) :
    walkHolonomyZ4 (gaugeShiftZ4 g φ) (a :: ws) =
      walkHolonomyZ4 g (a :: ws) + φ ((a :: ws).getLast (by simp)) - φ a := by
  induction ws generalizing a with
  | nil =>
      simp [walkHolonomyZ4]
  | cons b rest ih =>
      simp only [walkHolonomyZ4, gaugeShiftZ4]
      rw [ih b]
      have hLast : (a :: b :: rest).getLast (by simp) = (b :: rest).getLast (by simp) := by
        simp [List.getLast_cons]
      rw [hLast]
      have hAssoc :
          (g a b + φ b - φ a) + (walkHolonomyZ4 g (b :: rest) + φ ((b :: rest).getLast (by simp)) - φ b) =
          (g a b + walkHolonomyZ4 g (b :: rest)) + φ ((b :: rest).getLast (by simp)) - φ a := by
        bv_decide
      exact hAssoc

/-- 5. Cohomology Class Invariance: local `Z₄` gauge shifts preserve every closed-loop holonomy. -/
theorem gauge_preserves_closed_holonomy_z4 {m : Nat} (g : Fin m → Fin m → BitVec 2)
    (φ : Fin m → BitVec 2) (a : Fin m) (ws : List (Fin m))
    (hClosed : (a :: ws).getLast (by simp) = a) :
    walkHolonomyZ4 (gaugeShiftZ4 g φ) (a :: ws) = walkHolonomyZ4 g (a :: ws) := by
  rw [gauge_walk_relation_z4, hClosed]
  bv_decide

/-- 6. Local `Z₄` gauge shifts preserve the property of being a global coboundary (`H¹ = 0`). -/
theorem gauge_preserves_coboundary_z4 {m : Nat} (g : Fin m → Fin m → BitVec 2)
    (adj : Fin m → Fin m → Bool) (φ : Fin m → BitVec 2)
    (hCob : IsCoboundaryZ4 g adj) :
    IsCoboundaryZ4 (gaugeShiftZ4 g φ) adj := by
  rcases hCob with ⟨f, hf⟩
  refine ⟨fun i => f i + φ i, fun i j hij => ?_⟩
  simp only [gaugeShiftZ4, hf i j hij]
  bv_decide

/-! ## Section 2: V₄ (`BitVec 2` XOR) 1-Cochains & Normative Polarity Cohomology -/

def IsCoboundaryV4 {m : Nat} (g : Fin m → Fin m → BitVec 2) (adj : Fin m → Fin m → Bool) : Prop :=
  ∃ f : Fin m → BitVec 2, ∀ i j, adj i j = true → g i j = f i ^^^ f j

def walkHolonomyV4 {m : Nat} (g : Fin m → Fin m → BitVec 2) : List (Fin m) → BitVec 2
  | []           => 0#2
  | [_]          => 0#2
  | a :: b :: ws => g a b ^^^ walkHolonomyV4 g (b :: ws)

def gaugeShiftV4 {m : Nat} (g : Fin m → Fin m → BitVec 2) (φ : Fin m → BitVec 2)
    (i j : Fin m) : BitVec 2 :=
  φ i ^^^ g i j ^^^ φ j

/-- 7. Any `V₄` coboundary telescopes along an arbitrary walk to endpoint XOR difference. -/
theorem coboundary_walk_telescopes_v4 {m : Nat} (g : Fin m → Fin m → BitVec 2)
    (adj : Fin m → Fin m → Bool) (f : Fin m → BitVec 2)
    (hCob : ∀ i j, adj i j = true → g i j = f i ^^^ f j)
    (a : Fin m) (ws : List (Fin m)) (hAdj : AllAdj adj (a :: ws)) :
    walkHolonomyV4 g (a :: ws) = f a ^^^ f ((a :: ws).getLast (by simp)) := by
  induction ws generalizing a with
  | nil =>
      simp only [walkHolonomyV4, List.getLast_singleton]
      bv_decide
  | cons b rest ih =>
      rcases hAdj with ⟨hStep, hRest⟩
      have hEdge := hCob a b hStep
      have hTail := ih b hRest
      simp only [walkHolonomyV4]
      rw [hEdge, hTail]
      have hLast : (a :: b :: rest).getLast (by simp) = (b :: rest).getLast (by simp) := by
        simp [List.getLast_cons]
      rw [hLast]
      bv_decide

/-- 8. Every `V₄` coboundary has vanishing holonomy around every closed walk. -/
theorem coboundary_closed_loop_vanishes_v4 {m : Nat} (g : Fin m → Fin m → BitVec 2)
    (adj : Fin m → Fin m → Bool) (a : Fin m) (ws : List (Fin m))
    (hCob : IsCoboundaryV4 g adj) (hAdj : AllAdj adj (a :: ws))
    (hClosed : (a :: ws).getLast (by simp) = a) :
    walkHolonomyV4 g (a :: ws) = 0#2 := by
  rcases hCob with ⟨f, hf⟩
  rw [coboundary_walk_telescopes_v4 g adj f hf a ws hAdj, hClosed]
  bv_decide

/-- 9. Universal `V₄` Cohomological Obstruction No-Go (`H¹ ≠ 0`). -/
theorem nonzero_holonomy_no_global_section_v4_nogo {m : Nat} (g : Fin m → Fin m → BitVec 2)
    (adj : Fin m → Fin m → Bool) (a : Fin m) (ws : List (Fin m))
    (hAdj : AllAdj adj (a :: ws)) (hClosed : (a :: ws).getLast (by simp) = a)
    (hHol : walkHolonomyV4 g (a :: ws) ≠ 0#2) :
    ¬ IsCoboundaryV4 g adj := by
  intro hCob
  exact hHol (coboundary_closed_loop_vanishes_v4 g adj a ws hCob hAdj hClosed)

/-- 10. Local `V₄` gauge shifts preserve every closed-loop holonomy. -/
theorem gauge_preserves_closed_holonomy_v4 {m : Nat} (g : Fin m → Fin m → BitVec 2)
    (φ : Fin m → BitVec 2) (a : Fin m) (ws : List (Fin m))
    (hClosed : (a :: ws).getLast (by simp) = a) :
    walkHolonomyV4 (gaugeShiftV4 g φ) (a :: ws) = walkHolonomyV4 g (a :: ws) := by
  have hRel :
      ∀ (x : Fin m) (xs : List (Fin m)),
        walkHolonomyV4 (gaugeShiftV4 g φ) (x :: xs) =
          φ x ^^^ walkHolonomyV4 g (x :: xs) ^^^ φ ((x :: xs).getLast (by simp)) := by
    intro x xs
    induction xs generalizing x with
    | nil =>
        simp only [walkHolonomyV4, List.getLast_singleton]
        bv_decide
    | cons y rest ih =>
        simp only [walkHolonomyV4, gaugeShiftV4]
        rw [ih y]
        have hLast : (x :: y :: rest).getLast (by simp) = (y :: rest).getLast (by simp) := by
          simp [List.getLast_cons]
        rw [hLast]
        bv_decide
  rw [hRel a ws, hClosed]
  bv_decide

/-- 11. Local `V₄` gauge shifts preserve coboundary triviality. -/
theorem gauge_preserves_coboundary_v4 {m : Nat} (g : Fin m → Fin m → BitVec 2)
    (adj : Fin m → Fin m → Bool) (φ : Fin m → BitVec 2)
    (hCob : IsCoboundaryV4 g adj) :
    IsCoboundaryV4 (gaugeShiftV4 g φ) adj := by
  rcases hCob with ⟨f, hf⟩
  refine ⟨fun i => f i ^^^ φ i, fun i j hij => ?_⟩
  simp only [gaugeShiftV4, hf i j hij]
  bv_decide

/-! ## Section 3: Chain / Tree Triviality & 3-Chart Normative Mobius Obstruction -/

def chainAdj (m : Nat) (i j : Fin m) : Bool :=
  (i.val + 1 == j.val)

def prefixSectionZ4 (m : Nat) (g : Fin m → Fin m → BitVec 2) : Nat → BitVec 2
  | 0     => 0#2
  | k + 1 =>
      if h : k + 1 < m then
        prefixSectionZ4 m g k + g ⟨k, by omega⟩ ⟨k + 1, h⟩
      else
        0#2

/-- 12. Acyclic Tree/Chain Cohomological Triviality (`H¹(P_m, Z₄) = 0`):
    Every directed chain cover ALWAYS admits a constructive global gluing section. -/
theorem chain_cover_always_coboundary_z4 (m : Nat) (g : Fin m → Fin m → BitVec 2) :
    IsCoboundaryZ4 g (chainAdj m) := by
  refine ⟨fun i => prefixSectionZ4 m g i.val, fun i j hij => ?_⟩
  simp only [chainAdj, beq_iff_eq] at hij
  have hjLt : i.val + 1 < m := hij ▸ j.isLt
  have hiEq : (⟨i.val, i.isLt⟩ : Fin m) = i := Fin.ext rfl
  have hjEq : (⟨i.val + 1, hjLt⟩ : Fin m) = j := Fin.ext hij
  have hStep :
      prefixSectionZ4 m g j.val =
        prefixSectionZ4 m g i.val + g i j := by
    rw [← hij]
    simp only [prefixSectionZ4, hjLt, ↓reduceDIte, hiEq, hjEq]
  dsimp only
  rw [hStep]
  bv_decide

def triangleAdj (i j : Fin 3) : Bool :=
  i != j

def mobius3Z4 (i j : Fin 3) : BitVec 2 :=
  match i.val, j.val with
  | 0, 1 => 1#2
  | 1, 2 => 1#2
  | 2, 0 => 1#2
  | 1, 0 => 3#2
  | 2, 1 => 3#2
  | 0, 2 => 3#2
  | _, _ => 0#2

/-- 13. The 3-chart Mobius cocycle is strictly skew-symmetric on all pairwise overlaps. -/
theorem mobius3_pairwise_skew_symmetric :
    ∀ i j : Fin 3, mobius3Z4 j i = -mobius3Z4 i j := by
  decide

/-- 14. The 3-chart Mobius bundle has non-vanishing closed-loop holonomy `3#2 ≠ 0#2`. -/
theorem mobius3_holonomy_nonzero :
    walkHolonomyZ4 mobius3Z4 [0, 1, 2, 0] = 3#2 ∧
    walkHolonomyZ4 mobius3Z4 [0, 1, 2, 0] ≠ 0#2 := by
  decide

/-- 15. Minimal 3-Chart Normative Mobius No-Go (`H¹ ≠ 0`):
    All pairwise overlaps agree with skew-symmetry yet NO global gluing section exists. -/
theorem mobius3_h1_obstruction_nogo :
    ¬ IsCoboundaryZ4 mobius3Z4 triangleAdj := by
  refine nonzero_holonomy_no_global_section_z4_nogo mobius3Z4 triangleAdj 0 [1, 2, 0]
    ⟨by decide, by decide, by decide, trivial⟩ rfl (by decide)

/-! ## Section 4: 64-Bit SoundArena Chart Gluing Cohomology Bridge (`bv_decide`) -/

/-- 16. If three 64-bit pairwise boundary transitions `(t01, t12, t20)` have non-zero
    XOR holonomy `(t01 ^^^ t12 ^^^ t20) ≠ 0`, NO global assignment `(s0, s1, s2)` exists. -/
theorem arena64_triangle_twist_no_global_section_bv64
    (t01 t12 t20 : BitVec 64)
    (hTwist : (t01 ^^^ t12 ^^^ t20) ≠ 0#64) :
    ¬ ∃ (s0 s1 s2 : BitVec 64),
        (s0 ^^^ s1) = t01 ∧ (s1 ^^^ s2) = t12 ∧ (s2 ^^^ s0) = t20 := by
  rintro ⟨s0, s1, s2, h01, h12, h20⟩
  apply hTwist
  bv_decide

/-- 17. Exact Cohomological Vanishing Criterion for 64-Bit Chart Gluing:
    Global section existence is iff zero XOR holonomy `(t01 ^^^ t12 ^^^ t20) = 0`. -/
theorem arena64_triangle_zero_holonomy_iff_gluable_bv64
    (t01 t12 t20 : BitVec 64) :
    (t01 ^^^ t12 ^^^ t20) = 0#64 ↔
      ∃ (s0 s1 s2 : BitVec 64),
        (s0 ^^^ s1) = t01 ∧ (s1 ^^^ s2) = t12 ∧ (s2 ^^^ s0) = t20 := by
  constructor
  · intro hZero
    refine ⟨0#64, t01, t20, ?_, ?_, ?_⟩ <;> bv_decide
  · rintro ⟨s0, s1, s2, h01, h12, h20⟩
    bv_decide

/-- 18. Constructive trivial 64-bit coboundary gluing identity: coherent zero-twist transitions
    preserve every downstream slot bit identically around the triangle. -/
theorem arena64_zero_twist_glue_identity_bv64 (s0 s1 s2 : BitVec 64) :
    ((s0 ^^^ s1) ^^^ (s1 ^^^ s2) ^^^ (s2 ^^^ s0)) = 0#64 := by
  bv_decide

end Sound.Sheaf
