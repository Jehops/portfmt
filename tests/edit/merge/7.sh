echo "PLIST_FILES=bin/cloak" | ${PORTEDIT} merge 7.in | diff -L 7.expected -L 7.actual -u 7.expected -
