#!/bin/sh
set -eu
: "${BASEDIR:=/usr/src}"
: "${PORTSDIR:=/usr/ports}"
TMP="generated_rules.tmp"
OSRELS="11 12 13 14"

export LC_ALL=C

_make() {
	if [ "$1" = "Mk" ]; then
		flags="-f bsd.port.mk"
	else
		flags=""
	fi
	make -C "${PORTSDIR}/$1" ${flags} PORTSDIR="${PORTSDIR}" "$@" | tr ' ' '\n' | awk 'NF' | sort -u
}

exec 1>"parser/constants.h"
echo "/* Generated by generate_rules.sh; do not edit */"

echo "static struct { const char *uses; const char *flavor; } static_flavors_[] = {"
_make "Mk" USES=emacs -V FLAVORS | sed -e 's/^/	{ "emacs", "/' -e 's/$/" },/'
_make "Mk" USES=lazarus:flavors -V FLAVORS | sed -e 's/^/	{ "lazarus", "/' -e 's/$/" },/'
_make "Mk" USES=lua:module -V FLAVORS | sed -e 's/^/	{ "lua", "/' -e 's/$/" },/'
_make "Mk" USES=php:flavors -V FLAVORS | sed -e 's/^/	{ "php", "/' -e 's/$/" },/'
_make "devel/py-setuptools" BUILD_ALL_PYTHON_FLAVORS=yes -V FLAVORS | sed -e 's/^/	{ "python", "/' -e 's/$/" },/'
echo "};"

printf 'static const char *known_architectures_[] = {\n'
(
	make -C "${BASEDIR}" targets
	# Some arches are retired in FreeBSD 13 but still valid in other releases
	echo "/arm"
	echo "/mips64el"
	echo "/mips64elhf"
	echo "/mips64hf"
	echo "/mipsel"
	echo "/mipselhf"
	echo "/mipshf"
	echo "/mipsn32"
	echo "/powerpc64le"
	echo "/powerpcspe"
	echo "/sparc64"
) | awk -F/ 'NR > 1 { printf "\t\"%s\",\n", $2 }' | sort -u
echo '};'

exec 1>"generated_rules.h"
echo "/* Generated by generate_rules.sh; do not edit */"

echo "#define VAR_BROKEN_RUBY(block, flags, uses) \\"
(cd "${PORTSDIR}/lang"; ls -d ruby*) | tr '[a-z]' '[A-Z]' | sort | awk '{ lines[++i] = $0 }
END {
	for (i = 1; lines[i] != ""; i++) {
		printf "%s\t{ block, \"BROKEN_%s\", flags, uses }", start, lines[i]
		start = ", \\\n";
	}
	print ""
}'

printf '#define VAR_FOR_EACH_ARCH(block, var, flags, uses) \\\n'
(
	make -C "${BASEDIR}" targets
	# Some arches are retired in FreeBSD 13 but still valid in other releases
	echo "/arm"
	echo "/mips64el"
	echo "/mips64elhf"
	echo "/mips64hf"
	echo "/mipsel"
	echo "/mipselhf"
	echo "/mipshf"
	echo "/mipsn32"
	echo "/powerpc64le"
	echo "/powerpcspe"
	echo "/sparc64"
) | awk -F/ 'NR > 1 { print $2 }' | sort -u | awk '{ lines[NR] = $1 }
END {
	for (i = 1; lines[i] != ""; i++) {
		printf "%s\t{ block, var \"%s\", flags, uses }", start, lines[i]
		start = ", \\\n";
	}
	print ""
}'

printf '#define VAR_FOR_EACH_FREEBSD_VERSION_AND_ARCH(block, var, flags, uses) \\\n'
printf '	{ block, var "FreeBSD", flags, uses }, \\\n'
for ver in ${OSRELS}; do
	printf '	{ block, var "FreeBSD_%s", flags, uses }, \\\n' "${ver}"
	printf '	VAR_FOR_EACH_ARCH(block, var "FreeBSD_%s_", flags, uses), \\\n' "${ver}"
done
printf '	VAR_FOR_EACH_ARCH(block, var "FreeBSD_", flags, uses)\n'
printf '#define VAR_FOR_EACH_FREEBSD_VERSION(block, var, flags, uses) \\\n'
printf '	{ block, var "FreeBSD", flags, uses }'
for ver in ${OSRELS}; do
	printf ', \\\n	{ block, var "FreeBSD_%s", flags, uses }' "${ver}"
done
echo

echo 'static const char *use_gnome_rel[] = {'
_make "Mk" USES=gnome -V _USE_GNOME_ALL >"${TMP}"
sed -e 's/^/	"/' -e 's/$/",/' "${TMP}"
# USES=gnome silently allows for bogus component:build args etc,
# but we do not.
while read -r comp; do
	build=$(_make "Mk" USES=gnome -V "${comp}_BUILD_DEPENDS")
	if [ -n "${build}" ]; then
		echo "${comp}" | sed -e 's/^/	"/' -e 's/$/:build",/'
	fi
done <"${TMP}"
while read -r comp; do
	build=$(_make "Mk" USES=gnome -V "${comp}_RUN_DEPENDS")
	if [ -n "${build}" ]; then
		echo "${comp}" | sed -e 's/^/	"/' -e 's/$/:run",/'
	fi
done <"${TMP}"
echo '};'

echo 'static const char *use_kde_rel[] = {'
_make "Mk" CATEGORIES=devel USES=kde:5 -V _USE_KDE5_ALL >"${TMP}"
sed -e 's/^/	"/' -e 's/$/",/' "${TMP}"
sed -e 's/^/	"/' -e 's/$/_build",/' "${TMP}"
sed -e 's/^/	"/' -e 's/$/_run",/' "${TMP}"
echo '};'

echo 'static const char *use_qt_rel[] = {'
_make "Mk" USES=qt:5 -V _USE_QT_ALL >"${TMP}"
sed -e 's/^/	"/' -e 's/$/",/' "${TMP}"
sed -e 's/^/	"/' -e 's/$/_build",/' "${TMP}"
sed -e 's/^/	"/' -e 's/$/_run",/' "${TMP}"
echo '};'

echo 'static const char *use_pyqt_rel[] = {'
_make "Mk" USES=pyqt:5 -V _USE_PYQT_ALL >"${TMP}"
sed -e 's/^/	"/' -e 's/$/",/' "${TMP}"
sed -e 's/^/	"/' -e 's/$/_build",/' "${TMP}"
sed -e 's/^/	"/' -e 's/$/_run",/' "${TMP}"
sed -e 's/^/	"/' -e 's/$/_test",/' "${TMP}"
echo '};'

printf '#define VAR_FOR_EACH_SSL(block, var, flags, uses) \\\n'
valid_ssl=$(awk '/^# Possible values: / { values = $0; gsub(/(^# Possible values: |,)/, "", values); }
/SSL_DEFAULT/ { print values; exit }' "${PORTSDIR}/Mk/bsd.default-versions.mk")
start=""
for ssl in ${valid_ssl}; do
	[ -n "${start}" ] && echo "${start}"
	printf '	{ block, var "%s", flags, uses }' "${ssl}"
	start=", \\"
done
echo

echo "static const char *static_shebang_langs_[] = {"
_make "Mk" USES="lua shebangfix" -V SHEBANG_LANG | sed -e 's/^/	"/' -e 's/$/",/'
echo "};"

rm -f "${TMP}"
