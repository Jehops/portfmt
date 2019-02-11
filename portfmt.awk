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

function ceil(n,	i) {
	for (i = int(n); i < n; i++) { }
	return i;
}

function err(status, msg, a) {
	printf "portfmt: %s: %s\n", msg, a > "/dev/stderr"
	exit(status)
}

function print_(fmt, a, b, c, d, e, f, g, h) {
	if (INPLACE == 1) {
		printf fmt, a, b, c, d, e, f, g, h > FILENAME
	} else {
		printf fmt, a, b, c, d, e, f, g, h
	}
}

function max(a, b) {
	if (a > b) {
		return a
	} else {
		return b
	}
}

function repeat(s, n,	temp, i) {
	temp = ""
	for (i = 0; i < n; i++) {
		temp = sprintf("%s%s", s, temp)
	}
	return temp
}

function print_newline_array(var, start, arr, arrlen, goalcol,	end, i, level, sep, line) {
	# Handle variables with empty values
	if (arrlen <= 2 && (arr[1] == "<<<empty-value>>>" || arr[1] == "")) {
		print_("%s\n", start)
		return
	}
	sep = sprintf("%s%s", start, repeat("\t", ceil((goalcol - length(start)) / 8)))
	end = " \\\n"
	for (i = 1; i < arrlen; i++) {
		line = arr[i]
		if (line == "<<<empty-value>>>") {
			continue
		}
		if (i == arrlen - 1) {
			end = "\n"
		}
		print_("%s%s%s", sep, line, end)
		if (i == 1) {
			sep = repeat("\t", ceil(goalcol / 8))
		}
	}
}

