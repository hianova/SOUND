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

FLOW_DIR      ?= $(abspath ../FLOW)
FLOW_BUILD    := $(FLOW_DIR)/build/lean
FLOW_CORE_INC := $(FLOW_DIR)/core
FLOW_SDK_INC  := $(FLOW_DIR)/sdk
LEAN_SYSROOT  := $(shell $(LEAN) --print-prefix)

BUILD_DIR       := build
LEAN_DIR        := $(BUILD_DIR)/lean
SOUND_VER       := $(shell tr -d '[:space:]' < VERSION)
C_SOURCES       := sound.h sound.c sound_lean_jsi.c

SCHEMA_HDR      := $(BUILD_DIR)/sound_schema_abi.h
BILATTICE_OLEAN := $(LEAN_DIR)/Sound/Bilattice.olean
SOUND_OLEAN     := $(LEAN_DIR)/Sound.olean
STATIC_STAMP    := $(BUILD_DIR)/.static.ok
JSI_STAMP       := $(BUILD_DIR)/.jsi.ok

DYN_EXT      := $(if $(filter Darwin,$(UNAME_S)),dylib,so)
DYN_SHARED   := $(if $(filter Darwin,$(UNAME_S)),-dynamiclib,-shared -lm -lpthread -ldl)
RING0_SHARED_LDFLAGS := $(if $(filter Darwin,$(UNAME_S)),-dynamiclib,-shared)
DYN_LDFLAGS  := $(DYN_SHARED) -L"$(LEAN_SYSROOT)/lib/lean" -Wl,-rpath,"$(LEAN_SYSROOT)/lib/lean" -lleanshared
LIBSOUND_DYN := $(BUILD_DIR)/libsound.$(DYN_EXT)
LIBSOUND_JSI := $(BUILD_DIR)/libsound_jsi.$(DYN_EXT)

CFLAGS ?= -std=c17 -mgeneral-regs-only \
          -fsanitize=signed-integer-overflow,shift,bounds,null,return,unreachable \
          -fsanitize-trap=all \
          -Wall -Wextra -Wpedantic -Wundef -Wmissing-prototypes -Werror -O2
override CFLAGS += -DSOUND_VERSION_STRING=\"$(SOUND_VER)\" -I. -I$(BUILD_DIR) -isystem "$(FLOW_CORE_INC)" -isystem "$(FLOW_SDK_INC)"

FREESTANDING_CFLAGS := -std=c17 -fPIC -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
                       -mgeneral-regs-only -DSOUND_EXPORT_RING0=1 \
                       -Wall -Wextra -Wpedantic -Wundef -Wmissing-prototypes -Werror -O2 \
                       -I. -I$(BUILD_DIR) -isystem "$(FLOW_CORE_INC)"

JSI_CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Wundef -Wmissing-prototypes -Werror -O2 -fPIC \
              -I. -I$(BUILD_DIR) -isystem "$(FLOW_CORE_INC)" -isystem "$(FLOW_SDK_INC)" -isystem "$(LEAN_SYSROOT)/include"

.DEFAULT_GOAL := all
.PHONY: all upstream-deps lean ring0 jsi test verify fmt fmt-check clean

all: lean ring0 $(BUILD_DIR)/libsound.a $(LIBSOUND_DYN) $(BUILD_DIR)/sound jsi

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

upstream-deps:
	@test -f "$(FLOW_BUILD)/Flow/Core.olean" && test -f "$(FLOW_BUILD)/Flow/Schema.olean" && test -f "$(FLOW_BUILD)/Flow/Morphism.olean" || \
		$(MAKE) -C "$(FLOW_DIR)" build/lean/Flow.olean

