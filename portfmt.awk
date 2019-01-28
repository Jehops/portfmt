#!/usr/bin/awk -f
#
# Copyright (c) 2019, Tobias Kortkamp <tobik@FreeBSD.org>
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
# Editor setup for Kakoune in ~/.config/kak/kakrc mapped to ,1
# map global user 1 '|/usr/ports/Tools/scripts/portfmt.awk<ret>;' -docstring "portfmt on selection"
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

function print_newline_array(var, start, arr, arrlen,	end, i, level, sep, line) {
	# Handle variables with empty values
	if (arrlen == 2 && arr[1] == "<<<empty-value>>>") {
		printf "%s\n", start
		return
	}
	sep = sprintf("%s\t", start)
	end = " \\\n"
	for (i = 1; i < arrlen; i++) {
		line = arr[i]
		if (line == "<<<empty-value>>>") {
			continue
		}
		if (i == arrlen - 1) {
			end = "\n"
		}
		printf "%s%s%s", sep, line, end
		if (i == 1) {
			level = indent_level(start)
			level += indent_multi(var)
			sep = repeat("\t", level)
		}
	}
}

function print_token_array(var, start, tokens, tokenslen,	wrapcol, arr, arrlen, row, col, i, token) {
	if (ignore_wrap_col(var)) {
		wrapcol = 10000
	} else {
		wrapcol = WRAPCOL - length(start) - 8
	}
	arrlen = 1
	row = ""
	col = 0
	for (i = 1; i < tokenslen; i++) {
		token = tokens[i]
		if (token == "<<<empty-value>>>") {
			continue
		}
		col = col + length(token) + 1
		if (i != 1 && col >= wrapcol) {
			arr[arrlen++] = row
			row = ""
			col = 0
		}
		if (row == "") {
			row = token
		} else {
			row = sprintf("%s %s", row, token)
		}
	}
	arr[arrlen++] = row
	print_newline_array(var, start, arr, arrlen)
}

function greater_helper(rel, a, b,	ai, bi) {
	ai = rel[a]
	bi = rel[b]
	if (ai == 0 && bi == 0) {
		return (tolower(a) > tolower(b))
	} else if (ai == 0) {
		return 0 # b > a
	} else if (bi == 0) {
		return 1 # a > b
	} else {
		return ai > bi
	}
}

