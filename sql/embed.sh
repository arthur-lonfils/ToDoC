#!/bin/sh
# Generates src/migrations.c from sql/migrations/*.sql
# Each .sql file becomes a { version, sql } entry in an array.

set -e

MIGRATIONS_DIR="sql/migrations"
OUTPUT="src/migrations.c"

cat > "$OUTPUT" << 'HEADER'
/* Auto-generated — do not edit. Run `make embed` to regenerate. */
#include "migrate.h"

HEADER

# Collect migration files in order
files=$(find "$MIGRATIONS_DIR" -name '*.sql' | sort)
count=0

for f in $files; do
    count=$((count + 1))
done

printf "const migration_t migrations[] = {\n" >> "$OUTPUT"

for f in $files; do
    # Extract version number from filename (e.g., 001_initial.sql -> 1)
    basename=$(basename "$f" .sql)
    version=$(echo "$basename" | sed 's/_.*//' | sed 's/^0*//')
    if [ -z "$version" ]; then
        version=0
    fi

    printf "    { %d, \"%s\",\n" "$version" "$basename" >> "$OUTPUT"
    printf "      " >> "$OUTPUT"

    # Embed the SQL as a C string literal, escaping quotes and newlines
    first=1
    while IFS= read -r line || [ -n "$line" ]; do
        if [ "$first" = 1 ]; then
            first=0
        else
            printf "\n      " >> "$OUTPUT"
        fi
        # Escape backslashes first, then double quotes
        escaped=$(printf '%s' "$line" | sed 's/\\/\\\\/g; s/"/\\"/g')
        printf '"%s\\n"' "$escaped" >> "$OUTPUT"
    done < "$f"

    printf "\n    },\n" >> "$OUTPUT"
done

printf "};\n\n" >> "$OUTPUT"
printf "const int migrations_count = %d;\n" "$count" >> "$OUTPUT"

echo "Embedded $count migration(s) into $OUTPUT"