# Gate 0b: SOUND-local Bilattice & Hohfeldian bv_decide SSOT (`0 sorryAx`, leanchecker)
$(BILATTICE_OLEAN): Sound/Bilattice.lean | $(BUILD_DIR)
	@echo "=== [Gate 0] SOUND Bilattice & Hohfeldian bv_decide SSOT Audit ==="
	@mkdir -p $(LEAN_DIR)/Sound
	@awk 'index($$0,"sorry") && !index($$0,"sorryAx") {print "ERROR: sorry found in Sound/Bilattice.lean line " NR > "/dev/stderr"; bad=1} END {exit bad}' Sound/Bilattice.lean
	@! grep -rnE '\badmit\b' Sound/Bilattice.lean || (echo "ERROR: admit found in Sound/Bilattice.lean!" >&2 && exit 1)
	$(LEAN) -DwarningAsError=true --root=. -o $@ Sound/Bilattice.lean || (rm -f $@ && exit 1)
	LEAN_PATH="$(LEAN_DIR)" $(LEANCHECKER) Sound.Bilattice || (rm -f $@ && exit 1)
	@python3 -c 'import re; ns=[]; thms=[]; [ (ns.append(m.group(1).split(".")) if (m:=re.match(r"^\s*namespace\s+(\S+)",l)) else (ns.pop() if (me:=re.match(r"^\s*end\s+(\S+)",l)) and ns and me.group(1)==".".join(ns[-1]) else (thms.append(".".join([x for xs in ns for x in xs]+[m2.group(1)])) if (m2:=re.match(r"^\s*theorem\s+(\S+)",l)) else None))) for l in open("Sound/Bilattice.lean") ]; assert len(thms) >= 20, f"Expected >=20 theorems, got {len(thms)}"; open("$(BUILD_DIR)/.bilattice_axioms.lean","w").write("import Sound.Bilattice\n" + "".join(f"#print axioms {t}\n" for t in thms)); print(f"  [AUDIT] Collected {len(thms)} Sound.Bilattice theorems for axiom verification")'
	@ax_b="$$(LEAN_PATH="$(LEAN_DIR)" $(LEAN) -DwarningAsError=true $(BUILD_DIR)/.bilattice_axioms.lean)" && \
		! printf '%s\n' "$$ax_b" | grep -q 'sorryAx' || (echo "ERROR: sorryAx detected in Sound.Bilattice!" >&2 && exit 1)

# Gate 1: Pure FLOW SSOT Config (`Sound.lean`) + `leanchecker` + 512b C17 Schema ABI
$(SOUND_OLEAN): Sound.lean $(BILATTICE_OLEAN) | $(BUILD_DIR) upstream-deps
	@echo "=== [Gate 1] Lean 4 FLOW+Sound Config, leanchecker & Schema ABI Gate ==="
	@if grep -Eq '^[[:space:]]*(theorem|lemma|axiom)[[:space:]]' Sound.lean; then \
		echo "ERROR: Sound.lean violates pure-config rule (found local theorem/lemma/axiom)!" >&2; exit 1; \
	fi
	@if grep -rnE '\b(sorry|admit)\b' Sound.lean; then \
		echo "ERROR: Found forbidden placeholder in Sound.lean!" >&2; exit 1; \
	fi
	LEAN_PATH="$(FLOW_BUILD):$(LEAN_DIR)" $(LEAN) -DwarningAsError=true --root=. -o $@ Sound.lean || (rm -f $@ && exit 1)
	LEAN_PATH="$(FLOW_BUILD):$(LEAN_DIR)" $(LEANCHECKER) Sound || (rm -f $@ && exit 1)

$(SCHEMA_HDR): $(SOUND_OLEAN)
	printf 'import Sound\n#eval do if !(← Sound.emitSchemaOnly) then IO.Process.exit 1\n' | \
		LEAN_PATH="$(FLOW_BUILD):$(LEAN_DIR)" $(LEAN) -DwarningAsError=true --stdin
	@test -s $@
	@! grep -q 'memcpy' $@ || (echo "ERROR: memcpy leaked into freestanding sound_schema_abi.h!" >&2 && exit 1)

lean: $(SCHEMA_HDR)

