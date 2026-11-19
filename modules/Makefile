PYTHON   ?= python3
REPO_DIR := $(abspath ..)

.DEFAULT_GOAL := verify
.PHONY: verify test benchmark narrative latex

test:
	@echo "=== [Modules] Running Taxation/Accounting & Narrative Unittests ==="
	cd "$(REPO_DIR)" && $(PYTHON) -m unittest discover -s modules/tests -v
	cd "$(REPO_DIR)" && $(PYTHON) -m unittest discover -s modules/narrative/tests -v

benchmark:
	@echo "=== [Modules] Running Taxation & Accounting MCQ Benchmarks ==="
	cd "$(REPO_DIR)" && $(PYTHON) -m modules.common.cli packs
	cd "$(REPO_DIR)" && $(PYTHON) -m modules.common.cli benchmark --all

narrative:
	@echo "=== [Modules] Running Narrative Audit, Item Cards, Demo & Two-Scene Campaign ==="
	cd "$(REPO_DIR)" && $(PYTHON) -m modules.narrative.engine.cli audit
	cd "$(REPO_DIR)" && $(PYTHON) -m modules.narrative.engine.cli items
	cd "$(REPO_DIR)" && $(PYTHON) -m modules.narrative.engine.cli demo
	cd "$(REPO_DIR)" && $(PYTHON) -m modules.narrative.engine.cli campaign

latex:
	@echo "=== [Modules] Running Semantic LaTeX x SOUND Comprehension Verification ==="
	cd "$(REPO_DIR)" && $(PYTHON) -m modules.latex.cli verify modules/latex/docs/real_analysis_evt_ivt.tex
	cd "$(REPO_DIR)" && $(PYTHON) -m modules.latex.cli verify modules/latex/docs/tw_vat_statute_provisos.tex
	cd "$(REPO_DIR)" && $(PYTHON) -m modules.latex.cli ablate modules/latex/docs/real_analysis_evt_ivt.tex --target thm_evt --facts hyp_closed,hyp_bounded,hyp_finite_dim,hyp_nonempty,hyp_continuous
	cd "$(REPO_DIR)" && $(PYTHON) -m modules.latex.cli ablate modules/latex/docs/tw_vat_statute_provisos.tex --target eff_zero_refund --facts fact_export_goods,fact_input_invoice
	cd "$(REPO_DIR)" && $(PYTHON) -m modules.latex.cli emit-report modules/latex/docs/real_analysis_evt_ivt.tex --out /tmp/sound_math_evt_report.tex --ablate-target thm_evt --ablate-facts hyp_closed,hyp_bounded,hyp_finite_dim,hyp_nonempty,hyp_continuous
	cd "$(REPO_DIR)" && $(PYTHON) -m modules.latex.cli emit-report modules/latex/docs/tw_vat_statute_provisos.tex --out /tmp/sound_tw_vat_report.tex --ablate-target eff_zero_refund --ablate-facts fact_export_goods,fact_input_invoice

verify: test benchmark narrative latex
	@echo "=== ALL SOUND MODULES VERIFICATION GATES PASSED ==="
