/-
  Sound.Bilattice — 4-Valued Alternating-Fixpoint Bilattice, Hohfeldian Gates & Span Guard
  ==========================================================================================
  Domain-free well-founded bilattice operator (`Γ / Γ²`) over 64-node support,
  requisition, tag-stratified Hume firewall, inhibitor graphs, bounded admission guard,
  Hohfeldian dual-flag coherency, and 4-way epistemic bit partition (`bv_decide`).
  Zero external imports beyond `Std.Tactic.BVDecide`. Zero `sorryAx`.
-/
import Std.Tactic.BVDecide

namespace Sound.Bilattice

inductive FourVal where
  | accept
  | reject
  | conflict
  | halted
  deriving Repr, DecidableEq, Inhabited

structure GraphNode where
  id         : Nat
  tag        : UInt8 := 0
  rank       : UInt8 := 0
  bridge     : Bool := false
  disabled   : Bool := false
  halted     : Bool := false
  reqs       : List Nat := []
  clauses    : List (List Nat) := [[]]
  inhibitors : List Nat := []
  deriving Repr, DecidableEq, Inhabited

structure GuardCandidate where
  targetId    : Nat
  spanStart   : Nat
  spanEnd     : Nat
  quarantined : Bool := false
  deriving Repr, DecidableEq, Inhabited

structure GuardBatch where
  limitBytes : Nat
  flagMask   : UInt32
  auxMask    : UInt32
  items      : List GuardCandidate
  deriving Repr, DecidableEq, Inhabited

inductive GuardOutcome where
  | admitted (id : Nat)
  | needsInput
  | quarantined
  | invalidBounds
  deriving Repr, DecidableEq

def DistinctIds (nodes : List GraphNode) : Prop :=
  ∀ a ∈ nodes, ∀ b ∈ nodes, a.id = b.id → a = b

def inSet (xs : List Nat) (x : Nat) : Bool :=
  xs.contains x

def SubsetSet (xs ys : List Nat) : Prop :=
  ∀ x, inSet xs x = true → inSet ys x = true

theorem SubsetSet.refl (xs : List Nat) : SubsetSet xs xs :=
  fun _ hx => hx

theorem SubsetSet.trans {xs ys zs : List Nat}
    (h1 : SubsetSet xs ys) (h2 : SubsetSet ys zs) : SubsetSet xs zs :=
  fun x hx => h2 x (h1 x hx)

theorem contains_eq_true_iff_mem (l : List Nat) (x : Nat) :
    l.contains x = true ↔ x ∈ l := by
  induction l with
  | nil => simp
  | cons y ys ih =>
      simp only [List.contains_cons, Bool.or_eq_true, beq_iff_eq, List.mem_cons, ih]

def validClause (leafThreshold : UInt8) (nodes : List GraphNode) (u : GraphNode)
    (clause : List Nat) : Bool :=
  let leafOk := (u.rank < leafThreshold) || !clause.isEmpty
  let fwdOk :=
    (u.tag == 0) || u.bridge ||
      clause.any (fun rid => nodes.any (fun r => r.id == rid && r.tag != 0))
  let revOk :=
    (u.tag != 0) ||
      clause.all (fun rid => nodes.any (fun r => r.id == rid && r.tag == 0))
  leafOk && fwdOk && revOk

def nodeFires (leafThreshold : UInt8) (nodes : List GraphNode) (blockedBy active : List Nat)
    (u : GraphNode) : Bool :=
  !u.disabled &&
  !u.halted &&
  u.inhibitors.all (fun a => !inSet blockedBy a) &&
  u.reqs.all (inSet active) &&
  u.clauses.any (fun c => validClause leafThreshold nodes u c && c.all (inSet active))

def stepGamma (leafThreshold : UInt8) (nodes : List GraphNode) (blockedBy active : List Nat) :
    List Nat :=
  (nodes.filter (nodeFires leafThreshold nodes blockedBy active)).map GraphNode.id

def iterGamma (leafThreshold : UInt8) (nodes : List GraphNode) (blockedBy : List Nat) :
    Nat → List Nat
  | 0 => []
  | k + 1 => stepGamma leafThreshold nodes blockedBy (iterGamma leafThreshold nodes blockedBy k)

def gamma (leafThreshold : UInt8) (nodes : List GraphNode) (blockedBy : List Nat) : List Nat :=
  iterGamma leafThreshold nodes blockedBy nodes.length

def iterGamma2 (leafThreshold : UInt8) (nodes : List GraphNode) : Nat → List Nat
  | 0 => []
  | k + 1 => gamma leafThreshold nodes (gamma leafThreshold nodes (iterGamma2 leafThreshold nodes k))

def evalPStar (leafThreshold : UInt8) (nodes : List GraphNode) : List Nat :=
  gamma leafThreshold nodes (iterGamma2 leafThreshold nodes nodes.length)

def evalIStar (leafThreshold : UInt8) (nodes : List GraphNode) : List Nat :=
  gamma leafThreshold nodes (evalPStar leafThreshold nodes)

def evalNode (leafThreshold : UInt8) (nodes : List GraphNode) (u : GraphNode) : FourVal :=
  if u.halted then
    .halted
  else if inSet (evalIStar leafThreshold nodes) u.id then
    .accept
  else if inSet (evalPStar leafThreshold nodes) u.id then
    .conflict
  else
    .reject

def listToMask64 (ids : List Nat) : UInt64 :=
  ids.foldl (fun m i => m ||| ((1 : UInt64) <<< (UInt64.ofNat (i % 64)))) 0

def statusMasks (leafThreshold : UInt8) (nodes : List GraphNode) :
    UInt64 × UInt64 × UInt64 × UInt64 × UInt64 × UInt64 × UInt64 :=
  let nodeMask :=
    if nodes.length >= 64 then 0xFFFFFFFFFFFFFFFF
    else ((1 : UInt64) <<< (UInt64.ofNat nodes.length)) - 1
  let iStar := listToMask64 (evalIStar leafThreshold nodes)
  let pStar := listToMask64 (evalPStar leafThreshold nodes)
  let masks := Id.run do
    let mut acc : UInt64 := 0
    let mut rej : UInt64 := 0
    let mut conf : UInt64 := 0
    let mut hlt : UInt64 := 0
    for u in nodes do
      let bit := (1 : UInt64) <<< (UInt64.ofNat (u.id % 64))
      match evalNode leafThreshold nodes u with
      | .accept => acc := acc ||| bit
      | .reject => rej := rej ||| bit
      | .conflict => conf := conf ||| bit
      | .halted => hlt := hlt ||| bit
    return (acc, rej, conf, hlt)
  (nodeMask, iStar, pStar, masks.1, masks.2.1, masks.2.2.1, masks.2.2.2)

def admitGuard (b : GuardBatch) : GuardOutcome :=
  if b.flagMask != 0 || b.auxMask != 0 || b.items.length != 1 then
    .needsInput
  else
    match b.items with
    | [c] =>
        if !(c.spanStart < c.spanEnd && c.spanEnd ≤ b.limitBytes) then
          .invalidBounds
        else if c.quarantined then
          .quarantined
        else
          .admitted c.targetId
    | _ => .needsInput

theorem inSet_stepGamma_iff (t : UInt8) (nodes : List GraphNode) (blockedBy active : List Nat)
    (nid : Nat) :
    inSet (stepGamma t nodes blockedBy active) nid = true ↔
      ∃ u ∈ nodes, u.id = nid ∧ nodeFires t nodes blockedBy active u = true := by
  simp only [inSet, contains_eq_true_iff_mem, stepGamma, List.mem_map, List.mem_filter]
  constructor
  · rintro ⟨u, ⟨huMem, huFire⟩, rfl⟩
    exact ⟨u, huMem, rfl, huFire⟩
  · rintro ⟨u, huMem, rfl, huFire⟩
    exact ⟨u, ⟨huMem, huFire⟩, rfl⟩

