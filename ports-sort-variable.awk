BEGIN {
	reset()
}

function reset() {
	varname = "<unknown>"
	tokens_len = 1
	print_as_tokens = 1
	empty_lines_before_len = 1
	empty_lines_after_len = 1
	sorted = 1
	in_target = 0
}

function print_tokens() {
	for (i = 1; i < empty_lines_before_len; i++) {
		print empty_lines_before[i]
	}

	if (tokens_len <= 1) {
		return;
	}

	if (sorted) {
		sort_array(tokens, tokens_len)
	}
	if (print_as_tokens == 1) {
		print_token_array(sprintf("%s=", varname), tokens, tokens_len)
	} else {
		print_newline_array(sprintf("%s=", varname), tokens, tokens_len)
	}

	for (i = 1; i < empty_lines_after_len; i++) {
		print empty_lines_after[i]
	}

	reset()
}

/^[A-Za-z_]+[?+]?=/ {
	print_tokens()
}

/^#/ || /^\./ || in_target {
	skip = 1
}

/^[ \t]*$/ { # empty line
	skip = 1
	in_target = 0
}

/^[a-zA-Z-]+:/ {
	skip = 1
	in_target = 1
}

# Sanitize whitespace but do *not* sort tokens
/^COMMENT[+?]?=/ ||
/^[A-Z_]+_DESC[+?]?=/ ||
/^FLAVORS[+?]?=/ ||
/^LLD_UNSAFE[+?]?=/ ||
/^MAKE_JOBS_UNSAFE[+?]?=/ {
	sorted = 0
}

/^[a-zA-Z_]+_DEPENDS[+?]=/ ||
/^[A-Z_]+_ARGS[+?]?=/ ||
/^[A-Z_]+_CMAKE_OFF[+?]?=/ ||
/^[A-Z_]+_CMAKE_ON[+?]?=/ ||
/^[A-Z_]+_CONFIGURE_OFF[+?]?=/ ||
/^[A-Z_]+_CONFIGURE_ON[+?]?=/ {
	print_as_tokens = 0
}

!skip {
	if (match($0, /^[A-Z_+?]+=/)) {
		varname = substr($0, RSTART, RLENGTH - 1)
		empty_lines_before_len = 1
		empty_lines_after_len = 1
		i = 2
	} else {
		i = 1
	}
	for (; i <= NF; i++) {
		if ($i == "\\") {
			break
		}
		tokens[tokens_len++] = $i
	}
}

skip {
	if (tokens_len == 1) {
		empty_lines_before[empty_lines_before_len++] = $0;
	} else {
		empty_lines_after[empty_lines_after_len++] = $0;
	}
	skip = 0
}

END {
	print_tokens()
}
