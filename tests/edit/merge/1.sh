echo DISTVERSION=1.2.1 | ${PORTEDIT} merge 1.in | \
	diff -L 1.expected -L 1.actual -u 1.expected -
