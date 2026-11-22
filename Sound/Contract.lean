/-
  Sound.Contract — Informed-Consent, Benefit-Drift & Asymmetric Contract Verification (Pillar 5)
  ==============================================================================================
  Models asymmetric contracts between an adversarial institution (arbitrary legal strategy
  across unilateral modification powers, ambiguous obligations, hidden fine print, default
  cascades, and exit penalties) and a consumer summary commitment (`SummarySpec`).
  Proves fail-closed soundness of `.certified` verdicts across ALL adversarial actions (`∀`),
  strict veto under any immunity breach, ambiguity (`⊥`/`⊤`), presupposition smuggling,
  positive benefit drift, or unconscionable penalty asymmetry, and minimal repair soundness.
  Zero external imports beyond `Sound.Bilattice` & `Sound.Hohfeld`. Zero `sorryAx`.
-/
import Sound.Bilattice
import Sound.Hohfeld

namespace Sound.Contract

inductive ClauseKind where
  | baseFee
  | unilateralPower
  | ambiguousObligation
  | presupSmuggle
  | chisholmDefault
  | reciprocity
  deriving Repr, DecidableEq, Inhabited

structure SummarySpec where
  maxLossCap    : Int
  maxExitFee    : Int
  protectedMask : BitVec 64
  maxAsymNum    : Nat
  maxAsymDen    : Nat
  deriving Repr, DecidableEq, Inhabited

structure ContractClause where
  clauseId    : Nat
  kind        : ClauseKind
  disclosed   : Bool
  targetMask  : BitVec 64
  b4Truth     : BitVec 2
  baseLoss    : Nat
  extraDrift  : Nat
  exitFee     : Nat
  consPenalty : Nat
  instPenalty : Nat
  sheafCycle  : Bool := false
  deriving Repr, DecidableEq, Inhabited

structure AdversarialAction where
  useUnilateralPower  : Bool
  resolveAmbiguityBad : Bool
  invokeUndisclosed   : Bool
  triggerDefaultChain : Bool
  deriving Repr, DecidableEq, Inhabited

inductive ContractVerdict where
  | certified
  | vetoed
  deriving Repr, DecidableEq, Inhabited

def clauseWorstLoss (c : ContractClause) : Int :=
  (c.baseLoss : Int) + (c.extraDrift : Int)

