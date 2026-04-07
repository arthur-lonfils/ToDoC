#!/bin/sh
# Generates src/changelog_data.c from CHANGELOG.md.
# The full file becomes a single C string literal so the binary can
# print release notes via 'todoc changelog' without any network or
# filesystem dependency.

set -e

INPUT="CHANGELOG.md"
OUTPUT="src/changelog_data.c"

if [ ! -f "$INPUT" ]; then
    echo "embed_changelog.sh: $INPUT not found" >&2
    exit 1
fi

cat > "$OUTPUT" << 'HEADER'
/* Auto-generated — do not edit. Run `make embed` to regenerate. */
#include "changelog.h"

#include <stddef.h>

const char *changelog_data =
HEADER

# Embed the file as a multi-line C string literal. Escape backslashes
# first, then double quotes, then end each line with \n. Empty lines
# become "\n".
while IFS= read -r line || [ -n "$line" ]; do
    escaped=$(printf '%s' "$line" | sed 's/\\/\\\\/g; s/"/\\"/g')
    printf '    "%s\\n"\n' "$escaped" >> "$OUTPUT"
done < "$INPUT"

printf "    ;\n\n" >> "$OUTPUT"

# Compute byte length (not character length — fine for ASCII/UTF-8 prefix
# search). Use wc -c on the original file.
bytes=$(wc -c < "$INPUT" | tr -d ' ')
printf "const size_t changelog_data_len = %s;\n" "$bytes" >> "$OUTPUT"

echo "Embedded $INPUT ($bytes bytes) into $OUTPUT"
