/-
  SOUND v3.0.0 — Pure Epistemic Schema ABI Compiler & Golden Fixture Emitter
  ===========================================================================
  Consumes domain-free `../FLOW/{core,sdk}` (`Flow.Core`, `Flow.Schema`, `Flow.Morphism`)
  plus local formal bilattice theorems in `Sound/Bilattice.lean`.
  This file configures the 256-slot `16,384B` `soundArenaSchema`, declares the 10
  canonical golden soundness cases, emits `build/sound_schema_abi.h` (0 `memcpy`),
  and emits canonical packed `.flwa` (`"FLWA"`), `.fir2` (`"FIR2"`), and
  `golden_manifest.json` with ZERO local definitions of theorems, lemmas, or axioms.
-/

import Flow.Core
import Flow.Schema
import Flow.Morphism
import Sound.Bilattice

namespace Sound

def version       : String := "3.0.0"
def cellBytes     : Nat    := 64
def arenaCells    : Nat    := 256
def arenaBytes    : Nat    := arenaCells * cellBytes
def sndMagic      : UInt32 := 0x32444E53 -- "SND2"
def sndVersion    : UInt32 := 2
def fnvOffset     : UInt64 := 0xCBF29CE484222325
def leafThreshold : UInt8  := 3

/-! ## Part 1: SSOT 256-Slot (`16,384B`) Arena & Full-512b C17 Schema Config -/

def soundImageHeaderLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "img_hdr" "SoundImageHeader64" "img_hdr" [
    ("magic", .u32, 1), ("version", .u32, 1), ("num_nodes", .u32, 1), ("frame_flags", .u32, 1),
    ("phys_len", .u32, 1), ("phys_head", .u32, 1), ("reserved_hdr", .u64, 1),
    ("phys_hash", .u64, 1), ("schema_hash", .u64, 1), ("merkle_root", .u64, 1),
    ("reserved_tail", .u64, 1)
  ]

def soundNormativeLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "norm" "SoundNormative64" "norm" [
    ("revoked_mask", .u64, 1), ("penumbra_mask", .u64, 1),
    ("duty_pos_mask", .u64, 1), ("duty_neg_mask", .u64, 1),
    ("power_mask", .u64, 1), ("immunity_mask", .u64, 1),
    ("voidable_mask", .u64, 1), ("rescission_power_mask", .u64, 1)
  ]

def soundEvalSummaryLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "eval_out" "SoundEvalSummary64" "eval_out" [
    ("node_mask", .u64, 1), ("i_star", .u64, 1), ("p_star", .u64, 1),
    ("established_mask", .u64, 1), ("defeated_mask", .u64, 1),
    ("conflict_mask", .u64, 1), ("penumbra_mask", .u64, 1),
    ("eval_flags", .u32, 1), ("eval_epoch", .u32, 1)
  ]

def soundEvalStatusLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "eval_status" "SoundEvalStatus64" "eval_status" [
    ("status", .u8, 64)
  ]

def soundEvalReasonsLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "eval_reasons" "SoundEvalReasons64" "eval_reasons" [
    ("reasons", .u32, 16)
  ]

def soundNodeMetaLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "node_meta" "SoundNodeMeta64" "node_meta" [
    ("sort", .u8, 1), ("evid", .u8, 1), ("aspect", .u8, 1), ("num_clauses", .u8, 1),
    ("flags", .u32, 1), ("presup_mask", .u64, 1), ("attack_mask", .u64, 1),
    ("span_start", .u32, 1), ("span_end", .u32, 1), ("label_bytes", .u8, 32)
  ]

def soundNodeClausesLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "node_clauses" "SoundNodeClauses64" "node_clauses" [
    ("clauses", .u64, 8)
  ]

def soundPhysEventLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "phys_log" "SoundPhysEvent64" "phys_log" [
    ("event_id", .u32, 1), ("kind", .u32, 1), ("target_node", .u32, 1), ("duty_slot", .u32, 1),
    ("flags", .u32, 1), ("span_start", .u32, 1), ("span_end", .u32, 1), ("reserved0", .u32, 1),
    ("payload_hash", .u64, 1), ("prev_chain_hash", .u64, 1), ("chain_hash", .u64, 1),
    ("reserved1", .u64, 1)
  ]

def soundCandidateLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "candidate" "SoundCandidate64" "candidate" [
    ("target_node", .u32, 1), ("force", .u8, 1), ("sort", .u8, 1),
    ("evid", .u8, 1), ("aspect", .u8, 1),
    ("span_start", .u32, 1), ("span_end", .u32, 1), ("node_flags", .u32, 1), ("reserved0", .u32, 1),
    ("presup_mask", .u64, 1), ("premise_mask", .u64, 1), ("attack_mask", .u64, 1),
    ("payload_hash", .u64, 1), ("reserved1", .u64, 1)
  ]

def soundForestCtrlLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "forest_ctrl" "SoundForestCtrl64" "forest_ctrl" [
    ("utterance_bytes", .u32, 1), ("ambiguity_flags", .u32, 1),
    ("unresolved_anaphora", .u32, 1), ("num_candidates", .u32, 1),
    ("last_fiber", .u32, 1), ("last_admitted_node", .u32, 1),
    ("ctrl_flags", .u32, 1), ("last_reject_reason", .u32, 1),
    ("policy_mask", .u64, 1), ("bind_mask", .u64, 1),
    ("policy_hash", .u64, 1), ("fiber_epoch", .u32, 1), ("reserved", .u32, 1)
  ]

def soundSearuRepairLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "searu_repair" "SoundSearuRepair64" "searu_repair" [
    ("pinned_mask", .u64, 1), ("candidate_mask", .u64, 1),
    ("retracted_mask", .u64, 1), ("best_energy", .u64, 1),
    ("iters_used", .u32, 1), ("converged", .u32, 1),
    ("seed", .u64, 1), ("reserved", .u64, 2)
  ]

def soundArenaSchema : Flow.Schema.ArenaSpec where
  cPrefix         := "sound_schema"
  headerGuard     := "SOUND_SCHEMA_ABI_H"
  totalCells      := arenaCells
  cCellUnionName  := "SoundCell64"
  cArenaMapName   := "SoundArenaMap"
  cArenaUnionName := "SoundArena"
  regions := [
    { name := "img_hdr",      baseSlot := 0,   count := 1,  cSlotMacro := "SOUND_SLOT_IMG_HDR",           mapDecl := "SoundImageHeader64 img_hdr;" },
    { name := "norm",         baseSlot := 1,   count := 1,  cSlotMacro := "SOUND_SLOT_NORM",              mapDecl := "SoundNormative64 norm;" },
    { name := "eval_out",     baseSlot := 2,   count := 1,  cSlotMacro := "SOUND_SLOT_EVAL_OUT",          mapDecl := "SoundEvalSummary64 eval_out;" },
    { name := "eval_status",  baseSlot := 3,   count := 1,  cSlotMacro := "SOUND_SLOT_EVAL_STATUS",       mapDecl := "SoundEvalStatus64 eval_status;" },
    { name := "eval_reasons", baseSlot := 4,   count := 4,  cSlotMacro := "SOUND_SLOT_EVAL_REASONS_BASE", cCountMacro := "SOUND_EVAL_REASONS_CELLS", mapDecl := "SoundEvalReasons64 eval_reasons[SOUND_EVAL_REASONS_CELLS];" },
    { name := "node_meta",    baseSlot := 8,   count := 64, cSlotMacro := "SOUND_SLOT_NODE_META_BASE",    cCountMacro := "SOUND_MAX_NODES",          mapDecl := "SoundNodeMeta64 node_meta[SOUND_MAX_NODES];" },
    { name := "node_clauses", baseSlot := 72,  count := 64, cSlotMacro := "SOUND_SLOT_NODE_CLAUSES_BASE", cCountMacro := "SOUND_NODE_CLAUSES_SLOTS", mapDecl := "SoundNodeClauses64 node_clauses[SOUND_NODE_CLAUSES_SLOTS];" },
    { name := "phys_log",     baseSlot := 136, count := 64, cSlotMacro := "SOUND_SLOT_PHYS_LOG_BASE",     cCountMacro := "SOUND_MAX_EVENTS",         mapDecl := "SoundPhysEvent64 phys_log[SOUND_MAX_EVENTS];" },
    { name := "candidate",    baseSlot := 200, count := 16, cSlotMacro := "SOUND_SLOT_CANDIDATE_BASE",    cCountMacro := "SOUND_MAX_CANDIDATES",     mapDecl := "SoundCandidate64 candidate[SOUND_MAX_CANDIDATES];" },
    { name := "forest_ctrl",  baseSlot := 216, count := 1,  cSlotMacro := "SOUND_SLOT_FOREST_CTRL",       mapDecl := "SoundForestCtrl64 forest_ctrl;" },
    { name := "searu_repair", baseSlot := 217, count := 1,  cSlotMacro := "SOUND_SLOT_SEARU_REPAIR",      mapDecl := "SoundSearuRepair64 searu_repair;" },
    { name := "flow_aux",     baseSlot := 218, count := 38, cSlotMacro := "SOUND_SLOT_FLOW_AUX_BASE",     cCountMacro := "SOUND_MAX_FLOW_AUX",       mapDecl := "uint64_t flow_aux[SOUND_MAX_FLOW_AUX][8];" }
  ]
  layouts := [
    soundImageHeaderLayout, soundNormativeLayout, soundEvalSummaryLayout,
    soundEvalStatusLayout, soundEvalReasonsLayout, soundNodeMetaLayout,
    soundNodeClausesLayout, soundPhysEventLayout, soundCandidateLayout,
    soundForestCtrlLayout, soundSearuRepairLayout
  ]

