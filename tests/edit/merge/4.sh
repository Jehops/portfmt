printf "CARGO_CRATES=" | ${PORTEDIT} merge 4.in | \
	diff -L 4.expected -L 4.actual -u 4.expected -
