# The living hook guard's check (lane/livfault, livguard.c): a pool env whose livings' on_living_update pointers are
# overwritten must stop on the guard's LIVFAULT report (SIGABRT), not jump (the 2026-10-31 19:26 fault); one that is
# not touched must step on. Built against libneth.so as vec_bench is, so LD_LIBRARY_PATH=out/var/NAME runs it on a
# variant (out/var/noguard, -DLIVING_HOOK_GUARD=0: the check fails there).
#
#   make -C csrc/runtime livguard-check [LIVSNAP=out/java/snapshots/gold-g15-s42]
LIVSNAP ?= out/java/snapshots/gold-g15-s42

.PHONY: livguard livguard-check

livguard: $(RT_OUT)/livguard

$(RT_OUT)/livguard: $(RT_DIR)/livguard.c $(RT_DIR)/pool.h $(RL_LIB)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(RT_LDFLAGS) -o $@ livguard.c -L$(RT_OUT) -Wl,-rpath,$(RT_OUT) -lneth -lm

livguard-check: $(RT_OUT)/livguard
	@cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES= $(RT_OUT)/livguard $(LIVSNAP)
