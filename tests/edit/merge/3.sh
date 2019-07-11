printf "PORTNAME=tokei\nUSES=gmake\nCARGO_CRATES=\nCARGO_USE_GITHUB=\n" | ${PORTEDIT} merge 3.in | \
	diff -L 3.expected -L 3.actual -u 3.expected -
