# SOUND v3.0.0

**Pure Foundational Theory & Ring-0 Pure-GPR Epistemic, Cohomological, Normative & Linguistic Discovery Engine**

`SOUND v3.0.0` unifies non-monotonic argumentation, discrete sheaf cohomology, complete Hohfeldian legal algebra, formal linguistics, and asymmetric contract informed-consent verification on top of `FLOW` (`Flow.Core`, `Flow.Schema`, `Flow.Morphism`, `"FIR1"` wire IR) across five formally verified pillars (`148` Lean 4 theorems, `0 sorryAx`, `0 Lean.ofReduceBool`):

1. **Non-Monotonic Bilattice Logic (`Sound.Bilattice`, `classify-bilattice`, `paradox-min`)**:
   - Belnap 4-valued bilattice $B_4 = (\{\bot, \mathbf{F}, \mathbf{T}, \top\}, \le_t, \le_k)$ with exact Klein-$V_4$ involution symmetry (`¬`, conflict flip `c`), interlaced De Morgan distributivity, alternating-fixpoint operator $\Gamma^2$ (`I*`, `P*`), tag-stratified Hume No-Go firewall (`Is -> Ought` / `Ought -> Is`), evidential leaf quarantine, and bounded SipHash-protected admission guard.
   - Universal classical Odd-Cycle No-Go (`odd_cycle_classical_nogo`), Even-Cycle two-model bifurcation (`even_cycle_classical_two_models`), and Defeasible vs. Monotonicity No-Go.
   - Exhaustive attack-digraph phase atlas across all `64` ($N=3$) and `4,096` ($N=4$) loopless graphs:
     - $N=3$: `34` grounded-unique, `6` unique non-grounded, `22` multi-stable, `2` classically unsolvable (`30` with non-empty bilattice conflict).
     - $N=4$: `1,389` grounded-unique, `836` unique non-grounded, `1,657` multi-stable, `214` classically unsolvable (`2,707` with non-empty bilattice conflict).

2. **Discrete Čech Sheaf Cohomology ($H^1$) (`Sound.Sheaf`, `classify-sheaf`, `sheaf-h1`)**:
   - Discrete 1-cochain holonomy, coboundary vanishing (`IsCoboundaryZ4` / `IsCoboundaryV4`), gauge transformations, closed-loop obstructions (`nonzero_holonomy_no_global_section_*_nogo`), chain/tree triviality (`chain_cover_always_coboundary_z4`: $H^1 = 0$), and explicit 3-chart Möbius normative obstructions (`mobius3_h1_obstruction_nogo`: $H^1 \neq 0$).
   - Exact cohomology classification verifying $|H^1(\mathcal{G}, G)| = |G|^{|E|-|V|+1}$: `C3(Z4)` (`16/64` coboundaries, `48` obstructed, `4` gauge orbits), `C4(Z4)` (`64/256` coboundaries, `192` obstructed), `P3(Z4)` (`16/16` coboundaries), `C3(V4)` (`16/64` coboundaries), plus `K4` ($\beta_1 = 3$, $|H^1| = 64$ over $\mathbb{Z}_4 / V_4$) in experiments.

3. **Complete 8-Position Hohfeld Legal Algebra & Deontic Traps (`Sound.Hohfeld`, `classify-hohfeld`, `chisholm`, `norm-trap`)**:
   - All `8` first-order and second-order Hohfeldian positions (`Claim`, `Privilege`, `Duty`, `NoRight`, `Power`, `Immunity`, `Liability`, `Disability`) proved as a faithful Klein $V_4 = \{\mathrm{id}, C, O, D\}$ group action partitioning cleanly into `2` disjoint 4-element orbits.
   - Multi-step Kinetic-Freeze No-Go theorems (`immunity_kinetic_freeze_trace_nogo`, `disability_frozen_trace_nogo`, `zero_power_complete_freeze_nogo`), verified across all `82,944` 3-slot transition tuples (`0` coherence violations, `0` immunity leaks, `0` disability leaks) and exact reachability orbit sizes `|R| = 3^{3 - |I|} = 27, 9, 3, 1`.
   - Classical SDL Chisholm Contrary-to-Duty Explosion No-Go (`sdl_chisholm_explosion_nogo`) vs. SOUND stratified consistency (`sound_chisholm_stratified_consistent`), plus weighted SEARU / Lex Specialis partial-order tie obstructions (`E_A = E_B = 64` vs. KanLock override `E = 255`).

