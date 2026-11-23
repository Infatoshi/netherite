# The textured ray cast's gate (lane/raycast, raycast.h): pool_gate --raycast over semcam-gate's recordings (the
# overworld speedrun start, swimming in water and lava, the Nether, the End, a village), the cast after every tick
# both ways at each size (raycast_ref, every cell through the engine's chunk lookup and renderstate_compute's light;
# raycast, the runtime's fast path) and held byte-equal, and every row still test_snapshots' (the cast reads the env
# and changes nothing). --raycast-each: an episode checks one size, so a recording's sizes run on different envs at
# once; each (recording, size) runs twice and gives one digest (36 episodes on 16 envs, with resets between). The
# textures: out/java/render/forest-fast's table and atlas (the renderer's) and out/assets (made first).
#
#   make -C csrc/runtime raycast-gate [RCRECS="a b"] [RCSPECS="32x32 64x64 48x32"] [RCSPECS32="32x32:f32 ..."]
# (RCSPECS32: the FP32 instance's sizes, the same gate in a run of its own; RCSPECS32= none)
RCRECS  ?= $(SEMRECS)
RCSPECS ?= 32x32 64x64 48x32
# the FP32 instance's specs (lane/castf32, raycast.h "The two instances"), gated in a run of their own after RCSPECS'
# (so the FP64 lines are as they were); RCSPECS32= skips them
RCSPECS32 ?= 32x32:f32 64x64:f32 48x32:f32 16x16:f32
# the fixed and FP16 instances' specs (lane/castexact), a third run; RCSPECSX= skips them
RCSPECSX ?= 32x32:fx 16x16:fx 32x32:f16 16x16:f16
# the FP16 instance's larger sizes (lane/cxf16), a fourth run (pool_gate takes four specs a run): 128x128 is where a
# half's direction component small enough that its reciprocal overflows first met the eye on a boundary; RCSPECS16= skips
RCSPECS16 ?= 64x64:f16 128x128:f16

.PHONY: raycast-gate cast-cmp

# the casts' two instances against each other (lane/castf32, pool_gate --cast-cmp): after every tick of the gate's
# recordings, the semantic camera at CMPSEM and the textured cast at each of CMPSPECS by FP64 and by FP32, what
# differs counted by kind (a line a recording and the total, in parts per million)
#   make -C csrc/runtime cast-cmp [RCRECS="a b"] [CMPSPECS="64x64 16x16"] [CMPSEM=64x36:64]
CMPSPECS ?= 64x64 32x32 16x16
CMPSEM   ?= 64x36:64
cast-cmp: pool
	@$(MAKE) --no-print-directory -s -C $(RT_NATIVE) assets
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) pool-refs RECS="$(RCRECS)"
	cd $(RT_ROOT) && $(RT_OUT)/pool_gate -n 16 -j 16 --ticks 1 --episodes $(words $(RCRECS)) --cast-cmp --semcam $(CMPSEM) \
	  $(foreach s,$(CMPSPECS),--raycast $(s)) $(RCRECS)

raycast-gate: pool $(if $(LIBPIC),$(RT_OUT)/pool_gate_pic)
	@$(MAKE) --no-print-directory -s -C $(RT_NATIVE) assets
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) pool-refs RECS="$(RCRECS)"
	cd $(RT_ROOT) && $(POOL_GATE) -n 16 -j 16 --ticks 1 --episodes $(words $(foreach s,$(RCSPECS),$(RCRECS) $(RCRECS))) --raycast-each \
	  $(foreach s,$(RCSPECS),--raycast $(s)) $(RCRECS)
	$(if $(strip $(RCSPECS32)),cd $(RT_ROOT) && $(RT_OUT)/pool_gate -n 16 -j 16 --ticks 1 \
	  --episodes $(words $(foreach s,$(RCSPECS32),$(RCRECS) $(RCRECS))) --raycast-each $(foreach s,$(RCSPECS32),--raycast $(s)) $(RCRECS))
	$(if $(strip $(RCSPECSX)),cd $(RT_ROOT) && $(RT_OUT)/pool_gate -n 16 -j 16 --ticks 1 \
	  --episodes $(words $(foreach s,$(RCSPECSX),$(RCRECS) $(RCRECS))) --raycast-each $(foreach s,$(RCSPECSX),--raycast $(s)) $(RCRECS))
	$(if $(strip $(RCSPECS16)),cd $(RT_ROOT) && $(RT_OUT)/pool_gate -n 16 -j 16 --ticks 1 \
	  --episodes $(words $(foreach s,$(RCSPECS16),$(RCRECS) $(RCRECS))) --raycast-each $(foreach s,$(RCSPECS16),--raycast $(s)) $(RCRECS))

