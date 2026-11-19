PYTHON   ?= python3
REPO_DIR := $(abspath ../..)
BUILD_DIR := build
ZIP_PATH  := $(BUILD_DIR)/submission.zip
TGZ_PATH  := $(BUILD_DIR)/sound_colab_bundle.tgz
ADK_DIR   := adk_pack

.DEFAULT_GOAL := verify
.PHONY: verify test stress-synthetic inspect-data stress-data pack-submission pack-colab clean

test:
	@echo "=== [Kaggle Agent] Running Unit & Adversarial Parity Tests ==="
	cd "$(REPO_DIR)" && $(PYTHON) -m unittest discover -s kaggle/agent/tests -v

stress-synthetic:
	@echo "=== [Kaggle Agent] Running Synthetic & Graph Stress Suite ==="
	cd "$(REPO_DIR)" && $(PYTHON) -m kaggle.agent.dataset_harness stress --synthetic

inspect-data:
	@echo "=== [Kaggle Agent] Inspecting Unpacked Kaggle Dataset (if present) ==="
	cd "$(REPO_DIR)" && $(PYTHON) -m kaggle.agent.dataset_harness inspect

stress-data:
	@echo "=== [Kaggle Agent] Running Stress Suite on Kaggle Dataset ==="
	cd "$(REPO_DIR)" && $(PYTHON) -m kaggle.agent.dataset_harness stress --require-data --max-tasks 129

pack-submission:
	@echo "=== [Kaggle Agent] Packing Portable ADK Submission Zip ==="
	cd "$(REPO_DIR)" && $(PYTHON) -m kaggle.agent.dataset_harness pack-submission --out "$(CURDIR)/$(ZIP_PATH)"

pack-colab:
	@echo "=== [Kaggle Agent] Packing Colab A100/H100 Runtime Bundle ==="
	cd "$(REPO_DIR)" && $(PYTHON) -m kaggle.agent.dataset_harness pack-colab --out "$(CURDIR)/$(TGZ_PATH)"

verify: test stress-synthetic pack-submission pack-colab
	@echo "=== ALL KAGGLE/AGENT VERIFICATION GATES PASSED ==="

clean:
	rm -rf "$(BUILD_DIR)"