4. **Formal Linguistics Algebra (`Sound.Linguistics`, `classify-projection`, `aspect-firewall`, `presup-filter`)**:
   - Vendler Aktionsart feature lattice (`±Dynamic × ±Telic`) isomorphism (`State`, `Activity`, `Accomplishment`, `Achievement`), universal Homogeneity vs. Quantization No-Go (`homogeneous_quantized_nogo`), and Imperfective-to-Constitutive semantic firewall (`imperfective_activity_blocked_constitutive_nogo`).
   - 4-valued Presupposition Projection algebra (`Hole` negation / `Plug` irrealis / Karttunen–Heim `Filter` conditional) and Classical Bivalent Truth-Gap No-Go (`classical_bivalent_truth_gap_nogo`), classifying all `256` unary $B_4 \to B_4$ operators (`10` involutive, `36` $t$-antitone, `36` $k$-monotone, `1` unique classical negation), all `16` Hole pairs (`12` Strawson truth-gap pairs), and all `256` conditional pairs.
   - 5 Illocutionary Force Endofunctors (`Assertion`, `Query`, `Irrealis`, `Directive`, `Declarative`) preserving epistemic/normative/modal conservation laws.

5. **Asymmetric Contract Informed-Consent, Compositional Loophole Graph & Intake Gates (`Sound.Contract`, `audit-contract`, `hunt contract-drift`, `hunt contract-loophole`)**:
   - Formalizes consumer–institution asymmetric contracts (`SummarySpec` + `ContractClause` + `ClauseEdge` + quantified `∀ act : AdversarialAction` & `ValidClosedEdgeWalk`) with fail-closed `.vetoed` enforcement across immunity breaches (`protectedMask`), undefined/conflicting `B4` obligations (`0#2`/`3#2`), undisclosed fine-print smuggling, Chisholm default acceleration cascades, discrete Čech sheaf $H^1(\mathcal{G}, \mathbb{Z}_4)$ loophole cycles, Hohfeld override/bypass chains, fail-closed natural-language span coverage & **Fact-Fidelity** gates against omitting or value-downplaying LLM proposers plus optional `INFO` Cognitive Opacity receipt telemetry ([`tools/sound_contract_intake.py`](tools/sound_contract_intake.py)), exit-fee traps, `__int128` unconscionability penalty ratios (`consPenalty * maxAsymDen > instPenalty * maxAsymNum`), and positive Benefit Drift (`worstCaseLoss - maxLossCap > 0`).
   - Proves universal compositional adversarial loss soundness (`compositional_certified_bounds_loss`, `compositional_loophole_separation_witness`, `sheaf_cycle_forbids_global_interpretation`, `override_bypass_forces_graph_veto`, `graph_drift_forces_veto`, `graph_repair_restores_certified`), emits Markdown counterexample & redline receipts (`--out-receipt`), and audits `5` paired contract families plus omitting, semantic-downplaying, and complete raw-ZH intake cases in [`benchmarks/contracts/`](benchmarks/contracts/) via [`experiments/contract_informed_consent_audit.py`](experiments/contract_informed_consent_audit.py) (`5/5` predatory + omitting + downplaying `VETOED`, `5/5` fair + complete intake `CERTIFIED`, `13/13` live Lean certificates / `40` theorems `leanchecker` PASS).

## Architecture

