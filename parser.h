/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019 Tobias Kortkamp <tobik@FreeBSD.org>
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

enum ParserBehavior {
	PARSER_DEFAULT = 0,
	PARSER_COLLAPSE_ADJACENT_VARIABLES = 2,
	PARSER_FORMAT_TARGET_COMMANDS = 4,
	PARSER_KEEP_EOL_COMMENTS = 8,
	PARSER_OUTPUT_DUMP_TOKENS = 16,
	PARSER_OUTPUT_EDITED = 32,
	PARSER_OUTPUT_INPLACE = 64,
	PARSER_OUTPUT_NO_COLOR = 128,
	PARSER_OUTPUT_RAWLINES = 256,
	PARSER_OUTPUT_REFORMAT = 512,
	PARSER_SANITIZE_APPEND = 1024,
	PARSER_UNSORTED_VARIABLES = 2048,
};

enum ParserError {
	PARSER_ERROR_OK = 0,
	PARSER_ERROR_BUFFER_TOO_SMALL,
	PARSER_ERROR_EDIT_FAILED,
	PARSER_ERROR_EXPECTED_CHAR,
	PARSER_ERROR_EXPECTED_INT,
	PARSER_ERROR_INVALID_REGEXP,
	PARSER_ERROR_IO,
	PARSER_ERROR_NOT_FOUND,
	PARSER_ERROR_UNHANDLED_TOKEN_TYPE,
	PARSER_ERROR_UNSPECIFIED,
};

struct ParserSettings {
	enum ParserBehavior behavior;
	int target_command_format_threshold;
	size_t target_command_format_wrapcol;
	size_t wrapcol;
};

struct Array;
struct Parser;
struct Token;

typedef struct Array *(*ParserEditFn)(struct Parser *, struct Array *, enum ParserError *, const void *);

struct Parser *parser_new(struct ParserSettings *);
void parser_init_settings(struct ParserSettings *);
enum ParserError parser_read_from_buffer(struct Parser *, const char *, size_t);
enum ParserError parser_read_from_file(struct Parser *, FILE *);
enum ParserError parser_read_finish(struct Parser *);
char *parser_error_tostring(struct Parser *);
void parser_free(struct Parser *);
enum ParserError parser_output_write_to_file(struct Parser *, FILE *);

enum ParserError parser_edit(struct Parser *, ParserEditFn, const void *);
void parser_enqueue_output(struct Parser *, const char *);
int parser_lookup_variable(struct Parser *, const char *, struct Array **, struct Array **);
int parser_lookup_variable_str(struct Parser *, const char *, char **, char **);
void parser_mark_for_gc(struct Parser *, struct Token *);
void parser_mark_edited(struct Parser *, struct Token *);
struct ParserSettings parser_settings(struct Parser *);

struct Array *refactor_collapse_adjacent_variables(struct Parser *, struct Array *, enum ParserError *error, const void *);
struct Array *refactor_sanitize_append_modifier(struct Parser *, struct Array *, enum ParserError *error, const void *);
struct Array *refactor_sanitize_eol_comments(struct Parser *, struct Array *, enum ParserError *error, const void *);
struct Array *edit_bump_revision(struct Parser *, struct Array *, enum ParserError *error, const void *);
struct Array *edit_merge(struct Parser *, struct Array *, enum ParserError *error, const void *);
struct Array *edit_output_variable_value(struct Parser *, struct Array *, enum ParserError *error, const void *);
struct Array *edit_output_unknown_variables(struct Parser *, struct Array *, enum ParserError *error, const void *);
struct Array *edit_set_version(struct Parser *, struct Array *, enum ParserError *error, const void *);
struct Array *lint_order(struct Parser *, struct Array *, enum ParserError *error, const void *);
