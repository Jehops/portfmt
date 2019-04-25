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
				  "elifmake|endif))([[:space:]]+|$)",
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
	[RE_PLIST_FILES]      = { "^([A-Z0-9_]+_PLIST_DIRS[+?:]?|"
				  "[A-Z0-9_]+_PLIST_FILES[+?:]?|"
				  "PLIST_FILES[+?:]?|"
				  "PLIST_DIRS[+?:]?)",
				  REG_EXTENDED, {} },
	[RE_PLIST_KEYWORDS]   = { "^\"@([a-z]|-)+ ",			      REG_EXTENDED, {} },
	[RE_MODIFIER]	      = { "[:!?+]?=$",				      REG_EXTENDED, {} },
	[RE_TARGET] 	      = { "^[\\$\\{:\\}A-Za-z0-9\\/\\._-]+::?([[:space:]]+|$)",	  REG_EXTENDED, {} },
	[RE_VAR] 	      = { "^(-|[\\$\\{\\}a-zA-Z0-9\\._+ ])+[[:space:]]*[+!?:]?=", REG_EXTENDED, {} },
};

static const char *print_as_newlines_[] = {
	"BUILD_DEPENDS",
	"CARGO_CRATES",
	"CFLAGS",
	"CMAKE_ARGS",
	"CMAKE_BOOL",
	"CO_ENV",
	"CONFIGURE_ARGS",
	"CONFIGURE_ENV",
	"CONFIGURE_OFF",
	"CONFIGURE_ON",
	"CPPFLAGS",
	"CXXFLAGS",
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
	"LDFLAGS",
	"LIB_DEPENDS",
	"MAKE_ARGS",
	"MAKE_ENV",
	"MASTER_SITES",
	"MASTER_SITES_ABBREVS",
	"MASTER_SITES_SUBDIRS",
	"MESON_ARGS",
	"MOZ_OPTIONS",
	"OPTIONS_EXCLUDE",
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
	"network",
	"opengl",
	"qscintilla2",
	"sql",
	"svg",
	"test",
	"webkit",
	"xml",
	"xmlpatterns",
	"sip",
	"multimediawidgets",
	"printsupport",
	"qml",
	"quickwidgets",
	"serialport",
	"webchannel",
	"webengine",
	"webkitwidgets",
	"widgets",

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
	"network_build",
	"opengl_build",
	"qscintilla2_build",
	"sql_build",
	"svg_build",
	"test_build",
	"webkit_build",
	"xml_build",
	"xmlpatterns_build",
	"sip_build",
	"multimediawidgets_build",
	"printsupport_build",
	"qml_build",
	"quickwidgets_build",
	"serialport_build",
	"webchannel_build",
	"webengine_build",
	"webkitwidgets_build",
	"widgets_build",

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
	"network_run",
	"opengl_run",
	"qscintilla2_run",
	"sql_run",
	"svg_run",
	"test_run",
	"webkit_run",
	"xml_run",
	"xmlpatterns_run",
	"sip_run",
	"multimediawidgets_run",
	"printsupport_run",
	"qml_run",
	"quickwidgets_run",
	"serialport_run",
	"webchannel_run",
	"webengine_run",
	"webkitwidgets_run",
	"widgets_run",
};

