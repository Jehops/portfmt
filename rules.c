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
#include <string.h>

#include "rules.h"
#include "util.h"
#include "variable.h"

static int compare_rel(const char *[], size_t, const char *, const char *);
static int compare_license_perms(struct Variable *, const char *, const char *, int *);
static int compare_plist_files(struct Variable *, const char *, const char *, int *);
static int compare_use_pyqt(struct Variable *, const char *, const char *, int *);
static int compare_use_qt(struct Variable *, const char *, const char *, int *);
static char *options_helpers_pattern(void);

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
	[RE_CONTINUE_LINE]    = { "[^\\\\]\\\\$", REG_EXTENDED, {} },
	[RE_EMPTY_LINE]       = { "^[[:space:]]*$", 			      0, {} },
	[RE_LICENSE_NAME]     = { "^(_?(-|LICENSE_NAME_[A-Za-z0-9._+ ])+|"
				  "^LICENSE_(FILE|NAME)_|"
				  "^LICENSE_(NAME|TEXT)$|"
				  "_?(-|LICENSE_TEXT_[A-Za-z0-9._+ ])+$)",
				  REG_EXTENDED, {} },
	[RE_LICENSE_PERMS]    = { "^(_?LICENSE_PERMS_(-|[A-Z0-9\\._+ ])+[+?:]?|"
				  "_LICENSE_LIST_PERMS[+?:]?|"
				  "LICENSE_PERMS[+?:]?)",
				  REG_EXTENDED, {} },
	[RE_OPTIONS_HELPER]   = { "generated in compile_regular_expressions", REG_EXTENDED, {} },
	[RE_OPT_USE_PREFIX]   = { "^[A-Za-z0-9_]+\\+?=", REG_EXTENDED, {} },
	[RE_OPT_USE]	      = { "^[A-Z0-9_]+_USE$", REG_EXTENDED, {} },
	[RE_OPT_VARS]	      = { "^[A-Z0-9_]+_VARS$", REG_EXTENDED, {} },
	[RE_PLIST_FILES]      = { "^([A-Z0-9_]+_PLIST_DIRS[+?:]?|"
				  "[A-Z0-9_]+_PLIST_FILES[+?:]?|"
				  "PLIST_FILES[+?:]?|"
				  "PLIST_DIRS[+?:]?)",
				  REG_EXTENDED, {} },
	[RE_PLIST_KEYWORDS]   = { "^\"@([a-z]|-)+ ",			      REG_EXTENDED, {} },
	[RE_MODIFIER]	      = { "[:!?+]?=$",				      REG_EXTENDED, {} },
	[RE_TARGET] 	      = { "^([^:]|[^[:space:]])+::?(\\.|[[:space:]]+|$)", REG_EXTENDED, {} },
	[RE_VAR] 	      = { "^ *[^[:space:]=]+[[:space:]]*[+!?:]?=",	      REG_EXTENDED, {} },
};

static const char *print_as_newlines_[] = {
	"BUILD_DEPENDS",
	"CARGO_CRATES",
	"CARGO_ENV",
	"CMAKE_ARGS",
	"CMAKE_BOOL",
	"CO_ENV",
	"CONFIGURE_ARGS",
	"CONFIGURE_ENV",
	"CONFIGURE_OFF",
	"CONFIGURE_ON",
	"D4P_ENV",
	"DESKTOP_ENTRIES",
	"DEV_ERROR",
	"DEV_WARNING",
	"DISTFILES",
	"EXTRACT_DEPENDS",
	"FETCH_DEPENDS",
	"GH_TUPLE",
	"GLIB_SCHEMAS",
	"GN_ARGS",
	"GO_ENV",
	"LIB_DEPENDS",
	"MAKE_ARGS",
	"MAKE_ENV",
	"MASTER_SITES_ABBREVS",
	"MASTER_SITES_SUBDIRS",
	"MASTER_SITES",
	"MESON_ARGS",
	"MOZ_OPTIONS",
	"PATCH_DEPENDS",
	"PKG_DEPENDS",
	"PKG_ENV",
	"PLIST_FILES",
	"PLIST_SUB",
	"QA_ENV",
	"RUN_DEPENDS",
	"SUB_LIST",
	"SUBDIR",
	"TEST_ARGS",
	"TEST_DEPENDS",
	"VARS",
};

static const char *options_helpers_[] = {
	// _OPTIONS_FLAGS
	"ALL_TARGET",
	"BINARY_ALIAS",
	"BROKEN",
	"CATEGORIES",
	"CFLAGS",
	"CONFIGURE_ENV",
	"CONFLICTS",
	"CONFLICTS_BUILD",
	"CONFLICTS_INSTALL",
	"CPPFLAGS",
	"CXXFLAGS",
	"DESC",
	"DESKTOP_ENTRIES",
	"DISTFILES",
	"EXTRA_PATCHES",
	"EXTRACT_ONLY",
	"GH_ACCOUNT",
	"GH_PROJECT",
	"GH_SUBDIR",
	"GH_TAGNAME",
	"GH_TUPLE",
	"GL_ACCOUNT",
	"GL_COMMIT",
	"GL_PROJECT",
	"GL_SITE",
	"GL_SUBDIR",
	"GL_TUPLE",
	"IGNORE",
	"INFO",
	"INSTALL_TARGET",
	"LDFLAGS",
	"LIBS",
	"MAKE_ARGS",
	"MAKE_ENV",
	"MASTER_SITES",
	"PATCH_SITES",
	"PATCHFILES",
	"PLIST_DIRS",
	"PLIST_FILES",
	"PLIST_SUB",
	"PORTDOCS",
	"PORTEXAMPLES",
	"SUB_FILES",
	"SUB_LIST",
	"TEST_TARGET",
	"USES",

	// _OPTIONS_DEPENDS
	"PKG_DEPENDS",
	"FETCH_DEPENDS",
	"EXTRACT_DEPENDS",
	"PATCH_DEPENDS",
	"BUILD_DEPENDS",
	"LIB_DEPENDS",
	"RUN_DEPENDS",
	"TEST_DEPENDS",

	// Other special options helpers
	"USE",
	"VARS",

	// Add _OFF variants of the above
	"ALL_TARGET_OFF",
	"BINARY_ALIAS_OFF",
	"BROKEN_OFF",
	"CATEGORIES_OFF",
	"CFLAGS_OFF",
	"CONFIGURE_ENV_OFF",
	"CONFLICTS_OFF",
	"CONFLICTS_BUILD_OFF",
	"CONFLICTS_INSTALL_OFF",
	"CPPFLAGS_OFF",
	"CXXFLAGS_OFF",
	"DESKTOP_ENTRIES_OFF",
	"DISTFILES_OFF",
	"EXTRA_PATCHES_OFF",
	"EXTRACT_ONLY_OFF",
	"GH_ACCOUNT_OFF",
	"GH_PROJECT_OFF",
	"GH_SUBDIR_OFF",
	"GH_TAGNAME_OFF",
	"GH_TUPLE_OFF",
	"GL_ACCOUNT_OFF",
	"GL_COMMIT_OFF",
	"GL_PROJECT_OFF",
	"GL_SITE_OFF",
	"GL_SUBDIR_OFF",
	"GL_TUPLE_OFF",
	"IGNORE_OFF",
	"INFO_OFF",
	"INSTALL_TARGET_OFF",
	"LDFLAGS_OFF",
	"LIBS_OFF",
	"MAKE_ARGS_OFF",
	"MAKE_ENV_OFF",
	"MASTER_SITES_OFF",
	"PATCH_SITES_OFF",
	"PATCHFILES_OFF",
	"PLIST_DIRS_OFF",
	"PLIST_FILES_OFF",
	"PLIST_SUB_OFF",
	"PORTDOCS_OFF",
	"PORTEXAMPLES_OFF",
	"SUB_FILES_OFF",
	"SUB_LIST_OFF",
	"TEST_TARGET_OFF",
	"USES_OFF",
	"PKG_DEPENDS_OFF",
	"FETCH_DEPENDS_OFF",
	"EXTRACT_DEPENDS_OFF",
	"PATCH_DEPENDS_OFF",
	"BUILD_DEPENDS_OFF",
	"LIB_DEPENDS_OFF",
	"RUN_DEPENDS_OFF",
	"TEST_DEPENDS_OFF",
	"USE_OFF",
	"VARS_OFF",

	// Other irregular helpers
	"CONFIGURE_ENABLE",
	"CONFIGURE_WITH",
	"CMAKE_BOOL",
	"CMAKE_BOOL_OFF",
	"CMAKE_ON",
	"CMAKE_OFF",
	"DESC",
	"MESON_DISABLED",
	"MESON_ENABLED",
	"MESON_TRUE",
	"MESON_FALSE",
	"MESON_YES",
	"MESON_NO",
	"CONFIGURE_ON",
	"MESON_ON",
	"QMAKE_ON",
	"CONFIGURE_OFF",
	"MESON_OFF",
	"QMAKE_OFF",
	"CABAL_FLAGS",
	"EXECUTABLES",
	"USE_CABAL",
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
	"widgets_run",
	"xml_run",
	"xmlpatterns_run",
};

