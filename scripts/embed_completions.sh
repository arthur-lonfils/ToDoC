#!/bin/sh
# Generates src/completions_data.c from scripts/completions/*.{bash,zsh,fish}.
# Each file becomes a { shell, body } entry in an array. Mirrors the
# pattern used by sql/embed.sh and scripts/embed_changelog.sh.

set -e

DIR="scripts/completions"
OUTPUT="src/completions_data.c"

if [ ! -d "$DIR" ]; then
    echo "embed_completions.sh: $DIR not found" >&2
    exit 1
fi

cat > "$OUTPUT" << 'HEADER'
/* Auto-generated — do not edit. Run `make embed` to regenerate. */
#include "completions.h"

HEADER

files=$(find "$DIR" -maxdepth 1 -type f \( -name '*.bash' -o -name '*.zsh' -o -name '*.fish' \) | sort)
count=0
for f in $files; do
    count=$((count + 1))
done

printf "const completion_script_t completion_scripts[] = {\n" >> "$OUTPUT"

for f in $files; do
    name=$(basename "$f")
    # bash → "bash", zsh → "zsh", fish → "fish" (strip the leading "todoc.")
    shell="${name#todoc.}"

    printf "    { \"%s\",\n" "$shell" >> "$OUTPUT"
    printf "      " >> "$OUTPUT"

    first=1
    while IFS= read -r line || [ -n "$line" ]; do
        if [ "$first" = 1 ]; then
            first=0
        else
            printf "\n      " >> "$OUTPUT"
        fi
        # Escape backslashes first, then double quotes — same scheme as
        # sql/embed.sh and scripts/embed_changelog.sh.
        escaped=$(printf '%s' "$line" | sed 's/\\/\\\\/g; s/"/\\"/g')
        printf '"%s\\n"' "$escaped" >> "$OUTPUT"
    done < "$f"

    printf "\n    },\n" >> "$OUTPUT"
done

printf "};\n\n" >> "$OUTPUT"
printf "const int completion_scripts_count = %d;\n" "$count" >> "$OUTPUT"

echo "Embedded $count completion script(s) into $OUTPUT"
