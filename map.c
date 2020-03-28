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

#if HAVE_SYS_TREE
# include <sys/tree.h>
#endif
#include <stdlib.h>

#include "array.h"
#include "map.h"
#include "util.h"

struct MapNode {
	SPLAY_ENTRY(MapNode) entry;
	void *key;
	void *value;
	struct Map *map;
};

struct Map {
	SPLAY_HEAD(MapTree, MapNode) root;
	MapCompareFn compare;
	void *compare_userdata;
	void (*keyfree)(void *);
	void (*valuefree)(void *);
	size_t len;
};

struct MapIterator {
	struct MapNode *current;
};

static int nodecmp(struct MapNode *, struct MapNode *);
SPLAY_PROTOTYPE(MapTree, MapNode, entry, nodecmp);

struct Map *
map_new(MapCompareFn compare, void *compare_userdata, void *keyfree, void *valuefree)
{
	struct Map *map = xmalloc(sizeof(struct Map));
	SPLAY_INIT(&map->root);
	map->compare = compare;
	map->compare_userdata = compare_userdata;
	map->keyfree = keyfree;
	map->valuefree = valuefree;
	return map;
}

void
map_free(struct Map *map)
{
	if (map == NULL) {
		return;
	}

	map_truncate(map);
	free(map);
}

void
map_add(struct Map *map, void *key, void *value)
{
	if (!map_contains(map, key)) {
		struct MapNode *node = xmalloc(sizeof(struct MapNode));
		node->key = key;
		node->value = value;
		node->map = map;
		SPLAY_INSERT(MapTree, &map->root, node);
		map->len++;
	}
}

void
map_remove(struct Map *map, void *key)
{
	struct MapNode search;
	search.key = key;
	search.map = map;
	struct MapNode *node = SPLAY_FIND(MapTree, &map->root, &search);
	if (node != NULL) {
		SPLAY_REMOVE(MapTree, &map->root, node);
		if (map->keyfree != NULL) {
			map->keyfree(node->key);
		}
		if (map->valuefree != NULL) {
			map->valuefree(node->value);
		}
		free(node);
		map->len--;
	}
}

void *
map_get(struct Map *map, void *key)
{
	struct MapNode search;
	search.key = key;
	search.map = map;
	struct MapNode *result = SPLAY_FIND(MapTree, &map->root, &search);
	if (result != NULL) {
		return result->value;
	}
	return NULL;
}

int
map_contains(struct Map *map, void *key)
{
	return map_get(map, key) != NULL;
}

size_t
map_len(struct Map *map)
{
	return map->len;
}

void
map_truncate(struct Map *map)
{
	struct MapNode *node;
	struct MapNode *next;
	for (node = SPLAY_MIN(MapTree, &map->root); node != NULL; node = next) {
		next = SPLAY_NEXT(MapTree, &map->root, node);
		SPLAY_REMOVE(MapTree, &map->root, node);
		if (map->keyfree != NULL) {
			map->keyfree(node->key);
		}
		if (map->valuefree != NULL) {
			map->valuefree(node->value);
		}
		free(node);
	}
	SPLAY_INIT(&map->root);
	map->len = 0;
}

int
nodecmp(struct MapNode *e1, struct MapNode *e2)
{
	if (e1->map->compare == NULL) {
		if (e1->key == e2->key) {
			return 0;
		} else if (e1->key < e2->key) {
			return -1;
		} else {
			return 1;
		}
	} else {
		return e1->map->compare(&e1->key, &e2->key, e1->map->compare_userdata);
	}
}

SPLAY_GENERATE(MapTree, MapNode, entry, nodecmp);

struct MapIterator *
map_iterator(struct Map *map)
{
	struct MapIterator *iter = xmalloc(sizeof(struct MapIterator));
	iter->current = SPLAY_MIN(MapTree, &map->root);
	return iter;
}

void
map_iterator_free(struct MapIterator **iter)
{
	if (*iter != NULL) {
		free(*iter);
		*iter = NULL;
	}
}

void *
map_iterator_next(struct MapIterator **iter_, void **value)
{
	struct MapIterator *iter = *iter_;
	if (iter->current == NULL) {
		map_iterator_free(iter_);
		return NULL;
	}

	void *key = iter->current->key;
	*value = iter->current->value;
	iter->current = SPLAY_NEXT(MapTree, &(iter->current->map->root), iter->current);
	return key;
}
