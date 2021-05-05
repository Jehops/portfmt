/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2021 Tobias Kortkamp <tobik@FreeBSD.org>
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

#if HAVE_ERR
# include <err.h>
#endif
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "portscan/status.h"

static void portscan_status_signal_handler(int);

static enum PortscanState state = PORTSCAN_STATUS_START;
static struct timespec tic;
static unsigned int interval;
static atomic_int siginfo_requested = ATOMIC_VAR_INIT(0);
static atomic_size_t scanned = ATOMIC_VAR_INIT(0);
static size_t max_scanned;

void
portscan_status_init(unsigned int progress_interval)
{
	interval = progress_interval;
	clock_gettime(CLOCK_MONOTONIC, &tic);

#ifdef SIGINFO
	if (signal(SIGINFO, portscan_status_signal_handler)) {
		err(1, "signal");
	}
#endif
	if (signal(SIGUSR2, portscan_status_signal_handler)) {
		err(1, "signal");
	}
	if (interval) {
		if (signal(SIGALRM, portscan_status_signal_handler)) {
			err(1, "signal");
		}
		alarm(interval);
	}
}

void
portscan_status_inc()
{
	scanned++;
}

void
portscan_status_reset(enum PortscanState new_state, size_t max)
{
	state = new_state;
	scanned = 0;
	max_scanned = max;
	if (interval) {
		siginfo_requested = 1;
	}
}

void
portscan_status_print()
{
	int expected = 1;
	if (atomic_compare_exchange_strong(&siginfo_requested, &expected, 0)) {
		int percent = 0;
		if (max_scanned > 0) {
			percent = scanned * 100 / max_scanned;
		}
		struct timespec toc;
		clock_gettime(CLOCK_MONOTONIC, &toc);
		int seconds = (toc.tv_nsec - tic.tv_nsec) / 1000000000.0 + (toc.tv_sec  - tic.tv_sec);
		switch (state) {
		case PORTSCAN_STATUS_START:
			fprintf(stderr, "[  0%%] starting (%ds)\n", seconds);
			break;
		case PORTSCAN_STATUS_CATEGORIES:
			fprintf(stderr, "[%3d%%] scanning categories %zu/%zu (%ds)\n", percent, scanned, max_scanned, seconds);
			break;
		case PORTSCAN_STATUS_PORTS:
			fprintf(stderr, "[%3d%%] scanning ports %zu/%zu (%ds)\n", percent, scanned, max_scanned, seconds);
			break;
		case PORTSCAN_STATUS_RESULT:
			fprintf(stderr, "[%3d%%] compiling result %zu/%zu (%ds)\n", percent, scanned, max_scanned, seconds);
			break;
		case PORTSCAN_STATUS_FINISHED:
			fprintf(stderr, "[100%%] finished in %ds\n", seconds);
			break;
		default:
			abort();
		}
		if (interval) {
			alarm(interval);
		}
	}
}

void
portscan_status_signal_handler(int si)
{
	siginfo_requested = 1;
}
