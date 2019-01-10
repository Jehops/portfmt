#!/usr/bin/awk -f
#
# Copyright (c) 2018, Tobias Kortkamp
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#
# THIS SOFTWARE IS PROVIDED BY ``AS IS'' AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
# NO EVENT SHALL I BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$
#
# MAINTAINER=	tobik@FreeBSD.org
#
# Format port Makefiles.  Best used via your editor and piping
# only parts of the Makefile to it.
#
# Usage: ${PORTSDIR}/Tools/scripts/portfmt.awk < Makefile
#

### Utility functions

function ceil(n,	i) {
	for (i = int(n); i < n; i++) { }
	return i;
}

function indent_level(s) {
	return ceil(length(s) / 8)
}

function repeat(s, n,	temp, i) {
	temp = ""
	for (i = 0; i < n; i++) {
		temp = sprintf("%s%s", s, temp)
	}
	return temp
}

function print_newline_array(start, arr, arrlen,	sep, end, i) {
	sep = sprintf("%s\t", start)
	end = " \\\n"
	for (i = 1; i < arrlen; i++) {
		if (i == arrlen - 1) {
			end = "\n"
		}
		printf "%s%s%s", sep, arr[i], end
		if (i == 1) {
			sep = repeat("\t", indent_level(start))
		}
	}
}

function print_token_array(start, tokens, tokenslen,	wrapcol, arr, arrlen, row, col, i) {
	wrapcol = 80 - length(start) - 8
	arrlen = 1
	row = ""
	col = 0
	for (i = 1; i < tokenslen; i++) {
		col = col + length(tokens[i]) + 1
		if (col >= wrapcol) {
			arr[arrlen++] = row
			row = ""
			col = 0
		}
		if (row == "") {
			row = tokens[i]
		} else {
			row = sprintf("%s %s", row, tokens[i])
		}
	}
	arr[arrlen++] = row
	print_newline_array(start, arr, arrlen)
}

# Case insensitive Bubble sort because it's super easy and we don't
# need anything better here ;-)
function sort_array(arr, arrlen,	i, j, temp) {
	for (i = 1; i < arrlen; i++) {
		for (j = i + 1; j < arrlen; j++) {
			if (arr[i] > arr[j]) {
				temp = arr[j]
				arr[j] = arr[i]
				arr[i] = temp
			}
		}
	}
}


### Script starts here

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
	maybe_in_target = 0
}

function print_tokens(	i) {
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

/^[A-Za-z0-9_]+[?+:]?=/ {
	print_tokens()
}

# If we were in a target previously, but there was an empty line,
# then we are actually still in the same target as before if the
# current line begins with a tab.
maybe_in_target && /^\t/ {
	in_target = 1
}

maybe_in_target {
	maybe_in_target = 0
}

/^#/ || /^\./ || /^[A-Z_]+!=/ || in_target {
	skip = 1
}

/^[ \t]*$/ { # empty line
	skip = 1
	in_target = 0
	maybe_in_target = 1
}

/^[a-zA-Z_-]+:/ {
	skip = 1
	in_target = 1
}

# Sanitize whitespace but do *not* sort tokens
/^BROKEN_[A-Za-z_0-9]+?[+?:]?=/ ||
/^IGNORE_[A-Za-z_0-9]+?[+?:]?=/ ||
/^BROKEN[+?:]?=/ ||
/^IGNORE[+?:]?=/ ||
/^DEPRECATED[+?:]?=/ ||
/^EXPIRATION_DATE[+?:]?=/ ||
/^EXTRACT_AFTER_ARGS[+?:]?=/ ||
/^EXTRACT_BEFORE_ARGS[+?:]?=/ ||
/^EXTRACT_CMD[+?:]?=/ ||
/^CATEGORIES[+?:]?=/ ||
/^DESKTOP_ENTRIES[+?:]?=/ ||
/^[A-Z0-9_]+_DESKTOP_ENTRIES[+?:]?=/ ||
/^COMMENT[+?:]?=/ ||
/^LICENSE_NAME[+?:]?=/ ||
/^[A-Z_]+_DESC[+?:]?=/ ||
/^FLAVORS[+?:]?=/ ||
/^LLD_UNSAFE[+?:]?=/ ||
/^CARGO_FEATURES[+?:]?=/ ||
/^MAKE_JOBS_UNSAFE[+?:]?=/ {
	sorted = 0
}

/^([A-Z_]+_)?MASTER_SITES[+?:]?=/ ||
/^[a-zA-Z0-9_]+_DEPENDS[+?:]?=/ ||
/^[a-zA-Z0-9_]+_C(XX|PP)?FLAGS[+?:]?=/ ||
/^C(XX|PP)?FLAGS[+?:]?=/ ||
/^CARGO_CRATES?[+?:]?=/ ||
/^DESKTOP_ENTRIES[+?:]?=/ ||
/^OPTIONS_EXCLUDE:=/ ||
/^[CM][A-Z_]+_ARGS(_OFF)?[+?:]?=/ ||
/^[A-Z0-9_]+_DESKTOP_ENTRIES[+?:]?=/ ||
/^[A-Z0-9_]+_ENV(_OFF)?[+?:]?=/ ||
/^[A-Z0-9_]+_VARS(_OFF)?[+?:]?=/ ||
/^[A-Z0-9_]+_CMAKE_OFF[+?:]?=/ ||
/^[A-Z0-9_]+_CMAKE_ON[+?:]?=/ ||
/^[A-Z0-9_]+_CONFIGURE_OFF[+?:]?=/ ||
/^[A-Z0-9_]+_CONFIGURE_ON[+?:]?=/ {
	print_as_tokens = 0
}

!skip {
	portfmt_no_skip()
} function portfmt_no_skip(	i, arrtemp, quoted, token) {
	if (match($0, /^[a-zA-Z0-9_]+[+?:]?=/)) {
		# Handle special lines like: VAR=xyz
		if (split($1, arrtemp, "=") > 1 && arrtemp[2] != "" && arrtemp[2] != "\\") {
			tokens[tokens_len++] = arrtemp[2]
		}
		varname = arrtemp[1]

		empty_lines_before_len = 1
		empty_lines_after_len = 1
		i = 2
	} else {
		i = 1
	}

	quoted = 0
	for (; i <= NF; i++) {
		if ($i == "\\") {
			break
		}

		if (quoted && tokens_len > 0) {
			token = tokens[tokens_len - 1]
			tokens[tokens_len - 1] = sprintf("%s %s", token, $i)
		} else {
			tokens[tokens_len++] = $i
		}
		if (match($i, /"/) && !match($i, /""/)) {
			quoted = !quoted
		}
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
