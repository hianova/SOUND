PYTHON   ?= python3
REPO_DIR := $(abspath ..)

.DEFAULT_GOAL := verify
.PHONY: verify test

test:
	@echo "=== [Modules] Running Domain-Free SOUND SDK Unittests ==="
	cd "$(REPO_DIR)" && $(PYTHON) -m unittest discover -s modules/tests -v

verify: test
	@echo "=== ALL SOUND SDK VERIFICATION GATES PASSED ==="
