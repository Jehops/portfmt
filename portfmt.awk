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

function print_newline_array(var, start, arr, arrlen,	end, i, level, sep) {
	sep = sprintf("%s\t", start)
	end = " \\\n"
	for (i = 1; i < arrlen; i++) {
		if (i == arrlen - 1) {
			end = "\n"
		}
		printf "%s%s%s", sep, arr[i], end
		if (i == 1) {
			level = indent_level(start)
			if (indent_twice[var]) {
				level++
			}
			sep = repeat("\t", level)
		}
	}
}

function print_token_array(var, start, tokens, tokenslen,	wrapcol, arr, arrlen, row, col, i) {
	if (ignore_wrap_col[var]) {
		wrapcol = 1000
	} else {
		wrapcol = 80 - length(start) - 8
	}
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
	print_newline_array(var, start, arr, arrlen)
}

function greater_helper(rel, a, b,	ai, bi) {
	ai = rel[a]
	bi = rel[b]
	if (ai == 0 && bi == 0) {
		return a > b
	} else if (ai == 0) {
		return false # b > a
	} else if (bi == 0) {
		return true # a > b
	} else {
		return ai > bi
	}
}

function greater(a, b) {
	if (order == "license-perms") {
		return greater_helper(license_perms_rel, a, b)
	} else if (order == "use-qt") {
		return greater_helper(use_qt_rel, a, b)
	} else {
		return a > b
	}
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


### Script starts here

BEGIN {
	setup_relations()
	reset()
}

function setup_relations(	i) {
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

# Some variables are usually indented with an extra tab by porters.
	indent_twice["OPTIONS_DEFINE"] = 1
	indent_twice["USES"] = 1
	indent_twice["USE_GL"] = 1
	indent_twice["USE_QT"] = 1
	indent_twice["USERS"] = 1
	indent_twice["GROUPS"] = 1

	ignore_wrap_col["GH_TUPLE"] = 1
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
	order = "default"
}

function assign_variable(var) {
	if (indent_twice[var]) {
		return sprintf("%s=\t", var)
	} else {
		if (varname ~ /^LICENSE.*\+$/) { # e.g., LICENSE_FILE_GPLv3+ =, but not CFLAGS+=
			return sprintf("%s =", varname)
		} else {
			return sprintf("%s=", var)
		}
	}
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
		print_token_array(varname, assign_variable(varname), tokens, tokens_len)
	} else {
		print_newline_array(varname, assign_variable(varname), tokens, tokens_len)
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
/^CARGO_CRATES[+?:]?=/ ||
/^GH_TUPLE[+?:]?=/ ||
/^DESKTOP_ENTRIES[+?:]?=/ ||
/^[A-Z0-9_]+_DESKTOP_ENTRIES[+?:]?=/ ||
/^COMMENT[+?:]?=/ ||
/^LICENSE_NAME[+?:]?=/ ||
/^LICENSE_NAME_[A-Z0-9._-+ ]+[+?:]?=/ ||
/^[A-Z_]+_DESC[+?:]?=/ ||
/^FLAVORS[+?:]?=/ ||
/^LLD_UNSAFE[+?:]?=/ ||
/^CARGO_FEATURES[+?:]?=/ ||
/^MAKE_JOBS_UNSAFE[+?:]?=/ {
	sorted = 0
}

/^LICENSE_PERMS_[A-Z0-9._-+ ]+[+?:]?=/ ||
/^LICENSE_PERMS[+?:]?=/ {
	order = "license-perms"
}

/^USE_QT[+?:]?=/ {
	order = "use-qt"
}

/^([A-Z_]+_)?MASTER_SITES[+?:]?=/ ||
/^[a-zA-Z0-9_]+_DEPENDS[+?:]?=/ ||
/^[a-zA-Z0-9_]+_C(XX|PP)?FLAGS[+?:]?=/ ||
/^C(XX|PP)?FLAGS[+?:]?=/ ||
/^CARGO_CRATES?[+?:]?=/ ||
/^DESKTOP_ENTRIES[+?:]?=/ ||
/^OPTIONS_EXCLUDE:=/ ||
/^PLIST_FILES[+?:]?=/ ||
/^SUB_LIST[+?:]?=/ ||
/^PLIST_SUB[+?:]?=/ ||
/^GH_TUPLE[+?:]?=/ ||
/^CARGO_GH_CARGOTOML[+?:]?=/ ||
/^[CM][A-Z_]+_ARGS(_OFF)?[+?:]?=/ ||
/^[A-Z0-9_]+_DESKTOP_ENTRIES[+?:]?=/ ||
/^[A-Z0-9_]+_GH_TUPLE[+?:]?=/ ||
/^[A-Z0-9_]+_PLIST_FILES[+?:]?=/ ||
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
	if (match($0, /^[a-zA-Z0-9._-+ ]+[+?:]?=/)) {
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

	if ($2 == "=") {
		$2 = ""
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
