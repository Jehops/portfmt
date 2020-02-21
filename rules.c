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

#include <sys/param.h>
#include <assert.h>
#include <ctype.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <math.h>
#include <regex.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "array.h"
#include "conditional.h"
#include "regexp.h"
#include "rules.h"
#include "parser.h"
#include "set.h"
#include "token.h"
#include "util.h"
#include "variable.h"

static int case_sensitive_sort(struct Parser *, struct Variable *);
static int compare_rel(const char *[], size_t, const char *, const char *);
static int compare_license_perms(struct Variable *, const char *, const char *, int *);
static int compare_plist_files(struct Parser *, struct Variable *, const char *, const char *, int *);
static int compare_use_pyqt(struct Variable *, const char *, const char *, int *);
static int compare_use_qt(struct Variable *, const char *, const char *, int *);
static char *extract_subpkg(struct Parser *, const char *, char **);
static int is_flavors_helper(struct Parser *, const char *, char **, char **);
static void target_extract_opt(struct Parser *, const char *, char **, char **, int *);
static int variable_has_flag(struct Parser *, const char *, int);

static struct {
	const char *pattern;
	int flags;
	regex_t re;
} regular_expressions[] = {
	[RE_CONDITIONAL]      = { "^(include|\\.[[:space:]]*(error|export|export-env|"
				  "export\\.env|export-literal|info|undef|unexport|for|endfor|"
				  "unexport-env|warning|if|ifdef|ifndef|include|"
				  "ifmake|ifnmake|else|elif|elifdef|elifndef|"
				  "elifmake|endif|sinclude))([[:space:]]*|$|\\(|!)",
				  REG_EXTENDED, {} },
	[RE_LICENSE_NAME]     = { "^(_?(-|LICENSE_NAME_[A-Za-z0-9._+ ])+|"
				  "^LICENSE_(FILE|NAME)_|"
				  "^LICENSE_(NAME|TEXT)$|"
				  "_?(-|LICENSE_TEXT_[A-Za-z0-9._+ ])+$)",
				  REG_EXTENDED, {} },
	[RE_LICENSE_PERMS]    = { "^(_?LICENSE_PERMS_(-|[A-Z0-9\\._+ ])+|"
				  "_LICENSE_LIST_PERMS|LICENSE_PERMS)",
				  REG_EXTENDED, {} },
	[RE_OPTIONS_GROUP]    = { "^_?OPTIONS_(GROUP|MULTI|RADIO|SINGLE)_([-_[:upper:][:digit:]]+)",
				  REG_EXTENDED, {} },
	[RE_PLIST_KEYWORDS]   = { "^\"@([a-z]|-)+ ",			      REG_EXTENDED, {} },
};

static const char *license_perms_rel[] = {
	"dist-mirror",
	"no-dist-mirror",
	"dist-sell",
	"no-dist-sell",
	"pkg-mirror",
	"no-pkg-mirror",
	"pkg-sell",
	"no-pkg-sell",
	"auto-accept",
	"no-auto-accept",
	"none",
};

static const char *use_qt_rel[] = {
	"3d",
	"assistant",
	"buildtools",
	"canvas3d",
	"charts",
	"concurrent",
	"connectivity",
	"core",
	"datavis3d",
	"dbus",
	"declarative",
	"designer",
	"diag",
	"doc",
	"examples",
	"gamepad",
	"graphicaleffects",
	"gui",
	"help",
	"imageformats",
	"l1x++n",
	"linguist",
	"linguisttools",
	"location",
	"multimedia",
	"network",
	"networkauth",
	"opengl",
	"paths",
	"phonon4",
	"pixeltool",
	"plugininfo",
	"printsupport",
	"qdbus",
	"qdbusviewer",
	"qdoc-data",
	"qdoc",
	"qev",
	"qmake",
	"quickcontrols",
	"quickcontrols2",
	"remoteobjects",
	"script",
	"scripttools",
	"scxml",
	"sensors",
	"serialbus",
	"serialport",
	"speech",
	"sql-ibase",
	"sql-mysql",
	"sql-odbc",
	"sql-pgsql",
	"sql-sqlite2",
	"sql-sqlite3",
	"sql-tds",
	"sql",
	"svg",
	"testlib",
	"uiplugin",
	"uitools",
	"virtualkeyboard",
	"wayland",
	"webchannel",
	"webengine",
	"webglplugin",
	"webkit",
	"websockets-qml",
	"websockets",
	"webview",
	"widgets",
	"x11extras",
	"xml",
	"xmlpatterns",

	// _run variants of the above
	"3d_run",
	"assistant_run",
	"buildtools_run",
	"canvas3d_run",
	"charts_run",
	"concurrent_run",
	"connectivity_run",
	"core_run",
	"datavis3d_run",
	"dbus_run",
	"declarative_run",
	"designer_run",
	"diag_run",
	"doc_run",
	"examples_run",
	"gamepad_run",
	"graphicaleffects_run",
	"gui_run",
	"help_run",
	"imageformats_run",
	"l1x++n_run",
	"linguist_run",
	"linguisttools_run",
	"location_run",
	"multimedia_run",
	"network_run",
	"networkauth_run",
	"opengl_run",
	"paths_run",
	"phonon4_run",
	"pixeltool_run",
	"plugininfo_run",
	"printsupport_run",
	"qdbus_run",
	"qdbusviewer_run",
	"qdoc-data_run",
	"qdoc_run",
	"qev_run",
	"qmake_run",
	"quickcontrols_run",
	"quickcontrols2_run",
	"remoteobjects_run",
	"script_run",
	"scripttools_run",
	"scxml_run",
	"sensors_run",
	"serialbus_run",
	"serialport_run",
	"speech_run",
	"sql-ibase_run",
	"sql-mysql_run",
	"sql-odbc_run",
	"sql-pgsql_run",
	"sql-sqlite2_run",
	"sql-sqlite3_run",
	"sql-tds_run",
	"sql_run",
	"svg_run",
	"testlib_run",
	"uiplugin_run",
	"uitools_run",
	"virtualkeyboard_run",
	"wayland_run",
	"webchannel_run",
	"webengine_run",
	"webglplugin_run",
	"webkit_run",
	"websockets-qml_run",
	"websockets_run",
	"webview_run",
	"widgets_run",
	"x11extras_run",
	"xml_run",
	"xmlpatterns_run",

	// _build variants of the above
	"3d_build",
	"assistant_build",
	"buildtools_build",
	"canvas3d_build",
	"charts_build",
	"concurrent_build",
	"connectivity_build",
	"core_build",
	"datavis3d_build",
	"dbus_build",
	"declarative_build",
	"designer_build",
	"diag_build",
	"doc_build",
	"examples_build",
	"gamepad_build",
	"graphicaleffects_build",
	"gui_build",
	"help_build",
	"imageformats_build",
	"l1x++n_build",
	"linguist_build",
	"linguisttools_build",
	"location_build",
	"multimedia_build",
	"network_build",
	"networkauth_build",
	"opengl_build",
	"paths_build",
	"phonon4_build",
	"pixeltool_build",
	"plugininfo_build",
	"printsupport_build",
	"qdbus_build",
	"qdbusviewer_build",
	"qdoc-data_build",
	"qdoc_build",
	"qev_build",
	"qmake_build",
	"quickcontrols_build",
	"quickcontrols2_build",
	"remoteobjects_build",
	"script_build",
	"scripttools_build",
	"scxml_build",
	"sensors_build",
	"serialbus_build",
	"serialport_build",
	"speech_build",
	"sql-ibase_build",
	"sql-mysql_build",
	"sql-odbc_build",
	"sql-pgsql_build",
	"sql-sqlite2_build",
	"sql-sqlite3_build",
	"sql-tds_build",
	"sql_build",
	"svg_build",
	"testlib_build",
	"uiplugin_build",
	"uitools_build",
	"virtualkeyboard_build",
	"wayland_build",
	"webchannel_build",
	"webengine_build",
	"webkit_build",
	"webglplugin_build",
	"websockets-qml_build",
	"websockets_build",
	"webview_build",
	"widgets_build",
	"x11extras_build",
	"xml_build",
	"xmlpatterns_build",
};

static const char *use_pyqt_rel[] = {
	"core",
	"dbus",
	"dbussupport",
	"demo",
	"designer",
	"designerplugin",
	"gui",
	"help",
	"multimedia",
	"multimediawidgets",
	"network",
	"opengl",
	"printsupport",
	"qml",
	"qscintilla2",
	"quickwidgets",
	"serialport",
	"sip",
	"sql",
	"svg",
	"test",
	"webchannel",
	"webengine",
	"webkit",
	"webkitwidgets",
	"websockets",
	"widgets",
	"xml",
	"xmlpatterns",

	// _build variants of the above
	"core_build",
	"dbus_build",
	"dbussupport_build",
	"demo_build",
	"designer_build",
	"designerplugin_build",
	"gui_build",
	"help_build",
	"multimedia_build",
	"multimediawidgets_build",
	"network_build",
	"opengl_build",
	"printsupport_build",
	"qml_build",
	"qscintilla2_build",
	"quickwidgets_build",
	"serialport_build",
	"sip_build",
	"sql_build",
	"svg_build",
	"test_build",
	"webchannel_build",
	"webengine_build",
	"webkit_build",
	"webkitwidgets_build",
	"websockets_build",
	"widgets_build",
	"xml_build",
	"xmlpatterns_build",

	// _run variants of the above
	"core_run",
	"dbus_run",
	"dbussupport_run",
	"demo_run",
	"designer_run",
	"designerplugin_run",
	"gui_run",
	"help_run",
	"multimedia_run",
	"multimediawidgets_run",
	"network_run",
	"opengl_run",
	"printsupport_run",
	"qml_run",
	"qscintilla2_run",
	"quickwidgets_run",
	"serialport_run",
	"sip_run",
	"sql_run",
	"svg_run",
	"test_run",
	"webchannel_run",
	"webengine_run",
	"webkit_run",
	"webkitwidgets_run",
	"websockets_run",
	"widgets_run",
	"xml_run",
	"xmlpatterns_run",

	// _test variants of the above
	"core_test",
	"dbus_test",
	"dbussupport_test",
	"demo_test",
	"designer_test",
	"designerplugin_test",
	"gui_test",
	"help_test",
	"multimedia_test",
	"multimediawidgets_test",
	"network_test",
	"opengl_test",
	"printsupport_test",
	"qml_test",
	"qscintilla2_test",
	"quickwidgets_test",
	"serialport_test",
	"sip_test",
	"sql_test",
	"svg_test",
	"test_test",
	"webchannel_test",
	"webengine_test",
	"webkit_test",
	"webkitwidgets_test",
	"websockets_test",
	"widgets_test",
	"xml_test",
	"xmlpatterns_test",
};

static const char *target_command_wrap_after_each_token_[] = {
	"${INSTALL_DATA}",
	"${INSTALL_LIB}",
	"${INSTALL_MAN}",
	"${INSTALL_PROGRAM}",
	"${INSTALL_SCRIPT}",
	"${INSTALL}",
	"${MKDIR}",
	"${MV}",
	"${REINPLACE_CMD}",
	"${RMDIR}",
	"${SED}",
	"${STRIP_CMD}",
};

static const char *target_order_[] = {
	"post-chroot",
	"pre-everything",
	"pre-fetch",
	"pre-fetch-script",
	"do-fetch",
	"post-fetch",
	"post-fetch-script",
	"pre-extract",
	"pre-extract-script",
	"do-extract",
	"post-extract",
	"post-extract-script",
	"pre-patch",
	"pre-patch-script",
	"do-patch",
	"post-patch",
	"post-patch-script",
	"pre-configure",
	"pre-configure-script",
	"do-configure",
	"post-configure",
	"post-configure-script",
	"pre-build",
	"pre-build-script",
	"do-build",
	"post-build",
	"post-build-script",
	"pre-install",
	"pre-su-install",
	"do-install",
	"post-install",
	"post-install-script",
	"post-stage",
	"pre-test",
	"do-test",
	"post-test",
	"pre-package",
	"do-package",
	"post-package",
	"makesum",
};

static const char *special_targets_[] = {
	".BEGIN",
	".DEFAULT",
	".DELETE_ON_ERROR",
	".END",
	".ERROR",
	".EXEC",
	".IGNORE",
	".INTERRUPT",
	".MADE",
	".MAIN",
	".MAKE",
	".MAKEFLAGS",
	".META",
	".NO_PARALLEL",
	".NOMAIN",
	".NOMETA_CMP",
	".NOMETA",
	".NOPATH",
	".NOTPARALLEL",
	".OBJDIR",
	".OPTIONAL",
	".ORDER",
	".PATH",
	".PHONY",
	".PRECIOUS",
	".RECURSIVE",
	".SHELL",
	".SILENT",
	".STALE",
	".SUFFIXES",
	".USE",
	".USEBEFORE",
	".WAIT",
};

static const char *static_flavors_[] = {
	// lazarus
	"gtk2",
	"qt5",
	// php
	"php71",
	"php72",
	"php73",
	"php74",
	// python
	"py27",
	"py35",
	"py36",
	"py37",
	"py38",
};

enum VariableOrderFlag {
	VAR_DEFAULT = 0,
	VAR_CASE_SENSITIVE_SORT = 1,
	// Lines that are best not wrapped to 80 columns
	VAR_IGNORE_WRAPCOL = 2,
	VAR_LEAVE_UNFORMATTED = 4,
	// Sanitize whitespace but do *not* sort tokens; more complicated
	// patterns below in leave_unsorted()
	VAR_LEAVE_UNSORTED = 8,
	VAR_PRINT_AS_NEWLINES = 16,
	// Do not indent with the rest of the variables in a paragraph
	VAR_SKIP_GOALCOL = 32,
	VAR_SUBPKG_HELPER = 64,
};

struct VariableOrderEntry {
	enum BlockType block;
	const char *var;
	enum VariableOrderFlag flags;
};
#define VAR_FOR_EACH_ARCH(block, var, flags) \
	{ block, var "aarch64", flags }, \
	{ block, var "amd64", flags }, \
	{ block, var "arm", flags }, \
	{ block, var "armv6", flags }, \
	{ block, var "armv7", flags }, \
	{ block, var "i386", flags }, \
	{ block, var "mips", flags }, \
	{ block, var "mips64", flags }, \
	{ block, var "mips64el", flags }, \
	{ block, var "mips64elhf", flags }, \
	{ block, var "mips64hf", flags }, \
	{ block, var "mipsel", flags }, \
	{ block, var "mipselhf", flags }, \
	{ block, var "mipsn32", flags }, \
	{ block, var "powerpc", flags }, \
	{ block, var "powerpc64", flags }, \
	{ block, var "powerpcspe", flags }, \
	{ block, var "riscv64", flags }, \
	{ block, var "sparc64", flags }
