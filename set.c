/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2020 Tobias Kortkamp <tobik@FreeBSD.org>
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

#include "config.h"

#include <stdlib.h>

#include "array.h"
#include "set.h"
#include "util.h"

struct Set {
	struct Array *array;
	SetCompareFn compare;
	void *compare_userdata;
	void (*free)(void *);
};

struct Set *
set_new(SetCompareFn compare, void *compare_userdata, void *freefn)
{
	struct Set *set = xmalloc(sizeof(struct Set));
	set->array = array_new();
	set->compare = compare;
	set->compare_userdata = compare_userdata;
	set->free = freefn;
	return set;
}

void
set_free(struct Set *set)
{
	if (set == NULL) {
		return;
	}

	if (set->free != NULL) {
		for (size_t i = 0; i < array_len(set->array); i++) {
			set->free(array_get(set->array, i));
		}
	}
	array_free(set->array);
	free(set);
}

void
set_add(struct Set *set, void *value)
{
	if (!set_contains(set, value)) {
		array_append(set->array, value);
	}
}

int
set_contains(struct Set *set, void *value)
{
	return array_find(set->array, value, set->compare, set->compare_userdata) != -1;
}

struct Array *
set_toarray(struct Set *set)
{
	struct Array *array = array_new();
	for (size_t i = 0; i < array_len(set->array); i++) {
		array_append(array, array_get(set->array, i));
	}
	return array;
}

void
set_truncate(struct Set *set)
{
	if (set->free != NULL) {
		for (size_t i = 0; i < array_len(set->array); i++) {
			set->free(array_get(set->array, i));
		}
	}
	array_truncate(set->array);
}
