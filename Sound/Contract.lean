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
import Sound.Sheaf
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

/-! ## Compositional Loophole Graph, Discrete Čech H¹ Cycles & Coverage Gate -/

inductive EdgeKind where
  | override
  | redirect
  | powerDelegate
  | defaultTrigger
  deriving Repr, DecidableEq, Inhabited

structure ClauseEdge where
  src           : Nat
  dst           : Nat
  edgeKind      : EdgeKind
  cocycleShift  : BitVec 2 := 0#2
  extraExposure : Nat := 0
  deriving Repr, DecidableEq, Inhabited

def edgesExposure (edges : List ClauseEdge) : Int :=
  (edges.map (fun e => (e.extraExposure : Int))).sum

def compositionalWorstLoss (clauses : List ContractClause) (edges : List ClauseEdge) : Int :=
  worstCaseLoss clauses + edgesExposure edges

def edgeReachSeed (clauses : List ContractClause) (edges : List ClauseEdge) : List Bool :=
  (List.range clauses.length).map fun i =>
    let isUni := match clauses[i]? with
      | some c => c.kind == .unilateralPower
      | none => false
    let inEdge := edges.any fun e =>
      e.dst == i && (e.edgeKind == .override || e.edgeKind == .powerDelegate)
    isUni || inEdge

def edgeReachStep (edges : List ClauseEdge) (reach : List Bool) : List Bool :=
  (List.range reach.length).map fun j =>
    (reach[j]?.getD false) ||
      edges.any fun e =>
        (reach[e.src]?.getD false) && e.dst == j &&
          (e.edgeKind == .override || e.edgeKind == .powerDelegate || e.edgeKind == .redirect)

def edgeReachFixed (clauses : List ContractClause) (edges : List ClauseEdge) : List Bool :=
  (List.range clauses.length).foldl (fun r _ => edgeReachStep edges r) (edgeReachSeed clauses edges)

def edgeHohfeldBypass (spec : SummarySpec) (clauses : List ContractClause)
    (edges : List ClauseEdge) : Bool :=
  let reach := edgeReachFixed clauses edges
  (List.range clauses.length).any fun i =>
    (reach[i]?.getD false) &&
      match clauses[i]? with
      | some c => (c.targetMask &&& spec.protectedMask) != 0#64
      | none => false

def setListIdx {α : Type} : List α → Nat → α → List α
  | [], _, _ => []
  | _ :: xs, 0, v => v :: xs
  | x :: xs, k + 1, v => x :: setListIdx xs k v

def propagatePotStep (n : Nat) (edges : List ClauseEdge)
    (pots : List (Option (BitVec 2))) : List (Option (BitVec 2)) :=
  edges.foldl (fun acc e =>
    if e.src < n && e.dst < n then
      match acc[e.src]?, acc[e.dst]? with
      | some (some ps), some none => setListIdx acc e.dst (some (ps + e.cocycleShift))
      | some none, some (some pd) => setListIdx acc e.src (some (pd - e.cocycleShift))
      | _, _ => acc
    else acc) pots

