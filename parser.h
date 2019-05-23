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

#include "array.h"

enum ParserBehavior {
	PARSER_DEFAULT = 0,
	PARSER_COLLAPSE_ADJACENT_VARIABLES = 2,
	PARSER_FORMAT_TARGET_COMMANDS = 4,
	PARSER_KEEP_EOL_COMMENTS = 8,
	PARSER_OUTPUT_DUMP_TOKENS = 16,
	PARSER_OUTPUT_EDITED = 32,
	PARSER_OUTPUT_RAWLINES = 64,
	PARSER_OUTPUT_REFORMAT = 128,
	PARSER_SANITIZE_APPEND = 256,
	PARSER_UNSORTED_VARIABLES = 512,
};

struct ParserSettings {
	enum ParserBehavior behavior;
	int target_command_format_threshold;
	size_t target_command_format_wrapcol;
	size_t wrapcol;
};

struct Parser;

struct Parser *parser_new(struct ParserSettings *);
void parser_init_settings(struct ParserSettings *);
void parser_read(struct Parser *, char *);
void parser_free(struct Parser *);
char *parser_lookup_variable(struct Parser *, const char *);
struct Array *parser_get_all_variable_names(struct Parser *);
int parser_edit_bump_revision(struct Parser *);
int parser_edit_set_variable(struct Parser *, const char *, const char *, const char *);
void parser_read_finish(struct Parser *);
void parser_output_prepare(struct Parser *);
int parser_output_variable_value(struct Parser *, const char *);
void parser_output_write(struct Parser *, int);
int parser_output_variable_order(struct Parser *);
int parser_output_linted_variable_order(struct Parser *);
int parser_output_unknown_variables(struct Parser *);
