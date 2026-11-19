PYTHON   ?= python3
REPO_DIR := $(abspath ..)

.DEFAULT_GOAL := verify
.PHONY: verify test benchmark narrative

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

verify: test benchmark narrative
	@echo "=== ALL SOUND MODULES VERIFICATION GATES PASSED ==="
