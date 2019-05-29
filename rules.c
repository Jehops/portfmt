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
static char *flavors_helpers_pattern(void);
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
	[RE_FLAVORS_HELPER]   = { "generated in compile_regular_expressions", REG_EXTENDED, {} },
	[RE_LICENSE_NAME]     = { "^(_?(-|LICENSE_NAME_[A-Za-z0-9._+ ])+|"
				  "^LICENSE_(FILE|NAME)_|"
				  "^LICENSE_(NAME|TEXT)$|"
				  "_?(-|LICENSE_TEXT_[A-Za-z0-9._+ ])+$)",
				  REG_EXTENDED, {} },
	[RE_LICENSE_PERMS]    = { "^(_?LICENSE_PERMS_(-|[A-Z0-9\\._+ ])+[+?:]?|"
				  "_LICENSE_LIST_PERMS[+?:]?|"
				  "LICENSE_PERMS[+?:]?)",
				  REG_EXTENDED, {} },
	[RE_OPTIONS_GROUP]    = { "^_?OPTIONS_(GROUP|MULTI|RADIO|SINGLE)_([A-Z0-9\\._+ ])+",
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
	"BROKEN_FreeBSD_11_powerpcspe",
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
	"BROKEN_FreeBSD_12_powerpcspe",
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
	"BROKEN_FreeBSD_13_powerpcspe",
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
	"BROKEN_FreeBSD_powerpcspe",
	"BROKEN_FreeBSD_sparc64",
	"BROKEN_FreeBSD",
	"BROKEN_i386",
	"BROKEN_mips",
	"BROKEN_mips64",
	"BROKEN_powerpc",
	"BROKEN_powerpc64",
	"BROKEN_powerpcspe",
	"BROKEN_sparc64",
	"BROKEN_SSL_REASON_base",
	"BROKEN_SSL_REASON_libressl-devel",
	"BROKEN_SSL_REASON_libressl",
	"BROKEN_SSL_REASON_openssl",
	"BROKEN_SSL_REASON_openssl111",
	"BROKEN_SSL_REASON",
	"BROKEN_SSL",
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
	"IGNORE_FreeBSD_11_powerpcspe",
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
	"IGNORE_FreeBSD_12_powerpcspe",
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
	"IGNORE_powerpcspe",
	"IGNORE_sparc64",
	"IGNORE_SSL_REASON_base",
	"IGNORE_SSL_REASON_libressl-devel",
	"IGNORE_SSL_REASON_libressl",
	"IGNORE_SSL_REASON_openssl",
	"IGNORE_SSL_REASON_openssl111",
	"IGNORE_SSL_REASON",
	"IGNORE_SSL",
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
	"CMAKE_BUILD_TYPE",
	"CMAKE_INSTALL_PREFIX",
	"CMAKE_SOURCE_PATH",
	"DISTFILES_amd64",
	"DISTFILES_i386",
	"DISTVERSIONPREFIX",
	"DISTVERSIONSUFFIX",
	"EXPIRATION_DATE",
	"EXTRACT_AFTER_ARGS",
	"EXTRACT_BEFORE_ARGS",
	"FETCH_AFTER_ARGS",
	"FETCH_BEFORE_ARGS",
	"GNU_CONFIGURE_PREFIX",
	"IGNORE_WITH_MYSQL",
	"IGNORE_WITH_PHP",
	"MAKE_JOBS_UNSAFE",
	"MASTER_SITE_SUBDIR",
	"NO_OCAML_BUILDDEPENDS",
	"NO_OCAML_RUNDEPENDS",
	"NO_OCAMLTK_BUILDDEPENDS",
	"NO_OCAMLTK_RUNDEPENDS",
	"NO_OPTIONS_SORT",
	"NOPRECIOUSMAKEVARS",
	"NOT_FOR_ARCHS_REASON_aarch64",
	"NOT_FOR_ARCHS_REASON_amd64",
	"NOT_FOR_ARCHS_REASON_armv6",
	"NOT_FOR_ARCHS_REASON_armv7",
	"NOT_FOR_ARCHS_REASON_i386",
	"NOT_FOR_ARCHS_REASON_mips",
	"NOT_FOR_ARCHS_REASON_mips64",
	"NOT_FOR_ARCHS_REASON_powerpc",
	"NOT_FOR_ARCHS_REASON_powerpc64",
	"NOT_FOR_ARCHS_REASON_powerpcspe",
	"NOT_FOR_ARCHS_REASON_sparc64",
	"NOT_FOR_ARCHS_REASON",
	"NOT_FOR_ARCHS",
	"OCAML_PKGDIRS",
	"ONLY_FOR_ARCHS_REASON_aarch64",
	"ONLY_FOR_ARCHS_REASON_amd64",
	"ONLY_FOR_ARCHS_REASON_armv6",
	"ONLY_FOR_ARCHS_REASON_armv7",
	"ONLY_FOR_ARCHS_REASON_i386",
	"ONLY_FOR_ARCHS_REASON_mips",
	"ONLY_FOR_ARCHS_REASON_mips64",
	"ONLY_FOR_ARCHS_REASON_powerpc",
	"ONLY_FOR_ARCHS_REASON_powerpc64",
	"ONLY_FOR_ARCHS_REASON_powerpcspe",
	"ONLY_FOR_ARCHS_REASON_sparc64",
	"ONLY_FOR_ARCHS_REASON",
	"ONLY_FOR_ARCHS",
	"OPTIONS_DEFAULT_aarch64",
	"OPTIONS_DEFAULT_amd64",
	"OPTIONS_DEFAULT_armv6",
	"OPTIONS_DEFAULT_armv7",
	"OPTIONS_DEFAULT_DragonFly",
	"OPTIONS_DEFAULT_FreeBSD_11",
	"OPTIONS_DEFAULT_FreeBSD_12",
	"OPTIONS_DEFAULT_FreeBSD_13",
	"OPTIONS_DEFAULT_FreeBSD",
	"OPTIONS_DEFAULT_i386",
	"OPTIONS_DEFAULT_mips",
	"OPTIONS_DEFAULT_mips64",
	"OPTIONS_DEFAULT_powerpc",
	"OPTIONS_DEFAULT_powerpc64",
	"OPTIONS_DEFAULT_powerpcspe",
	"OPTIONS_DEFAULT_sparc64",
	"OPTIONS_DEFINE_aarch64",
	"OPTIONS_DEFINE_amd64",
	"OPTIONS_DEFINE_armv6",
	"OPTIONS_DEFINE_armv7",
	"OPTIONS_DEFINE_DragonFly",
	"OPTIONS_DEFINE_FreeBSD_11",
	"OPTIONS_DEFINE_FreeBSD_12",
	"OPTIONS_DEFINE_FreeBSD_13",
	"OPTIONS_DEFINE_FreeBSD",
	"OPTIONS_DEFINE_i386",
	"OPTIONS_DEFINE_mips",
	"OPTIONS_DEFINE_mips64",
	"OPTIONS_DEFINE_powerpc",
	"OPTIONS_DEFINE_powerpc64",
	"OPTIONS_DEFINE_powerpcspe",
	"OPTIONS_DEFINE_sparc64",
	"PYDISTUTILS_BUILD_TARGET",
	"PYDISTUTILS_BUILDARGS",
	"PYDISTUTILS_CONFIGURE_TARGET",
	"PYDISTUTILS_CONFIGUREARGS",
	"PYDISTUTILS_EGGINFO",
	"PYDISTUTILS_INSTALL_TARGET",
	"PYDISTUTILS_INSTALLARGS",
	"PYDISTUTILS_PKGNAME",
	"PYDISTUTILS_PKGVERSION",
	"PYTHON_NO_DEPENDS",
	"RUBY_EXTCONF_SUBDIRS",
	"RUBY_EXTCONF",
	"RUBY_NO_BUILD_DEPENDS",
	"RUBY_NO_RUN_DEPENDS",
	"RUBY_REQUIRE",
	"USE_OCAML_CAMLP4",
	"USE_OCAML_FINDLIB",
	"USE_OCAML_LDCONFIG",
	"USE_OCAML_TK",
	"USE_OCAML_WASH",
	"USE_OCAMLFIND_PLIST",
	"USE_RUBY_EXTCONF",
	"USE_RUBY_RDOC",
	"USE_RUBY_SETUP",
	"USE_RUBYGEMS",
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
	"BROKEN_FreeBSD_11_powerpcspe",
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
	"BROKEN_FreeBSD_12_powerpcspe",
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
	"BROKEN_FreeBSD_13_powerpcspe",
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
	"BROKEN_FreeBSD_powerpcspe",
	"BROKEN_FreeBSD_sparc64",
	"BROKEN_FreeBSD",
	"BROKEN_i386",
	"BROKEN_mips",
	"BROKEN_mips64",
	"BROKEN_powerpc",
	"BROKEN_powerpc64",
	"BROKEN_powerpcspe",
	"BROKEN_sparc64",
	"BROKEN_SSL_REASON_base",
	"BROKEN_SSL_REASON_libressl-devel",
	"BROKEN_SSL_REASON_libressl",
	"BROKEN_SSL_REASON_openssl",
	"BROKEN_SSL_REASON_openssl111",
	"BROKEN_SSL_REASON",
	"BROKEN_SSL",
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
	"IGNORE_FreeBSD_11_powerpcspe",
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
	"IGNORE_FreeBSD_12_powerpcspe",
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
	"IGNORE_FreeBSD_13_powerpcspe",
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
	"IGNORE_FreeBSD_powerpcspe",
	"IGNORE_FreeBSD_sparc64",
	"IGNORE_i386",
	"IGNORE_mips",
	"IGNORE_mips64",
	"IGNORE_powerpc",
	"IGNORE_powerpc64",
	"IGNORE_powerpcspe",
	"IGNORE_sparc64",
	"IGNORE_SSL_REASON_base",
	"IGNORE_SSL_REASON_libressl-devel",
	"IGNORE_SSL_REASON_libressl",
	"IGNORE_SSL_REASON_openssl",
	"IGNORE_SSL_REASON_openssl111",
	"IGNORE_SSL_REASON",
	"IGNORE_SSL",
	"IGNORE",
	"MASTER_SITES",
	"NO_CCACHE",
	"NO_CDROM",
	"NO_PACKAGE",
	"RESTRICTED",
};

struct VariableOrderEntry {
	enum BlockType block;
	int score;
	const char *var;
};
/* Scores generated via
import sys
i = int(sys.argv[1])
for f in sys.stdin:
	print(f[:-1].replace(', "', ', {}, "'.format(i)))
	i += 50
*/ 
/* Based on:
 * https://www.freebsd.org/doc/en_US.ISO8859-1/books/porters-handbook/porting-order.html
 */
static struct VariableOrderEntry variable_order_[] = {
	{ BLOCK_PORTNAME, 0, "PORTNAME" },
	{ BLOCK_PORTNAME, 50, "PORTVERSION" },
	{ BLOCK_PORTNAME, 100, "DISTVERSIONPREFIX" },
	{ BLOCK_PORTNAME, 150, "DISTVERSION" },
	{ BLOCK_PORTNAME, 200, "DISTVERSIONSUFFIX" },
	{ BLOCK_PORTNAME, 250, "PORTREVISION" },
	{ BLOCK_PORTNAME, 300, "PORTEPOCH" },
	{ BLOCK_PORTNAME, 350, "CATEGORIES" },
	{ BLOCK_PORTNAME, 400, "MASTER_SITES" },
	{ BLOCK_PORTNAME, 450, "MASTER_SITE_SUBDIR" },
	{ BLOCK_PORTNAME, 500, "PKGNAMEPREFIX" },
	{ BLOCK_PORTNAME, 550, "PKGNAMESUFFIX" },
	{ BLOCK_PORTNAME, 600, "DISTNAME" },
	{ BLOCK_PORTNAME, 650, "EXTRACT_SUFX" },
	{ BLOCK_PORTNAME, 700, "DISTFILES" },
	{ BLOCK_PORTNAME, 750, "DIST_SUBDIR" },
	{ BLOCK_PORTNAME, 800, "EXTRACT_ONLY" },

