echo DISTVERSION=1.2.1 | ${PORTEDIT} merge 2.in | \
	diff -L 2.expected -L 2.actual -u 2.expected -
