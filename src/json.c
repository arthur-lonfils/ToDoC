#include "json.h"

#include <stdio.h>

void json_str(FILE *fp, const char *s)
{
    if (!s) {
        fputs("null", fp);
        return;
    }
    fputc('"', fp);
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
        case '"':
            fputs("\\\"", fp);
            break;
        case '\\':
            fputs("\\\\", fp);
            break;
        case '\n':
            fputs("\\n", fp);
            break;
        case '\r':
            fputs("\\r", fp);
            break;
        case '\t':
            fputs("\\t", fp);
            break;
        case '\b':
            fputs("\\b", fp);
            break;
        case '\f':
            fputs("\\f", fp);
            break;
        default:
            if (c < 0x20) {
                fprintf(fp, "\\u%04x", c);
            } else {
                fputc((int)c, fp);
            }
            break;
        }
    }
    fputc('"', fp);
}

void json_int(FILE *fp, int v)
{
    fprintf(fp, "%d", v);
}

void json_int64(FILE *fp, long long v)
{
    fprintf(fp, "%lld", v);
}

void json_bool(FILE *fp, int v)
{
    fputs(v ? "true" : "false", fp);
}

void json_key(FILE *fp, const char *k)
{
    json_str(fp, k);
    fputc(':', fp);
}

void json_comma(FILE *fp, int *first)
{
    if (*first) {
        *first = 0;
    } else {
        fputc(',', fp);
    }
}
