${PORTEDIT} bump-revision bump-revision_2.in | \
	diff -L bump-revision_2.expected -L bump-revision_2.actual \
		-u bump-revision_2.expected -
