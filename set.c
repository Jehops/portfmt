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
#include "map.h"
#include "set.h"
#include "util.h"

struct Set {
	struct Map *map;
};

struct SetIterator {
	struct MapIterator *iter;
};

struct Set *
set_new(SetCompareFn compare, void *compare_userdata, void *freefn)
{
	struct Set *set = xmalloc(sizeof(struct Set));
	set->map = map_new(compare, compare_userdata, freefn, NULL);
	return set;
}

void
set_free(struct Set *set)
{
	if (set == NULL) {
		return;
	}

	set_truncate(set);
	free(set);
}

void
set_add(struct Set *set, void *element)
{
	map_add(set->map, element, element);
}

void
set_remove(struct Set *set, void *element)
{
	map_remove(set->map, element);
}

void *
set_get(struct Set *set, void *element)
{
	return map_get(set->map, element);
}

int
set_contains(struct Set *set, void *element)
{
	return map_get(set->map, element) != NULL;
}

size_t
set_len(struct Set *set)
{
	return map_len(set->map);
}

struct Array *
set_toarray(struct Set *set)
{
	struct Array *array = array_new();
	MAP_FOREACH (set->map, void *, key, void *, element) {
		array_append(array, key);
	}
	return array;
}

void
set_truncate(struct Set *set)
{
	map_truncate(set->map);
}

struct SetIterator *
set_iterator(struct Set *set)
{
	struct SetIterator *iter = xmalloc(sizeof(struct SetIterator));
	iter->iter = map_iterator(set->map);
	return iter;
}

void
set_iterator_free(struct SetIterator **iter_)
{
	struct SetIterator *iter = *iter_;
	if (iter != NULL) {
		map_iterator_free(&iter->iter);
		free(iter);
		*iter_ = NULL;
	}
}

void *
set_iterator_next(struct SetIterator **iter_)
{
	void *element;
	struct SetIterator *iter = *iter_;
	void *next = map_iterator_next(&iter->iter, &element);
	if (next == NULL) {
		set_iterator_free(iter_);
		*iter_ = NULL;
		return NULL;
	}
	return next;
}