	{ BLOCK_PATCHFILES, 10000, "PATCH_SITES" },
	{ BLOCK_PATCHFILES, 10050, "PATCHFILES" },
	{ BLOCK_PATCHFILES, 10100, "PATCH_DIST_STRIP" },

	{ BLOCK_MAINTAINER, 20000, "MAINTAINER" },
	{ BLOCK_MAINTAINER, 20050, "COMMENT" },

	{ BLOCK_LICENSE, 30000, "LICENSE" },
	{ BLOCK_LICENSE, 30050, "LICENSE_COMB" },
	{ BLOCK_LICENSE, 30100, "LICENSE_GROUPS" },
	{ BLOCK_LICENSE, 30150, "LICENSE_NAME" },
	{ BLOCK_LICENSE, 30200, "LICENSE_TEXT" },
	{ BLOCK_LICENSE, 30250, "LICENSE_FILE" },
	{ BLOCK_LICENSE, 30300, "LICENSE_PERMS" },
	{ BLOCK_LICENSE, 30350, "LICENSE_DISTFILES" },

	{ BLOCK_LICENSE_OLD, 30400, "RESTRICTED" },
	{ BLOCK_LICENSE_OLD, 30450, "RESTRICTED_FILES" },
	{ BLOCK_LICENSE_OLD, 30500, "NO_CDROM" },
	{ BLOCK_LICENSE_OLD, 30550, "NO_PACKAGE" },

	{ BLOCK_BROKEN, 40000, "DEPRECATED" },
	{ BLOCK_BROKEN, 40050, "EXPIRATION_DATE" },
	{ BLOCK_BROKEN, 40100, "FORBIDDEN" },

	{ BLOCK_BROKEN, 50000, "BROKEN" },
	{ BLOCK_BROKEN, 50050, "BROKEN_aarch64" },
	{ BLOCK_BROKEN, 50100, "BROKEN_amd64" },
	{ BLOCK_BROKEN, 50150, "BROKEN_armv6" },
	{ BLOCK_BROKEN, 50200, "BROKEN_armv7" },
	{ BLOCK_BROKEN, 50250, "BROKEN_i386" },
	{ BLOCK_BROKEN, 50300, "BROKEN_mips" },
	{ BLOCK_BROKEN, 50350, "BROKEN_mips64" },
	{ BLOCK_BROKEN, 50400, "BROKEN_powerpc" },
	{ BLOCK_BROKEN, 50450, "BROKEN_powerpc64" },
	{ BLOCK_BROKEN, 50500, "BROKEN_powerpcspe" },
	{ BLOCK_BROKEN, 50550, "BROKEN_sparc64" },
	{ BLOCK_BROKEN, 50600, "BROKEN_DragonFly" },
	{ BLOCK_BROKEN, 50650, "BROKEN_FreeBSD_11" },
	{ BLOCK_BROKEN, 50700, "BROKEN_FreeBSD_11_aarch64" },
	{ BLOCK_BROKEN, 50750, "BROKEN_FreeBSD_11_amd64" },
	{ BLOCK_BROKEN, 50800, "BROKEN_FreeBSD_11_armv6" },
	{ BLOCK_BROKEN, 50850, "BROKEN_FreeBSD_11_armv7" },
	{ BLOCK_BROKEN, 50900, "BROKEN_FreeBSD_11_i386" },
	{ BLOCK_BROKEN, 50950, "BROKEN_FreeBSD_11_mips" },
	{ BLOCK_BROKEN, 51000, "BROKEN_FreeBSD_11_mips64" },
	{ BLOCK_BROKEN, 51050, "BROKEN_FreeBSD_11_powerpc" },
	{ BLOCK_BROKEN, 51100, "BROKEN_FreeBSD_11_powerpc64" },
	{ BLOCK_BROKEN, 51150, "BROKEN_FreeBSD_11_powerpcspe" },
	{ BLOCK_BROKEN, 51200, "BROKEN_FreeBSD_11_sparc64" },
	{ BLOCK_BROKEN, 51250, "BROKEN_FreeBSD_12" },
	{ BLOCK_BROKEN, 51300, "BROKEN_FreeBSD_12_aarch64" },
	{ BLOCK_BROKEN, 51350, "BROKEN_FreeBSD_12_amd64" },
	{ BLOCK_BROKEN, 51400, "BROKEN_FreeBSD_12_armv6" },
	{ BLOCK_BROKEN, 51450, "BROKEN_FreeBSD_12_armv7" },
	{ BLOCK_BROKEN, 51500, "BROKEN_FreeBSD_12_i386" },
	{ BLOCK_BROKEN, 51550, "BROKEN_FreeBSD_12_mips" },
	{ BLOCK_BROKEN, 51600, "BROKEN_FreeBSD_12_mips64" },
	{ BLOCK_BROKEN, 51650, "BROKEN_FreeBSD_12_powerpc" },
	{ BLOCK_BROKEN, 51700, "BROKEN_FreeBSD_12_powerpcspe" },
	{ BLOCK_BROKEN, 51750, "BROKEN_FreeBSD_12_sparc64" },
	{ BLOCK_BROKEN, 51800, "BROKEN_FreeBSD_13" },
	{ BLOCK_BROKEN, 51850, "BROKEN_FreeBSD_13_aarch64" },
	{ BLOCK_BROKEN, 51900, "BROKEN_FreeBSD_13_amd64" },
	{ BLOCK_BROKEN, 51950, "BROKEN_FreeBSD_13_armv6" },
	{ BLOCK_BROKEN, 52000, "BROKEN_FreeBSD_13_armv7" },
	{ BLOCK_BROKEN, 52050, "BROKEN_FreeBSD_13_i386" },
	{ BLOCK_BROKEN, 52100, "BROKEN_FreeBSD_13_mips" },
	{ BLOCK_BROKEN, 52150, "BROKEN_FreeBSD_13_mips64" },
	{ BLOCK_BROKEN, 52200, "BROKEN_FreeBSD_13_powerpc" },
	{ BLOCK_BROKEN, 52250, "BROKEN_FreeBSD_13_powerpcspe" },
	{ BLOCK_BROKEN, 52300, "BROKEN_FreeBSD_13_sparc64" },
	{ BLOCK_BROKEN, 52350, "BROKEN_FreeBSD_aarch64" },
	{ BLOCK_BROKEN, 52400, "BROKEN_FreeBSD_amd64" },
	{ BLOCK_BROKEN, 52450, "BROKEN_FreeBSD_armv6" },
	{ BLOCK_BROKEN, 52500, "BROKEN_FreeBSD_armv7" },
	{ BLOCK_BROKEN, 52550, "BROKEN_FreeBSD_i386" },
	{ BLOCK_BROKEN, 52600, "BROKEN_FreeBSD_mips" },
	{ BLOCK_BROKEN, 52650, "BROKEN_FreeBSD_mips64" },
	{ BLOCK_BROKEN, 52700, "BROKEN_FreeBSD_powerpc" },
	{ BLOCK_BROKEN, 52750, "BROKEN_FreeBSD_powerpcspe" },
	{ BLOCK_BROKEN, 52800, "BROKEN_FreeBSD_sparc64" },
	{ BLOCK_BROKEN, 52850, "BROKEN_RUBY20" },
	{ BLOCK_BROKEN, 52900, "BROKEN_RUBY21" },
	{ BLOCK_BROKEN, 52950, "BROKEN_RUBY22" },
	{ BLOCK_BROKEN, 53000, "BROKEN_RUBY24" },
	{ BLOCK_BROKEN, 53050, "BROKEN_RUBY25" },
	{ BLOCK_BROKEN, 53100, "BROKEN_RUBY26" },
	{ BLOCK_BROKEN, 53150, "BROKEN_SSL" },
	{ BLOCK_BROKEN, 53200, "BROKEN_SSL_REASON" },
	{ BLOCK_BROKEN, 53250, "BROKEN_SSL_REASON_base" },
	{ BLOCK_BROKEN, 53300, "BROKEN_SSL_REASON_libressl" },
	{ BLOCK_BROKEN, 53350, "BROKEN_SSL_REASON_libressl-devel" },
	{ BLOCK_BROKEN, 53400, "BROKEN_SSL_REASON_openssl" },
	{ BLOCK_BROKEN, 53450, "BROKEN_SSL_REASON_openssl111" },
	{ BLOCK_BROKEN, 53500, "IGNORE" },
	{ BLOCK_BROKEN, 53550, "IGNORE_aarch64" },
	{ BLOCK_BROKEN, 53600, "IGNORE_amd64" },
	{ BLOCK_BROKEN, 53650, "IGNORE_armv6" },
	{ BLOCK_BROKEN, 53700, "IGNORE_armv7" },
	{ BLOCK_BROKEN, 53750, "IGNORE_i386" },
	{ BLOCK_BROKEN, 53800, "IGNORE_mips" },
	{ BLOCK_BROKEN, 53850, "IGNORE_mips64" },
	{ BLOCK_BROKEN, 53900, "IGNORE_powerpc" },
	{ BLOCK_BROKEN, 53950, "IGNORE_powerpc64" },
	{ BLOCK_BROKEN, 54000, "IGNORE_powerpcspe" },
	{ BLOCK_BROKEN, 54050, "IGNORE_sparc64" },
	{ BLOCK_BROKEN, 54100, "IGNORE_DragonFly" },
	{ BLOCK_BROKEN, 54150, "IGNORE_FreeBSD_11" },
	{ BLOCK_BROKEN, 54200, "IGNORE_FreeBSD_11_aarch64" },
	{ BLOCK_BROKEN, 54250, "IGNORE_FreeBSD_11_amd64" },
	{ BLOCK_BROKEN, 54300, "IGNORE_FreeBSD_11_armv6" },
	{ BLOCK_BROKEN, 54350, "IGNORE_FreeBSD_11_armv7" },
	{ BLOCK_BROKEN, 54400, "IGNORE_FreeBSD_11_i386" },
	{ BLOCK_BROKEN, 54450, "IGNORE_FreeBSD_11_mips" },
	{ BLOCK_BROKEN, 54500, "IGNORE_FreeBSD_11_mips64" },
	{ BLOCK_BROKEN, 54550, "IGNORE_FreeBSD_11_powerpc" },
	{ BLOCK_BROKEN, 54600, "IGNORE_FreeBSD_11_powerpc64" },
	{ BLOCK_BROKEN, 54650, "IGNORE_FreeBSD_11_powerpcspe" },
	{ BLOCK_BROKEN, 54700, "IGNORE_FreeBSD_11_sparc64" },
	{ BLOCK_BROKEN, 54750, "IGNORE_FreeBSD_12" },
	{ BLOCK_BROKEN, 54800, "IGNORE_FreeBSD_12_aarch64" },
	{ BLOCK_BROKEN, 54850, "IGNORE_FreeBSD_12_amd64" },
	{ BLOCK_BROKEN, 54900, "IGNORE_FreeBSD_12_armv6" },
	{ BLOCK_BROKEN, 54950, "IGNORE_FreeBSD_12_armv7" },
	{ BLOCK_BROKEN, 55000, "IGNORE_FreeBSD_12_i386" },
	{ BLOCK_BROKEN, 55050, "IGNORE_FreeBSD_12_mips" },
	{ BLOCK_BROKEN, 55100, "IGNORE_FreeBSD_12_mips64" },
	{ BLOCK_BROKEN, 55150, "IGNORE_FreeBSD_12_powerpc" },
	{ BLOCK_BROKEN, 55200, "IGNORE_FreeBSD_12_powerpc64" },
	{ BLOCK_BROKEN, 55250, "IGNORE_FreeBSD_12_powerpcspe" },
	{ BLOCK_BROKEN, 55300, "IGNORE_FreeBSD_12_sparc64" },
	{ BLOCK_BROKEN, 55350, "IGNORE_FreeBSD_13" },
	{ BLOCK_BROKEN, 55400, "IGNORE_FreeBSD_13_aarch64" },
	{ BLOCK_BROKEN, 55450, "IGNORE_FreeBSD_13_amd64" },
	{ BLOCK_BROKEN, 55500, "IGNORE_FreeBSD_13_armv6" },
	{ BLOCK_BROKEN, 55550, "IGNORE_FreeBSD_13_armv7" },
	{ BLOCK_BROKEN, 55600, "IGNORE_FreeBSD_13_i386" },
	{ BLOCK_BROKEN, 55650, "IGNORE_FreeBSD_13_mips" },
	{ BLOCK_BROKEN, 55700, "IGNORE_FreeBSD_13_mips64" },
	{ BLOCK_BROKEN, 55750, "IGNORE_FreeBSD_13_powerpc" },
	{ BLOCK_BROKEN, 55800, "IGNORE_FreeBSD_13_powerpc64" },
	{ BLOCK_BROKEN, 55850, "IGNORE_FreeBSD_13_powerpcspe" },
	{ BLOCK_BROKEN, 55900, "IGNORE_FreeBSD_13_sparc64" },
	{ BLOCK_BROKEN, 55950, "IGNORE_FreeBSD_aarch64" },
	{ BLOCK_BROKEN, 56000, "IGNORE_FreeBSD_amd64" },
	{ BLOCK_BROKEN, 56050, "IGNORE_FreeBSD_armv6" },
	{ BLOCK_BROKEN, 56100, "IGNORE_FreeBSD_armv7" },
	{ BLOCK_BROKEN, 56150, "IGNORE_FreeBSD_i386" },
	{ BLOCK_BROKEN, 56200, "IGNORE_FreeBSD_mips" },
	{ BLOCK_BROKEN, 56250, "IGNORE_FreeBSD_mips64" },
	{ BLOCK_BROKEN, 56300, "IGNORE_FreeBSD_powerpc" },
	{ BLOCK_BROKEN, 56350, "IGNORE_FreeBSD_powerpc64" },
	{ BLOCK_BROKEN, 56400, "IGNORE_FreeBSD_powerpcspe" },
	{ BLOCK_BROKEN, 56450, "IGNORE_FreeBSD_sparc64" },
	{ BLOCK_BROKEN, 56500, "IGNORE_SSL" },
	{ BLOCK_BROKEN, 56550, "IGNORE_SSL_REASON" },
	{ BLOCK_BROKEN, 56600, "IGNORE_SSL_REASON_base" },
	{ BLOCK_BROKEN, 56650, "IGNORE_SSL_REASON_libressl" },
	{ BLOCK_BROKEN, 56700, "IGNORE_SSL_REASON_libressl-devel" },
	{ BLOCK_BROKEN, 56750, "IGNORE_SSL_REASON_openssl" },
	{ BLOCK_BROKEN, 56800, "IGNORE_SSL_REASON_openssl111" },
	{ BLOCK_BROKEN, 56850, "ONLY_FOR_ARCHS" },
	{ BLOCK_BROKEN, 56900, "ONLY_FOR_ARCHS_REASON" },
	{ BLOCK_BROKEN, 56950, "ONLY_FOR_ARCHS_REASON_aarch64" },
	{ BLOCK_BROKEN, 57000, "ONLY_FOR_ARCHS_REASON_amd64" },
	{ BLOCK_BROKEN, 57050, "ONLY_FOR_ARCHS_REASON_armv6" },
	{ BLOCK_BROKEN, 57100, "ONLY_FOR_ARCHS_REASON_armv7" },
	{ BLOCK_BROKEN, 57150, "ONLY_FOR_ARCHS_REASON_i386" },
	{ BLOCK_BROKEN, 57200, "ONLY_FOR_ARCHS_REASON_mips" },
	{ BLOCK_BROKEN, 57250, "ONLY_FOR_ARCHS_REASON_mips64" },
	{ BLOCK_BROKEN, 57300, "ONLY_FOR_ARCHS_REASON_powerpc" },
	{ BLOCK_BROKEN, 57350, "ONLY_FOR_ARCHS_REASON_powerpc64" },
	{ BLOCK_BROKEN, 57400, "ONLY_FOR_ARCHS_REASON_powerpcspe" },
	{ BLOCK_BROKEN, 57450, "ONLY_FOR_ARCHS_REASON_sparc64" },
	{ BLOCK_BROKEN, 57500, "NOT_FOR_ARCHS" },
	{ BLOCK_BROKEN, 57550, "NOT_FOR_ARCHS_REASON" },
	{ BLOCK_BROKEN, 57600, "NOT_FOR_ARCHS_REASON_aarch64" },
	{ BLOCK_BROKEN, 57650, "NOT_FOR_ARCHS_REASON_amd64" },
	{ BLOCK_BROKEN, 57700, "NOT_FOR_ARCHS_REASON_armv6" },
	{ BLOCK_BROKEN, 57750, "NOT_FOR_ARCHS_REASON_armv7" },
	{ BLOCK_BROKEN, 57800, "NOT_FOR_ARCHS_REASON_i386" },
	{ BLOCK_BROKEN, 57850, "NOT_FOR_ARCHS_REASON_mips" },
	{ BLOCK_BROKEN, 57900, "NOT_FOR_ARCHS_REASON_mips64" },
	{ BLOCK_BROKEN, 57950, "NOT_FOR_ARCHS_REASON_powerpc" },
	{ BLOCK_BROKEN, 58000, "NOT_FOR_ARCHS_REASON_powerpc64" },
	{ BLOCK_BROKEN, 58050, "NOT_FOR_ARCHS_REASON_powerpcspe" },
	{ BLOCK_BROKEN, 58100, "NOT_FOR_ARCHS_REASON_sparc64" },

