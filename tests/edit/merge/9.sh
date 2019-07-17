echo "MASTER_SITES=" | ${PORTEDIT} merge 9.in | diff -L 9.expected -L 9.actual -u 9.expected -
