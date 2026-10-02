# The semantic camera's gate (lane/semteach, semcam.h): pool_gate --semcam over a few recordings (the overworld
# speedrun start, swimming in water and lava, the Nether, the End, a village), the camera cast after every tick both
# ways (semcam_cast_ref, every cell through the engine's chunk lookup; semcam_cast, the runtime's fast path) and held
# byte-equal, every episode's casts digested (a recording run twice gives one digest), and every row still
# test_snapshots' (the cast reads the env and changes nothing).
#
#   make -C csrc/runtime semcam-gate [SEMRECS="a b"] [SEMSPEC=64x36:64]
SEMRECS ?= sr-speedrun-s1 swim-s42 phys-lavaswim-s1 nether-trip-s1 end-dragon-s1 cov-villagerep-s42
SEMSPEC ?= 64x36:64

.PHONY: semcam-gate

semcam-gate: pool
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) pool-refs RECS="$(SEMRECS)"
	cd $(RT_ROOT) && $(RT_OUT)/pool_gate -n 12 -j 12 --ticks 1 --episodes 12 --semcam $(SEMSPEC) $(SEMRECS)