def emitSchemaOnly : IO Bool :=
  soundArenaSchema.emitC17HeaderChecked "build/sound_schema_abi.h"

/-! ## Part 2: Epistemic/Normative Graph Spec & 16,384B Sealed `.snd` Encoder -/

inductive SortTag where
  | factual
  | normative
  | institutional
  deriving Repr, DecidableEq, Inhabited

def SortTag.toUInt8 : SortTag → UInt8
  | .factual       => 0
  | .normative     => 1
  | .institutional => 2

inductive EvidentialSource where
  | directWitness
  | sensory
  | enactedNorm
  | inferential
  | reportative
  | assumed
  deriving Repr, DecidableEq, Inhabited

def EvidentialSource.toUInt8 : EvidentialSource → UInt8
  | .directWitness => 0
  | .sensory       => 1
  | .enactedNorm   => 2
  | .inferential   => 3
  | .reportative   => 4
  | .assumed       => 5

inductive EventAspect where
  | state
  | activity
  | accomplishment
  | achievement
  deriving Repr, DecidableEq, Inhabited

def EventAspect.toUInt8 : EventAspect → UInt8
  | .state          => 0
  | .activity       => 1
  | .accomplishment => 2
  | .achievement    => 3

structure EpistemicNode where
  id              : Nat
  label           : String
  sort            : SortTag
  source          : EvidentialSource
  aspect          : EventAspect := .state
  isConstitutive  : Bool := false
  revoked         : Bool := false
  penumbraConcept : Option String := none
  presupDeps      : List Nat := []
  supports        : List (List Nat) := [[]]
  attackers       : List Nat := []
  spanStart       : Nat := 0
  spanEnd         : Nat := 0
  deriving Repr, DecidableEq, Inhabited

structure NormativeSlots where
  revokedMask         : UInt64 := 0
  penumbraMask        : UInt64 := 0
  dutyPosMask         : UInt64 := 0
  dutyNegMask         : UInt64 := 0
  powerMask           : UInt64 := 0
  immunityMask        : UInt64 := 0
  voidableMask        : UInt64 := 0
  rescissionPowerMask : UInt64 := 0
  deriving Repr, DecidableEq, Inhabited

structure GoldenCase where
  name  : String
  nodes : List EpistemicNode
  slots : NormativeSlots
  deriving Repr, DecidableEq, Inhabited

def toBilatticeNode (u : EpistemicNode) : Sound.Bilattice.GraphNode :=
  { id         := u.id
  , tag        := u.sort.toUInt8
  , rank       := u.source.toUInt8
  , bridge     := (u.source == .enactedNorm) || u.isConstitutive
  , disabled   := u.revoked
  , halted     := u.penumbraConcept.isSome
  , reqs       := u.presupDeps
  , clauses    := u.supports
  , inhibitors := u.attackers }

def revMaskOf (nodes : List EpistemicNode) : UInt64 :=
  nodes.foldl
    (fun m u => if u.revoked then m ||| ((1 : UInt64) <<< (UInt64.ofNat (u.id % 64))) else m) 0

def penMaskOf (nodes : List EpistemicNode) : UInt64 :=
  nodes.foldl
    (fun m u => if u.penumbraConcept.isSome then m ||| ((1 : UInt64) <<< (UInt64.ofNat (u.id % 64))) else m) 0