#define VAR_FOR_EACH_FREEBSD_VERSION_AND_ARCH(block, var, flags) \
	{ block, var "FreeBSD", flags }, \
	{ block, var "FreeBSD_11", flags }, \
	VAR_FOR_EACH_ARCH(block, var "FreeBSD_11_", flags), \
	{ block, var "FreeBSD_12", flags }, \
	VAR_FOR_EACH_ARCH(block, var "FreeBSD_12_", flags), \
	{ block, var "FreeBSD_13", flags }, \
	VAR_FOR_EACH_ARCH(block, var "FreeBSD_13_", flags), \
	VAR_FOR_EACH_ARCH(block, var "FreeBSD_", flags)
#define VAR_FOR_EACH_FREEBSD_VERSION(block, var, flags) \
	{ block, var "FreeBSD", flags }, \
	{ block, var "FreeBSD_11", flags }, \
	{ block, var "FreeBSD_12", flags }, \
	{ block, var "FreeBSD_13", flags }
#define VAR_FOR_EACH_SSL(block, var, flags) \
	{ block, var "base", flags}, \
	{ block, var "libressl", flags}, \
	{ block, var "libressl-devel", flags}, \
	{ block, var "openssl", flags}
// Based on: https://www.freebsd.org/doc/en/books/porters-handbook/porting-order.html
static struct VariableOrderEntry variable_order_[] = {
	{ BLOCK_PORTNAME, "PORTNAME", VAR_DEFAULT },
	{ BLOCK_PORTNAME, "PORTVERSION", VAR_DEFAULT },
	{ BLOCK_PORTNAME, "DISTVERSIONPREFIX", VAR_SKIP_GOALCOL },
	{ BLOCK_PORTNAME, "DISTVERSION", VAR_DEFAULT },
	{ BLOCK_PORTNAME, "DISTVERSIONSUFFIX", VAR_SKIP_GOALCOL },
	/* XXX: hack to fix inserting PORTREVISION in aspell ports */
	{ BLOCK_PORTNAME, "SPELLVERSION", VAR_DEFAULT },
	{ BLOCK_PORTNAME, "PORTREVISION", VAR_DEFAULT },
	{ BLOCK_PORTNAME, "PORTEPOCH", VAR_DEFAULT },
	{ BLOCK_PORTNAME, "CATEGORIES", VAR_LEAVE_UNSORTED },
	{ BLOCK_PORTNAME, "MASTER_SITES", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_PORTNAME, "MASTER_SITE_SUBDIR", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL },
	{ BLOCK_PORTNAME, "PKGNAMEPREFIX", VAR_DEFAULT },
	{ BLOCK_PORTNAME, "PKGNAMESUFFIX", VAR_DEFAULT },
	{ BLOCK_PORTNAME, "DISTNAME", VAR_DEFAULT },
	{ BLOCK_PORTNAME, "EXTRACT_SUFX", VAR_DEFAULT },
	{ BLOCK_PORTNAME, "DISTFILES", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED |  VAR_PRINT_AS_NEWLINES },
	VAR_FOR_EACH_ARCH(BLOCK_PORTNAME, "DISTFILES_", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL),
	{ BLOCK_PORTNAME, "DIST_SUBDIR", VAR_DEFAULT },
	{ BLOCK_PORTNAME, "EXTRACT_ONLY", VAR_DEFAULT },
	{ BLOCK_PORTNAME, "EXTRACT_ONLY_7z", VAR_SKIP_GOALCOL },

	{ BLOCK_PATCHFILES, "PATCH_SITES", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_PATCHFILES, "PATCHFILES", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_PATCHFILES, "PATCH_DIST_STRIP", VAR_DEFAULT },

	{ BLOCK_MAINTAINER, "MAINTAINER", VAR_IGNORE_WRAPCOL },
	{ BLOCK_MAINTAINER, "COMMENT", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_SUBPKG_HELPER },

	{ BLOCK_LICENSE, "LICENSE", VAR_SKIP_GOALCOL },
	{ BLOCK_LICENSE, "LICENSE_COMB", VAR_SKIP_GOALCOL },
	{ BLOCK_LICENSE, "LICENSE_GROUPS", VAR_SKIP_GOALCOL },
	{ BLOCK_LICENSE, "LICENSE_NAME", VAR_LEAVE_UNSORTED | VAR_SKIP_GOALCOL },
	{ BLOCK_LICENSE, "LICENSE_TEXT", VAR_LEAVE_UNSORTED | VAR_SKIP_GOALCOL },
	{ BLOCK_LICENSE, "LICENSE_FILE", VAR_SKIP_GOALCOL },
	{ BLOCK_LICENSE, "LICENSE_PERMS", VAR_SKIP_GOALCOL },
	{ BLOCK_LICENSE, "LICENSE_DISTFILES", VAR_SKIP_GOALCOL },

	{ BLOCK_LICENSE_OLD, "RESTRICTED", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED },
	{ BLOCK_LICENSE_OLD, "RESTRICTED_FILES", VAR_DEFAULT },
	{ BLOCK_LICENSE_OLD, "NO_CDROM", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED },
	{ BLOCK_LICENSE_OLD, "NO_PACKAGE", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED },
	{ BLOCK_LICENSE_OLD, "LEGAL_PACKAGE", VAR_LEAVE_UNSORTED },
	{ BLOCK_LICENSE_OLD, "LEGAL_TEXT", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED },

	{ BLOCK_BROKEN, "DEPRECATED", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED },
	{ BLOCK_BROKEN, "EXPIRATION_DATE", VAR_SKIP_GOALCOL },
	{ BLOCK_BROKEN, "FORBIDDEN", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED },
	{ BLOCK_BROKEN, "MANUAL_PACKAGE_BUILD", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_SKIP_GOALCOL },

	{ BLOCK_BROKEN, "BROKEN", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED },
	VAR_FOR_EACH_ARCH(BLOCK_BROKEN, "BROKEN_", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_SKIP_GOALCOL),
	{ BLOCK_BROKEN, "BROKEN_DragonFly", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_SKIP_GOALCOL },
	VAR_FOR_EACH_FREEBSD_VERSION_AND_ARCH(BLOCK_BROKEN, "BROKEN_", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_SKIP_GOALCOL),
	{ BLOCK_BROKEN, "IGNORE", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL | VAR_LEAVE_UNSORTED },
	VAR_FOR_EACH_ARCH(BLOCK_BROKEN, "IGNORE_", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL | VAR_LEAVE_UNSORTED),
	{ BLOCK_BROKEN, "IGNORE_DragonFly", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL | VAR_LEAVE_UNSORTED },
	VAR_FOR_EACH_FREEBSD_VERSION_AND_ARCH(BLOCK_BROKEN, "IGNORE_", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_SKIP_GOALCOL),
	{ BLOCK_BROKEN, "ONLY_FOR_ARCHS", VAR_SKIP_GOALCOL },
	{ BLOCK_BROKEN, "ONLY_FOR_ARCHS_REASON", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL | VAR_LEAVE_UNSORTED },
	VAR_FOR_EACH_ARCH(BLOCK_BROKEN, "ONLY_FOR_ARCHS_REASON_", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL | VAR_LEAVE_UNSORTED),
	{ BLOCK_BROKEN, "NOT_FOR_ARCHS", VAR_SKIP_GOALCOL },
	{ BLOCK_BROKEN, "NOT_FOR_ARCHS_REASON", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL | VAR_LEAVE_UNSORTED },
	VAR_FOR_EACH_ARCH(BLOCK_BROKEN, "NOT_FOR_ARCHS_REASON_", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL | VAR_LEAVE_UNSORTED),

