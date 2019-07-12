printf "PORTNAME+=a b" | ${PORTEDIT} merge 5.in | \
	diff -L 5.expected -L 5.actual -u 5.expected -
