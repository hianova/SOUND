export PATH := $(HOME)/.elan/bin:/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:/usr/local/bin:$(PATH)

CC           ?= clang
CXX          ?= clang++
AR           ?= ar
LEAN         ?= lean
LEANCHECKER  ?= leanchecker
CLANG_FORMAT ?= clang-format
UNAME_S      := $(shell uname -s)

CLANG_TIDY := $(shell if [ -x /opt/homebrew/opt/llvm/bin/clang-tidy ]; then echo /opt/homebrew/opt/llvm/bin/clang-tidy; \
	elif command -v brew >/dev/null 2>&1 && [ -x "$$(brew --prefix llvm 2>/dev/null)/bin/clang-tidy" ]; then echo "$$(brew --prefix llvm)/bin/clang-tidy"; \
	else echo clang-tidy; fi)

FLOW_DIR     ?= $(abspath ../flow)
FLOW_BUILD   := $(FLOW_DIR)/build/lean
LEAN_SYSROOT := $(shell $(LEAN) --print-prefix)

BUILD_DIR    := build
SOUND_VER    := $(shell tr -d '[:space:]' < VERSION)
C_SOURCES    := sound.h sound.c sound_lean_jsi.c

SCHEMA_HDR   := $(BUILD_DIR)/sound_schema_abi.h
SOUND_OLEAN  := $(BUILD_DIR)/Sound.olean
STATIC_STAMP := $(BUILD_DIR)/.static.ok
JSI_STAMP    := $(BUILD_DIR)/.jsi.ok

DYN_EXT      := $(if $(filter Darwin,$(UNAME_S)),dylib,so)
DYN_SHARED   := $(if $(filter Darwin,$(UNAME_S)),-dynamiclib,-shared -lm -lpthread -ldl)
DYN_LDFLAGS  := $(DYN_SHARED) -L"$(LEAN_SYSROOT)/lib/lean" -Wl,-rpath,"$(LEAN_SYSROOT)/lib/lean" -lleanshared
LIBSOUND_JSI := $(BUILD_DIR)/libsound_jsi.$(DYN_EXT)

CFLAGS ?= -std=c17 -mgeneral-regs-only \
          -fsanitize=signed-integer-overflow,shift,bounds,null,return,unreachable \
          -fsanitize-trap=all \
          -Wall -Wextra -Wpedantic -Wundef -Wmissing-prototypes -Werror -O2
override CFLAGS += -DSOUND_VERSION_STRING=\"$(SOUND_VER)\" -I. -I$(BUILD_DIR) -isystem "$(FLOW_DIR)"

FREESTANDING_CFLAGS := -std=c17 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
                       -mgeneral-regs-only -DSOUND_EXPORT_RING0=1 \
                       -Wall -Wextra -Wpedantic -Wundef -Wmissing-prototypes -Werror -O2 \
                       -I. -I$(BUILD_DIR) -isystem "$(FLOW_DIR)"

JSI_CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Wundef -Wmissing-prototypes -Werror -O2 -fPIC \
              -I. -I$(BUILD_DIR) -isystem "$(FLOW_DIR)" -isystem "$(LEAN_SYSROOT)/include"

.DEFAULT_GOAL := all
.PHONY: all upstream-deps lean ring0 jsi test verify fmt fmt-check clean

all: lean ring0 $(BUILD_DIR)/libsound.a $(BUILD_DIR)/sound jsi

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

upstream-deps:
	@test -f "$(FLOW_BUILD)/Flow.olean" && test -f "$(FLOW_BUILD)/Flow/Bilattice.olean" || \
		$(MAKE) -C "$(FLOW_DIR)" build/lean/Flow.olean

