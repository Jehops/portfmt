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

/^[A-Za-z0-9_]+[?+]?=/ {
	print_tokens()
}

/^#/ || /^\./ || /^[A-Z_]+!=/ || in_target {
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
/^CATEGORIES[+?]?=/ ||
/^COMMENT[+?]?=/ ||
/^[A-Z_]+_DESC[+?]?=/ ||
/^FLAVORS[+?]?=/ ||
/^LLD_UNSAFE[+?]?=/ ||
/^CARGO_FEATURES[+?]?=/ ||
/^MAKE_JOBS_UNSAFE[+?]?=/ {
	sorted = 0
}

/^([A-Z_]+_)?MASTER_SITES[+?]?=/ ||
/^[a-zA-Z_]+_DEPENDS[+?]?=/ ||
/^CARGO_CRATES?[+?]?=/ ||
/^[A-Z_]+_ARGS(_OFF)?[+?]?=/ ||
/^[A-Z_]+_ENV(_OFF)?[+?]?=/ ||
/^[A-Z_]+_VARS(_OFF)?[+?]?=/ ||
/^[A-Z_]+_CMAKE_OFF[+?]?=/ ||
/^[A-Z_]+_CMAKE_ON[+?]?=/ ||
/^[A-Z_]+_CONFIGURE_OFF[+?]?=/ ||
/^[A-Z_]+_CONFIGURE_ON[+?]?=/ {
	print_as_tokens = 0
}

!skip {
	if (match($0, /^[a-zA-Z0-9_+?]+=/)) {
		# Handle lines like: VAR=xyz
		if (split($1, arrtemp, "=") > 1 && arrtemp[2] != "") {
			tokens[tokens_len++] = arrtemp[2]
		}
		varname = arrtemp[1]

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
