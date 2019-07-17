echo "PLIST_FILES=bin/cloak" | ${PORTEDIT} merge 6.in | diff -L 6.expected -L 6.actual -u 6.expected -