- [`Sound/Bilattice.lean`](Sound/Bilattice.lean), [`Sound/Sheaf.lean`](Sound/Sheaf.lean), [`Sound/Hohfeld.lean`](Sound/Hohfeld.lean), [`Sound/Linguistics.lean`](Sound/Linguistics.lean), [`Sound/Contract.lean`](Sound/Contract.lean), [`Sound/Theory.lean`](Sound/Theory.lean): Pure Lean 4 formal foundations (`65 + 18 + 22 + 21 + 22 = 148` top-level theorems, `0 sorryAx`, `0 Lean.ofReduceBool`), importing only Lean stdlib (`Std.Tactic.BVDecide`).
- [`Sound.lean`](Sound.lean): Pure `Flow.Schema` `256`-slot (`16,384B`) arena ABI compiler & golden fixture emitter (`0` local theorems/lemmas/axioms, `0 memcpy` in generated `build/sound_schema_abi.h`, emitting `10` canonical `.snd` arenas + `"FIR1"` circuits + `golden_manifest.json`).
- [`include/sound.h`](include/sound.h) & [`src/sound_ring0.c`](src/sound_ring0.c): Freestanding Ring-0 Pure-GPR C17 kernel (`0` undef symbols, `0` `udiv`/`sdiv`/FPU/SIMD/indirect calls, `C++20` header clean, `clang-tidy` 0 warnings).
- [`src/sound_cert.h`](src/sound_cert.h) & [`src/sound_cli.c`](src/sound_cli.c): Zero-heap, zero-`leanshared` host CLI & certificate emitter (`T1..T20` built-in self-tests, zero-heap JSON `audit-contract`, `--out-receipt` Markdown redlines, `"FIR1"` synthesis/eval, and live `.lean` / `.json` certificate emission).
- [`oracles/verify_sound_oracle.py`](oracles/verify_sound_oracle.py): Independent pure-Python exact oracle checking 10/10 Golden `SoundArena` (`16,384B`) + `"FIR1"` (`2,144B`) parity + SEARU repair, all 4 `classify-*` universes, and `8/8` live `lean -DwarningAsError=true` + `leanchecker` + `0 sorryAx` / `0 ofReduceBool` certificate replays (`paradox-min`, `sheaf-h1`, `chisholm`, `norm-trap`, `aspect-firewall`, `presup-filter`, `contract-drift`, `contract-loophole`).
- [`tools/sound_sabotage_check.py`](tools/sound_sabotage_check.py): Parallel `51`-mutant single-fault C17 verification suite across `17` categories (`51/51 = 100.0%` killed, `0` compile errors).
- [`tools/sound_contract_intake.py`](tools/sound_contract_intake.py): Fail-closed natural-language contract sentence/span coverage + deterministic Fact-Fidelity gate (preventing monetary/hostile-power/rate downplaying by LLM proposers) with optional `INFO` Cognitive Opacity NCD audit before invoking `./build/sound audit-contract`.
- [`experiments/`](experiments): 5 pure-theory & contract-consent stdlib-Python scripts (`bilattice_phase_atlas.py`, `sheaf_h1_atlas.py`, `hohfeld_norm_orbits.py`, `linguistics_projection_atlas.py`, `contract_informed_consent_audit.py`) cross-checked against `./build/sound`.

## Build & Verify

```sh
make clean && make -j verify-all && make experiments
```

Run standalone classifiers, hunts, and arena verification:

```sh
./build/sound selftest
./build/sound classify-bilattice  --out-json build/certs/classify_bilattice.json
./build/sound classify-sheaf      --out-json build/certs/classify_sheaf.json
./build/sound classify-hohfeld    --out-json build/certs/classify_hohfeld.json
./build/sound classify-projection --out-json build/certs/classify_projection.json

./build/sound hunt paradox-min     --seed 0x1337 --out-lean build/certs/SoundHuntParadox.lean --out-fir build/certs/paradox.fir --out-json build/certs/paradox.json
./build/sound hunt sheaf-h1        --seed 0x1337 --out-lean build/certs/SoundHuntSheaf.lean   --out-fir build/certs/sheaf.fir   --out-json build/certs/sheaf.json
./build/sound hunt chisholm                      --out-lean build/certs/SoundHuntChisholm.lean --out-fir build/certs/chisholm.fir --out-json build/certs/chisholm.json
./build/sound hunt norm-trap       --seed 0x77   --out-lean build/certs/SoundHuntNormTrap.lean --out-fir build/certs/norm_trap.fir --out-json build/certs/norm_trap.json
./build/sound hunt aspect-firewall --seed 0x42   --out-lean build/certs/SoundHuntAspect.lean   --out-fir build/certs/aspect.fir  --out-json build/certs/aspect.json
./build/sound hunt presup-filter   --seed 0x55   --out-lean build/certs/SoundHuntPresup.lean   --out-fir build/certs/presup.fir  --out-json build/certs/presup.json
./build/sound hunt contract-drift    --seed 0x534F554E445F3330 --out-lean build/certs/SoundHuntContract.lean --out-fir build/certs/contract.fir --out-json build/certs/contract.json
./build/sound hunt contract-loophole --seed 0x534F554E445F3331 --out-lean build/certs/SoundHuntLoophole.lean --out-fir build/certs/loophole.fir --out-json build/certs/loophole.json
./build/sound audit-contract benchmarks/contracts/elder_investment_policy_predatory.json --out-lean build/certs/CertElderPred.lean --out-fir build/certs/elder_pred.fir --out-json build/certs/elder_pred.json --out-receipt build/certs/elder_pred_receipt.md
python3 tools/sound_contract_intake.py --raw-text benchmarks/contracts/elder_policy_raw_zh.txt --proposer-json benchmarks/contracts/elder_policy_proposer_downplaying.json --out-receipt build/certs/intake_receipt.md [--summary-text summary.txt]

./build/sound verify build/golden/case1_corroborated.snd --json
./build/sound repair build/golden/nixon_diamond.snd
./build/sound lift-fir build/golden/case1_corroborated.snd build/case1_corroborated_cli.fir
```
