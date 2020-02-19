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
#include "tree.h"
#include "util.h"

struct SetNode {
	SPLAY_ENTRY(SetNode) entry;
	void *value;
	struct Set *set;
};

struct Set {
	SPLAY_HEAD(SetTree, SetNode) root;
	SetCompareFn compare;
	void *compare_userdata;
	void (*free)(void *);
};

static int nodecmp(struct SetNode *, struct SetNode *);
SPLAY_PROTOTYPE(SetTree, SetNode, entry, nodecmp);

struct Set *
set_new(SetCompareFn compare, void *compare_userdata, void *freefn)
{
	struct Set *set = xmalloc(sizeof(struct Set));
	SPLAY_INIT(&set->root);
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

	set_truncate(set);
	free(set);
}

void
set_add(struct Set *set, void *value)
{
	if (!set_contains(set, value)) {
		struct SetNode *node = xmalloc(sizeof(struct SetNode));
		node->value = value;
		node->set = set;
		SPLAY_INSERT(SetTree, &set->root, node);
	}
}

int
set_contains(struct Set *set, void *value)
{
	struct SetNode node;
	node.value = value;
	node.set = set;
	return SPLAY_FIND(SetTree, &set->root, &node) != NULL;
}

struct Array *
set_toarray(struct Set *set)
{
	struct Array *array = array_new();
	struct SetNode *node;
	SPLAY_FOREACH (node, SetTree, &set->root) {
		array_append(array, node->value);
	}
	return array;
}

void
set_truncate(struct Set *set)
{
	struct SetNode *node;
	struct SetNode *next;
	for (node = SPLAY_MIN(SetTree, &set->root); node != NULL; node = next) {
		next = SPLAY_NEXT(SetTree, &set->root, node);
		SPLAY_REMOVE(SetTree, &set->root, node);
		if (set->free != NULL) {
			set->free(node->value);
		}
		free(node);
	}
	SPLAY_INIT(&set->root);
}

int
nodecmp(struct SetNode *e1, struct SetNode *e2)
{
	if (e1->set->compare == NULL) {
		if (e1->value == e2->value) {
			return 0;
		} else if (e1->value < e2->value) {
			return -1;
		} else {
			return 1;
		}
	} else {
		return e1->set->compare(&e1->value, &e2->value, e1->set->compare_userdata);
	}
}

SPLAY_GENERATE(SetTree, SetNode, entry, nodecmp);