// Sanitize whitespace but do *not* sort tokens; more complicated
// patterns below in leave_unsorted()
static const char *leave_unsorted_[] = {
	"_ALL_EXCLUDE",
	"_BUILD_SEQ",
	"_BUILD_SETUP",
	"_CARGO_GIT_PATCH_CARGOTOML",
	"_CONFIGURE_SEQ",
	"_DEPENDS-LIST",
	"_EXTRACT_SEQ",
	"_FETCH_SEQ",
	"_FLAVOR_RECURSIVE_SH",
	"_LICENSE_TEXT",
	"_OPTIONS_DEPENDS",
	"_OPTIONS_TARGETS",
	"_PACKAGE_SEQ",
	"_PATCH_SEQ",
	"_PATCHFILES",
	"_PATCHFILES2",
	"_PKG_SEQ",
	"_PKGTOOLSDEFINED",
	"_PORTS_DIRECTORIES",
	"_PORTSEARCH",
	"_PRETTY_PRINT_DEPENDS_LIST",
	"_RANDOMIZE_SITES",
	"_REALLY_ALL_POSSIBLE_OPTIONS",
	"_SANITY_SEQ",
	"_STAGE_SEQ",
	"_TARGETS_STAGES",
	"_TARGETS",
	"_TEST_SEQ",
	"_tmp_seq",
	"_UNIFIED_DEPENDS",
	"_UNSUPPORTED_SYSTEM_MESSAGE",
	"ALL_NOTNEEDED",
	"ALL_TARGET",
	"ALL_UNSUPPORTED",
	"ALL-DEPENDS-FLAVORS-LIST",
	"ALL-DEPENDS-LIST",
	"AWK",
	"BASENAME",
	"BRANDELF",
	"BROKEN_aarch64",
	"BROKEN_amd64",
	"BROKEN_armv6",
	"BROKEN_armv7",
	"BROKEN_DragonFly",
	"BROKEN_FreeBSD_11_aarch64",
	"BROKEN_FreeBSD_11_amd64",
	"BROKEN_FreeBSD_11_armv6",
	"BROKEN_FreeBSD_11_armv7",
	"BROKEN_FreeBSD_11_i386",
	"BROKEN_FreeBSD_11_mips",
	"BROKEN_FreeBSD_11_mips64",
	"BROKEN_FreeBSD_11_powerpc",
	"BROKEN_FreeBSD_11_powerpc64",
	"BROKEN_FreeBSD_11_sparc64",
	"BROKEN_FreeBSD_11",
	"BROKEN_FreeBSD_12_aarch64",
	"BROKEN_FreeBSD_12_amd64",
	"BROKEN_FreeBSD_12_armv6",
	"BROKEN_FreeBSD_12_armv7",
	"BROKEN_FreeBSD_12_i386",
	"BROKEN_FreeBSD_12_mips",
	"BROKEN_FreeBSD_12_mips64",
	"BROKEN_FreeBSD_12_powerpc",
	"BROKEN_FreeBSD_12_powerpc64",
	"BROKEN_FreeBSD_12_sparc64",
	"BROKEN_FreeBSD_12",
	"BROKEN_FreeBSD_13_aarch64",
	"BROKEN_FreeBSD_13_amd64",
	"BROKEN_FreeBSD_13_armv6",
	"BROKEN_FreeBSD_13_armv7",
	"BROKEN_FreeBSD_13_i386",
	"BROKEN_FreeBSD_13_mips",
	"BROKEN_FreeBSD_13_mips64",
	"BROKEN_FreeBSD_13_powerpc",
	"BROKEN_FreeBSD_13_powerpc64",
	"BROKEN_FreeBSD_13_sparc64",
	"BROKEN_FreeBSD_13",
	"BROKEN_FreeBSD_aarch64",
	"BROKEN_FreeBSD_amd64",
	"BROKEN_FreeBSD_armv6",
	"BROKEN_FreeBSD_armv7",
	"BROKEN_FreeBSD_i386",
	"BROKEN_FreeBSD_mips",
	"BROKEN_FreeBSD_mips64",
	"BROKEN_FreeBSD_powerpc",
	"BROKEN_FreeBSD_powerpc64",
	"BROKEN_FreeBSD_sparc64",
	"BROKEN_FreeBSD",
	"BROKEN_i386",
	"BROKEN_mips",
	"BROKEN_mips64",
	"BROKEN_powerpc",
	"BROKEN_powerpc64",
	"BROKEN_sparc64",
	"BROKEN",
	"BSDMAKE",
	"BUILD_FAIL_MESSAGE",
	"BUILD-DEPENDS-LIST",
	"BZCAT",
	"BZIP2_CMD",
	"CARGO_CARGO_RUN",
	"CARGO_CRATES",
	"CARGO_FEATURES",
	"CAT",
	"CATEGORIES",
	"CC",
	"CHGRP",
	"CHMOD",
	"CHOWN",
	"CHROOT",
	"CLEAN-DEPENDS-LIMITED-LIST",
	"CLEAN-DEPENDS-LIST",
	"COMM",
	"COMMENT",
	"COPYTREE_BIN",
	"COPYTREE_SHARE",
	"CP",
	"CPIO",
	"CPP",
	"CUT",
	"CXX",
	"DAEMONARGS",
	"DC",
	"DEBUG_MSG",
	"DEPENDS-LIST",
	"DEPRECATED",
	"DESC",
	"DESKTOP_ENTRIES",
	"DIALOG",
	"DIALOG4PORTS",
	"DIFF",
	"DIRNAME",
	"DO_MAKE_BUILD",
	"DO_MAKE_TEST",
	"ECHO_CMD",
	"ECHO_MSG",
	"EGREP",
	"EXPIRATION_DATE",
	"EXPR",
	"EXTRA_PATCHES",
	"EXTRACT_AFTER_ARGS",
	"EXTRACT_BEFORE_ARGS",
	"FALSE",
	"FETCH_AFTER_ARGS",
	"FETCH_ARGS",
	"FETCH_BEFORE_ARGS",
	"FETCH_LIST",
	"FETCH_LIST",
	"FILE",
	"FIND",
	"FLAVORS",
	"FLEX",
	"FMT_80",
	"FMT",
	"GH_TUPLE",
	"GMAKE",
	"GREP",
	"GUNZIP_CMD",
	"GZCAT",
	"GZIP_CMD",
	"GZIP",
	"HEAD",
	"HTMLIFY",
	"ID",
	"IDENT",
	"IGNORE_aarch64",
	"IGNORE_amd64",
	"IGNORE_armv6",
	"IGNORE_armv7",
	"IGNORE_DragonFly",
	"IGNORE_FreeBSD_11_aarch64",
	"IGNORE_FreeBSD_11_aarch64",
	"IGNORE_FreeBSD_11_amd64",
	"IGNORE_FreeBSD_11_armv6",
	"IGNORE_FreeBSD_11_armv7",
	"IGNORE_FreeBSD_11_i386",
	"IGNORE_FreeBSD_11_mips",
	"IGNORE_FreeBSD_11_mips64",
	"IGNORE_FreeBSD_11_powerpc",
	"IGNORE_FreeBSD_11_powerpc64",
	"IGNORE_FreeBSD_11_sparc64",
	"IGNORE_FreeBSD_11",
	"IGNORE_FreeBSD_12_aarch64",
	"IGNORE_FreeBSD_12_amd64",
	"IGNORE_FreeBSD_12_armv6",
	"IGNORE_FreeBSD_12_armv7",
	"IGNORE_FreeBSD_12_i386",
	"IGNORE_FreeBSD_12_mips",
	"IGNORE_FreeBSD_12_mips64",
	"IGNORE_FreeBSD_12_powerpc",
	"IGNORE_FreeBSD_12_powerpc64",
	"IGNORE_FreeBSD_12_sparc64",
	"IGNORE_FreeBSD_12",
	"IGNORE_FreeBSD_13_aarch64",
	"IGNORE_FreeBSD_13_amd64",
	"IGNORE_FreeBSD_13_armv6",
	"IGNORE_FreeBSD_13_armv7",
	"IGNORE_FreeBSD_13_i386",
	"IGNORE_FreeBSD_13_mips",
	"IGNORE_FreeBSD_13_mips64",
	"IGNORE_FreeBSD_13_powerpc",
	"IGNORE_FreeBSD_13_sparc64",
	"IGNORE_FreeBSD_13",
	"IGNORE_FreeBSD_aarch64",
	"IGNORE_FreeBSD_amd64",
	"IGNORE_FreeBSD_armv6",
	"IGNORE_FreeBSD_armv7",
	"IGNORE_FreeBSD_i386",
	"IGNORE_FreeBSD_mips",
	"IGNORE_FreeBSD_mips64",
	"IGNORE_FreeBSD_powerpc",
	"IGNORE_FreeBSD_sparc64",
	"IGNORE_FreeBSD",
	"IGNORE_i386",
	"IGNORE_mips",
	"IGNORE_mips64",
	"IGNORE_powerpc",
	"IGNORE_powerpc64",
	"IGNORE_sparc64",
	"IGNORE",
	"IGNORECMD",
	"IGNOREDIR",
	"INSTALL_DATA",
	"INSTALL_KLD",
	"INSTALL_LIB",
	"INSTALL_MAN",
	"INSTALL_PROGRAM",
	"INSTALL_SCRIPT",
	"INSTALL_TARGET",
	"intlhack_PRE_PATCH",
	"JOT",
	"LDCONFIG",
	"LHA_CMD",
	"LIBS",
	"LICENSE_NAME",
	"LICENSE_TEXT",
	"LN",
	"LS",
	"MAKE_JOBS_UNSAFE",
	"MANUAL_PACKAGE_BUILD",
	"MASTER_SITES",
	"MASTER_SORT_AWK",
	"MD5",
	"MISSING-DEPENDS-LIST",
	"MKDIR",
	"MKTEMP",
	"MOUNT_DEVFS",
	"MOUNT_NULLFS",
	"MOUNT",
	"MOZ_SED_ARGS",
	"MOZCONFIG_SED",
	"MTREE_ARGS",
	"MULTI_EOL",
	"MV",
	"NO_CCACHE",
	"NO_CDROM",
	"NO_PACKAGE",
	"OBJCOPY",
	"OBJDUMP",
	"PASTE",
	"PATCH_ARGS",
	"PATCH_DIST_ARGS",
	"PAX",
	"PKG_ADD",
	"PKG_BIN",
	"PKG_CREATE",
	"PKG_DELETE",
	"PKG_INFO",
	"PKG_QUERY",
	"PKG_REGISTER",
	"PKG_VERSION",
	"PRINTF",
	"PS_CMD",
	"PW",
	"RADIO_EOL",
	"RANDOM_ARGS",
	"READELF",
	"REALPATH",
	"referencehack_PRE_PATCH",
	"REINPLACE_ARGS",
	"RESTRICTED",
	"RLN",
	"RM",
	"RMDIR",
	"RUBY_CONFIG",
	"RUN-DEPENDS-LIST",
	"SANITY_DEPRECATED",
	"SANITY_NOTNEEDED",
	"SANITY_UNSUPPORTED",
	"SED",
	"SET_LATE_CONFIGURE_ARGS",
	"SETENV",
	"SH",
	"SHA256",
	"SINGLE_EOL",
	"SOELIM",
	"SORT",
	"STAT",
	"STRIP_CMD",
	"SU_CMD",
	"SYSCTL",
	"TAIL",
	"TEST_TARGET",
	"TEST-DEPENDS-LIST",
	"TEST",
	"TEX_FORMAT_LUATEX",
	"TEXHASHDIRS",
	"TR",
	"TRUE",
	"UMOUNT",
	"UNAME",
	"UNMAKESELF_CMD",
	"UNZIP_CMD",
	"UNZIP_NATIVE_CMD",
	"WARNING",
	"WHICH",
	"XARGS",
	"XMKMF",
	"XZ_CMD",
	"XZ",
	"XZCAT",
	"YACC",
};

