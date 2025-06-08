#!/bin/bash

# fix true/false variable names in MOO database files

if [ $# -eq 0 ]; then
    echo "usage: $0 <filename>"
    exit 1
fi

DB="$1"

if [ ! -f "$DB" ]; then
    echo "error: $DB not found."
    exit 1
fi

# see what needs changing
(size=$(du -h "$DB" | cut -f1) && lines=$(wc -l < "$DB") && printf "Analyzing %s (%s, %s lines) for true/false declarations... " "$DB" "$size" "$lines")
changes=$(sed -E -e 's/^(\s*)(true|false)(\s*=\s+)/\1\2_\3/gI' \
                 -e 's/(\s*=\s*)(true|false)(\s*=)/\1\2_\3/gI' \
                 -e 's/(\s*=\s*)(true|false)(\s*=)/\1\2_\3/gI' "$DB" | \
          diff -u "$DB" - 2>/dev/null | \
          grep '^[+-]' | grep -v '^[+-][+-][+-]' || true)
echo "done!"

if [ -n "$changes" ]; then
    count=$(echo "$changes" | grep '^+' | wc -l)
    echo "Found $count lines with true/false variables being set:"
    echo "$changes" | sed 's/^-/  old: /;s/^+/  new: /'
    echo
    
    read -p "Apply these changes now? (y/n): " answer
    
    if [[ "$answer" =~ ^[yY]$ ]]; then
        cp "$DB" "$DB.bak"
        sed -i -E -e 's/^(\s*)(true|false)(\s*=\s+)/\1\2_\3/gI' \
                  -e 's/(\s*=\s*)(true|false)(\s*=)/\1\2_\3/gI' \
                  -e 's/(\s*=\s*)(true|false)(\s*=)/\1\2_\3/gI' "$DB"
        echo "Done. Backup saved as $DB.bak."
    else
        echo "Cancelled."
    fi
else
    echo "Nothing found to fix."
fi