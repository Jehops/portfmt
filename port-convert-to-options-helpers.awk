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
		return var
	} else {
		return sprintf("%s_OFF", var)
	}
}

function reset() {
	opt = "<<<unknown>>>"
	state = 0
	in_target = 0
	maybe_in_target = 0
}

function on_off(state) {
	if (state) {
		return "on"
	} else {
		return "off"
	}
}

/^\.if \$\{PORT_OPTIONS:M[A-Z0-9_]+}/, /^.endif$/ {
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
			printf "\n%s-%s-%s:\n", in_target, opt, on_off(state)
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
}

/^[A-Za-z0-9_-]+:/ && !/:=/ {
	in_target = $0
	gsub(/:$/, "", in_target)
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
		printf("%s_%s=\t%s\n", opt, lookup_helper(var, state), rest)
	}
}

