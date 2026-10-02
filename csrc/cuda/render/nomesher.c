/* The judges' plugin has no device mesher (render_no_mesher: a frame
 * with a mesh feed fails, and play draws it in C): the calls host.cu
 * makes into cuda/meshing, which the plugin never reaches, so it links
 * host.cu's object without the mesher's cubin. */
#include "../meshing/meshing.h"

#include <stdlib.h>

struct meshing *meshing_new(void *stream, int max_slots, const char *cubin)
{
    (void)stream; (void)max_slots; (void)cubin;
    return NULL;
}
void meshing_free(struct meshing *m) { (void)m; }
int meshing_context(struct meshing *m, const struct rb_mesher *t) { (void)m; (void)t; abort(); }
void meshing_forget(struct meshing *m, int slot) { (void)m; (void)slot; }
void meshing_begin(struct meshing *m) { (void)m; abort(); }
int meshing_add(struct meshing *m, int slot, const struct meshfeed_out *f) { (void)m; (void)slot; (void)f; abort(); }
int meshing_count_start(struct meshing *m) { (void)m; abort(); }
void meshing_set_tab(struct meshing *m, int on) { (void)m; (void)on; }
int meshing_count_wait(struct meshing *m, const struct meshing_count **counts, int *n)
{
    (void)m; (void)counts; (void)n;
    abort();
}
int meshing_write(struct meshing *m, int32_t *const *out) { (void)m; (void)out; abort(); }
const struct meshing_stats *meshing_stats(struct meshing *m) { (void)m; abort(); }