// Don't indent with the rest of the variables in a paragraph
static const char *skip_goalcol_[] = {
	"CARGO_CRATES",
	"DISTFILES_amd64",
	"DISTFILES_i386",
	"DISTVERSIONPREFIX",
	"DISTVERSIONSUFFIX",
	"EXPIRATION_DATE",
	"EXTRACT_AFTER_ARGS",
	"EXTRACT_BEFORE_ARGS",
	"FETCH_AFTER_ARGS",
	"FETCH_BEFORE_ARGS",
	"MAKE_JOBS_UNSAFE",
	"MASTER_SITE_SUBDIR",
	"ONLY_FOR_ARCHS_REASON",
	"ONLY_FOR_ARCHS_REASON_aarch64",
	"ONLY_FOR_ARCHS_REASON_amd64",
	"ONLY_FOR_ARCHS_REASON_armv6",
	"ONLY_FOR_ARCHS_REASON_armv7",
	"ONLY_FOR_ARCHS_REASON_i386",
	"ONLY_FOR_ARCHS_REASON_mips",
	"ONLY_FOR_ARCHS_REASON_mips64",
	"ONLY_FOR_ARCHS_REASON_powerpc",
	"ONLY_FOR_ARCHS_REASON_powerpc64",
	"ONLY_FOR_ARCHS_REASON_sparc64",
};

/* Lines that are best not wrapped to 80 columns
 * especially don't wrap BROKEN and IGNORE with \ or it introduces
 * some spurious extra spaces when the message is displayed to users
 */

static const char *ignore_wrap_col_[] = {
	"BROKEN_aarch64",
	"BROKEN_amd64",
	"BROKEN_armv6",
	"BROKEN_armv7",
	"BROKEN_DragonFly",
	"BROKEN_FreeBSD_11_aarch64",
	"BROKEN_FreeBSD_11_amd64",
	"BROKEN_FreeBSD_11_armv6",
	"BROKEN_FreeBSD_11_armv7",
	"BROKEN_FreeBSD_11_i386",
	"BROKEN_FreeBSD_11_mips",
	"BROKEN_FreeBSD_11_mips64",
	"BROKEN_FreeBSD_11_powerpc",
	"BROKEN_FreeBSD_11_powerpc64",
	"BROKEN_FreeBSD_11_sparc64",
	"BROKEN_FreeBSD_11",
	"BROKEN_FreeBSD_12_aarch64",
	"BROKEN_FreeBSD_12_amd64",
	"BROKEN_FreeBSD_12_armv6",
	"BROKEN_FreeBSD_12_armv7",
	"BROKEN_FreeBSD_12_i386",
	"BROKEN_FreeBSD_12_mips",
	"BROKEN_FreeBSD_12_mips64",
	"BROKEN_FreeBSD_12_powerpc",
	"BROKEN_FreeBSD_12_powerpc64",
	"BROKEN_FreeBSD_12_sparc64",
	"BROKEN_FreeBSD_12",
	"BROKEN_FreeBSD_13_aarch64",
	"BROKEN_FreeBSD_13_amd64",
	"BROKEN_FreeBSD_13_armv6",
	"BROKEN_FreeBSD_13_armv7",
	"BROKEN_FreeBSD_13_i386",
	"BROKEN_FreeBSD_13_mips",
	"BROKEN_FreeBSD_13_mips64",
	"BROKEN_FreeBSD_13_powerpc",
	"BROKEN_FreeBSD_13_powerpc64",
	"BROKEN_FreeBSD_13_sparc64",
	"BROKEN_FreeBSD_13",
	"BROKEN_FreeBSD_aarch64",
	"BROKEN_FreeBSD_amd64",
	"BROKEN_FreeBSD_armv6",
	"BROKEN_FreeBSD_armv7",
	"BROKEN_FreeBSD_i386",
	"BROKEN_FreeBSD_mips",
	"BROKEN_FreeBSD_mips64",
	"BROKEN_FreeBSD_powerpc",
	"BROKEN_FreeBSD_powerpc64",
	"BROKEN_FreeBSD_sparc64",
	"BROKEN_FreeBSD",
	"BROKEN_i386",
	"BROKEN_mips",
	"BROKEN_mips64",
	"BROKEN_powerpc",
	"BROKEN_powerpc64",
	"BROKEN_sparc64",
	"BROKEN",
	"CARGO_CARGO_RUN",
	"COMMENT",
	"DESC",
	"DEV_ERROR",
	"DEV_WARNING",
	"DISTFILES",
	"GH_TUPLE",
	"IGNORE_aarch64",
	"IGNORE_amd64",
	"IGNORE_armv6",
	"IGNORE_armv7",
	"IGNORE_FreeBSD_11_aarch64",
	"IGNORE_FreeBSD_11_amd64",
	"IGNORE_FreeBSD_11_armv6",
	"IGNORE_FreeBSD_11_armv7",
	"IGNORE_FreeBSD_11_i386",
	"IGNORE_FreeBSD_11_mips",
	"IGNORE_FreeBSD_11_mips64",
	"IGNORE_FreeBSD_11_powerpc",
	"IGNORE_FreeBSD_11_powerpc64",
	"IGNORE_FreeBSD_11_sparc64",
	"IGNORE_FreeBSD_11",
	"IGNORE_FreeBSD_12_aarch64",
	"IGNORE_FreeBSD_12_amd64",
	"IGNORE_FreeBSD_12_armv6",
	"IGNORE_FreeBSD_12_armv7",
	"IGNORE_FreeBSD_12_i386",
	"IGNORE_FreeBSD_12_mips",
	"IGNORE_FreeBSD_12_mips64",
	"IGNORE_FreeBSD_12_powerpc",
	"IGNORE_FreeBSD_12_powerpc64",
	"IGNORE_FreeBSD_12_sparc64",
	"IGNORE_FreeBSD_12",
	"IGNORE_FreeBSD_13_aarch64",
	"IGNORE_FreeBSD_13_amd64",
	"IGNORE_FreeBSD_13_armv6",
	"IGNORE_FreeBSD_13_armv7",
	"IGNORE_FreeBSD_13_i386",
	"IGNORE_FreeBSD_13_mips",
	"IGNORE_FreeBSD_13_mips64",
	"IGNORE_FreeBSD_13_powerpc",
	"IGNORE_FreeBSD_13_powerpc64",
	"IGNORE_FreeBSD_13_sparc64",
	"IGNORE_FreeBSD_13",
	"IGNORE_FreeBSD_aarch64",
	"IGNORE_FreeBSD_amd64",
	"IGNORE_FreeBSD_armv6",
	"IGNORE_FreeBSD_armv7",
	"IGNORE_FreeBSD_i386",
	"IGNORE_FreeBSD_mips",
	"IGNORE_FreeBSD_mips64",
	"IGNORE_FreeBSD_powerpc",
	"IGNORE_FreeBSD_powerpc64",
	"IGNORE_FreeBSD_sparc64",
	"IGNORE_i386",
	"IGNORE_mips",
	"IGNORE_mips64",
	"IGNORE_powerpc",
	"IGNORE_powerpc64",
	"IGNORE_sparc64",
	"IGNORE",
	"MASTER_SITES",
	"NO_CCACHE",
	"NO_CDROM",
	"NO_PACKAGE",
	"RESTRICTED",
};

struct VariableOrderEntry {
	enum BlockType block;
	const char *var;
};
/* Based on:
 * https://www.freebsd.org/doc/en_US.ISO8859-1/books/porters-handbook/porting-order.html
 */
static struct VariableOrderEntry variable_order_[] = {
	{ BLOCK_PORTNAME, "PORTNAME" },
	{ BLOCK_PORTNAME, "PORTVERSION" },
	{ BLOCK_PORTNAME, "DISTVERSIONPREFIX" },
	{ BLOCK_PORTNAME, "DISTVERSION" },
	{ BLOCK_PORTNAME, "DISTVERSIONSUFFIX" },
	{ BLOCK_PORTNAME, "PORTREVISION" },
	{ BLOCK_PORTNAME, "PORTEPOCH" },
	{ BLOCK_PORTNAME, "CATEGORIES" },
	{ BLOCK_PORTNAME, "MASTER_SITES" },
	{ BLOCK_PORTNAME, "MASTER_SITE_SUBDIR" },
	{ BLOCK_PORTNAME, "PKGNAMEPREFIX" },
	{ BLOCK_PORTNAME, "PKGNAMESUFFIX" },
	{ BLOCK_PORTNAME, "DISTNAME" },
	{ BLOCK_PORTNAME, "EXTRACT_SUFX" },
	{ BLOCK_PORTNAME, "DISTFILES" },
	{ BLOCK_PORTNAME, "DIST_SUBDIR" },
	{ BLOCK_PORTNAME, "EXTRACT_ONLY" },

	{ BLOCK_PATCHFILES, "PATCH_SITES" },
	{ BLOCK_PATCHFILES, "PATCHFILES" },
	{ BLOCK_PATCHFILES, "PATCH_DIST_STRIP" },