private def u64Kv (name : String) (v : UInt64) : String × Int :=
  (name, Int.ofNat v.toNat)

private def labelEntries (label : String) : List (String × Int) :=
  let raw := label.toUTF8
  Id.run do
    let mut out : List (String × Int) := []
    for k in [:32] do
      let b : UInt8 := if h : k + 1 < 32 ∧ k < raw.size then raw[k]'h.2 else 0
      out := (s!"label_bytes[{k}]", Int.ofNat b.toNat) :: out
    return out.reverse

private def clauseEntries (supports : List (List Nat)) : List (String × Int) :=
  Id.run do
    let mut out : List (String × Int) := []
    for cIdx in [:8] do
      let cmask :=
        match supports[cIdx]? with
        | some cl => Sound.Bilattice.listToMask64 cl
        | none    => 0
      out := u64Kv s!"clauses[{cIdx}]" cmask :: out
    return out.reverse

def buildSealedArenaCells (nodes : List EpistemicNode) (slots : NormativeSlots) :
    Option (Array Flow.Cell64) := do
  if nodes.length > 64 then
    none
  let mut cells : Array Flow.Cell64 := Id.run do
    let mut arr : Array Flow.Cell64 := #[]
    for _ in [:arenaCells] do
      arr := arr.push {}
    return arr
  let revMask := slots.revokedMask ||| revMaskOf nodes
  let penMask := slots.penumbraMask ||| penMaskOf nodes
  let normCell ← soundNormativeLayout.packCell [
    u64Kv "revoked_mask" revMask,
    u64Kv "penumbra_mask" penMask,
    u64Kv "duty_pos_mask" slots.dutyPosMask,
    u64Kv "duty_neg_mask" slots.dutyNegMask,
    u64Kv "power_mask" slots.powerMask,
    u64Kv "immunity_mask" slots.immunityMask,
    u64Kv "voidable_mask" slots.voidableMask,
    u64Kv "rescission_power_mask" slots.rescissionPowerMask
  ]
  cells := cells.set! 1 normCell
  let mut idx : Nat := 0
  for u in nodes do
    let nc := min u.supports.length 8
    let nflags : Nat := if u.isConstitutive then 1 else 0
    let metaVals : List (String × Int) :=
      [ ("sort", Int.ofNat u.sort.toUInt8.toNat)
      , ("evid", Int.ofNat u.source.toUInt8.toNat)
      , ("aspect", Int.ofNat u.aspect.toUInt8.toNat)
      , ("num_clauses", Int.ofNat nc)
      , ("flags", Int.ofNat nflags)
      , u64Kv "presup_mask" (Sound.Bilattice.listToMask64 u.presupDeps)
      , u64Kv "attack_mask" (Sound.Bilattice.listToMask64 u.attackers)
      , ("span_start", Int.ofNat u.spanStart)
      , ("span_end", Int.ofNat u.spanEnd) ] ++ labelEntries u.label
    let metaCell ← soundNodeMetaLayout.packCell metaVals
    let clCell ← soundNodeClausesLayout.packCell (clauseEntries u.supports)
    cells := cells.set! (8 + idx) metaCell
    cells := cells.set! (72 + idx) clCell
    idx := idx + 1
  let mut tailBytes := ByteArray.empty
  for slot in [1:arenaCells] do
    tailBytes := tailBytes ++ (cells[slot]!).toBytes
  let merkle := Flow.fnv1a64Bytes tailBytes
  let hdrCell ← soundImageHeaderLayout.packCell [
    ("magic", Int.ofNat sndMagic.toNat),
    ("version", Int.ofNat sndVersion.toNat),
    ("num_nodes", Int.ofNat nodes.length),
    ("frame_flags", 0),
    ("phys_len", 0),
    ("phys_head", 0),
    ("reserved_hdr", 0),
    u64Kv "phys_hash" fnvOffset,
    u64Kv "schema_hash" soundArenaSchema.schemaHash,
    u64Kv "merkle_root" merkle,
    ("reserved_tail", 0)
  ]
  return cells.set! 0 hdrCell

def buildSealedArenaBytes (nodes : List EpistemicNode) (slots : NormativeSlots) : Option ByteArray := do
  let cells ← buildSealedArenaCells nodes slots
  let mut out := ByteArray.empty
  for c in cells do
    out := out ++ c.toBytes
  return out

