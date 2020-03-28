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
struct Set;
struct SetIterator;
typedef int (*SetCompareFn)(const void *, const void *, void *);

struct Set *set_new(SetCompareFn, void *, void *);
void set_free(struct Set *);
void set_add(struct Set *, void *);
void set_remove(struct Set *, void *);
void *set_get(struct Set *, void *);
int set_contains(struct Set *, void *);
size_t set_len(struct Set *);
struct Array *set_toarray(struct Set *);
void set_truncate(struct Set *);

struct SetIterator *set_iterator(struct Set *);
void set_iterator_free(struct SetIterator **);
void *set_iterator_next(struct SetIterator **);

#define SET_FOREACH(SET, TYPE, VAR) \
	for (struct SetIterator *__##VAR##_iter __attribute__((cleanup(set_iterator_free))) = set_iterator(SET); __##VAR##_iter != NULL; set_iterator_free(&__##VAR##_iter)) \
	for (TYPE VAR = set_iterator_next(&__##VAR##_iter); VAR != NULL; VAR = set_iterator_next(&__##VAR##_iter))
