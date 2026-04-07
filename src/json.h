#ifndef TODOC_JSON_H
#define TODOC_JSON_H

#include <stdio.h>

/* Tiny set of JSON output primitives. All write directly to the
 * given FILE*. There is intentionally no AST, no allocation, no
 * pretty-printing — callers compose objects and arrays by
 * interleaving these calls. */

/* Print a JSON string value (with surrounding quotes and escapes).
 * NULL becomes the literal `null`. */
void json_str(FILE *fp, const char *s);

/* Print a JSON number. */
void json_int(FILE *fp, int v);
void json_int64(FILE *fp, long long v);
void json_bool(FILE *fp, int v);

/* Print a key followed by a colon, e.g. `"name":` */
void json_key(FILE *fp, const char *k);

/* On the first call sets *first to 0 and writes nothing.
 * On every subsequent call writes a comma. Useful for arrays/objects:
 *
 *     int first = 1;
 *     for (int i = 0; i < n; i++) {
 *         json_comma(fp, &first);
 *         json_str(fp, names[i]);
 *     }
 */
void json_comma(FILE *fp, int *first);

#endif
