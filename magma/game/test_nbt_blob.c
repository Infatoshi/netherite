#include "game/nbt_blob.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "nbt_blob: FAIL: %s\n", message); \
        return 1; \
    } \
} while (0)

int main(void) {
    static const unsigned char all_types[] = {
        10,0,0,
        1,0,1,'b',255,
        2,0,1,'s',0x80,0,
        3,0,1,'i',0x80,0,0,0,
        4,0,1,'l',0x80,0,0,0,0,0,0,0,
        5,0,1,'f',0x7f,0xc0,0,1,
        6,0,1,'d',0xff,0xf0,0,0,0,0,0,0,
        7,0,1,'a',0,0,0,3,0x80,0,0x7f,
        8,0,1,'t',0,1,'x',
        9,0,1,'q',3,0,0,0,2,0,0,0,1,0xff,0xff,0xff,0xff,
        10,0,1,'c',0,
        11,0,1,'j',0,0,0,1,0x7f,0xff,0xff,0xff,
        12,0,1,'k',0,0,0,1,0x7f,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
        0,
    };
    static const unsigned char child[] = {
        10,0,0,8,0,4,'N','a','m','e',0,1,'x',0,
    };
    static const unsigned char wrapped[] = {
        10,0,0,10,0,10,'S','k','u','l','l','O','w','n','e','r',
        8,0,4,'N','a','m','e',0,1,'x',0,0,
    };
    static const unsigned char wrong_root[] = {1,0,0,0};
    static const unsigned char named_root[] = {10,0,1,'x',0};
    static const unsigned char trailing[] = {10,0,0,0,0};
    static const unsigned char negative_array[] = {
        10,0,0,7,0,1,'a',255,255,255,255,0,
    };
    static const unsigned char end_list[] = {
        10,0,0,9,0,1,'q',0,0,0,0,1,0,
    };
    static const unsigned char bad_utf[] = {
        10,0,0,8,0,1,'t',0,1,0xff,0,
    };
    GmNbtBlob source = {0};
    GmNbtBlob copy = {0};
    GmNbtBlob outer = {0};

    CHECK(gm_nbt_blob_validate_root_compound(
              all_types, sizeof all_types),
          "all twelve NBT types validate");
    CHECK(!gm_nbt_blob_validate_root_compound(
              wrong_root, sizeof wrong_root)
              && !gm_nbt_blob_validate_root_compound(
                  named_root, sizeof named_root)
              && !gm_nbt_blob_validate_root_compound(
                  trailing, sizeof trailing)
              && !gm_nbt_blob_validate_root_compound(
                  negative_array, sizeof negative_array)
              && !gm_nbt_blob_validate_root_compound(
                  end_list, sizeof end_list)
              && !gm_nbt_blob_validate_root_compound(
                  bad_utf, sizeof bad_utf),
          "malformed root/type/length/list/UTF inputs are rejected");
    CHECK(gm_nbt_blob_set(&source, child, sizeof child)
              && gm_nbt_blob_copy(&copy, &source)
              && source.data != copy.data
              && source.len == copy.len
              && !memcmp(source.data, copy.data, source.len),
          "blob set/copy retains independent lossless storage");
    CHECK(gm_nbt_blob_wrap_named_compound(
              &outer, "SkullOwner", &source)
              && outer.len == sizeof wrapped
              && !memcmp(outer.data, wrapped, sizeof wrapped),
          "named compound wrapper is byte exact");
    gm_nbt_blob_clear(&outer);
    gm_nbt_blob_clear(&copy);
    gm_nbt_blob_clear(&source);
    puts("nbt_blob: PASS");
    return 0;
}