// Sanitize whitespace but do *not* sort tokens; more complicated
// patterns below in leave_unsorted()
static const char *leave_unsorted_[] = {
	"_ALL_EXCLUDE",
	"_BUILD_SEQ",
	"_BUILD_SETUP",
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
	"BROKEN_aarch64",
	"BROKEN_amd64",
	"BROKEN_armv6",
	"BROKEN_armv7",
	"BROKEN_DragonFly",
	"BROKEN_FreeBSD",
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
	"BROKEN_i386",
	"BROKEN_mips",
	"BROKEN_mips64",
	"BROKEN_powerpc",
	"BROKEN_powerpc64",
	"BROKEN_sparc64",
	"BROKEN",
	"BUILD_FAIL_MESSAGE",
	"BUILD-DEPENDS-LIST",
	"CARGO_CARGO_RUN",
	"CARGO_CRATES",
	"CARGO_FEATURES",
	"CATEGORIES",
	"CC",
	"CLEAN-DEPENDS-LIMITED-LIST",
	"CLEAN-DEPENDS-LIST",
	"COMMENT",
	"COPYTREE_BIN",
	"COPYTREE_SHARE",
	"CPP",
	"CXX",
	"DAEMONARGS",
	"DEPENDS-LIST",
	"DEPRECATED",
	"DESC",
	"DESKTOP_ENTRIES",
	"DO_MAKE_BUILD",
	"DO_MAKE_TEST",
	"EXPIRATION_DATE",
	"EXTRA_PATCHES",
	"EXTRACT_AFTER_ARGS",
	"EXTRACT_BEFORE_ARGS",
	"FETCH_AFTER_ARGS",
	"FETCH_ARGS",
	"FETCH_BEFORE_ARGS",
	"FETCH_LIST",
	"FETCH_LIST",
	"FLAVORS",
	"GH_TUPLE",
	"HTMLIFY",
	"IGNORE_aarch64",
	"IGNORE_amd64",
	"IGNORE_armv6",
	"IGNORE_armv7",
	"IGNORE_DragonFly",
	"IGNORE_FreeBSD",
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
	"IGNORE_FreeBSD_13_aarch64",
	"IGNORE_FreeBSD_13_amd64",
	"IGNORE_FreeBSD_13_armv6",
	"IGNORE_FreeBSD_13_armv7",
	"IGNORE_FreeBSD_13_i386",
	"IGNORE_FreeBSD_13_mips",
	"IGNORE_FreeBSD_13_mips64",
	"IGNORE_FreeBSD_13_powerpc",
	"IGNORE_FreeBSD_13_sparc64",
	"IGNORE_FreeBSD_aarch64",
	"IGNORE_FreeBSD_amd64",
	"IGNORE_FreeBSD_armv6",
	"IGNORE_FreeBSD_armv7",
	"IGNORE_FreeBSD_i386",
	"IGNORE_FreeBSD_mips",
	"IGNORE_FreeBSD_mips64",
	"IGNORE_FreeBSD_powerpc",
	"IGNORE_FreeBSD_sparc64",
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
	"LIBS",
	"LICENSE_NAME",
	"LICENSE_TEXT",
	"MAKE_JOBS_UNSAFE",
	"MASTER_SITES",
	"MASTER_SORT_AWK",
	"MISSING-DEPENDS-LIST",
	"MOZ_SED_ARGS",
	"MOZCONFIG_SED",
	"MTREE_ARGS",
	"MULTI_EOL",
	"NO_CCACHE",
	"NO_CDROM",
	"NO_PACKAGE",
	"PATCH_ARGS",
	"PATCH_DIST_ARGS",
	"RADIO_EOL",
	"RANDOM_ARGS",
	"referencehack_PRE_PATCH",
	"REINPLACE_ARGS",
	"RESTRICTED",
	"RUBY_CONFIG",
	"RUN-DEPENDS-LIST",
	"SANITY_DEPRECATED",
	"SANITY_NOTNEEDED",
	"SANITY_UNSUPPORTED",
	"SINGLE_EOL",
	"TEST_TARGET",
	"TEST-DEPENDS-LIST",
	"TEX_FORMAT_LUATEX",
	"TEXHASHDIRS",
};

// Don't indent with the rest of the variables in a paragraph
static const char *skip_goalcol_[] = {
	"CARGO_CRATES",
	"DISTVERSIONPREFIX",
	"DISTVERSIONSUFFIX",
	"EXTRACT_AFTER_ARGS",
	"EXTRACT_BEFORE_ARGS",
	"FETCH_AFTER_ARGS",
	"FETCH_BEFORE_ARGS",
	"MAKE_JOBS_UNSAFE",
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
	"BROKEN_FreeBSD",
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
compare_tokens(struct Variable *var, const char *a, const char *b)
{
	int result;
	if (compare_license_perms(var, a, b, &result) ||
	    compare_plist_files(var, a, b, &result) ||
	    compare_use_pyqt(var, a, b, &result) ||
	    compare_use_qt(var, a, b, &result)) {
		return result;
	}

	return strcasecmp(a, b);
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
