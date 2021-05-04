${PORTEDIT} merge -e 'RUN_DEPENDS+=foo' 22.in | diff -L 22.expected -L 22.actual -u 22.expected -
