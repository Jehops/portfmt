/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2021 Tobias Kortkamp <tobik@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#pragma once

struct Array;
struct Parser;
enum ParserError;
enum ParserMergeBehavior;

struct ParserEdit {
	struct Parser *subparser;
	const char *arg1;
	enum ParserMergeBehavior merge_behavior;
};

struct ParserEditOutput {
	int (*keyfilter)(struct Parser *, const char *, void *);
	void *keyuserdata;
	int (*filter)(struct Parser *, const char *, void *);
	void *userdata;
	int found;
	int return_values;
	struct Array *keys;
	struct Array *values;
};

struct Array *edit_bump_revision(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
struct Array *edit_merge(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
struct Array *edit_set_version(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
struct Array *kakoune_select_object_on_line(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
struct Array *lint_bsd_port(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
struct Array *lint_clones(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
struct Array *lint_order(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
struct Array *output_unknown_targets(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
struct Array *output_unknown_variables(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
struct Array *output_variable_value(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
struct Array *refactor_collapse_adjacent_variables(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
struct Array *refactor_dedup_tokens(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
struct Array *refactor_remove_consecutive_empty_lines(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
struct Array *refactor_sanitize_append_modifier(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
struct Array *refactor_sanitize_cmake_args(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
struct Array *refactor_sanitize_comments(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
struct Array *refactor_sanitize_eol_comments(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
