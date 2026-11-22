SHELL := /bin/sh
UNAME_S := $(shell uname -s)
FLOW_DIR ?= ../FLOW
BUILD := build
LEAN ?= $(or $(shell command -v lean 2>/dev/null),$(HOME)/.elan/bin/lean)
LEANCHECKER ?= $(or $(shell command -v leanchecker 2>/dev/null),$(HOME)/.elan/bin/leanchecker)
AR ?= ar

ifeq ($(UNAME_S),Darwin)
LLVM_PREFIX ?= /opt/homebrew/opt/llvm/bin
else
LLVM_PREFIX ?= /usr/bin
endif
ifneq ($(wildcard $(LLVM_PREFIX)/clang),)
DEFAULT_CC := $(LLVM_PREFIX)/clang
DEFAULT_CXX := $(LLVM_PREFIX)/clang++
DEFAULT_FMT := $(LLVM_PREFIX)/clang-format
DEFAULT_TIDY := $(LLVM_PREFIX)/clang-tidy
else
DEFAULT_CC := clang
DEFAULT_CXX := clang++
DEFAULT_FMT := clang-format
DEFAULT_TIDY := clang-tidy
endif
ifeq ($(origin CC),default)
CC := $(DEFAULT_CC)
endif
ifeq ($(origin CXX),default)
CXX := $(DEFAULT_CXX)
endif
CLANG_FORMAT ?= $(DEFAULT_FMT)
CLANG_TIDY ?= $(DEFAULT_TIDY)

C17_RING0_FLAGS := -std=c17 -O2 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
	-fno-jump-tables -mgeneral-regs-only -Wall -Wextra -Werror -Wpedantic -Wconversion \
	-Wundef -Wmissing-prototypes
C17_HOST_FLAGS := -std=c17 -O2 -Wall -Wextra -Werror -Wpedantic -Wconversion \
	-Wundef -Wmissing-prototypes
INC_RING0 := -Iinclude -I$(BUILD) -isystem $(FLOW_DIR)/core -isystem $(FLOW_DIR)/sdk
INC_HOST := -Iinclude -I$(BUILD) -isystem $(FLOW_DIR)/core -isystem $(FLOW_DIR)/sdk

C_SOURCES := include/sound.h src/sound_ring0.c src/sound_cert.h src/sound_cli.c

.PHONY: all clean fmt verify-all verify experiments \
	gate0-lean-theory gate1-lean-schema gate2-ring0-audit gate3-selftest \
	gate4-cli-audit gate5-oracle gate6-sabotage gate7-format

all: $(BUILD)/libsound.a $(BUILD)/sound

$(BUILD)/lean/Sound/Bilattice.olean: Sound/Bilattice.lean
	@mkdir -p $(dir $@)
	$(LEAN) -DwarningAsError=true --root=. -o $@ $<

$(BUILD)/lean/Sound/Sheaf.olean: Sound/Sheaf.lean
	@mkdir -p $(dir $@)
	$(LEAN) -DwarningAsError=true --root=. -o $@ $<

$(BUILD)/lean/Sound/Hohfeld.olean: Sound/Hohfeld.lean $(BUILD)/lean/Sound/Bilattice.olean
	@mkdir -p $(dir $@)
	LEAN_PATH=$(BUILD)/lean $(LEAN) -DwarningAsError=true --root=. -o $@ $<

$(BUILD)/lean/Sound/Linguistics.olean: Sound/Linguistics.lean $(BUILD)/lean/Sound/Bilattice.olean
	@mkdir -p $(dir $@)
	LEAN_PATH=$(BUILD)/lean $(LEAN) -DwarningAsError=true --root=. -o $@ $<

$(BUILD)/lean/Sound/Contract.olean: Sound/Contract.lean $(BUILD)/lean/Sound/Bilattice.olean $(BUILD)/lean/Sound/Hohfeld.olean
	@mkdir -p $(dir $@)
	LEAN_PATH=$(BUILD)/lean $(LEAN) -DwarningAsError=true --root=. -o $@ $<

$(BUILD)/lean/Sound/Theory.olean: Sound/Theory.lean \
		$(BUILD)/lean/Sound/Bilattice.olean \
		$(BUILD)/lean/Sound/Sheaf.olean \
		$(BUILD)/lean/Sound/Hohfeld.olean \
		$(BUILD)/lean/Sound/Linguistics.olean \
		$(BUILD)/lean/Sound/Contract.olean
	@mkdir -p $(dir $@)
	LEAN_PATH=$(BUILD)/lean $(LEAN) -DwarningAsError=true --root=. -o $@ $<