	{ BLOCK_DEPENDS, "FETCH_DEPENDS", VAR_PRINT_AS_NEWLINES },
	VAR_FOR_EACH_ARCH(BLOCK_DEPENDS, "FETCH_DEPENDS_", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL),
	{ BLOCK_DEPENDS, "EXTRACT_DEPENDS", VAR_PRINT_AS_NEWLINES },
	VAR_FOR_EACH_ARCH(BLOCK_DEPENDS, "EXTRACT_DEPENDS_", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL),
	{ BLOCK_DEPENDS, "PATCH_DEPENDS", VAR_PRINT_AS_NEWLINES },
	VAR_FOR_EACH_ARCH(BLOCK_DEPENDS, "PATCH_DEPENDS_", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL),
	{ BLOCK_DEPENDS, "CRAN_DEPENDS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_DEPENDS, "BUILD_DEPENDS", VAR_PRINT_AS_NEWLINES },
	VAR_FOR_EACH_ARCH(BLOCK_DEPENDS, "BUILD_DEPENDS_", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL),
	{ BLOCK_DEPENDS, "LIB_DEPENDS", VAR_PRINT_AS_NEWLINES },
	VAR_FOR_EACH_ARCH(BLOCK_DEPENDS, "LIB_DEPENDS_", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL),
	{ BLOCK_DEPENDS, "RUN_DEPENDS", VAR_PRINT_AS_NEWLINES },
	VAR_FOR_EACH_ARCH(BLOCK_DEPENDS, "RUN_DEPENDS_", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL),
	{ BLOCK_DEPENDS, "TEST_DEPENDS", VAR_PRINT_AS_NEWLINES },
	VAR_FOR_EACH_ARCH(BLOCK_DEPENDS, "TEST_DEPENDS_", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL),
#if PORTFMT_SUBPACKAGES
	{ BLOCK_DEPENDS, "SELF_DEPENDS", VAR_SUBPKG_HELPER },
#endif

	{ BLOCK_FLAVORS, "FLAVORS", VAR_LEAVE_UNSORTED },
	{ BLOCK_FLAVORS, "FLAVOR", VAR_DEFAULT },

#if PORTFMT_SUBPACKAGES
	{ BLOCK_SUBPACKAGES, "SUBPACKAGES", VAR_DEFAULT },
#endif

	{ BLOCK_FLAVORS_HELPER, "PKGNAMEPREFIX", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_FLAVORS_HELPER, "PKGNAMESUFFIX", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_FLAVORS_HELPER, "PKG_DEPENDS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_FLAVORS_HELPER, "EXTRACT_DEPENDS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_FLAVORS_HELPER, "PATCH_DEPENDS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_FLAVORS_HELPER, "FETCH_DEPENDS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_FLAVORS_HELPER, "BUILD_DEPENDS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_FLAVORS_HELPER, "LIB_DEPENDS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_FLAVORS_HELPER, "RUN_DEPENDS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_FLAVORS_HELPER, "TEST_DEPENDS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_FLAVORS_HELPER, "CONFLICTS", VAR_DEFAULT },
	{ BLOCK_FLAVORS_HELPER, "CONFLICTS_BUILD", VAR_DEFAULT },
	{ BLOCK_FLAVORS_HELPER, "CONFLICTS_INSTALL", VAR_DEFAULT },
	{ BLOCK_FLAVORS_HELPER, "DESCR", VAR_DEFAULT },
	{ BLOCK_FLAVORS_HELPER, "PLIST", VAR_DEFAULT },

	{ BLOCK_USES, "USES", VAR_DEFAULT },
	{ BLOCK_USES, "BROKEN_SSL", VAR_IGNORE_WRAPCOL },
	{ BLOCK_USES, "BROKEN_SSL_REASON", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL | VAR_LEAVE_UNSORTED },
	VAR_FOR_EACH_SSL(BLOCK_USES, "BROKEN_SSL_REASON_", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL | VAR_LEAVE_UNSORTED),
	{ BLOCK_USES, "IGNORE_SSL", VAR_IGNORE_WRAPCOL },
	{ BLOCK_USES, "IGNORE_SSL_REASON", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL | VAR_LEAVE_UNSORTED },
	VAR_FOR_EACH_SSL(BLOCK_USES, "IGNORE_SSL_REASON_", VAR_IGNORE_WRAPCOL | VAR_SKIP_GOALCOL | VAR_LEAVE_UNSORTED),
	{ BLOCK_USES, "IGNORE_WITH_MYSQL", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "CPE_PART", VAR_DEFAULT },
	{ BLOCK_USES, "CPE_VENDOR", VAR_DEFAULT },
	{ BLOCK_USES, "CPE_PRODUCT", VAR_DEFAULT },
	{ BLOCK_USES, "CPE_VERSION", VAR_DEFAULT },
	{ BLOCK_USES, "CPE_UPDATE", VAR_DEFAULT },
	{ BLOCK_USES, "CPE_EDITION", VAR_DEFAULT },
	{ BLOCK_USES, "CPE_LANG", VAR_DEFAULT },
	{ BLOCK_USES, "CPE_SW_EDITION", VAR_DEFAULT },
	{ BLOCK_USES, "CPE_TARGET_SW", VAR_DEFAULT },
	{ BLOCK_USES, "CPE_TARGET_HW", VAR_DEFAULT },
	{ BLOCK_USES, "CPE_OTHER", VAR_DEFAULT },
	{ BLOCK_USES, "DOS2UNIX_REGEX", VAR_DEFAULT },
	{ BLOCK_USES, "DOS2UNIX_FILES", VAR_DEFAULT },
	{ BLOCK_USES, "DOS2UNIX_GLOB", VAR_DEFAULT },
	{ BLOCK_USES, "DOS2UNIX_WRKSRC", VAR_DEFAULT },
	{ BLOCK_USES, "FONTNAME", VAR_DEFAULT },
	{ BLOCK_USES, "FONTSDIR", VAR_DEFAULT },
	{ BLOCK_USES, "FONTPATHD", VAR_DEFAULT },
	{ BLOCK_USES, "FONTPATHSPEC", VAR_DEFAULT },
	{ BLOCK_USES, "KMODDIR", VAR_DEFAULT },
	{ BLOCK_USES, "KERN_DEBUGDIR", VAR_DEFAULT },
	{ BLOCK_USES, "NCURSES_IMPL", VAR_DEFAULT },
	{ BLOCK_USES, "PATHFIX_CMAKELISTSTXT", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "PATHFIX_MAKEFILEIN", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "PATHFIX_WRKSRC", VAR_DEFAULT },
	{ BLOCK_USES, "QMAIL_PREFIX", VAR_DEFAULT },
	{ BLOCK_USES, "QMAIL_SLAVEPORT", VAR_DEFAULT },
	{ BLOCK_USES, "WANT_PGSQL", VAR_DEFAULT },
	{ BLOCK_USES, "USE_ANT", VAR_DEFAULT },
	{ BLOCK_USES, "USE_ASDF", VAR_DEFAULT },
	{ BLOCK_USES, "USE_ASDF_FASL", VAR_DEFAULT },
	{ BLOCK_USES, "FASL_BUILD", VAR_DEFAULT },
	{ BLOCK_USES, "ASDF_MODULES", VAR_DEFAULT },
	{ BLOCK_USES, "USE_BINUTILS", VAR_DEFAULT },
	{ BLOCK_USES, "USE_CLISP", VAR_DEFAULT },
	{ BLOCK_USES, "USE_CSTD", VAR_DEFAULT },
	{ BLOCK_USES, "USE_CXXSTD", VAR_DEFAULT },
	{ BLOCK_USES, "USE_FPC", VAR_DEFAULT },
	{ BLOCK_USES, "USE_GCC", VAR_DEFAULT },
	{ BLOCK_USES, "USE_GECKO", VAR_DEFAULT },
	{ BLOCK_USES, "USE_GENERIC_PKGMESSAGE", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_GITHUB", VAR_DEFAULT },
	{ BLOCK_USES, "GH_ACCOUNT", VAR_DEFAULT },
	{ BLOCK_USES, "GH_PROJECT", VAR_DEFAULT },
	{ BLOCK_USES, "GH_SUBDIR", VAR_DEFAULT },
	{ BLOCK_USES, "GH_TAGNAME", VAR_DEFAULT },
	{ BLOCK_USES, "GH_TUPLE", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_USES, "USE_GITLAB", VAR_DEFAULT },
	{ BLOCK_USES, "GL_SITE", VAR_DEFAULT },
	{ BLOCK_USES, "GL_ACCOUNT", VAR_DEFAULT },
	{ BLOCK_USES, "GL_PROJECT", VAR_DEFAULT },
	{ BLOCK_USES, "GL_COMMIT", VAR_DEFAULT },
	{ BLOCK_USES, "GL_SUBDIR", VAR_DEFAULT },
	{ BLOCK_USES, "GL_TUPLE", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_USES, "USE_GL", VAR_DEFAULT },
	{ BLOCK_USES, "USE_GNOME", VAR_DEFAULT },
	{ BLOCK_USES, "USE_GNOME_SUBR", VAR_DEFAULT },
	{ BLOCK_USES, "GCONF_SCHEMAS", VAR_DEFAULT },
	{ BLOCK_USES, "GLIB_SCHEMAS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_USES, "GNOME_LOCALSTATEDIR", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "INSTALLS_ICONS", VAR_DEFAULT },
	{ BLOCK_USES, "INSTALLS_OMF", VAR_DEFAULT },
	{ BLOCK_USES, "USE_GNUSTEP", VAR_DEFAULT },
	{ BLOCK_USES, "GNUSTEP_PREFIX", VAR_DEFAULT },
	{ BLOCK_USES, "DEFAULT_LIBVERSION", VAR_DEFAULT },
	{ BLOCK_USES, "ADDITIONAL_CFLAGS", VAR_DEFAULT },
	{ BLOCK_USES, "ADDITIONAL_CPPFLAGS", VAR_DEFAULT },
	{ BLOCK_USES, "ADDITIONAL_CXXFLAGS", VAR_DEFAULT },
	{ BLOCK_USES, "ADDITIONAL_OBJCCFLAGS", VAR_DEFAULT },
	{ BLOCK_USES, "ADDITIONAL_OBJCFLAGS", VAR_DEFAULT },
	{ BLOCK_USES, "ADDITIONAL_LDFLAGS", VAR_DEFAULT },
	{ BLOCK_USES, "ADDITIONAL_FLAGS", VAR_DEFAULT },
	{ BLOCK_USES, "ADDITIONAL_INCLUDE_DIRS", VAR_DEFAULT },
	{ BLOCK_USES, "ADDITIONAL_LIB_DIRS", VAR_DEFAULT },
	{ BLOCK_USES, "USE_GSTREAMER", VAR_DEFAULT },
	{ BLOCK_USES, "USE_GSTREAMER1", VAR_DEFAULT },
	{ BLOCK_USES, "USE_HORDE_BUILD", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_HORDE_RUN", VAR_DEFAULT },
	{ BLOCK_USES, "HORDE_DIR", VAR_DEFAULT },
	{ BLOCK_USES, "USE_JAVA", VAR_DEFAULT },
	{ BLOCK_USES, "JAVA_VERSION", VAR_DEFAULT },
	{ BLOCK_USES, "JAVA_OS", VAR_DEFAULT },
	{ BLOCK_USES, "JAVA_VENDOR", VAR_DEFAULT },
	{ BLOCK_USES, "JAVA_EXTRACT", VAR_DEFAULT },
	{ BLOCK_USES, "JAVA_BUILD", VAR_DEFAULT },
	{ BLOCK_USES, "JAVA_RUN", VAR_DEFAULT },
	{ BLOCK_USES, "USE_KDE", VAR_DEFAULT },
	{ BLOCK_USES, "KDE_PLASMA_VERSION", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "KDE_PLASMA_BRANCH", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "KDE_FRAMEWORKS_VERSION", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "KDE_FRAMEWORKS_BRANCH", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "KDE_APPLICATIONS_VERSION", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "KDE_APPLICATIONS_SHLIB_VER", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "KDE_APPLICATIONS_BRANCH", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "CALLIGRA_VERSION", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "CALLIGRA_BRANCH", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_LDCONFIG", VAR_DEFAULT },
	{ BLOCK_USES, "USE_LDCONFIG32", VAR_DEFAULT },
	{ BLOCK_USES, "USE_LINUX", VAR_DEFAULT },
	{ BLOCK_USES, "USE_LINUX_PREFIX", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_LINUX_RPM", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_LINUX_RPM_BAD_PERMS", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_LOCALE", VAR_DEFAULT },
	{ BLOCK_USES, "USE_LXQT", VAR_DEFAULT },
	{ BLOCK_USES, "USE_MATE", VAR_DEFAULT },
	{ BLOCK_USES, "USE_MOZILLA", VAR_DEFAULT },
	{ BLOCK_USES, "USE_MYSQL", VAR_DEFAULT },
	{ BLOCK_USES, "USE_OCAML", VAR_DEFAULT },
	{ BLOCK_USES, "NO_OCAML_BUILDDEPENDS", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "NO_OCAML_RUNDEPENDS", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_OCAML_FINDLIB", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_OCAML_CAMLP4", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_OCAML_TK", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "NO_OCAMLTK_BUILDDEPENDS", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "NO_OCAMLTK_RUNDEPENDS", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_OCAML_LDCONFIG", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_OCAMLFIND_PLIST", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_OCAML_WASH", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "OCAML_PKGDIRS", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "OCAML_LDLIBS", VAR_DEFAULT },
	{ BLOCK_USES, "USE_OPENLDAP", VAR_DEFAULT },
	{ BLOCK_USES, "WANT_OPENLDAP_SASL", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "WANT_OPENLDAP_VER", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_PERL5", VAR_DEFAULT },
	{ BLOCK_USES, "USE_PHP", VAR_DEFAULT },
	{ BLOCK_USES, "IGNORE_WITH_PHP", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "PHP_MODNAME", VAR_DEFAULT },
	{ BLOCK_USES, "PHP_MOD_PRIO", VAR_DEFAULT },
	{ BLOCK_USES, "PEAR_CHANNEL", VAR_DEFAULT },
	{ BLOCK_USES, "PEAR_CHANNEL_VER", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_PYQT", VAR_DEFAULT },
	{ BLOCK_USES, "PYQT_DIST", VAR_DEFAULT },
	{ BLOCK_USES, "PYQT_SIPDIR", VAR_DEFAULT },
	{ BLOCK_USES, "USE_PYTHON", VAR_DEFAULT },
	{ BLOCK_USES, "PYTHON_NO_DEPENDS", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "PYTHON_CMD", VAR_DEFAULT },
	{ BLOCK_USES, "PYSETUP", VAR_DEFAULT },
	{ BLOCK_USES, "PYDISTUTILS_SETUP", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "PYDISTUTILS_CONFIGURE_TARGET", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "PYDISTUTILS_BUILD_TARGET", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "PYDISTUTILS_INSTALL_TARGET", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "PYDISTUTILS_CONFIGUREARGS", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "PYDISTUTILS_BUILDARGS", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "PYDISTUTILS_INSTALLARGS", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "PYDISTUTILS_INSTALLNOSINGLE", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "PYDISTUTILS_PKGNAME", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "PYDISTUTILS_PKGVERSION", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "PYDISTUTILS_EGGINFO", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "PYDISTUTILS_EGGINFODIR", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_QT", VAR_DEFAULT },
	{ BLOCK_USES, "QT_BINARIES", VAR_DEFAULT },
	{ BLOCK_USES, "QT_CONFIG", VAR_DEFAULT },
	{ BLOCK_USES, "QT_DEFINES", VAR_DEFAULT },
	{ BLOCK_USES, "QT5_VERSION", VAR_DEFAULT },
	{ BLOCK_USES, "USE_RC_SUBR", VAR_DEFAULT },
	{ BLOCK_USES, "USE_RUBY", VAR_DEFAULT },
	{ BLOCK_USES, "BROKEN_RUBY24", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED },
	{ BLOCK_USES, "BROKEN_RUBY25", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED },
	{ BLOCK_USES, "BROKEN_RUBY26", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED },
	{ BLOCK_USES, "RUBY_NO_BUILD_DEPENDS", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "RUBY_NO_RUN_DEPENDS", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_RUBY_EXTCONF", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "RUBY_EXTCONF", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "RUBY_EXTCONF_SUBDIRS", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_RUBY_SETUP", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "RUBY_SETUP", VAR_DEFAULT },
	{ BLOCK_USES, "USE_RUBY_RDOC", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "RUBY_REQUIRE", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_RUBYGEMS", VAR_SKIP_GOALCOL },
	{ BLOCK_USES, "USE_SBCL", VAR_DEFAULT },
	{ BLOCK_USES, "USE_SDL", VAR_DEFAULT },
	{ BLOCK_USES, "USE_SM_COMPAT", VAR_DEFAULT },
	{ BLOCK_USES, "USE_SUBMAKE", VAR_DEFAULT },
	{ BLOCK_USES, "USE_TEX", VAR_DEFAULT },
	{ BLOCK_USES, "USE_WX", VAR_DEFAULT },
	{ BLOCK_USES, "USE_WX_NOT", VAR_DEFAULT },
	{ BLOCK_USES, "WANT_WX", VAR_DEFAULT },
	{ BLOCK_USES, "WANT_WX_VER", VAR_DEFAULT },
	{ BLOCK_USES, "WITH_WX_VER", VAR_DEFAULT },
	{ BLOCK_USES, "WX_COMPS", VAR_DEFAULT },
	{ BLOCK_USES, "WX_CONF_ARGS", VAR_DEFAULT },
	{ BLOCK_USES, "WX_PREMK", VAR_DEFAULT },
	{ BLOCK_USES, "USE_XFCE", VAR_DEFAULT },
	{ BLOCK_USES, "USE_XORG", VAR_DEFAULT },
	{ BLOCK_USES, "XORG_CAT", VAR_DEFAULT },

	{ BLOCK_SHEBANGFIX, "SHEBANG_FILES", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "SHEBANG_GLOB", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "SHEBANG_REGEX", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "SHEBANG_LANG", VAR_DEFAULT },

	// There might be more like these.  Doing the check dynamically
	// might case more false positives than it would be worth the effort.
	{ BLOCK_SHEBANGFIX, "awk_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "awk_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "bash_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "bash_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "bltwish_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "bltwish_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "cw_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "cw_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "env_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "env_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "expect_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "expect_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "gawk_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "gawk_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "gjs_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "gjs_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "hhvm_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "hhvm_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "icmake_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "icmake_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "kaptain_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "kaptain_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "ksh_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "ksh_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "lua_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "lua_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "make_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "make_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "netscript_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "netscript_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "nobash_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "nobash_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "nviz_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "nviz_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "octave_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "octave_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "perl_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "perl_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "perl2_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "perl2_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "php_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "php_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "python_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "python_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "python2_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "python2_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "python3_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "python3_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "r_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "r_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "rackup_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "rackup_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "rc_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "rc_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "rep_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "rep_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "ruby_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "ruby_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "ruby2_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "ruby2_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "sed_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "sed_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "sh_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "sh_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "swipl_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "swipl_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "tclsh_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "tclsh_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "tk_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "tk_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "wish_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "wish_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "zsh_OLD_CMD", VAR_DEFAULT },
	{ BLOCK_SHEBANGFIX, "zsh_CMD", VAR_DEFAULT },

	{ BLOCK_UNIQUEFILES, "UNIQUE_PREFIX", VAR_DEFAULT },
	{ BLOCK_UNIQUEFILES, "UNIQUE_PREFIX_FILES", VAR_DEFAULT },
	{ BLOCK_UNIQUEFILES, "UNIQUE_SUFFIX", VAR_DEFAULT },
	{ BLOCK_UNIQUEFILES, "UNIQUE_SUFFIX_FILES", VAR_DEFAULT },

	{ BLOCK_APACHE, "AP_EXTRAS", VAR_DEFAULT },
	{ BLOCK_APACHE, "AP_INC", VAR_DEFAULT },
	{ BLOCK_APACHE, "AP_LIB", VAR_DEFAULT },
	{ BLOCK_APACHE, "AP_FAST_BUILD", VAR_DEFAULT },
	{ BLOCK_APACHE, "AP_GENPLIST", VAR_DEFAULT },
	{ BLOCK_APACHE, "MODULENAME", VAR_DEFAULT },
	{ BLOCK_APACHE, "SHORTMODNAME", VAR_DEFAULT },
	{ BLOCK_APACHE, "SRC_FILE", VAR_DEFAULT },

	{ BLOCK_ELIXIR, "ELIXIR_APP_NAME", VAR_DEFAULT },
	{ BLOCK_ELIXIR, "ELIXIR_LIB_ROOT", VAR_DEFAULT },
	{ BLOCK_ELIXIR, "ELIXIR_APP_ROOT", VAR_DEFAULT },
	{ BLOCK_ELIXIR, "ELIXIR_HIDDEN", VAR_DEFAULT },
	{ BLOCK_ELIXIR, "ELIXIR_LOCALE", VAR_DEFAULT },
	{ BLOCK_ELIXIR, "MIX_CMD", VAR_DEFAULT },
	{ BLOCK_ELIXIR, "MIX_COMPILE", VAR_DEFAULT },
	{ BLOCK_ELIXIR, "MIX_REWRITE", VAR_DEFAULT },
	{ BLOCK_ELIXIR, "MIX_BUILD_DEPS", VAR_DEFAULT },
	{ BLOCK_ELIXIR, "MIX_RUN_DEPS", VAR_DEFAULT },
	{ BLOCK_ELIXIR, "MIX_DOC_DIRS", VAR_DEFAULT },
	{ BLOCK_ELIXIR, "MIX_DOC_FILES", VAR_DEFAULT },
	{ BLOCK_ELIXIR, "MIX_ENV", VAR_DEFAULT },
	{ BLOCK_ELIXIR, "MIX_ENV_NAME", VAR_DEFAULT },
	{ BLOCK_ELIXIR, "MIX_BUILD_NAME", VAR_DEFAULT },
	{ BLOCK_ELIXIR, "MIX_TARGET", VAR_DEFAULT },
	{ BLOCK_ELIXIR, "MIX_EXTRA_APPS", VAR_DEFAULT },
	{ BLOCK_ELIXIR, "MIX_EXTRA_DIRS", VAR_DEFAULT },
	{ BLOCK_ELIXIR, "MIX_EXTRA_FILES", VAR_DEFAULT },

	{ BLOCK_EMACS, "EMACS_FLAVORS_EXCLUDE", VAR_DEFAULT },
	{ BLOCK_EMACS, "EMACS_NO_DEPENDS", VAR_DEFAULT },

	{ BLOCK_ERLANG, "ERL_APP_NAME", VAR_DEFAULT },
	{ BLOCK_ERLANG, "ERL_APP_ROOT", VAR_DEFAULT },
	{ BLOCK_ERLANG, "REBAR_CMD", VAR_DEFAULT },
	{ BLOCK_ERLANG, "REBAR3_CMD", VAR_DEFAULT },
	{ BLOCK_ERLANG, "REBAR_PROFILE", VAR_DEFAULT },
	{ BLOCK_ERLANG, "REBAR_TARGETS", VAR_DEFAULT },
	{ BLOCK_ERLANG, "ERL_BUILD_NAME", VAR_DEFAULT },
	{ BLOCK_ERLANG, "ERL_BUILD_DEPS", VAR_DEFAULT },
	{ BLOCK_ERLANG, "ERL_RUN_DEPS", VAR_DEFAULT },
	{ BLOCK_ERLANG, "ERL_DOCS", VAR_DEFAULT },

	{ BLOCK_CMAKE, "CMAKE_ARGS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_CMAKE, "CMAKE_OFF", VAR_DEFAULT },
	{ BLOCK_CMAKE, "CMAKE_ON", VAR_DEFAULT },
	{ BLOCK_CMAKE, "CMAKE_BUILD_TYPE", VAR_SKIP_GOALCOL },
	{ BLOCK_CMAKE, "CMAKE_INSTALL_PREFIX", VAR_SKIP_GOALCOL },
	{ BLOCK_CMAKE, "CMAKE_SOURCE_PATH", VAR_SKIP_GOALCOL },

	{ BLOCK_CONFIGURE, "HAS_CONFIGURE", VAR_DEFAULT },
	{ BLOCK_CONFIGURE, "GNU_CONFIGURE", VAR_DEFAULT },
	{ BLOCK_CONFIGURE, "GNU_CONFIGURE_PREFIX", VAR_SKIP_GOALCOL },
	{ BLOCK_CONFIGURE, "CONFIGURE_CMD", VAR_DEFAULT },
	{ BLOCK_CONFIGURE, "CONFIGURE_LOG", VAR_DEFAULT },
	{ BLOCK_CONFIGURE, "CONFIGURE_SCRIPT", VAR_DEFAULT },
	{ BLOCK_CONFIGURE, "CONFIGURE_SHELL", VAR_DEFAULT },
	{ BLOCK_CONFIGURE, "CONFIGURE_ARGS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_CONFIGURE, "CONFIGURE_ENV", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_CONFIGURE, "CONFIGURE_OUTSOURCE", VAR_DEFAULT },
	{ BLOCK_CONFIGURE, "CONFIGURE_TARGET", VAR_DEFAULT },
	{ BLOCK_CONFIGURE, "WITHOUT_FBSD10_FIX", VAR_SKIP_GOALCOL },

	{ BLOCK_QMAKE, "QMAKE_ARGS", VAR_DEFAULT },
	{ BLOCK_QMAKE, "QMAKE_ENV", VAR_DEFAULT },
	{ BLOCK_QMAKE, "QMAKE_CONFIGURE_ARGS", VAR_DEFAULT },
	{ BLOCK_QMAKE, "QMAKE_SOURCE_PATH", VAR_DEFAULT },

	{ BLOCK_MESON, "MESON_ARGS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_MESON, "MESON_BUILD_DIR", VAR_DEFAULT },

	{ BLOCK_SCONS, "CCFLAGS", VAR_DEFAULT },
	{ BLOCK_SCONS, "CPPPATH", VAR_DEFAULT },
	{ BLOCK_SCONS, "LINKFLAGS", VAR_DEFAULT },
	{ BLOCK_SCONS, "LIBPATH", VAR_DEFAULT },

	{ BLOCK_CABAL, "USE_CABAL", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL },
	{ BLOCK_CABAL, "CABAL_FLAGS", VAR_DEFAULT },
	{ BLOCK_CABAL, "EXECUTABLES", VAR_DEFAULT },

	{ BLOCK_CARGO, "CARGO_CRATES", VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL },
	{ BLOCK_CARGO, "CARGO_USE_GITHUB", VAR_DEFAULT },
	{ BLOCK_CARGO, "CARGO_USE_GITLAB", VAR_DEFAULT },
	{ BLOCK_CARGO, "CARGO_GIT_SUBDIR", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_CARGO, "CARGO_CARGOLOCK", VAR_DEFAULT },
	{ BLOCK_CARGO, "CARGO_CARGOTOML", VAR_DEFAULT },
	{ BLOCK_CARGO, "CARGO_FEATURES", VAR_DEFAULT },

	{ BLOCK_CARGO, "CARGO_BUILDDEP", VAR_DEFAULT },
	{ BLOCK_CARGO, "CARGO_BUILD", VAR_DEFAULT },
	{ BLOCK_CARGO, "CARGO_BUILD_ARGS", VAR_DEFAULT },
	{ BLOCK_CARGO, "CARGO_INSTALL", VAR_DEFAULT },
	{ BLOCK_CARGO, "CARGO_INSTALL_ARGS", VAR_DEFAULT },
	{ BLOCK_CARGO, "CARGO_INSTALL_PATH", VAR_DEFAULT },
	{ BLOCK_CARGO, "CARGO_TEST", VAR_DEFAULT },
	{ BLOCK_CARGO, "CARGO_TEST_ARGS", VAR_DEFAULT },
	{ BLOCK_CARGO, "CARGO_UPDATE_ARGS", VAR_DEFAULT },
	{ BLOCK_CARGO, "CARGO_CARGO_BIN", VAR_DEFAULT },
	{ BLOCK_CARGO, "CARGO_DIST_SUBDIR", VAR_DEFAULT },
	{ BLOCK_CARGO, "CARGO_ENV", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_CARGO, "CARGO_TARGET_DIR", VAR_DEFAULT },
	{ BLOCK_CARGO, "CARGO_VENDOR_DIR", VAR_DEFAULT },

	{ BLOCK_GO, "GO_PKGNAME", VAR_DEFAULT },
	{ BLOCK_GO, "GO_TARGET", VAR_DEFAULT },
	{ BLOCK_GO, "GO_BUILDFLAGS", VAR_LEAVE_UNFORMATTED },
	{ BLOCK_GO, "GO_TESTTARGET", VAR_DEFAULT },
	{ BLOCK_GO, "GO_TESTFLAGS", VAR_LEAVE_UNFORMATTED },
	{ BLOCK_GO, "CGO_ENABLED", VAR_DEFAULT },
	{ BLOCK_GO, "CGO_CFLAGS", VAR_DEFAULT },
	{ BLOCK_GO, "CGO_LDFLAGS", VAR_DEFAULT },

	{ BLOCK_LAZARUS, "NO_LAZBUILD", VAR_DEFAULT },
	{ BLOCK_LAZARUS, "LAZARUS_PROJECT_FILES", VAR_DEFAULT },
	{ BLOCK_LAZARUS, "LAZARUS_DIR", VAR_DEFAULT },
	{ BLOCK_LAZARUS, "LAZBUILD_ARGS", VAR_DEFAULT },
	{ BLOCK_LAZARUS, "LAZARUS_NO_FLAVORS", VAR_DEFAULT },

	{ BLOCK_LINUX, "BIN_DISTNAMES", VAR_DEFAULT },
	{ BLOCK_LINUX, "LIB_DISTNAMES", VAR_DEFAULT },
	{ BLOCK_LINUX, "SHARE_DISTNAMES", VAR_DEFAULT },
	{ BLOCK_LINUX, "SRC_DISTFILES", VAR_DEFAULT },

	{ BLOCK_NUGET, "NUGET_DEPENDS", VAR_DEFAULT },
	{ BLOCK_NUGET, "NUGET_PACKAGEDIR", VAR_DEFAULT },
	{ BLOCK_NUGET, "NUGET_LAYOUT", VAR_DEFAULT },
	{ BLOCK_NUGET, "NUGET_FEEDS", VAR_DEFAULT },
	// TODO: These need to be handled specially
	//{ BLOCK_NUGET, "_URL", VAR_DEFAULT },
	//{ BLOCK_NUGET, "_FILE", VAR_DEFAULT },
	//{ BLOCK_NUGET, "_DEPENDS", VAR_DEFAULT },
	{ BLOCK_NUGET, "PAKET_PACKAGEDIR", VAR_DEFAULT },
	{ BLOCK_NUGET, "PAKET_DEPENDS", VAR_DEFAULT },

	{ BLOCK_MAKE, "MAKEFILE", VAR_DEFAULT },
	{ BLOCK_MAKE, "MAKE_CMD", VAR_DEFAULT },
	{ BLOCK_MAKE, "MAKE_ARGS", VAR_PRINT_AS_NEWLINES },
	// MAKE_ARGS_arch/gcc/clang is not a framework
	// but is in common use so list it here too.
	{ BLOCK_MAKE, "MAKE_ARGS_clang", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_MAKE, "MAKE_ARGS_gcc", VAR_PRINT_AS_NEWLINES },
	VAR_FOR_EACH_ARCH(BLOCK_MAKE, "MAKE_ARGS_", VAR_PRINT_AS_NEWLINES),
	{ BLOCK_MAKE, "MAKE_ENV", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_MAKE, "MAKE_ENV_clang", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_MAKE, "MAKE_ENV_gcc", VAR_PRINT_AS_NEWLINES },
	VAR_FOR_EACH_ARCH(BLOCK_MAKE, "MAKE_ENV_", VAR_PRINT_AS_NEWLINES),
	{ BLOCK_MAKE, "MAKE_FLAGS", VAR_DEFAULT },
	{ BLOCK_MAKE, "MAKE_JOBS_UNSAFE", VAR_SKIP_GOALCOL },
	{ BLOCK_MAKE, "ALL_TARGET", VAR_LEAVE_UNSORTED },
	{ BLOCK_MAKE, "INSTALL_TARGET", VAR_LEAVE_UNSORTED },
	{ BLOCK_MAKE, "TEST_ARGS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_MAKE, "TEST_ENV", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_MAKE, "TEST_TARGET", VAR_LEAVE_UNSORTED },

	{ BLOCK_CFLAGS, "CFLAGS", VAR_DEFAULT },
	{ BLOCK_CFLAGS, "CFLAGS_clang", VAR_DEFAULT },
	{ BLOCK_CFLAGS, "CFLAGS_gcc", VAR_DEFAULT },
	VAR_FOR_EACH_ARCH(BLOCK_CFLAGS, "CFLAGS_", VAR_DEFAULT),
	{ BLOCK_CFLAGS, "CPPFLAGS", VAR_DEFAULT },
	{ BLOCK_CFLAGS, "CPPFLAGS_clang", VAR_DEFAULT },
	{ BLOCK_CFLAGS, "CPPFLAGS_gcc", VAR_DEFAULT },
	VAR_FOR_EACH_ARCH(BLOCK_CFLAGS, "CPPFLAGS_", VAR_DEFAULT),
	{ BLOCK_CFLAGS, "CXXFLAGS", VAR_DEFAULT },
	{ BLOCK_CFLAGS, "CXXFLAGS_clang", VAR_DEFAULT },
	{ BLOCK_CFLAGS, "CXXFLAGS_gcc", VAR_DEFAULT },
	VAR_FOR_EACH_ARCH(BLOCK_CFLAGS, "CXXFLAGS_", VAR_DEFAULT),
	{ BLOCK_CFLAGS, "RUSTFLAGS", VAR_DEFAULT },
	{ BLOCK_CFLAGS, "LDFLAGS", VAR_DEFAULT },
	VAR_FOR_EACH_ARCH(BLOCK_CFLAGS, "LDFLAGS_", VAR_DEFAULT),
	{ BLOCK_CFLAGS, "LIBS", VAR_DEFAULT },
	{ BLOCK_CFLAGS, "LLD_UNSAFE", VAR_DEFAULT },
	{ BLOCK_CFLAGS, "SSP_UNSAFE", VAR_DEFAULT },
	{ BLOCK_CFLAGS, "SSP_CFLAGS", VAR_DEFAULT },

	{ BLOCK_CONFLICTS, "CONFLICTS", VAR_DEFAULT },
	{ BLOCK_CONFLICTS, "CONFLICTS_BUILD", VAR_DEFAULT },
	{ BLOCK_CONFLICTS, "CONFLICTS_INSTALL", VAR_DEFAULT },

	{ BLOCK_STANDARD, "AR", VAR_LEAVE_UNSORTED },
	{ BLOCK_STANDARD, "AS", VAR_LEAVE_UNSORTED },
	{ BLOCK_STANDARD, "CC", VAR_LEAVE_UNSORTED },
	{ BLOCK_STANDARD, "CPP", VAR_LEAVE_UNSORTED },
	{ BLOCK_STANDARD, "CXX", VAR_LEAVE_UNSORTED },
	{ BLOCK_STANDARD, "LD", VAR_LEAVE_UNSORTED },
	{ BLOCK_STANDARD, "STRIP", VAR_LEAVE_UNSORTED },
	{ BLOCK_STANDARD, "ETCDIR", VAR_DEFAULT },
	{ BLOCK_STANDARD, "ETCDIR_REL", VAR_DEFAULT },
	{ BLOCK_STANDARD, "DATADIR", VAR_DEFAULT },
	{ BLOCK_STANDARD, "DATADIR_REL", VAR_DEFAULT },
	{ BLOCK_STANDARD, "DOCSDIR", VAR_DEFAULT },
	{ BLOCK_STANDARD, "DOCSDIR_REL", VAR_DEFAULT },
	{ BLOCK_STANDARD, "EXAMPLESDIR", VAR_DEFAULT },
	{ BLOCK_STANDARD, "FILESDIR", VAR_DEFAULT },
	{ BLOCK_STANDARD, "MASTERDIR", VAR_DEFAULT },
	{ BLOCK_STANDARD, "MANDIRS", VAR_DEFAULT },
	{ BLOCK_STANDARD, "MANPREFIX", VAR_DEFAULT },
	{ BLOCK_STANDARD, "MAN1PREFIX", VAR_DEFAULT },
	{ BLOCK_STANDARD, "MAN2PREFIX", VAR_DEFAULT },
	{ BLOCK_STANDARD, "MAN3PREFIX", VAR_DEFAULT },
	{ BLOCK_STANDARD, "MAN4PREFIX", VAR_DEFAULT },
	{ BLOCK_STANDARD, "MAN5PREFIX", VAR_DEFAULT },
	{ BLOCK_STANDARD, "MAN6PREFIX", VAR_DEFAULT },
	{ BLOCK_STANDARD, "MAN7PREFIX", VAR_DEFAULT },
	{ BLOCK_STANDARD, "MAN8PREFIX", VAR_DEFAULT },
	{ BLOCK_STANDARD, "MAN9PREFIX", VAR_DEFAULT },
	{ BLOCK_STANDARD, "PATCHDIR", VAR_DEFAULT },
	{ BLOCK_STANDARD, "PKGDIR", VAR_DEFAULT },
	{ BLOCK_STANDARD, "SCRIPTDIR", VAR_DEFAULT },
	{ BLOCK_STANDARD, "STAGEDIR", VAR_DEFAULT },
	{ BLOCK_STANDARD, "SRC_BASE", VAR_DEFAULT },
	{ BLOCK_STANDARD, "WWWDIR", VAR_DEFAULT },
	{ BLOCK_STANDARD, "WWWDIR_REL", VAR_DEFAULT },
	{ BLOCK_STANDARD, "BINARY_ALIAS", VAR_DEFAULT },
	{ BLOCK_STANDARD, "BINARY_WRAPPERS", VAR_SKIP_GOALCOL },
	{ BLOCK_STANDARD, "BINMODE", VAR_DEFAULT },
	{ BLOCK_STANDARD, "MANMODE", VAR_DEFAULT },
	{ BLOCK_STANDARD, "_SHAREMODE", VAR_DEFAULT },
	{ BLOCK_STANDARD, "BUNDLE_LIBS", VAR_DEFAULT },
	{ BLOCK_STANDARD, "DESKTOP_ENTRIES", VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL },
	{ BLOCK_STANDARD, "DESKTOPDIR", VAR_DEFAULT },
	{ BLOCK_STANDARD, "EXTRA_PATCHES", VAR_LEAVE_UNSORTED },
	VAR_FOR_EACH_ARCH(BLOCK_STANDARD, "EXTRA_PATCHES_", VAR_LEAVE_UNSORTED),
	{ BLOCK_STANDARD, "EXTRACT_CMD", VAR_LEAVE_UNSORTED },
	{ BLOCK_STANDARD, "EXTRACT_BEFORE_ARGS", VAR_LEAVE_UNSORTED | VAR_SKIP_GOALCOL },
	{ BLOCK_STANDARD, "EXTRACT_AFTER_ARGS", VAR_LEAVE_UNSORTED | VAR_SKIP_GOALCOL },
	{ BLOCK_STANDARD, "FETCH_CMD", VAR_LEAVE_UNSORTED },
	{ BLOCK_STANDARD, "FETCH_ARGS", VAR_LEAVE_UNSORTED },
	{ BLOCK_STANDARD, "FETCH_REGET", VAR_LEAVE_UNSORTED },
	{ BLOCK_STANDARD, "FETCH_ENV", VAR_LEAVE_UNSORTED },
	{ BLOCK_STANDARD, "FETCH_BEFORE_ARGS", VAR_LEAVE_UNSORTED | VAR_SKIP_GOALCOL },
	{ BLOCK_STANDARD, "FETCH_AFTER_ARGS", VAR_LEAVE_UNSORTED | VAR_SKIP_GOALCOL },
	{ BLOCK_STANDARD, "PATCH_STRIP", VAR_DEFAULT },
	{ BLOCK_STANDARD, "PATCH_ARGS", VAR_LEAVE_UNSORTED },
	{ BLOCK_STANDARD, "PATCH_DIST_ARGS", VAR_LEAVE_UNSORTED },
	{ BLOCK_STANDARD, "REINPLACE_CMD", VAR_DEFAULT },
	{ BLOCK_STANDARD, "REINPLACE_ARGS", VAR_DEFAULT },
	{ BLOCK_STANDARD, "DISTORIG", VAR_DEFAULT },
	{ BLOCK_STANDARD, "IA32_BINARY_PORT", VAR_DEFAULT },
	{ BLOCK_STANDARD, "IS_INTERACTIVE", VAR_DEFAULT },
	{ BLOCK_STANDARD, "NO_ARCH", VAR_DEFAULT },
	{ BLOCK_STANDARD, "NO_ARCH_IGNORE", VAR_DEFAULT },
	{ BLOCK_STANDARD, "NO_BUILD", VAR_DEFAULT },
	{ BLOCK_STANDARD, "NOCCACHE", VAR_DEFAULT },
	{ BLOCK_STANDARD, "NO_CCACHE", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED },
	{ BLOCK_STANDARD, "NO_INSTALL", VAR_DEFAULT },
	{ BLOCK_STANDARD, "NO_MTREE", VAR_DEFAULT },
	{ BLOCK_STANDARD, "MASTER_SORT", VAR_DEFAULT },
	{ BLOCK_STANDARD, "MASTER_SORT_REGEX", VAR_DEFAULT },
	{ BLOCK_STANDARD, "MTREE_CMD", VAR_LEAVE_UNSORTED },
	{ BLOCK_STANDARD, "MTREE_ARGS", VAR_LEAVE_UNSORTED },
	{ BLOCK_STANDARD, "MTREE_FILE", VAR_DEFAULT },
	{ BLOCK_STANDARD, "NOPRECIOUSMAKEVARS", VAR_SKIP_GOALCOL },
	{ BLOCK_STANDARD, "NO_TEST", VAR_DEFAULT },
	{ BLOCK_STANDARD, "PORTSCOUT", VAR_DEFAULT },
	{ BLOCK_STANDARD, "SCRIPTS_ENV", VAR_DEFAULT },
	{ BLOCK_STANDARD, "SUB_FILES", VAR_DEFAULT },
	{ BLOCK_STANDARD, "SUB_LIST", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_STANDARD, "UID_FILES", VAR_DEFAULT },
	// TODO: Missing *many* variables here

	{ BLOCK_WRKSRC, "NO_WRKSUBDIR", VAR_DEFAULT },
	{ BLOCK_WRKSRC, "AUTORECONF_WRKSRC", VAR_DEFAULT },
	{ BLOCK_WRKSRC, "BUILD_WRKSRC", VAR_DEFAULT },
	{ BLOCK_WRKSRC, "CONFIGURE_WRKSRC", VAR_DEFAULT },
	{ BLOCK_WRKSRC, "INSTALL_WRKSRC", VAR_DEFAULT },
	{ BLOCK_WRKSRC, "PATCH_WRKSRC", VAR_DEFAULT },
	{ BLOCK_WRKSRC, "TEST_WRKSRC", VAR_DEFAULT },
	{ BLOCK_WRKSRC, "WRKDIR", VAR_DEFAULT },
	{ BLOCK_WRKSRC, "WRKSRC", VAR_DEFAULT },
	{ BLOCK_WRKSRC, "WRKSRC_SUBDIR", VAR_DEFAULT },

	{ BLOCK_USERS, "USERS", VAR_DEFAULT },
	{ BLOCK_USERS, "GROUPS", VAR_DEFAULT },

	{ BLOCK_PLIST, "DESCR", VAR_SUBPKG_HELPER },
	{ BLOCK_PLIST, "DISTINFO_FILE", VAR_DEFAULT },
	{ BLOCK_PLIST, "PKGHELP", VAR_DEFAULT },
	{ BLOCK_PLIST, "PKGPREINSTALL", VAR_SUBPKG_HELPER },
	{ BLOCK_PLIST, "PKGINSTALL", VAR_SUBPKG_HELPER },
	{ BLOCK_PLIST, "PKGPOSTINSTALL", VAR_SUBPKG_HELPER },
	{ BLOCK_PLIST, "PKGPREDEINSTALL", VAR_SUBPKG_HELPER },
	{ BLOCK_PLIST, "PKGDEINSTALL", VAR_SUBPKG_HELPER },
	{ BLOCK_PLIST, "PKGPOSTDEINSTALL", VAR_SUBPKG_HELPER },
	{ BLOCK_PLIST, "PKGPREUPGRADE", VAR_SUBPKG_HELPER },
	{ BLOCK_PLIST, "PKGUPGRADE", VAR_SUBPKG_HELPER },
	{ BLOCK_PLIST, "PKGPOSTUPGRADE", VAR_SUBPKG_HELPER },
	{ BLOCK_PLIST, "PKGMESSAGE", VAR_SUBPKG_HELPER },
	{ BLOCK_PLIST, "PKG_DBDIR", VAR_DEFAULT },
	{ BLOCK_PLIST, "PKG_SUFX", VAR_DEFAULT },
	{ BLOCK_PLIST, "PLIST", VAR_DEFAULT },
	{ BLOCK_PLIST, "POST_PLIST", VAR_DEFAULT },
	{ BLOCK_PLIST, "TMPPLIST", VAR_DEFAULT },
	{ BLOCK_PLIST, "INFO", VAR_DEFAULT },
	{ BLOCK_PLIST, "INFO_PATH", VAR_DEFAULT },
	{ BLOCK_PLIST, "PLIST_DIRS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_PLIST, "PLIST_FILES", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_PLIST, "PLIST_SUB", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_PLIST, "PORTDATA", VAR_CASE_SENSITIVE_SORT },
	{ BLOCK_PLIST, "PORTDOCS", VAR_CASE_SENSITIVE_SORT },
	{ BLOCK_PLIST, "PORTEXAMPLES", VAR_CASE_SENSITIVE_SORT },

	{ BLOCK_OPTDEF, "OPTIONS_DEFINE", VAR_DEFAULT },
	// These do not exist in the framework but some ports
	// define them themselves
	{ BLOCK_OPTDEF, "OPTIONS_DEFINE_DragonFly", VAR_SKIP_GOALCOL },
	VAR_FOR_EACH_FREEBSD_VERSION(BLOCK_OPTDEF, "OPTIONS_DEFINE_", VAR_SKIP_GOALCOL),
	VAR_FOR_EACH_ARCH(BLOCK_OPTDEF, "OPTIONS_DEFINE_", VAR_SKIP_GOALCOL),
	{ BLOCK_OPTDEF, "OPTIONS_DEFAULT", VAR_DEFAULT },
	{ BLOCK_OPTDEF, "OPTIONS_DEFAULT_DragonFly", VAR_SKIP_GOALCOL },
	VAR_FOR_EACH_FREEBSD_VERSION(BLOCK_OPTDEF, "OPTIONS_DEFAULT_", VAR_SKIP_GOALCOL),
	VAR_FOR_EACH_ARCH(BLOCK_OPTDEF, "OPTIONS_DEFAULT_", VAR_SKIP_GOALCOL),
	{ BLOCK_OPTDEF, "OPTIONS_GROUP", VAR_DEFAULT },
	{ BLOCK_OPTDEF, "OPTIONS_MULTI", VAR_DEFAULT },
	{ BLOCK_OPTDEF, "OPTIONS_RADIO", VAR_DEFAULT },
	{ BLOCK_OPTDEF, "OPTIONS_SINGLE", VAR_DEFAULT },
	{ BLOCK_OPTDEF, "OPTIONS_EXCLUDE", VAR_DEFAULT },
	{ BLOCK_OPTDEF, "OPTIONS_EXCLUDE_DragonFly", VAR_SKIP_GOALCOL },
	VAR_FOR_EACH_FREEBSD_VERSION(BLOCK_OPTDEF, "OPTIONS_EXCLUDE_", VAR_SKIP_GOALCOL),
	VAR_FOR_EACH_ARCH(BLOCK_OPTDEF, "OPTIONS_EXCLUDE_", VAR_SKIP_GOALCOL),
	{ BLOCK_OPTDEF, "OPTIONS_SLAVE", VAR_DEFAULT },
	{ BLOCK_OPTDEF, "OPTIONS_OVERRIDE", VAR_DEFAULT },
	{ BLOCK_OPTDEF, "NO_OPTIONS_SORT", VAR_SKIP_GOALCOL },
	{ BLOCK_OPTDEF, "OPTIONS_SUB", VAR_DEFAULT },

	{ BLOCK_OPTDESC, "DESC", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED },

	{ BLOCK_OPTHELPER, "IMPLIES", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "PREVENTS", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "PREVENTS_MSG", VAR_DEFAULT },
#if PORTFMT_SUBPACKAGES
	{ BLOCK_OPTHELPER, "SUBPACKAGES", VAR_DEFAULT },
#endif
	{ BLOCK_OPTHELPER, "CATEGORIES_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "CATEGORIES", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "MASTER_SITES_OFF", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "MASTER_SITES", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "DISTFILES_OFF", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "DISTFILES", VAR_IGNORE_WRAPCOL | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "EXTRACT_ONLY_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "EXTRACT_ONLY", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "PATCH_SITES_OFF", VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "PATCH_SITES", VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "PATCHFILES_OFF", VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "PATCHFILES", VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "BROKEN_OFF", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED },
	{ BLOCK_OPTHELPER, "BROKEN", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED },
	{ BLOCK_OPTHELPER, "IGNORE_OFF", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED },
	{ BLOCK_OPTHELPER, "IGNORE", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED },
	{ BLOCK_OPTHELPER, "BUILD_DEPENDS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SUBPKG_HELPER },
	{ BLOCK_OPTHELPER, "BUILD_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SUBPKG_HELPER },
	{ BLOCK_OPTHELPER, "EXTRACT_DEPENDS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SUBPKG_HELPER },
	{ BLOCK_OPTHELPER, "EXTRACT_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SUBPKG_HELPER },
	{ BLOCK_OPTHELPER, "FETCH_DEPENDS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SUBPKG_HELPER },
	{ BLOCK_OPTHELPER, "FETCH_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SUBPKG_HELPER },
	{ BLOCK_OPTHELPER, "LIB_DEPENDS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SUBPKG_HELPER },
	{ BLOCK_OPTHELPER, "LIB_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SUBPKG_HELPER },
	{ BLOCK_OPTHELPER, "PATCH_DEPENDS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SUBPKG_HELPER },
	{ BLOCK_OPTHELPER, "PATCH_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SUBPKG_HELPER },
	{ BLOCK_OPTHELPER, "PKG_DEPENDS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SUBPKG_HELPER },
	{ BLOCK_OPTHELPER, "PKG_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SUBPKG_HELPER },
	{ BLOCK_OPTHELPER, "RUN_DEPENDS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SUBPKG_HELPER },
	{ BLOCK_OPTHELPER, "RUN_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SUBPKG_HELPER },
	{ BLOCK_OPTHELPER, "TEST_DEPENDS_OFF", VAR_PRINT_AS_NEWLINES | VAR_SUBPKG_HELPER },
	{ BLOCK_OPTHELPER, "TEST_DEPENDS", VAR_PRINT_AS_NEWLINES | VAR_SUBPKG_HELPER },
	{ BLOCK_OPTHELPER, "USES_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "USES", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "USE_OFF", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "USE", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "GH_ACCOUNT_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "GH_ACCOUNT", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "GH_PROJECT_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "GH_PROJECT", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "GH_SUBDIR_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "GH_SUBDIR", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "GH_TAGNAME_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "GH_TAGNAME", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "GH_TUPLE_OFF", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "GH_TUPLE", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "GL_ACCOUNT_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "GL_ACCOUNT", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "GL_COMMIT_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "GL_COMMIT", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "GL_PROJECT_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "GL_PROJECT", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "GL_SITE_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "GL_SITE", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "GL_SUBDIR_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "GL_SUBDIR", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "GL_TUPLE_OFF", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "GL_TUPLE", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "CMAKE_BOOL_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "CMAKE_BOOL", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "CMAKE_OFF", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "CMAKE_ON", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "CONFIGURE_OFF", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "CONFIGURE_ON", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "CONFIGURE_ENABLE", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "CONFIGURE_WITH", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "CONFIGURE_ENV_OFF", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "CONFIGURE_ENV", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "QMAKE_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "QMAKE_ON", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "MESON_DISABLED", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "MESON_ENABLED", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "MESON_FALSE", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "MESON_NO", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "MESON_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "MESON_ON", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "MESON_TRUE", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "MESON_YES", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "USE_CABAL", VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL },
	{ BLOCK_OPTHELPER, "CABAL_FLAGS", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "EXECUTABLES", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "MAKE_ARGS_OFF", VAR_DEFAULT | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "MAKE_ARGS", VAR_DEFAULT | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "MAKE_ENV_OFF", VAR_DEFAULT | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "MAKE_ENV", VAR_DEFAULT | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "ALL_TARGET_OFF", VAR_LEAVE_UNSORTED },
	{ BLOCK_OPTHELPER, "ALL_TARGET", VAR_LEAVE_UNSORTED },
	{ BLOCK_OPTHELPER, "INSTALL_TARGET_OFF", VAR_LEAVE_UNSORTED },
	{ BLOCK_OPTHELPER, "INSTALL_TARGET", VAR_LEAVE_UNSORTED },
	{ BLOCK_OPTHELPER, "TEST_TARGET_OFF", VAR_LEAVE_UNSORTED },
	{ BLOCK_OPTHELPER, "TEST_TARGET", VAR_LEAVE_UNSORTED },
	{ BLOCK_OPTHELPER, "CFLAGS_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "CFLAGS", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "CPPFLAGS_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "CPPFLAGS", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "CXXFLAGS_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "CXXFLAGS", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "LDFLAGS_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "LDFLAGS", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "LIBS_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "LIBS", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "CONFLICTS_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "CONFLICTS", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "CONFLICTS_BUILD_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "CONFLICTS_BUILD", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "CONFLICTS_INSTALL_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "CONFLICTS_INSTALL", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "BINARY_ALIAS_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "BINARY_ALIAS", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "DESKTOP_ENTRIES_OFF", VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL },
	{ BLOCK_OPTHELPER, "DESKTOP_ENTRIES", VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES | VAR_SKIP_GOALCOL },
	{ BLOCK_OPTHELPER, "EXTRA_PATCHES_OFF", VAR_LEAVE_UNSORTED },
	{ BLOCK_OPTHELPER, "EXTRA_PATCHES", VAR_LEAVE_UNSORTED },
	{ BLOCK_OPTHELPER, "SUB_FILES_OFF", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "SUB_FILES", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "SUB_LIST_OFF", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "SUB_LIST", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "INFO_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "INFO", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "PLIST_DIRS_OFF", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "PLIST_DIRS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "PLIST_FILES_OFF", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "PLIST_FILES", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "PLIST_SUB_OFF", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "PLIST_SUB", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "PORTDOCS_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "PORTDOCS", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "PORTEXAMPLES_OFF", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "PORTEXAMPLES", VAR_DEFAULT },
	{ BLOCK_OPTHELPER, "VARS_OFF", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_OPTHELPER, "VARS", VAR_PRINT_AS_NEWLINES },
};

// Variables that are somewhere in the ports framework but that
// ports do not usually set.  Portclippy will flag them as "unknown".
// We can set special formatting rules for them here instead of in
// variable_order_.
static struct VariableOrderEntry special_variables_[] = {
	{ BLOCK_UNKNOWN, "_ALL_EXCLUDE", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_BUILD_SEQ", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_BUILD_SETUP", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_CARGO_GIT_PATCH_CARGOTOML", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_CONFIGURE_SEQ", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_DEPENDS-LIST", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_EXTRACT_SEQ", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_FETCH_SEQ", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_FLAVOR_RECURSIVE_SH", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_IPXE_BUILDCFG", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_UNKNOWN, "_LICENSE_TEXT", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_OPTIONS_DEPENDS", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_OPTIONS_TARGETS", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_PACKAGE_SEQ", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_PATCH_SEQ", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_PATCHFILES", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_PATCHFILES2", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_PKG_SEQ", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_PKGTOOLSDEFINED", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_PORTS_DIRECTORIES", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_PORTSEARCH", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_PRETTY_PRINT_DEPENDS_LIST", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_RANDOMIZE_SITES", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_REALLY_ALL_POSSIBLE_OPTIONS", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_SANITY_SEQ", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_STAGE_SEQ", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_TARGETS_STAGES", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_TARGETS", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_TEST_SEQ", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_tmp_seq", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_UNIFIED_DEPENDS", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "_UNSUPPORTED_SYSTEM_MESSAGE", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "ALL_NOTNEEDED", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "ALL_UNSUPPORTED", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "ALL-DEPENDS-FLAVORS-LIST", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "ALL-DEPENDS-LIST", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "AWK", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "BASENAME", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "BRANDELF", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "BSDMAKE", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "BUILD_FAIL_MESSAGE", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "BUILD-DEPENDS-LIST", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "BZCAT", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "BZIP2_CMD", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "CARGO_CARGO_RUN", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "CAT", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "CHGRP", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "CHMOD", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "CHOWN", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "CHROOT", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "CLEAN-DEPENDS-LIMITED-LIST", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "CLEAN-DEPENDS-LIST", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "CO_ENV", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_UNKNOWN, "COMM", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "COPYTREE_BIN", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "COPYTREE_SHARE", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "CP", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "CPIO", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "CPP", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "CUT", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "D4P_ENV", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_UNKNOWN, "DAEMONARGS", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "DC", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "DEBUG_MSG", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "DEPENDS-LIST", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "DEV_ERROR", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_UNKNOWN, "DEV_WARNING", VAR_IGNORE_WRAPCOL | VAR_LEAVE_UNSORTED | VAR_PRINT_AS_NEWLINES },
	{ BLOCK_UNKNOWN, "DIALOG", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "DIALOG4PORTS", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "DIFF", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "DIRNAME", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "DO_MAKE_BUILD", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "DO_MAKE_TEST", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "ECHO_CMD", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "ECHO_MSG", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "EGREP", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "EXPR", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "FALSE", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "FETCH_LIST", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "FILE", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "FIND", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "FLEX", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "FMT_80", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "FMT", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "GMAKE", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "GN_ARGS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_UNKNOWN, "GO_ENV", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_UNKNOWN, "GREP", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "GUNZIP_CMD", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "GZCAT", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "GZIP_CMD", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "GZIP", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "HEAD", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "HTMLIFY", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "ID", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "IDENT", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "IGNORECMD", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "IGNOREDIR", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "INSTALL_DATA", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "INSTALL_KLD", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "INSTALL_LIB", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "INSTALL_MAN", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "INSTALL_PROGRAM", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "INSTALL_SCRIPT", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "INSTALL_TARGET", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "intlhack_PRE_PATCH", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "IPXE_BUILDCFG", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_UNKNOWN, "JOT", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "LDCONFIG", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "LHA_CMD", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "LIBS", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "LN", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "LS", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "MASTER_SITES_ABBREVS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_UNKNOWN, "MASTER_SORT_AWK", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "MD5", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "MISSING-DEPENDS-LIST", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "MKDIR", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "MKTEMP", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "MOUNT_DEVFS", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "MOUNT_NULLFS", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "MOUNT", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "MOZ_OPTIONS", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_UNKNOWN, "MOZ_SED_ARGS", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "MOZCONFIG_SED", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "MULTI_EOL", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "MV", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "OBJCOPY", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "OBJDUMP", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "PASTE", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "PAX", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "PKG_ADD", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "PKG_BIN", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "PKG_CREATE", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "PKG_DELETE", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "PKG_INFO", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "PKG_QUERY", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "PKG_REGISTER", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "PKG_VERSION", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "PRINTF", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "PS_CMD", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "PW", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "QA_ENV", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_UNKNOWN, "RADIO_EOL", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "RANDOM_ARGS", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "READELF", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "REALPATH", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "referencehack_PRE_PATCH", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "REINPLACE_ARGS", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "RLN", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "RM", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "RMDIR", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "RUBY_CONFIG", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "RUN-DEPENDS-LIST", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "SANITY_DEPRECATED", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "SANITY_NOTNEEDED", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "SANITY_UNSUPPORTED", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "SED", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "SET_LATE_CONFIGURE_ARGS", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "SETENV", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "SH", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "SHA256", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "SINGLE_EOL", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "SOELIM", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "SORT", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "STAT", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "STRIP_CMD", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "SU_CMD", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "SUBDIR", VAR_PRINT_AS_NEWLINES },
	{ BLOCK_UNKNOWN, "SYSCTL", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "TAIL", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "TEST-DEPENDS-LIST", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "TEST", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "TEX_FORMAT_LUATEX", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "TEXHASHDIRS", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "TR", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "TRUE", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "UMOUNT", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "UNAME", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "UNMAKESELF_CMD", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "UNZIP_CMD", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "UNZIP_NATIVE_CMD", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "WARNING", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "WHICH", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "XARGS", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "XMKMF", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "XZ_CMD", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "XZ", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "XZCAT", VAR_LEAVE_UNSORTED },
	{ BLOCK_UNKNOWN, "YACC", VAR_LEAVE_UNSORTED },
};

#undef VAR_FOR_EACH_ARCH
#undef VAR_FOR_EACH_FREEBSD_VERSION
#undef VAR_FOR_EACH_FREEBSD_VERSION_AND_ARCH
#undef VAR_FOR_EACH_SSL

static volatile int rules_initialized = 0;

int
variable_has_flag(struct Parser *parser, const char *var, int flag)
{
	char *helper;
	if (is_options_helper(parser, var, NULL, &helper, NULL)) {
		for (size_t i = 0; i < nitems(variable_order_); i++) {
			if ((variable_order_[i].block == BLOCK_OPTHELPER ||
			     variable_order_[i].block == BLOCK_OPTDEF ||
			     variable_order_[i].block == BLOCK_OPTDESC) &&
			    (variable_order_[i].flags & flag) &&
			    strcmp(helper, variable_order_[i].var) == 0) {
				free(helper);
				return 1;
			}
		}
		free(helper);
	}

	if (is_flavors_helper(parser, var, NULL, &helper)) {
		for (size_t i = 0; i < nitems(variable_order_); i++) {
			if (variable_order_[i].block == BLOCK_FLAVORS_HELPER &&
			    (variable_order_[i].flags & flag) &&
			    strcmp(helper, variable_order_[i].var) == 0) {
				free(helper);
				return 1;
			}
		}
		free(helper);
	}

	for (size_t i = 0; i < nitems(variable_order_); i++) {
		if (variable_order_[i].block != BLOCK_OPTHELPER &&
		    variable_order_[i].block != BLOCK_OPTDEF &&
		    variable_order_[i].block != BLOCK_OPTDESC &&
		    variable_order_[i].block != BLOCK_FLAVORS_HELPER &&
		    (variable_order_[i].flags & flag) &&
		    strcmp(var, variable_order_[i].var) == 0) {
			return 1;
		}
	}

	for (size_t i = 0; i < nitems(special_variables_); i++) {
		if ((special_variables_[i].flags & flag) &&
		    strcmp(var, special_variables_[i].var) == 0) {
			return 1;
		}
	}

	return 0;
}

int
ignore_wrap_col(struct Parser *parser, struct Variable *var)
{
	const char *varname = variable_name(var);

	if (variable_modifier(var) == MODIFIER_SHELL ||
	    matches(RE_LICENSE_NAME, varname)) {
		return 1;
	}

	return variable_has_flag(parser, varname, VAR_IGNORE_WRAPCOL);
}

int
indent_goalcol(struct Variable *var)
{
	size_t varlength = strlen(variable_name(var)) + 1;
	switch (variable_modifier(var)) {
	case MODIFIER_ASSIGN:
		varlength += 1;
		break;
	case MODIFIER_APPEND:
	case MODIFIER_EXPAND:
	case MODIFIER_OPTIONAL:
	case MODIFIER_SHELL:
		varlength += 2;
		break;
	default:
		errx(1, "Unknown variable modifier: %d", variable_modifier(var));
	}
	if (((varlength + 1) % 8) == 0) {
		varlength++;
	}
	return ceil(varlength / 8.0) * 8;
}

int
is_comment(struct Token *token)
{
	if (token == NULL || token_data(token) == NULL) {
		return 0;
	}

	char *datap = token_data(token);
	for (; *datap != 0 && isspace(*datap); datap++);
	return *datap == '#';
}

int
is_include_bsd_port_mk(struct Token *t)
{
	struct Conditional *c = token_conditional(t);
	return c && token_type(t) == CONDITIONAL_TOKEN &&
		conditional_type(c) == COND_INCLUDE &&
		(strcmp(token_data(t), "<bsd.port.options.mk>") == 0 ||
		strcmp(token_data(t), "<bsd.port.pre.mk>") == 0 ||
		strcmp(token_data(t), "<bsd.port.post.mk>") == 0 ||
		strcmp(token_data(t), "<bsd.port.mk>") == 0);
}

int
case_sensitive_sort(struct Parser *parser, struct Variable *var)
{
	return variable_has_flag(parser, variable_name(var), VAR_CASE_SENSITIVE_SORT);
}

int
leave_unformatted(struct Parser *parser, struct Variable *var)
{
	return variable_has_flag(parser, variable_name(var), VAR_LEAVE_UNFORMATTED);
}

int
leave_unsorted(struct Parser *parser, struct Variable *var)
{
	const char *varname = variable_name(var);
	if (variable_has_flag(parser, varname, VAR_LEAVE_UNSORTED)) {
		return 1;
	}

	if (variable_modifier(var) == MODIFIER_SHELL ||
	    str_endswith(varname, "_CMD") ||
	    str_endswith(varname, "_ALT") ||
	    str_endswith(varname, "_REASON") ||
	    str_endswith(varname, "_USE_GNOME_IMPL") ||
	    str_endswith(varname, "FLAGS") ||
	    matches(RE_LICENSE_NAME, varname)) {
		return 1;
	}

	return 0;
}

int
preserve_eol_comment(struct Token *t)
{
	if (t == NULL || token_type(t) != VARIABLE_TOKEN) {
		return 0;
	}

	if (!is_comment(t)) {
		return 1;
	}

	/* Remove all whitespace from the comment first to cover more cases */
	char *token = xmalloc(strlen(token_data(t)) + 1);
	for (char *tokenp = token, *datap = token_data(t); *datap != 0; datap++) {
		if (!isspace(*datap)) {
			*tokenp++ = *datap;
		}
	}
	int retval = strcmp(token, "#") == 0 || strcmp(token, "#empty") == 0 ||
		strcmp(token, "#none") == 0;
	free(token);
	return retval;
}

int
print_as_newlines(struct Parser *parser, struct Variable *var)
{
	return variable_has_flag(parser, variable_name(var), VAR_PRINT_AS_NEWLINES);
}

int
skip_dedup(struct Parser *parser, struct Variable *var)
{
	// XXX: skip_dedup is probably a super set of leave_unsorted
	return leave_unsorted(parser, var);
}

int
skip_goalcol(struct Parser *parser, struct Variable *var)
{
	const char *varname = variable_name(var);

	if (matches(RE_LICENSE_NAME, varname)) {
		return 1;
	}

	return variable_has_flag(parser, varname, VAR_SKIP_GOALCOL);
}

static int
compare_rel(const char *rel[], size_t rellen, const char *a, const char *b)
{
	ssize_t ai = -1;
	ssize_t bi = -1;
	for (size_t i = 0; i < rellen; i++) {
		if (ai == -1 && strcmp(a, rel[i]) == 0) {
			ai = i;
		}
		if (bi == -1 && strcmp(b, rel[i]) == 0) {
			bi = i;
		}
		if (ai != -1 && bi != -1) {
			if (bi > ai) {
				return -1;
			} else if (ai > bi) {
				return 1;
			} else {
				return 0;
			}
		}
	}

	return strcasecmp(a, b);
}

int
compare_tokens(const void *ap, const void *bp, void *userdata)
{
	struct Parser *parser = userdata;
	struct Token *a = *(struct Token**)ap;
	struct Token *b = *(struct Token**)bp;
	assert(token_type(a) == VARIABLE_TOKEN);
	assert(token_type(b) == VARIABLE_TOKEN);
	assert(variable_cmp(token_variable(a), token_variable(b)) == 0);

	struct Variable *var = token_variable(a);

	/* End-of-line comments always go last */
	if (is_comment(a) && is_comment(b)) {
		return 0;
	}
	if (is_comment(a)) {
		return 1;
	}
	if (is_comment(b)) {
		return -1;
	}

	int result;
	if (compare_license_perms(var, token_data(a), token_data(b), &result) ||
	    compare_plist_files(parser, var, token_data(a), token_data(b), &result) ||
	    compare_use_pyqt(var, token_data(a), token_data(b), &result) ||
	    compare_use_qt(var, token_data(a), token_data(b), &result)) {
		return result;
	}

	if (case_sensitive_sort(parser, var)) {
		return strcmp(token_data(a), token_data(b));
	} else {
		return strcasecmp(token_data(a), token_data(b));
	}
}

int
compare_license_perms(struct Variable *var, const char *a, const char *b, int *result)
{
	assert(result != NULL);

	if (!matches(RE_LICENSE_PERMS, variable_name(var))) {
		return 0;
	}

	*result = compare_rel(license_perms_rel, nitems(license_perms_rel), a, b);
	return 1;
}

int
compare_plist_files(struct Parser *parser, struct Variable *var, const char *a, const char *b, int *result)
{
	assert(result != NULL);

	char *helper = NULL;
	if (is_options_helper(parser, variable_name(var), NULL, &helper, NULL)) {
		if (strcmp(helper, "PLIST_FILES_OFF") != 0 &&
		    strcmp(helper, "PLIST_FILES") != 0 &&
		    strcmp(helper, "PLIST_DIRS_OFF") != 0 &&
		    strcmp(helper, "PLIST_DIRS") != 0) {
			free(helper);
			return 0;
		}
		free(helper);
	} else if (strcmp(variable_name(var), "PLIST_FILES") != 0 &&
	    strcmp(variable_name(var), "PLIST_DIRS") != 0) {
		return 0;
	}

	/* Ignore plist keywords */
	char *as = sub(RE_PLIST_KEYWORDS, "", a);
	char *bs = sub(RE_PLIST_KEYWORDS, "", b);
	*result = strcasecmp(as, bs);
	free(as);
	free(bs);

	return 1;
}

int
compare_use_pyqt(struct Variable *var, const char *a, const char *b, int *result)
{
	assert(result != NULL);

	if (strcmp(variable_name(var), "USE_PYQT") != 0) {
		return 0;
	}

	*result = compare_rel(use_pyqt_rel, nitems(use_pyqt_rel), a, b);
	return 1;
}

int
compare_use_qt(struct Variable *var, const char *a, const char *b, int *result)
{
	assert(result != NULL);

	if (strcmp(variable_name(var), "USE_QT") != 0) {
		return 0;
	}

	*result = compare_rel(use_qt_rel, nitems(use_qt_rel), a, b);
	return 1;
}

int
is_flavors_helper(struct Parser *parser, const char *var, char **prefix_ret, char **helper_ret)
{
	const char *suffix = NULL;
	for (size_t i = 0; i < nitems(variable_order_); i++) {
		if (variable_order_[i].block != BLOCK_FLAVORS_HELPER) {
			continue;
		}
		const char *helper = variable_order_[i].var;
		if (str_endswith(var, helper) &&
		    strlen(var) > strlen(helper) &&
		    var[strlen(var) - strlen(helper) - 1] == '_') {
			suffix = helper;
			break;
		}
	}
	if (suffix == NULL) {
		return 0;
	}

	// ^[-_[:lower:][:digit:]]+_
	size_t len = strlen(var) - strlen(suffix);
	if (len == 0) {
		return 0;
	}
	if (var[len - 1] != '_') {
		return 0;
	}
	for (size_t i = 0; i < len; i++) {
		char c = var[i];
		if (c != '-' && c != '_' && !islower(c) && !isdigit(c)) {
			return 0;
		}
	}

	if (parser_settings(parser).behavior & PARSER_DYNAMIC_PORT_OPTIONS) {
		goto done;
	}

	char *prefix = xstrndup(var, len - 1);
	int found = 0;
	// XXX: This can still be made a lot more strict
	for (size_t i = 0; i < nitems(static_flavors_); i++) {
		const char *flavor = static_flavors_[i];
		if (strcmp(prefix, flavor) == 0) {
			found = 1;
			break;
		}
	}
	if (!found) {
		struct Array *flavors = NULL;
		if (!parser_lookup_variable_all(parser, "FLAVORS", &flavors, NULL)) {
			free(prefix);
			return 0;
		}
		for (size_t i = 0; i < array_len(flavors); i++) {
			const char *flavor = array_get(flavors, i);
			if (strcmp(prefix, flavor) == 0) {
				found = 1;
				break;
			}
		}
		array_free(flavors);
		if (!found) {
			free(prefix);
			return 0;
		}
	}
	free(prefix);

done:
	if (prefix_ret) {
		*prefix_ret = xstrndup(var, len);
	}
	if (helper_ret) {
		*helper_ret = xstrdup(suffix);
	}

	return 1;
}

char *
extract_subpkg(struct Parser *parser, const char *var_, char **subpkg_ret)
{
	char *var = NULL;
	const char *subpkg = NULL;
	for (ssize_t i = strlen(var_) - 1; i > -1; i--) {
		char c = var_[i];
		if (c != '-' && c != '_' && !islower(c) && !isdigit(c)) {
			if (c == '.') {
				subpkg = var_ + i + 1;
				var = xstrndup(var_, i);
			} else {
				var = xstrdup(var_);
			}
			break;
		}
	}

	if (var == NULL) {
		if (subpkg_ret) {
			*subpkg_ret = NULL;
		}
		return NULL;
	}

	if (subpkg && !(parser_settings(parser).behavior & PARSER_DYNAMIC_PORT_OPTIONS)) {
		int found = 0;
#if PORTFMT_SUBPACKAGES
		struct Set *subpkgs = parser_subpackages(parser);
		SET_FOREACH (subpkgs, const char *, pkg) {
			if (strcmp(subpkg, pkg) == 0) {
				found = 1;
				break;
			}
		}
#endif
		if (!found) {
			if (subpkg_ret) {
				*subpkg_ret = NULL;
			}
			free(var);
			return NULL;
		}
	}

	if (subpkg_ret) {
		if (subpkg) {
			*subpkg_ret = xstrdup(subpkg);
		} else {
			*subpkg_ret = NULL;
		}
	}

	return var;
}

int
is_options_helper(struct Parser *parser, const char *var_, char **prefix_ret, char **helper_ret, char **subpkg_ret)
{
	char *subpkg;
	char *var;
	if ((var = extract_subpkg(parser, var_, &subpkg)) == NULL) {
		return 0;
	}

	const char *suffix = NULL;
	if (str_endswith(var, "DESC")) {
		suffix = "DESC";
	} else {
		for (size_t i = 0; i < nitems(variable_order_); i++) {
			if (variable_order_[i].block != BLOCK_OPTHELPER) {
				continue;
			}
			const char *helper = variable_order_[i].var;
			if (str_endswith(var, helper) &&
			    strlen(var) > strlen(helper) &&
			    var[strlen(var) - strlen(helper) - 1] == '_') {
				suffix = helper;
				break;
			}
		}
	}
	if (suffix == NULL) {
		free(subpkg);
		free(var);
		return 0;
	}

	if (subpkg) {
		int found = 0;
#if PORTFMT_SUBPACKAGES
		for (size_t i = 0; i < nitems(variable_order_); i++) {
			if (variable_order_[i].block != BLOCK_OPTHELPER ||
			    !(variable_order_[i].flags & VAR_SUBPKG_HELPER)) {
				continue;
			}
			if (strcmp(variable_order_[i].var, suffix) == 0) {
				found = 1;
			}
		}
#endif
		if (!found) {
			free(subpkg);
			free(var);
			return 0;
		}
	}


	// ^[-_[:upper:][:digit:]]+_
	size_t len = strlen(var) - strlen(suffix);
	if (len == 0) {
		free(subpkg);
		free(var);
		return 0;
	}
	if (var[len - 1] != '_') {
		free(subpkg);
		free(var);
		return 0;
	}
	for (size_t i = 0; i < len; i++) {
		char c = var[i];
		if (c != '-' && c != '_' && !isupper(c) && !isdigit(c)) {
			free(subpkg);
			free(var);
			return 0;
		}
	}

	if (parser_settings(parser).behavior & PARSER_DYNAMIC_PORT_OPTIONS) {
		goto done;
	}

	char *prefix = xstrndup(var, len - 1);
	struct Set *groups = NULL;
	struct Set *options = NULL;
	parser_port_options(parser, &groups, &options);
	if (strcmp(suffix, "DESC") == 0) {
		SET_FOREACH (groups, const char *, group) {
			if (strcmp(prefix, group) == 0) {
				free(prefix);
				goto done;
			}
		}
	}
	int found = 0;
	SET_FOREACH (options, const char *, option) {
		if (strcmp(prefix, option) == 0) {
			found = 1;
			break;
		}
	}
	free(prefix);
	if (!found) {
		free(subpkg);
		free(var);
		return 0;
	}

done:
	if (prefix_ret) {
		*prefix_ret = xstrndup(var, len);
	}
	if (helper_ret) {
		*helper_ret = xstrdup(suffix);
	}
	if (subpkg_ret) {
		if (subpkg) {
			*subpkg_ret = xstrdup(subpkg);
		} else {
			*subpkg_ret = NULL;
		}
	}

	free(subpkg);
	free(var);

	return 1;
}

enum BlockType
variable_order_block(struct Parser *parser, const char *var)
{
	if (strcmp(var, "LICENSE") == 0) {
		return BLOCK_LICENSE;
	}
	for (size_t i = 0; i < nitems(variable_order_); i++) {
		if (variable_order_[i].block != BLOCK_LICENSE ||
		    strcmp(variable_order_[i].var, "LICENSE") == 0) {
			continue;
		}
		if (strcmp(variable_order_[i].var, var) == 0) {
			return BLOCK_LICENSE;
		}
		if (str_startswith(var, variable_order_[i].var)) {
			const char *suffix = var + strlen(variable_order_[i].var);
			struct Array *licenses = NULL;
			if (*suffix == '_' && parser_lookup_variable(parser, "LICENSE", &licenses, NULL) != NULL) {
				for (size_t j = 0; j < array_len(licenses); j++) {
					if (strcmp(suffix + 1, array_get(licenses, j)) == 0) {
						array_free(licenses);
						return BLOCK_LICENSE;
					}
				}
				array_free(licenses);
			}
		}
	}

	if (is_flavors_helper(parser, var, NULL, NULL)) {
		return BLOCK_FLAVORS_HELPER;
	}

	if (is_options_helper(parser, var, NULL, NULL, NULL)) {
		if (str_endswith(var, "_DESC")) {
			return BLOCK_OPTDESC;
		} else {
			return BLOCK_OPTHELPER;
		}
	}

	if (matches(RE_OPTIONS_GROUP, var)) {
		return BLOCK_OPTDEF;
	}

	const char *tmp = var;
	char *var_without_subpkg = extract_subpkg(parser, var, NULL);
	if (var_without_subpkg) {
		tmp = var_without_subpkg;
	}
	for (size_t i = 0; i < nitems(variable_order_); i++) {
		switch (variable_order_[i].block) {
		case BLOCK_FLAVORS_HELPER:
		case BLOCK_OPTHELPER:
		case BLOCK_OPTDESC:
			continue;
		case BLOCK_LICENSE:
		case BLOCK_OPTDEF:
			// RE_LICENSE_*, RE_OPTIONS_GROUP do not
			// cover all cases.
		default:
			break;
		}
		if (strcmp(tmp, variable_order_[i].var) == 0) {
			free(var_without_subpkg);
			return variable_order_[i].block;
		}
	}
	free(var_without_subpkg);

	return BLOCK_UNKNOWN;
}

int
compare_order(const void *ap, const void *bp, void *userdata)
{
	struct Parser *parser = userdata;
	const char *a = *(const char **)ap;
	const char *b = *(const char **)bp;

	if (strcmp(a, b) == 0) {
		return 0;
	}
	enum BlockType ablock = variable_order_block(parser, a);
	enum BlockType bblock = variable_order_block(parser, b);
	if (ablock < bblock) {
		return -1;
	} else if (ablock > bblock) {
		return 1;
	}

	if (ablock == BLOCK_LICENSE) {
		int ascore = -1;
		int bscore = -1;
		for (size_t i = 0; i < nitems(variable_order_); i++) {
			if (variable_order_[i].block != BLOCK_LICENSE) {
				continue;
			}
			if (strcmp(variable_order_[i].var, "LICENSE") == 0) {
				continue;
			}
			if (str_startswith(a, variable_order_[i].var)) {
				ascore = i;
			}
			if (str_startswith(b, variable_order_[i].var)) {
				bscore = i;
			}
		}
		if (ascore < bscore) {
			return -1;
		} else if (ascore > bscore) {
			return 1;
		}
	} else if (ablock == BLOCK_FLAVORS_HELPER) {
		int ascore = -1;
		int bscore = -1;

		char *ahelper = NULL;
		char *aprefix = NULL;
		char *bhelper = NULL;
		char *bprefix = NULL;
		if (!is_flavors_helper(parser, a, &aprefix, &ahelper) ||
		    !is_flavors_helper(parser, b, &bprefix, &bhelper)) {
			abort();
		}
		assert(ahelper != NULL && aprefix != NULL);
		assert(bhelper != NULL && bprefix != NULL);

		// Only compare if common prefix (helper for the same flavor)
		int prefix_score = strcmp(aprefix, bprefix);
		if (prefix_score == 0) {
			for (size_t i = 0; i < nitems(variable_order_); i++) {
				if (variable_order_[i].block != BLOCK_FLAVORS_HELPER) {
					continue;
				}
				if (strcmp(ahelper, variable_order_[i].var) == 0) {
					ascore = i;
				}
				if (strcmp(bhelper, variable_order_[i].var) == 0) {
					bscore = i;
				}
			}
		}

		free(aprefix);
		free(ahelper);
		free(bprefix);
		free(bhelper);

		if (prefix_score != 0) {
			return prefix_score;
		} else if (ascore < bscore) {
			return -1;
		} else if (ascore > bscore) {
			return 1;
		} else {
			return strcmp(a, b);
		}
	} else if (ablock == BLOCK_OPTDESC) {
		return strcmp(a, b);
	} else if (ablock == BLOCK_OPTHELPER) {
		int ascore = -1;
		int bscore = -1;

		char *ahelper = NULL;
		char *aprefix = NULL;
		char *bhelper = NULL;
		char *bprefix = NULL;
		// TODO SUBPKG
		if (!is_options_helper(parser, a, &aprefix, &ahelper, NULL) ||
		    !is_options_helper(parser, b, &bprefix, &bhelper, NULL)) {
			abort();
		}
		assert(ahelper != NULL && aprefix != NULL);
		assert(bhelper != NULL && bprefix != NULL);

		// Only compare if common prefix (helper for the same option)
		int prefix_score = strcmp(aprefix, bprefix);
		if (prefix_score == 0) {
			for (size_t i = 0; i < nitems(variable_order_); i++) {
				if (variable_order_[i].block != BLOCK_OPTHELPER) {
					continue;
				}
				if (strcmp(ahelper, variable_order_[i].var) == 0) {
					ascore = i;
				}
				if (strcmp(bhelper, variable_order_[i].var) == 0) {
					bscore = i;
				}
			}
		}

		free(aprefix);
		free(ahelper);
		free(bprefix);
		free(bhelper);

		if (prefix_score != 0) {
			return prefix_score;
		} else if (ascore < bscore) {
			return -1;
		} else if (ascore > bscore) {
			return 1;
		} else {
			return strcmp(a, b);
		}
	} else if (ablock == BLOCK_OPTDEF) {
		int ascore = -1;
		int bscore = -1;

		for (size_t i = 0; i < nitems(variable_order_); i++) {
			if (variable_order_[i].block != BLOCK_OPTDEF) {
				continue;
			}
			if (str_startswith(a, variable_order_[i].var)) {
				ascore = i;
			}
			if (str_startswith(b, variable_order_[i].var)) {
				bscore = i;
			}
		}

		if (ascore < bscore) {
			return -1;
		} else if (ascore > bscore) {
			return 1;
		} else {
			return strcmp(a, b);
		}
	}

	char *asubpkg = NULL;
	char *a_without_subpkg = extract_subpkg(parser, a, &asubpkg);
	if (a_without_subpkg == NULL) {
		a_without_subpkg = xstrdup(a);
	}
	char *bsubpkg = NULL;
	char *b_without_subpkg = extract_subpkg(parser, b, &bsubpkg);
	if (b_without_subpkg == NULL) {
		b_without_subpkg = xstrdup(b);
	}
	int ascore = -1;
	int bscore = -1;
	for (size_t i = 0; i < nitems(variable_order_) && (ascore == -1 || bscore == -1); i++) {
		if (strcmp(a_without_subpkg, variable_order_[i].var) == 0) {
			ascore = i;
		}
		if (strcmp(b_without_subpkg, variable_order_[i].var) == 0) {
			bscore = i;
		}
	}

	int retval = 0;
	if (strcmp(a_without_subpkg, b_without_subpkg) == 0 && asubpkg && bsubpkg) {
		retval = strcmp(asubpkg, bsubpkg);
	} else if (asubpkg && !bsubpkg) {
		retval = 1;
	} else if (!asubpkg && bsubpkg) {
		retval = -1;
	} else if (ascore < bscore) {
		retval = -1;
	} else if (ascore > bscore) {
		retval = 1;
	} else {
		retval = strcmp(a_without_subpkg, b_without_subpkg);
	}
	free(a_without_subpkg);
	free(asubpkg);
	free(b_without_subpkg);
	free(bsubpkg);

	return retval;
}

void
target_extract_opt(struct Parser *parser, const char *target, char **target_out, char **opt_out, int *state)
{
	*opt_out = NULL;
	*target_out = NULL;
	*state = 0;

	int on;
	if ((on = str_endswith(target, "-on")) || str_endswith(target, "-off")) {
		const char *p = target;
		for (; *p == '-' || (islower(*p) && isalpha(*p)); p++);
		if (on) {
			*opt_out = xstrndup(p, strlen(p) - strlen("-on"));
			*state = 1;
		} else {
			*opt_out = xstrndup(p, strlen(p) - strlen("-off"));
			*state = 0;
		}
		char *tmp;
		xasprintf(&tmp, "%s_USES", *opt_out);
		if (is_options_helper(parser, tmp, NULL, NULL, NULL)) {
			*target_out = xstrndup(target, strlen(target) - strlen(p) - 1);
		} else {
			*target_out = xstrdup(target);
			free(*opt_out);
			*opt_out = NULL;
		}
		free(tmp);
	} else {
		*target_out = xstrdup(target);
	}
}

int
is_known_target(struct Parser *parser, const char *target)
{
	char *root, *opt;
	int state;
	target_extract_opt(parser, target, &root, &opt, &state);
	free(opt);

	for (size_t i = 0; i < nitems(target_order_); i++) {
		if (strcmp(target_order_[i], root) == 0) {
			free(root);
			return 1;
		}
	}

	free(root);
	return 0;
}

int
is_special_target(const char *target)
{
	for (size_t i = 0; i < nitems(special_targets_); i++) {
		if (strcmp(target, special_targets_[i]) == 0) {
			return 1;
		}
	}

	return 0;
}

int
compare_target_order(const void *ap, const void *bp, void *userdata)
{
	struct Parser *parser = userdata;
	int retval = 0;
	const char *a_ = *(const char **)ap;
	const char *b_ = *(const char **)bp;

	if (strcmp(a_, b_) == 0) {
		return 0;
	}

	char *a, *b, *aopt, *bopt;
	int aoptstate, boptstate;
	target_extract_opt(parser, a_, &a, &aopt, &aoptstate);
	target_extract_opt(parser, b_, &b, &bopt, &boptstate);

	ssize_t aindex = -1;
	ssize_t bindex = -1;
	for (size_t i = 0; i < nitems(target_order_) && (aindex == -1 || bindex == -1); i++) {
		if (aindex == -1 && strcmp(target_order_[i], a) == 0) {
			aindex = i;
		}
		if (bindex == -1 && strcmp(target_order_[i], b) == 0) {
			bindex = i;
		}
	}

	if (aindex == -1) {
		retval = 1;
		goto cleanup;
	} else if (bindex == -1) {
		retval = -1;
		goto cleanup;
	} else if (aindex == bindex) {
		if (aopt == NULL) {
			retval = -1;
			goto cleanup;
		}
		if (bopt == NULL) {
			retval = 1;
			goto cleanup;
		}

		int c = strcmp(aopt, bopt);
		if (c < 0) {
			retval = -1;
			goto cleanup;
		} else if (c > 0) {
			retval = 1;
			goto cleanup;
		}

		if (aoptstate && !boptstate) {
			retval = 1;
			goto cleanup;
		} else if (!aoptstate && boptstate) {
			retval = -1;
			goto cleanup;
		} else {
			// should not happen
			abort();
		}
	} else if (aindex < bindex) {
		retval = -1;
		goto cleanup;
	} else if (aindex > bindex) {
		retval = 1;
		goto cleanup;
	} else {
		// should not happen
		abort();
	}

cleanup:
	free(a);
	free(aopt);
	free(b);
	free(bopt);

	return retval;
}

const char *
blocktype_tostring(enum BlockType block)
{
	switch (block) {
	case BLOCK_APACHE:
		return "USES=apache related variables";
	case BLOCK_BROKEN:
		return "BROKEN/IGNORE/DEPRECATED messages";
	case BLOCK_CABAL:
		return "USES=cabal related variables";
	case BLOCK_CARGO:
		return "USES=cargo related variables";
	case BLOCK_CFLAGS:
		return "CFLAGS/CXXFLAGS/LDFLAGS block";
	case BLOCK_CMAKE:
		return "USES=cmake related variables";
	case BLOCK_CONFIGURE:
		return "Configure block";
	case BLOCK_CONFLICTS:
		return "Conflicts";
	case BLOCK_DEPENDS:
		return "Dependencies";
	case BLOCK_ELIXIR:
		return "USES=elixir related variables";
	case BLOCK_EMACS:
		return "USES=emacs related variables";
	case BLOCK_ERLANG:
		return "USES=erlang related variables";
	case BLOCK_FLAVORS:
		return "Flavors";
	case BLOCK_FLAVORS_HELPER:
		return "Flavors helpers";
	case BLOCK_GO:
		return "USES=go related variables";
	case BLOCK_LAZARUS:
		return "USES=lazarus related variables";
	case BLOCK_LICENSE:
		return "License block";
	case BLOCK_LICENSE_OLD:
		return "Old-school license block (please replace with LICENSE)";
	case BLOCK_LINUX:
		return "USES=linux related variables";
	case BLOCK_MAINTAINER:
		return "Maintainer block";
	case BLOCK_MAKE:
		return "Make block";
	case BLOCK_MESON:
		return "USES=meson related variables";
	case BLOCK_NUGET:
		return "USES=mono related variables";
	case BLOCK_OPTDEF:
		return "Options definitions";
	case BLOCK_OPTDESC:
		return "Options descriptions";
	case BLOCK_OPTHELPER:
		return "Options helpers";
	case BLOCK_PATCHFILES:
		return "Patch files";
	case BLOCK_PLIST:
		return "Packaging list block";
	case BLOCK_PORTNAME:
		return "PORTNAME block";
	case BLOCK_QMAKE:
		return "USES=qmake related variables";
	case BLOCK_SCONS:
		return "USES=scons related variables";
	case BLOCK_SHEBANGFIX:
		return "USES=shebangfix related variables";
	case BLOCK_STANDARD:
		return "Standard bsd.port.mk variables";
#if PORTFMT_SUBPACKAGES
	case BLOCK_SUBPACKAGES:
		return "Subpackages block";
#endif
	case BLOCK_UNIQUEFILES:
		return "USES=uniquefiles block";
	case BLOCK_UNKNOWN:
		return "Unknown variables";
	case BLOCK_USERS:
		return "Users and groups block";
	case BLOCK_USES:
		return "USES block";
	case BLOCK_WRKSRC:
		return "WRKSRC block";
	}

	abort();
}

int
target_command_wrap_after_each_token(const char *command)
{
	if (*command == '@') {
		command++;
	}
	for (size_t i = 0; i < nitems(target_command_wrap_after_each_token_); i++) {
		if (strcmp(command, target_command_wrap_after_each_token_[i]) == 0) {
			return 1;
		}
	}
	return 0;
}

int
target_command_should_wrap(const char *word)
{
	if (strcmp(word, "&&") == 0 ||
	    strcmp(word, "||") == 0 ||
	    strcmp(word, "then") == 0 ||
	    (str_endswith(word, ";") && !str_endswith(word, "\\;")) ||
	    strcmp(word, "|") == 0) {
		return 1;
	}

	return 0;
}

regex_t *
regex(enum RegularExpression re)
{
	return &regular_expressions[re].re;
}

int
matches(enum RegularExpression re, const char *s)
{
	return regexec(&regular_expressions[re].re, s, 0, NULL, 0) == 0;
}

char *
sub(enum RegularExpression re, const char *replacement, const char *s)
{
	assert(replacement != NULL);
	assert(s != NULL);

	size_t len = strlen(replacement) + strlen(s) + 1;
	char *buf = xmalloc(len);
	buf[0] = 0;

	regmatch_t pmatch[1];
	if (regexec(&regular_expressions[re].re, s, 1, pmatch, 0) == 0) {
		strncpy(buf, s, pmatch[0].rm_so);
		xstrlcat(buf, replacement, len);
		strncat(buf, s + pmatch[0].rm_eo, strlen(s) - pmatch[0].rm_eo);
	} else {
		xstrlcat(buf, s, len);
	}

	return buf;
}

void
rules_init()
{
	if (rules_initialized) {
		return;
	}

	for (size_t i = 0; i < nitems(regular_expressions); i++) {
		const char *pattern;
		pattern = regular_expressions[i].pattern;

		int error = regcomp(&regular_expressions[i].re, pattern,
				    regular_expressions[i].flags);
		if (error != 0) {
			size_t errbuflen = regerror(error, &regular_expressions[i].re, NULL, 0);
			char *errbuf = xmalloc(errbuflen);
			regerror(error, &regular_expressions[i].re, errbuf, errbuflen);
			errx(1, "regcomp: %zu: %s", i, errbuf);
		}
	}

	rules_initialized = 1;
}