# Gate 1: Pure FLOW SSOT Config (`Sound.lean`) + `leanchecker` + 512b C17 Schema ABI
$(SOUND_OLEAN): Sound.lean | $(BUILD_DIR) upstream-deps
	@echo "=== [Gate 1] Lean 4 FLOW SSOT Config, leanchecker & Schema ABI Gate ==="
	@if grep -Eq '^[[:space:]]*(theorem|lemma|axiom)[[:space:]]' Sound.lean; then \
		echo "ERROR: Sound.lean violates FLOW SSOT pure-config rule (found local theorem/lemma/axiom)!" >&2; exit 1; \
	fi
	@if grep -rnE '\b(sorry|admit)\b' Sound.lean; then \
		echo "ERROR: Found forbidden placeholder in Sound.lean!" >&2; exit 1; \
	fi
	LEAN_PATH="$(FLOW_BUILD)" $(LEAN) -DwarningAsError=true -o $@ Sound.lean || (rm -f $@ && exit 1)
	LEAN_PATH="$(FLOW_BUILD):$(BUILD_DIR)" $(LEANCHECKER) Sound || (rm -f $@ && exit 1)

$(SCHEMA_HDR): $(SOUND_OLEAN)
	printf 'import Sound\n#eval do if !(← Sound.emitSchemaOnly) then IO.Process.exit 1\n' | \
		LEAN_PATH="$(FLOW_BUILD):$(BUILD_DIR)" $(LEAN) -DwarningAsError=true --stdin
	@test -s $@
	@! grep -q 'memcpy' $@ || (echo "ERROR: memcpy leaked into freestanding sound_schema_abi.h!" >&2 && exit 1)

lean: $(SCHEMA_HDR)

# Gate 2: Ring-0 Freestanding Pure-GPR, C++20 ABI & clang-tidy Static Analysis Gate
$(BUILD_DIR)/sound_ring0.o: sound.h VERSION $(SCHEMA_HDR) | $(BUILD_DIR)
	@echo "=== [Gate 2] Ring-0 Freestanding Pure-GPR, C++20 ABI & clang-tidy Gate ==="
	printf '#include "sound.h"\n' | $(CC) $(FREESTANDING_CFLAGS) -x c -c - -o $@
	@test -z "$$(nm -u $@)" || (echo "ERROR: Undefined symbols in Ring-0 object:" && nm -u $@ && exit 1)
	@! objdump -d $@ | grep -Eq '[[:space:]](udiv|sdiv|divq?|idivq?|blr|br)[[:space:]]' || \
		(echo "ERROR: Division or indirect jump/call emitted in Ring-0 kernel!" && objdump -d $@ && exit 1)
	@! objdump -d $@ | grep -Eq '([[:space:]]|%)(fadd|fsub|fmul|fdiv|scvtf|ucvtf|fcvtzs|fcvtzu|fcmp|fmov|addsd|subsd|mulsd|divsd|cvtsi2sd|cvttsd2si|xmm[0-9]|ymm[0-9]|zmm[0-9])' || \
		(echo "ERROR: FPU/SIMD instruction emitted in Ring-0 kernel!" && objdump -d $@ && exit 1)
	@printf '#include "sound.h"\nint sound_cpp20_probe(void) { return (int)(sizeof(SoundArena) + alignof(SoundCell64)); }\n' | \
		$(CXX) -x c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -I. -I$(BUILD_DIR) -isystem "$(FLOW_DIR)" -c - -o $(BUILD_DIR)/.cpp20_probe.o
	@rm -f $(BUILD_DIR)/.cpp20_probe.o
	@echo "  [PASS] 0 undef, 0 udiv/sdiv, 0 BLR/BR, 0 FPU/SIMD, C++20 16KB/64B ABI clean"

$(STATIC_STAMP): $(C_SOURCES) $(SCHEMA_HDR) | $(BUILD_DIR)
	$(CLANG_TIDY) -checks='-*,clang-analyzer-*,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling' \
		--header-filter='sound\.h' -warnings-as-errors='*' \
		sound.c sound_lean_jsi.c \
		-- -std=c17 -I. -I$(BUILD_DIR) -isystem "$(FLOW_DIR)" -isystem "$(LEAN_SYSROOT)/include"
	@touch $@

ring0: $(BUILD_DIR)/sound_ring0.o $(STATIC_STAMP)

