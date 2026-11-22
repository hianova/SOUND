/-
  Sound.Linguistics — Vendler Aktionsart Lattice, 4-Valued Presupposition Projection & Force
  ============================================================================================
  Elevates natural-language representation primitives into verified algebraic laws:
  - Vendler aspectual feature lattice (±Dynamic × ±Telic) & Homogeneity/Quantization No-Go
  - Imperfective-to-Constitutive semantic firewall
  - 4-valued presupposition projection algebra (Hole / Plug / Filter) & Strawson Gap No-Go
  - Illocutionary Force Endofunctors over `(Epistemic, Normative, Modal)` product space.
  Zero external imports beyond `Std.Tactic.BVDecide` & `Sound.Bilattice`. Zero `sorryAx`.
-/
import Std.Tactic.BVDecide
import Sound.Bilattice

namespace Sound.Linguistics

open Sound.Bilattice

/-! ## Section 1: Vendler Aspectual Lattice & Homogeneity vs. Quantization No-Go -/

inductive Aspect where
  | state
  | activity
  | accomplishment
  | achievement
  deriving Repr, DecidableEq, Inhabited

def Aspect.isDynamic : Aspect → Bool
  | .state          => false
  | .activity       => true
  | .accomplishment => true
  | .achievement    => false

def Aspect.isTelic : Aspect → Bool
  | .state          => false
  | .activity       => false
  | .accomplishment => true
  | .achievement    => true

def Aspect.ofFeatures (dyn telic : Bool) : Aspect :=
  match dyn, telic with
  | false, false => .state
  | true,  false => .activity
  | true,  true  => .accomplishment
  | false, true  => .achievement

/-- 1. `Aspect.ofFeatures ∘ (isDynamic, isTelic)` is the exact left inverse across all 4 classes. -/
theorem aspect_of_features_roundtrip (a : Aspect) :
    Aspect.ofFeatures a.isDynamic a.isTelic = a := by
  cases a <;> rfl

/-- 2. The feature map `(isDynamic, isTelic)` is a right inverse, giving `Aspect ≃ Bool × Bool`. -/
theorem features_of_aspect_roundtrip (dyn telic : Bool) :
    (Aspect.ofFeatures dyn telic).isDynamic = dyn ∧
    (Aspect.ofFeatures dyn telic).isTelic = telic := by
  cases dyn <;> cases telic <;> decide

/-- 3. Telicity strictly partitions the Vendler lattice into atelic vs. constitutive telic classes. -/
theorem aspect_telic_partition (a : Aspect) :
    a.isTelic = true ↔ (a = .accomplishment ∨ a = .achievement) := by
  cases a <;> decide

structure Interval where
  s   : Nat
  e   : Nat
  hlt : s < e
  deriving Repr, DecidableEq

def ProperSub (i j : Interval) : Prop :=
  j.s ≤ i.s ∧ i.e ≤ j.e ∧ (j.s < i.s ∨ i.e < j.e)

def DownwardHomogeneous (P : Interval → Prop) : Prop :=
  ∀ i j, ProperSub i j → P j → P i

def Quantized (P : Interval → Prop) : Prop :=
  ∀ i j, ProperSub i j → P j → ¬ P i

/-- 4. Universal Homogeneity vs. Quantization No-Go (Imperfective Paradox Separation):
    No interval predicate holding on a non-atomic interval (`len ≥ 2`) can be simultaneously
    downward-homogeneous (atelic process/state) and quantized (telic constitutive act). -/
theorem homogeneous_quantized_nogo (P : Interval → Prop) (j : Interval)
    (hLen : j.s + 2 ≤ j.e) (hPj : P j) :
    ¬ (DownwardHomogeneous P ∧ Quantized P) := by
  intro ⟨hHom, hQuant⟩
  let i : Interval := ⟨j.s, j.s + 1, by omega⟩
  have hiE : i.e = j.s + 1 := rfl
  have hiS : i.s = j.s := rfl
  have hSub : ProperSub i j := ⟨hiS ▸ Nat.le_refl _, by omega, Or.inr (by omega)⟩
  have hPi : P i := hHom i j hSub hPj
  have hNotPi : ¬ P i := hQuant i j hSub hPj
  exact hNotPi hPi

def constitutiveAspectGate (premiseAspect : Aspect) (requiresTelic : Bool) : Bool :=
  (!requiresTelic) || premiseAspect.isTelic