# the cast on a camera with no forward (lane/crash2): every 25th row of RNRECS cast again with a NaN yaw, a NaN pitch and
# both, each frame held equal to the same camera at 0, in the FP64 and the FP32 instance. Before the fix a NaN yaw gave
# every ray a NaN direction and the cast never returned (a search stood still for 15 minutes), so the command runs under
# timeout RNSECS and a hang fails it (rc 124); RAYCAST-NAN PASS or FAIL
#   make -C csrc/runtime raycast-nan-gate [RNRECS="a b"] [RNSECS=120]
RNRECS ?= nether-trip-s1 sr-speedrun-s1
RNSECS ?= 120

.PHONY: raycast-nan-gate

raycast-nan-gate: pool $(if $(LIBPIC),$(RT_OUT)/pool_gate_pic)
	@$(MAKE) --no-print-directory -s -C $(RT_NATIVE) assets
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) pool-refs RECS="$(RNRECS)"
	@cd $(RT_ROOT) && timeout $(RNSECS) $(POOL_GATE) -n 4 -j 4 --ticks 1 --episodes $(words $(RNRECS) $(RNRECS)) --raycast-nan --raycast-each \
	  --raycast 32x32 --raycast 32x32:f32 $(RNRECS) && echo "RAYCAST-NAN PASS" || { echo "RAYCAST-NAN FAIL (rc $$?)"; exit 1; }

# fast's cast gate (lane/fastcast, rcfast.sh): each recording replayed full and then fast (engine.tick_scope fast, its
# rows held but for d.cseed, d.cw and d.cmath), the cast after every row, every row's cast held equal to full's.
# fast fails it today (it skips the client world's light check): the default recordings are three golden-chain
# segments where it does (DEVLOG 2026-10-24 lane/fastcast: 2,556 of 497,217 rows over both chains).
#   make -C csrc/runtime raycast-fast-gate [RCFRECS="a b"] [RCFSPEC=32x32]
RCFRECS ?= gold-g29-s42 gold-g7-s42 gold-g8-s42
RCFSPEC ?= 32x32

.PHONY: raycast-fast-gate

raycast-fast-gate: pool
	@$(MAKE) --no-print-directory -s -C $(RT_NATIVE) assets
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) pool-refs RECS="$(RCFRECS)"
	@bash $(RT_DIR)/rcfast.sh -s $(RCFSPEC) $(RT_OUT)/rcfast $(RCFRECS)

# observation parity (lane/parity, rcparity.sh; GOAL.md "Done means" 4): the oracle's own ray cast (RayCast.java,
# --raycast: each row's d.rc) against pool_gate's (--raycast-rows), row for row, on pool-gate's recordings at each
# size; a recording's first differing row named, its two frames drawn and surveyed by pxdiff. RCPARITY PASS or FAIL.
#   make -C csrc/runtime raycast-parity [RPRECS="a b"] [RPSIZES="16x16 64x64"] [RPJ=8]
RPRECS  ?= $(shell grep -v '^\#' $(RT_DIR)/pool_gate.list | awk 'NF {print $$1}')
RPSIZES ?= 16x16 64x64
RPJ     ?= 8

.PHONY: raycast-parity

raycast-parity: pool
	@$(MAKE) --no-print-directory -s -C $(RT_NATIVE) assets
	@$(MAKE) --no-print-directory -s -C $(RT_ROOT)/oracle build
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) pool-refs RECS="$(RPRECS)"
	@bash $(RT_DIR)/rcparity.sh -s "$(RPSIZES)" -j $(RPJ) $(RT_OUT)/rcparity $(RPRECS)

# a contact sheet of frames, to look at the cast beside the renderer's (raycast_sheet.c)
$(RT_OUT)/raycast_sheet: $(RT_DIR)/raycast_sheet.c $(RT_NLIB)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(RT_LDFLAGS) -o $@ $(notdir $<) $(RT_NLIB) -lz -lm

pool: $(RT_OUT)/raycast_sheet

# the cast at -O3 (1.55 M instructions a 32x32 cast at configs/tests/gold-fast.yaml against 1.61 M at -O2); the
# arithmetic is the same (-ffp-contract=off). RCXFLAGS: more flags for the cast's objects alone, both instances (its
# compile-time knobs, lane/hostf32: -DRC_W32=8, -DRC_BLOCK=6, ...), with VARIANT=NAME (rlbind.mk) a variant library
# whose engine objects come from ccache: make -C csrc/runtime rlbind VARIANT=w8 RCXFLAGS='-DRC_W32=8'
RCXFLAGS ?=
$(RT_OUT)/obj/raycast.o $(RL_PICR)/raycast.o $(RT_OUT)/obj/raycast_f32.o $(RL_PICR)/raycast_f32.o \
  $(RT_OUT)/obj/raycast_fx.o $(RL_PICR)/raycast_fx.o $(RT_OUT)/obj/raycast_f16.o $(RL_PICR)/raycast_f16.o: RT_CFLAGS += -O3 $(RCXFLAGS)
