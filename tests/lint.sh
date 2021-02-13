#!/bin/sh
set -u
ROOT="${PWD}"
CPPCHECK="cppcheck"
MANDOC="mandoc"

lint_failed=0
lint_run=0

cd "${ROOT}"
if type ${CPPCHECK} >/dev/null 2>&1; then
lint_run=$((lint_run + 1))
srcs="$(find . -name '*.c' -and -not -name 'tests.c')"
if ! ${CPPCHECK} --error-exitcode=1 --library=posix --quiet --force --inconclusive ${srcs}; then
	lint_failed=$((lint_failed + 1))
fi
fi

cd "${ROOT}/man"
if type ${MANDOC} >/dev/null 2>&1; then
lint_run=$((lint_run + 1))
if ! ${MANDOC} -Tlint -Wstyle ./*.1; then
	lint_failed=$((lint_failed + 1))
fi
fi

printf "fail: %s ok: %s/%s\n" "${lint_failed}" "$((lint_run - lint_failed))" "${lint_run}"
if [ "${lint_failed}" -gt 0 ]; then
	exit 1
fi
