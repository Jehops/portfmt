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

#include <assert.h>
#include <err.h>
#include <stdlib.h>

#include "conditional.h"
#include "rules.h"
#include "util.h"

struct Conditional {
	struct sbuf *name;
	struct Target *target;
};

struct Conditional *
conditional_new(struct sbuf *s, struct Target *target)
{
	struct Conditional *cond = malloc(sizeof(struct Conditional));
	if (cond == NULL) {
		err(1, "malloc");
	}

	cond->name = sbuf_dup(s);
	sbuf_finishx(cond->name);

	cond->target = target;

	return cond;
}

void
conditional_free(struct Conditional *cond)
{
	sbuf_delete(cond->name);
	free(cond);
}

struct sbuf *
conditional_name(struct Conditional *cond)
{
	return cond->name;
}

struct sbuf *
conditional_tostring(struct Conditional *cond)
{
	struct sbuf *buf;
	if (cond->target) {
		buf = sbuf_dupstr(NULL);
		sbuf_printf(buf, "%s/%s",
			    sbuf_data(target_name(cond->target)),
			    sbuf_data(cond->name));
	} else {
		buf = sbuf_dup(cond->name);
	}
	sbuf_finishx(buf);
	return buf;
}
