echo "PLIST_FILES=" | ${PORTEDIT} merge 8.in | diff -L 8.expected -L 8.actual -u 8.expected -