function print_token_array(var, start, tokens, tokenslen, goalcol,	wrapcol, arr, arrlen, row, i, token) {
	if (ignore_wrap_col(var)) {
		wrapcol = 10000
	} else {
		wrapcol = WRAPCOL - goalcol
	}
	arrlen = 1
	row = ""
	for (i = 1; i < tokenslen; i++) {
		token = tokens[i]
		if (token == "<<<empty-value>>>") {
			continue
		}
		if ((length(row) + length(token)) > wrapcol) {
			if (row == "") {
				arr[arrlen++] = token
			} else {
				arr[arrlen++] = row
			}
			row = ""
		}
		if (row == "") {
			row = token
		} else {
			row = sprintf("%s %s", row, token)
		}
	}
	if (row != "") {
		arr[arrlen++] = row
	}
	print_newline_array(var, start, arr, arrlen, goalcol)
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
		gsub(/^"@[a-z\-]+ /, "", b)
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

function leave_unsorted(varname,	helper, var) {
	if (UNSORTED) {
		return 1
	}
	var = strip_modifier(varname)
	if (leave_unsorted_[var] ||
	    var ~ /^LICENSE_NAME_[A-Z0-9._\-+ ]$/ ||
	    var ~ /^LICENSE_TEXT_[A-Z0-9._\-+ ]$/) {
		return 1
	}
	for (helper in options_helpers_) {
		if (leave_unsorted_[helper] && (var ~ sprintf("_%s$", helper) || var ~ sprintf("_%s_OFF$", helper))) {
			return 1
		}
	}
	return 0
}

function ignore_wrap_col(varname,	var) {
	var = strip_modifier(varname)
	if (ignore_wrap_col_[var]) {
		return 1
	}
	if (var ~ /_DESC$/) {
		return 1
	}
	return 0
}

function print_as_newlines(varname,	helper, var) {
	var = strip_modifier(varname)
	if (print_as_newlines_[var]) {
		return 1
	}
	for (helper in options_helpers_) {
		if (print_as_newlines_[helper] && (var ~ sprintf("_%s$", helper) || var ~ sprintf("_%s_OFF$", helper))) {
			return 1
		}
	}
	return 0
}

BEGIN {
	INPLACE = ENVIRON["INPLACE"]
	if (INPLACE != 1 || ARGC < 2) {
		INPLACE = 0
	}
	UNSORTED = ENVIRON["UNSORTED"]
	if (UNSORTED != 1) {
		UNSORTED = 0
	}
	WRAPCOL = ENVIRON["WRAPCOL"]
	if (!WRAPCOL) {
		WRAPCOL = 80
	}
	setup_relations()
	reset()
}

function setup_relations(	i, archs) {
	# _OPTIONS_FLAGS
	options_helpers_["ALL_TARGET"] = 1
	options_helpers_["BINARY_ALIAS"] = 1
	options_helpers_["BROKEN"] = 1
	options_helpers_["CATEGORIES"] = 1
	options_helpers_["CFLAGS"] = 1
	options_helpers_["CONFIGURE_ENV"] = 1
	options_helpers_["CONFLICTS"] = 1
	options_helpers_["CONFLICTS_BUILD"] = 1
	options_helpers_["CONFLICTS_INSTALL"] = 1
	options_helpers_["CPPFLAGS"] = 1
	options_helpers_["CXXFLAGS"] = 1
	options_helpers_["DESKTOP_ENTRIES"] = 1
	options_helpers_["DISTFILES"] = 1
	options_helpers_["EXTRA_PATCHES"] = 1
	options_helpers_["EXTRACT_ONLY"] = 1
	options_helpers_["GH_ACCOUNT"] = 1
	options_helpers_["GH_PROJECT"] = 1
	options_helpers_["GH_SUBDIR"] = 1
	options_helpers_["GH_TAGNAME"] = 1
	options_helpers_["GH_TUPLE"] = 1
	options_helpers_["GL_ACCOUNT"] = 1
	options_helpers_["GL_COMMIT"] = 1
	options_helpers_["GL_PROJECT"] = 1
	options_helpers_["GL_SITE"] = 1
	options_helpers_["GL_SUBDIR"] = 1
	options_helpers_["GL_TUPLE"] = 1
	options_helpers_["IGNORE"] = 1
	options_helpers_["INFO"] = 1
	options_helpers_["INSTALL_TARGET"] = 1
	options_helpers_["LDFLAGS"] = 1
	options_helpers_["LIBS"] = 1
	options_helpers_["MAKE_ARGS"] = 1
	options_helpers_["MAKE_ENV"] = 1
	options_helpers_["MASTER_SITES"] = 1
	options_helpers_["PATCH_SITES"] = 1
	options_helpers_["PATCHFILES"] = 1
	options_helpers_["PLIST_DIRS"] = 1
	options_helpers_["PLIST_FILES"] = 1
	options_helpers_["PLIST_SUB"] = 1
	options_helpers_["PORTDOCS"] = 1
	options_helpers_["PORTEXAMPLES"] = 1
	options_helpers_["SUB_FILES"] = 1
	options_helpers_["SUB_LIST"] = 1
	options_helpers_["TEST_TARGET"] = 1
	options_helpers_["USES"] = 1

	# _OPTIONS_DEPENDS
	options_helpers_["PKG_DEPENDS"] = 1
	options_helpers_["FETCH_DEPENDS"] = 1
	options_helpers_["EXTRACT_DEPENDS"] = 1
	options_helpers_["PATCH_DEPENDS"] = 1
	options_helpers_["BUILD_DEPENDS"] = 1
	options_helpers_["LIB_DEPENDS"] = 1
	options_helpers_["RUN_DEPENDS"] = 1
	options_helpers_["TEST_DEPENDS"] = 1

	# Other special options helpers
	options_helpers_["USE"] = 1
	options_helpers_["VARS"] = 1

	# Add _OFF variants
	for (i in options_helpers_) {
		options_helpers_[sprintf("%s_OFF", i)] = 1
	}

	# Other irregular helpers
	options_helpers_["CONFIGURE_ENABLE"] = 1
	options_helpers_["CONFIGURE_WITH"] = 1
	options_helpers_["CMAKE_BOOL"] = 1
	options_helpers_["CMAKE_BOOL_OFF"] = 1
	options_helpers_["CMAKE_ON"] = 1
	options_helpers_["CMAKE_OFF"] = 1
	options_helpers_["DESC"] = 1
	options_helpers_["MESON_TRUE"] = 1
	options_helpers_["MESON_FALSE"] = 1
	options_helpers_["MESON_YES"] = 1
	options_helpers_["MESON_NO"] = 1
	options_helpers_["CONFIGURE_ON"] = 1
	options_helpers_["MESON_ON"] = 1
	options_helpers_["QMAKE_ON"] = 1
	options_helpers_["CONFIGURE_OFF"] = 1
	options_helpers_["MESON_OFF"] = 1
	options_helpers_["QMAKE_OFF"] = 1

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

# Sanitize whitespace but do *not* sort tokens; more complicated patterns below
	leave_unsorted_["BROKEN"] = 1
	leave_unsorted_["CARGO_CARGO_RUN"] = 1
	leave_unsorted_["CARGO_CRATES"] = 1
	leave_unsorted_["CARGO_FEATURES"] = 1
	leave_unsorted_["CATEGORIES"] = 1
	leave_unsorted_["CFLAGS"] = 1
	leave_unsorted_["COMMENT"] = 1
	leave_unsorted_["CPPFLAGS"] = 1
	leave_unsorted_["CXXFLAGS"] = 1
	leave_unsorted_["DAEMONARGS"] = 1
	leave_unsorted_["DEPRECATED"] = 1
	leave_unsorted_["DESC"] = 1
	leave_unsorted_["DESKTOP_ENTRIES"] = 1
	leave_unsorted_["EXPIRATION_DATE"] = 1
	leave_unsorted_["EXTRACT_AFTER_ARGS"] = 1
	leave_unsorted_["EXTRACT_BEFORE_ARGS"] = 1
	leave_unsorted_["EXTRACT_CMD"] = 1
	leave_unsorted_["FLAVORS"] = 1
	leave_unsorted_["GH_TUPLE"] = 1
	leave_unsorted_["IGNORE"] = 1
	leave_unsorted_["LDFLAGS"] = 1
	leave_unsorted_["LICENSE_NAME"] = 1
	leave_unsorted_["LICENSE_TEXT"] = 1
	leave_unsorted_["MAKE_JOBS_UNSAFE"] = 1
	leave_unsorted_["MASTER_SITES"] = 1
	leave_unsorted_["MOZ_SED_ARGS"] = 1
	leave_unsorted_["MOZCONFIG_SED"] = 1
	leave_unsorted_["RESTRICTED"] = 1

# Don't indent with the rest of the variables in a paragraph
	skip_goalcol_["CARGO_CRATES"] = 1
	skip_goalcol_["DISTVERSIONPREFIX"] = 1
	skip_goalcol_["DISTVERSIONSUFFIX"] = 1

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

	print_as_newlines_["BUILD_DEPENDS"] = 1
	print_as_newlines_["CARGO_CRATES"] = 1
	print_as_newlines_["CARGO_GH_CARGOTOML"] = 1
	print_as_newlines_["CFLAGS"] = 1
	print_as_newlines_["CMAKE_ARGS"] = 1
	print_as_newlines_["CMAKE_BOOL"] = 1
	print_as_newlines_["CONFIGURE_ARGS"] = 1
	print_as_newlines_["CONFIGURE_ENV"] = 1
	print_as_newlines_["CONFIGURE_OFF"] = 1
	print_as_newlines_["CONFIGURE_ON"] = 1
	print_as_newlines_["CPPFLAGS"] = 1
	print_as_newlines_["CXXFLAGS"] = 1
	print_as_newlines_["DESKTOP_ENTRIES"] = 1
	print_as_newlines_["DEV_ERROR"] = 1
	print_as_newlines_["DEV_WARNING"] = 1
	print_as_newlines_["DISTFILES"] = 1
	print_as_newlines_["EXTRACT_DEPENDS"] = 1
	print_as_newlines_["FETCH_DEPENDS"] = 1
	print_as_newlines_["GH_TUPLE"] = 1
	print_as_newlines_["LDFLAGS"] = 1
	print_as_newlines_["LIB_DEPENDS"] = 1
	print_as_newlines_["MAKE_ARGS"] = 1
	print_as_newlines_["MAKE_ENV"] = 1
	print_as_newlines_["MASTER_SITES"] = 1
	print_as_newlines_["MOZ_OPTIONS"] = 1
	print_as_newlines_["OPTIONS_EXCLUDE"] = 1
	print_as_newlines_["PATCH_DEPENDS"] = 1
	print_as_newlines_["PKG_DEPENDS"] = 1
	print_as_newlines_["PLIST_FILES"] = 1
	print_as_newlines_["PLIST_SUB"] = 1
	print_as_newlines_["RUN_DEPENDS"] = 1
	print_as_newlines_["SUB_LIST"] = 1
	print_as_newlines_["TEST_DEPENDS"] = 1
	print_as_newlines_["VARS"] = 1

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
		return sprintf("%s=", var)
	}
}

function print_tokens(	i) {
	output[++output_len] = "empty"
	output[output_len, "length"] = empty_lines_before_len - 1
	for (i = 1; i < empty_lines_before_len; i++) {
		output[output_len, i] = empty_lines_before[i]
	}

	if (tokens_len <= 1) {
		reset()
		return
	}

	if (!leave_unsorted(varname)) {
		sort_array(tokens, tokens_len)
	}

	output[++output_len] = "tokens"
	output[output_len, "var"] = varname
	output[output_len, "length"] = tokens_len
	for (i = 1; i <= tokens_len; i++) {
		output[output_len, i] = tokens[i]
	}

	output[++output_len] = "empty"
	output[output_len, "length"] = empty_lines_after_len - 1
	for (i = 1; i < empty_lines_after_len; i++) {
		output[output_len, i] = empty_lines_after[i]
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

function consume_token(line, pos, startchar, endchar,	start, i, c, counter) {
	start = pos
	counter = 0
	for (i = pos; i <= length(line); i++) {
		c = substr(line, i, 1)
		if (startchar == endchar) {
			if (c == startchar) {
				if (counter == 1) {
					return i
				} else {
					counter++
				}
			}
		} else {
			if (c == startchar) {
				counter++
			} else if (c == endchar && counter == 1) {
				return i
			} else if (c == endchar) {
				counter--
			}
		}
	}
	err(1, "tokenizer", sprintf("expected %s", endchar)) 
}

!skip {
	portfmt_no_skip()
} function portfmt_no_skip(	i, c, arrtemp, dollar, start, token, pos) {
	if (match($0, /^[\$\{\}a-zA-Z0-9._\-+ ]+[+?:]?=/)) {
		# Handle special lines like: VAR=xyz
		if (split(substr($0, RSTART, RLENGTH), arrtemp, "=") > 1 && arrtemp[2] != "" && arrtemp[2] != "\\") {
			token = arrtemp[2]
			for (i = 3; i <= length(arrtemp); i++) {
				if (arrtemp[i] != "" && arrtemp[i] != "\\") {
					token = sprintf("%s=%s", token, arrtemp[i])
				}
			}
			tokens[tokens_len++] = token
		}
		varname = arrtemp[1]

		i = 2
		pos = RLENGTH + 1
	} else {
		pos = 1
		i = 1
	}

	if (pos >= length($0)) {
		tokens[tokens_len++] = "<<<empty-value>>>"
		return
	}

	dollar = 0
	start = pos
	gsub(/\\$/, "", $0)
	for (i = pos; i <= length($0); i++) {
		c = substr($0, i, 1)
		if (dollar) {
			if (c == "{") {
				i = consume_token($0, i, "{", "}")
				dollar = 0
			} else {
				err(1, "tokenizer", "expected {")
			}
		} else {
			if (c == " " || c == "\t") {
				token = substr($0, start, i - start + 1)
				gsub(/^[[:blank:]]*/, "", token)
				gsub(/[[:blank:]]*$/, "", token)
				if (token != "" && token != "\\") {
					tokens[tokens_len++] = token
				}
				start = i
			} else if (c == "\"") {
				i = consume_token($0, i, "\"", "\"")
			} else if (c == "'") {
				i = consume_token($0, i, "'", "'")
			} else if (c == "$") {
				dollar++
			} else if (c == "#") {
				# Try to push end of line comments out of the way above
				# the variable as a way to preserve them.  They clash badly
				# with sorting tokens in variables.  We could add more
				# special cases for this, but often having them at the top
				# is just as good.
				empty_lines_before[empty_lines_before_len++] = substr($0, i)
				tokens[tokens_len++] = "<<<empty-value>>>"
				return
			}
		}
	}
	token = substr($0, start, i - start + 1)
	gsub(/^[[:blank:]]*/, "", token)
	gsub(/[[:blank:]]*$/, "", token)
	if (token != "" && token != "\\") {
		tokens[tokens_len++] = token
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

/^[A-Z0-9_]+_PLIST_DIRS[+?:]?=/ ||
/^[A-Z0-9_]+_PLIST_FILES[+?:]?=/ ||
/^PLIST_DIRS[+?:]?=/ ||
/^PLIST_FILES[+?:]?=/ {
	order = "plist-files"
}

/^USE_QT[+?:]?=/ {
	order = "use-qt"
}

function skip_goalcol(var) {
	if (skip_goalcol_[var]) {
		return 1
	}

	if (var ~ /^LICENSE_(FILE|NAME)_/) {
		return 1
	}

	return 0
}

function indent_goalcol(var,	d, varlength) {
	varlength = length(var)
	# include assignment '='
	d = 1
	if (((varlength + 1) % 8) == 0) {
		d++
	}
	return ceil((varlength + d) / 8) * 8
}

function find_goalcol_per_parapgraph(goalcol, output, output_len,	i, k, ok, last, moving_goalcol, var)  {
	moving_goalcol = 0
	last = 1
	for (i = 1; i <= output_len; i++) {
		if (output[i] == "tokens") {
			var = output[i, "var"]
			if (skip_goalcol(var)) {
				goalcol[i] = indent_goalcol(var)
			} else {
				moving_goalcol = max(indent_goalcol(var), moving_goalcol)
			}
		} else if (output[i] == "empty") {
			if (output[i, "length"] == 0) {
				continue
			}
			ok = 1
			for (k = 1; k <= output[i, "length"]; k++) {
				if (output[i, k] == "") {
					break
				}
				if (output[i, k] ~ /^#/) {
					ok = 0
					break
				}
			}
			if (!ok) {
				continue
			}

			moving_goalcol = max(16, moving_goalcol)
			for (k = last; k < i; k++) {
				if (!skip_goalcol(output[k, "var"])) {
					goalcol[k] = moving_goalcol
				}
			}
			last = i
			moving_goalcol = 0
		} else {
			err(1, "Unhandled output type", output[i])
		}
	}
	if (moving_goalcol) {
		moving_goalcol = max(16, moving_goalcol)
		for (k = last; k < i; k++) {
			if (!skip_goalcol(output[k, "var"])) {
				goalcol[k] = moving_goalcol
			}
		}
	}
}

function final_output(	goalcol, i, j, tokens, tokens_len, var) {
	find_goalcol_per_parapgraph(goalcol, output, output_len)

	for (i = 1; i <= output_len; i++) {
		if (output[i] == "tokens") {
			var = output[i, "var"]
			tokens_len = output[i, "length"]
			for (j = 1; j <= tokens_len; j++) {
				tokens[j] = output[i, j]
			}
			if (print_as_newlines(var)) {
				print_newline_array(var, assign_variable(var), tokens, tokens_len, goalcol[i])
			} else {
				print_token_array(var, assign_variable(var), tokens, tokens_len, goalcol[i])
			}
		} else if (output[i] == "empty") {
			for (j = 1; j <= output[i, "length"]; j++) {
				print_("%s\n", output[i, j])
			}
		} else {
			err(1, "Unhandled output type", output[i])
		}
	}
}

END {
	print_tokens()
	final_output()
}
