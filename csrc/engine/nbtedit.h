/* nbtjson reads a tree but has no key-replacing setter: nbt_put always appends,
 * and the struct is opaque, so a caller cannot overwrite a member in place. The
 * crafting code needs exactly that, adding one key to a compound that may
 * already hold others (a color inside an armor's display compound,
 * map_is_scaling on a filled map, FadeColors inside an explosion), so this file
 * does it on the canonical text form: render, drop the old member if there is
 * one, append the new one, and parse the result back into a tree.
 *
 * A canonical string is a quoted span with no escapes, so the member scan is
 * unambiguous: a value ends at its closing quote when it is a string, at the
 * matching brace or bracket when it is a compound or list (strings are skipped
 * whole so a quote inside one cannot be mistaken for the end). */
#ifndef NETHERITE_NBTEDIT_H
#define NETHERITE_NBTEDIT_H

#include "nbtjson.h"

/* comp with key set to value; value's tree is consumed either way. comp is left
 * alone, the result is a new tree, or NULL when comp is not a compound. */
nbt *nbt_set_key(const nbt *comp, const char *key, nbt *value);

#endif