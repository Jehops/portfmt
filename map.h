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
#pragma once

struct Array;
struct Map;
typedef int (*MapCompareFn)(const void *, const void *, void *);

struct Map *map_new(MapCompareFn, void *, void *, void *);
void map_free(struct Map *);
void map_add(struct Map *, void *, void *);
void map_remove(struct Map *, void *);
void *map_get(struct Map *, void *);
int map_contains(struct Map *, void *);
size_t map_len(struct Map *);
void map_truncate(struct Map *);

struct MapIterator *map_iterator(struct Map *);
void map_iterator_free(struct MapIterator **);
void *map_iterator_next(struct MapIterator **, void **);

#define MAP_FOREACH(MAP, KEYTYPE, KEYVAR, VALTYPE, VALVAR) \
	for (struct MapIterator *__##KEYVAR##_iter __attribute__((cleanup(map_iterator_free))) = map_iterator(MAP); __##KEYVAR##_iter != NULL; map_iterator_free(&__##KEYVAR##_iter)) \
	for (VALTYPE VALVAR = NULL; __##KEYVAR##_iter != NULL; map_iterator_free(&__##KEYVAR##_iter)) \
	for (KEYTYPE KEYVAR = map_iterator_next(&__##KEYVAR##_iter, (void **)&VALVAR); KEYVAR != NULL; KEYVAR = map_iterator_next(&__##KEYVAR##_iter, (void **)&VALVAR))
