# The trainer's binding (lane/rlbind): rlbind.[ch], the pipeline as a shared
# library a Python trainer loads in its own process. Linux and CUDA only;
# never part of native's `all`, clangcheck or the Mac build. Uses pool.mk's
# and pipeline.mk's variables (included first by the Makefile).
#
#   make -C csrc/runtime rlbind      out/runtime/libneth.so (and playview.so beside it)
#   make -C csrc/runtime rl-gate [CONFIG=configs/speedrun.yaml] [N=16] [WARM=20] [STEPS=200] [MCSR=~/dev/mcsr-speedrun]
#          the binding's gate: mcsr-speedrun's Python binding (mcsr/nwrl.py) runs pipe_bench's
#          corpus through libneth.so (its seeded walk, drawn the same way) and its trajectory and
#          frame digests (the frames read by torch from the device buffer, DLPack) must equal
#          pipe_bench --frame-digest's on the same corpus
#   make -C csrc/runtime rl-raw-gate [RAWRECS="..."]   (lane/iron, rlraw.h)
#          raw keyboard and mouse through the binding: each lever's raw acts (csrc/runtime/rawact/NAME.jsonl:
#          the screen's pointer moved by the look, the buttons pressed and released, keys typed) stepped through
#          libneth.so with neth_rl_raw on (mcsr/nwrl_raw_gate.py); every tick's window clicks and closes equal the
#          Java recording's script (oracle/tests/NAME.jsonl) and every row test_snapshots' (pool-refs)
#   make -C csrc/runtime rl-gui-gate [GUIRECS="..."]   (lane/invact, rlgui.h)
#          the gui ops through the binding: each recording's script (agent acts and gui ops, oracle/tests/NAME.jsonl)
#          stepped through libneth.so (mcsr/nwrl_gui_gate.py), every tick's row record equal to test_snapshots'
#          (pool-refs), the role table checked against every open screen's slots
#
# The library is the engine, the pool and the pipeline built position
# independent (out/native/pic, out/runtime/pic: the same sources, -fPIC, and
# the thread-local env pointer at the initial-exec model a loaded library
# may use: its thread-locals, 176 bytes (the link checks 512 at most), fit the static TLS glibc
# keeps for loaded libraries), the device objects of pipeline.mk, and CUDA's
# shared runtime (the static one's thread-locals are page aligned, which a
# loaded library's static TLS cannot be). One libcudart.so.13 serves the
# process: load the library before torch (mcsr/nwrl.py does at import), so
# torch runs on this toolkit's runtime, which is newer than its own.
RL_PICN   := $(RT_ROOT)/out/native/pic
RL_PICR   := $(RT_OUT)/pic
RL_PICDEF := -fPIC -DNETH_ENV_TLS_MODEL='"initial-exec"'
# lane/cpuhot: the library's C (the engine's and the runtime's PIC objects) compiled with gcc's profile feedback from
# the committed profile in csrc/runtime/pgo (native/ the engine's objects, runtime/ the runtime's; made by pgo.mk's
# rl-pgo from a fixed CPU run): branch layout, hot and cold splitting and inlining from the env's own counts, the same
# flags otherwise (-ffp-contract=off, no fast math: every row and frame the same). gcc reads an object's profile
# beside it (out/native/pic/obj/NAME.gcda, out/runtime/pic/NAME.gcda), where the build copies the committed one
# (RL_PGOSTAMP); there ccache finds it, hashes it and serves any tree's compile. A function edited since the profile
# was made compiles as without it (-Wno-coverage-mismatch), one never run in the profile at -O2 as before
# (-fprofile-partial-training); the counts are racy (pgo.mk), so gcc reads them with -fprofile-correction. The
# profile's checksum is in the flags, so a new profile rebuilds every object (the engine's flag stamp, RL_PICSTAMP).
# RL_PGO=0 builds without it; the instrumented build (RL_PGO=gen) is pgo.mk's. maybe-uninitialized's false positives
# move with the inlining: the builds without the profile keep that warning.
RL_PGO      ?= 1
RL_PGODIR   := $(RT_DIR)/pgo
RL_PGOGCDA  := $(if $(filter 1,$(RL_PGO)),$(sort $(wildcard $(RL_PGODIR)/native/*.gcda $(RL_PGODIR)/runtime/*.gcda)))
RL_PGOFLAGS := $(if $(RL_PGOGCDA),-fprofile-use -fprofile-correction -fprofile-partial-training -Wno-missing-profile \
               -Wno-coverage-mismatch -Wno-maybe-uninitialized -DNW_PGO=$(firstword $(shell cat $(RL_PGOGCDA) | cksum)))
# ccache (4.12) refuses -fprofile-partial-training as an unknown profiling option and runs the compiler uncached:
# left out of its hash, it caches (every profiled compile has it, and the profile's checksum is hashed)
RL_PGOCC    := $(if $(RL_PGOGCDA),CCACHE_IGNOREOPTIONS=-fprofile-partial-training )
RL_COMMA    := ,
RL_PGOLD    := $(if $(RL_PGOGCDA),-Wl$(RL_COMMA)-z$(RL_COMMA)keep-text-section-prefix)
RL_PGOGENDIR = $(RT_OUT)/pgogen/prof
ifeq ($(RL_PGO),gen)
RL_PGOFLAGS := -fprofile-generate=$(RL_PGOGENDIR) -fprofile-update=single -Wno-maybe-uninitialized
RL_PGOLD    := -fprofile-generate
endif
# RL_XFLAGS: more flags for both PIC compiles (a measurement's variant; the flag stamps rebuild for it)
RL_XFLAGS   ?=
# lane/xhigh-patch: the library's host flags, on both PIC compiles and every VARIANT's.
# RL_WSPIN: pool.c's worker spin before it sleeps (pool.h POOL_WSPIN, 50 us without it): 0, the workers sleep at once
# (gpu-workstation, 64 workers: +1.45% env-steps/s, 4 rounds apart, the same digest; the spin was 79% of worker_main's samples).
# RL_NATIVE: auto (the default) adds RL_ZEN5 where gcc names the host znver5 (-march=native), 1 always, 0 never: the
# AVX-512BW section scans (obsview.c) and no stack protector or CET (Ubuntu's gcc puts them on; endbr64 on every
# function). The same rows and digests: -ffp-contract=off holds, and no flag here changes a float's rounding. A library
# built so runs only on an AVX-512 Zen 5; each host builds its own out/.
RL_WSPIN    ?= 0
RL_NATIVE   ?= auto
RL_ZEN5     := -march=znver5 -fno-stack-protector -fno-stack-clash-protection -fcf-protection=none
ifeq ($(RL_NATIVE),auto)
RL_NATIVE   := $(if $(filter znver5,$(shell $(RT_CC) -march=native -Q --help=target 2>/dev/null | awk '$$1 == "-march=" {print $$2; exit}')),1,0)
endif
RL_HOSTFLAGS := $(if $(RL_WSPIN),-DPOOL_WSPIN=$(RL_WSPIN)) $(if $(filter 1,$(RL_NATIVE)),$(RL_ZEN5))
# native's own CFLAGS default and the two (quoted for the sub-make's shell)
RL_NCFLAGS := -O2 -g -std=c11 -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -Werror -fPIC -DNETH_ENV_TLS_MODEL=\"initial-exec\" \
              $(RL_PGOFLAGS) $(RL_HOSTFLAGS) $(RL_XFLAGS)
RL_SRC    := pool.c feed.c agent.c pipeline.c view.c gensrv.c rlbind.c rlgui.c moveact.c rlmove.c rlloop.c rlact.c rlraw.c rlscan.c semcam.c raycast.c vecenv.c \
             endview.c vecdragon.c rcmirror.c raycast_f32.c raycast_fx.c raycast_f16.c netherview.c shview.c obsview.c \
             walkto.c shoracle.c
RL_OBJ   := $(patsubst %.c,$(RL_PICR)/%.o,$(RL_SRC))
RL_LIB    := $(RT_OUT)/libneth.so
# pipeline.mk's device objects, their host code position independent (nvcc
# takes -fPIC from its environment, so ccache, which does not hash it, is off)
RL_CUDAN  ?= $(RL_PICN)
RL_CUDA_BASE := $(RL_CUDAN)/obj/cuda_render.o $(RL_CUDAN)/obj/cuda_lighting.o $(RL_CUDAN)/obj/cuda_worldgen.o
RL_CUDA_RC   := $(RL_CUDAN)/obj/cuda_raycast.o
RL_CUDA      := $(RL_CUDA_BASE) $(RL_CUDA_RC)
# A variant library (lane/tooling: 45 variant-build scripts in the lanes' out/ trees, 2026-10-31):
#   make -C csrc/runtime rlbind VARIANT=NAME XFLAGS='-fno-plt' [XLDFLAGS='...']
# builds out/var/NAME/libneth.so with XFLAGS added to the engine's and the runtime's compiles (their PIC objects in
# out/var/NAME/native and out/var/NAME/pic, so the default library and its objects are untouched) and XLDFLAGS to
# the link; the device objects are the default build's (out/native/pic/obj/cuda_*.o, not rebuilt with the flags).
# A changed XFLAGS rebuilds NAME's objects (the engine's flag stamp, out/var/NAME/flags.stamp the runtime's). It runs
# -j RL_VARJ (16) by default. Use it with LD_LIBRARY_PATH=out/var/NAME (ntloop's and vec_bench's RUNPATH yields to
# it), e.g. out/native/abrun -c 'LD_LIBRARY_PATH=out/var/{} out/runtime/vec_bench ...' base noplt. XFLAGS takes no
# single quote. (RL_XFLAGS, lane/cpuhot's, above: VARIANT sets it from XFLAGS; its flag stamp rebuilds for it.)
# RCXFLAGS='-DRC_W32=8 ...' (raycast.mk) adds flags to the cast's objects alone: a cast knob's variant whose engine
# objects are the default's from ccache, all 17 of lane/hostf32's grid built in 29 s.
# lane/macromod: MACROS=0 builds libneth without the macros (csrc/mods/macros): vecenv.c against nomacros.c's empty
# dispatcher (-DNETH_RL_MACROS=0), the policy's action raw controls only. It is the variant out/var/nomacros (VARIANT's
# rules below: its own objects, the default library untouched): LD_LIBRARY_PATH=out/var/nomacros out/runtime/vec_bench
ifeq ($(MACROS),0)
VARIANT := nomacros
XFLAGS  += -DNETH_RL_MACROS=0
endif
RL_XLDFLAGS :=
RL_VSTAMP   :=
RL_NVXFLAGS ?=
ifneq ($(VARIANT),)
RL_VAR      := $(RT_ROOT)/out/var/$(VARIANT)
RL_PICN     := $(RL_VAR)/native
RL_PICR     := $(RL_VAR)/pic
RL_OBJ      := $(patsubst %.c,$(RL_PICR)/%.o,$(RL_SRC))
RL_LIB      := $(RL_VAR)/libneth.so
RL_NCFLAGS  += $(XFLAGS)
RL_XFLAGS   := $(XFLAGS)
RL_XLDFLAGS := $(XLDFLAGS)
RL_VSTAMP   := $(RL_VAR)/flags.stamp
ifneq ($(RL_NVXFLAGS),)
RL_CUDA_RC  := $(RL_PICN)/obj/cuda_raycast.o
RL_CUDA     := $(RL_CUDA_BASE) $(RL_CUDA_RC)
endif
RL_VARJ     ?= 16
MAKEFLAGS   += -j$(RL_VARJ)
$(shell mkdir -p $(RL_PICR) && { printf '%s\n' '$(XFLAGS) | $(XLDFLAGS) | $(RCXFLAGS) | $(RL_NVXFLAGS)' | cmp -s - $(RL_VSTAMP) || \
  printf '%s\n' '$(XFLAGS) | $(XLDFLAGS) | $(RCXFLAGS) | $(RL_NVXFLAGS)' > $(RL_VSTAMP); })
endif
# the runtime's PIC objects rebuild when their flags change (the profile's checksum among them), as the engine's do
RL_PICSTAMP := $(RL_PICR)/flags.stamp
RL_PICFLAGS := $(subst ',,$(RT_CC) $(RT_CFLAGS) $(RL_PICDEF) $(RL_PGOFLAGS) $(RL_HOSTFLAGS) $(RL_XFLAGS))
$(shell mkdir -p $(RL_PICR) && { printf '%s\n' '$(RL_PICFLAGS)' | cmp -s - $(RL_PICSTAMP) || printf '%s\n' '$(RL_PICFLAGS)' > $(RL_PICSTAMP); })
# the profile beside the objects (RL_PGOFLAGS)
RL_PGOSTAMP := $(if $(RL_PGOGCDA),$(RL_PICR)/pgo.stamp)
CONFIG_RL ?= $(RT_ROOT)/configs/speedrun.yaml
MCSR      ?= $(HOME)/dev/mcsr-speedrun
WARM      ?= 20
STEPS     ?= 200

.PHONY: rlbind rl-pic rl-cuda rl-gate rl-gui-gate rl-act-gate rl-raw-gate

# not in `all`: the PIC device objects build without ccache (minutes); make rlbind

rlbind: $(RL_LIB) $(if $(VARIANT),,$(RT_OUT)/playview.so)

$(RL_PICR)/pgo.stamp: $(RL_PGOGCDA)
	@mkdir -p $(RL_PICN)/obj $(RL_PICR) && rm -f $(RL_PICN)/obj/*.gcda $(RL_PICR)/*.gcda
	@cp $(filter $(RL_PGODIR)/native/%,$^) $(RL_PICN)/obj/
	@cp $(filter $(RL_PGODIR)/runtime/%,$^) $(RL_PICR)/
	@touch $@

# engine/lang.c includes ../../out/native/lang_generated.h, the plain build's: made first, or a fresh tree's PIC build
# at -j16 can compile lang.c before it exists
rl-pic: $(RL_PGOSTAMP)
	@$(MAKE) --no-print-directory -s -C $(RT_NATIVE) $(RT_NOUT)/lang_generated.h
	@$(RL_PGOCC)$(MAKE) --no-print-directory -s -C $(RT_NATIVE) OUT=$(RL_PICN) CFLAGS='$(RL_NCFLAGS)' $(RL_PICN)/libnetherite.a $(RL_PICN)/obj/dev.o

# (an empty recipe: make reads the archive's time again after rl-pic, so an engine edit relinks in the same run; with
# no recipe it kept the old time and the library linked on the next make)
$(RL_PICN)/libnetherite.a $(RL_PICN)/obj/dev.o: rl-pic ;

rl-cuda:
	@NVCC_APPEND_FLAGS="-Xcompiler -fPIC" $(MAKE) --no-print-directory -s -C $(RT_NATIVE) OUT=$(RL_CUDAN) CCACHE= CFLAGS='$(RL_NCFLAGS)' $(RL_CUDA_BASE) $(if $(RL_NVXFLAGS),,$(RL_CUDA_RC))
ifneq ($(RL_NVXFLAGS),)
	@NVCC_APPEND_FLAGS="-Xcompiler -fPIC $(RL_NVXFLAGS)" $(MAKE) --no-print-directory -s -C $(RT_NATIVE) OUT=$(RL_PICN) CCACHE= CFLAGS='$(RL_NCFLAGS)' $(RL_CUDA_RC)
endif

# the instrumented build (pgo.mk) links the device objects the plain build made
ifneq ($(RL_PGO),gen)
$(RL_CUDA): rl-cuda
endif

$(RL_PICR)/%.o: $(RT_DIR)/%.c $(RT_DIR)/pipeline.h $(RT_DIR)/pool.h $(RT_DIR)/view.h $(RT_DIR)/rlbind.h $(RT_DIR)/rlgui.h $(RT_DIR)/rlgui_int.h $(RT_DIR)/moveact.h $(RT_DIR)/rlmove.h \
  $(RT_DIR)/rlloop.h $(RT_DIR)/rlact.h $(RT_DIR)/rlact_int.h $(RT_DIR)/semcam.h $(RT_DIR)/raycast.h $(RT_DIR)/rlraw.h $(RT_DIR)/rlscan.h $(RT_DIR)/vecenv.h \
  $(RT_DIR)/endview.h $(RT_DIR)/vecdragon.h $(RT_DIR)/rchead.h $(RT_DIR)/rcmirror.h $(RT_DIR)/../cuda/raycast/rcdev.h $(RL_VSTAMP) $(RL_PICSTAMP) $(RL_PGOSTAMP) | pool-native
	@mkdir -p $(RL_PICR)
	cd $(RT_DIR) && $(RL_PGOCC)$(RT_CCC) $(RT_CFLAGS) $(RL_PICDEF) $(RL_PGOFLAGS) $(RL_HOSTFLAGS) $(RL_XFLAGS) -MMD -MP -c -o $@ $(notdir $<)

# the dev ops (engine/dev.c) too, for neth_rl_make_ex's NETH_RL_DEV (lane/invact: the gui gate's dev recordings and the
# scenario starts; neth_rl_make refuses a dev start as the product does)
# rlcompat.ld: the old nwrl_* names as aliases, and libnwrl.so a link to the library, for the trainers that load it by
# the old file and names (lane/neth-refactor)
$(RL_LIB): $(RL_OBJ) $(RL_PICN)/libnetherite.a $(RL_PICN)/obj/dev.o $(RL_CUDA) $(RT_DIR)/rlcompat.ld
	$(RT_CC) -shared $(RT_LDFLAGS) $(RL_PGOLD) $(RL_XLDFLAGS) -Wl,-Bsymbolic-functions -o $@ $(RL_OBJ) $(RL_PICN)/obj/dev.o -Wl,--whole-archive $(RL_PICN)/libnetherite.a \
	  -Wl,--no-whole-archive $(RL_CUDA) -L$(PL_CUDALIB) -Wl,-rpath,$(PL_CUDALIB) -lcudart -lcuda -lstdc++ -ldl -lrt -lz -lm -lpthread \
	  $(RT_DIR)/rlcompat.ld
	ln -sfn $(notdir $@) $(dir $@)libnwrl.so
	@# the thread-locals within glibc's static TLS surplus for a loaded library (glibc.rtld.optional_static_tls, 512
	@# bytes): past it the trainer's dlopen fails "cannot allocate memory in static TLS block" (lane/raycast's 9,744
	@# bytes, 2026-10-16); keep large per-thread state on the heap, one pointer a thread
	@tls=$$(readelf -lW $@ | awk '$$1 == "TLS" {print $$6}'); if [ -n "$$tls" ] && [ $$(($$tls)) -gt 512 ]; then \
	  echo "$@: $$(($$tls)) bytes of thread-locals, over the 512 a loaded library may use"; rm -f $@; exit 1; fi

-include $(wildcard $(RL_PICR)/*.d)

rl-gate: rlbind pipeline
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(RT_OUT)/pipe_bench --config $(CONFIG_RL) --ns $(N) --warm $(WARM) --steps $(STEPS) \
	  --frame-digest > $(RT_OUT)/rl-gate.bench.txt && grep -E "digest|FAIL" $(RT_OUT)/rl-gate.bench.txt
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(CUDA_DEV) NETHERITE=$(RT_ROOT) PYTHONPATH=$(MCSR) UV_CACHE_DIR=$(RT_CACHE)/uv uv run --no-sync --frozen --project $(MCSR) \
	  python -m mcsr.nwrl_gate --lib $(RL_LIB) --config $(CONFIG_RL) --n $(N) --warm $(WARM) --steps $(STEPS) --bench $(RT_OUT)/rl-gate.bench.txt

# the gui gate's recordings: crafting (2x2 and 3x3, every recipe), chests, furnaces, drops and drags (lane/invact)
GUIRECS ?= cat-recipes-s1 craft-spam-s1 chest-toggle-s1 cov-containers-s1 cat-clicks-s1 cont-wbfull-s1 furnace-break-s1 \
  chest-break-s1 cat-charcoal-s1 achieve-s1 container-break-s1 containers2-s42
rl-gui-gate: rlbind
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) pool-refs RECS="$(GUIRECS)"
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(CUDA_DEV) NETHERITE=$(RT_ROOT) PYTHONPATH=$(MCSR) UV_CACHE_DIR=$(RT_CACHE)/uv uv run --no-sync --frozen --project $(MCSR) \
	  python -m mcsr.nwrl_gui_gate --lib $(RL_LIB) --config $(RT_ROOT)/config.yaml --refdir $(RT_OUT)/ref --work $(RT_OUT)/guigate $(GUIRECS)

# the action compiler's levers (lane/guiact, rlact.h): each lever script (guiact/NAME.jsonl: the compiler's ops and
# its switches) stepped through libneth.so; the ops it gives each tick equal to the recording's expanded script
# (oracle/tests/NAME.jsonl, recorded by Java) and every tick's row record equal to test_snapshots' (mcsr/nwrl_act_gate.py)
ACTRECS ?= guiact-move-s1 guiact-item-s1 guiact-recipe-s1 guiact-mask-s1
rl-act-gate: rlbind
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) pool-refs RECS="$(ACTRECS)"
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(CUDA_DEV) NETHERITE=$(RT_ROOT) PYTHONPATH=$(MCSR) UV_CACHE_DIR=$(RT_CACHE)/uv uv run --no-sync --frozen --project $(MCSR) \
	  python -m mcsr.nwrl_act_gate --lib $(RL_LIB) --config $(RT_ROOT)/configs/speedrun.yaml --refdir $(RT_OUT)/ref --work $(RT_OUT)/actgate $(ACTRECS)

# the raw gate's recordings (lane/iron): the inventory's 2x2 grid (a shift-click, single places, a hotbar key and a
# drop over the hovered slot, a right drag, a click outside the window) and a furnace and a table
RAWRECS ?= rawact-inv-s1 rawact-furnace-s1
rl-raw-gate: rlbind
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) pool-refs RECS="$(RAWRECS)"
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(CUDA_DEV) NETHERITE=$(RT_ROOT) PYTHONPATH=$(MCSR) UV_CACHE_DIR=$(RT_CACHE)/uv uv run --no-sync --frozen --project $(MCSR) \
	  python -m mcsr.nwrl_raw_gate --lib $(RL_LIB) --config $(RT_ROOT)/configs/speedrun.yaml --refdir $(RT_OUT)/ref --work $(RT_OUT)/rawgate $(RAWRECS)