theorem nodeFires_mono_anti (t : UInt8) (nodes : List GraphNode) {B1 B2 A1 A2 : List Nat}
    {u : GraphNode} (hB : SubsetSet B1 B2) (hA : SubsetSet A1 A2)
    (hfire : nodeFires t nodes B2 A1 u = true) :
    nodeFires t nodes B1 A2 u = true := by
  simp only [nodeFires, Bool.and_eq_true, List.all_eq_true, List.any_eq_true, Bool.not_eq_true'] at hfire ⊢
  rcases hfire with ⟨⟨⟨⟨hdis, hhlt⟩, hinh⟩, hreq⟩, ⟨c, hcMem, hval, hsat⟩⟩
  refine ⟨⟨⟨⟨hdis, hhlt⟩, ?_⟩, ?_⟩, ⟨c, hcMem, hval, ?_⟩⟩
  · intro a ha
    have haB2 : inSet B2 a = false := hinh a ha
    cases hB1 : inSet B1 a
    · rfl
    · have : inSet B2 a = true := hB a hB1
      rw [haB2] at this
      cases this
  · intro p hp
    exact hA p (hreq p hp)
  · intro req hreq
    exact hA req (hsat req hreq)

theorem stepGamma_mono_anti (t : UInt8) (nodes : List GraphNode) {B1 B2 A1 A2 : List Nat}
    (hB : SubsetSet B1 B2) (hA : SubsetSet A1 A2) :
    SubsetSet (stepGamma t nodes B2 A1) (stepGamma t nodes B1 A2) := by
  intro nid hnid
  rw [inSet_stepGamma_iff] at hnid ⊢
  obtain ⟨u, huMem, hid, hfire⟩ := hnid
  exact ⟨u, huMem, hid, nodeFires_mono_anti t nodes hB hA hfire⟩

theorem iterGamma_mono_anti (t : UInt8) (nodes : List GraphNode) {B1 B2 : List Nat}
    (hB : SubsetSet B1 B2) :
    ∀ k, SubsetSet (iterGamma t nodes B2 k) (iterGamma t nodes B1 k) := by
  intro k
  induction k with
  | zero =>
      intro x hx
      simp [inSet, iterGamma] at hx
  | succ k ih =>
      exact stepGamma_mono_anti t nodes hB ih

theorem gamma_antitone (t : UInt8) (nodes : List GraphNode) {X Y : List Nat}
    (hsub : SubsetSet X Y) :
    SubsetSet (gamma t nodes Y) (gamma t nodes X) :=
  iterGamma_mono_anti t nodes hsub nodes.length

theorem iterGamma_step_sub (t : UInt8) (nodes : List GraphNode) (B : List Nat) (k : Nat) :
    SubsetSet (iterGamma t nodes B k) (iterGamma t nodes B (k + 1)) := by
  induction k with
  | zero =>
      intro x hx
      simp [inSet, iterGamma] at hx
  | succ k ih =>
      exact stepGamma_mono_anti t nodes (SubsetSet.refl B) ih

theorem iterGamma2_step_sub (t : UInt8) (nodes : List GraphNode) (k : Nat) :
    SubsetSet (iterGamma2 t nodes k) (iterGamma2 t nodes (k + 1)) := by
  induction k with
  | zero =>
      intro x hx
      simp [inSet, iterGamma2] at hx
  | succ k ih =>
      exact gamma_antitone t nodes (gamma_antitone t nodes ih)

theorem iterGamma2_sub_gamma (t : UInt8) (nodes : List GraphNode) (k : Nat) :
    SubsetSet (iterGamma2 t nodes k) (gamma t nodes (iterGamma2 t nodes k)) := by
  induction k with
  | zero =>
      intro x hx
      simp [inSet, iterGamma2] at hx
  | succ k ih =>
      exact gamma_antitone t nodes (gamma_antitone t nodes ih)

theorem IStar_sub_PStar (t : UInt8) (nodes : List GraphNode) :
    SubsetSet (evalIStar t nodes) (evalPStar t nodes) :=
  gamma_antitone t nodes (iterGamma2_sub_gamma t nodes nodes.length)

theorem mutual_fixpoint_of_stabilized (t : UInt8) (nodes : List GraphNode) (k : Nat)
    (hStab : SubsetSet (iterGamma2 t nodes (k + 1)) (iterGamma2 t nodes k)) :
    let I := iterGamma2 t nodes k
    let P := gamma t nodes I
    SubsetSet I (gamma t nodes P) ∧ SubsetSet (gamma t nodes P) I := by
  exact ⟨iterGamma2_step_sub t nodes k, hStab⟩

theorem iterGamma_elim (t : UInt8) (nodes : List GraphNode) (B : List Nat) (k nid : Nat)
    (hmem : inSet (iterGamma t nodes B k) nid = true) :
    ∃ (k' : Nat) (u : GraphNode), k = k' + 1 ∧ u ∈ nodes ∧ u.id = nid ∧
      nodeFires t nodes B (iterGamma t nodes B k') u = true := by
  cases k with
  | zero =>
      simp [inSet, iterGamma] at hmem
  | succ k' =>
      rw [iterGamma, inSet_stepGamma_iff] at hmem
      obtain ⟨u, huMem, hid, hfire⟩ := hmem
      exact ⟨k', u, rfl, huMem, hid, hfire⟩

theorem gamma_elim (t : UInt8) (nodes : List GraphNode) (B : List Nat) (nid : Nat)
    (hmem : inSet (gamma t nodes B) nid = true) :
    ∃ (k' : Nat) (u : GraphNode), nodes.length = k' + 1 ∧ u ∈ nodes ∧ u.id = nid ∧
      nodeFires t nodes B (iterGamma t nodes B k') u = true :=
  iterGamma_elim t nodes B nodes.length nid hmem

theorem halted_never_active (t : UInt8) (nodes : List GraphNode) (u : GraphNode)
    (huMem : u ∈ nodes) (huniq : DistinctIds nodes) (hhlt : u.halted = true) :
    evalNode t nodes u = .halted ∧
    inSet (evalIStar t nodes) u.id = false ∧
    inSet (evalPStar t nodes) u.id = false := by
  have notInGamma : ∀ B, inSet (gamma t nodes B) u.id = false := by
    intro B
    cases hmem : inSet (gamma t nodes B) u.id
    · rfl
    · exfalso
      obtain ⟨_, n, _, hnMem, hnid, hfire⟩ := gamma_elim t nodes B u.id hmem
      obtain rfl := huniq n hnMem u huMem hnid
      simp only [nodeFires, hhlt, Bool.not_true, Bool.and_false, Bool.false_and] at hfire
      cases hfire
  exact ⟨by simp [evalNode, hhlt], notInGamma (evalPStar t nodes), notInGamma _⟩

theorem evalIStar_true_of_accept (t : UInt8) (nodes : List GraphNode) (u : GraphNode)
    (hacc : evalNode t nodes u = .accept) :
    u.halted = false ∧ inSet (evalIStar t nodes) u.id = true := by
  cases hhlt : u.halted with
  | true => simp [evalNode, hhlt] at hacc
  | false =>
      simp only [evalNode, hhlt, Bool.false_eq_true, ↓reduceIte] at hacc
      cases hmem : inSet (evalIStar t nodes) u.id
      · simp only [hmem, Bool.false_eq_true, ↓reduceIte] at hacc
        split at hacc <;> cases hacc
      · exact ⟨rfl, rfl⟩

theorem disabled_or_ungrounded_or_missing_req_never_accept (t : UInt8) (nodes : List GraphNode)
    (u : GraphNode) (huMem : u ∈ nodes) (huniq : DistinctIds nodes)
    (hBad : u.disabled = true ∨
            (t ≤ u.rank ∧ ∀ c ∈ u.clauses, c.isEmpty = true) ∨
            (∃ p ∈ u.reqs, inSet (evalIStar t nodes) p = false)) :
    evalNode t nodes u ≠ .accept := by
  intro hacc
  obtain ⟨_, hI⟩ := evalIStar_true_of_accept t nodes u hacc
  obtain ⟨k', n, hlen, hnMem, hnid, hfire⟩ :=
    gamma_elim t nodes (evalPStar t nodes) u.id hI
  obtain rfl := huniq n hnMem u huMem hnid
  simp only [nodeFires, Bool.and_eq_true, Bool.not_eq_true', List.all_eq_true,
    List.any_eq_true] at hfire
  rcases hfire with ⟨⟨⟨⟨hdis, _⟩, _⟩, hreq⟩, ⟨c, hcMem, hval, _⟩⟩
  rcases hBad with hbadDis | ⟨hbadRank, hbadLeaf⟩ | ⟨p, hpMem, hpFalse⟩
  · rw [hbadDis] at hdis
    cases hdis
  · have hcEmpty : c.isEmpty = true := hbadLeaf c hcMem
    simp only [validClause, hcEmpty, Bool.not_true, Bool.or_false, Bool.and_eq_true,
      decide_eq_true_eq] at hval
    exact absurd hval.1.1 (Nat.not_lt_of_ge hbadRank)
  · have hpPrev : inSet (iterGamma t nodes (evalPStar t nodes) k') p = true := hreq p hpMem
    have hpStep := iterGamma_step_sub t nodes (evalPStar t nodes) k' p hpPrev
    have hpStar : inSet (evalIStar t nodes) p = true := by
      simpa [evalIStar, gamma, hlen] using hpStep
    rw [hpFalse] at hpStar
    cases hpStar

theorem nonbase_validClause_witness (t : UInt8) (nodes : List GraphNode) (r : GraphNode)
    (c : List Nat) (hNoBridge : r.bridge = false) (hNonBase : r.tag ≠ 0)
    (hval : validClause t nodes r c = true) :
    ∃ rid ∈ c, ∃ q ∈ nodes, q.id = rid ∧ q.tag ≠ 0 := by
  have hrTagBool : (r.tag == 0) = false := by
    cases h : (r.tag == 0)
    · rfl
    · exfalso
      exact hNonBase (beq_iff_eq.mp h)
  simp only [validClause, hrTagBool, hNoBridge, Bool.false_or, Bool.and_eq_true,
    List.any_eq_true, beq_iff_eq, bne_iff_ne, ne_eq] at hval
  obtain ⟨⟨_, ⟨rid, hridMem, q, hqMem, hqid, hqNonBase⟩⟩, _⟩ := hval
  exact ⟨rid, hridMem, q, hqMem, hqid, hqNonBase⟩

theorem iterGamma_only_base_tag_without_bridge (t : UInt8) (nodes : List GraphNode)
    (huniq : DistinctIds nodes) (hNoBridge : ∀ n ∈ nodes, n.bridge = false)
    (B : List Nat) :
    ∀ k nid, inSet (iterGamma t nodes B k) nid = true →
      ∀ r ∈ nodes, r.id = nid → r.tag = 0 := by
  intro k
  induction k with
  | zero =>
      intro nid hmem
      simp [inSet, iterGamma] at hmem
  | succ k ih =>
      intro nid hmem r hrMem hrid
      rw [iterGamma, inSet_stepGamma_iff] at hmem
      obtain ⟨u, huMem, huid, hfire⟩ := hmem
      obtain rfl := huniq r hrMem u huMem (hrid.trans huid.symm)
      simp only [nodeFires, Bool.and_eq_true, List.any_eq_true, List.all_eq_true] at hfire
      rcases hfire with ⟨_, ⟨c, _, hval, hsat⟩⟩
      by_cases htag : r.tag = 0
      · exact htag
      · exfalso
        have hrNb := hNoBridge r huMem
        obtain ⟨rid, hridMem, q, hqMem, hqid, hqNonBase⟩ :=
          nonbase_validClause_witness t nodes r c hrNb htag hval
        have hreqActive : inSet (iterGamma t nodes B k) rid = true := hsat rid hridMem
        exact hqNonBase (ih rid hreqActive q hqMem hqid)

theorem fwd_tag_firewall (t : UInt8) (nodes : List GraphNode) (u : GraphNode)
    (huMem : u ∈ nodes) (huniq : DistinctIds nodes)
    (hNoBridge : ∀ n ∈ nodes, n.bridge = false) (hNonBase : u.tag ≠ 0) :
    evalNode t nodes u ≠ .accept := by
  intro hacc
  obtain ⟨_, hI⟩ := evalIStar_true_of_accept t nodes u hacc
  have hBase := iterGamma_only_base_tag_without_bridge t nodes huniq hNoBridge
    (evalPStar t nodes) nodes.length u.id hI u huMem rfl
  exact hNonBase hBase

theorem rev_tag_firewall (t : UInt8) (nodes : List GraphNode) (u r : GraphNode)
    (c : List Nat) (huniq : DistinctIds nodes) (huBase : u.tag = 0)
    (hrMem : r ∈ nodes) (hrNonBase : r.tag ≠ 0) (hrInClause : r.id ∈ c) :
    validClause t nodes u c = false := by
  cases hval : validClause t nodes u c
  · rfl
  · exfalso
    simp only [validClause, huBase, beq_self_eq_true, bne_self_eq_false, Bool.false_or,
      Bool.and_eq_true, List.all_eq_true, List.any_eq_true, beq_iff_eq] at hval
    obtain ⟨q, hqMem, hqid, hqBase⟩ := hval.2 r.id hrInClause
    obtain rfl := huniq q hqMem r hrMem hqid
    exact hrNonBase hqBase

theorem accept_defeats_all_inhibitors (t : UInt8) (nodes : List GraphNode) (u : GraphNode)
    (huMem : u ∈ nodes) (huniq : DistinctIds nodes)
    (hacc : evalNode t nodes u = .accept) :
    ∀ a ∈ u.inhibitors, inSet (evalPStar t nodes) a = false := by
  obtain ⟨_, hI⟩ := evalIStar_true_of_accept t nodes u hacc
  obtain ⟨_, n, _, hnMem, hnid, hfire⟩ :=
    gamma_elim t nodes (evalPStar t nodes) u.id hI
  obtain rfl := huniq n hnMem u huMem hnid
  simp only [nodeFires, Bool.and_eq_true, List.all_eq_true, Bool.not_eq_true'] at hfire
  rcases hfire with ⟨⟨⟨⟨_, _⟩, hinh⟩, _⟩, _⟩
  exact hinh

theorem direct_consistency (t : UInt8) (nodes : List GraphNode) (u v : GraphNode)
    (huMem : u ∈ nodes) (hvMem : v ∈ nodes) (huniq : DistinctIds nodes)
    (hinh : u.id ∈ v.inhibitors ∨ v.id ∈ u.inhibitors) :
    ¬ (evalNode t nodes u = .accept ∧ evalNode t nodes v = .accept) := by
  intro ⟨huAcc, hvAcc⟩
  rcases hinh with huInV | hvInU
  · obtain ⟨_, huI⟩ := evalIStar_true_of_accept t nodes u huAcc
    have huP := IStar_sub_PStar t nodes u.id huI
    have huNotP := accept_defeats_all_inhibitors t nodes v hvMem huniq hvAcc u.id huInV
    rw [huNotP] at huP
    cases huP
  · obtain ⟨_, hvI⟩ := evalIStar_true_of_accept t nodes v hvAcc
    have hvP := IStar_sub_PStar t nodes v.id hvI
    have hvNotP := accept_defeats_all_inhibitors t nodes u huMem huniq huAcc v.id hvInU
    rw [hvNotP] at hvP
    cases hvP

theorem admitGuard_blocks_flagged_or_multi (b : GuardBatch)
    (hBad : b.flagMask ≠ 0 ∨ b.auxMask ≠ 0 ∨ b.items.length ≠ 1) :
    ∀ nid, admitGuard b ≠ .admitted nid := by
  intro nid hadm
  simp only [admitGuard] at hadm
  rcases hBad with h1 | h2 | h3
  · have : (b.flagMask != 0) = true := by simp [h1]
    simp [this] at hadm
  · have : (b.auxMask != 0) = true := by simp [h2]
    simp [this] at hadm
  · have : (b.items.length != 1) = true := by simp [h3]
    simp [this] at hadm

theorem admitGuard_blocks_quarantined (b : GuardBatch) (c : GuardCandidate)
    (hcList : b.items = [c]) (hcQuar : c.quarantined = true) :
    ∀ nid, admitGuard b ≠ .admitted nid := by
  intro nid hadm
  simp only [admitGuard, hcList, hcQuar, ↓reduceIte] at hadm
  split at hadm
  · cases hadm
  · split at hadm <;> cases hadm

/-! ## Hohfeldian Dual-Flag Gates & 4-Way Epistemic Partition (`bv_decide`) -/

def gatedDualUpdateBV64 (pos neg pow lock bit : BitVec 64) (gp rp gn rn : Bool) :
    BitVec 64 × BitVec 64 :=
  if ((pow &&& bit) == 0#64) || ((lock &&& bit) != 0#64) then
    (pos, neg)
  else
    let dp0 := if gp then pos ||| bit else pos
    let dp  := if rp then dp0 &&& ~~~bit else dp0
    let dn0 := if gn then neg ||| bit else neg
    let dn  := if rn then dn0 &&& ~~~bit else dn0
    if (dp &&& dn) != 0#64 then (pos, neg) else (dp, dn)

theorem gated_dual_update_locked_bv64 (pos neg pow lock bit : BitVec 64)
    (gp rp gn rn : Bool) (hLock : ((lock &&& bit) != 0#64) = true) :
    gatedDualUpdateBV64 pos neg pow lock bit gp rp gn rn = (pos, neg) := by
  simp [gatedDualUpdateBV64, hLock]

theorem gated_dual_update_no_pow_bv64 (pos neg pow lock bit : BitVec 64)
    (gp rp gn rn : Bool) (hNoPow : ((pow &&& bit) == 0#64) = true) :
    gatedDualUpdateBV64 pos neg pow lock bit gp rp gn rn = (pos, neg) := by
  simp [gatedDualUpdateBV64, hNoPow]

theorem gated_dual_update_coherent_bv64 (pos neg pow lock bit : BitVec 64)
    (gp rp gn rn : Bool) (hCoh : (pos &&& neg) = 0#64) :
    let res := gatedDualUpdateBV64 pos neg pow lock bit gp rp gn rn
    (res.1 &&& res.2) = 0#64 := by
  dsimp only
  unfold gatedDualUpdateBV64
  by_cases hGate : (((pow &&& bit) == 0#64) || ((lock &&& bit) != 0#64)) = true
  · simp [hGate, hCoh]
  · simp only [hGate, Bool.false_eq_true, ↓reduceIte]
    by_cases hBad : ((let dp0 := if gp then pos ||| bit else pos
                      let dp := if rp then dp0 &&& ~~~bit else dp0
                      let dn0 := if gn then neg ||| bit else neg
                      let dn := if rn then dn0 &&& ~~~bit else dn0
                      dp &&& dn) != 0#64) = true
    · simp [hBad, hCoh]
    · simp only [bne_iff_ne, ne_eq, Decidable.not_not] at hBad
      simp [hBad]

theorem bit_partition_four_way_bv64 (nodeMask iStar pStar guardMask : BitVec 64)
    (hSub : (iStar &&& ~~~pStar) = 0#64) :
    let g := guardMask &&& nodeMask
    let ng := nodeMask &&& ~~~g
    let e := iStar &&& ng
    let c := (pStar &&& ~~~iStar) &&& ng
    let d := ng &&& ~~~pStar
    (e &&& c) = 0#64 ∧
    (e &&& d) = 0#64 ∧
    (e &&& g) = 0#64 ∧
    (c &&& d) = 0#64 ∧
    (c &&& g) = 0#64 ∧
    (d &&& g) = 0#64 ∧
    (e ||| c ||| d ||| g) = nodeMask := by
  bv_decide

/-! ## Prepared-Statement Policy/Input Isolation (`bv_decide`) -/

/-- Pure bit-vector admission predicate modeling Zero-Trust Prepared-Statement
    policy/data plane separation:
    - `policyMask` and `bindMask` must be strictly disjoint;
    - `targetMask` must be non-zero, non-immutable, outside `policyMask`,
      and strictly contained in `bindMask`;
    - neither the target slot nor candidate may carry non-factual sort,
      privilege escalation flags / enacted-norm authority, or topology edges. -/
def preparedPolicyAdmitBV64
    (policyMask bindMask immMask targetMask candTopoMask : BitVec 64)
    (targetNonFactual candNonFactual candPrivOrEnacted : Bool) : Bool :=
  ((policyMask &&& bindMask) == 0#64) &&
  (targetMask != 0#64) &&
  ((targetMask &&& immMask) == 0#64) &&
  ((targetMask &&& policyMask) == 0#64) &&
  ((targetMask &&& ~~~bindMask) == 0#64) &&
  (!targetNonFactual) &&
  (!candNonFactual) &&
  (!candPrivOrEnacted) &&
  (candTopoMask == 0#64)

/-- 31. Admitted prepared-statement candidates guarantee strict disjointness of
    policy and bind masks, with `targetMask` inside `bindMask` and outside `policyMask`. -/
theorem prepared_policy_disjoint_mask_bv64
    (policyMask bindMask immMask targetMask candTopoMask : BitVec 64)
    (targetNonFactual candNonFactual candPrivOrEnacted : Bool)
    (hAdmit : preparedPolicyAdmitBV64 policyMask bindMask immMask targetMask candTopoMask
        targetNonFactual candNonFactual candPrivOrEnacted = true) :
    (policyMask &&& bindMask) = 0#64 ∧
    (targetMask &&& bindMask) = targetMask ∧
    (targetMask &&& policyMask) = 0#64 := by
  unfold preparedPolicyAdmitBV64 at hAdmit
  bv_decide

/-- 32. Any target overlap with an immutable slot (`immMask`) is unconditionally blocked. -/
theorem prepared_blocks_immunity_bv64
    (policyMask bindMask immMask targetMask candTopoMask : BitVec 64)
    (targetNonFactual candNonFactual candPrivOrEnacted : Bool)
    (hImm : (targetMask &&& immMask) ≠ 0#64) :
    preparedPolicyAdmitBV64 policyMask bindMask immMask targetMask candTopoMask
        targetNonFactual candNonFactual candPrivOrEnacted = false := by
  unfold preparedPolicyAdmitBV64
  bv_decide

/-- 33. Any attempt to write into a sealed policy slot (`policyMask`) is unconditionally blocked. -/
theorem prepared_blocks_policy_slot_bv64
    (policyMask bindMask immMask targetMask candTopoMask : BitVec 64)
    (targetNonFactual candNonFactual candPrivOrEnacted : Bool)
    (hPol : (targetMask &&& policyMask) ≠ 0#64) :
    preparedPolicyAdmitBV64 policyMask bindMask immMask targetMask candTopoMask
        targetNonFactual candNonFactual candPrivOrEnacted = false := by
  unfold preparedPolicyAdmitBV64
  bv_decide

/-- 34. Any write to an unbound or zero target slot outside `bindMask` is blocked. -/
theorem prepared_blocks_unbound_slot_bv64
    (policyMask bindMask immMask targetMask candTopoMask : BitVec 64)
    (targetNonFactual candNonFactual candPrivOrEnacted : Bool)
    (hUnbound : targetMask = 0#64 ∨ (targetMask &&& ~~~bindMask) ≠ 0#64) :
    preparedPolicyAdmitBV64 policyMask bindMask immMask targetMask candTopoMask
        targetNonFactual candNonFactual candPrivOrEnacted = false := by
  unfold preparedPolicyAdmitBV64
  bv_decide

/-- 35. Cross-plane sort mutations (`targetNonFactual` / `candNonFactual`) and
    privilege / enacted-norm escalation (`candPrivOrEnacted`) are unconditionally blocked. -/
theorem prepared_blocks_cross_plane_or_priv_esc
    (policyMask bindMask immMask targetMask candTopoMask : BitVec 64)
    (targetNonFactual candNonFactual candPrivOrEnacted : Bool)
    (hEsc : targetNonFactual = true ∨ candNonFactual = true ∨ candPrivOrEnacted = true) :
    preparedPolicyAdmitBV64 policyMask bindMask immMask targetMask candTopoMask
        targetNonFactual candNonFactual candPrivOrEnacted = false := by
  unfold preparedPolicyAdmitBV64
  bv_decide

/-- 36. Any non-zero structural/topology edge injection (`presup ||| premise ||| attack`) is blocked. -/
theorem prepared_blocks_topology_injection_bv64
    (policyMask bindMask immMask targetMask candPresup candPremise candAttack : BitVec 64)
    (targetNonFactual candNonFactual candPrivOrEnacted : Bool)
    (hTopo : (candPresup ||| candPremise ||| candAttack) ≠ 0#64) :
    preparedPolicyAdmitBV64 policyMask bindMask immMask targetMask
        (candPresup ||| candPremise ||| candAttack)
        targetNonFactual candNonFactual candPrivOrEnacted = false := by
  unfold preparedPolicyAdmitBV64
  bv_decide

/-! ## HoTT Fallback Fiber & Branchless KanLock Retractions -/

/-- Canonical 14-branch HoTT fallback fiber replacing split imperative error/verdict codes.
    Every kernel transition is a total morphism `SoundArena → SoundArena` tagged with
    its landing fiber (`0 = committed`, `1..13 = hold/fallback fibers`). -/
inductive SoundFiber where
  | committed
  | fallbackNull
  | fallbackBounds
  | fallbackFormat
  | fallbackMac
  | holdImmunity
  | holdNoPower
  | holdMissingContext
  | holdIncoherent
  | fallbackPolicyTamper
  | fallbackClarify
  | fallbackIrrealis
  | fallbackSpan
  | fallbackPolicyViolation
  deriving Repr, DecidableEq, Inhabited

/-- ABI wire encoding of `SoundFiber` matching C17 `sound_fiber_t` (`0..13`). -/
def SoundFiber.code : SoundFiber → UInt32
  | .committed               => 0
  | .fallbackNull            => 1
  | .fallbackBounds          => 2
  | .fallbackFormat          => 3
  | .fallbackMac             => 4
  | .holdImmunity            => 5
  | .holdNoPower             => 6
  | .holdMissingContext      => 7
  | .holdIncoherent          => 8
  | .fallbackPolicyTamper    => 9
  | .fallbackClarify         => 10
  | .fallbackIrrealis        => 11
  | .fallbackSpan            => 12
  | .fallbackPolicyViolation => 13

/-- Branchless 64-bit Hohfeldian potestative transition producing both the new
    `(duty_pos, duty_neg)` state and its exact HoTT landing fiber. -/
def potestativeFiberStepBV64 (pos neg pow imm bit : BitVec 64)
    (gp rp gn rn : Bool) : BitVec 64 × BitVec 64 × SoundFiber :=
  let immHit := (imm &&& bit) != 0#64
  let powOk  := (pow &&& bit) != 0#64
  let dp0    := if gp then pos ||| bit else pos
  let dp     := if rp then dp0 &&& ~~~bit else dp0
  let dn0    := if gn then neg ||| bit else neg
  let dn     := if rn then dn0 &&& ~~~bit else dn0
  let cohOk  := (dp &&& dn) == 0#64
  let commit := (!immHit) && powOk && cohOk
  let nextPos := if commit then dp else pos
  let nextNeg := if commit then dn else neg
  let fiber :=
    if immHit then .holdImmunity
    else if !powOk then .holdNoPower
    else if !cohOk then .holdIncoherent
    else .committed
  (nextPos, nextNeg, fiber)

/-- 37. `SoundFiber.code` is injective across all 14 HoTT landing fibers. -/
theorem sound_fiber_code_injective (a b : SoundFiber) (h : a.code = b.code) : a = b := by
  cases a <;> cases b <;> first | rfl | cases h

/-- 38. Branchless `potestativeFiberStepBV64` agrees bit-for-bit with `gatedDualUpdateBV64`. -/
theorem potestative_fiber_agrees_with_gated_dual_bv64
    (pos neg pow imm bit : BitVec 64) (gp rp gn rn : Bool) :
    let r := potestativeFiberStepBV64 pos neg pow imm bit gp rp gn rn
    let g := gatedDualUpdateBV64 pos neg pow imm bit gp rp gn rn
    r.1 = g.1 ∧ r.2.1 = g.2 := by
  dsimp only [potestativeFiberStepBV64, gatedDualUpdateBV64]
  by_cases hGate : (((pow &&& bit) == 0#64) || ((imm &&& bit) != 0#64)) = true
  · simp only [hGate, ↓reduceIte]
    bv_decide
  · simp only [hGate, Bool.false_eq_true, ↓reduceIte]
    by_cases hBad :
        ((let dp0 := if gp then pos ||| bit else pos
          let dp  := if rp then dp0 &&& ~~~bit else dp0
          let dn0 := if gn then neg ||| bit else neg
          let dn  := if rn then dn0 &&& ~~~bit else dn0
          dp &&& dn) != 0#64) = true
    · simp only [hBad, ↓reduceIte]
      bv_decide
    · simp only [hBad, Bool.false_eq_true, ↓reduceIte]
      bv_decide

/-- 39. Every non-committed fallback fiber (`≠ .committed`) is an exact identity retraction. -/
theorem potestative_fallback_retraction_is_identity_bv64
    (pos neg pow imm bit : BitVec 64) (gp rp gn rn : Bool)
    (hFallback : (potestativeFiberStepBV64 pos neg pow imm bit gp rp gn rn).2.2 ≠ .committed) :
    let r := potestativeFiberStepBV64 pos neg pow imm bit gp rp gn rn
    r.1 = pos ∧ r.2.1 = neg := by
  dsimp only [potestativeFiberStepBV64]
  by_cases hImm : ((imm &&& bit) != 0#64) = true
  · simp [hImm]
  · by_cases hPow : ((pow &&& bit) != 0#64) = true
    · by_cases hCoh :
          ((let dp0 := if gp then pos ||| bit else pos
            let dp  := if rp then dp0 &&& ~~~bit else dp0
            let dn0 := if gn then neg ||| bit else neg
            let dn  := if rn then dn0 &&& ~~~bit else dn0
            dp &&& dn) == 0#64) = true
      · simp [potestativeFiberStepBV64, hImm, hPow, hCoh] at hFallback
      · simp [hImm, hPow, hCoh]
    · simp [hImm, hPow]

/-- 40. Both committed steps and fallback retractions preserve dual-duty coherence and KanLock bits. -/
theorem potestative_commit_preserves_coherence_and_kanlock_bv64
    (pos neg pow imm bit : BitVec 64) (gp rp gn rn : Bool)
    (hCoh : (pos &&& neg) = 0#64) :
    let r := potestativeFiberStepBV64 pos neg pow imm bit gp rp gn rn
    (r.1 &&& r.2.1) = 0#64 ∧
    (r.1 &&& imm) = (pos &&& imm) ∧
    (r.2.1 &&& imm) = (neg &&& imm) := by
  dsimp only [potestativeFiberStepBV64]
  bv_decide

/-! ## Atomic Multi-Slot Batch, Lex Specialis Priority SEARU & Chart Gluing -/

/-- Atomic 2-slot Prepared-Statement batch admission model (`bind_mask` vs `policy | KanLock`,
    disjoint target bits, low-evidence ceiling, and utterance span-hash equality). -/
def batchAdmit2BV64
    (act policyMask bindMask immMask t0 t1 : BitVec 64)
    (evidCeil0 evidCeil1 spanHash0 spanHash1 : Bool) : BitVec 64 × SoundFiber :=
  let dup := (t0 &&& t1) != 0#64
  let slot0Ok :=
    (t0 != 0#64) && ((t0 &&& ~~~bindMask) == 0#64) &&
    ((t0 &&& (policyMask ||| immMask)) == 0#64) && evidCeil0 && spanHash0
  let slot1Ok :=
    (t1 != 0#64) && ((t1 &&& ~~~bindMask) == 0#64) &&
    ((t1 &&& (policyMask ||| immMask)) == 0#64) && evidCeil1 && spanHash1
  let allSpan := spanHash0 && spanHash1
  let allPol := (!dup) && evidCeil0 && evidCeil1 &&
    (t0 != 0#64) && ((t0 &&& ~~~bindMask) == 0#64) && ((t0 &&& (policyMask ||| immMask)) == 0#64) &&
    (t1 != 0#64) && ((t1 &&& ~~~bindMask) == 0#64) && ((t1 &&& (policyMask ||| immMask)) == 0#64)
  if (!allSpan) then (act, .fallbackSpan)
  else if !(slot0Ok && slot1Ok && !dup && allPol) then (act, .fallbackPolicyViolation)
  else (act ||| t0 ||| t1, .committed)

/-- 41. Any batch rejection (policy violation, duplicate target, evid ceiling, or span hash mismatch)
    acts as an exact HoTT identity retraction on the entire 64-bit active state. -/
theorem batch_admit2_reject_is_identity_bv64
    (act policyMask bindMask immMask t0 t1 : BitVec 64)
    (evidCeil0 evidCeil1 spanHash0 spanHash1 : Bool)
    (hReject :
      (batchAdmit2BV64 act policyMask bindMask immMask t0 t1
        evidCeil0 evidCeil1 spanHash0 spanHash1).2 ≠ .committed) :
    (batchAdmit2BV64 act policyMask bindMask immMask t0 t1
      evidCeil0 evidCeil1 spanHash0 spanHash1).1 = act := by
  dsimp only [batchAdmit2BV64]
  by_cases hs : (!(spanHash0 && spanHash1)) = true
  · simp [hs]
  · simp only [hs, Bool.false_eq_true, ↓reduceIte]
    by_cases hp :
      (!(((t0 != 0#64) && ((t0 &&& ~~~bindMask) == 0#64) &&
          ((t0 &&& (policyMask ||| immMask)) == 0#64) && evidCeil0 && spanHash0) &&
         ((t1 != 0#64) && ((t1 &&& ~~~bindMask) == 0#64) &&
          ((t1 &&& (policyMask ||| immMask)) == 0#64) && evidCeil1 && spanHash1) &&
         !((t0 &&& t1) != 0#64) &&
         (!((t0 &&& t1) != 0#64) && evidCeil0 && evidCeil1 &&
          (t0 != 0#64) && ((t0 &&& ~~~bindMask) == 0#64) && ((t0 &&& (policyMask ||| immMask)) == 0#64) &&
          (t1 != 0#64) && ((t1 &&& ~~~bindMask) == 0#64) && ((t1 &&& (policyMask ||| immMask)) == 0#64)))) = true
    · simp [hp]
    · simp only [batchAdmit2BV64, hs, hp] at hReject
      simp_all

/-- 42. Any committed 2-slot batch preserves every policy and KanLock immunity bit. -/
theorem batch_admit2_commit_preserves_policy_and_kanlock_bv64
    (act policyMask bindMask immMask t0 t1 : BitVec 64)
    (evidCeil0 evidCeil1 spanHash0 spanHash1 : Bool)
    (hCommit :
      (batchAdmit2BV64 act policyMask bindMask immMask t0 t1
        evidCeil0 evidCeil1 spanHash0 spanHash1).2 = .committed) :
    let next := (batchAdmit2BV64 act policyMask bindMask immMask t0 t1
      evidCeil0 evidCeil1 spanHash0 spanHash1).1
    (next &&& policyMask) = (act &&& policyMask) ∧
    (next &&& immMask) = (act &&& immMask) := by
  dsimp only [batchAdmit2BV64] at hCommit ⊢
  by_cases hs : (!(spanHash0 && spanHash1)) = true
  · simp [hs] at hCommit
  · simp only [hs, Bool.false_eq_true, ↓reduceIte] at hCommit ⊢
    by_cases hp :
      (!(((t0 != 0#64) && ((t0 &&& ~~~bindMask) == 0#64) &&
          ((t0 &&& (policyMask ||| immMask)) == 0#64) && evidCeil0 && spanHash0) &&
         ((t1 != 0#64) && ((t1 &&& ~~~bindMask) == 0#64) &&
          ((t1 &&& (policyMask ||| immMask)) == 0#64) && evidCeil1 && spanHash1) &&
         !((t0 &&& t1) != 0#64) &&
         (!((t0 &&& t1) != 0#64) && evidCeil0 && evidCeil1 &&
          (t0 != 0#64) && ((t0 &&& ~~~bindMask) == 0#64) && ((t0 &&& (policyMask ||| immMask)) == 0#64) &&
          (t1 != 0#64) && ((t1 &&& ~~~bindMask) == 0#64) && ((t1 &&& (policyMask ||| immMask)) == 0#64)))) = true
    · simp [hp] at hCommit
    · simp only [hp, Bool.false_eq_true, ↓reduceIte]
      bv_decide

/-- Weighted SEARU energy on a mutual-attack conflict pair `(A, B)`:
    conflict penalty `65536`, non-maximal omission penalty `16384`, plus omitted priority weights. -/
def lexSpecialisPairEnergy (keepA keepB : Bool) (wA wB : BitVec 16) : BitVec 32 :=
  let conflictTerm : BitVec 32 := if keepA && keepB then 65536#32 else 0#32
  let unblockedA : BitVec 32 := if (!keepA) && (!keepB) then 16384#32 else 0#32
  let lossA : BitVec 32 := if !keepA then BitVec.zeroExtend 32 wA else 0#32
  let lossB : BitVec 32 := if !keepB then BitVec.zeroExtend 32 wB else 0#32
  conflictTerm + unblockedA + lossA + lossB

/-- 43. Under strict Lex Specialis weight `0 < wB < wA <= 255`, keeping `{A}` uniquely minimizes energy. -/
theorem lex_specialis_strict_priority_unique_min_bv
    (wA wB : BitVec 16)
    (hwBpos : 0#16 < wB) (hGt : wB < wA) (hwAmax : wA ≤ 255#16) :
    lexSpecialisPairEnergy true false wA wB < lexSpecialisPairEnergy false true wA wB ∧
    lexSpecialisPairEnergy true false wA wB < lexSpecialisPairEnergy true true wA wB ∧
    lexSpecialisPairEnergy true false wA wB < lexSpecialisPairEnergy false false wA wB := by
  dsimp only [lexSpecialisPairEnergy]
  bv_decide

/-- 44. When node `B` is pinned by `KanLock` (`keepB = true`), dropping `A` yields strictly lower
    energy than keeping both `{A, B}` even under maximum adversarial weight `wA = 255`, `wB = 1`. -/
theorem lex_specialis_kanlock_dominates_weight_bv
    (wA wB : BitVec 16) (hwAmax : wA ≤ 255#16) (hwBmax : wB ≤ 255#16) :
    lexSpecialisPairEnergy false true wA wB < lexSpecialisPairEnergy true true wA wB := by
  dsimp only [lexSpecialisPairEnergy]
  bv_decide

/-- Cross-chart Sheaf / Chart Gluing admissibility predicate across two 64-node `SoundArena`s. -/
def chartGlueValidBV64
    (srcEst srcConf srcExport dstPolicy dstBind dstImm dstImport : BitVec 64) : Bool :=
  (srcExport != 0#64) &&
  ((srcExport &&& ~~~srcEst) == 0#64) &&
  ((srcExport &&& srcConf) == 0#64) &&
  (dstImport != 0#64) &&
  ((dstImport &&& ~~~dstBind) == 0#64) &&
  ((dstImport &&& dstPolicy) == 0#64) &&
  ((dstImport &&& dstImm) == 0#64)

/-- 45. Valid chart gluing exports zero conflicts from the source chart and mutates zero
    policy or `KanLock` slots in the destination chart. -/
theorem chart_glue_valid_conflict_free_and_kanlock_safe_bv64
    (srcEst srcConf srcExport dstPolicy dstBind dstImm dstImport : BitVec 64)
    (hGlue : chartGlueValidBV64 srcEst srcConf srcExport dstPolicy dstBind dstImm dstImport = true) :
    (srcExport &&& srcEst) = srcExport ∧
    (srcExport &&& srcConf) = 0#64 ∧
    (dstImport &&& dstPolicy) = 0#64 ∧
    (dstImport &&& dstImm) = 0#64 := by
  unfold chartGlueValidBV64 at hGlue
  bv_decide

/-- 46. Gluing valid external witnesses into destination factual bindings is monotone and
    preserves every slot outside `dstBind`. -/
theorem chart_glue_monotone_bind_bv64
    (srcEst srcConf srcExport dstPolicy dstBind dstImm dstImport dstAct : BitVec 64)
    (hGlue : chartGlueValidBV64 srcEst srcConf srcExport dstPolicy dstBind dstImm dstImport = true) :
    let nextAct := dstAct ||| dstImport
    (dstAct &&& nextAct) = dstAct ∧
    (nextAct &&& ~~~dstBind) = (dstAct &&& ~~~dstBind) := by
  unfold chartGlueValidBV64 at hGlue
  bv_decide

/-! ## Pillar 1 Pure Theory: Belnap FOUR Algebra & Odd-Cycle Classical No-Go -/

abbrev B4 := BitVec 2

def b4Bot   : B4 := 0#2
def b4False : B4 := 1#2
def b4True  : B4 := 2#2
def b4Top   : B4 := 3#2

def b4Neg (x : B4) : B4 :=
  ((x &&& 2#2) >>> 1) ||| ((x &&& 1#2) <<< 1)

def b4Conf (x : B4) : B4 :=
  (b4Neg x) ^^^ 3#2

def b4AndT (x y : B4) : B4 :=
  ((x &&& y) &&& 2#2) ||| ((x ||| y) &&& 1#2)

def b4OrT (x y : B4) : B4 :=
  ((x ||| y) &&& 2#2) ||| ((x &&& y) &&& 1#2)

def b4AndK (x y : B4) : B4 :=
  x &&& y

def b4OrK (x y : B4) : B4 :=
  x ||| y

def b4LeT (x y : B4) : Bool :=
  (((x &&& 2#2) &&& ~~~(y &&& 2#2)) == 0#2) &&
  (((y &&& 1#2) &&& ~~~(x &&& 1#2)) == 0#2)

def b4LeK (x y : B4) : Bool :=
  (x &&& ~~~y) == 0#2

/-- 47. Truth negation `b4Neg` is an exact involution on the 4-valued bilattice. -/
theorem b4_neg_involutive (x : B4) : b4Neg (b4Neg x) = x := by
  unfold b4Neg
  bv_decide

/-- 48. Knowledge conflation `b4Conf` is an exact involution on the 4-valued bilattice. -/
theorem b4_conf_involutive (x : B4) : b4Conf (b4Conf x) = x := by
  unfold b4Conf b4Neg
  bv_decide

/-- 49. Truth negation and knowledge conflation commute, generating a Klein `V4` symmetry. -/
theorem b4_neg_conf_commute (x : B4) : b4Neg (b4Conf x) = b4Conf (b4Neg x) := by
  unfold b4Conf b4Neg
  bv_decide

/-- 50. Truth negation obeys De Morgan dualities for truth meet. -/
theorem b4_demorgan_truth_and (x y : B4) : b4Neg (b4AndT x y) = b4OrT (b4Neg x) (b4Neg y) := by
  unfold b4Neg b4AndT b4OrT
  bv_decide

/-- 51. Truth negation obeys De Morgan dualities for truth join. -/
theorem b4_demorgan_truth_or (x y : B4) : b4Neg (b4OrT x y) = b4AndT (b4Neg x) (b4Neg y) := by
  unfold b4Neg b4AndT b4OrT
  bv_decide

/-- 52. Knowledge conflation obeys De Morgan dualities for knowledge meet. -/
theorem b4_demorgan_conf_k_and (x y : B4) : b4Conf (b4AndK x y) = b4OrK (b4Conf x) (b4Conf y) := by
  unfold b4Conf b4Neg b4AndK b4OrK
  bv_decide

/-- 53. Knowledge conflation obeys De Morgan dualities for knowledge join. -/
theorem b4_demorgan_conf_k_or (x y : B4) : b4Conf (b4OrK x y) = b4AndK (b4Conf x) (b4Conf y) := by
  unfold b4Conf b4Neg b4AndK b4OrK
  bv_decide

/-- 54. Truth meet is monotone with respect to the knowledge order `≤_k` (interlaced property). -/
theorem b4_interlaced_andT_mono_k (x1 x2 y1 y2 : B4)
    (hx : b4LeK x1 x2 = true) (hy : b4LeK y1 y2 = true) :
    b4LeK (b4AndT x1 y1) (b4AndT x2 y2) = true := by
  unfold b4LeK b4AndT at *
  bv_decide

/-- 55. Truth join is monotone with respect to the knowledge order `≤_k` (interlaced property). -/
theorem b4_interlaced_orT_mono_k (x1 x2 y1 y2 : B4)
    (hx : b4LeK x1 x2 = true) (hy : b4LeK y1 y2 = true) :
    b4LeK (b4OrT x1 y1) (b4OrT x2 y2) = true := by
  unfold b4LeK b4OrT at *
  bv_decide

/-- 56. Knowledge meet is monotone with respect to the truth order `≤_t` (interlaced property). -/
theorem b4_interlaced_andK_mono_t (x1 x2 y1 y2 : B4)
    (hx : b4LeT x1 x2 = true) (hy : b4LeT y1 y2 = true) :
    b4LeT (b4AndK x1 y1) (b4AndK x2 y2) = true := by
  unfold b4LeT b4AndK at *
  bv_decide

/-- 57. Knowledge join is monotone with respect to the truth order `≤_t` (interlaced property). -/
theorem b4_interlaced_orK_mono_t (x1 x2 y1 y2 : B4)
    (hx : b4LeT x1 x2 = true) (hy : b4LeT y1 y2 = true) :
    b4LeT (b4OrK x1 y1) (b4OrK x2 y2) = true := by
  unfold b4LeT b4OrK at *
  bv_decide

/-- 58. Truth negation reverses the truth order `≤_t` while strictly preserving knowledge `≤_k`. -/
theorem b4_neg_reverses_t_preserves_k (x y : B4) :
    (b4LeT x y = true → b4LeT (b4Neg y) (b4Neg x) = true) ∧
    (b4LeK x y = true → b4LeK (b4Neg x) (b4Neg y) = true) := by
  unfold b4LeT b4LeK b4Neg
  bv_decide

/-- 59. Conflation preserves the truth order `≤_t` while reversing the knowledge order `≤_k`. -/
theorem b4_conf_preserves_t_reverses_k (x y : B4) :
    (b4LeT x y = true → b4LeT (b4Conf x) (b4Conf y) = true) ∧
    (b4LeK x y = true → b4LeK (b4Conf y) (b4Conf x) = true) := by
  unfold b4LeT b4LeK b4Conf b4Neg
  bv_decide

/-- 60. Bilattice truth and knowledge operations distribute over each other. -/
theorem b4_distributive_tk (x y z : B4) :
    b4AndT x (b4OrK y z) = b4OrK (b4AndT x y) (b4AndT x z) := by
  unfold b4AndT b4OrK
  bv_decide

def IsClassicalAttackModel {n : Nat} (atk : Fin n → Fin n → Bool) (val : Fin n → Bool) : Prop :=
  ∀ u, val u = true ↔ ∀ v, atk v u = true → val v = false

def oddCycleAtk (k : Nat) (u v : Fin (2 * k + 1)) : Bool :=
  ((u.val + 1) % (2 * k + 1) == v.val)

def evenCycleAtk (k : Nat) (u v : Fin (2 * (k + 1))) : Bool :=
  ((u.val + 1) % (2 * (k + 1)) == v.val)

private theorem oddCycle_step_flip (k : Nat) (val : Fin (2 * k + 1) → Bool)
    (hModel : IsClassicalAttackModel (oddCycleAtk k) val)
    (m : Nat) (hm : m + 1 < 2 * k + 1) :
    val ⟨m + 1, hm⟩ = !(val ⟨m, Nat.lt_of_succ_lt hm⟩) := by
  have hMod : (m + 1) % (2 * k + 1) = m + 1 := Nat.mod_eq_of_lt hm
  have hAtk : oddCycleAtk k ⟨m, Nat.lt_of_succ_lt hm⟩ ⟨m + 1, hm⟩ = true := by
    simp [oddCycleAtk, hMod]
  have hIff := hModel ⟨m + 1, hm⟩
  cases hv : val ⟨m, Nat.lt_of_succ_lt hm⟩
  · have hAll : ∀ v : Fin (2 * k + 1), oddCycleAtk k v ⟨m + 1, hm⟩ = true → val v = false := by
      intro v hvAtk
      simp only [oddCycleAtk, beq_iff_eq] at hvAtk
      have hvLt : v.val < 2 * k + 1 := v.isLt
      have hvm : v.val = m := by
        by_cases hWrap : v.val + 1 < 2 * k + 1
        · rw [Nat.mod_eq_of_lt hWrap] at hvAtk
          omega
        · have hEq : v.val + 1 = 2 * k + 1 := by omega
          rw [hEq, Nat.mod_self] at hvAtk
          omega
      have hSub : v = ⟨m, Nat.lt_of_succ_lt hm⟩ := Fin.ext hvm
      rw [hSub, hv]
    have hTrue : val ⟨m + 1, hm⟩ = true := hIff.mpr hAll
    simp [hTrue]
  · cases hNext : val ⟨m + 1, hm⟩
    · rfl
    · have hFalse := hIff.mp hNext ⟨m, Nat.lt_of_succ_lt hm⟩ hAtk
      rw [hv] at hFalse
      cases hFalse

private theorem oddCycle_wrap_flip (k : Nat) (val : Fin (2 * k + 1) → Bool)
    (hModel : IsClassicalAttackModel (oddCycleAtk k) val) :
    val ⟨0, by omega⟩ = !(val ⟨2 * k, by omega⟩) := by
  have hAtk : oddCycleAtk k ⟨2 * k, by omega⟩ ⟨0, by omega⟩ = true := by
    simp [oddCycleAtk]
  have hIff := hModel ⟨0, by omega⟩
  cases hv : val ⟨2 * k, by omega⟩
  · have hAll : ∀ v : Fin (2 * k + 1), oddCycleAtk k v ⟨0, by omega⟩ = true → val v = false := by
      intro v hvAtk
      simp only [oddCycleAtk, beq_iff_eq] at hvAtk
      have hvLt : v.val < 2 * k + 1 := v.isLt
      have hv2k : v.val = 2 * k := by
        by_cases hWrap : v.val + 1 < 2 * k + 1
        · rw [Nat.mod_eq_of_lt hWrap] at hvAtk
          omega
        · omega
      have hSub : v = ⟨2 * k, by omega⟩ := Fin.ext hv2k
      rw [hSub, hv]
    have hTrue : val ⟨0, by omega⟩ = true := hIff.mpr hAll
    exact hTrue
  · cases h0 : val ⟨0, by omega⟩
    · rfl
    · have hFalse := hIff.mp h0 ⟨2 * k, by omega⟩ hAtk
      rw [hv] at hFalse
      cases hFalse

private theorem oddCycle_parity (k : Nat) (val : Fin (2 * k + 1) → Bool)
    (hModel : IsClassicalAttackModel (oddCycleAtk k) val) :
    ∀ m (hm : m < 2 * k + 1),
      val ⟨m, hm⟩ = if m % 2 == 0 then val ⟨0, by omega⟩ else !(val ⟨0, by omega⟩) := by
  intro m
  induction m with
  | zero =>
      intro _
      simp
  | succ m ih =>
      intro hm
      have hStep := oddCycle_step_flip k val hModel m hm
      have hPrev := ih (Nat.lt_of_succ_lt hm)
      rw [hStep, hPrev]
      by_cases hEven : m % 2 = 0
      · have hSucc : (m + 1) % 2 = 1 := by omega
        simp [hEven, hSucc]
      · have hOdd : m % 2 = 1 := by omega
        have hSucc : (m + 1) % 2 = 0 := by omega
        simp [hOdd, hSucc]

/-- 61. Universal Odd-Cycle Classical 2-Valued No-Go Theorem:
    For EVERY `k : Nat`, an unblocked directed cycle of length `2k + 1` admits ZERO
    classical 2-valued total extension models, forcing 4-valued bilattice semantics. -/
theorem odd_cycle_classical_nogo (k : Nat) (val : Fin (2 * k + 1) → Bool) :
    ¬ IsClassicalAttackModel (oddCycleAtk k) val := by
  intro hModel
  have hPar := oddCycle_parity k val hModel (2 * k) (by omega)
  have hEven : (2 * k) % 2 = 0 := by omega
  simp only [hEven, beq_self_eq_true, ↓reduceIte] at hPar
  have hWrap := oddCycle_wrap_flip k val hModel
  rw [hPar] at hWrap
  cases h0 : val ⟨0, by omega⟩
  · simp only [Fin.zero_eta] at h0 hWrap
    rw [h0] at hWrap
    cases hWrap
  · simp only [Fin.zero_eta] at h0 hWrap
    rw [h0] at hWrap
    cases hWrap

private theorem evenCycle_mod_model (k : Nat) (b : Nat) (hb : b < 2) :
    IsClassicalAttackModel (evenCycleAtk k) (fun i => i.val % 2 == b) := by
  intro u
  constructor
  · intro hu v hvAtk
    simp only [beq_iff_eq] at hu
    simp only [evenCycleAtk, beq_iff_eq] at hvAtk
    simp only [beq_eq_false_iff_ne, ne_eq]
    have hvLt : v.val < 2 * (k + 1) := v.isLt
    by_cases hWrap : v.val + 1 < 2 * (k + 1)
    · rw [Nat.mod_eq_of_lt hWrap] at hvAtk
      omega
    · have hEq : v.val + 1 = 2 * (k + 1) := by omega
      rw [hEq, Nat.mod_self] at hvAtk
      have hvVal : v.val = 2 * k + 1 := by omega
      have huVal : u.val = 0 := hvAtk.symm
      omega
  · intro hAll
    simp only [beq_iff_eq]
    let pVal := (u.val + (2 * k + 1)) % (2 * (k + 1))
    let p : Fin (2 * (k + 1)) := ⟨pVal, Nat.mod_lt _ (by omega)⟩
    have hpAtk : evenCycleAtk k p u = true := by
      simp only [evenCycleAtk, p, pVal, beq_iff_eq]
      have huLt : u.val < 2 * (k + 1) := u.isLt
      by_cases h0 : u.val = 0
      · have hP : (0 + (2 * k + 1)) % (2 * (k + 1)) = 2 * k + 1 := by
          simpa using Nat.mod_eq_of_lt (show 2 * k + 1 < 2 * (k + 1) by omega)
        rw [h0, hP]
        have hSum : 2 * k + 1 + 1 = 2 * (k + 1) := by omega
        rw [hSum, Nat.mod_self]
      · have hMod : (u.val + (2 * k + 1)) % (2 * (k + 1)) = u.val - 1 := by
          have hSum : u.val + (2 * k + 1) = (u.val - 1) + 2 * (k + 1) := by omega
          rw [hSum, Nat.add_mod_right, Nat.mod_eq_of_lt (by omega)]
        rw [hMod]
        have hBack : u.val - 1 + 1 = u.val := by omega
        rw [hBack, Nat.mod_eq_of_lt huLt]
    have hpFalse := hAll p hpAtk
    simp only [beq_eq_false_iff_ne, ne_eq, p, pVal] at hpFalse
    have huLt : u.val < 2 * (k + 1) := u.isLt
    by_cases h0 : u.val = 0
    · have hP : (0 + (2 * k + 1)) % (2 * (k + 1)) = 2 * k + 1 := by
        simpa using Nat.mod_eq_of_lt (show 2 * k + 1 < 2 * (k + 1) by omega)
      rw [h0, hP] at hpFalse
      rw [h0]
      omega
    · have hMod : (u.val + (2 * k + 1)) % (2 * (k + 1)) = u.val - 1 := by
        have hSum : u.val + (2 * k + 1) = (u.val - 1) + 2 * (k + 1) := by omega
        rw [hSum, Nat.add_mod_right, Nat.mod_eq_of_lt (by omega)]
      rw [hMod] at hpFalse
      omega

/-- 62. Even-Cycle Classical Phase Separation:
    Every directed cycle of length `2(k + 1)` admits AT LEAST two distinct classical 2-valued models. -/
theorem even_cycle_classical_two_models (k : Nat) :
    ∃ (valA valB : Fin (2 * (k + 1)) → Bool),
      IsClassicalAttackModel (evenCycleAtk k) valA ∧
      IsClassicalAttackModel (evenCycleAtk k) valB ∧
      valA ≠ valB := by
  let valA : Fin (2 * (k + 1)) → Bool := fun i => i.val % 2 == 0
  let valB : Fin (2 * (k + 1)) → Bool := fun i => i.val % 2 == 1
  refine ⟨valA, valB, evenCycle_mod_model k 0 (by omega), evenCycle_mod_model k 1 (by omega), ?_⟩
  intro hEq
  have h0 := congrFun hEq ⟨0, by omega⟩
  simp [valA, valB] at h0

def cycle3Nodes : List GraphNode :=
  [ { id := 0, tag := 1, rank := 2, bridge := true, inhibitors := [2] }
  , { id := 1, tag := 1, rank := 2, bridge := true, inhibitors := [0] }
  , { id := 2, tag := 1, rank := 2, bridge := true, inhibitors := [1] } ]

/-- 63. While 3-cycles collapse classically, `SOUND`'s 4-valued bilattice stabilizes in an
    exact mutual fixpoint `I* = []`, `P* = [0, 1, 2]` marking all 3 nodes as `.conflict`. -/
theorem cycle3_bilattice_conflict_fixpoint :
    evalIStar 3 cycle3Nodes = [] ∧
    evalPStar 3 cycle3Nodes = [0, 1, 2] ∧
    cycle3Nodes.map (evalNode 3 cycle3Nodes) = [.conflict, .conflict, .conflict] := by
  decide

/-- 64. Monotonicity vs. Defeasibility No-Go:
    No monotone consequence operator can simultaneously support adding a new premise/norm
    that retracts a previously entailed conclusion. -/
theorem monotonicity_vs_defeasible_nogo
    (F : (Nat → Bool) → (Nat → Bool))
    (hMono : ∀ A B, (∀ x, A x = true → B x = true) → (∀ x, F A x = true → F B x = true))
    (A B : Nat → Bool) (hSub : ∀ x, A x = true → B x = true) (u : Nat) :
    ¬ (F A u = true ∧ F B u = false) := by
  intro ⟨hFA, hFB⟩
  have hTrue : F B u = true := hMono A B hSub u hFA
  rw [hFB] at hTrue
  cases hTrue

def defeasibleBaseNodes : List GraphNode :=
  [ { id := 0, tag := 1, rank := 2, bridge := true, inhibitors := [1] }
  , { id := 1, tag := 1, rank := 2, bridge := true, disabled := true } ]

def defeasibleRebuttedNodes : List GraphNode :=
  [ { id := 0, tag := 1, rank := 2, bridge := true, inhibitors := [1] }
  , { id := 1, tag := 1, rank := 2, bridge := true, disabled := false } ]

/-- 65. Witness that `SOUND` escapes monotone limits: enabling attacker node `1` cleanly
    retracts node `0` from `.accept` to `.reject`. -/
theorem bilattice_defeasible_rebuttal_witness :
    evalNode 3 defeasibleBaseNodes { id := 0, tag := 1, rank := 2, bridge := true, inhibitors := [1] } = .accept ∧
    evalNode 3 defeasibleRebuttedNodes { id := 0, tag := 1, rank := 2, bridge := true, inhibitors := [1] } = .reject := by
  decide

end Sound.Bilattice