# the FP16 instance (raycast_f16.c): each _Float16 operation rounded to half (gcc's excess precision 16; clang's float16
# excess precision none), not computed in float and rounded once at an assignment
RC_F16FLAGS := $(if $(findstring clang,$(shell $(RT_CC) --version 2>/dev/null | head -1)),-Xclang -ffloat16-excess-precision=none,-fexcess-precision=16)
# (x86-64: the half <-> float conversions by F16C's instructions, not libgcc's calls: every x86 the runtime runs on has
# them, and they round as the calls do)
RC_F16FLAGS += $(if $(filter x86_64,$(shell uname -m)),-mf16c)
$(RT_OUT)/obj/raycast_f16.o $(RL_PICR)/raycast_f16.o: RT_CFLAGS += $(RC_F16FLAGS)

# The device's cast (lane/rcdev, cuda/raycast): pool_gate_rcd is pool_gate with cuda/raycast linked; its
# --raycast-device casts each spec on the device after every tick too, the frame against raycast_ref's and the seen
# record against raycast_seen's, and each recording's line counts the device casts that were equal
#   make -C csrc/runtime raycast-device-gate [RCRECS="a b"] [RCSPECS="32x32 64x64 48x32"]
RT_RCDOBJ := $(RT_NOUT)/obj/cuda_raycast.o
.PHONY: raycast-device-gate pool-rcd
pool-rcd:
	@$(MAKE) --no-print-directory -s -C $(RT_NATIVE) $(RT_RCDOBJ)
$(RT_RCDOBJ): pool-rcd
$(RT_OUT)/pool_gate_rcd: $(RT_DIR)/pool_gate.c $(RT_DIR)/raycast.h $(RT_DIR)/rchead.h $(RT_DIR)/rcmirror.h $(RT_DIR)/../cuda/raycast/rcdev.h \
  $(RT_OUT)/libpool.a $(RT_DIR)/../tests/rowrec.h $(RT_NLIB) $(RT_DEVOBJ) $(RT_RCDOBJ)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) -DNETHERITE_DEV -DPOOL_GATE_RCD $(RT_LDFLAGS) -o $@ $(notdir $<) $(RT_OUT)/libpool.a \
	  $(RT_DEVOBJ) $(RT_RCDOBJ) $(RT_NLIB) -L$(RT_CUDALIB) -Wl,-rpath,$(RT_CUDALIB) -lcudart -lstdc++ -ldl -lz -lm -lpthread
# the device object first, then the link in a make of its own: a make that builds both reads the object's time from
# before its build (the object's rule has no recipe here) and kept a stale gate (lane/rcdev's first planted bug)
raycast-device-gate: pool
	@$(MAKE) --no-print-directory -s -C $(RT_NATIVE) $(RT_RCDOBJ)
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) $(RT_OUT)/pool_gate_rcd
	@$(MAKE) --no-print-directory -s -C $(RT_NATIVE) assets
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) pool-refs RECS="$(RCRECS)"
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(or $(CUDA_DEV),0) $(RT_OUT)/pool_gate_rcd -n 16 -j 16 --ticks 1 \
	  --episodes $(words $(foreach s,$(RCSPECS),$(RCRECS) $(RCRECS))) --raycast-each --raycast-device \
	  $(foreach s,$(RCSPECS),--raycast $(s)) $(RCRECS)
	$(if $(strip $(RCSPECS32)),cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(or $(CUDA_DEV),0) $(RT_OUT)/pool_gate_rcd -n 16 -j 16 --ticks 1 \
	  --episodes $(words $(foreach s,$(RCSPECS32),$(RCRECS) $(RCRECS))) --raycast-each --raycast-device \
	  $(foreach s,$(RCSPECS32),--raycast $(s)) $(RCRECS))
	$(if $(strip $(RCSPECSX)),cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(or $(CUDA_DEV),0) $(RT_OUT)/pool_gate_rcd -n 16 -j 16 --ticks 1 \
	  --episodes $(words $(foreach s,$(RCSPECSX),$(RCRECS) $(RCRECS))) --raycast-each --raycast-device \
	  $(foreach s,$(RCSPECSX),--raycast $(s)) $(RCRECS))
	$(if $(strip $(RCSPECS16)),cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(or $(CUDA_DEV),0) $(RT_OUT)/pool_gate_rcd -n 16 -j 16 --ticks 1 \
	  --episodes $(words $(foreach s,$(RCSPECS16),$(RCRECS) $(RCRECS))) --raycast-each --raycast-device \
	  $(foreach s,$(RCSPECS16),--raycast $(s)) $(RCRECS))
