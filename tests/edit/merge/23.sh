${PORTEDIT} merge -e 'IGNORE=foo' 23.in | diff -L 23.expected -L 23.actual -u 23.expected -