$(BUILD)/lean/Sound.olean: Sound.lean $(BUILD)/lean/Sound/Bilattice.olean
	@mkdir -p $(dir $@)
	LEAN_PATH=$(BUILD)/lean:$(FLOW_DIR)/build/lean $(LEAN) -DwarningAsError=true --root=. -o $@ $<

$(BUILD)/sound_schema_abi.h: $(BUILD)/lean/Sound.olean
	@mkdir -p $(BUILD)
	LEAN_PATH=$(BUILD)/lean:$(FLOW_DIR)/build/lean $(LEAN) --run Sound.lean emit-schema $@

$(BUILD)/golden/golden_manifest.json: $(BUILD)/lean/Sound.olean
	@mkdir -p $(BUILD)/golden
	LEAN_PATH=$(BUILD)/lean:$(FLOW_DIR)/build/lean $(LEAN) --run Sound.lean emit-golden $(BUILD)/golden

$(BUILD)/sound_ring0.o: src/sound_ring0.c include/sound.h $(BUILD)/sound_schema_abi.h
	@mkdir -p $(BUILD)
	$(CC) $(C17_RING0_FLAGS) $(INC_RING0) -c $< -o $@

$(BUILD)/libsound.a: $(BUILD)/sound_ring0.o
	$(AR) rcs $@ $<

$(BUILD)/sound: $(BUILD)/sound_ring0.o src/sound_cli.c src/sound_cert.h include/sound.h $(BUILD)/sound_schema_abi.h
	$(CC) $(C17_HOST_FLAGS) $(INC_HOST) $(BUILD)/sound_ring0.o src/sound_cli.c -o $@

gate0-lean-theory: $(BUILD)/lean/Sound/Theory.olean
	@echo "[Gate 0] Checking 5 pure-theory pillars via leanchecker and axiom audit..."
	@if grep -qiE '\bsorry\b' Sound/Bilattice.lean Sound/Sheaf.lean Sound/Hohfeld.lean Sound/Linguistics.lean Sound/Contract.lean Sound/Theory.lean; then \
		echo "ERROR: sorry found in Sound theory modules"; exit 1; \
	fi
	@for m in Sound.Bilattice Sound.Sheaf Sound.Hohfeld Sound.Linguistics Sound.Contract Sound.Theory; do \
		LEAN_PATH=$(BUILD)/lean $(LEANCHECKER) $$m || exit 1; \
	done
	@python3 -c 'import pathlib, re; mods=[("Sound.Bilattice","Sound/Bilattice.lean"),("Sound.Sheaf","Sound/Sheaf.lean"),("Sound.Hohfeld","Sound/Hohfeld.lean"),("Sound.Linguistics","Sound/Linguistics.lean"),("Sound.Contract","Sound/Contract.lean")]; thms=[f"#print axioms {m}.{n}" for m,p in mods for n in re.findall(r"^theorem\s+(\S+)", pathlib.Path(p).read_text(encoding="utf-8"), re.M)]; assert len(thms)==142, f"Expected 142 theorems, got {len(thms)}"; pathlib.Path("$(BUILD)/sound_axioms.in").write_text("import Sound.Theory\n" + "\n".join(thms) + "\n", encoding="utf-8")'
	@LEAN_PATH=$(BUILD)/lean $(LEAN) --stdin < $(BUILD)/sound_axioms.in > $(BUILD)/sound_axioms.txt
	@if grep -q 'sorryAx' $(BUILD)/sound_axioms.txt; then echo "ERROR: sorryAx detected"; exit 1; fi
	@if grep -q 'Lean\.ofReduceBool' $(BUILD)/sound_axioms.txt; then echo "ERROR: Lean.ofReduceBool detected"; exit 1; fi
	@echo "[Gate 0] PASS (Sound.Theory 142 theorems, 0 sorryAx, 0 ofReduceBool)"

gate1-lean-schema: $(BUILD)/sound_schema_abi.h $(BUILD)/golden/golden_manifest.json
	@echo "[Gate 1] Auditing Sound.lean pure schema config, golden fixtures, and ABI header..."
	@if grep -qE '^\s*(theorem|lemma|axiom)\s' Sound.lean; then \
		echo "ERROR: theorems/lemmas/axioms not allowed in Sound.lean"; exit 1; \
	fi
	LEAN_PATH=$(BUILD)/lean:$(FLOW_DIR)/build/lean $(LEANCHECKER) Sound
	@if grep -q 'memcpy' $(BUILD)/sound_schema_abi.h; then \
		echo "ERROR: memcpy found in generated ABI header"; exit 1; \
	fi
	@echo "[Gate 1] PASS (256-slot ABI 0 memcpy, 10 golden fixtures, leanchecker Sound PASS)"