/-- 5. Imperfective-to-Constitutive No-Go:
    States and atelic activities are unconditionally blocked by any telic constitutive gate
    (e.g., "is attempting delivery" cannot constitute "completed legal transfer"). -/
theorem imperfective_activity_blocked_constitutive_nogo (a : Aspect)
    (hAtelic : a = .state ∨ a = .activity) :
    constitutiveAspectGate a true = false := by
  rcases hAtelic with rfl | rfl <;> rfl

/-- 6. Telic accomplishments and achievements cleanly satisfy the constitutive gate. -/
theorem telic_aspect_passes_constitutive (a : Aspect)
    (hTelic : a = .accomplishment ∨ a = .achievement) :
    constitutiveAspectGate a true = true := by
  rcases hTelic with rfl | rfl <;> rfl

/-! ## Section 2: 4-Valued Presupposition Projection Algebra (Hole / Plug / Filter) -/

structure PresupExpr where
  assertVal : B4
  presupVal : B4
  deriving Repr, DecidableEq

def evalPresup (e : PresupExpr) : B4 :=
  if e.presupVal == 2#2 then e.assertVal else 0#2

def projNeg (e : PresupExpr) : PresupExpr :=
  ⟨b4Neg e.assertVal, e.presupVal⟩

def projIrrealis (_ : PresupExpr) : PresupExpr :=
  ⟨0#2, 2#2⟩

def projCond (a b : PresupExpr) : PresupExpr :=
  ⟨ b4OrT (b4Neg a.assertVal) b.assertVal
  , if a.assertVal == 2#2 then b4AndT a.presupVal b.presupVal else a.presupVal ⟩

/-- 7. Classical Bivalent Truth-Gap No-Go:
    Under Boolean bivalence, excluded middle holds so failed presuppositions force either a
    sentence or its negation to be true, collapsing Strawson truth-value gaps. -/
theorem classical_bivalent_truth_gap_nogo (b : Bool) :
    b = true ∨ (!b) = true := by
  cases b <;> decide

/-- 8. Strawson Presupposition Failure Quarantine:
    When `e.presupVal ≠ 2#2`, BOTH `e` and its negation `projNeg e` drop to `bot = 0#2`. -/
