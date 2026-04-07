#include "changelog.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Embedded data index ─────────────────────────────────────── */

/* One parsed version section. All pointers are into changelog_data
 * (no allocation). The section's body runs from `body_start` for
 * `body_len` bytes and includes the heading line itself. */
typedef struct {
    char name[32]; /* "0.5.0" — bare semver, no leading 'v' */
    int major, minor, patch;
    const char *body_start;
    size_t body_len;
} version_t;

/* Strip a leading 'v' or 'V' if present. */
static const char *strip_v(const char *s)
{
    return (s && (s[0] == 'v' || s[0] == 'V')) ? s + 1 : s;
}

/* Parse "X.Y.Z" into three ints. Returns 0 on success. */
static int parse_semver(const char *s, int *maj, int *min, int *pat)
{
    if (!s) {
        return -1;
    }
    s = strip_v(s);
    return sscanf(s, "%d.%d.%d", maj, min, pat) == 3 ? 0 : -1;
}

/* Compare two version_t newest-first (so qsort orders highest first). */
static int version_cmp_desc(const void *a, const void *b)
{
    const version_t *va = a;
    const version_t *vb = b;
    if (va->major != vb->major) {
        return vb->major - va->major;
    }
    if (va->minor != vb->minor) {
        return vb->minor - va->minor;
    }
    return vb->patch - va->patch;
}

/* Walk changelog_data and build an array of version_t. Caller frees
 * the returned pointer. *out_count is the number of entries. Returns
 * NULL on parse failure. */
static version_t *index_changelog(int *out_count)
{
    *out_count = 0;
    if (!changelog_data || changelog_data_len == 0) {
        return NULL;
    }

    /* First pass: count headings to size the array. */
    int n = 0;
    for (const char *p = changelog_data; *p; p++) {
        if ((p == changelog_data || p[-1] == '\n') && p[0] == '#' && p[1] == '#' && p[2] == ' ' &&
            p[3] == '[') {
            n++;
        }
    }
    if (n == 0) {
        return NULL;
    }

    version_t *versions = todoc_calloc((size_t)n, sizeof(version_t));
    int idx = 0;

    /* Second pass: capture each section's start, name, and length. */
    for (const char *p = changelog_data; *p; p++) {
        if ((p == changelog_data || p[-1] == '\n') && p[0] == '#' && p[1] == '#' && p[2] == ' ' &&
            p[3] == '[') {
            /* p points to the start of '## [...]' line */
            const char *name_start = p + 4;
            const char *name_end = strchr(name_start, ']');
            if (!name_end || (size_t)(name_end - name_start) >= sizeof(versions[idx].name)) {
                continue;
            }
            size_t name_len = (size_t)(name_end - name_start);
            memcpy(versions[idx].name, name_start, name_len);
            versions[idx].name[name_len] = '\0';

            if (parse_semver(versions[idx].name, &versions[idx].major, &versions[idx].minor,
                             &versions[idx].patch) != 0) {
                /* Skip non-semver headings (e.g. "Unreleased"). */
                continue;
            }

            versions[idx].body_start = p;
            /* body_len patched below once we know the next start */
            idx++;
        }
    }

    /* Walk again to set body_len: each section runs until the start of
     * the next one, or until end of data for the last one. */
    for (int i = 0; i < idx; i++) {
        const char *next_start =
            (i + 1 < idx) ? versions[i + 1].body_start : changelog_data + changelog_data_len;
        versions[i].body_len = (size_t)(next_start - versions[i].body_start);
    }

    *out_count = idx;
    return versions;
}

/* ── Public API ──────────────────────────────────────────────── */

void changelog_print_latest(void)
{
    int n = 0;
    version_t *vs = index_changelog(&n);
    if (!vs || n == 0) {
        fprintf(stderr, "todoc: changelog is empty.\n");
        free(vs);
        return;
    }
    /* The CHANGELOG.md is already newest-first (git-cliff default), so
     * the first parsed entry is the latest. Sort defensively anyway. */
    qsort(vs, (size_t)n, sizeof(*vs), version_cmp_desc);
    fwrite(vs[0].body_start, 1, vs[0].body_len, stdout);
    free(vs);
}

int changelog_print_version(const char *version)
{
    if (!version) {
        return 1;
    }
    const char *needle = strip_v(version);

    int n = 0;
    version_t *vs = index_changelog(&n);
    if (!vs || n == 0) {
        free(vs);
        return 1;
    }

    int hit = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(vs[i].name, needle) == 0) {
            fwrite(vs[i].body_start, 1, vs[i].body_len, stdout);
            hit = 1;
            break;
        }
    }
    free(vs);
    return hit ? 0 : 1;
}

int changelog_print_since(const char *since)
{
    int s_maj = 0, s_min = 0, s_pat = 0;
    if (parse_semver(since, &s_maj, &s_min, &s_pat) != 0) {
        fprintf(stderr, "todoc: invalid version '%s' (expected X.Y.Z)\n", since ? since : "");
        return 1;
    }

    int n = 0;
    version_t *vs = index_changelog(&n);
    if (!vs || n == 0) {
        free(vs);
        return 1;
    }
    qsort(vs, (size_t)n, sizeof(*vs), version_cmp_desc);

    int printed = 0;
    for (int i = 0; i < n; i++) {
        /* "newer than since" — strict greater-than */
        int cmp;
        if (vs[i].major != s_maj) {
            cmp = vs[i].major - s_maj;
        } else if (vs[i].minor != s_min) {
            cmp = vs[i].minor - s_min;
        } else {
            cmp = vs[i].patch - s_pat;
        }
        if (cmp > 0) {
            fwrite(vs[i].body_start, 1, vs[i].body_len, stdout);
            printed++;
        }
    }

    free(vs);
    return printed > 0 ? 0 : 1;
}

void changelog_print_all(void)
{
    if (!changelog_data || changelog_data_len == 0) {
        fprintf(stderr, "todoc: changelog is empty.\n");
        return;
    }
    fwrite(changelog_data, 1, changelog_data_len, stdout);
}

void changelog_list_versions(void)
{
    int n = 0;
    version_t *vs = index_changelog(&n);
    if (!vs || n == 0) {
        fprintf(stderr, "todoc: changelog is empty.\n");
        free(vs);
        return;
    }
    qsort(vs, (size_t)n, sizeof(*vs), version_cmp_desc);

    for (int i = 0; i < n; i++) {
        /* Try to extract the date from the heading line, which looks
         * like '## [X.Y.Z] — YYYY-MM-DD\n'. We just print the heading
         * line as-is minus the leading '## ' and the trailing newline. */
        const char *line_start = vs[i].body_start;
        const char *eol = memchr(line_start, '\n', vs[i].body_len);
        if (!eol) {
            eol = line_start + vs[i].body_len;
        }
        /* Skip the leading "## " (3 chars). */
        const char *content = line_start + 3;
        size_t content_len = (size_t)(eol - content);
        printf("%.*s\n", (int)content_len, content);
    }
    free(vs);
}
