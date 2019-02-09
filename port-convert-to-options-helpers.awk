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
# Convert ports to options helpers.  Pipe the output to portfmt.awk.
#

BEGIN {
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

	irregular_helpers["CONFIGURE_ARGS", 0] = "CONFIGURE_OFF"
	irregular_helpers["CONFIGURE_ARGS", 1] = "CONFIGURE_ON"

	reset()
}

function lookup_helper(var, state,	helper) {
	helper = irregular_helpers[var, state]
	if (helper) {
		return helper
	}
	if (state) {
		helper = var
	} else {
		helper = sprintf("%s_OFF", var)
	}

	if (options_helpers_[helper]) {
		return helper
	}
	return ""
}

function reset() {
	opt = "<<<unknown>>>"
	state = 0
	in_target = 0
	maybe_in_target = 0
	skip_next_if_empty = 0
}

function on_off(state, on, off) {
	if (state) {
		return on
	} else {
		return off
	}
}

/^\.if !?\$\{PORT_OPTIONS:M[A-Z0-9_]+}/, /^.endif$/ {
	if ($0 ~ /^.endif$/) {
		reset()
		next
	} else if ($0 ~ /^\.if !?\$\{PORT_OPTIONS:M[A-Z0-9_]+}/) {
		if (match($0, /:M[A-Z0-9_]+\}/)) {
			opt = substr($0, RSTART, RLENGTH)
			gsub(/^:M/, "", opt)
			gsub(/\}$/, "", opt)
		}
		if (match($0, /^\.if !/)) {
			state = 0
		} else {
			state = 1
		}
		if (in_target) {
			printf "\n%s-%s-%s:\n", in_target, opt, on_off(state, "on", "off")
		}
		next
	} else if ($0 ~ /^.else$/) {
		state = !state
		if (in_target) {
			printf "\n%s-%s-on:\n", in_target, opt, on_off(state)
		}
		next
	} else if (opt == "<<<unknown>>>") {
		next
	} else if ($0 ~ /^[A-Z_]+\+?=[ 	]*.*$/) {
		extract_helper()
		next
	}
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

/^[ \t]*$/ { # empty line
	in_target = 0
	maybe_in_target = 1
	if (skip_next_if_empty) {
		skip_next_if_empty = 0
		next
	}
}

/^[A-Za-z0-9_-]+:/ && !/:=/ {
	in_target = $0
	gsub(/:$/, "", in_target)
}

/^\.include <bsd\.port\.options\.mk>$/ {
	skip_next_if_empty = 1
	next
}

{
	print
}

function extract_helper(	var, rest) {
	if (match($0, /^[A-Z_]+\+?=/)) {
		var = substr($0, RSTART, RLENGTH)
		gsub(/\+?=$/, "", var)
		rest = substr($0, RSTART + RLENGTH)
		gsub(/^[ 	]+/, "", rest)
		if (var == "PLIST_SUB") {
			if (state && rest == sprintf("%s=\"\"", opt)) {
				return
			}
			if (!state && rest == sprintf("%s=\"@comment \"", opt)) {
				return
			}
		}
		helper = lookup_helper(var, state)
		if (!helper) {
			if (var ~ /^USE_/) {
				gsub(/^USE_/, "", var)
				if (rest ~ / /) {
					gsub(/ /, ",", rest)
				}
				printf("%s_USE%s+=\t%s=%s\n", opt, on_off(state, "", "_OFF"), var, rest)
			} else {
				if (rest ~ / /) {
					printf("%s_VARS%s+=\t%s=\"%s\"\n", opt, on_off(state, "", "_OFF"), var, rest)
				} else {
					printf("%s_VARS%s+=\t%s=%s\n", opt, on_off(state, "", "_OFF"), var, rest)
				}
			}
		} else {
			printf("%s_%s=\t%s\n", opt, helper, rest)
		}
	}
}