private def pushVarU64 (buf : ByteArray) (v0 : UInt64) : ByteArray :=
  Id.run do
    let mut out := buf
    let mut v := v0
    for _ in [:10] do
      let lo : UInt8 := UInt8.ofNat (v.toNat &&& 0x7F)
      v := v >>> 7
      if v == 0 then
        out := out.push lo
        return out
      out := out.push (lo ||| 0x80)
    return out

private def pushVarU32 (buf : ByteArray) (v : Nat) : ByteArray :=
  pushVarU64 buf (UInt64.ofNat v)

private def flwaWordTag (v : UInt64) : Nat :=
  if v == 0 then 0
  else if v ≤ 0xFF then 1
  else if v ≤ 0xFFFFFFFF then 2
  else 3

private def pushTaggedU64 (buf : ByteArray) (v : UInt64) (tag : Nat) : ByteArray :=
  match tag with
  | 0 => buf
  | 1 => buf.push (UInt8.ofNat (v.toNat &&& 0xFF))
  | 2 => Flow.pushLE32 buf (UInt32.ofNat (v.toNat &&& 0xFFFFFFFF))
  | _ => Flow.pushLE64 buf v

private def cellWords (c : Flow.Cell64) : List UInt64 :=
  [c.w0, c.w1, c.w2, c.w3, c.w4, c.w5, c.w6, c.w7]

def packArenaCellsFlwa (cells : Array Flow.Cell64) : Option ByteArray := do
  if cells.size == 0 || cells.size > 65536 then
    none
  let mut body := ByteArray.empty
  let mut nzCells : Nat := 0
  let mut nextExpected : Nat := 0
  for idx in [:cells.size] do
    let words := cellWords (cells[idx]!)
    let mut desc : Nat := 0
    for wIdx in [:8] do
      let w := words[wIdx]!
      desc := desc ||| ((flwaWordTag w) <<< (wIdx * 2))
    if desc == 0 then
      continue
    let delta := idx - nextExpected
    body := pushVarU32 body delta
    body := Flow.pushLE16 body (UInt16.ofNat desc)
    for wIdx in [:8] do
      let w := words[wIdx]!
      let tag := (desc >>> (wIdx * 2)) &&& 3
      body := pushTaggedU64 body w tag
    nextExpected := idx + 1
    nzCells := nzCells + 1
  let mut hdr := ByteArray.empty
  hdr := Flow.pushLE32 hdr 0x41574C46 -- "FLWA"
  hdr := Flow.pushLE32 hdr 1
  hdr := Flow.pushLE32 hdr (UInt32.ofNat cells.size)
  hdr := Flow.pushLE32 hdr (UInt32.ofNat nzCells)
  let fullBody := hdr ++ body
  let trailer := Flow.fnv1a64Bytes fullBody
  return Flow.pushLE64 fullBody trailer

