${PORTEDIT} bump-revision bump-revision_1.in | \
	diff -L bump-revision_1.expected -L bump-revision_1.actual \
		-u bump-revision_1.expected -