function greater(a, b) {
	# Hack to treat something like ${PYTHON_PKGNAMEPREFIX} or
	# ${RUST_DEFAULT} as if they were PYTHON_PKGNAMEPREFIX or
	# RUST_DEFAULT for the sake of approximately sorting them
	# correctly in *_DEPENDS.
	gsub(/[\$\{\}]/, "", a)
	gsub(/[\$\{\}]/, "", b)

	if (order == "license-perms") {
		return greater_helper(license_perms_rel, a, b)
	} else if (order == "use-qt") {
		return greater_helper(use_qt_rel, a, b)
	} else if (order == "plist-files") {
		# Ignore plist keywords
		gsub(/^"@[a-z\-]+ /, "", a)
		gsub(/^"@[a-z\-]+ /, "\"", b)
	}

	# one-true-awk does this case-insensitive but gawk doesn't,
	# lowercase everything first.  This also coerces everything
	# to a string, so that we compare strings and not integers.
	return (tolower(a) > tolower(b))
}

# Case insensitive Bubble sort because it's super easy and we don't
# need anything better here ;-)
function sort_array(arr, arrlen,	i, j, temp) {
	for (i = 1; i < arrlen; i++) {
		for (j = i + 1; j < arrlen; j++) {
			if (greater(arr[i], arr[j])) {
				temp = arr[j]
				arr[j] = arr[i]
				arr[i] = temp
			}
		}
	}
}

function indent_multi(varname,	var) {
	var = strip_modifier(varname)
	return indent_multi_[var]
}

function leave_unsorted(varname,	var) {
	var = strip_modifier(varname)
	if (leave_unsorted_[var] ||
            var ~ /^LICENSE_NAME_[A-Z0-9._\-+ ]$/ ||
            var ~ /^LICENSE_TEXT_[A-Z0-9._\-+ ]$/ ||
	    var ~ /^[A-Z0-9_]+_MASTER_SITES$/ ||
	    var ~ /^[A-Z0-9_]+_DESC$/) {
		return 1
	}
	return 0
}

function ignore_wrap_col(varname,	var) {
	var = strip_modifier(varname)
	if (ignore_wrap_col_[var]) {
		return 1
	}
	return 0
}

function print_as_newlines(varname,	var) {
	var = strip_modifier(varname)
	if (print_as_newlines_[var] ||
	    var ~ /^([A-Z_]+_)?MASTER_SITES$/ ||
	    var ~ /^[a-zA-Z0-9_]+_DEPENDS$/ ||
	    var ~ /^[a-zA-Z0-9_]+_C(XX|PP)?FLAGS$/ ||
	    var ~ /^[CM][A-Z_]+_ARGS(_OFF)?$/ ||
	    var ~ /^[A-Z0-9_]+_DESKTOP_ENTRIES$/ ||
	    var ~ /^[A-Z0-9_]+_GH_TUPLE$/ ||
	    var ~ /^[A-Z0-9_]+_PLIST_FILES$/ ||
	    var ~ /^[A-Z0-9_]+_ENV(_OFF)?$/ ||
	    var ~ /^[A-Z0-9_]+_VARS(_OFF)?$/ ||
	    var ~ /^[A-Z0-9_]+_CMAKE_OFF$/ ||
	    var ~ /^[A-Z0-9_]+_CMAKE_ON$/ ||
	    var ~ /^[A-Z0-9_]+_CONFIGURE_OFF$/ ||
	    var ~ /^[A-Z0-9_]+_CONFIGURE_ON$/) {
		return 1
	}
	return 0
}

### Script starts here

BEGIN {
	WRAPCOL = ENVIRON["WRAPCOL"]
	if (!WRAPCOL) {
		WRAPCOL = 80
	}
	setup_relations()
	reset()
}

function setup_relations(	i, archs) {
	i = 0
	license_perms_rel["dist-mirror"] = i++
	license_perms_rel["no-dist-mirror"] = i++
	license_perms_rel["dist-sell"] = i++
	license_perms_rel["no-dist-sell"] = i++
	license_perms_rel["pkg-mirror"] = i++
	license_perms_rel["no-pkg-mirror"] = i++
	license_perms_rel["pkg-sell"] = i++
	license_perms_rel["no-pkg-sell"] = i++
	license_perms_rel["auto-accept"] = i++
	license_perms_rel["no-auto-accept"] = i++

	i = 0
	use_qt_rel["3d"] = i++
	use_qt_rel["assistant"] = i++
	use_qt_rel["buildtools"] = i++
	use_qt_rel["canvas3d"] = i++
	use_qt_rel["charts"] = i++
	use_qt_rel["concurrent"] = i++
	use_qt_rel["connectivity"] = i++
	use_qt_rel["core"] = i++
	use_qt_rel["datavis3d"] = i++
	use_qt_rel["dbus"] = i++
	use_qt_rel["declarative"] = i++
	use_qt_rel["designer"] = 4 
	use_qt_rel["diag"] = i++
	use_qt_rel["doc"] = i++
	use_qt_rel["examples"] = i++
	use_qt_rel["gamepad"] = i++
	use_qt_rel["graphicaleffects"] = i++
	use_qt_rel["gui"] = i++
	use_qt_rel["help"] = i++
	use_qt_rel["imageformats"] = i++
	use_qt_rel["l1x++n"] = i++
	use_qt_rel["linguist"] = i++
	use_qt_rel["linguisttools"] = i++
	use_qt_rel["location"] = i++
	use_qt_rel["multimedia"] = i++
	use_qt_rel["network"] = i++
	use_qt_rel["networkauth"] = i++
	use_qt_rel["opengl"] = i++
	use_qt_rel["paths"] = i++
	use_qt_rel["phonon4"] = i++
	use_qt_rel["pixeltool"] = i++
	use_qt_rel["plugininfo"] = i++
	use_qt_rel["printsupport"] = i++
	use_qt_rel["qdbus"] = i++
	use_qt_rel["qdbusviewer"] = i++
	use_qt_rel["qdoc-data"] = i++
	use_qt_rel["qdoc"] = i++
	use_qt_rel["qev"] = i++
	use_qt_rel["qmake"] = i++
	use_qt_rel["quickcontrols"] = i++
	use_qt_rel["quickcontrols2"] = i++
	use_qt_rel["remoteobjects"] = i++
	use_qt_rel["script"] = i++
	use_qt_rel["scripttools"] = i++
	use_qt_rel["scxml"] = i++
	use_qt_rel["sensors"] = i++
	use_qt_rel["serialbus"] = i++
	use_qt_rel["serialport"] = i++
	use_qt_rel["speech"] = i++
	use_qt_rel["sql-ibase"] = i++
	use_qt_rel["sql-mysql"] = i++
	use_qt_rel["sql-odbc"] = i++
	use_qt_rel["sql-pgsql"] = i++
	use_qt_rel["sql-sqlite2"] = i++
	use_qt_rel["sql-sqlite3"] = i++
	use_qt_rel["sql-tds"] = i++
	use_qt_rel["sql"] = i++
	use_qt_rel["svg"] = i++
	use_qt_rel["testlib"] = i++
	use_qt_rel["uiplugin"] = i++
	use_qt_rel["uitools"] = i++
	use_qt_rel["virtualkeyboard"] = i++
	use_qt_rel["wayland"] = i++
	use_qt_rel["webchannel"] = i++
	use_qt_rel["webengine"] = i++
	use_qt_rel["webkit"] = i++
	use_qt_rel["websockets-qml"] = i++
	use_qt_rel["websockets"] = i++
	use_qt_rel["webview"] = i++
	use_qt_rel["widgets"] = i++
	use_qt_rel["x11extras"] = i++
	use_qt_rel["xml"] = i++
	use_qt_rel["xmlpatterns"] = i++
	for (i in use_qt_rel) {
		use_qt_rel[sprintf("%s_run", i)] = use_qt_rel[i] + 1000
		use_qt_rel[sprintf("%s_build", i)] = use_qt_rel[i] + 2000
	}

# Special handling of some variables.  More complicated patterns,
# i.e., for options helpers, should go into the respective functions
# instead.

# Some variables are usually indented with one or more extra tabs by porters.
	indent_multi_["OPTIONS_DEFINE"] = 1
	indent_multi_["OPTIONS_GROUP"] = 1
	indent_multi_["OPTIONS_MULTI"] = 1
	indent_multi_["USES"] = 1
	indent_multi_["USE_GL"] = 1
	indent_multi_["USE_QT"] = 1
	indent_multi_["USERS"] = 1
	indent_multi_["GROUPS"] = 1

# Sanitize whitespace but do *not* sort tokens; more complicated patterns below
	leave_unsorted_["BROKEN"] = 1
	leave_unsorted_["CARGO_CARGO_RUN"] = 1
	leave_unsorted_["CARGO_CRATES"] = 1
	leave_unsorted_["CARGO_FEATURES"] = 1
	leave_unsorted_["CATEGORIES"] = 1
	leave_unsorted_["COMMENT"] = 1
	leave_unsorted_["DAEMONARGS"] = 1
	leave_unsorted_["DEPRECATED"] = 1
	leave_unsorted_["DESKTOP_ENTRIES"] = 1
	leave_unsorted_["EXPIRATION_DATE"] = 1
	leave_unsorted_["EXTRACT_AFTER_ARGS"] = 1
	leave_unsorted_["EXTRACT_BEFORE_ARGS"] = 1
	leave_unsorted_["EXTRACT_CMD"] = 1
	leave_unsorted_["FLAVORS"] = 1
	leave_unsorted_["GH_TUPLE"] = 1
	leave_unsorted_["IGNORE"] = 1
	leave_unsorted_["LICENSE_NAME"] = 1
	leave_unsorted_["LICENSE_TEXT"] = 1
	leave_unsorted_["MASTER_SITES"] = 1
	leave_unsorted_["MAKE_JOBS_UNSAFE"] = 1
	leave_unsorted_["MOZ_SED_ARGS"] = 1
	leave_unsorted_["MOZCONFIG_SED"] = 1
	leave_unsorted_["RESTRICTED"] = 1

# Lines that are best not wrapped to 80 columns
# especially don't wrap BROKEN and IGNORE with \ or it introduces
# some spurious extra spaces when the message is displayed to users
	ignore_wrap_col_["BROKEN"] = 1
	ignore_wrap_col_["CARGO_CARGO_RUN"] = 1
	ignore_wrap_col_["COMMENT"] = 1
	ignore_wrap_col_["DEV_ERROR"] = 1
	ignore_wrap_col_["DEV_WARNING"] = 1
	ignore_wrap_col_["DISTFILES"] = 1
	ignore_wrap_col_["GH_TUPLE"] = 1
	ignore_wrap_col_["IGNORE"] = 1
	ignore_wrap_col_["MASTER_SITES"] = 1
	ignore_wrap_col_["RESTRICTED"] = 1

	print_as_newlines_["CARGO_CRATES"] = 1
	print_as_newlines_["CARGO_GH_CARGOTOML"] = 1
	print_as_newlines_["CFLAGS"] = 1
	print_as_newlines_["CPPFLAGS"] = 1
	print_as_newlines_["CXXFLAGS"] = 1
	print_as_newlines_["DESKTOP_ENTRIES"] = 1
	print_as_newlines_["DEV_ERROR"] = 1
	print_as_newlines_["DEV_WARNING"] = 1
	print_as_newlines_["DISTFILES"] = 1
	print_as_newlines_["DISTFILES"] = 1
	print_as_newlines_["DISTFILES"] = 1
	print_as_newlines_["GH_TUPLE"] = 1
	print_as_newlines_["LDFLAGS"] = 1
	print_as_newlines_["MOZ_OPTIONS"] = 1
	print_as_newlines_["OPTIONS_EXCLUDE"] = 1
	print_as_newlines_["PLIST_FILES"] = 1
	print_as_newlines_["PLIST_SUB"] = 1
	print_as_newlines_["SUB_LIST"] = 1

	archs["FreeBSD_11"] = 0
	archs["FreeBSD_12"] = 0
	archs["FreeBSD_13"] = 0
	archs["DragonFly"] = 0
	archs["aarch64"] = 0
	archs["amd64"] = 0
	archs["armv6"] = 0
	archs["armv7"] = 0
	archs["mips"] = 0
	archs["mips64"] = 0
	archs["powerpc"] = 0
	archs["powerpc64"] = 0
	for (i in archs) {
		leave_unsorted_[sprintf("BROKEN_%s", i)] = 1
		ignore_wrap_col_[sprintf("BROKEN_%s", i)] = 1
		leave_unsorted_[sprintf("IGNORE_%s", i)] = 1
		ignore_wrap_col_[sprintf("IGNORE_%s", i)] = 1
	}
}

function reset() {
	varname = "<<<unknown>>>"
	tokens_len = 1
	empty_lines_before_len = 1
	empty_lines_after_len = 1
	in_target = 0
	maybe_in_target = 0
	order = "default"
}

function strip_modifier(var) {
	gsub(/[:?\+]$/, "", var)
	return var
}

function assign_variable(var) {
	if (varname ~ /^LICENSE.*\+$/) { # e.g., LICENSE_FILE_GPLv3+ =, but not CFLAGS+=
		return sprintf("%s =", varname)
	} else {
		return sprintf("%s=%s", var, repeat("\t", indent_multi(var)))
	}
}

function dump_empty_lines(	i) {
	for (i = 1; i < empty_lines_before_len; i++) {
		print empty_lines_before[i]
	}
	for (i = 1; i < empty_lines_after_len; i++) {
		print empty_lines_after[i]
	}
	empty_lines_before_len = 1
	empty_lines_after_len = 1
}

function print_tokens(	i) {
	for (i = 1; i < empty_lines_before_len; i++) {
		print empty_lines_before[i]
	}

	if (tokens_len <= 1) {
		return
	}

	if (!leave_unsorted(varname)) {
		sort_array(tokens, tokens_len)
	}
	if (print_as_newlines(varname)) {
		print_newline_array(varname, assign_variable(varname), tokens, tokens_len)
	} else {
		print_token_array(varname, assign_variable(varname), tokens, tokens_len)
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

/^[A-Za-z0-9_-]+:/ && !/:=/ {
	skip = 1
	in_target = 1
}

!skip {
	portfmt_no_skip()
} function portfmt_no_skip(	i, arrtemp, quoted, single_quoted, token, eol_comment, eol_comment_tokens) {
	if (match($0, /^[\$\{\}a-zA-Z0-9._\-+ ]+[+?:]?=/)) {
		# Handle special lines like: VAR=xyz
		if (split($1, arrtemp, "=") > 1 && arrtemp[2] != "" && arrtemp[2] != "\\") {
			token = arrtemp[2]
			for (i = 3; i <= length(arrtemp); i++) {
				if (arrtemp[i] != "" && arrtemp[i] != "\\") {
					token = sprintf("%s=%s", token, arrtemp[i])
				}
			}
			tokens[tokens_len++] = token
		}
		varname = arrtemp[1]

		dump_empty_lines()
		i = 2
	} else {
		i = 1
	}

	if ($2 == "=") {
		$2 = ""
	}

	quoted = 0
	single_quoted = 0
	eol_comment = 0
	for (; i <= NF; i++) {
		if ($i == "\\") {
			break
		}

		# Try to push end of line comments out of the way
		# above the variable as a way to preserve them.
		# They clash badly with sorting tokens in variables.
		# We could add more special cases for this, but
		# often having them at the top is just as good.
		if ($i == "#" || eol_comment) {
			eol_comment_tokens[eol_comment++] = $i
			continue
		}

		# Prune eol backslash sandwiched with token
		gsub(/\\$/, "", $i)
		if ((single_quoted || quoted) && tokens_len > 0) {
			token = tokens[tokens_len - 1]
			tokens[tokens_len - 1] = sprintf("%s %s", token, $i)
		} else {
			tokens[tokens_len++] = $i
		}
		if (match($i, /"/) && !match($i, /""/)) {
			quoted = !quoted
		}
		if (match($i, /'/) && !match($i, /''/)) {
			single_quoted = !single_quoted
		}
	}

	if (eol_comment) {
		token = ""
		for (i = 0; i <= eol_comment; i++) {
			token = sprintf("%s %s", token, eol_comment_tokens[i])
		}
		gsub(/(^ | $)/, "", token)
		empty_lines_before[empty_lines_before_len++] = token
	}

	if (tokens_len == 1) {
		tokens[tokens_len++] = "<<<empty-value>>>"
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

/^LICENSE_PERMS_[A-Z0-9._\-+ ]+[+?:]?=/ ||
/^LICENSE_PERMS[+?:]?=/ {
	order = "license-perms"
}

/^PLIST_DIRS[+?:]?=/ ||
/^PLIST_FILES[+?:]?=/ {
	order = "plist-files"
}

/^USE_QT[+?:]?=/ {
	order = "use-qt"
}

END {
	print_tokens()
}