def goldenCases : List GoldenCase :=
  [ { name := "case1_hearsay"
    , slots := {}
    , nodes :=
        [ { id := 0, label := "wang_stopped_dumping", sort := .factual, source := .reportative, aspect := .achievement, presupDeps := [1], supports := [[]] }
        , { id := 1, label := "wang_previously_dump", sort := .factual, source := .reportative, aspect := .activity, supports := [[]] } ] }
  , { name := "case1_corroborated"
    , slots := {}
    , nodes :=
        [ { id := 0, label := "wang_stopped_dumping", sort := .factual, source := .reportative, aspect := .achievement, presupDeps := [1], supports := [[2]] }
        , { id := 1, label := "wang_previously_dump", sort := .factual, source := .reportative, aspect := .activity, supports := [[3]] }
        , { id := 2, label := "direct_neighbor_wit", sort := .factual, source := .directWitness, aspect := .achievement }
        , { id := 3, label := "soil_sensor_log", sort := .factual, source := .sensory, aspect := .state } ] }
  , { name := "case2_auction_pre"
    , slots := { dutyPosMask := 0b1100, powerMask := 0b0100, immunityMask := 0b1000 }
    , nodes :=
        [ { id := 0, label := "alice_bid_intent", sort := .factual, source := .directWitness, aspect := .achievement }
        , { id := 1, label := "contract_formed", sort := .institutional, source := .directWitness, aspect := .achievement, isConstitutive := true, supports := [[0]] }
        , { id := 2, label := "alice_pay_duty", sort := .normative, source := .directWitness, aspect := .state, isConstitutive := true, supports := [[1]] }
        , { id := 3, label := "venue_cleanup_duty", sort := .normative, source := .enactedNorm, aspect := .state, supports := [[1], []] } ] }
  , { name := "case2_auction_post"
    , slots := { dutyPosMask := 0b1000, powerMask := 0b0100, immunityMask := 0b1000 }
    , nodes :=
        [ { id := 0, label := "alice_bid_intent", sort := .factual, source := .directWitness, aspect := .achievement, revoked := true }
        , { id := 1, label := "contract_formed", sort := .institutional, source := .directWitness, aspect := .achievement, isConstitutive := true, supports := [[0]] }
        , { id := 2, label := "alice_pay_duty", sort := .normative, source := .directWitness, aspect := .state, isConstitutive := true, supports := [[1]] }
        , { id := 3, label := "venue_cleanup_duty", sort := .normative, source := .enactedNorm, aspect := .state, supports := [[1], []] } ] }
  , { name := "case3_park"
    , slots := { dutyPosMask := 0b0010, dutyNegMask := 0b1000, powerMask := 0b0100, immunityMask := 0b0010 }
    , nodes :=
        [ { id := 0, label := "park_rule_enacted", sort := .normative, source := .enactedNorm, aspect := .state }
        , { id := 1, label := "sedan_forbidden", sort := .normative, source := .directWitness, aspect := .state, supports := [[0]] }
        , { id := 2, label := "emergency_exception", sort := .normative, source := .enactedNorm, aspect := .state }
        , { id := 3, label := "ambulance_forbidden", sort := .normative, source := .directWitness, aspect := .state, supports := [[0]], attackers := [2] }
        , { id := 4, label := "escooter_forbidden", sort := .normative, source := .directWitness, aspect := .state, supports := [[0]], penumbraConcept := some "vehicle_penumbra" } ] }
  , { name := "case4_hume"
    , slots := {}
    , nodes :=
        [ { id := 0, label := "bloodstain_on_hands", sort := .factual, source := .directWitness, aspect := .state }
        , { id := 1, label := "sole_presence", sort := .factual, source := .directWitness, aspect := .state }
        , { id := 2, label := "factual_injury_inf", sort := .factual, source := .inferential, aspect := .achievement, supports := [[0, 1]] }
        , { id := 3, label := "suspect_guilty", sort := .normative, source := .inferential, aspect := .state, supports := [[0, 1]] }
        , { id := 4, label := "enacted_statute", sort := .normative, source := .enactedNorm, aspect := .state }
        , { id := 5, label := "contra_factual_from_n", sort := .factual, source := .directWitness, aspect := .state, supports := [[4]] } ] }
  , { name := "case5_irrealis"
    , slots := {}
    , nodes :=
        [ { id := 0, label := "cond_zhang_stops", sort := .factual, source := .assumed, aspect := .achievement, supports := [] }
        , { id := 1, label := "presup_prior_dump", sort := .factual, source := .reportative, aspect := .activity, supports := [[]] }
        , { id := 2, label := "cond_no_penalty", sort := .normative, source := .enactedNorm, aspect := .state, presupDeps := [1], supports := [[0]] } ] }
  , { name := "case8a_cycle"
    , slots := {}
    , nodes :=
        [ { id := 0, label := "reg_alpha", sort := .institutional, source := .enactedNorm, aspect := .state, supports := [[1]] }
        , { id := 1, label := "auth_beta", sort := .institutional, source := .enactedNorm, aspect := .state, supports := [[0]] } ] }
  , { name := "nixon_diamond"
    , slots := {}
    , nodes :=
        [ { id := 0, label := "quaker_pacifist", sort := .normative, source := .enactedNorm, aspect := .state, attackers := [1] }
        , { id := 1, label := "republican_hawk", sort := .normative, source := .enactedNorm, aspect := .state, attackers := [0] } ] }
  , { name := "burden_3stage"
    , slots := { dutyPosMask := 0b0001 }
    , nodes :=
        [ { id := 0, label := "prima_facie_duty", sort := .normative, source := .enactedNorm, aspect := .state, attackers := [1] }
        , { id := 1, label := "self_defense_just", sort := .normative, source := .enactedNorm, aspect := .state, attackers := [2] }
        , { id := 2, label := "excess_force_rebut", sort := .normative, source := .enactedNorm, aspect := .state } ] }
  ]

