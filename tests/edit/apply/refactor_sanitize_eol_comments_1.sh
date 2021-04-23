${PORTEDIT} apply refactor.sanitize-eol-comments refactor_sanitize_eol_comments_1.in | \
	diff -L refactor_sanitize_eol_comments_1.expected -L refactor_sanitize_eol_comments_1.actual \
		-u refactor_sanitize_eol_comments_1.expected -