# Gate 2: Ring-0 Freestanding Pure-GPR (Core-only!), C++20 ABI & clang-tidy Static Analysis Gate
$(BUILD_DIR)/sound_ring0.o: sound.h VERSION $(SCHEMA_HDR) | $(BUILD_DIR)
	@echo "=== [Gate 2] Ring-0 Freestanding Pure-GPR, C++20 ABI & clang-tidy Gate ==="
	printf '#include "sound.h"\n' | $(CC) $(FREESTANDING_CFLAGS) -x c -c - -o $@
	@test -z "$$(nm -u $@)" || (echo "ERROR: Undefined symbols in Ring-0 object:" && nm -u $@ && exit 1)
	@! objdump -d $@ | grep -Eq '[[:space:]](udiv|sdiv|divq?|idivq?|blr|br)[[:space:]]' || \
		(echo "ERROR: Division or indirect jump/call emitted in Ring-0 kernel!" && objdump -d $@ && exit 1)
	@! objdump -d $@ | grep -Eq '([[:space:]]|%)(fadd|fsub|fmul|fdiv|scvtf|ucvtf|fcvtzs|fcvtzu|fcmp|fmov|addsd|subsd|mulsd|divsd|cvtsi2sd|cvttsd2si|xmm[0-9]|ymm[0-9]|zmm[0-9])' || \
		(echo "ERROR: FPU/SIMD instruction emitted in Ring-0 kernel!" && objdump -d $@ && exit 1)
	@printf '#include "sound.h"\nint sound_cpp20_probe(void) { return (int)(sizeof(SoundArena) + alignof(SoundCell64)); }\n' | \
		$(CXX) -x c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -I. -I$(BUILD_DIR) -isystem "$(FLOW_CORE_INC)" -c - -o $(BUILD_DIR)/.cpp20_probe.o
	@rm -f $(BUILD_DIR)/.cpp20_probe.o
	@echo "  [PASS] 0 undef, 0 udiv/sdiv, 0 BLR/BR, 0 FPU/SIMD, Core-only C++20 16KB/64B ABI clean"

$(STATIC_STAMP): $(C_SOURCES) $(SCHEMA_HDR) | $(BUILD_DIR)
	$(CLANG_TIDY) -checks='-*,clang-analyzer-*,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling' \
		--header-filter='sound\.h' -warnings-as-errors='*' \
		sound.c sound_lean_jsi.c \
		-- -std=c17 -I. -I$(BUILD_DIR) -isystem "$(FLOW_CORE_INC)" -isystem "$(FLOW_SDK_INC)" -isystem "$(LEAN_SYSROOT)/include"
	@touch $@

ring0: $(BUILD_DIR)/sound_ring0.o $(STATIC_STAMP)

# Gate 3: Pure-GPR libsound.a & Zero-Lean-Runtime Standalone `build/sound`
$(BUILD_DIR)/libsound.a: $(BUILD_DIR)/sound_ring0.o $(STATIC_STAMP)
	@rm -f $@
	$(AR) rcs $@ $(BUILD_DIR)/sound_ring0.o
	@! nm -u $@ | grep -Eq '(^|[^A-Za-z0-9_])(_?malloc|_?calloc|_?realloc|_?free|_?strtod|_?strtof|_?atof|_?lean_)(@|$$)' || \
		(echo "ERROR: Forbidden runtime/heap/float/lean symbol in libsound.a!" && nm -u $@ && rm -f $@ && exit 1)

$(LIBSOUND_DYN): $(BUILD_DIR)/sound_ring0.o $(STATIC_STAMP)
	$(CC) $(RING0_SHARED_LDFLAGS) $(BUILD_DIR)/sound_ring0.o -o $@
	@! nm -u $@ | grep -Eq '(^|[^A-Za-z0-9_])(_?malloc|_?calloc|_?realloc|_?free|_?strtod|_?strtof|_?atof|_?lean_)(@|$$)' || \
		(echo "ERROR: Forbidden runtime/heap/float/lean symbol in $(LIBSOUND_DYN)!" && nm -u $@ && rm -f $@ && exit 1)
	@if [ "$(UNAME_S)" = "Darwin" ] && command -v otool >/dev/null 2>&1; then \
		! otool -L $@ | grep -qi 'leanshared' || (echo "ERROR: leanshared leaked into $@!" && otool -L $@ && rm -f $@ && exit 1); \
	elif command -v ldd >/dev/null 2>&1; then \
		! ldd $@ | grep -qi 'leanshared' || (echo "ERROR: leanshared leaked into $@!" && ldd $@ && rm -f $@ && exit 1); \
	fi

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
		LEAN_PATH="$(FLOW_BUILD):$(LEAN_DIR)" $(LEAN) --load-dynlib=$(LIBSOUND_JSI) -DwarningAsError=true --stdin
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