/-! ## Part 3: Pure QTT Linear `.fir` Authority Gate Lifter (`SoundArena -> FIR1`) -/

def buildAuthorityFir (name : String) (mutableMask activeMask rescMask : UInt64) :
    Flow.Morphism.Graph :=
  let b0 := Flow.Morphism.Builder.init name .linear true
  let (b1, curPos)    := b0.input "cur_pos" 64
  let (b2, curNeg)    := b1.input "cur_neg" 64
  let (b3, reqPos)    := b2.input "req_pos" 64
  let (b4, reqNeg)    := b3.input "req_neg" 64
  let (b5, exResc)    := b4.input "ex_resc" 64
  let (b6, _)         := b5.constU64 "k_mut" 64 mutableMask
  let (b7, kEst)      := b6.constU64 "k_est" 64 activeMask
  let (b8, kResc)     := b7.constU64 "k_resc" 64 rescMask
  let (b9, diffPos)   := b8.xorVec "diff_pos" 64 reqPos curPos
  let (b10, stepPos)  := b9.andVec "step_pos" 64 diffPos 5
  let (b11, candPos)  := b10.xorVec "cand_pos" 64 curPos stepPos
  let (b12, diffNeg)  := b11.xorVec "diff_neg" 64 reqNeg curNeg
  let (b13, stepNeg)  := b12.andVec "step_neg" 64 diffNeg 5
  let (b14, candNeg)  := b13.xorVec "cand_neg" 64 curNeg stepNeg
  let (b15, clash)    := b14.andVec "clash" 64 candPos candNeg
  let (b16, cohOk)    := b15.eqZero "coh_ok" clash
  let (b17, nextPos)  := b16.ctrlSel "next_pos" 64 cohOk curPos candPos
  let (b18, nextNeg)  := b17.ctrlSel "next_neg" 64 cohOk curNeg candNeg
  let (b19, notEx)    := b18.notVec "not_ex" 64 exResc
  let (b20, nextResc) := b19.andVec "next_resc" 64 kResc notEx
  let (b21, firedR)   := b20.andVec "fired_resc" 64 kResc exResc
  let (b22, notFired) := b21.notVec "not_fired" 64 firedR
  let (b23, finalPos) := b22.andVec "final_pos" 64 nextPos notFired
  b23.build [finalPos, nextNeg, nextResc, kEst]

/-! ## Part 4: Pure Schema & Golden Fixture CLI Emitter (`0` `leanshared` JSI) -/

def emitSchemaTo (outPath : System.FilePath) : IO Unit := do
  if let some parent := outPath.parent then
    IO.FS.createDirAll parent
  let ok ← soundArenaSchema.emitC17HeaderChecked outPath.toString
  if !ok then
    throw <| IO.userError s!"[SOUND] Schema ABI emission/verification failed for {outPath}"

private def truncAscii23 (s : String) : ByteArray :=
  let raw := s.toUTF8
  let takeN := min raw.size 23
  raw.extract 0 takeN