def clauseActualLoss (act : AdversarialAction) (c : ContractClause) : Int :=
  (c.baseLoss : Int) +
    if (act.useUnilateralPower && c.kind == .unilateralPower) ||
       (act.resolveAmbiguityBad && (c.b4Truth == 0#2 || c.b4Truth == 3#2 || c.kind == .ambiguousObligation)) ||
       (act.invokeUndisclosed && !c.disclosed) ||
       (act.triggerDefaultChain && c.kind == .chisholmDefault)
    then (c.extraDrift : Int) else 0

def actualConsumerLoss (act : AdversarialAction) : List ContractClause → Int
  | [] => 0
  | c :: cs => clauseActualLoss act c + actualConsumerLoss act cs

def worstCaseLoss : List ContractClause → Int
  | [] => 0
  | c :: cs => clauseWorstLoss c + worstCaseLoss cs

def benefitDrift (spec : SummarySpec) (clauses : List ContractClause) : Int :=
  worstCaseLoss clauses - spec.maxLossCap

def clauseBreaksImmunity (spec : SummarySpec) (c : ContractClause) : Bool :=
  (c.kind == .unilateralPower) && ((c.targetMask &&& spec.protectedMask) != 0#64)

def clauseUninformedAmbiguity (c : ContractClause) : Bool :=
  (c.b4Truth == 0#2) || (c.b4Truth == 3#2) || (c.kind == .ambiguousObligation)

def clausePresupSmuggled (c : ContractClause) : Bool :=
  (!c.disclosed) && ((0 < c.baseLoss) || (0 < c.extraDrift) || (0 < c.exitFee))

def clauseUnconscionable (spec : SummarySpec) (c : ContractClause) : Bool :=
  decide (c.instPenalty * spec.maxAsymNum < c.consPenalty * spec.maxAsymDen)

def clauseSheafOrChisholm (spec : SummarySpec) (c : ContractClause) : Bool :=
  c.sheafCycle || (c.kind == .chisholmDefault && decide (0 < c.extraDrift)) ||
    decide (spec.maxExitFee < (c.exitFee : Int))

def clausePredatory (spec : SummarySpec) (c : ContractClause) : Bool :=
  clauseBreaksImmunity spec c ||
  clauseUninformedAmbiguity c ||
  clausePresupSmuggled c ||
  clauseUnconscionable spec c ||
  clauseSheafOrChisholm spec c ||
  decide (0 < c.extraDrift)

def auditContract (spec : SummarySpec) (clauses : List ContractClause) : ContractVerdict :=
  if clauses.any (clausePredatory spec) ||
     decide (spec.maxLossCap < worstCaseLoss clauses) ||
     (spec.maxAsymDen == 0) then
    .vetoed
  else
    .certified

def repairContract (spec : SummarySpec) (clauses : List ContractClause) : List ContractClause :=
  clauses.filter (fun c => !clausePredatory spec c)

private theorem clauseActualLoss_le_worst (act : AdversarialAction) (c : ContractClause) :
    clauseActualLoss act c ≤ clauseWorstLoss c := by
  unfold clauseActualLoss clauseWorstLoss
  split <;> omega

private theorem clauseWorstLoss_nonneg (c : ContractClause) :
    0 ≤ clauseWorstLoss c := by
  unfold clauseWorstLoss
  omega

/-- 1. Universal Adversarial Bound: under every legal institution strategy `act`,
       realized consumer loss never exceeds computed `worstCaseLoss`. -/
theorem actualLoss_le_worstCaseLoss (act : AdversarialAction) (clauses : List ContractClause) :
    actualConsumerLoss act clauses ≤ worstCaseLoss clauses := by
  induction clauses with
  | nil => simp [actualConsumerLoss, worstCaseLoss]
  | cons c cs ih =>
    simp only [actualConsumerLoss, worstCaseLoss]
    have hc := clauseActualLoss_le_worst act c
    omega

/-- 2. Worst-case consumer loss is always non-negative. -/
theorem worstCaseLoss_nonneg (clauses : List ContractClause) :
    0 ≤ worstCaseLoss clauses := by
  induction clauses with
  | nil => simp [worstCaseLoss]
  | cons c cs ih =>
    simp only [worstCaseLoss]
    have hc := clauseWorstLoss_nonneg c
    omega

/-- 3. Worst-case consumer loss is additive over contract clause composition. -/
theorem worstCaseLoss_append (c1 c2 : List ContractClause) :
    worstCaseLoss (c1 ++ c2) = worstCaseLoss c1 + worstCaseLoss c2 := by
  induction c1 with
  | nil => simp [worstCaseLoss]
  | cons c cs ih =>
    simp only [List.cons_append, worstCaseLoss, ih]
    omega

/-- 4. Appending hidden/rider clauses monotonically increases worst-case consumer loss. -/
theorem worstCaseLoss_monotone_append (c1 c2 : List ContractClause) :
    worstCaseLoss c1 ≤ worstCaseLoss (c1 ++ c2) := by
  rw [worstCaseLoss_append]
  have h2 := worstCaseLoss_nonneg c2
  omega

private theorem certified_extract (spec : SummarySpec) (clauses : List ContractClause)
    (hCert : auditContract spec clauses = .certified) :
    clauses.any (clausePredatory spec) = false ∧ worstCaseLoss clauses ≤ spec.maxLossCap := by
  unfold auditContract at hCert
  by_cases hAny : clauses.any (clausePredatory spec) = true
  · simp [hAny] at hCert
  · have hAnyF : clauses.any (clausePredatory spec) = false := by
      cases hb : clauses.any (clausePredatory spec)
      · rfl
      · exact False.elim (hAny hb)
    by_cases hLoss : spec.maxLossCap < worstCaseLoss clauses
    · simp [hAnyF, hLoss] at hCert
    · exact ⟨hAnyF, by omega⟩

private theorem no_predatory_clause_of_certified (spec : SummarySpec) (clauses : List ContractClause)
    (hCert : auditContract spec clauses = .certified) :
    ∀ c ∈ clauses, clausePredatory spec c = false := by
  have ⟨hAny, _⟩ := certified_extract spec clauses hCert
  intro c hc
  cases hb : clausePredatory spec c
  · rfl
  · have hAnyTrue : clauses.any (clausePredatory spec) = true :=
      List.any_eq_true.mpr ⟨c, hc, hb⟩
    rw [hAnyTrue] at hAny
    cases hAny

/-- 5. Informed-Consent Soundness: any certified contract guarantees consumer loss
       stays within the disclosed `SummarySpec.maxLossCap` under ALL adversarial actions. -/
theorem certified_bounds_adversarial_loss (spec : SummarySpec) (clauses : List ContractClause)
    (hCert : auditContract spec clauses = .certified) :
    ∀ act : AdversarialAction, actualConsumerLoss act clauses ≤ spec.maxLossCap := by
  have ⟨_, hWorst⟩ := certified_extract spec clauses hCert
  intro act
  exact Int.le_trans (actualLoss_le_worstCaseLoss act clauses) hWorst

/-- 6. Certified contracts incur non-positive Benefit Drift (`worstCaseLoss - maxLossCap ≤ 0`). -/
theorem certified_zero_benefit_drift (spec : SummarySpec) (clauses : List ContractClause)
    (hCert : auditContract spec clauses = .certified) :
    benefitDrift spec clauses ≤ 0 := by
  have ⟨_, hWorst⟩ := certified_extract spec clauses hCert
  unfold benefitDrift
  omega

/-- 7. Certified contracts never allow unilateral power clauses to touch protected immunities. -/
theorem certified_preserves_consumer_immunity (spec : SummarySpec) (clauses : List ContractClause)
    (hCert : auditContract spec clauses = .certified) :
    ∀ c ∈ clauses, clauseBreaksImmunity spec c = false := by
  intro c hc
  have hp := no_predatory_clause_of_certified spec clauses hCert c hc
  simp only [clausePredatory, Bool.or_eq_false_iff] at hp
  exact hp.1.1.1.1.1

/-- 8. Fail-Closed Ambiguity: certified contracts contain zero undefined (`0#2`) or
       conflicting (`3#2`) consumer obligations. -/
theorem certified_zero_uninformed_ambiguity (spec : SummarySpec) (clauses : List ContractClause)
    (hCert : auditContract spec clauses = .certified) :
    ∀ c ∈ clauses, clauseUninformedAmbiguity c = false := by
  intro c hc
  have hp := no_predatory_clause_of_certified spec clauses hCert c hc
  simp only [clausePredatory, Bool.or_eq_false_iff] at hp
  exact hp.1.1.1.1.2

/-- 9. Summary Fidelity: certified contracts contain zero undisclosed loss-bearing fine print. -/
theorem certified_zero_presup_smuggling (spec : SummarySpec) (clauses : List ContractClause)
    (hCert : auditContract spec clauses = .certified) :
    ∀ c ∈ clauses, clausePresupSmuggled c = false := by
  intro c hc
  have hp := no_predatory_clause_of_certified spec clauses hCert c hc
  simp only [clausePredatory, Bool.or_eq_false_iff] at hp
  exact hp.1.1.1.2

/-- 10. Reciprocity Symmetry Bound: certified contracts enforce `consPenalty * den ≤ instPenalty * num`. -/
theorem certified_reciprocity_bounded (spec : SummarySpec) (clauses : List ContractClause)
    (hCert : auditContract spec clauses = .certified) :
    ∀ c ∈ clauses, c.consPenalty * spec.maxAsymDen ≤ c.instPenalty * spec.maxAsymNum := by
  intro c hc
  have hp := no_predatory_clause_of_certified spec clauses hCert c hc
  simp only [clausePredatory, Bool.or_eq_false_iff] at hp
  have hUnc : clauseUnconscionable spec c = false := hp.1.1.2
  simp only [clauseUnconscionable, decide_eq_false_iff_not, Nat.not_lt] at hUnc
  exact hUnc

private theorem veto_of_predatory_clause (spec : SummarySpec) (clauses : List ContractClause)
    (c : ContractClause) (hc : c ∈ clauses) (hp : clausePredatory spec c = true) :
    auditContract spec clauses = .vetoed := by
  have hAny : clauses.any (clausePredatory spec) = true := List.any_eq_true.mpr ⟨c, hc, hp⟩
  simp [auditContract, hAny]

/-- 11. Any clause piercing consumer `protectedMask` via unilateral power forces `.vetoed`. -/
theorem immunity_breach_forces_veto (spec : SummarySpec) (clauses : List ContractClause)
    (c : ContractClause) (hc : c ∈ clauses) (hImm : clauseBreaksImmunity spec c = true) :
    auditContract spec clauses = .vetoed :=
  veto_of_predatory_clause spec clauses c hc (by simp [clausePredatory, hImm])

/-- 12. Any ambiguous obligation (`⊥` / `⊤`) forces fail-closed `.vetoed`. -/
theorem uninformed_ambiguity_forces_veto (spec : SummarySpec) (clauses : List ContractClause)
    (c : ContractClause) (hc : c ∈ clauses)
    (hAmb : clauseUninformedAmbiguity c = true) :
    auditContract spec clauses = .vetoed :=
  veto_of_predatory_clause spec clauses c hc (by simp [clausePredatory, hAmb])

/-- 13. Any undisclosed cost/drift clause smuggled past the summary forces `.vetoed`. -/
theorem presup_smuggle_forces_veto (spec : SummarySpec) (clauses : List ContractClause)
    (c : ContractClause) (hc : c ∈ clauses) (hSmug : clausePresupSmuggled c = true) :
    auditContract spec clauses = .vetoed :=
  veto_of_predatory_clause spec clauses c hc (by simp [clausePredatory, hSmug])

/-- 14. Strictly positive Benefit Drift (`0 < benefitDrift`) unconditionally forces `.vetoed`. -/
theorem positive_drift_forces_veto (spec : SummarySpec) (clauses : List ContractClause)
    (hDrift : 0 < benefitDrift spec clauses) :
    auditContract spec clauses = .vetoed := by
  unfold benefitDrift at hDrift
  have hLt : spec.maxLossCap < worstCaseLoss clauses := by omega
  simp [auditContract, hLt]

/-- 15. Any unconscionable asymmetric penalty ratio forces `.vetoed`. -/
theorem unconscionable_asym_forces_veto (spec : SummarySpec) (clauses : List ContractClause)
    (c : ContractClause) (hc : c ∈ clauses) (hAsym : clauseUnconscionable spec c = true) :
    auditContract spec clauses = .vetoed :=
  veto_of_predatory_clause spec clauses c hc (by simp [clausePredatory, hAsym])

/-- 16. Minimal Repair Soundness: `repairContract` eliminates all predatory clauses, and when
        remaining base fees fit `maxLossCap` (with `0 < maxAsymDen`), restores `.certified`. -/
theorem repair_eliminates_predatory_clauses (spec : SummarySpec) (clauses : List ContractClause) :
    (∀ c ∈ repairContract spec clauses, clausePredatory spec c = false) ∧
      (worstCaseLoss (repairContract spec clauses) ≤ spec.maxLossCap →
        0 < spec.maxAsymDen →
        auditContract spec (repairContract spec clauses) = .certified) := by
  have hClean : ∀ c ∈ repairContract spec clauses, clausePredatory spec c = false := by
    intro c hc
    simp only [repairContract, List.mem_filter, Bool.not_eq_true'] at hc
    exact hc.2
  refine ⟨hClean, ?_⟩
  intro hCap hDen
  have hAny : (repairContract spec clauses).any (clausePredatory spec) = false := by
    cases hb : (repairContract spec clauses).any (clausePredatory spec)
    · rfl
    · obtain ⟨c, hcMem, hcPred⟩ := List.any_eq_true.mp hb
      have hF := hClean c hcMem
      rw [hcPred] at hF
      cases hF
  have hDenNe : (spec.maxAsymDen == 0) = false := beq_false_of_ne (Nat.ne_of_gt hDen)
  unfold auditContract
  simp [hAny, Int.not_lt.mpr hCap, hDenNe]

end Sound.Contract