	{ BLOCK_MAINTAINER, "MAINTAINER" },
	{ BLOCK_MAINTAINER, "COMMENT" },

	{ BLOCK_LICENSE, "LICENSE" },
	{ BLOCK_LICENSE, "LICENSE_COMB" },
	{ BLOCK_LICENSE, "LICENSE_GROUPS" },
	{ BLOCK_LICENSE, "LICENSE_NAME" },
	{ BLOCK_LICENSE, "LICENSE_TEXT" },
	{ BLOCK_LICENSE, "LICENSE_FILE" },
	{ BLOCK_LICENSE, "LICENSE_PERMS" },
	{ BLOCK_LICENSE, "LICENSE_DISTFILES" },
	{ BLOCK_LICENSE, "RESTRICTED" },
	{ BLOCK_LICENSE, "RESTRICTED_FILES" },
	{ BLOCK_LICENSE, "NO_CDROM" },
	{ BLOCK_LICENSE, "NO_PACKAGE" },

	{ BLOCK_BROKEN, "DEPRECATED" },
	{ BLOCK_BROKEN, "EXPIRATION_DATE" },
	{ BLOCK_BROKEN, "FORBIDDEN" },

	{ BLOCK_BROKEN, "BROKEN" },
	{ BLOCK_BROKEN, "BROKEN_aarch64" },
	{ BLOCK_BROKEN, "BROKEN_amd64" },
	{ BLOCK_BROKEN, "BROKEN_armv6" },
	{ BLOCK_BROKEN, "BROKEN_armv7" },
	{ BLOCK_BROKEN, "BROKEN_i386" },
	{ BLOCK_BROKEN, "BROKEN_mips" },
	{ BLOCK_BROKEN, "BROKEN_mips64" },
	{ BLOCK_BROKEN, "BROKEN_powerpc" },
	{ BLOCK_BROKEN, "BROKEN_powerpc64" },
	{ BLOCK_BROKEN, "BROKEN_sparc64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_11" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_11_aarch64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_11_amd64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_11_armv6" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_11_armv7" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_11_i386" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_11_mips" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_11_mips64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_11_powerpc" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_11_powerpc64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_11_sparc64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_12" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_12_aarch64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_12_amd64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_12_armv6" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_12_armv7" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_12_i386" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_12_mips" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_12_mips64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_12_powerpc" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_12_powerpc64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_12_sparc64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_13" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_13_aarch64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_13_amd64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_13_armv6" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_13_armv7" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_13_i386" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_13_mips" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_13_mips64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_13_powerpc" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_13_powerpc64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_13_sparc64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_aarch64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_amd64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_armv6" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_armv7" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_i386" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_mips" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_mips64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_powerpc" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_powerpc64" },
	{ BLOCK_BROKEN, "BROKEN_FreeBSD_sparc64" },

	{ BLOCK_BROKEN, "IGNORE" },
	{ BLOCK_BROKEN, "IGNORE_aarch64" },
	{ BLOCK_BROKEN, "IGNORE_amd64" },
	{ BLOCK_BROKEN, "IGNORE_armv6" },
	{ BLOCK_BROKEN, "IGNORE_armv7" },
	{ BLOCK_BROKEN, "IGNORE_i386" },
	{ BLOCK_BROKEN, "IGNORE_mips" },
	{ BLOCK_BROKEN, "IGNORE_mips64" },
	{ BLOCK_BROKEN, "IGNORE_powerpc" },
	{ BLOCK_BROKEN, "IGNORE_powerpc64" },
	{ BLOCK_BROKEN, "IGNORE_sparc64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_11" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_11_aarch64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_11_amd64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_11_armv6" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_11_armv7" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_11_i386" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_11_mips" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_11_mips64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_11_powerpc" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_11_powerpc64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_11_sparc64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_12" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_12_aarch64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_12_amd64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_12_armv6" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_12_armv7" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_12_i386" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_12_mips" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_12_mips64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_12_powerpc" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_12_powerpc64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_12_sparc64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_13" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_13_aarch64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_13_amd64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_13_armv6" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_13_armv7" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_13_i386" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_13_mips" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_13_mips64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_13_powerpc" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_13_powerpc64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_13_sparc64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_aarch64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_amd64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_armv6" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_armv7" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_i386" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_mips" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_mips64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_powerpc" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_powerpc64" },
	{ BLOCK_BROKEN, "IGNORE_FreeBSD_sparc64" },

	{ BLOCK_BROKEN, "ONLY_FOR_ARCHS" },
	{ BLOCK_BROKEN, "ONLY_FOR_ARCHS_REASON" },
	{ BLOCK_BROKEN, "ONLY_FOR_ARCHS_REASON_aarch64" },
	{ BLOCK_BROKEN, "ONLY_FOR_ARCHS_REASON_amd64" },
	{ BLOCK_BROKEN, "ONLY_FOR_ARCHS_REASON_armv6" },
	{ BLOCK_BROKEN, "ONLY_FOR_ARCHS_REASON_armv7" },
	{ BLOCK_BROKEN, "ONLY_FOR_ARCHS_REASON_i386" },
	{ BLOCK_BROKEN, "ONLY_FOR_ARCHS_REASON_mips" },
	{ BLOCK_BROKEN, "ONLY_FOR_ARCHS_REASON_mips64" },
	{ BLOCK_BROKEN, "ONLY_FOR_ARCHS_REASON_powerpc" },
	{ BLOCK_BROKEN, "ONLY_FOR_ARCHS_REASON_powerpc64" },
	{ BLOCK_BROKEN, "ONLY_FOR_ARCHS_REASON_sparc64" },

	{ BLOCK_BROKEN, "NOT_FOR_ARCHS" },
	{ BLOCK_BROKEN, "NOT_FOR_ARCHS_REASON" },
	{ BLOCK_BROKEN, "NOT_FOR_ARCHS_REASON_aarch64" },
	{ BLOCK_BROKEN, "NOT_FOR_ARCHS_REASON_amd64" },
	{ BLOCK_BROKEN, "NOT_FOR_ARCHS_REASON_armv6" },
	{ BLOCK_BROKEN, "NOT_FOR_ARCHS_REASON_armv7" },
	{ BLOCK_BROKEN, "NOT_FOR_ARCHS_REASON_i386" },
	{ BLOCK_BROKEN, "NOT_FOR_ARCHS_REASON_mips" },
	{ BLOCK_BROKEN, "NOT_FOR_ARCHS_REASON_mips64" },
	{ BLOCK_BROKEN, "NOT_FOR_ARCHS_REASON_powerpc" },
	{ BLOCK_BROKEN, "NOT_FOR_ARCHS_REASON_powerpc64" },
	{ BLOCK_BROKEN, "NOT_FOR_ARCHS_REASON_sparc64" },

	{ BLOCK_DEPENDS, "FETCH_DEPENDS" },
	{ BLOCK_DEPENDS, "EXTRACT_DEPENDS" },
	{ BLOCK_DEPENDS, "PATCH_DEPENDS" },
	{ BLOCK_DEPENDS, "BUILD_DEPENDS" },
	{ BLOCK_DEPENDS, "LIB_DEPENDS" },
	{ BLOCK_DEPENDS, "RUN_DEPENDS" },
	{ BLOCK_DEPENDS, "TEST_DEPENDS" },

	{ BLOCK_FLAVORS, "FLAVORS" },
	{ BLOCK_FLAVORS, "FLAVOR" },
	{ BLOCK_FLAVORS, "flavor_PKGNAMEPREFIX" },
	{ BLOCK_FLAVORS, "flavor_PKGNAMESUFFIX" },
	{ BLOCK_FLAVORS, "flavor_PLIST" },
	{ BLOCK_FLAVORS, "flavor_DESCR" },
	{ BLOCK_FLAVORS, "flavor_CONFLICTS" },
	{ BLOCK_FLAVORS, "flavor_CONFLICTS_BUILD" },
	{ BLOCK_FLAVORS, "flavor_CONFLICTS_INSTALL" },
	{ BLOCK_FLAVORS, "flavor_PKG_DEPENDS" },
	{ BLOCK_FLAVORS, "flavor_EXTRACT_DEPENDS" },
	{ BLOCK_FLAVORS, "flavor_PATCH_DEPENDS" },
	{ BLOCK_FLAVORS, "flavor_FETCH_DEPENDS" },
	{ BLOCK_FLAVORS, "flavor_BUILD_DEPENDS" },
	{ BLOCK_FLAVORS, "flavor_LIB_DEPENDS" },
	{ BLOCK_FLAVORS, "flavor_RUN_DEPENDS" },
	{ BLOCK_FLAVORS, "flavor_TEST_DEPENDS" },

	{ BLOCK_USES, "USES" },
	{ BLOCK_USES, "CPE_PART" },
	{ BLOCK_USES, "CPE_VENDOR" },
	{ BLOCK_USES, "CPE_PRODUCT" },
	{ BLOCK_USES, "CPE_VERSION" },
	{ BLOCK_USES, "CPE_UPDATE" },
	{ BLOCK_USES, "CPE_EDITION" },
	{ BLOCK_USES, "CPE_LANG" },
	{ BLOCK_USES, "CPE_SW_EDITION" },
	{ BLOCK_USES, "CPE_TARGET_SW" },
	{ BLOCK_USES, "CPE_TARGET_HW" },
	{ BLOCK_USES, "CPE_OTHER" },
	{ BLOCK_USES, "DOS2UNIX_REGEX" },
	{ BLOCK_USES, "DOS2UNIX_FILES" },
	{ BLOCK_USES, "DOS2UNIX_GLOB" },
	{ BLOCK_USES, "DOS2UNIX_WRKSRC" },
	{ BLOCK_USES, "FONTNAME" },
	{ BLOCK_USES, "FONTSDIR" },
	{ BLOCK_USES, "HORDE_DIR" },
	{ BLOCK_USES, "PATHFIX_CMAKELISTSTXT" },
	{ BLOCK_USES, "PATHFIX_MAKEFILEIN" },
	{ BLOCK_USES, "PATHFIX_WRKSRC" },
	{ BLOCK_USES, "QMAIL_PREFIX" },
	{ BLOCK_USES, "QMAIL_SLAVEPORT" },
	{ BLOCK_USES, "WANT_PGSQL" },
	{ BLOCK_USES, "USE_ANT" },
	{ BLOCK_USES, "USE_BINUTILS" },
	{ BLOCK_USES, "USE_CABAL" },
	{ BLOCK_USES, "USE_CSTD" },
	{ BLOCK_USES, "USE_CXXSTD" },
	{ BLOCK_USES, "USE_FPC" },
	{ BLOCK_USES, "USE_GCC" },
	{ BLOCK_USES, "USE_GECKO" },
	{ BLOCK_USES, "USE_GITHUB" },
	{ BLOCK_USES, "GH_ACCOUNT" },
	{ BLOCK_USES, "GH_PROJECT" },
	{ BLOCK_USES, "GH_SUBDIR" },
	{ BLOCK_USES, "GH_TAGNAME" },
	{ BLOCK_USES, "GH_TUPLE" },
	{ BLOCK_USES, "USE_GITLAB" },
	{ BLOCK_USES, "GL_SITE" },
	{ BLOCK_USES, "GL_ACCOUNT" },
	{ BLOCK_USES, "GL_PROJECT" },
	{ BLOCK_USES, "GL_SUBDIR" },
	{ BLOCK_USES, "GL_TUPLE" },
	{ BLOCK_USES, "USE_GL" },
	{ BLOCK_USES, "USE_GNOME" },
	{ BLOCK_USES, "GCONF_SCHEMAS" },
	{ BLOCK_USES, "GLIB_SCHEMAS" },
	{ BLOCK_USES, "INSTALLS_ICONS" },
	{ BLOCK_USES, "INSTALLS_OMF" },
	{ BLOCK_USES, "USE_GNUSTEP" },
	{ BLOCK_USES, "USE_GSTREAMER" },
	{ BLOCK_USES, "USE_GSTREAMER1" },
	{ BLOCK_USES, "USE_JAVA" },
	{ BLOCK_USES, "JAVA_VERSION" },
	{ BLOCK_USES, "JAVA_OS" },
	{ BLOCK_USES, "JAVA_VENDOR" },
	{ BLOCK_USES, "JAVA_EXTRACT" },
	{ BLOCK_USES, "JAVA_BUILD" },
	{ BLOCK_USES, "JAVA_RUN" },
	{ BLOCK_USES, "USE_KDE" },
	{ BLOCK_USES, "USE_LDCONFIG" },
	{ BLOCK_USES, "USE_LINUX" },
	{ BLOCK_USES, "USE_LINUX_RPM" },
	{ BLOCK_USES, "USE_LOCALE" },
	{ BLOCK_USES, "USE_LXQT" },
	{ BLOCK_USES, "USE_MATE" },
	{ BLOCK_USES, "USE_OCAML" },
	{ BLOCK_USES, "NO_OCAML_BUILDDEPENDS" },
	{ BLOCK_USES, "NO_OCAML_RUNDEPENDS" },
	{ BLOCK_USES, "USE_OCAML_FINDLIB" },
	{ BLOCK_USES, "USE_OCAML_CAMLP4" },
	{ BLOCK_USES, "USE_OCAML_TK" },
	{ BLOCK_USES, "NO_OCAMLTK_BUILDDEPENDS" },
	{ BLOCK_USES, "NO_OCAMLTK_RUNDEPENDS" },
	{ BLOCK_USES, "USE_OCAML_LDCONFIG" },
	{ BLOCK_USES, "USE_OCAMLFIND_PLIST" },
	{ BLOCK_USES, "USE_OCAML_WASH" },
	{ BLOCK_USES, "OCAML_PKGDIRS" },
	{ BLOCK_USES, "OCAML_LDLIBS" },
	{ BLOCK_USES, "USE_OPENLDAP" },
	{ BLOCK_USES, "USE_PERL5" },
	{ BLOCK_USES, "USE_PHP" },
	{ BLOCK_USES, "PHP_MODPRIO" },
	{ BLOCK_USES, "USE_PYQT" },
	{ BLOCK_USES, "USE_PYTHON" },
	{ BLOCK_USES, "USE_RC_SUBR" },
	{ BLOCK_USES, "USE_RUBY" },
	{ BLOCK_USES, "RUBY_NO_BUILD_DEPENDS" },
	{ BLOCK_USES, "RUBY_NO_RUN_DEPENDS" },
	{ BLOCK_USES, "USE_RUBY_EXTCONF" },
	{ BLOCK_USES, "RUBY_EXTCONF" },
	{ BLOCK_USES, "RUBY_EXTCONF_SUBDIRS" },
	{ BLOCK_USES, "USE_RUBY_SETUP" },
	{ BLOCK_USES, "RUBY_SETUP" },
	{ BLOCK_USES, "USE_RUBY_RDOC" },
	{ BLOCK_USES, "RUBY_REQUIRE" },
	{ BLOCK_USES, "USE_RUBYGEMS" },
	{ BLOCK_USES, "USE_SDL" },
	{ BLOCK_USES, "USE_SUBMAKE" },
	{ BLOCK_USES, "USE_TEX" },
	{ BLOCK_USES, "USE_WX" },
	{ BLOCK_USES, "USE_WX_NOT" },
	{ BLOCK_USES, "WANT_WX" },
	{ BLOCK_USES, "WX_COMPS" },
	{ BLOCK_USES, "WX_CONF_ARGS" },
	{ BLOCK_USES, "WX_PREMK" },
	{ BLOCK_USES, "USE_XFCE" },
	{ BLOCK_USES, "USE_XORG" },

	{ BLOCK_SHEBANGFIX, "SHEBANG_FILES" },
	{ BLOCK_SHEBANGFIX, "SHEBANG_GLOB" },
	{ BLOCK_SHEBANGFIX, "SHEBANG_REGEX" },
	{ BLOCK_SHEBANGFIX, "SHEBANG_LANG" },
	{ BLOCK_SHEBANGFIX, "_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, "_CMD" },

	{ BLOCK_UNIQUEFILES, "UNIQUE_PREFIX" },
	{ BLOCK_UNIQUEFILES, "UNIQUE_PREFIX_FILES" },
	{ BLOCK_UNIQUEFILES, "UNIQUE_SUFFIX" },
	{ BLOCK_UNIQUEFILES, "UNIQUE_SUFFIX_FILESS" },

	{ BLOCK_APACHE, "AP_FAST_BUILD" },
	{ BLOCK_APACHE, "AP_GENPLIST" },
	{ BLOCK_APACHE, "MODULENAME" },
	{ BLOCK_APACHE, "SHORTMODNAME" },
	{ BLOCK_APACHE, "SRC_FILE" },

	{ BLOCK_ELIXIR, "ELIXIR_APP_NAME" },
	{ BLOCK_ELIXIR, "ELIXIR_LIB_ROOT" },
	{ BLOCK_ELIXIR, "ELIXIR_APP_ROOT" },
	{ BLOCK_ELIXIR, "ELIXIR_HIDDEN" },
	{ BLOCK_ELIXIR, "ELIXIR_LOCALE" },
	{ BLOCK_ELIXIR, "MIX_CMD" },
	{ BLOCK_ELIXIR, "MIX_COMPILE" },
	{ BLOCK_ELIXIR, "MIX_REWRITE" },
	{ BLOCK_ELIXIR, "MIX_BUILD_DEPS" },
	{ BLOCK_ELIXIR, "MIX_RUN_DEPS" },
	{ BLOCK_ELIXIR, "MIX_DOC_DIRS" },
	{ BLOCK_ELIXIR, "MIX_DOC_FILES" },
	{ BLOCK_ELIXIR, "MIX_ENV" },
	{ BLOCK_ELIXIR, "MIX_ENV_NAME" },
	{ BLOCK_ELIXIR, "MIX_BUILD_NAME" },
	{ BLOCK_ELIXIR, "MIX_TARGET" },
	{ BLOCK_ELIXIR, "MIX_EXTRA_APPS" },
	{ BLOCK_ELIXIR, "MIX_EXTRA_DIRS" },
	{ BLOCK_ELIXIR, "MIX_EXTRA_FILES" },

	{ BLOCK_EMACS, "EMACS_FLAVORS_EXCLUDE" },
	{ BLOCK_EMACS, "EMACS_NO_DEPENDS" },

	{ BLOCK_ERLANG, "ERL_APP_NAME" },
	{ BLOCK_ERLANG, "ERL_APP_ROOT" },
	{ BLOCK_ERLANG, "REBAR_CMD" },
	{ BLOCK_ERLANG, "REBAR3_CMD" },
	{ BLOCK_ERLANG, "REBAR_PROFILE" },
	{ BLOCK_ERLANG, "REBAR_TARGETS" },
	{ BLOCK_ERLANG, "ERL_BUILD_NAME" },
	{ BLOCK_ERLANG, "ERL_BUILD_DEPS" },
	{ BLOCK_ERLANG, "ERL_RUN_DEPS" },
	{ BLOCK_ERLANG, "ERL_DOCS" },

	{ BLOCK_CMAKE, "CMAKE_ARGS" },
	{ BLOCK_CMAKE, "CMAKE_OFF" },
	{ BLOCK_CMAKE, "CMAKE_ON" },
	{ BLOCK_CMAKE, "CMAKE_BUILD_TYPE" },
	{ BLOCK_CMAKE, "CMAKE_SOURCE_PATH" },

	{ BLOCK_CONFIGURE, "HAS_CONFIGURE" },
	{ BLOCK_CONFIGURE, "GNU_CONFIGURE" },
	{ BLOCK_CONFIGURE, "CONFIGURE_ARGS" },
	{ BLOCK_CONFIGURE, "CONFIGURE_ENV" },

	{ BLOCK_MAKE, "MAKEFILE" },
	{ BLOCK_MAKE, "MAKE_ARGS" },
	{ BLOCK_MAKE, "MAKE_ENV" },
	{ BLOCK_MAKE, "MAKE_FLAGS" },
	{ BLOCK_MAKE, "MAKE_JOBS_UNSAFE" },
	{ BLOCK_MAKE, "ALL_TARGET" },
	{ BLOCK_MAKE, "INSTALL_TARGET" },
	{ BLOCK_MAKE, "TEST_TARGET" },

	{ BLOCK_QMAKE, "QMAKE_ARGS" },
	{ BLOCK_QMAKE, "QMAKE_ENV" },
	{ BLOCK_QMAKE, "QMAKE_CONFIGUIRE_ARGS" },
	{ BLOCK_QMAKE, "QMAKE_SOURCE_PATH" },

	{ BLOCK_MESON, "MESON_ARGS" },
	{ BLOCK_MESON, "MESON_BUILD_DIR" },

	{ BLOCK_CARGO1, "CARGO_BUILDDEP" },
	{ BLOCK_CARGO1, "CARGO_BUILD" },
	{ BLOCK_CARGO1, "CARGO_BUILD_ARGS" },
	{ BLOCK_CARGO1, "CARGO_INSTALL" },
	{ BLOCK_CARGO1, "CARGO_INSTALL_ARGS" },
	{ BLOCK_CARGO1, "CARGO_TEST" },
	{ BLOCK_CARGO1, "CARGO_TEST_ARGS" },
	{ BLOCK_CARGO1, "CARGO_UPDATE_ARGS" },
	{ BLOCK_CARGO1, "CARGO_CARGO_BIN" },
	{ BLOCK_CARGO1, "CARGO_DIST_SUBDIR" },
	{ BLOCK_CARGO1, "CARGO_ENV" },
	{ BLOCK_CARGO1, "CARGO_TARGET_DIR" },
	{ BLOCK_CARGO1, "CARGO_VENDOR_DIR" },

	{ BLOCK_CARGO2, "CARGO_CRATES" },
	{ BLOCK_CARGO2, "CARGO_USE_GITHUB" },
	{ BLOCK_CARGO2, "CARGO_USE_GITLAB" },
	{ BLOCK_CARGO2, "CARGO_CARGOLOCK" },
	{ BLOCK_CARGO2, "CARGO_CARGOTOML" },
	{ BLOCK_CARGO2, "CARGO_FEATURES" },

	{ BLOCK_GO, "GO_PKGNAME" },
	{ BLOCK_GO, "GO_TARGET" },
	{ BLOCK_GO, "CGO_CFLAGS" },
	{ BLOCK_GO, "CGO_LDFLAGS" },
	{ BLOCK_GO, "GO_BUILDFLAGS" },

	{ BLOCK_LAZARUS, "NO_LAZBUILD" },
	{ BLOCK_LAZARUS, "LAZARUS_PROJECT_FILES" },
	{ BLOCK_LAZARUS, "LAZARUS_DIR" },
	{ BLOCK_LAZARUS, "LAZBUILD_ARGS" },
	{ BLOCK_LAZARUS, "LAZARUS_NO_FLAVORS" },

	{ BLOCK_LINUX, "BIN_DISTNAMES" },
	{ BLOCK_LINUX, "LIB_DISTNAMES" },
	{ BLOCK_LINUX, "SHARE_DISTNAMES" },
	{ BLOCK_LINUX, "SRC_DISTFILES" },

	{ BLOCK_NUGET, "NUGET_PACKAGEDIR" },
	{ BLOCK_NUGET, "NUGET_LAYOUT" },
	{ BLOCK_NUGET, "NUGET_FEEDS" },
	{ BLOCK_NUGET, "_URL" },
	{ BLOCK_NUGET, "_FILE" },
	{ BLOCK_NUGET, "_DEPENDS" },
	{ BLOCK_NUGET, "PAKET_PACKAGEDIR" },
	{ BLOCK_NUGET, "PAKET_DEPENDS" },

	{ BLOCK_CFLAGS, "CFLAGS" },
	{ BLOCK_CFLAGS, "CFLAGS_aarch64" },
	{ BLOCK_CFLAGS, "CFLAGS_amd64" },
	{ BLOCK_CFLAGS, "CFLAGS_armv6" },
	{ BLOCK_CFLAGS, "CFLAGS_armv7" },
	{ BLOCK_CFLAGS, "CFLAGS_i386" },
	{ BLOCK_CFLAGS, "CFLAGS_mips" },
	{ BLOCK_CFLAGS, "CFLAGS_mips64" },
	{ BLOCK_CFLAGS, "CFLAGS_powerpc" },
	{ BLOCK_CFLAGS, "CFLAGS_powerpc64" },
	{ BLOCK_CFLAGS, "CFLAGS_sparc64" },
	{ BLOCK_CFLAGS, "CXXFLAGS" },
	{ BLOCK_CFLAGS, "CXXFLAGS_aarch64" },
	{ BLOCK_CFLAGS, "CXXFLAGS_amd64" },
	{ BLOCK_CFLAGS, "CXXFLAGS_armv6" },
	{ BLOCK_CFLAGS, "CXXFLAGS_armv7" },
	{ BLOCK_CFLAGS, "CXXFLAGS_i386" },
	{ BLOCK_CFLAGS, "CXXFLAGS_mips" },
	{ BLOCK_CFLAGS, "CXXFLAGS_mips64" },
	{ BLOCK_CFLAGS, "CXXFLAGS_powerpc" },
	{ BLOCK_CFLAGS, "CXXFLAGS_powerpc64" },
	{ BLOCK_CFLAGS, "CXXFLAGS_sparc64" },
	{ BLOCK_CFLAGS, "LDFLAGS" },
	{ BLOCK_CFLAGS, "LDFLAGS_aarch64" },
	{ BLOCK_CFLAGS, "LDFLAGS_amd64" },
	{ BLOCK_CFLAGS, "LDFLAGS_armv6" },
	{ BLOCK_CFLAGS, "LDFLAGS_armv7" },
	{ BLOCK_CFLAGS, "LDFLAGS_i386" },
	{ BLOCK_CFLAGS, "LDFLAGS_mips" },
	{ BLOCK_CFLAGS, "LDFLAGS_mips64" },
	{ BLOCK_CFLAGS, "LDFLAGS_powerpc" },
	{ BLOCK_CFLAGS, "LDFLAGS_powerpc64" },
	{ BLOCK_CFLAGS, "LDFLAGS_sparc64" },
	{ BLOCK_CFLAGS, "LIBS" },
	{ BLOCK_CFLAGS, "LLD_UNSAFE" },
	{ BLOCK_CFLAGS, "SSP_UNSAFE" },
	{ BLOCK_CFLAGS, "SSP_CFLAGS" },

	{ BLOCK_CONFLICTS, "CONFLICTS" },
	{ BLOCK_CONFLICTS, "CONFLICTS_BUILD" },
	{ BLOCK_CONFLICTS, "CONFLICTS_INSTALL" },

	{ BLOCK_STANDARD, "DESKTOP_ENTRIES" },
	{ BLOCK_STANDARD, "FILESDIR" },
	{ BLOCK_STANDARD, "MASTERDIR" },
	{ BLOCK_STANDARD, "NO_ARCH" },
	{ BLOCK_STANDARD, "NO_ARCH_IGNORE" },
	{ BLOCK_STANDARD, "NO_BUILD" },
	{ BLOCK_STANDARD, "NO_WRKSUBDIR" },
	{ BLOCK_STANDARD, "SUB_FILES" },
	{ BLOCK_STANDARD, "SUB_LIST" },
	{ BLOCK_STANDARD, "USERS" },
	{ BLOCK_STANDARD, "GROUPS" },
	{ BLOCK_STANDARD, "SCRIPTDIR" },
	{ BLOCK_STANDARD, "WRKSRC" },
	{ BLOCK_STANDARD, "WRKSRC_SUBDIR" },
	// TODO: Missing *many* variables here

	{ BLOCK_PLIST, "INFO" },
	{ BLOCK_PLIST, "INFO_PATH" },
	{ BLOCK_PLIST, "PLIST_DIRS" },
	{ BLOCK_PLIST, "PLIST_FILES" },
	{ BLOCK_PLIST, "PLIST_SUB" },
	{ BLOCK_PLIST, "PORTDATA" },
	{ BLOCK_PLIST, "PORTDOCS" },
	{ BLOCK_PLIST, "PORTEXAMPLES" },

	{ BLOCK_OPTDEF, "OPTIONS_DEFINE" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFINE_aarch64" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFINE_amd64" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFINE_armv6" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFINE_armv7" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFINE_i386" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFINE_mips" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFINE_mips64" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFINE_powerpc" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFINE_powerpc64" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFINE_sparc64" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFAULT" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFAULT_aarch64" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFAULT_amd64" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFAULT_armv6" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFAULT_armv7" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFAULT_i386" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFAULT_mips" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFAULT_mips64" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFAULT_powerpc" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFAULT_powerpc64" },
	{ BLOCK_OPTDEF, "OPTIONS_DEFAULT_sparc64" },
	{ BLOCK_OPTDEF, "OPTIONS_GROUP" },
	{ BLOCK_OPTDEF, "OPTIONS_MULTI" },
	{ BLOCK_OPTDEF, "OPTIONS_RADIO" },
	{ BLOCK_OPTDEF, "OPTIONS_EXCLUDE" },
	{ BLOCK_OPTDEF, "OPTIONS_EXCLUDE_aarch64" },
	{ BLOCK_OPTDEF, "OPTIONS_EXCLUDE_amd64" },
	{ BLOCK_OPTDEF, "OPTIONS_EXCLUDE_armv6" },
	{ BLOCK_OPTDEF, "OPTIONS_EXCLUDE_armv7" },
	{ BLOCK_OPTDEF, "OPTIONS_EXCLUDE_i386" },
	{ BLOCK_OPTDEF, "OPTIONS_EXCLUDE_mips" },
	{ BLOCK_OPTDEF, "OPTIONS_EXCLUDE_mips64" },
	{ BLOCK_OPTDEF, "OPTIONS_EXCLUDE_powerpc" },
	{ BLOCK_OPTDEF, "OPTIONS_EXCLUDE_powerpc64" },
	{ BLOCK_OPTDEF, "OPTIONS_EXCLUDE_sparc64" },
	{ BLOCK_OPTDEF, "OPTIONS_SUB" },

	{ BLOCK_OPTDESC, "DESC" },

	{ BLOCK_OPTHELPER, "CATEGORIES_OFF" },
	{ BLOCK_OPTHELPER, "CATEGORIES" },
	{ BLOCK_OPTHELPER, "MASTER_SITES_OFF" },
	{ BLOCK_OPTHELPER, "MASTER_SITES" },

	{ BLOCK_OPTHELPER, "DISTFILES_OFF" },
	{ BLOCK_OPTHELPER, "DISTFILES" },

	{ BLOCK_OPTHELPER, "PATCH_SITES_OFF" },
	{ BLOCK_OPTHELPER, "PATCH_SITES" },
	{ BLOCK_OPTHELPER, "PATCHFILES_OFF" },
	{ BLOCK_OPTHELPER, "PATCHFILES" },

	{ BLOCK_OPTHELPER, "BUILD_DEPENDS_OFF" },
	{ BLOCK_OPTHELPER, "BUILD_DEPENDS" },
	{ BLOCK_OPTHELPER, "EXTRACT_DEPENDS_OFF" },
	{ BLOCK_OPTHELPER, "EXTRACT_DEPENDS" },
	{ BLOCK_OPTHELPER, "FETCH_DEPENDS_OFF" },
	{ BLOCK_OPTHELPER, "FETCH_DEPENDS" },
	{ BLOCK_OPTHELPER, "LIB_DEPENDS_OFF" },
	{ BLOCK_OPTHELPER, "LIB_DEPENDS" },
	{ BLOCK_OPTHELPER, "PATCH_DEPENDS_OFF" },
	{ BLOCK_OPTHELPER, "PATCH_DEPENDS" },
	{ BLOCK_OPTHELPER, "PKG_DEPENDS_OFF" },
	{ BLOCK_OPTHELPER, "PKG_DEPENDS" },
	{ BLOCK_OPTHELPER, "RUN_DEPENDS_OFF" },
	{ BLOCK_OPTHELPER, "RUN_DEPENDS" },
	{ BLOCK_OPTHELPER, "TEST_DEPENDS_OFF" },
	{ BLOCK_OPTHELPER, "TEST_DEPENDS" },

	{ BLOCK_OPTHELPER, "USES" },
	{ BLOCK_OPTHELPER, "USES_OFF" },
	{ BLOCK_OPTHELPER, "USE" },
	{ BLOCK_OPTHELPER, "USE_OFF" },
	{ BLOCK_OPTHELPER, "USE_CABAL" },
	{ BLOCK_OPTHELPER, "GH_ACCOUNT_OFF" },
	{ BLOCK_OPTHELPER, "GH_ACCOUNT" },
	{ BLOCK_OPTHELPER, "GH_PROJECT_OFF" },
	{ BLOCK_OPTHELPER, "GH_PROJECT" },
	{ BLOCK_OPTHELPER, "GH_SUBDIR_OFF" },
	{ BLOCK_OPTHELPER, "GH_SUBDIR" },
	{ BLOCK_OPTHELPER, "GH_TAGNAME_OFF" },
	{ BLOCK_OPTHELPER, "GH_TAGNAME" },
	{ BLOCK_OPTHELPER, "GH_TUPLE_OFF" },
	{ BLOCK_OPTHELPER, "GH_TUPLE" },
	{ BLOCK_OPTHELPER, "GL_ACCOUNT_OFF" },
	{ BLOCK_OPTHELPER, "GL_ACCOUNT" },
	{ BLOCK_OPTHELPER, "GL_COMMIT_OFF" },
	{ BLOCK_OPTHELPER, "GL_COMMIT" },
	{ BLOCK_OPTHELPER, "GL_PROJECT_OFF" },
	{ BLOCK_OPTHELPER, "GL_PROJECT" },
	{ BLOCK_OPTHELPER, "GL_SITE_OFF" },
	{ BLOCK_OPTHELPER, "GL_SITE" },
	{ BLOCK_OPTHELPER, "GL_SUBDIR_OFF" },
	{ BLOCK_OPTHELPER, "GL_SUBDIR" },
	{ BLOCK_OPTHELPER, "GL_TUPLE_OFF" },
	{ BLOCK_OPTHELPER, "GL_TUPLE" },

	{ BLOCK_OPTHELPER, "CMAKE_BOOL_OFF" },
	{ BLOCK_OPTHELPER, "CMAKE_BOOL" },
	{ BLOCK_OPTHELPER, "CMAKE_OFF" },
	{ BLOCK_OPTHELPER, "CMAKE_ON" },

	{ BLOCK_OPTHELPER, "CONFIGURE_OFF" },
	{ BLOCK_OPTHELPER, "CONFIGURE_ON" },
	{ BLOCK_OPTHELPER, "CONFIGURE_ENABLE" },
	{ BLOCK_OPTHELPER, "CONFIGURE_WITH" },
	{ BLOCK_OPTHELPER, "CONFIGURE_ENV_OFF" },
	{ BLOCK_OPTHELPER, "CONFIGURE_ENV" },

	{ BLOCK_OPTHELPER, "MESON_DISABLED" },
	{ BLOCK_OPTHELPER, "MESON_ENABLED" },
	{ BLOCK_OPTHELPER, "MESON_FALSE" },
	{ BLOCK_OPTHELPER, "MESON_NO" },
	{ BLOCK_OPTHELPER, "MESON_OFF" },
	{ BLOCK_OPTHELPER, "MESON_ON" },
	{ BLOCK_OPTHELPER, "MESON_TRUE" },
	{ BLOCK_OPTHELPER, "MESON_YES" },

	{ BLOCK_OPTHELPER, "ALL_TARGET_OFF" },
	{ BLOCK_OPTHELPER, "ALL_TARGET" },
	{ BLOCK_OPTHELPER, "BINARY_ALIAS_OFF" },
	{ BLOCK_OPTHELPER, "BINARY_ALIAS" },
	{ BLOCK_OPTHELPER, "BROKEN_OFF" },
	{ BLOCK_OPTHELPER, "BROKEN" },
	{ BLOCK_OPTHELPER, "CABAL_FLAGS" },
	{ BLOCK_OPTHELPER, "CFLAGS_OFF" },
	{ BLOCK_OPTHELPER, "CFLAGS" },
	{ BLOCK_OPTHELPER, "CONFLICTS_BUILD_OFF" },
	{ BLOCK_OPTHELPER, "CONFLICTS_BUILD" },
	{ BLOCK_OPTHELPER, "CONFLICTS_INSTALL_OFF" },
	{ BLOCK_OPTHELPER, "CONFLICTS_INSTALL" },
	{ BLOCK_OPTHELPER, "CONFLICTS_OFF" },
	{ BLOCK_OPTHELPER, "CONFLICTS" },
	{ BLOCK_OPTHELPER, "CPPFLAGS_OFF" },
	{ BLOCK_OPTHELPER, "CPPFLAGS" },
	{ BLOCK_OPTHELPER, "CXXFLAGS_OFF" },
	{ BLOCK_OPTHELPER, "CXXFLAGS" },
	{ BLOCK_OPTHELPER, "DESKTOP_ENTRIES_OFF" },
	{ BLOCK_OPTHELPER, "DESKTOP_ENTRIES" },
	{ BLOCK_OPTHELPER, "EXECUTABLES" },
	{ BLOCK_OPTHELPER, "EXTRA_PATCHES_OFF" },
	{ BLOCK_OPTHELPER, "EXTRA_PATCHES" },
	{ BLOCK_OPTHELPER, "EXTRACT_ONLY_OFF" },
	{ BLOCK_OPTHELPER, "EXTRACT_ONLY" },
	{ BLOCK_OPTHELPER, "IGNORE_OFF" },
	{ BLOCK_OPTHELPER, "IGNORE" },
	{ BLOCK_OPTHELPER, "INFO_OFF" },
	{ BLOCK_OPTHELPER, "INFO" },
	{ BLOCK_OPTHELPER, "INSTALL_TARGET_OFF" },
	{ BLOCK_OPTHELPER, "INSTALL_TARGET" },
	{ BLOCK_OPTHELPER, "LDFLAGS_OFF" },
	{ BLOCK_OPTHELPER, "LDFLAGS" },
	{ BLOCK_OPTHELPER, "LIBS_OFF" },
	{ BLOCK_OPTHELPER, "LIBS" },
	{ BLOCK_OPTHELPER, "MAKE_ARGS_OFF" },
	{ BLOCK_OPTHELPER, "MAKE_ARGS" },
	{ BLOCK_OPTHELPER, "MAKE_ENV_OFF" },
	{ BLOCK_OPTHELPER, "MAKE_ENV" },
	{ BLOCK_OPTHELPER, "PLIST_DIRS_OFF" },
	{ BLOCK_OPTHELPER, "PLIST_DIRS" },
	{ BLOCK_OPTHELPER, "PLIST_FILES_OFF" },
	{ BLOCK_OPTHELPER, "PLIST_FILES" },
	{ BLOCK_OPTHELPER, "PLIST_SUB_OFF" },
	{ BLOCK_OPTHELPER, "PLIST_SUB" },
	{ BLOCK_OPTHELPER, "PORTDOCS_OFF" },
	{ BLOCK_OPTHELPER, "PORTDOCS" },
	{ BLOCK_OPTHELPER, "PORTEXAMPLES_OFF" },
	{ BLOCK_OPTHELPER, "PORTEXAMPLES" },
	{ BLOCK_OPTHELPER, "QMAKE_OFF" },
	{ BLOCK_OPTHELPER, "QMAKE_ON" },
	{ BLOCK_OPTHELPER, "SUB_FILES_OFF" },
	{ BLOCK_OPTHELPER, "SUB_FILES" },
	{ BLOCK_OPTHELPER, "SUB_LIST_OFF" },
	{ BLOCK_OPTHELPER, "SUB_LIST" },
	{ BLOCK_OPTHELPER, "TEST_TARGET_OFF" },
	{ BLOCK_OPTHELPER, "TEST_TARGET" },
	{ BLOCK_OPTHELPER, "VARS_OFF" },
	{ BLOCK_OPTHELPER, "VARS" },
};

int
ignore_wrap_col(struct Variable *var)
{
	if (variable_modifier(var) == MODIFIER_SHELL ||
	    matches(RE_LICENSE_NAME, variable_name(var), NULL)) {
		return 1;
	}

	for (size_t i = 0; i < nitems(ignore_wrap_col_); i++) {
		if (strcmp(variable_name(var), ignore_wrap_col_[i]) == 0) {
			return 1;
		}
	}

	if (matches(RE_OPTIONS_HELPER, variable_name(var), NULL)) {
		for (size_t i = 0; i < nitems(ignore_wrap_col_); i++) {
			if (str_endswith(variable_name(var), ignore_wrap_col_[i])) {
				return 1;
			}
		}
	}

	return 0;
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
leave_unsorted(struct Variable *var)
{
	for (size_t i = 0; i < nitems(leave_unsorted_); i++) {
		if (strcmp(variable_name(var), leave_unsorted_[i]) == 0) {
			return 1;
		}
	}

	if (variable_modifier(var) == MODIFIER_SHELL ||
	    str_endswith(variable_name(var), "_CMD") ||
	    str_endswith(variable_name(var), "_ALT") ||
	    str_endswith(variable_name(var), "_REASON") ||
	    str_endswith(variable_name(var), "_USE_GNOME_IMPL") ||
	    str_endswith(variable_name(var), "FLAGS") ||
	    matches(RE_LICENSE_NAME, variable_name(var), NULL)) {
		return 1;
	}

	if (matches(RE_OPTIONS_HELPER, variable_name(var), NULL)) {
		for (size_t i = 0; i < nitems(leave_unsorted_); i++) {
			if (str_endswith(variable_name(var), leave_unsorted_[i])) {
				return 1;
			}
		}
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
print_as_newlines(struct Variable *var)
{
	for (size_t i = 0; i < nitems(print_as_newlines_); i++) {
		if (strcmp(variable_name(var), print_as_newlines_[i]) == 0) {
			return 1;
		}
	}

	if (matches(RE_OPTIONS_HELPER, variable_name(var), NULL)) {
		for (size_t i = 0; i < nitems(print_as_newlines_); i++) {
			if (str_endswith(variable_name(var), print_as_newlines_[i])) {
				return 1;
			}
		}
	}

	return 0;
}

int
skip_goalcol(struct Variable *var)
{
	if (matches(RE_LICENSE_NAME, variable_name(var), NULL)) {
		return 1;
	}

	for (size_t i = 0; i < nitems(skip_goalcol_); i++) {
		if (strcmp(variable_name(var), skip_goalcol_[i]) == 0) {
			return 1;
		}
	}

	return 0;
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
compare_tokens(const void *ap, const void *bp)
{
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
	    compare_plist_files(var, token_data(a), token_data(b), &result) ||
	    compare_use_pyqt(var, token_data(a), token_data(b), &result) ||
	    compare_use_qt(var, token_data(a), token_data(b), &result)) {
		return result;
	}

	return strcasecmp(token_data(a), token_data(b));
}

int
compare_license_perms(struct Variable *var, const char *a, const char *b, int *result)
{
	assert(result != NULL);

	if (!matches(RE_LICENSE_PERMS, variable_name(var), NULL)) {
		return 0;
	}

	*result = compare_rel(license_perms_rel, nitems(license_perms_rel), a, b);
	return 1;
}

int
compare_plist_files(struct Variable *var, const char *a, const char *b, int *result)
{
	assert(result != NULL);

	if (!matches(RE_PLIST_FILES, variable_name(var), NULL)) {
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

enum BlockType
variable_order_block(const char *var)
{
	for (size_t i = 0; i < nitems(variable_order_); i++) {
		if (strcmp(var, variable_order_[i].var) == 0) {
			return variable_order_[i].block;
		}
	}
	return BLOCK_UNKNOWN;
}

int
compare_order(const void *ap, const void *bp)
{
	const char *a = *(const char **)ap;
	const char *b = *(const char **)bp;

	if (strcmp(a, b) == 0) {
		return 0;
	}

	ssize_t ai = -1;
	ssize_t bi = -1;
	for (size_t i = 0; i < nitems(variable_order_) && (ai == -1 || bi == -1); i++) {
		if (strcmp(a, variable_order_[i].var) == 0) {
			ai = i;
		}
		if (strcmp(b, variable_order_[i].var) == 0) {
			bi = i;
		}
	}

	if (ai < bi) {
		return -1;
	} else {
		return 1;
	}
}

const char *
blocktype_tostring(enum BlockType block)
{
	switch (block) {
	case BLOCK_APACHE:
		return "BLOCK_APACHE";
	case BLOCK_BROKEN:
		return "BLOCK_BROKEN";
	case BLOCK_CARGO1:
		return "BLOCK_CARGO1";
	case BLOCK_CARGO2:
		return "BLOCK_CARGO2";
	case BLOCK_CFLAGS:
		return "BLOCK_CFLAGS";
	case BLOCK_CMAKE:
		return "BLOCK_CMAKE";
	case BLOCK_CONFIGURE:
		return "BLOCK_CONFIGURE";
	case BLOCK_CONFLICTS:
		return "BLOCK_CONFLICTS";
	case BLOCK_DEPENDS:
		return "BLOCK_DEPENDS";
	case BLOCK_ELIXIR:
		return "BLOCK_ELIXIR";
	case BLOCK_EMACS:
		return "BLOCK_EMACS";
	case BLOCK_ERLANG:
		return "BLOCK_ERLANG";
	case BLOCK_FLAVORS:
		return "BLOCK_FLAVORS";
	case BLOCK_GO:
		return "BLOCK_GO";
	case BLOCK_LAZARUS:
		return "BLOCK_LAZARUS";
	case BLOCK_LICENSE:
		return "BLOCK_LICENSE";
	case BLOCK_LINUX:
		return "BLOCK_LINUX";
	case BLOCK_MAINTAINER:
		return "BLOCK_MAINTAINER";
	case BLOCK_MAKE:
		return "BLOCK_MAKE";
	case BLOCK_MESON:
		return "BLOCK_MESON";
	case BLOCK_NUGET:
		return "BLOCK_NUGET";
	case BLOCK_OPTDEF:
		return "BLOCK_OPTDEF";
	case BLOCK_OPTDESC:
		return "BLOCK_OPTDESC";
	case BLOCK_OPTHELPER:
		return "BLOCK_OPTHELPER";
	case BLOCK_PATCHFILES:
		return "BLOCK_PATCHFILES";
	case BLOCK_PLIST:
		return "BLOCK_PLIST";
	case BLOCK_PORTNAME:
		return "BLOCK_PORTNAME";
	case BLOCK_QMAKE:
		return "BLOCK_QMAKE";
	case BLOCK_SHEBANGFIX:
		return "BLOCK_SHEBANGFIX";
	case BLOCK_STANDARD:
		return "BLOCK_STANDARD";
	case BLOCK_UNIQUEFILES:
		return "BLOCK_UNIQUEFILES";
	case BLOCK_UNKNOWN:
		return "BLOCK_UNKNOWN";
	case BLOCK_USES:
		return "BLOCK_USES";
	}

	abort();
}

char *
options_helpers_pattern()
{
	size_t len = strlen("_(");
	for (size_t i = 0; i < nitems(options_helpers_); i++) {
		const char *helper = options_helpers_[i];
		len += strlen(helper);
		if (i < (nitems(options_helpers_) - 1)) {
			len += strlen("|");
		}
	}
	len += strlen(")$") + 1;

	char *buf = xmalloc(len);
	xstrlcat(buf, "_(", len);
	for (size_t i = 0; i < nitems(options_helpers_); i++) {
		const char *helper = options_helpers_[i];
		xstrlcat(buf, helper, len);
		if (i < (nitems(options_helpers_) - 1)) {
			xstrlcat(buf, "|", len);
		}
	}
	xstrlcat(buf, ")$", len);

	return buf;
}

int
target_command_should_wrap(char *word)
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

int
matches(enum RegularExpression re, const char *s, regmatch_t *match)
{
	int nmatch = 0;
	if (match) {
		nmatch = 1;
	}
	return regexec(&regular_expressions[re].re, s, nmatch, match, 0) == 0;
}

char *
sub(enum RegularExpression re, const char *replacement, const char *s)
{
	size_t len = strlen(replacement) + strlen(s) + 1;
	char *buf = xmalloc(len);
	buf[0] = 0;

	regmatch_t pmatch[1];
	if (regexec(&regular_expressions[re].re, s, 1, pmatch, 0) == 0) {
		strncpy(buf, s, pmatch[0].rm_so);
		if (replacement) {
			xstrlcat(buf, replacement, len);
		}
		strncat(buf, s + pmatch[0].rm_eo, strlen(s) - pmatch[0].rm_eo);
	} else {
		xstrlcat(buf, s, len);
	}

	return buf;
}

void
compile_regular_expressions()
{
	for (size_t i = 0; i < nitems(regular_expressions); i++) {
		char *buf = NULL;
		const char *pattern;
		switch (i) {
		case RE_OPTIONS_HELPER:
			buf = options_helpers_pattern();
			pattern = buf;
			break;
		default:
			pattern = regular_expressions[i].pattern;
			break;
		}

		int error = regcomp(&regular_expressions[i].re, pattern,
				    regular_expressions[i].flags);
		if (error != 0) {
			size_t errbuflen = regerror(error, &regular_expressions[i].re, NULL, 0);
			char *errbuf = xmalloc(errbuflen);
			regerror(error, &regular_expressions[i].re, errbuf, errbuflen);
			errx(1, "regcomp: %zu: %s", i, errbuf);
		}

		if (buf) {
			free(buf);
		}
	}
}
