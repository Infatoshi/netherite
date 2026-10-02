# The device mesher's table mode (cuda/meshing table.h): its device half
# (table.c) linked into mesh.cubin with the mesher's (meshing.mk, included
# before this file: its recipe reads MESH_BC, MESH_API and MESH_GLOBALS_OK
# when it runs), and its host half (table_host.c) linked into cuda_render.o
# with host.c (render.mk reads MESH_TAB_OBJ).
MESH_BC   += $(MESH_OUT)/table.bc
MESH_API  := $(MESH_API),mesh_tab_cache,mesh_tab_list,mesh_tab_draw,mesh_tab_draw_box,mesh_tab_draw_door,mesh_tab_draw_fluid,mesh_tab_draw_gen,mesh_tab_place,mesh_tab_copy
MESH_GLOBALS_OK := $(MESH_GLOBALS_OK)|mtab_pb|mtab_bt|mtab_lb
MESH_TAB_OBJ := $(OUT)/obj/cuda_meshing_table.o

$(MESH_CUBIN): $(MESH_OUT)/table.bc
$(MESH_OUT)/table.bc: $(MESH_DIR)/table.c
	@mkdir -p $(MESH_OUT)
	$(MESH_LLVM)/clang $(MESH_DFLAGS) -fconvergent-functions -fno-builtin -MMD -MP -emit-llvm -c -o $@ $<
-include $(MESH_OUT)/table.d

$(MESH_TAB_OBJ): $(MESH_DIR)/table_host.c $(MESH_DIR)/table.h $(MESH_DIR)/kernels.h engine/render_blocks.h engine/render_blocks_int.h
	@mkdir -p $(dir $@)
	$(CCC) $(filter-out -include %envheap.h,$(CFLAGS)) -I$(CUDA_BIN)/../include -c -o $@ $<
$(OUT)/obj/cuda_meshing_host.o: $(MESH_DIR)/table.h
