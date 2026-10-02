/* Build the engine's checked language table from the user's client jar. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX 512
struct row { char *key, *value; };
static struct row rows[MAX];
static int count;
static void die(const char *s) { fprintf(stderr, "langgen: %s\n", s); exit(1); }
static char *copy(const char *s) { char *p = malloc(strlen(s) + 1); if (!p) die("out of memory"); return strcpy(p, s); }
static void emit(FILE *out, const char *s)
{
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        if (*p == '"' || *p == '\\') { fputc('\\', out); fputc(*p, out); }
        else if (*p < 32 || *p >= 127) fprintf(out, "\\%03o", *p);
        else fputc(*p, out);
    }
    fputc('"', out);
}
int main(int argc, char **argv)
{
    if (argc != 4) die("usage: langgen keys.tsv en_US.lang output.h");
    FILE *f = fopen(argv[1], "rb"); if (!f) die("cannot open key list");
    char line[8192];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#') continue;
        if (count == MAX) die("too many keys");
        if (strchr(line, '=') || strchr(line, ' ') || strchr(line, '\t')) die("invalid key list");
        for (int i = 0; i < count; ++i) if (!strcmp(rows[i].key, line)) die("duplicate key");
        rows[count++].key = copy(line);
    }
    fclose(f);
    f = fopen(argv[2], "rb"); if (!f) die("cannot open language file");
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#') continue;
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq++ = 0;
        for (int i = 0; i < count; ++i) if (!strcmp(rows[i].key, line)) {
            /* StringTranslate's numeric normalization; achievement hint uses E. */
            if (!strcmp(line, "achievement.openInventory.desc")) {
                char *slot = strstr(eq, "%1$s");
                if (!slot) die("inventory hint placeholder changed");
                *slot = 'E'; memmove(slot + 1, slot + 4, strlen(slot + 4) + 1);
            } else if (!strcmp(line, "commands.generic.num.tooSmall") ||
                       !strcmp(line, "commands.generic.num.tooBig") ||
                       !strcmp(line, "commands.help.header")) {
                for (char *p = eq; (p = strstr(p, "%d")) != NULL; p += 2) p[1] = 's';
            }
            free(rows[i].value); rows[i].value = copy(eq); break;
        }
    }
    fclose(f);
    for (int i = 0; i < count; ++i) if (!rows[i].value) { fprintf(stderr, "langgen: missing key %s\n", rows[i].key); return 1; }
    f = fopen(argv[3], "wb"); if (!f) die("cannot write output");
    fputs("/* Generated from the user's Minecraft 1.7.10 en_US.lang. Do not track. */\n", f);
    fputs("static const struct lang_row LANG_ROWS[] = {\n", f);
    for (int i = 0; i < count; ++i) { fputs("    {", f); emit(f, rows[i].key); fputs(", ", f); emit(f, rows[i].value); fputs("},\n", f); }
    fputs("};\n", f);
    if (fclose(f)) die("write failed");
    return 0;
}
