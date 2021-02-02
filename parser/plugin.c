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

#include "config.h"

#include <dirent.h>
#include <dlfcn.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "util.h"
#include "plugin.h"

#ifdef PORTFMT_STATIC
#undef PLUGIN
#define PLUGIN(name, f) \
	extern struct Array *f##_dispatch(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
#include "parser/plugin_registry.h"
static struct ParserPluginInfo plugins[] = {
#undef PLUGIN
#define PLUGIN(name, f) { 0, name, f##_dispatch },
#include "parser/plugin_registry.h"
};
static size_t plugins_len = nitems(plugins);

void parser_plugin_load_all()
{}

struct ParserPluginInfo *
parser_plugin_info(const char *plugin)
{
	for (size_t i = 0; i < plugins_len; i++) {
		if (strcmp(plugin, plugins[i].name) == 0) {
			return &plugins[i];
		}
	}

	return NULL;
}

#else

static struct ParserPluginInfo *plugins[256];
static size_t plugins_len = 0;

static void
parser_plugin_load(const char *filename)
{
	void *handle = dlopen(filename, RTLD_LAZY);
	if (handle == NULL) {
		errx(1, "dlopen: %s: %s", filename, dlerror());
	}
	void (*register_plugin)(void) = dlsym(handle, "register_plugin");
	if (register_plugin == NULL) {
		dlclose(handle);
		return;
	}
	register_plugin();
}

void
parser_plugin_load_all()
{
	const char *plugin_dir;
	if ((plugin_dir = getenv("PORTFMT_PLUGIN_PATH")) == NULL) {
		plugin_dir = PORTFMT_PLUGIN_PATH;
	}

	DIR *dir = opendir(plugin_dir);
	if (dir == NULL) {
		err(1, "opendir: %s", plugin_dir);
	}

	struct dirent *d;
	while ((d = readdir(dir)) != NULL) {
		if (str_startswith(d->d_name, PORTFMT_PLUGIN_PREFIX) &&
		    str_endswith(d->d_name, PORTFMT_PLUGIN_SUFFIX)) {
			char *filename;
			xasprintf(&filename, "%s/%s", plugin_dir, d->d_name);
			parser_plugin_load(filename);
			free(filename);
		}
	}
	closedir(dir);
}

struct ParserPluginInfo *
parser_plugin_info(const char *plugin)
{
	for (size_t i = 0; i < plugins_len; i++) {
		if (strcmp(plugin, plugins[i]->name) == 0) {
			return plugins[i];
		}
	}

	return NULL;
}

void
parser_plugin_register(struct ParserPluginInfo *info)
{
	if (info == NULL) {
		return;
	}

	if (plugins_len >= nitems(plugins)) {
		abort();
	}

	for (size_t i = 0; i < plugins_len; i++) {
		if (strcmp(info->name, plugins[i]->name) == 0) {
			return;
		}
	}

	if (info->version == 0) {
		plugins[plugins_len++] = info;
	}
}
#endif