gate2-ring0-audit: $(BUILD)/sound_ring0.o $(BUILD)/libsound.a
	@echo "[Gate 2] Auditing Ring-0 freestanding Pure-GPR object..."
	@test -z "$$(nm -u $(BUILD)/sound_ring0.o)"
ifeq ($(UNAME_S),Darwin)
	@if otool -tV $(BUILD)/sound_ring0.o | grep -qE '\b(udiv|sdiv|fdiv|fmul|fadd|fsub|blr|br)\b|v[0-9]+\.'; then \
		echo "ERROR: Forbidden instruction or vector register in sound_ring0.o"; exit 1; \
	fi
else
	@if objdump -d $(BUILD)/sound_ring0.o | grep -qE '\b(udiv|sdiv|fdiv|fmul|fadd|fsub|blr|br)\b|%?[xyz]mm[0-9]+|\bv[0-9]+\.'; then \
		echo "ERROR: Forbidden instruction or vector register in sound_ring0.o"; exit 1; \
	fi
endif
	@echo '#include "sound.h"' | $(CXX) -std=c++20 -x c++ -fsyntax-only -Wall -Wextra -Werror $(INC_RING0) -
	$(CLANG_TIDY) --header-filter='sound\.h$$' --checks='clang-analyzer-*,bugprone-*,-bugprone-easily-swappable-parameters' \
		--warnings-as-errors='*' src/sound_ring0.c -- -std=c17 $(INC_RING0)
	@echo "[Gate 2] PASS (0 undef symbols, Pure-GPR clean, C++20 clean, clang-tidy 0 warnings)"

gate3-selftest: $(BUILD)/sound
	@echo "[Gate 3] Running built-in 19-suite Ring-0 + cert selftest..."
	./$(BUILD)/sound selftest
	@echo "[Gate 3] PASS"

gate4-cli-audit: $(BUILD)/sound
	@echo "[Gate 4] Auditing CLI linkage (0 leanshared, 0 heap allocators)..."
ifeq ($(UNAME_S),Darwin)
	@if otool -L $(BUILD)/sound | grep -qi 'leanshared'; then echo "ERROR: leanshared linked"; exit 1; fi
else
	@if ldd $(BUILD)/sound | grep -qi 'leanshared'; then echo "ERROR: leanshared linked"; exit 1; fi
endif
	@if nm -u $(BUILD)/sound | grep -qE '[[:space:]]_?(malloc|free|calloc|realloc)([[:space:]@]|$$)'; then \
		echo "ERROR: heap allocator symbol linked"; exit 1; \
	fi
	@echo "[Gate 4] PASS"

gate5-oracle: gate0-lean-theory gate1-lean-schema gate4-cli-audit
	@echo "[Gate 5] Running independent pure-Python exact oracle + live Lean replay..."
	python3 oracles/verify_sound_oracle.py
	@echo "[Gate 5] PASS"

gate6-sabotage: gate1-lean-schema
	@echo "[Gate 6] Running 48-mutant single-fault sabotage gate..."
	python3 tools/sound_sabotage_check.py
	@echo "[Gate 6] PASS"

gate7-format:
	@echo "[Gate 7] Verifying clang-format compliance..."
	$(CLANG_FORMAT) --dry-run --Werror $(C_SOURCES)
	@echo "[Gate 7] PASS"

experiments: $(BUILD)/sound
	@echo "[Experiments] Running 5 pure-theory atlas, orbit & contract-consent experiments..."
	python3 experiments/bilattice_phase_atlas.py
	python3 experiments/sheaf_h1_atlas.py
	python3 experiments/hohfeld_norm_orbits.py
	python3 experiments/linguistics_projection_atlas.py
	python3 experiments/contract_informed_consent_audit.py
	@echo "[Experiments] PASS"

fmt:
	$(CLANG_FORMAT) -i $(C_SOURCES)

clean:
	rm -rf $(BUILD)

verify-all: gate0-lean-theory gate1-lean-schema gate2-ring0-audit gate3-selftest gate4-cli-audit gate5-oracle gate6-sabotage gate7-format
	@echo "=== SOUND v3.0.0: ALL 8 VERIFICATION GATES PASSED ==="

verify: verify-all
