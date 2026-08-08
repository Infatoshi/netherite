#include "inventory_stack_rules.h"
#include "items_core.h"

#include <stdio.h>

int main(void) {
    printf("{\"ok\":true,\"items\":[");
    for (int item = 0; item <= 2267; ++item) {
        ICStack tagged = ic_mk(item, 2, 0);
        ICStack restored;
        ICStack split;
        int round_trip = 1;
        int split_exact = 1;
        if (item) putchar(',');
        if (item != 0) {
            tagged.tag_id = item + 1;
            restored = tagged;
            split = ic_with_count(&tagged, 1);
            round_trip = restored.item == tagged.item
                && restored.count == tagged.count
                && restored.meta == tagged.meta
                && ic_stack_equal(&restored, &tagged);
            split_exact = split.count == 1
                && split.tag_id == tagged.tag_id
                && ic_stack_equal(&split, &tagged);
        }
        printf("[%d,%d,%d,%d,%d]",
            isr_max_stack_size(item, 0),
            isr_has_subtypes(item), isr_is_damageable(item),
            round_trip, split_exact);
    }
    printf("]}\n");
    return 0;
}
