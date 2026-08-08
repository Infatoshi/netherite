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
    static const char *const adventure_blocks[] = {
        "minecraft:stone", "minecraft:log"
    };
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
    static const unsigned char leather_color[] = {
        10,0,0,
        10,0,7,'d','i','s','p','l','a','y',
        3,0,5,'c','o','l','o','r',0,0x10,0x20,0x30,
        0,0,
    };
    static const unsigned char entity_item_tag[] = {
        10,0,0,
        10,0,9,'E','n','t','i','t','y','T','a','g',
          1,0,5,'S','m','a','l','l',1,
          9,0,3,'P','o','s',6,0,0,0,3,
            0x3f,0xf8,0,0,0,0,0,0,
            0x40,0,0,0,0,0,0,0,
            0xc0,0x08,0,0,0,0,0,0,
          10,0,4,'P','o','s','e',
            9,0,4,'H','e','a','d',5,0,0,0,3,
              0x41,0x20,0,0,0x41,0xa0,0,0,0x41,0xf0,0,0,
            0,
          9,0,10,'A','r','m','o','r','I','t','e','m','s',10,0,0,0,2,
            0,
            8,0,2,'i','d',0,23,
              'm','i','n','e','c','r','a','f','t',':',
              'd','i','a','m','o','n','d','_','b','o','o','t','s',
            1,0,5,'C','o','u','n','t',1,
            2,0,6,'D','a','m','a','g','e',0,7,
            0,
          0,
        0,
    };
    GmNbtBlob source = {0};
    GmNbtBlob copy = {0};
    GmNbtBlob outer = {0};
    GmNbtBlob dye = {0};
    GmNbtBlob item_tag = {0};
    GmNbtBlob entity_tag = {0};
    GmNbtBlob pose = {0};
    GmNbtBlob boots = {0};
    GmNbtBlob edited = {0};
    GmNbtBlob display = {0};
    int32_t color = 0;
    double number = 0.0;
    double list[4] = {0};
    size_t list_count = 0;
    int number_type = 0;
    char text[64];

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
    CHECK(gm_nbt_blob_set(&dye, leather_color, sizeof leather_color)
              && gm_nbt_blob_find_compound_int(
                  &dye, "display", "color", &color)
              && color == 0x102030
              && !gm_nbt_blob_find_compound_int(
                  &dye, "display", "missing", &color)
              && !gm_nbt_blob_find_compound_int(
                  &dye, "missing", "color", &color),
          "nested compound int lookup preserves signed big-endian payload");
    CHECK(gm_nbt_blob_find_number(
              &source, "missing", &number, &number_type) == 0
              && gm_nbt_blob_find_number(
                  &(GmNbtBlob){(unsigned char *)all_types, sizeof all_types},
                  "i", &number, &number_type)
              && number == -2147483648.0 && number_type == 3
              && gm_nbt_blob_find_string(
                  &(GmNbtBlob){(unsigned char *)all_types, sizeof all_types},
                  "t", text, sizeof text)
              && !strcmp(text, "x")
              && gm_nbt_blob_find_numeric_list(
                  &(GmNbtBlob){(unsigned char *)all_types, sizeof all_types},
                  "q", list, 4, &list_count, &number_type)
              && list_count == 2 && number_type == 3
              && list[0] == 1.0 && list[1] == -1.0,
          "direct numeric, string, and homogeneous-list queries decode types");
    CHECK(gm_nbt_blob_set(&item_tag, entity_item_tag, sizeof entity_item_tag)
              && gm_nbt_blob_extract_compound(
                  &item_tag, "EntityTag", &entity_tag)
              && gm_nbt_blob_find_number(
                  &entity_tag, "Small", &number, &number_type)
              && number == 1.0 && number_type == 1
              && gm_nbt_blob_find_numeric_list(
                  &entity_tag, "Pos", list, 4, &list_count, &number_type)
              && list_count == 3 && number_type == 6
              && list[0] == 1.5 && list[1] == 2.0 && list[2] == -3.0
              && gm_nbt_blob_extract_compound(&entity_tag, "Pose", &pose)
              && gm_nbt_blob_find_numeric_list(
                  &pose, "Head", list, 4, &list_count, &number_type)
              && list_count == 3 && number_type == 5
              && list[0] == 10.0 && list[1] == 20.0 && list[2] == 30.0
              && gm_nbt_blob_extract_compound_list_element(
                  &entity_tag, "ArmorItems", 1, &boots)
              && gm_nbt_blob_find_string(
                  &boots, "id", text, sizeof text)
              && !strcmp(text, "minecraft:diamond_boots")
              && gm_nbt_blob_find_number(
                  &boots, "Damage", &number, &number_type)
              && number == 7.0,
          "compound and compound-list extraction preserves nested entity NBT");
    CHECK(gm_nbt_blob_make_empty(&edited)
              && gm_nbt_blob_set_byte(&edited, "Flight", 2)
              && gm_nbt_blob_set_int(&edited, "generation", 2)
              && gm_nbt_blob_set_string(&edited, "author", "Oracle")
              && gm_nbt_blob_find_number(
                  &edited, "Flight", &number, &number_type)
              && number == 2.0 && number_type == 1
              && gm_nbt_blob_find_string(
                  &edited, "author", text, sizeof text)
              && !strcmp(text, "Oracle")
              && gm_nbt_blob_make_empty(&display)
              && gm_nbt_blob_set_int(&display, "color", 0xabcdef)
              && gm_nbt_blob_set_compound(&edited, "display", &display)
              && gm_nbt_blob_find_compound_int(
                  &edited, "display", "color", &color)
              && color == 0xabcdef
              && gm_nbt_blob_append_compound_list(
                  &edited, "Patterns", &source)
              && gm_nbt_blob_append_compound_list(
                  &edited, "Patterns", &source)
              && gm_nbt_blob_extract_compound_list_element(
                  &edited, "Patterns", 1, &boots)
              && gm_nbt_blob_find_string(&boots, "Name", text, sizeof text)
              && !strcmp(text, "x")
              && gm_nbt_blob_set_string_list(
                  &edited, "CanDestroy", adventure_blocks, 2)
              && gm_nbt_blob_find_string_list_element(
                  &edited, "CanDestroy", 0, text, sizeof text)
              && !strcmp(text, "minecraft:stone")
              && gm_nbt_blob_find_string_list_element(
                  &edited, "CanDestroy", 1, text, sizeof text)
              && !strcmp(text, "minecraft:log")
              && !gm_nbt_blob_find_string_list_element(
                  &edited, "CanDestroy", 2, text, sizeof text),
          "cold compound editors replace scalars and append compound lists");
    gm_nbt_blob_clear(&boots);
    gm_nbt_blob_clear(&display);
    gm_nbt_blob_clear(&edited);
    gm_nbt_blob_clear(&pose);
    gm_nbt_blob_clear(&entity_tag);
    gm_nbt_blob_clear(&item_tag);
    gm_nbt_blob_clear(&dye);
    gm_nbt_blob_clear(&outer);
    gm_nbt_blob_clear(&copy);
    gm_nbt_blob_clear(&source);
    puts("nbt_blob: PASS");
    return 0;
}