# Gate 3: Pure-GPR libsound.a & Zero-Lean-Runtime Standalone `build/sound`
$(BUILD_DIR)/libsound.a: $(BUILD_DIR)/sound_ring0.o $(STATIC_STAMP)
	@rm -f $@
	$(AR) rcs $@ $(BUILD_DIR)/sound_ring0.o
	@! nm -u $@ | grep -Eq '(^|[^A-Za-z0-9_])(_?malloc|_?calloc|_?realloc|_?free|_?strtod|_?strtof|_?atof|_?lean_)(@|$$)' || \
		(echo "ERROR: Forbidden runtime/heap/float/lean symbol in libsound.a!" && nm -u $@ && rm -f $@ && exit 1)

$(BUILD_DIR)/sound: sound.c sound.h $(SCHEMA_HDR) $(BUILD_DIR)/libsound.a | $(BUILD_DIR)
	@echo "=== [Gate 3] Zero-Lean-Runtime Standalone C17 Binary Gate ==="
	$(CC) $(CFLAGS) sound.c $(BUILD_DIR)/libsound.a -o $@
	@! nm -u $@ | grep -Eq '(^|[^A-Za-z0-9_])(_?malloc|_?calloc|_?realloc|_?free|_?strtod|_?strtof|_?atof|_?lean_)(@|$$)' || \
		(echo "ERROR: Heap, float, or Lean symbol leaked into sound CLI!" && nm -u $@ && rm -f $@ && exit 1)
	@if [ "$(UNAME_S)" = "Darwin" ] && command -v otool >/dev/null 2>&1; then \
		! otool -L $@ | grep -qi 'leanshared' || (echo "ERROR: leanshared leaked into standalone $@!" && otool -L $@ && rm -f $@ && exit 1); \
	elif command -v ldd >/dev/null 2>&1; then \
		! ldd $@ | grep -qi 'leanshared' || (echo "ERROR: leanshared leaked into standalone $@!" && ldd $@ && rm -f $@ && exit 1); \
	fi
	@echo "  [PASS] 0 heap, 0 float, 0 leanshared in $(BUILD_DIR)/sound"

# Gate 4: Elaboration-Time Lean 4 <-> C17 JSI Boundary Gate
$(LIBSOUND_JSI): sound_lean_jsi.c sound.h $(SCHEMA_HDR) $(STATIC_STAMP) | $(BUILD_DIR)
	$(CC) $(JSI_CFLAGS) sound_lean_jsi.c $(DYN_LDFLAGS) -o $@

$(JSI_STAMP): $(SOUND_OLEAN) $(LIBSOUND_JSI) | $(BUILD_DIR)
	@echo "=== [Gate 4] Elaboration-Time JSI Parity, SEARU Repair & 4,096 Fuzz Gate ==="
	printf 'import Sound\n#eval Sound.compileTimeBoundaryGate\n' | \
		LEAN_PATH="$(FLOW_BUILD):$(BUILD_DIR)" $(LEAN) --load-dynlib=$(LIBSOUND_JSI) -DwarningAsError=true --stdin
	@touch $@

jsi: $(JSI_STAMP)

fmt:
	$(CLANG_FORMAT) -i $(C_SOURCES)

fmt-check:
	@echo "=== [Gate 5b] clang-format Check ==="
	$(CLANG_FORMAT) --dry-run --Werror $(C_SOURCES)
	@echo "  [PASS] clang-format clean"

test: all
	@echo "=== [Gate 5a] Standalone Pure-GPR CLI Self-Test, Verify, Repair & FIR1 Lift ==="
	./$(BUILD_DIR)/sound selftest
	./$(BUILD_DIR)/sound verify $(BUILD_DIR)/case3_park.snd
	./$(BUILD_DIR)/sound repair $(BUILD_DIR)/nixon_diamond.snd
	./$(BUILD_DIR)/sound lift-fir $(BUILD_DIR)/case3_park.snd $(BUILD_DIR)/case3_park_cli.fir
	cmp -s $(BUILD_DIR)/case3_park.fir $(BUILD_DIR)/case3_park_cli.fir
	./$(BUILD_DIR)/sound lift-fir $(BUILD_DIR)/nixon_diamond.snd $(BUILD_DIR)/nixon_repaired.fir --repaired

verify: test fmt-check
	@echo "=== ALL SOUND v$(SOUND_VER) GATES PASSED ==="

clean:
	rm -rf $(BUILD_DIR)