	{ BLOCK_DEPENDS, 60000, "FETCH_DEPENDS" },
	{ BLOCK_DEPENDS, 60050, "EXTRACT_DEPENDS" },
	{ BLOCK_DEPENDS, 60100, "PATCH_DEPENDS" },
	{ BLOCK_DEPENDS, 60150, "BUILD_DEPENDS" },
	{ BLOCK_DEPENDS, 60200, "LIB_DEPENDS" },
	{ BLOCK_DEPENDS, 60250, "RUN_DEPENDS" },
	{ BLOCK_DEPENDS, 60300, "TEST_DEPENDS" },

	{ BLOCK_FLAVORS, 70000, "FLAVORS" },
	{ BLOCK_FLAVORS, 70050, "FLAVOR" },

	{ BLOCK_FLAVORS_HELPER, 70100, "PKGNAMEPREFIX" },
	{ BLOCK_FLAVORS_HELPER, 70150, "PKGNAMESUFFIX" },
	{ BLOCK_FLAVORS_HELPER, 70200, "PKG_DEPENDS" },
	{ BLOCK_FLAVORS_HELPER, 70250, "EXTRACT_DEPENDS" },
	{ BLOCK_FLAVORS_HELPER, 70300, "PATCH_DEPENDS" },
	{ BLOCK_FLAVORS_HELPER, 70350, "FETCH_DEPENDS" },
	{ BLOCK_FLAVORS_HELPER, 70400, "BUILD_DEPENDS" },
	{ BLOCK_FLAVORS_HELPER, 70450, "LIB_DEPENDS" },
	{ BLOCK_FLAVORS_HELPER, 70500, "RUN_DEPENDS" },
	{ BLOCK_FLAVORS_HELPER, 70550, "TEST_DEPENDS" },
	{ BLOCK_FLAVORS_HELPER, 70600, "CONFLICTS" },
	{ BLOCK_FLAVORS_HELPER, 70650, "CONFLICTS_BUILD" },
	{ BLOCK_FLAVORS_HELPER, 70700, "CONFLICTS_INSTALL" },
	{ BLOCK_FLAVORS_HELPER, 70750, "DESCR" },
	{ BLOCK_FLAVORS_HELPER, 70800, "PLIST" },