def solveSheafPots (n : Nat) (edges : List ClauseEdge) : List (Option (BitVec 2)) :=
  let init := List.replicate n (none : Option (BitVec 2))
  (List.range n).foldl (fun pots root =>
    match pots[root]? with
    | some none =>
      let seeded := setListIdx pots root (some 0#2)
      (List.range n).foldl (fun p _ => propagatePotStep n edges p) seeded
    | _ => pots) init

def edgeSheafObstruction (n : Nat) (edges : List ClauseEdge) : Bool :=
  let pots := solveSheafPots n edges
  edges.any fun e =>
    if e.src < n && e.dst < n then
      match pots[e.src]?, pots[e.dst]? with
      | some (some ps), some (some pd) => (pd - ps) != e.cocycleShift
      | _, _ => true
    else false

def edgeChisholmCascade (edges : List ClauseEdge) : Bool :=
  edges.any fun e => e.edgeKind == .defaultTrigger && decide (0 < e.extraExposure)

def auditContractGraph (spec : SummarySpec) (clauses : List ContractClause)
    (edges : List ClauseEdge) (coverageGaps : Nat := 0) : ContractVerdict :=
  if auditContract spec clauses == .vetoed ||
     edgeHohfeldBypass spec clauses edges ||
     edgeSheafObstruction clauses.length edges ||
     edgeChisholmCascade edges ||
     decide (spec.maxLossCap < compositionalWorstLoss clauses edges) ||
     decide (0 < coverageGaps) then
    .vetoed
  else
    .certified

def edgeBenignForRepair (spec : SummarySpec) (clauses : List ContractClause) (e : ClauseEdge) : Bool :=
  let dstOk := match clauses[e.dst]? with
    | some c => (c.targetMask &&& spec.protectedMask) == 0#64
    | none => true
  (e.edgeKind == .redirect) && (e.cocycleShift == 0#2) && (e.extraExposure == 0) && dstOk

def repairContractGraph (spec : SummarySpec) (clauses : List ContractClause)
    (edges : List ClauseEdge) : List ContractClause × List ClauseEdge :=
  let cs' := repairContract spec clauses
  (cs', edges.filter (edgeBenignForRepair spec cs'))

def walkShiftZ4 : List ClauseEdge → BitVec 2
  | [] => 0#2
  | e :: es => e.cocycleShift + walkShiftZ4 es

def ValidClosedEdgeWalk (start cur : Nat) : List ClauseEdge → Prop
  | [] => start = cur
  | e :: es => e.src = cur ∧ ValidClosedEdgeWalk start e.dst es

private theorem bv2_sub_add_cancel (a b c : BitVec 2) : (b - a) + (c - b) = c - a := by
  decide +revert

private theorem bv2_sub_self_zero (a : BitVec 2) : a - a = 0#2 := by
  decide +revert

private theorem closed_walk_shift_zero (φ : Nat → BitVec 2) (edges : List ClauseEdge)
    (hCoh : ∀ e ∈ edges, φ e.dst - φ e.src = e.cocycleShift) :
    ∀ (s c : Nat) (w : List ClauseEdge),
      (∀ e ∈ w, e ∈ edges) → ValidClosedEdgeWalk s c w →
      walkShiftZ4 w = φ s - φ c := by
  intro s c w
  induction w generalizing c with
  | nil =>
    intro _ hW
    simp only [ValidClosedEdgeWalk] at hW
    subst hW
    simp only [walkShiftZ4, bv2_sub_self_zero]
  | cons e es ih =>
    intro hSub hW
    rcases hW with ⟨hSrc, hRest⟩
    have heMem : e ∈ edges := hSub e (by simp)
    have hE : φ e.dst - φ e.src = e.cocycleShift := hCoh e heMem
    have hTail : walkShiftZ4 es = φ s - φ e.dst :=
      ih e.dst (fun x hx => hSub x (by simp [hx])) hRest
    simp only [walkShiftZ4, ← hE, hSrc, hTail]
    exact bv2_sub_add_cancel (φ c) (φ e.dst) (φ s)

private theorem setListIdx_length {α : Type} (xs : List α) (k : Nat) (v : α) :
    (setListIdx xs k v).length = xs.length := by
  induction xs generalizing k with
  | nil => rfl
  | cons x tl ih =>
    cases k with
    | zero => simp [setListIdx]
    | succ k' => simp [setListIdx, ih]

private theorem setListIdx_get {α : Type} (xs : List α) (k j : Nat) (v : α) (hj : j < xs.length) :
    (setListIdx xs k v)[j]? = if j == k then some v else xs[j]? := by
  induction xs generalizing k j with
  | nil => simp at hj
  | cons x tl ih =>
    cases k with
    | zero =>
      cases j with
      | zero => simp [setListIdx]
      | succ j' => simp [setListIdx]
    | succ k' =>
      cases j with
      | zero => simp [setListIdx]
      | succ j' =>
        simp only [List.length_cons] at hj
        simp only [setListIdx, List.getElem?_cons_succ]
        rw [ih k' j' (by omega)]
        rfl

private theorem edgeReachSeed_length (clauses : List ContractClause) (edges : List ClauseEdge) :
    (edgeReachSeed clauses edges).length = clauses.length := by
  simp [edgeReachSeed]

private theorem edgeReachStep_length (edges : List ClauseEdge) (reach : List Bool) :
    (edgeReachStep edges reach).length = reach.length := by
  simp [edgeReachStep]

private def ReachProtectedClean (spec : SummarySpec) (clauses : List ContractClause)
    (reach : List Bool) : Prop :=
  ∀ i < reach.length, reach[i]? = some true →
    match clauses[i]? with
    | some c => (c.targetMask &&& spec.protectedMask) = 0#64
    | none => True

private theorem reachSeed_clean_of_repair (spec : SummarySpec) (cs : List ContractClause)
    (es : List ClauseEdge)
    (hCs : ∀ c ∈ cs, clausePredatory spec c = false)
    (hNoPow : ∀ e ∈ es, e.edgeKind ≠ .override ∧ e.edgeKind ≠ .powerDelegate) :
    ReachProtectedClean spec cs (edgeReachSeed cs es) := by
  intro i hi hT
  have hiLen : i < cs.length := by
    rw [edgeReachSeed_length] at hi
    exact hi
  have hInEdge : es.any (fun e => e.dst == i && (e.edgeKind == .override || e.edgeKind == .powerDelegate)) = false := by
    cases hb : es.any (fun e => e.dst == i && (e.edgeKind == .override || e.edgeKind == .powerDelegate))
    · rfl
    · obtain ⟨e, heMem, heB⟩ := List.any_eq_true.mp hb
      simp only [Bool.and_eq_true, Bool.or_eq_true, beq_iff_eq] at heB
      have hNP := hNoPow e heMem
      rcases heB.2 with h1 | h2
      · exact False.elim (hNP.1 h1)
      · exact False.elim (hNP.2 h2)
  simp only [edgeReachSeed, List.getElem?_map, List.getElem?_range hiLen, Option.map_some,
    Option.some.injEq, hInEdge, Bool.or_false] at hT
  cases hC : cs[i]? with
  | none => trivial
  | some c =>
    have hcMem : c ∈ cs := List.mem_of_getElem? hC
    have hcF := hCs c hcMem
    simp only [clausePredatory, Bool.or_eq_false_iff, clauseBreaksImmunity] at hcF
    have hBI := hcF.1.1.1.1.1
    simp only [hC] at hT
    simp only [hT, Bool.true_and, bne_eq_false_iff_eq] at hBI
    exact hBI

private theorem reachStep_clean_of_repair (spec : SummarySpec) (cs : List ContractClause)
    (es : List ClauseEdge) (reach : List Bool)
    (hEs : ∀ e ∈ es, edgeBenignForRepair spec cs e = true)
    (hPre : ReachProtectedClean spec cs reach) :
    ReachProtectedClean spec cs (edgeReachStep es reach) := by
  intro j hj hT
  have hjLen : j < reach.length := by
    rw [edgeReachStep_length] at hj
    exact hj
  simp only [edgeReachStep, List.getElem?_map, List.getElem?_range hjLen, Option.map_some,
    Option.some.injEq, Bool.or_eq_true] at hT
  rcases hT with hPrev | hViaEdge
  · have hRj : reach[j]? = some true := by
      cases hGet : reach[j]? with
      | none => simp [hGet] at hPrev
      | some b =>
        simp only [hGet, Option.getD_some] at hPrev
        simp [hPrev]
    exact hPre j hjLen hRj
  · obtain ⟨e, heMem, heB⟩ := List.any_eq_true.mp hViaEdge
    simp only [Bool.and_eq_true, beq_iff_eq] at heB
    have hBen := hEs e heMem
    simp only [edgeBenignForRepair, Bool.and_eq_true] at hBen
    rw [← heB.1.2]
    cases hC : cs[e.dst]? with
    | none => trivial
    | some c =>
      have hbR := hBen.2
      simp only [hC, beq_iff_eq] at hbR
      exact hbR

private def PotsZeroOrNone (pots : List (Option (BitVec 2))) : Prop :=
  ∀ x ∈ pots, x = none ∨ x = some 0#2

private theorem setListIdx_preserves_potsZero {pots : List (Option (BitVec 2))}
    (hP : PotsZeroOrNone pots) (k : Nat) :
    PotsZeroOrNone (setListIdx pots k (some 0#2)) := by
  intro x hx
  induction pots generalizing k with
  | nil => simp [setListIdx] at hx
  | cons p ps ih =>
    cases k with
    | zero =>
      simp only [setListIdx, List.mem_cons] at hx
      rcases hx with rfl | hxTail
      · exact Or.inr rfl
      · exact hP x (by simp [hxTail])
    | succ k' =>
      simp only [setListIdx, List.mem_cons] at hx
      rcases hx with rfl | hxTail
      · exact hP x (by simp)
      · exact ih (fun y hy => hP y (by simp [hy])) k' hxTail

private theorem setListIdx_preserves_some {pots : List (Option (BitVec 2))}
    (k j : Nat) (v : BitVec 2) (hJ : j < pots.length)
    (hSome : ∃ a, pots[j]? = some (some a)) :
    ∃ b, (setListIdx pots k (some v))[j]? = some (some b) := by
  rw [setListIdx_get pots k j (some v) hJ]
  split
  · exact ⟨v, rfl⟩
  · exact hSome

private theorem propagatePotStep_preserves_zero_and_some (n : Nat) (es : List ClauseEdge)
    (hZ : ∀ e ∈ es, e.cocycleShift = 0#2) (pots : List (Option (BitVec 2)))
    (hLen : pots.length = n) (hP : PotsZeroOrNone pots) :
    (propagatePotStep n es pots).length = n ∧
      PotsZeroOrNone (propagatePotStep n es pots) ∧
      ∀ j < n, (∃ a, pots[j]? = some (some a)) →
        ∃ b, (propagatePotStep n es pots)[j]? = some (some b) := by
  unfold propagatePotStep
  suffices h : ∀ (rem : List ClauseEdge) (acc : List (Option (BitVec 2))),
      (∀ e ∈ rem, e.cocycleShift = 0#2) → acc.length = n → PotsZeroOrNone acc →
      let out := rem.foldl (fun acc e =>
        if e.src < n && e.dst < n then
          match acc[e.src]?, acc[e.dst]? with
          | some (some ps), some none => setListIdx acc e.dst (some (ps + e.cocycleShift))
          | some none, some (some pd) => setListIdx acc e.src (some (pd - e.cocycleShift))
          | _, _ => acc
        else acc) acc
      out.length = n ∧ PotsZeroOrNone out ∧
        ∀ j < n, (∃ a, acc[j]? = some (some a)) → ∃ b, out[j]? = some (some b) by
    exact h es pots hZ hLen hP
  intro rem
  induction rem with
  | nil =>
    intro acc _ hAL hAP
    exact ⟨hAL, hAP, fun _ _ hS => hS⟩
  | cons e rest ih =>
    intro acc hRZ hAL hAP
    simp only [List.foldl_cons]
    have heZ : e.cocycleShift = 0#2 := hRZ e (by simp)
    have hRestZ : ∀ x ∈ rest, x.cocycleShift = 0#2 := fun x hx => hRZ x (by simp [hx])
    have hStep :
        let next :=
          if e.src < n && e.dst < n then
            match acc[e.src]?, acc[e.dst]? with
            | some (some ps), some none => setListIdx acc e.dst (some (ps + e.cocycleShift))
            | some none, some (some pd) => setListIdx acc e.src (some (pd - e.cocycleShift))
            | _, _ => acc
          else acc
        next.length = n ∧ PotsZeroOrNone next ∧
          ∀ j < n, (∃ a, acc[j]? = some (some a)) → ∃ b, next[j]? = some (some b) := by
      dsimp only
      by_cases hBd : (e.src < n && e.dst < n) = true
      · simp only [hBd, ↓reduceIte]
        cases hs : acc[e.src]? with
        | none => exact ⟨hAL, hAP, fun _ _ hx => hx⟩
        | some ops =>
          cases ops with
          | none =>
            cases hd : acc[e.dst]? with
            | none => exact ⟨hAL, hAP, fun _ _ hx => hx⟩
            | some opd =>
              cases opd with
              | none => exact ⟨hAL, hAP, fun _ _ hx => hx⟩
              | some pd =>
                have hPdZero : pd = 0#2 := by
                  have hMem : some pd ∈ acc := List.mem_of_getElem? hd
                  cases hAP (some pd) hMem with
                  | inl hN => cases hN
                  | inr hS => exact Option.some.inj hS
                have hDiff : pd - e.cocycleShift = 0#2 := by rw [hPdZero, heZ]; decide
                simp only [hDiff]
                exact ⟨by rw [setListIdx_length, hAL],
                       setListIdx_preserves_potsZero hAP e.src,
                       fun j hj hx => setListIdx_preserves_some e.src j 0#2 (by omega) hx⟩
          | some ps =>
            cases hd : acc[e.dst]? with
            | none => exact ⟨hAL, hAP, fun _ _ hx => hx⟩
            | some opd =>
              cases opd with
              | some _ => exact ⟨hAL, hAP, fun _ _ hx => hx⟩
              | none =>
                have hPsZero : ps = 0#2 := by
                  have hMem : some ps ∈ acc := List.mem_of_getElem? hs
                  cases hAP (some ps) hMem with
                  | inl hN => cases hN
                  | inr hS => exact Option.some.inj hS
                have hSum : ps + e.cocycleShift = 0#2 := by rw [hPsZero, heZ]; decide
                simp only [hSum]
                exact ⟨by rw [setListIdx_length, hAL],
                       setListIdx_preserves_potsZero hAP e.dst,
                       fun j hj hx => setListIdx_preserves_some e.dst j 0#2 (by omega) hx⟩
      · have hBdF : (e.src < n && e.dst < n) = false := by
          cases hb : (e.src < n && e.dst < n)
          · rfl
          · exact False.elim (hBd hb)
        simp only [hBdF, Bool.false_eq_true, ↓reduceIte]
        exact ⟨hAL, hAP, fun _ _ hx => hx⟩
    have hRec := ih _ hRestZ hStep.1 hStep.2.1
    refine ⟨hRec.1, hRec.2.1, fun j hj hx => hRec.2.2 j hj (hStep.2.2 j hj hx)⟩

private theorem edgeSheafObstruction_false_of_zero_shifts (n : Nat) (es : List ClauseEdge)
    (hZ : ∀ e ∈ es, e.cocycleShift = 0#2) :
    edgeSheafObstruction n es = false := by
  have hPots :
      (solveSheafPots n es).length = n ∧
        PotsZeroOrNone (solveSheafPots n es) ∧
        ∀ j < n, ∃ a, (solveSheafPots n es)[j]? = some (some a) := by
    unfold solveSheafPots
    suffices hRoots : ∀ roots : List Nat,
        ∀ pots : List (Option (BitVec 2)),
          pots.length = n → PotsZeroOrNone pots →
          (∀ j < n, j ∉ roots → ∃ a, pots[j]? = some (some a)) →
          let res := roots.foldl (fun pots root =>
            match pots[root]? with
            | some none =>
              let seeded := setListIdx pots root (some 0#2)
              (List.range n).foldl (fun p _ => propagatePotStep n es p) seeded
            | _ => pots) pots
          res.length = n ∧ PotsZeroOrNone res ∧ ∀ j < n, ∃ a, res[j]? = some (some a) by
      apply hRoots (List.range n) (List.replicate n none) (List.length_replicate ..)
      · intro x hx
        exact Or.inl (List.eq_of_mem_replicate hx)
      · intro j hj hNot
        simp [List.mem_range, hj] at hNot
    intro roots
    induction roots with
    | nil =>
      intro pots hL hP hCov
      exact ⟨hL, hP, fun j hj => hCov j hj (by simp)⟩
    | cons r rs ih =>
      intro pots hL hP hCov
      simp only [List.foldl_cons]
      have hStep :
          let next := match pots[r]? with
            | some none =>
              let seeded := setListIdx pots r (some 0#2)
              (List.range n).foldl (fun p _ => propagatePotStep n es p) seeded
            | _ => pots
          next.length = n ∧ PotsZeroOrNone next ∧
            ∀ j < n, (r = j ∨ ∃ a, pots[j]? = some (some a)) →
              ∃ a, next[j]? = some (some a) := by
        cases hR : pots[r]? with
        | none =>
          simp only
          refine ⟨hL, hP, fun j hj hOr => ?_⟩
          rcases hOr with rfl | hS
          · have : r < pots.length := by omega
            have hSome := List.getElem?_eq_getElem (by omega : r < pots.length)
            rw [hR] at hSome
            cases hSome
          · exact hS
        | some opr =>
          cases opr with
          | some ar =>
            simp only
            refine ⟨hL, hP, fun j hj hOr => ?_⟩
            rcases hOr with rfl | hS
            · exact ⟨ar, hR⟩
            · exact hS
          | none =>
            have hrLt : r < pots.length := by
              by_cases hc : r < pots.length
              · exact hc
              · have := List.getElem?_eq_none (Nat.le_of_not_lt hc)
                rw [this] at hR
                cases hR
            have hSeedLen : (setListIdx pots r (some 0#2)).length = n := by
              rw [setListIdx_length, hL]
            have hSeedP := setListIdx_preserves_potsZero hP r
            suffices hInner : ∀ steps : List Nat, ∀ q : List (Option (BitVec 2)),
                q.length = n → PotsZeroOrNone q →
                let qout := steps.foldl (fun p _ => propagatePotStep n es p) q
                qout.length = n ∧ PotsZeroOrNone qout ∧
                  ∀ j < n, (∃ a, q[j]? = some (some a)) → ∃ a, qout[j]? = some (some a) by
              have hRun := hInner (List.range n) (setListIdx pots r (some 0#2)) hSeedLen hSeedP
              refine ⟨hRun.1, hRun.2.1, fun j hj hOr => ?_⟩
              apply hRun.2.2 j hj
              rcases hOr with rfl | hS
              · exact ⟨0#2, by rw [setListIdx_get pots r r (some 0#2) hrLt]; simp⟩
              · exact setListIdx_preserves_some r j 0#2 (by omega) hS
            intro steps
            induction steps with
            | nil => intro q hQL hQP; exact ⟨hQL, hQP, fun _ _ hs => hs⟩
            | cons _ stTail ihSt =>
              intro q hQL hQP
              simp only [List.foldl_cons]
              have hp1 := propagatePotStep_preserves_zero_and_some n es hZ q hQL hQP
              have hp2 := ihSt (propagatePotStep n es q) hp1.1 hp1.2.1
              exact ⟨hp2.1, hp2.2.1, fun j hj hs => hp2.2.2 j hj (hp1.2.2 j hj hs)⟩
      apply ih _ hStep.1 hStep.2.1
      intro j hj hNotRs
      apply hStep.2.2 j hj
      by_cases hEq : r = j
      · exact Or.inl hEq
      · exact Or.inr (hCov j hj (fun h => hNotRs (Or.resolve_left (List.mem_cons.mp h) (Ne.symm hEq))))
  unfold edgeSheafObstruction
  cases hb : es.any (fun e =>
    if e.src < n && e.dst < n then
      match (solveSheafPots n es)[e.src]?, (solveSheafPots n es)[e.dst]? with
      | some (some ps), some (some pd) => (pd - ps) != e.cocycleShift
      | _, _ => true
    else false)
  · rfl
  · obtain ⟨e, heMem, heB⟩ := List.any_eq_true.mp hb
    cases hIn : (e.src < n && e.dst < n)
    · simp [hIn] at heB
    · simp only [hIn, ↓reduceIte] at heB
      have hSrcLt : e.src < n := by
        simp only [Bool.and_eq_true, decide_eq_true_eq] at hIn
        exact hIn.1
      have hDstLt : e.dst < n := by
        simp only [Bool.and_eq_true, decide_eq_true_eq] at hIn
        exact hIn.2
      obtain ⟨ps, hPs⟩ := hPots.2.2 e.src hSrcLt
      obtain ⟨pd, hPd⟩ := hPots.2.2 e.dst hDstLt
      have hPsZ : ps = 0#2 := by
        have hm : some ps ∈ solveSheafPots n es := List.mem_of_getElem? hPs
        cases hPots.2.1 (some ps) hm with
        | inl hn => cases hn
        | inr hs => exact Option.some.inj hs
      have hPdZ : pd = 0#2 := by
        have hm : some pd ∈ solveSheafPots n es := List.mem_of_getElem? hPd
        cases hPots.2.1 (some pd) hm with
        | inl hn => cases hn
        | inr hs => exact Option.some.inj hs
      have heZ := hZ e heMem
      simp only [hPs, hPd, hPsZ, hPdZ, heZ] at heB
      have hSubZ : ((0#2 - 0#2 : BitVec 2) != 0#2) = false := by decide
      rw [hSubZ] at heB
      cases heB

/-- 17. Compositional Certified Soundness: any certified contract graph guarantees actual
        consumer loss plus all cross-clause edge exposure stays within `SummarySpec.maxLossCap`. -/
theorem compositional_certified_bounds_loss (spec : SummarySpec) (clauses : List ContractClause)
    (edges : List ClauseEdge)
    (hCert : auditContractGraph spec clauses edges 0 = .certified) :
    ∀ act : AdversarialAction,
      actualConsumerLoss act clauses + edgesExposure edges ≤ spec.maxLossCap := by
  unfold auditContractGraph at hCert
  cases hBase : auditContract spec clauses
  · have hD : ¬ (spec.maxLossCap < compositionalWorstLoss clauses edges) := by
      intro hGt
      have hDt : decide (spec.maxLossCap < compositionalWorstLoss clauses edges) = true := decide_eq_true hGt
      simp [hBase, hDt] at hCert
    have hLe : compositionalWorstLoss clauses edges ≤ spec.maxLossCap := by omega
    intro act
    have hAct := actualLoss_le_worstCaseLoss act clauses
    unfold compositionalWorstLoss at hLe
    omega
  · simp [hBase] at hCert

/-- 18. Compositional Loophole Separation Witness: individual single-clause auditing is
        strictly weaker than compositional graph auditing — there exists a 3-clause contract
        whose individual clauses all certify, yet cross-clause interactions force `.vetoed`. -/
theorem compositional_loophole_separation_witness :
    ∃ (spec : SummarySpec) (clauses : List ContractClause) (edges : List ClauseEdge),
      auditContract spec clauses = .certified ∧
      auditContractGraph spec clauses edges 0 = .vetoed := by
  let spec : SummarySpec :=
    { maxLossCap := 10000, maxExitFee := 2000, protectedMask := 1#64, maxAsymNum := 2, maxAsymDen := 1 }
  let c0 : ContractClause :=
    { clauseId := 1, kind := .baseFee, disclosed := true, targetMask := 1#64, b4Truth := 2#2,
      baseLoss := 5000, extraDrift := 0, exitFee := 500, consPenalty := 100, instPenalty := 100 }
  let c1 : ContractClause :=
    { clauseId := 2, kind := .reciprocity, disclosed := true, targetMask := 0#64, b4Truth := 2#2,
      baseLoss := 3000, extraDrift := 0, exitFee := 500, consPenalty := 100, instPenalty := 100 }
  let c2 : ContractClause :=
    { clauseId := 3, kind := .reciprocity, disclosed := true, targetMask := 0#64, b4Truth := 2#2,
      baseLoss := 1000, extraDrift := 0, exitFee := 500, consPenalty := 100, instPenalty := 100 }
  let es : List ClauseEdge :=
    [ { src := 0, dst := 1, edgeKind := .redirect, cocycleShift := 1#2, extraExposure := 0 }
    , { src := 1, dst := 2, edgeKind := .powerDelegate, cocycleShift := 1#2, extraExposure := 0 }
    , { src := 2, dst := 0, edgeKind := .override, cocycleShift := 1#2, extraExposure := 5000 } ]
  exact ⟨spec, [c0, c1, c2], es, by decide, by decide⟩

/-- 19. Cohomological Loop Obstruction (`H¹ ≠ 0`): any closed walk of clause edges carrying
        a non-zero `Z₄` holonomy shift unconditionally forbids a consistent global interpretation. -/
theorem sheaf_cycle_forbids_global_interpretation (edges w : List ClauseEdge) (start : Nat)
    (hSub : ∀ e ∈ w, e ∈ edges)
    (hWalk : ValidClosedEdgeWalk start start w)
    (hHol : walkShiftZ4 w ≠ 0#2) :
    ¬ ∃ φ : Nat → BitVec 2, ∀ e ∈ edges, φ e.dst - φ e.src = e.cocycleShift := by
  rintro ⟨φ, hCoh⟩
  have hZero := closed_walk_shift_zero φ edges hCoh start start w hSub hWalk
  rw [bv2_sub_self_zero] at hZero
  exact hHol hZero

/-- 20. Override / Delegation Bypass Forces Graph Veto: any transitive bypass reaching
        consumer-protected `protectedMask` unconditionally forces `.vetoed`. -/
theorem override_bypass_forces_graph_veto (spec : SummarySpec) (clauses : List ContractClause)
    (edges : List ClauseEdge) (g : Nat)
    (hBypass : edgeHohfeldBypass spec clauses edges = true) :
    auditContractGraph spec clauses edges g = .vetoed := by
  simp [auditContractGraph, hBypass]

/-- 21. Compositional Drift Forces Graph Veto: whenever clauses plus cross-clause edge
        exposure exceed `maxLossCap`, the contract graph is fail-closed `.vetoed`. -/
theorem graph_drift_forces_veto (spec : SummarySpec) (clauses : List ContractClause)
    (edges : List ClauseEdge) (g : Nat)
    (hDrift : spec.maxLossCap < compositionalWorstLoss clauses edges) :
    auditContractGraph spec clauses edges g = .vetoed := by
  have hD : decide (spec.maxLossCap < compositionalWorstLoss clauses edges) = true := decide_eq_true hDrift
  simp [auditContractGraph, hD]

/-- 22. Graph Repair Soundness: `repairContractGraph` strips predatory clauses and all
        non-benign override/sheaf/default edges, eliminating bypass, `H¹` obstruction, and
        edge exposure, and restores `.certified` once base losses fit `maxLossCap`. -/
theorem graph_repair_restores_certified (spec : SummarySpec) (clauses : List ContractClause)
    (edges : List ClauseEdge) :
    let cs' := (repairContractGraph spec clauses edges).1
    let es' := (repairContractGraph spec clauses edges).2
    edgesExposure es' = 0 ∧
      edgeHohfeldBypass spec cs' es' = false ∧
      edgeSheafObstruction cs'.length es' = false ∧
      (worstCaseLoss cs' ≤ spec.maxLossCap → 0 < spec.maxAsymDen →
        auditContractGraph spec cs' es' 0 = .certified) := by
  intro cs' es'
  have hCs : ∀ c ∈ cs', clausePredatory spec c = false :=
    (repair_eliminates_predatory_clauses spec clauses).1
  have hEs : ∀ e ∈ es', edgeBenignForRepair spec cs' e = true := by
    intro e he
    simp only [es', repairContractGraph, List.mem_filter] at he
    exact he.2
  have hExp : edgesExposure es' = 0 := by
    unfold edgesExposure
    suffices hZeros : ∀ zs : List ClauseEdge, (∀ e ∈ zs, e.extraExposure = 0) →
        (zs.map (fun e => (e.extraExposure : Int))).sum = 0 by
      apply hZeros es'
      intro e he
      have hb := hEs e he
      simp only [edgeBenignForRepair, Bool.and_eq_true, beq_iff_eq] at hb
      exact hb.1.2
    intro zs hZ
    induction zs with
    | nil => simp
    | cons z ztl ih =>
      simp only [List.map_cons, List.sum_cons, hZ z (by simp), ih (fun x hx => hZ x (by simp [hx]))]
      rfl
  have hNoPow : ∀ e ∈ es', e.edgeKind ≠ .override ∧ e.edgeKind ≠ .powerDelegate := by
    intro e he
    have hb := hEs e he
    simp only [edgeBenignForRepair, Bool.and_eq_true, beq_iff_eq] at hb
    rw [hb.1.1.1]
    exact ⟨by decide, by decide⟩
  have hReachClean : ReachProtectedClean spec cs' (edgeReachFixed cs' es') := by
    unfold edgeReachFixed
    suffices hFold : ∀ (steps : List Nat) (r : List Bool),
        ReachProtectedClean spec cs' r →
        ReachProtectedClean spec cs' (steps.foldl (fun acc _ => edgeReachStep es' acc) r) by
      exact hFold (List.range cs'.length) (edgeReachSeed cs' es')
        (reachSeed_clean_of_repair spec cs' es' hCs hNoPow)
    intro steps
    induction steps with
    | nil => intro r hr; exact hr
    | cons _ tl ih =>
      intro r hr
      exact ih (edgeReachStep es' r) (reachStep_clean_of_repair spec cs' es' r hEs hr)
  have hBypass : edgeHohfeldBypass spec cs' es' = false := by
    unfold edgeHohfeldBypass
    cases hb : (List.range cs'.length).any (fun i =>
      ((edgeReachFixed cs' es')[i]?.getD false) &&
        match cs'[i]? with
        | some c => (c.targetMask &&& spec.protectedMask) != 0#64
        | none => false)
    · rfl
    · obtain ⟨i, hiR, hiB⟩ := List.any_eq_true.mp hb
      simp only [List.mem_range] at hiR
      simp only [Bool.and_eq_true] at hiB
      have hLen : (edgeReachFixed cs' es').length = cs'.length := by
        unfold edgeReachFixed
        suffices hL : ∀ (ss : List Nat) (r : List Bool),
            r.length = cs'.length → (ss.foldl (fun acc _ => edgeReachStep es' acc) r).length = cs'.length by
          exact hL (List.range cs'.length) (edgeReachSeed cs' es') (edgeReachSeed_length cs' es')
        intro ss
        induction ss with
        | nil => intro r hr; exact hr
        | cons _ tl ih =>
          intro r hr
          exact ih (edgeReachStep es' r) (by rw [edgeReachStep_length, hr])
      have hRi : (edgeReachFixed cs' es')[i]? = some true := by
        cases hGet : (edgeReachFixed cs' es')[i]? with
        | none => simp [hGet] at hiB
        | some b =>
          simp only [hGet, Option.getD_some] at hiB
          simp [hiB.1]
      have hCl := hReachClean i (by omega) hRi
      cases hCi : cs'[i]? with
      | none => simp [hCi] at hiB
      | some ci =>
        simp only [hCi] at hCl hiB
        simp [hCl] at hiB
  have hSheaf : edgeSheafObstruction cs'.length es' = false := by
    apply edgeSheafObstruction_false_of_zero_shifts cs'.length es'
    intro e he
    have hb := hEs e he
    simp only [edgeBenignForRepair, Bool.and_eq_true, beq_iff_eq] at hb
    exact hb.1.1.2
  refine ⟨hExp, hBypass, hSheaf, ?_⟩
  intro hCap hDen
  have hBaseCert : auditContract spec cs' = .certified :=
    (repair_eliminates_predatory_clauses spec clauses).2 hCap hDen
  have hChisF : edgeChisholmCascade es' = false := by
    unfold edgeChisholmCascade
    cases hb : es'.any (fun e => e.edgeKind == .defaultTrigger && decide (0 < e.extraExposure))
    · rfl
    · obtain ⟨e, he, heB⟩ := List.any_eq_true.mp hb
      have hBen := hEs e he
      simp only [edgeBenignForRepair, Bool.and_eq_true, beq_iff_eq] at hBen
      simp only [Bool.and_eq_true, beq_iff_eq] at heB
      rw [hBen.1.1.1] at heB
      cases heB.1
  have hCompLe : ¬ (spec.maxLossCap < compositionalWorstLoss cs' es') := by
    unfold compositionalWorstLoss
    rw [hExp]
    omega
  have hCompF : decide (spec.maxLossCap < compositionalWorstLoss cs' es') = false :=
    decide_eq_false hCompLe
  unfold auditContractGraph
  simp [hBaseCert, hBypass, hSheaf, hChisF, hCompF]

end Sound.Contract