def encodeFir2 (g : Flow.Morphism.Graph) : Option ByteArray := do
  if !g.wf then none
  let lutTotal : Nat := g.ops.foldl (fun acc op => acc + op.lutWords.length) 0
  if lutTotal > 4096 then
    none
  let modName := truncAscii23 g.name
  if modName.size == 0 then
    none
  let multByte : UInt8 := match g.mult with | .linear => 0 | .cartesian => 1
  let flags : Nat := if g.cleanAnc then 1 else 0
  let mut buf := ByteArray.empty
  buf := Flow.pushLE32 buf 0x32524946 -- "FIR2"
  buf := buf.push 2
  buf := buf.push multByte
  buf := pushVarU32 buf flags
  buf := pushVarU32 buf g.inputs.length
  buf := pushVarU32 buf g.outputs.length
  buf := pushVarU32 buf g.ops.length
  buf := pushVarU32 buf lutTotal
  buf := buf.push (UInt8.ofNat modName.size)
  buf := buf ++ modName
  let mut prevW : Nat := 1
  for v in g.vals do
    let vn := truncAscii23 v.name
    let sameW : Nat := if v.width == prevW then 1 else 0
    let isCl : Nat := if v.isClassical then 1 else 0
    let vtag : UInt8 := UInt8.ofNat (isCl ||| (sameW <<< 1) ||| (vn.size <<< 2))
    buf := buf.push vtag
    if sameW == 0 then
      buf := pushVarU32 buf v.width
      prevW := v.width
    buf := buf ++ vn
  for oid in g.outputs do
    buf := pushVarU32 buf oid
  for op in g.ops do
    let ka : UInt8 := UInt8.ofNat ((op.kind.code.toNat &&& 0x1F) ||| ((op.ins.length &&& 0x07) <<< 5))
    let pmask : UInt8 := UInt8.ofNat (
      (if op.lutWords.isEmpty then 0 else 1) |||
      (if op.p0 != 0 then 2 else 0) |||
      (if op.p1 != 0 then 4 else 0) |||
      (if op.p2 != 0 then 8 else 0) |||
      (if op.p3 != 0 then 16 else 0))
    buf := (buf.push ka).push pmask
    for iv in op.ins do
      buf := pushVarU32 buf ((op.outVal - 1) - iv)
    if !op.lutWords.isEmpty then
      buf := pushVarU32 buf op.lutWords.length
    if op.p0 != 0 then
      buf := pushVarU64 buf op.p0
    if op.p1 != 0 then
      buf := pushVarU64 buf op.p1
    if op.p2 != 0 then
      buf := pushVarU64 buf op.p2
    if op.p3 != 0 then
      buf := pushVarU64 buf op.p3
  for op in g.ops do
    for lw in op.lutWords do
      buf := pushVarU64 buf lw
  let h := Flow.fnv1a64Bytes buf
  return Flow.pushLE64 buf h

def emitGoldenDir (dir : System.FilePath) : IO Unit := do
  IO.FS.createDirAll dir
  let mut manifestRows : List String := []
  for gc in goldenCases do
    let some cells := buildSealedArenaCells gc.nodes gc.slots
      | throw <| IO.userError s!"[SOUND] build cells {gc.name} failed"
    let some binFlwa := packArenaCellsFlwa cells
      | throw <| IO.userError s!"[SOUND] pack FLWA {gc.name} failed"
    let bNodes := gc.nodes.map toBilatticeNode
    let (nm, iStar, pStar, est, defM, conf, pen) := Sound.Bilattice.statusMasks leafThreshold bNodes
    let mutMask := gc.slots.powerMask &&& (~~~gc.slots.immunityMask)
    let rescMask := gc.slots.rescissionPowerMask &&& (~~~gc.slots.immunityMask)
    let some expFir2 := encodeFir2 (buildAuthorityFir gc.name mutMask est rescMask)
      | throw <| IO.userError s!"[SOUND] encodeFir2 {gc.name} failed"
    IO.FS.writeBinFile (dir / s!"{gc.name}.flwa") binFlwa
    IO.FS.writeBinFile (dir / s!"{gc.name}.fir2") expFir2
    let row :=
      "  {\n" ++
      s!"    \"name\": \"{gc.name}\",\n" ++
      s!"    \"num_nodes\": {gc.nodes.length},\n" ++
      s!"    \"node_mask\": {nm},\n" ++
      s!"    \"i_star\": {iStar},\n" ++
      s!"    \"p_star\": {pStar},\n" ++
      s!"    \"established_mask\": {est},\n" ++
      s!"    \"defeated_mask\": {defM},\n" ++
      s!"    \"conflict_mask\": {conf},\n" ++
      s!"    \"penumbra_mask\": {pen}\n" ++
      "  }"
    manifestRows := manifestRows ++ [row]
  let manifestJson :=
    "{\n" ++
    s!"  \"version\": \"{version}\",\n" ++
    s!"  \"schema_hash\": {soundArenaSchema.schemaHash},\n" ++
    "  \"cases\": [\n" ++
    String.intercalate ",\n" manifestRows ++ "\n" ++
    "  ]\n" ++
    "}\n"
  IO.FS.writeFile (dir / "golden_manifest.json") manifestJson

end Sound

def main (args : List String) : IO Unit := do
  match args with
  | ["emit-schema", outPath] => Sound.emitSchemaTo outPath
  | ["emit-golden", dirPath] => Sound.emitGoldenDir dirPath
  | _ =>
      Sound.emitSchemaTo "build/sound_schema_abi.h"
      Sound.emitGoldenDir "build/golden"