	{ BLOCK_USES, 80000, "USES" },
	{ BLOCK_USES, 80050, "CPE_PART" },
	{ BLOCK_USES, 80100, "CPE_VENDOR" },
	{ BLOCK_USES, 80150, "CPE_PRODUCT" },
	{ BLOCK_USES, 80200, "CPE_VERSION" },
	{ BLOCK_USES, 80250, "CPE_UPDATE" },
	{ BLOCK_USES, 80300, "CPE_EDITION" },
	{ BLOCK_USES, 80350, "CPE_LANG" },
	{ BLOCK_USES, 80400, "CPE_SW_EDITION" },
	{ BLOCK_USES, 80450, "CPE_TARGET_SW" },
	{ BLOCK_USES, 80500, "CPE_TARGET_HW" },
	{ BLOCK_USES, 80550, "CPE_OTHER" },
	{ BLOCK_USES, 80600, "DOS2UNIX_REGEX" },
	{ BLOCK_USES, 80650, "DOS2UNIX_FILES" },
	{ BLOCK_USES, 80700, "DOS2UNIX_GLOB" },
	{ BLOCK_USES, 80750, "DOS2UNIX_WRKSRC" },
	{ BLOCK_USES, 80800, "FONTNAME" },
	{ BLOCK_USES, 80850, "FONTSDIR" },
	{ BLOCK_USES, 80900, "HORDE_DIR" },
	{ BLOCK_USES, 80950, "IGNORE_WITH_MYSQL" },
	{ BLOCK_USES, 81000, "PATHFIX_CMAKELISTSTXT" },
	{ BLOCK_USES, 81050, "PATHFIX_MAKEFILEIN" },
	{ BLOCK_USES, 81100, "PATHFIX_WRKSRC" },
	{ BLOCK_USES, 81150, "QMAIL_PREFIX" },
	{ BLOCK_USES, 81200, "QMAIL_SLAVEPORT" },
	{ BLOCK_USES, 81250, "WANT_PGSQL" },
	{ BLOCK_USES, 81300, "USE_ANT" },
	{ BLOCK_USES, 81301, "USE_ASDF" },
	{ BLOCK_USES, 81302, "USE_ASDF_FASL" },
	{ BLOCK_USES, 81303, "FASL_BUILD" },
	{ BLOCK_USES, 81304, "ASDF_MODULES" },
	{ BLOCK_USES, 81350, "USE_BINUTILS" },
	{ BLOCK_USES, 81400, "USE_CABAL" },
	{ BLOCK_USES, 81401, "USE_CLISP" },
	{ BLOCK_USES, 81450, "USE_CSTD" },
	{ BLOCK_USES, 81500, "USE_CXXSTD" },
	{ BLOCK_USES, 81550, "USE_FPC" },
	{ BLOCK_USES, 81600, "USE_GCC" },
	{ BLOCK_USES, 81650, "USE_GECKO" },
	{ BLOCK_USES, 81700, "USE_GITHUB" },
	{ BLOCK_USES, 81750, "GH_ACCOUNT" },
	{ BLOCK_USES, 81800, "GH_PROJECT" },
	{ BLOCK_USES, 81850, "GH_SUBDIR" },
	{ BLOCK_USES, 81900, "GH_TAGNAME" },
	{ BLOCK_USES, 81950, "GH_TUPLE" },
	{ BLOCK_USES, 82000, "USE_GITLAB" },
	{ BLOCK_USES, 82050, "GL_SITE" },
	{ BLOCK_USES, 82100, "GL_ACCOUNT" },
	{ BLOCK_USES, 82150, "GL_PROJECT" },
	{ BLOCK_USES, 82200, "GL_COMMIT" },
	{ BLOCK_USES, 82250, "GL_SUBDIR" },
	{ BLOCK_USES, 82300, "GL_TUPLE" },
	{ BLOCK_USES, 82350, "USE_GL" },
	{ BLOCK_USES, 82400, "USE_GNOME" },
	{ BLOCK_USES, 82401, "USE_GNOME_SUBR" },
	{ BLOCK_USES, 82450, "GCONF_SCHEMAS" },
	{ BLOCK_USES, 82500, "GLIB_SCHEMAS" },
	{ BLOCK_USES, 82550, "INSTALLS_ICONS" },
	{ BLOCK_USES, 82600, "INSTALLS_OMF" },
	{ BLOCK_USES, 82650, "USE_GNUSTEP" },
	{ BLOCK_USES, 82700, "GNUSTEP_PREFIX" },
	{ BLOCK_USES, 82750, "DEFAULT_LIBVERSION" },
	{ BLOCK_USES, 82800, "USE_GSTREAMER" },
	{ BLOCK_USES, 82850, "USE_GSTREAMER1" },
	{ BLOCK_USES, 82900, "USE_JAVA" },
	{ BLOCK_USES, 82950, "JAVA_VERSION" },
	{ BLOCK_USES, 83000, "JAVA_OS" },
	{ BLOCK_USES, 83050, "JAVA_VENDOR" },
	{ BLOCK_USES, 83100, "JAVA_EXTRACT" },
	{ BLOCK_USES, 83150, "JAVA_BUILD" },
	{ BLOCK_USES, 83200, "JAVA_RUN" },
	{ BLOCK_USES, 83250, "USE_KDE" },
	{ BLOCK_USES, 83300, "USE_LDCONFIG" },
	{ BLOCK_USES, 83350, "USE_LDCONFIG32" },
	{ BLOCK_USES, 83400, "USE_LINUX" },
	{ BLOCK_USES, 83450, "USE_LINUX_RPM" },
	{ BLOCK_USES, 83500, "USE_LOCALE" },
	{ BLOCK_USES, 83550, "USE_LXQT" },
	{ BLOCK_USES, 83600, "USE_MATE" },
	{ BLOCK_USES, 83601, "USE_MOZILLA" },
	{ BLOCK_USES, 83601, "USE_MYSQL" },
	{ BLOCK_USES, 83650, "USE_OCAML" },
	{ BLOCK_USES, 83700, "NO_OCAML_BUILDDEPENDS" },
	{ BLOCK_USES, 83750, "NO_OCAML_RUNDEPENDS" },
	{ BLOCK_USES, 83800, "USE_OCAML_FINDLIB" },
	{ BLOCK_USES, 83850, "USE_OCAML_CAMLP4" },
	{ BLOCK_USES, 83900, "USE_OCAML_TK" },
	{ BLOCK_USES, 83950, "NO_OCAMLTK_BUILDDEPENDS" },
	{ BLOCK_USES, 84000, "NO_OCAMLTK_RUNDEPENDS" },
	{ BLOCK_USES, 84050, "USE_OCAML_LDCONFIG" },
	{ BLOCK_USES, 84100, "USE_OCAMLFIND_PLIST" },
	{ BLOCK_USES, 84150, "USE_OCAML_WASH" },
	{ BLOCK_USES, 84200, "OCAML_PKGDIRS" },
	{ BLOCK_USES, 84250, "OCAML_LDLIBS" },
	{ BLOCK_USES, 84300, "USE_OPENLDAP" },
	{ BLOCK_USES, 84350, "USE_PERL5" },
	{ BLOCK_USES, 84400, "USE_PHP" },
	{ BLOCK_USES, 84450, "IGNORE_WITH_PHP" },
	{ BLOCK_USES, 84500, "PHP_MODPRIO" },
	{ BLOCK_USES, 84550, "USE_PYQT" },
	{ BLOCK_USES, 84600, "USE_PYTHON" },
	{ BLOCK_USES, 84650, "PYTHON_NO_DEPENDS" },
	{ BLOCK_USES, 84700, "PYTHON_CMD" },
	{ BLOCK_USES, 84750, "PYSETUP" },
	{ BLOCK_USES, 84800, "PYDISTUTILS_PKGNAME" },
	{ BLOCK_USES, 84850, "PYDISTUTILS_PKGVERSION" },
	{ BLOCK_USES, 84900, "PYDISTUTILS_CONFIGURE_TARGET" },
	{ BLOCK_USES, 84950, "PYDISTUTILS_BUILD_TARGET" },
	{ BLOCK_USES, 85000, "PYDISTUTILS_INSTALL_TARGET" },
	{ BLOCK_USES, 85050, "PYDISTUTILS_CONFIGUREARGS" },
	{ BLOCK_USES, 85100, "PYDISTUTILS_BUILDARGS" },
	{ BLOCK_USES, 85150, "PYDISTUTILS_INSTALLARGS" },
	{ BLOCK_USES, 85200, "PYDISTUTILS_EGGINFO" },
	{ BLOCK_USES, 85250, "USE_QT" },
	{ BLOCK_USES, 85300, "USE_RC_SUBR" },
	{ BLOCK_USES, 85350, "USE_RUBY" },
	{ BLOCK_USES, 85400, "RUBY_NO_BUILD_DEPENDS" },
	{ BLOCK_USES, 85450, "RUBY_NO_RUN_DEPENDS" },
	{ BLOCK_USES, 85500, "USE_RUBY_EXTCONF" },
	{ BLOCK_USES, 85550, "RUBY_EXTCONF" },
	{ BLOCK_USES, 85600, "RUBY_EXTCONF_SUBDIRS" },
	{ BLOCK_USES, 85650, "USE_RUBY_SETUP" },
	{ BLOCK_USES, 85700, "RUBY_SETUP" },
	{ BLOCK_USES, 85750, "USE_RUBY_RDOC" },
	{ BLOCK_USES, 85800, "RUBY_REQUIRE" },
	{ BLOCK_USES, 85850, "USE_RUBYGEMS" },
	{ BLOCK_USES, 85900, "USE_SBCL" },
	{ BLOCK_USES, 85901, "USE_SDL" },
	{ BLOCK_USES, 85950, "USE_SUBMAKE" },
	{ BLOCK_USES, 86000, "USE_TEX" },
	{ BLOCK_USES, 86050, "USE_WX" },
	{ BLOCK_USES, 86100, "USE_WX_NOT" },
	{ BLOCK_USES, 86150, "WANT_WX" },
	{ BLOCK_USES, 86151, "WANT_WX_VER" },
	{ BLOCK_USES, 86152, "WITH_WX_VER" },
	{ BLOCK_USES, 86200, "WX_COMPS" },
	{ BLOCK_USES, 86250, "WX_CONF_ARGS" },
	{ BLOCK_USES, 86300, "WX_PREMK" },
	{ BLOCK_USES, 86350, "USE_XFCE" },
	{ BLOCK_USES, 86400, "USE_XORG" },

	{ BLOCK_SHEBANGFIX, 90000, "SHEBANG_FILES" },
	{ BLOCK_SHEBANGFIX, 90050, "SHEBANG_GLOB" },
	{ BLOCK_SHEBANGFIX, 90100, "SHEBANG_REGEX" },
	{ BLOCK_SHEBANGFIX, 90150, "SHEBANG_LANG" },

