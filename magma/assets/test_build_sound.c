#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int asset_build_sound(const char *index_path, const char *out_path);

static char *read_all(const char *path)
{
    FILE *fp = fopen(path, "rb");
    long n;
    char *buf;
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    n = ftell(fp);
    if (n < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, fp) != (size_t)n) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    buf[n] = 0;
    return buf;
}

static char *strip_comments(const char *s)
{
    size_t n = strlen(s);
    char *o = (char *)malloc(n + 1);
    size_t i = 0, j = 0;
    if (!o)
        return NULL;
    while (s[i]) {
        if (s[i] == '/' && s[i + 1] == '*') {
            i += 2;
            while (s[i] && !(s[i] == '*' && s[i + 1] == '/'))
                i++;
            if (s[i])
                i += 2;
            continue;
        }
        o[j++] = s[i++];
    }
    o[j] = 0;
    return o;
}

static void squash(char *s)
{
    char *d = s;
    char *p = s;
    int sp = 0;
    while (*p) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            if (!sp) {
                *d++ = ' ';
                sp = 1;
            }
        } else {
            *d++ = *p;
            sp = 0;
        }
        p++;
    }
    *d = 0;
}

int main(int argc, char **argv)
{
    const char *index_path;
    const char *out_path;
    const char *golden;
    char *got, *want, *sg, *sw;
    if (argc != 4) {
        fprintf(stderr, "usage: %s <index.json> <out.h> <golden.h>\n", argv[0]);
        return 2;
    }
    index_path = argv[1];
    out_path = argv[2];
    golden = argv[3];
    if (asset_build_sound(index_path, out_path) != 0) {
        fprintf(stderr, "FAIL: asset_build_sound\n");
        return 1;
    }
    got = read_all(out_path);
    want = read_all(golden);
    if (!got || !want) {
        fprintf(stderr, "FAIL: read headers\n");
        free(got);
        free(want);
        return 1;
    }
    sg = strip_comments(got);
    sw = strip_comments(want);
    free(got);
    free(want);
    if (!sg || !sw) {
        free(sg);
        free(sw);
        return 1;
    }
    squash(sg);
    squash(sw);
    if (strcmp(sg, sw) != 0) {
        fprintf(stderr, "FAIL: header data mismatch\n");
        fprintf(stderr, " got len %zu want len %zu\n", strlen(sg), strlen(sw));
        free(sg);
        free(sw);
        return 1;
    }
    free(sg);
    free(sw);
    printf("test_build_sound: PASS\n");
    return 0;
}