theorem presup_failure_blocks_both_assert_and_neg (e : PresupExpr)
    (hFail : e.presupVal ≠ 2#2) :
    evalPresup e = 0#2 ∧ evalPresup (projNeg e) = 0#2 := by
  have hNeq : (e.presupVal == 2#2) = false := beq_false_of_ne hFail
  simp [evalPresup, projNeg, hNeq]

/-- 9. Failed presuppositions strictly prevent acceptance (`2#2`) for both assertion and negation. -/
theorem presup_failure_never_accept (e : PresupExpr)
    (hFail : e.presupVal ≠ 2#2) :
    evalPresup e ≠ 2#2 ∧ evalPresup (projNeg e) ≠ 2#2 := by
  obtain ⟨h1, h2⟩ := presup_failure_blocks_both_assert_and_neg e hFail
  rw [h1, h2]
  decide

/-- 10. Sentential negation is an exact projection Hole and an involution on `PresupExpr`. -/
theorem projNeg_is_hole_bv (e : PresupExpr) :
    (projNeg e).presupVal = e.presupVal ∧ projNeg (projNeg e) = e := by
  cases e with
  | mk a p =>
      simp only [projNeg, b4_neg_involutive, and_self]

/-- 11. Irrealis modality is an exact Plug: it discharges internal presuppositions and zeroes
    real-world assertoric commitment (`evalPresup = 0#2`). -/
theorem projIrrealis_is_plug_bv (e : PresupExpr) :
    (projIrrealis e).presupVal = 2#2 ∧ evalPresup (projIrrealis e) = 0#2 := by
  simp [projIrrealis, evalPresup]

/-- 12. Karttunen/Heim Conditional Filter (False Antecedent):
    When a conditional's antecedent is asserted false (`1#2`) and grounded, the consequent's
    presupposition is filtered out and never projects to the matrix. -/
theorem projCond_filters_false_antecedent_bv (a b : PresupExpr)
    (haPres : a.presupVal = 2#2) (haFalse : a.assertVal = 1#2) :
    (projCond a b).presupVal = 2#2 := by
  simp [projCond, haPres, haFalse]

/-- 13. Conditional Projection (True Antecedent):
    When a grounded antecedent evaluates true (`2#2`), it transparently passes the consequent's
    presupposition. -/
theorem projCond_projects_true_antecedent_bv (a b : PresupExpr)
    (haPres : a.presupVal = 2#2) (haTrue : a.assertVal = 2#2) :
    (projCond a b).presupVal = b.presupVal := by
  have hAnd : b4AndT 2#2 b.presupVal = b.presupVal := by
    unfold b4AndT
    bv_decide
  simp [projCond, haPres, haTrue, hAnd]

/-- 14. Antecedent failure monotonicity: a failing antecedent presupposition ALWAYS poisons the
    conditional, even when `a.assertVal = 2#2`. -/
theorem projCond_preserves_antecedent_failure_bv (a b : PresupExpr)
    (hFail : a.presupVal ≠ 2#2) :
    (projCond a b).presupVal ≠ 2#2 := by
  unfold projCond b4AndT
  bv_decide

/-! ## Section 3: Illocutionary Force Endofunctors over `(Epistemic, Normative, Modal)` -/

inductive Force where
  | assertion
  | query
  | directive
  | declarative
  | irrealis
  deriving Repr, DecidableEq, Inhabited

structure WorldState where
  epistemic : BitVec 64
  dutyPos   : BitVec 64
  dutyNeg   : BitVec 64
  power     : BitVec 64
  immunity  : BitVec 64
  modal     : BitVec 64
  deriving Repr, DecidableEq

structure ForceAct where
  slotBit : BitVec 64
  aspect  : Aspect
  gp      : Bool
  rp      : Bool
  gn      : Bool
  rn      : Bool
  deriving Repr, DecidableEq

def applyForce (f : Force) (st : WorldState) (act : ForceAct) : WorldState × SoundFiber :=
  match f with
  | .assertion =>
      ({ st with epistemic := st.epistemic ||| act.slotBit }, .committed)
  | .query =>
      (st, .committed)
  | .irrealis =>
      ({ st with modal := st.modal ||| act.slotBit }, .fallbackIrrealis)
  | .directive =>
      let r := potestativeFiberStepBV64 st.dutyPos st.dutyNeg st.power st.immunity
        act.slotBit act.gp act.rp act.gn act.rn
      ({ st with dutyPos := r.1, dutyNeg := r.2.1 }, r.2.2)
  | .declarative =>
      if !act.aspect.isTelic then
        (st, .fallbackPolicyViolation)
      else
        let r := potestativeFiberStepBV64 st.dutyPos st.dutyNeg st.power st.immunity
          act.slotBit act.gp act.rp act.gn act.rn
        let epNext := if r.2.2 == .committed then st.epistemic ||| act.slotBit else st.epistemic
        ({ st with epistemic := epNext, dutyPos := r.1, dutyNeg := r.2.1 }, r.2.2)

/-- 15. Illocutionary Hume Firewall (`assertion`):
    Assertions strictly conserve `dutyPos`, `dutyNeg`, `power`, `immunity`, and `modal`. -/
theorem force_assertion_conserves_normative_bv64 (st : WorldState) (act : ForceAct) :
    let st' := (applyForce .assertion st act).1
    st'.dutyPos = st.dutyPos ∧ st'.dutyNeg = st.dutyNeg ∧
    st'.power = st.power ∧ st'.immunity = st.immunity ∧ st'.modal = st.modal := by
  simp [applyForce]

/-- 16. Zero-Mutation Observation (`query`):
    Queries are an exact identity morphism on `WorldState`. -/
theorem force_query_is_identity_bv64 (st : WorldState) (act : ForceAct) :
    applyForce .query st act = (st, .committed) :=
  rfl

/-- 17. Modal Quarantine (`irrealis`):
    Irrealis acts isolate strictly inside `.modal` with zero leakage to real-world epistemic or
    normative state. -/
theorem force_irrealis_zero_real_leakage_bv64 (st : WorldState) (act : ForceAct) :
    let res := applyForce .irrealis st act
    res.1.epistemic = st.epistemic ∧ res.1.dutyPos = st.dutyPos ∧
    res.1.dutyNeg = st.dutyNeg ∧ res.1.power = st.power ∧
    res.1.immunity = st.immunity ∧ res.2 = .fallbackIrrealis := by
  simp [applyForce]

/-- 18. Normative Directive Separation:
    Directives strictly conserve `.epistemic` and `.modal`, preserve coherence, and preserve
    all `KanLock` immune duty bits. -/
theorem force_directive_blocks_epistemic_and_respects_kanlock_bv64
    (st : WorldState) (act : ForceAct) (hCoh : (st.dutyPos &&& st.dutyNeg) = 0#64) :
    let st' := (applyForce .directive st act).1
    st'.epistemic = st.epistemic ∧ st'.modal = st.modal ∧
    (st'.dutyPos &&& st'.dutyNeg) = 0#64 ∧
    (st'.dutyPos &&& st.immunity) = (st.dutyPos &&& st.immunity) ∧
    (st'.dutyNeg &&& st.immunity) = (st.dutyNeg &&& st.immunity) := by
  have hPot := potestative_commit_preserves_coherence_and_kanlock_bv64
    st.dutyPos st.dutyNeg st.power st.immunity act.slotBit act.gp act.rp act.gn act.rn hCoh
  exact ⟨rfl, rfl, hPot.1, hPot.2.1, hPot.2.2⟩

/-- 19. Vendler Aspect × Constitutive Declarative No-Go:
    Any `.declarative` attempted with an atelic aspect (`state`/`activity`) acts as an exact
    identity retraction landing on `.fallbackPolicyViolation`. -/
theorem force_declarative_blocks_atelic_aspect_bv64
    (st : WorldState) (act : ForceAct) (hAtelic : act.aspect.isTelic = false) :
    applyForce .declarative st act = (st, .fallbackPolicyViolation) := by
  simp [applyForce, hAtelic]

/-- 20. Telic Declarative Coherence & KanLock Conservation:
    Regardless of outcome, `.declarative` always preserves `(dutyPos &&& dutyNeg) = 0` and
    every `Immunity`-locked duty bit. -/
theorem force_declarative_preserves_coherence_and_kanlock_bv64
    (st : WorldState) (act : ForceAct) (hCoh : (st.dutyPos &&& st.dutyNeg) = 0#64) :
    let st' := (applyForce .declarative st act).1
    (st'.dutyPos &&& st'.dutyNeg) = 0#64 ∧
    (st'.dutyPos &&& st.immunity) = (st.dutyPos &&& st.immunity) ∧
    (st'.dutyNeg &&& st.immunity) = (st.dutyNeg &&& st.immunity) := by
  dsimp only [applyForce]
  by_cases hTel : act.aspect.isTelic = true
  · have hNeg : (!act.aspect.isTelic) = false := by simp [hTel]
    have hPot := potestative_commit_preserves_coherence_and_kanlock_bv64
      st.dutyPos st.dutyNeg st.power st.immunity act.slotBit act.gp act.rp act.gn act.rn hCoh
    simpa [hNeg] using hPot
  · have hFalse : act.aspect.isTelic = false := Bool.eq_false_iff.mpr hTel
    simp [hFalse, hCoh]

/-- 21. Any force transition ending on a non-`.committed` fiber leaves `(dutyPos, dutyNeg)`
    strictly unchanged (total HoTT retraction property across all 5 forces). -/
theorem force_noncommitted_never_mutates_duties_bv64
    (f : Force) (st : WorldState) (act : ForceAct)
    (hNon : (applyForce f st act).2 ≠ .committed) :
    (applyForce f st act).1.dutyPos = st.dutyPos ∧
    (applyForce f st act).1.dutyNeg = st.dutyNeg := by
  cases f with
  | assertion => simp [applyForce] at hNon
  | query => simp [applyForce] at hNon
  | irrealis => simp [applyForce]
  | directive =>
      exact potestative_fallback_retraction_is_identity_bv64
        st.dutyPos st.dutyNeg st.power st.immunity act.slotBit act.gp act.rp act.gn act.rn hNon
  | declarative =>
      dsimp only [applyForce] at hNon ⊢
      by_cases hTel : act.aspect.isTelic = true
      · have hNeg : (!act.aspect.isTelic) = false := by simp [hTel]
        simp only [hNeg, Bool.false_eq_true, ↓reduceIte] at hNon ⊢
        exact potestative_fallback_retraction_is_identity_bv64
          st.dutyPos st.dutyNeg st.power st.immunity act.slotBit act.gp act.rp act.gn act.rn hNon
      · have hFalse : act.aspect.isTelic = false := Bool.eq_false_iff.mpr hTel
        simp [hFalse]

end Sound.Linguistics