	// There might be more like these.  Doing the check dynamically
	// might case more false positives than it would be worth the effort.
	{ BLOCK_SHEBANGFIX, 90200, "awk_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 90250, "awk_CMD" },
	{ BLOCK_SHEBANGFIX, 90300, "bash_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 90350, "bash_CMD" },
	{ BLOCK_SHEBANGFIX, 90400, "bltwish_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 90450, "bltwish_CMD" },
	{ BLOCK_SHEBANGFIX, 90500, "cw_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 90550, "cw_CMD" },
	{ BLOCK_SHEBANGFIX, 90600, "env_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 90650, "env_CMD" },
	{ BLOCK_SHEBANGFIX, 90700, "expect_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 90750, "expect_CMD" },
	{ BLOCK_SHEBANGFIX, 90800, "gawk_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 90850, "gawk_CMD" },
	{ BLOCK_SHEBANGFIX, 90900, "gjs_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 90950, "gjs_CMD" },
	{ BLOCK_SHEBANGFIX, 91000, "hhvm_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 91050, "hhvm_CMD" },
	{ BLOCK_SHEBANGFIX, 91100, "icmake_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 91150, "icmake_CMD" },
	{ BLOCK_SHEBANGFIX, 91200, "kaptain_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 91250, "kaptain_CMD" },
	{ BLOCK_SHEBANGFIX, 91300, "make_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 91350, "make_CMD" },
	{ BLOCK_SHEBANGFIX, 91400, "netscript_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 91450, "netscript_CMD" },
	{ BLOCK_SHEBANGFIX, 91500, "nobash_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 91550, "nobash_CMD" },
	{ BLOCK_SHEBANGFIX, 91600, "nviz_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 91650, "nviz_CMD" },
	{ BLOCK_SHEBANGFIX, 91700, "octave_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 91750, "octave_CMD" },
	{ BLOCK_SHEBANGFIX, 91800, "perl_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 91850, "perl_CMD" },
	{ BLOCK_SHEBANGFIX, 91900, "perl2_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 91950, "perl2_CMD" },
	{ BLOCK_SHEBANGFIX, 92000, "php_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 92050, "php_CMD" },
	{ BLOCK_SHEBANGFIX, 92100, "python_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 92150, "python_CMD" },
	{ BLOCK_SHEBANGFIX, 92200, "python2_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 92250, "python2_CMD" },
	{ BLOCK_SHEBANGFIX, 92300, "python3_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 92350, "python3_CMD" },
	{ BLOCK_SHEBANGFIX, 92400, "r_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 92450, "r_CMD" },
	{ BLOCK_SHEBANGFIX, 92500, "rackup_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 92550, "rackup_CMD" },
	{ BLOCK_SHEBANGFIX, 92600, "rc_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 92650, "rc_CMD" },
	{ BLOCK_SHEBANGFIX, 92700, "rep_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 92750, "rep_CMD" },
	{ BLOCK_SHEBANGFIX, 92800, "ruby_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 92850, "ruby_CMD" },
	{ BLOCK_SHEBANGFIX, 92900, "ruby2_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 92950, "ruby2_CMD" },
	{ BLOCK_SHEBANGFIX, 93000, "sed_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 93050, "sed_CMD" },
	{ BLOCK_SHEBANGFIX, 93100, "sh_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 93150, "sh_CMD" },
	{ BLOCK_SHEBANGFIX, 93200, "swipl_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 93250, "swipl_CMD" },
	{ BLOCK_SHEBANGFIX, 93300, "tk_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 93350, "tk_CMD" },
	{ BLOCK_SHEBANGFIX, 93400, "zsh_OLD_CMD" },
	{ BLOCK_SHEBANGFIX, 93450, "zsh_CMD" },

	{ BLOCK_UNIQUEFILES, 100000, "UNIQUE_PREFIX" },
	{ BLOCK_UNIQUEFILES, 100050, "UNIQUE_PREFIX_FILES" },
	{ BLOCK_UNIQUEFILES, 100100, "UNIQUE_SUFFIX" },
	{ BLOCK_UNIQUEFILES, 100150, "UNIQUE_SUFFIX_FILES" },

	{ BLOCK_APACHE, 110000, "AP_EXTRAS" },
	{ BLOCK_APACHE, 110050, "AP_INC" },
	{ BLOCK_APACHE, 110100, "AP_LIB" },
	{ BLOCK_APACHE, 110150, "AP_FAST_BUILD" },
	{ BLOCK_APACHE, 110200, "AP_GENPLIST" },
	{ BLOCK_APACHE, 110250, "MODULENAME" },
	{ BLOCK_APACHE, 110300, "SHORTMODNAME" },
	{ BLOCK_APACHE, 110350, "SRC_FILE" },

	{ BLOCK_ELIXIR, 120000, "ELIXIR_APP_NAME" },
	{ BLOCK_ELIXIR, 120050, "ELIXIR_LIB_ROOT" },
	{ BLOCK_ELIXIR, 120100, "ELIXIR_APP_ROOT" },
	{ BLOCK_ELIXIR, 120150, "ELIXIR_HIDDEN" },
	{ BLOCK_ELIXIR, 120200, "ELIXIR_LOCALE" },
	{ BLOCK_ELIXIR, 120250, "MIX_CMD" },
	{ BLOCK_ELIXIR, 120300, "MIX_COMPILE" },
	{ BLOCK_ELIXIR, 120350, "MIX_REWRITE" },
	{ BLOCK_ELIXIR, 120400, "MIX_BUILD_DEPS" },
	{ BLOCK_ELIXIR, 120450, "MIX_RUN_DEPS" },
	{ BLOCK_ELIXIR, 120500, "MIX_DOC_DIRS" },
	{ BLOCK_ELIXIR, 120550, "MIX_DOC_FILES" },
	{ BLOCK_ELIXIR, 120600, "MIX_ENV" },
	{ BLOCK_ELIXIR, 120650, "MIX_ENV_NAME" },
	{ BLOCK_ELIXIR, 120700, "MIX_BUILD_NAME" },
	{ BLOCK_ELIXIR, 120750, "MIX_TARGET" },
	{ BLOCK_ELIXIR, 120800, "MIX_EXTRA_APPS" },
	{ BLOCK_ELIXIR, 120850, "MIX_EXTRA_DIRS" },
	{ BLOCK_ELIXIR, 120900, "MIX_EXTRA_FILES" },

	{ BLOCK_EMACS, 130000, "EMACS_FLAVORS_EXCLUDE" },
	{ BLOCK_EMACS, 130050, "EMACS_NO_DEPENDS" },

	{ BLOCK_ERLANG, 140000, "ERL_APP_NAME" },
	{ BLOCK_ERLANG, 140050, "ERL_APP_ROOT" },
	{ BLOCK_ERLANG, 140100, "REBAR_CMD" },
	{ BLOCK_ERLANG, 140150, "REBAR3_CMD" },
	{ BLOCK_ERLANG, 140200, "REBAR_PROFILE" },
	{ BLOCK_ERLANG, 140250, "REBAR_TARGETS" },
	{ BLOCK_ERLANG, 140300, "ERL_BUILD_NAME" },
	{ BLOCK_ERLANG, 140350, "ERL_BUILD_DEPS" },
	{ BLOCK_ERLANG, 140400, "ERL_RUN_DEPS" },
	{ BLOCK_ERLANG, 140450, "ERL_DOCS" },

	{ BLOCK_CMAKE, 150000, "CMAKE_ARGS" },
	{ BLOCK_CMAKE, 150050, "CMAKE_OFF" },
	{ BLOCK_CMAKE, 150100, "CMAKE_ON" },
	{ BLOCK_CMAKE, 150150, "CMAKE_BUILD_TYPE" },
	{ BLOCK_CMAKE, 150200, "CMAKE_INSTALL_PREFIX" },
	{ BLOCK_CMAKE, 150250, "CMAKE_SOURCE_PATH" },

	{ BLOCK_CONFIGURE, 160000, "HAS_CONFIGURE" },
	{ BLOCK_CONFIGURE, 160050, "GNU_CONFIGURE" },
	{ BLOCK_CONFIGURE, 160100, "GNU_CONFIGURE_PREFIX" },
	{ BLOCK_CONFIGURE, 160150, "CONFIGURE_CMD" },
	{ BLOCK_CONFIGURE, 160200, "CONFIGURE_LOG" },
	{ BLOCK_CONFIGURE, 160250, "CONFIGURE_SCRIPT" },
	{ BLOCK_CONFIGURE, 160300, "CONFIGURE_SHELL" },
	{ BLOCK_CONFIGURE, 160350, "CONFIGURE_ARGS" },
	{ BLOCK_CONFIGURE, 160400, "CONFIGURE_ENV" },
	{ BLOCK_CONFIGURE, 160450, "CONFIGURE_OUTSOURCE" },
	{ BLOCK_CONFIGURE, 160500, "CONFIGURE_TARGET" },

	{ BLOCK_QMAKE, 180000, "QMAKE_ARGS" },
	{ BLOCK_QMAKE, 180050, "QMAKE_ENV" },
	{ BLOCK_QMAKE, 180100, "QMAKE_CONFIGUIRE_ARGS" },
	{ BLOCK_QMAKE, 180150, "QMAKE_SOURCE_PATH" },

	{ BLOCK_MESON, 190000, "MESON_ARGS" },
	{ BLOCK_MESON, 190050, "MESON_BUILD_DIR" },

	{ BLOCK_SCONS, 193000, "CCFLAGS" },
	{ BLOCK_SCONS, 193050, "CPPPATH" },
	{ BLOCK_SCONS, 193100, "LINKFLAGS" },
	{ BLOCK_SCONS, 193150, "LIBPATH" },

	{ BLOCK_CARGO1, 200000, "CARGO_CRATES" },
	{ BLOCK_CARGO1, 200050, "CARGO_USE_GITHUB" },
	{ BLOCK_CARGO1, 200100, "CARGO_USE_GITLAB" },
	{ BLOCK_CARGO1, 200150, "CARGO_CARGOLOCK" },
	{ BLOCK_CARGO1, 200200, "CARGO_CARGOTOML" },
	{ BLOCK_CARGO1, 200250, "CARGO_FEATURES" },

	{ BLOCK_CARGO2, 210000, "CARGO_BUILDDEP" },
	{ BLOCK_CARGO2, 210050, "CARGO_BUILD" },
	{ BLOCK_CARGO2, 210100, "CARGO_BUILD_ARGS" },
	{ BLOCK_CARGO2, 210150, "CARGO_INSTALL" },
	{ BLOCK_CARGO2, 210200, "CARGO_INSTALL_ARGS" },
	{ BLOCK_CARGO2, 210250, "CARGO_TEST" },
	{ BLOCK_CARGO2, 210300, "CARGO_TEST_ARGS" },
	{ BLOCK_CARGO2, 210350, "CARGO_UPDATE_ARGS" },
	{ BLOCK_CARGO2, 210400, "CARGO_CARGO_BIN" },
	{ BLOCK_CARGO2, 210450, "CARGO_DIST_SUBDIR" },
	{ BLOCK_CARGO2, 210500, "CARGO_ENV" },
	{ BLOCK_CARGO2, 210550, "CARGO_TARGET_DIR" },
	{ BLOCK_CARGO2, 210600, "CARGO_VENDOR_DIR" },

	{ BLOCK_GO, 220000, "GO_PKGNAME" },
	{ BLOCK_GO, 220050, "GO_TARGET" },
	{ BLOCK_GO, 220100, "CGO_ENABLED" },
	{ BLOCK_GO, 220150, "CGO_CFLAGS" },
	{ BLOCK_GO, 220200, "CGO_LDFLAGS" },
	{ BLOCK_GO, 220250, "GO_BUILDFLAGS" },

	{ BLOCK_LAZARUS, 230000, "NO_LAZBUILD" },
	{ BLOCK_LAZARUS, 230050, "LAZARUS_PROJECT_FILES" },
	{ BLOCK_LAZARUS, 230100, "LAZARUS_DIR" },
	{ BLOCK_LAZARUS, 230150, "LAZBUILD_ARGS" },
	{ BLOCK_LAZARUS, 230200, "LAZARUS_NO_FLAVORS" },

	{ BLOCK_LINUX, 240000, "BIN_DISTNAMES" },
	{ BLOCK_LINUX, 240050, "LIB_DISTNAMES" },
	{ BLOCK_LINUX, 240100, "SHARE_DISTNAMES" },
	{ BLOCK_LINUX, 240150, "SRC_DISTFILES" },

	{ BLOCK_NUGET, 250000, "NUGET_DEPENDS" },
	{ BLOCK_NUGET, 250050, "NUGET_PACKAGEDIR" },
	{ BLOCK_NUGET, 250100, "NUGET_LAYOUT" },
	{ BLOCK_NUGET, 250150, "NUGET_FEEDS" },
	{ BLOCK_NUGET, 250200, "_URL" },
	{ BLOCK_NUGET, 250250, "_FILE" },
	{ BLOCK_NUGET, 250300, "_DEPENDS" },
	{ BLOCK_NUGET, 250350, "PAKET_PACKAGEDIR" },
	{ BLOCK_NUGET, 250400, "PAKET_DEPENDS" },

	{ BLOCK_MAKE, 255000, "MAKEFILE" },
	{ BLOCK_MAKE, 255050, "MAKE_CMD" },
	{ BLOCK_MAKE, 255100, "MAKE_ARGS" },
	// MAKE_ARGS_arch/gcc/clang is not a framework
	// but is in common use so list it here too.
	{ BLOCK_MAKE, 255150, "MAKE_ARGS_clang" },
	{ BLOCK_MAKE, 255200, "MAKE_ARGS_gcc" },
	{ BLOCK_MAKE, 255250, "MAKE_ARGS_aarch64" },
	{ BLOCK_MAKE, 255300, "MAKE_ARGS_amd64" },
	{ BLOCK_MAKE, 255350, "MAKE_ARGS_armv6" },
	{ BLOCK_MAKE, 255400, "MAKE_ARGS_armv7" },
	{ BLOCK_MAKE, 255450, "MAKE_ARGS_mips" },
	{ BLOCK_MAKE, 255500, "MAKE_ARGS_mips64" },
	{ BLOCK_MAKE, 255550, "MAKE_ARGS_powerpc" },
	{ BLOCK_MAKE, 255600, "MAKE_ARGS_powerpc64" },
	{ BLOCK_MAKE, 255650, "MAKE_ARGS_powerpcspe" },
	{ BLOCK_MAKE, 255700, "MAKE_ARGS_sparc64" },
	{ BLOCK_MAKE, 255750, "MAKE_ENV" },
	{ BLOCK_MAKE, 255800, "MAKE_ENV_clang" },
	{ BLOCK_MAKE, 255850, "MAKE_ENV_gcc" },
	{ BLOCK_MAKE, 255900, "MAKE_ENV_aarch64" },
	{ BLOCK_MAKE, 255950, "MAKE_ENV_amd64" },
	{ BLOCK_MAKE, 256000, "MAKE_ENV_armv6" },
	{ BLOCK_MAKE, 256050, "MAKE_ENV_armv7" },
	{ BLOCK_MAKE, 256100, "MAKE_ENV_mips" },
	{ BLOCK_MAKE, 256150, "MAKE_ENV_mips64" },
	{ BLOCK_MAKE, 256200, "MAKE_ENV_powerpc" },
	{ BLOCK_MAKE, 256250, "MAKE_ENV_powerpc64" },
	{ BLOCK_MAKE, 256300, "MAKE_ENV_powerpcspe" },
	{ BLOCK_MAKE, 256350, "MAKE_ENV_sparc64" },
	{ BLOCK_MAKE, 256400, "MAKE_FLAGS" },
	{ BLOCK_MAKE, 256450, "MAKE_JOBS_UNSAFE" },
	{ BLOCK_MAKE, 256500, "TEST_ARGS" },
	{ BLOCK_MAKE, 256550, "TEST_ENV" },
	{ BLOCK_MAKE, 256600, "ALL_TARGET" },
	{ BLOCK_MAKE, 256650, "INSTALL_TARGET" },
	{ BLOCK_MAKE, 256700, "TEST_TARGET" },

	{ BLOCK_CFLAGS, 260000, "CFLAGS" },
	{ BLOCK_CFLAGS, 260050, "CFLAGS_clang" },
	{ BLOCK_CFLAGS, 260100, "CFLAGS_gcc" },
	{ BLOCK_CFLAGS, 260150, "CFLAGS_aarch64" },
	{ BLOCK_CFLAGS, 260200, "CFLAGS_amd64" },
	{ BLOCK_CFLAGS, 260250, "CFLAGS_armv6" },
	{ BLOCK_CFLAGS, 260300, "CFLAGS_armv7" },
	{ BLOCK_CFLAGS, 260350, "CFLAGS_i386" },
	{ BLOCK_CFLAGS, 260400, "CFLAGS_mips" },
	{ BLOCK_CFLAGS, 260450, "CFLAGS_mips64" },
	{ BLOCK_CFLAGS, 260500, "CFLAGS_powerpc" },
	{ BLOCK_CFLAGS, 260550, "CFLAGS_powerpc64" },
	{ BLOCK_CFLAGS, 260600, "CFLAGS_powerpcspe" },
	{ BLOCK_CFLAGS, 260650, "CFLAGS_sparc64" },
	{ BLOCK_CFLAGS, 260700, "CPPFLAGS" },
	{ BLOCK_CFLAGS, 260750, "CPPFLAGS_clang" },
	{ BLOCK_CFLAGS, 260800, "CPPFLAGS_gcc" },
	{ BLOCK_CFLAGS, 260850, "CPPFLAGS_aarch64" },
	{ BLOCK_CFLAGS, 260900, "CPPFLAGS_amd64" },
	{ BLOCK_CFLAGS, 260950, "CPPFLAGS_armv6" },
	{ BLOCK_CFLAGS, 261000, "CPPFLAGS_armv7" },
	{ BLOCK_CFLAGS, 261050, "CPPFLAGS_i386" },
	{ BLOCK_CFLAGS, 261100, "CPPFLAGS_mips" },
	{ BLOCK_CFLAGS, 261150, "CPPFLAGS_mips64" },
	{ BLOCK_CFLAGS, 261200, "CPPFLAGS_powerpc" },
	{ BLOCK_CFLAGS, 261250, "CPPFLAGS_powerpc64" },
	{ BLOCK_CFLAGS, 261300, "CPPFLAGS_powerpcspe" },
	{ BLOCK_CFLAGS, 261350, "CPPFLAGS_sparc64" },
	{ BLOCK_CFLAGS, 261400, "CXXFLAGS" },
	{ BLOCK_CFLAGS, 261450, "CXXFLAGS_clang" },
	{ BLOCK_CFLAGS, 261500, "CXXFLAGS_gcc" },
	{ BLOCK_CFLAGS, 261550, "CXXFLAGS_aarch64" },
	{ BLOCK_CFLAGS, 261600, "CXXFLAGS_amd64" },
	{ BLOCK_CFLAGS, 261650, "CXXFLAGS_armv6" },
	{ BLOCK_CFLAGS, 261700, "CXXFLAGS_armv7" },
	{ BLOCK_CFLAGS, 261750, "CXXFLAGS_i386" },
	{ BLOCK_CFLAGS, 261800, "CXXFLAGS_mips" },
	{ BLOCK_CFLAGS, 261850, "CXXFLAGS_mips64" },
	{ BLOCK_CFLAGS, 261900, "CXXFLAGS_powerpc" },
	{ BLOCK_CFLAGS, 261950, "CXXFLAGS_powerpc64" },
	{ BLOCK_CFLAGS, 262000, "CXXFLAGS_powerpcspe" },
	{ BLOCK_CFLAGS, 262050, "CXXFLAGS_sparc64" },
	{ BLOCK_CFLAGS, 262100, "LDFLAGS" },
	{ BLOCK_CFLAGS, 262150, "LDFLAGS_aarch64" },
	{ BLOCK_CFLAGS, 262200, "LDFLAGS_amd64" },
	{ BLOCK_CFLAGS, 262250, "LDFLAGS_armv6" },
	{ BLOCK_CFLAGS, 262300, "LDFLAGS_armv7" },
	{ BLOCK_CFLAGS, 262350, "LDFLAGS_i386" },
	{ BLOCK_CFLAGS, 262400, "LDFLAGS_mips" },
	{ BLOCK_CFLAGS, 262450, "LDFLAGS_mips64" },
	{ BLOCK_CFLAGS, 262500, "LDFLAGS_powerpc" },
	{ BLOCK_CFLAGS, 262550, "LDFLAGS_powerpc64" },
	{ BLOCK_CFLAGS, 262600, "LDFLAGS_powerpcspe" },
	{ BLOCK_CFLAGS, 262650, "LDFLAGS_sparc64" },
	{ BLOCK_CFLAGS, 262700, "LIBS" },
	{ BLOCK_CFLAGS, 262750, "LLD_UNSAFE" },
	{ BLOCK_CFLAGS, 262800, "SSP_UNSAFE" },
	{ BLOCK_CFLAGS, 262850, "SSP_CFLAGS" },

	{ BLOCK_CONFLICTS, 270000, "CONFLICTS" },
	{ BLOCK_CONFLICTS, 270050, "CONFLICTS_BUILD" },
	{ BLOCK_CONFLICTS, 270100, "CONFLICTS_INSTALL" },

	{ BLOCK_STANDARD, 280000, "ARCH" },
	{ BLOCK_STANDARD, 280050, "AR" },
	{ BLOCK_STANDARD, 280100, "AS" },
	{ BLOCK_STANDARD, 280150, "CC" },
	{ BLOCK_STANDARD, 280200, "CXX" },
	{ BLOCK_STANDARD, 280250, "LD" },
	{ BLOCK_STANDARD, 280251, "STRIP" },
	{ BLOCK_STANDARD, 280300, "DESCR" },
	{ BLOCK_STANDARD, 280301, "EXTRA_PATCHES" },
	{ BLOCK_STANDARD, 280348, "ETCDIR" },
	{ BLOCK_STANDARD, 280349, "ETCDIR_REL" },
	{ BLOCK_STANDARD, 280350, "DATADIR" },
	{ BLOCK_STANDARD, 280400, "DATADIR_REL" },
	{ BLOCK_STANDARD, 280450, "DOCSDIR" },
	{ BLOCK_STANDARD, 280500, "DOCSDIR_REL" },
	{ BLOCK_STANDARD, 280550, "EXAMPLESDIR" },
	{ BLOCK_STANDARD, 280600, "FILESDIR" },
	{ BLOCK_STANDARD, 280650, "MASTERDIR" },
	{ BLOCK_STANDARD, 280700, "WWWDIR" },
	{ BLOCK_STANDARD, 280750, "WWWDIR_REL" },
	{ BLOCK_STANDARD, 280800, "DESKTOP_ENTRIES" },
	{ BLOCK_STANDARD, 280850, "DESKTOPDIR" },
	{ BLOCK_STANDARD, 280900, "NO_ARCH" },
	{ BLOCK_STANDARD, 280950, "NO_ARCH_IGNORE" },
	{ BLOCK_STANDARD, 281000, "NO_BUILD" },
	{ BLOCK_STANDARD, 281050, "NOCCACHE" },
	{ BLOCK_STANDARD, 281100, "NO_CCACHE" },
	{ BLOCK_STANDARD, 281150, "NO_MTREE" },
	{ BLOCK_STANDARD, 281200, "MTREE_FILE" },
	{ BLOCK_STANDARD, 281250, "NOPRECIOUSMAKEVARS" },
	{ BLOCK_STANDARD, 281300, "NO_TEST" },
	{ BLOCK_STANDARD, 281350, "NO_WRKSUBDIR" },
	{ BLOCK_STANDARD, 281400, "BUILD_WRKSRC" },
	{ BLOCK_STANDARD, 281450, "CONFIGURE_WRKSRC" },
	{ BLOCK_STANDARD, 281500, "INSTALL_WRKSRC" },
	{ BLOCK_STANDARD, 281501, "PATCH_WRKSRC" },
	{ BLOCK_STANDARD, 281550, "TEST_WRKSRC" },
	{ BLOCK_STANDARD, 281600, "PORTSCOUT" },
	{ BLOCK_STANDARD, 281650, "SUB_FILES" },
	{ BLOCK_STANDARD, 281700, "SUB_LIST" },
	{ BLOCK_STANDARD, 281850, "SCRIPTDIR" },
	{ BLOCK_STANDARD, 281900, "WITHOUT_FBSD10_FIX" },
	{ BLOCK_STANDARD, 281950, "WRKDIR" },
	{ BLOCK_STANDARD, 282000, "WRKSRC" },
	{ BLOCK_STANDARD, 282050, "WRKSRC_SUBDIR" },
	{ BLOCK_STANDARD, 282100, "STAGEDIR" },
	// TODO: Missing *many* variables here

	{ BLOCK_USERS, 289000, "USERS" },
	{ BLOCK_USERS, 289050, "GROUPS" },

	{ BLOCK_PLIST, 290000, "INFO" },
	{ BLOCK_PLIST, 290050, "INFO_PATH" },
	{ BLOCK_PLIST, 290100, "PLIST" },
	{ BLOCK_PLIST, 290101, "POST_PLIST" },
	{ BLOCK_PLIST, 290150, "TMPPLIST" },
	{ BLOCK_PLIST, 290200, "PLIST_DIRS" },
	{ BLOCK_PLIST, 290250, "PLIST_FILES" },
	{ BLOCK_PLIST, 290300, "PLIST_SUB" },
	{ BLOCK_PLIST, 290350, "PORTDATA" },
	{ BLOCK_PLIST, 290400, "PORTDOCS" },
	{ BLOCK_PLIST, 290450, "PORTEXAMPLES" },

	{ BLOCK_OPTDEF, 300000, "OPTIONS_DEFINE" },
	// These do not exist in the framework but some ports
	// define them themselves
	{ BLOCK_OPTDEF, 300001, "OPTIONS_DEFINE_DragonFly" },
	{ BLOCK_OPTDEF, 300002, "OPTIONS_DEFINE_FreeBSD" },
	{ BLOCK_OPTDEF, 300003, "OPTIONS_DEFINE_FreeBSD_11" },
	{ BLOCK_OPTDEF, 300004, "OPTIONS_DEFINE_FreeBSD_12" },
	{ BLOCK_OPTDEF, 300005, "OPTIONS_DEFINE_FreeBSD_13" },
	{ BLOCK_OPTDEF, 300050, "OPTIONS_DEFINE_aarch64" },
	{ BLOCK_OPTDEF, 300100, "OPTIONS_DEFINE_amd64" },
	{ BLOCK_OPTDEF, 300150, "OPTIONS_DEFINE_armv6" },
	{ BLOCK_OPTDEF, 300200, "OPTIONS_DEFINE_armv7" },
	{ BLOCK_OPTDEF, 300250, "OPTIONS_DEFINE_i386" },
	{ BLOCK_OPTDEF, 300300, "OPTIONS_DEFINE_mips" },
	{ BLOCK_OPTDEF, 300350, "OPTIONS_DEFINE_mips64" },
	{ BLOCK_OPTDEF, 300400, "OPTIONS_DEFINE_powerpc" },
	{ BLOCK_OPTDEF, 300450, "OPTIONS_DEFINE_powerpc64" },
	{ BLOCK_OPTDEF, 300451, "OPTIONS_DEFINE_powerpcspe" },
	{ BLOCK_OPTDEF, 300500, "OPTIONS_DEFINE_sparc64" },
	{ BLOCK_OPTDEF, 300550, "OPTIONS_DEFAULT" },
	{ BLOCK_OPTDEF, 300600, "OPTIONS_DEFAULT_DragonFly" },
	{ BLOCK_OPTDEF, 300650, "OPTIONS_DEFAULT_FreeBSD" },
	{ BLOCK_OPTDEF, 300700, "OPTIONS_DEFAULT_FreeBSD_11" },
	{ BLOCK_OPTDEF, 300750, "OPTIONS_DEFAULT_FreeBSD_12" },
	{ BLOCK_OPTDEF, 300800, "OPTIONS_DEFAULT_FreeBSD_13" },
	{ BLOCK_OPTDEF, 300850, "OPTIONS_DEFAULT_aarch64" },
	{ BLOCK_OPTDEF, 300900, "OPTIONS_DEFAULT_amd64" },
	{ BLOCK_OPTDEF, 300950, "OPTIONS_DEFAULT_armv6" },
	{ BLOCK_OPTDEF, 301000, "OPTIONS_DEFAULT_armv7" },
	{ BLOCK_OPTDEF, 301050, "OPTIONS_DEFAULT_i386" },
	{ BLOCK_OPTDEF, 301100, "OPTIONS_DEFAULT_mips" },
	{ BLOCK_OPTDEF, 301150, "OPTIONS_DEFAULT_mips64" },
	{ BLOCK_OPTDEF, 301200, "OPTIONS_DEFAULT_powerpc" },
	{ BLOCK_OPTDEF, 301250, "OPTIONS_DEFAULT_powerpc64" },
	{ BLOCK_OPTDEF, 301250, "OPTIONS_DEFAULT_powerpcspe" },
	{ BLOCK_OPTDEF, 301300, "OPTIONS_DEFAULT_sparc64" },
	{ BLOCK_OPTDEF, 301350, "OPTIONS_GROUP" },
	{ BLOCK_OPTDEF, 301400, "OPTIONS_MULTI" },
	{ BLOCK_OPTDEF, 301450, "OPTIONS_RADIO" },
	{ BLOCK_OPTDEF, 301500, "OPTIONS_SINGLE" },
	{ BLOCK_OPTDEF, 301550, "OPTIONS_EXCLUDE" },
	{ BLOCK_OPTDEF, 301600, "OPTIONS_EXCLUDE_DragonFly" },
	{ BLOCK_OPTDEF, 301650, "OPTIONS_EXCLUDE_FreeBSD" },
	{ BLOCK_OPTDEF, 301700, "OPTIONS_EXCLUDE_FreeBSD_11" },
	{ BLOCK_OPTDEF, 301750, "OPTIONS_EXCLUDE_FreeBSD_12" },
	{ BLOCK_OPTDEF, 301800, "OPTIONS_EXCLUDE_FreeBSD_13" },
	{ BLOCK_OPTDEF, 301850, "OPTIONS_EXCLUDE_aarch64" },
	{ BLOCK_OPTDEF, 301900, "OPTIONS_EXCLUDE_amd64" },
	{ BLOCK_OPTDEF, 301950, "OPTIONS_EXCLUDE_armv6" },
	{ BLOCK_OPTDEF, 302000, "OPTIONS_EXCLUDE_armv7" },
	{ BLOCK_OPTDEF, 302050, "OPTIONS_EXCLUDE_i386" },
	{ BLOCK_OPTDEF, 302100, "OPTIONS_EXCLUDE_mips" },
	{ BLOCK_OPTDEF, 302150, "OPTIONS_EXCLUDE_mips64" },
	{ BLOCK_OPTDEF, 302200, "OPTIONS_EXCLUDE_powerpc" },
	{ BLOCK_OPTDEF, 302250, "OPTIONS_EXCLUDE_powerpc64" },
	{ BLOCK_OPTDEF, 302250, "OPTIONS_EXCLUDE_powerpcspe" },
	{ BLOCK_OPTDEF, 302300, "OPTIONS_EXCLUDE_sparc64" },
	{ BLOCK_OPTDEF, 302350, "OPTIONS_SLAVE" },
	{ BLOCK_OPTDEF, 302400, "NO_OPTIONS_SORT" },
	{ BLOCK_OPTDEF, 302450, "OPTIONS_SUB" },

	{ BLOCK_OPTDESC, 310000, "DESC" },

	{ BLOCK_OPTHELPER, 320000, "IMPLIES" },
	{ BLOCK_OPTHELPER, 320050, "PREVENTS" },
	{ BLOCK_OPTHELPER, 320100, "PREVENTS_MSG" },
	{ BLOCK_OPTHELPER, 320150, "CATEGORIES_OFF" },
	{ BLOCK_OPTHELPER, 320200, "CATEGORIES" },
	{ BLOCK_OPTHELPER, 320250, "MASTER_SITES_OFF" },
	{ BLOCK_OPTHELPER, 320300, "MASTER_SITES" },
	{ BLOCK_OPTHELPER, 320350, "DISTFILES_OFF" },
	{ BLOCK_OPTHELPER, 320400, "DISTFILES" },
	{ BLOCK_OPTHELPER, 320450, "PATCH_SITES_OFF" },
	{ BLOCK_OPTHELPER, 320500, "PATCH_SITES" },
	{ BLOCK_OPTHELPER, 320550, "PATCHFILES_OFF" },
	{ BLOCK_OPTHELPER, 320600, "PATCHFILES" },
	{ BLOCK_OPTHELPER, 320650, "BUILD_DEPENDS_OFF" },
	{ BLOCK_OPTHELPER, 320700, "BUILD_DEPENDS" },
	{ BLOCK_OPTHELPER, 320750, "EXTRACT_DEPENDS_OFF" },
	{ BLOCK_OPTHELPER, 320800, "EXTRACT_DEPENDS" },
	{ BLOCK_OPTHELPER, 320850, "FETCH_DEPENDS_OFF" },
	{ BLOCK_OPTHELPER, 320900, "FETCH_DEPENDS" },
	{ BLOCK_OPTHELPER, 320950, "LIB_DEPENDS_OFF" },
	{ BLOCK_OPTHELPER, 321000, "LIB_DEPENDS" },
	{ BLOCK_OPTHELPER, 321050, "PATCH_DEPENDS_OFF" },
	{ BLOCK_OPTHELPER, 321100, "PATCH_DEPENDS" },
	{ BLOCK_OPTHELPER, 321150, "PKG_DEPENDS_OFF" },
	{ BLOCK_OPTHELPER, 321200, "PKG_DEPENDS" },
	{ BLOCK_OPTHELPER, 321250, "RUN_DEPENDS_OFF" },
	{ BLOCK_OPTHELPER, 321300, "RUN_DEPENDS" },
	{ BLOCK_OPTHELPER, 321350, "TEST_DEPENDS_OFF" },
	{ BLOCK_OPTHELPER, 321400, "TEST_DEPENDS" },
	{ BLOCK_OPTHELPER, 321450, "USES" },
	{ BLOCK_OPTHELPER, 321500, "USES_OFF" },
	{ BLOCK_OPTHELPER, 321550, "USE" },
	{ BLOCK_OPTHELPER, 321600, "USE_OFF" },
	{ BLOCK_OPTHELPER, 321650, "USE_CABAL" },
	{ BLOCK_OPTHELPER, 321700, "GH_ACCOUNT_OFF" },
	{ BLOCK_OPTHELPER, 321750, "GH_ACCOUNT" },
	{ BLOCK_OPTHELPER, 321800, "GH_PROJECT_OFF" },
	{ BLOCK_OPTHELPER, 321850, "GH_PROJECT" },
	{ BLOCK_OPTHELPER, 321900, "GH_SUBDIR_OFF" },
	{ BLOCK_OPTHELPER, 321950, "GH_SUBDIR" },
	{ BLOCK_OPTHELPER, 322000, "GH_TAGNAME_OFF" },
	{ BLOCK_OPTHELPER, 322050, "GH_TAGNAME" },
	{ BLOCK_OPTHELPER, 322100, "GH_TUPLE_OFF" },
	{ BLOCK_OPTHELPER, 322150, "GH_TUPLE" },
	{ BLOCK_OPTHELPER, 322200, "GL_ACCOUNT_OFF" },
	{ BLOCK_OPTHELPER, 322250, "GL_ACCOUNT" },
	{ BLOCK_OPTHELPER, 322300, "GL_COMMIT_OFF" },
	{ BLOCK_OPTHELPER, 322350, "GL_COMMIT" },
	{ BLOCK_OPTHELPER, 322400, "GL_PROJECT_OFF" },
	{ BLOCK_OPTHELPER, 322450, "GL_PROJECT" },
	{ BLOCK_OPTHELPER, 322500, "GL_SITE_OFF" },
	{ BLOCK_OPTHELPER, 322550, "GL_SITE" },
	{ BLOCK_OPTHELPER, 322600, "GL_SUBDIR_OFF" },
	{ BLOCK_OPTHELPER, 322650, "GL_SUBDIR" },
	{ BLOCK_OPTHELPER, 322700, "GL_TUPLE_OFF" },
	{ BLOCK_OPTHELPER, 322750, "GL_TUPLE" },
	{ BLOCK_OPTHELPER, 322800, "CMAKE_BOOL_OFF" },
	{ BLOCK_OPTHELPER, 322850, "CMAKE_BOOL" },
	{ BLOCK_OPTHELPER, 322900, "CMAKE_OFF" },
	{ BLOCK_OPTHELPER, 322950, "CMAKE_ON" },
	{ BLOCK_OPTHELPER, 323000, "CONFIGURE_OFF" },
	{ BLOCK_OPTHELPER, 323050, "CONFIGURE_ON" },
	{ BLOCK_OPTHELPER, 323100, "CONFIGURE_ENABLE" },
	{ BLOCK_OPTHELPER, 323150, "CONFIGURE_WITH" },
	{ BLOCK_OPTHELPER, 323200, "CONFIGURE_ENV_OFF" },
	{ BLOCK_OPTHELPER, 323250, "CONFIGURE_ENV" },
	{ BLOCK_OPTHELPER, 323300, "MESON_DISABLED" },
	{ BLOCK_OPTHELPER, 323350, "MESON_ENABLED" },
	{ BLOCK_OPTHELPER, 323400, "MESON_FALSE" },
	{ BLOCK_OPTHELPER, 323450, "MESON_NO" },
	{ BLOCK_OPTHELPER, 323500, "MESON_OFF" },
	{ BLOCK_OPTHELPER, 323550, "MESON_ON" },
	{ BLOCK_OPTHELPER, 323600, "MESON_TRUE" },
	{ BLOCK_OPTHELPER, 323650, "MESON_YES" },
	{ BLOCK_OPTHELPER, 323700, "ALL_TARGET_OFF" },
	{ BLOCK_OPTHELPER, 323750, "ALL_TARGET" },
	{ BLOCK_OPTHELPER, 323800, "BINARY_ALIAS_OFF" },
	{ BLOCK_OPTHELPER, 323850, "BINARY_ALIAS" },
	{ BLOCK_OPTHELPER, 323900, "BROKEN_OFF" },
	{ BLOCK_OPTHELPER, 323950, "BROKEN" },
	{ BLOCK_OPTHELPER, 324000, "CABAL_FLAGS" },
	{ BLOCK_OPTHELPER, 324050, "CFLAGS_OFF" },
	{ BLOCK_OPTHELPER, 324100, "CFLAGS" },
	{ BLOCK_OPTHELPER, 324150, "CONFLICTS_BUILD_OFF" },
	{ BLOCK_OPTHELPER, 324200, "CONFLICTS_BUILD" },
	{ BLOCK_OPTHELPER, 324250, "CONFLICTS_INSTALL_OFF" },
	{ BLOCK_OPTHELPER, 324300, "CONFLICTS_INSTALL" },
	{ BLOCK_OPTHELPER, 324350, "CONFLICTS_OFF" },
	{ BLOCK_OPTHELPER, 324400, "CONFLICTS" },
	{ BLOCK_OPTHELPER, 324450, "CPPFLAGS_OFF" },
	{ BLOCK_OPTHELPER, 324500, "CPPFLAGS" },
	{ BLOCK_OPTHELPER, 324550, "CXXFLAGS_OFF" },
	{ BLOCK_OPTHELPER, 324600, "CXXFLAGS" },
	{ BLOCK_OPTHELPER, 324650, "DESKTOP_ENTRIES_OFF" },
	{ BLOCK_OPTHELPER, 324700, "DESKTOP_ENTRIES" },
	{ BLOCK_OPTHELPER, 324750, "EXECUTABLES" },
	{ BLOCK_OPTHELPER, 324800, "EXTRA_PATCHES_OFF" },
	{ BLOCK_OPTHELPER, 324850, "EXTRA_PATCHES" },
	{ BLOCK_OPTHELPER, 324900, "EXTRACT_ONLY_OFF" },
	{ BLOCK_OPTHELPER, 324950, "EXTRACT_ONLY" },
	{ BLOCK_OPTHELPER, 325000, "IGNORE_OFF" },
	{ BLOCK_OPTHELPER, 325050, "IGNORE" },
	{ BLOCK_OPTHELPER, 325100, "INFO_OFF" },
	{ BLOCK_OPTHELPER, 325150, "INFO" },
	{ BLOCK_OPTHELPER, 325200, "INSTALL_TARGET_OFF" },
	{ BLOCK_OPTHELPER, 325250, "INSTALL_TARGET" },
	{ BLOCK_OPTHELPER, 325300, "LDFLAGS_OFF" },
	{ BLOCK_OPTHELPER, 325350, "LDFLAGS" },
	{ BLOCK_OPTHELPER, 325400, "LIBS_OFF" },
	{ BLOCK_OPTHELPER, 325450, "LIBS" },
	{ BLOCK_OPTHELPER, 325500, "MAKE_ARGS_OFF" },
	{ BLOCK_OPTHELPER, 325550, "MAKE_ARGS" },
	{ BLOCK_OPTHELPER, 325600, "MAKE_ENV_OFF" },
	{ BLOCK_OPTHELPER, 325650, "MAKE_ENV" },
	{ BLOCK_OPTHELPER, 325700, "PLIST_DIRS_OFF" },
	{ BLOCK_OPTHELPER, 325750, "PLIST_DIRS" },
	{ BLOCK_OPTHELPER, 325800, "PLIST_FILES_OFF" },
	{ BLOCK_OPTHELPER, 325850, "PLIST_FILES" },
	{ BLOCK_OPTHELPER, 325900, "PLIST_SUB_OFF" },
	{ BLOCK_OPTHELPER, 325950, "PLIST_SUB" },
	{ BLOCK_OPTHELPER, 326000, "PORTDOCS_OFF" },
	{ BLOCK_OPTHELPER, 326050, "PORTDOCS" },
	{ BLOCK_OPTHELPER, 326100, "PORTEXAMPLES_OFF" },
	{ BLOCK_OPTHELPER, 326150, "PORTEXAMPLES" },
	{ BLOCK_OPTHELPER, 326200, "QMAKE_OFF" },
	{ BLOCK_OPTHELPER, 326250, "QMAKE_ON" },
	{ BLOCK_OPTHELPER, 326300, "SUB_FILES_OFF" },
	{ BLOCK_OPTHELPER, 326350, "SUB_FILES" },
	{ BLOCK_OPTHELPER, 326400, "SUB_LIST_OFF" },
	{ BLOCK_OPTHELPER, 326450, "SUB_LIST" },
	{ BLOCK_OPTHELPER, 326500, "TEST_TARGET_OFF" },
	{ BLOCK_OPTHELPER, 326550, "TEST_TARGET" },
	{ BLOCK_OPTHELPER, 326600, "VARS_OFF" },
	{ BLOCK_OPTHELPER, 326650, "VARS" },
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
			if (*suffix == '_') {
				return BLOCK_LICENSE;
			}
		}
	}

	if (matches(RE_FLAVORS_HELPER, var, NULL)) {
		return BLOCK_FLAVORS_HELPER;
	}

	if (matches(RE_OPTIONS_HELPER, var, NULL)) {
		if (str_endswith(var, "_DESC")) {
			return BLOCK_OPTDESC;
		} else {
			return BLOCK_OPTHELPER;
		}
	}

	if (matches(RE_OPTIONS_GROUP, var, NULL)) {
		return BLOCK_OPTDEF;
	}

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
	enum BlockType ablock = variable_order_block(a);
	enum BlockType bblock = variable_order_block(b);
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
				ascore = variable_order_[i].score;
			}
			if (str_startswith(b, variable_order_[i].var)) {
				bscore = variable_order_[i].score;
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

		for (size_t i = 0; i < nitems(variable_order_); i++) {
			if (variable_order_[i].block != BLOCK_FLAVORS_HELPER) {
				continue;
			}
			// Only compare if common prefix (helper for the same flavor)
			char *prefix;
			if ((prefix = str_common_prefix(a, b)) != NULL) {
				if (str_endswith(prefix, "_")) {
					if (str_endswith(a, variable_order_[i].var)) {
						ascore = variable_order_[i].score;
					}
					if (str_endswith(b, variable_order_[i].var)) {
						bscore = variable_order_[i].score;
					}
				}
				free(prefix);
			}
		}

		if (ascore < bscore) {
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

		for (size_t i = 0; i < nitems(variable_order_); i++) {
			if (variable_order_[i].block != BLOCK_OPTHELPER) {
				continue;
			}
			// Only compare if common prefix (helper for the same option)
			char *prefix;
			if ((prefix = str_common_prefix(a, b)) != NULL) {
				if (str_endswith(prefix, "_")) {
					if (str_endswith(a, variable_order_[i].var)) {
						ascore = variable_order_[i].score;
					}
					if (str_endswith(b, variable_order_[i].var)) {
						bscore = variable_order_[i].score;
					}
				}
				free(prefix);
			}
		}

		if (ascore < bscore) {
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
				ascore = variable_order_[i].score;
			}
			if (str_startswith(b, variable_order_[i].var)) {
				bscore = variable_order_[i].score;
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

	int ascore = -1;
	int bscore = -1;
	for (size_t i = 0; i < nitems(variable_order_) && (ascore == -1 || bscore == -1); i++) {
		if (strcmp(a, variable_order_[i].var) == 0) {
			ascore = variable_order_[i].score;
		}
		if (strcmp(b, variable_order_[i].var) == 0) {
			bscore = variable_order_[i].score;
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
	case BLOCK_FLAVORS_HELPER:
		return "BLOCK_FLAVORS_HELPER";
	case BLOCK_GO:
		return "BLOCK_GO";
	case BLOCK_LAZARUS:
		return "BLOCK_LAZARUS";
	case BLOCK_LICENSE:
		return "BLOCK_LICENSE";
	case BLOCK_LICENSE_OLD:
		return "BLOCK_LICENSE_OLD";
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
	case BLOCK_SCONS:
		return "BLOCK_SCONS";
	case BLOCK_SHEBANGFIX:
		return "BLOCK_SHEBANGFIX";
	case BLOCK_STANDARD:
		return "BLOCK_STANDARD";
	case BLOCK_UNIQUEFILES:
		return "BLOCK_UNIQUEFILES";
	case BLOCK_UNKNOWN:
		return "BLOCK_UNKNOWN";
	case BLOCK_USERS:
		return "BLOCK_USERS";
	case BLOCK_USES:
		return "BLOCK_USES";
	}

	abort();
}

char *
flavors_helpers_pattern()
{
	size_t len = strlen("^[a-z0-9_]+_(");
	for (size_t i = 0; i < nitems(variable_order_); i++) {
		if (variable_order_[i].block != BLOCK_FLAVORS_HELPER) {
			continue;
		}
		const char *helper = variable_order_[i].var;
		len += strlen(helper) + strlen("|");
	}
	len -= 1;
	len += strlen(")$") + 1;

	char *buf = xmalloc(len);
	xstrlcat(buf, "^[a-z0-9_]+_(", len);
	for (size_t i = 0; i < nitems(variable_order_); i++) {
		if (variable_order_[i].block != BLOCK_FLAVORS_HELPER) {
			continue;
		}
		const char *helper = variable_order_[i].var;
		xstrlcat(buf, helper, len);
		xstrlcat(buf, "|", len);
	}
	buf[strlen(buf) - 1] = 0;
	xstrlcat(buf, ")$", len);

	return buf;
}

char *
options_helpers_pattern()
{
	size_t len = strlen("_(");
	for (size_t i = 0; i < nitems(variable_order_); i++) {
		if (variable_order_[i].block != BLOCK_OPTHELPER) {
			continue;
		}
		const char *helper = variable_order_[i].var;
		len += strlen(helper) + strlen("|");
	}
	len += strlen("DESC)$") + 1;

	char *buf = xmalloc(len);
	xstrlcat(buf, "_(", len);
	for (size_t i = 0; i < nitems(variable_order_); i++) {
		if (variable_order_[i].block != BLOCK_OPTHELPER) {
			continue;
		}
		const char *helper = variable_order_[i].var;
		xstrlcat(buf, helper, len);
		xstrlcat(buf, "|", len);
	}
	xstrlcat(buf, "DESC)$", len);

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
		case RE_FLAVORS_HELPER:
			buf = flavors_helpers_pattern();
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
