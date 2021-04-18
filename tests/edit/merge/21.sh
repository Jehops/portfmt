${PORTEDIT} merge -e 'PATCHFILES+=foo # asf' -e 'COMMENT+=foo' 21.in | diff -L 21.expected -L 21.actual -u 21.expected -
