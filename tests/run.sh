#!/bin/sh
set -u
ROOT="${PWD}"
LOG="${ROOT}/tests/log"
PORTCLIPPY="${ROOT}/portclippy"
PORTEDIT="${ROOT}/portedit"
PORTFMT="${ROOT}/portfmt"
PORTSCAN="${ROOT}/portscan"
: ${SH:=/bin/sh}

export PORTCLIPPY
export PORTEDIT
export PORTFMT
export PORTFMT_PLUGIN_PATH
export PORTSCAN
export ROOT

tests_failed=0
tests_run=0

exec 3>&1
exec >"${LOG}"
exec 2>&1

cd "${ROOT}/tests/parser" || exit 1
for test in *.sh; do
	if sh -eu "${test}"; then
		echo -n . >&3
	else
		echo -n X >&3
		echo "parser/${test%*.sh}: FAIL" >&2
		tests_run=$((tests_run + 1))
		tests_failed=$((tests_failed + 1))
		continue
	fi
	tests_run=$((tests_run + 1))
done

cd "${ROOT}/tests/format" || exit 1
for test in *.in; do
	t=${test%*.in}
	tests_run=$((tests_run + 1))
	if ${PORTFMT} -t <"${t}.in" >"${t}.actual"; then
		if diff -L "${t}.expected" -L "${t}.actual" -u "${t}.expected" "${t}.actual"; then
			echo -n . >&3
		else
			echo -n X >&3
			echo "format/${t}#1: FAIL" >&2
			tests_failed=$((tests_failed + 1))
			continue
		fi
	else
		echo -n X >&3
		echo "format/${t}#1: FAIL" >&2
		tests_failed=$((tests_failed + 1))
		continue
	fi

	tests_run=$((tests_run + 1))
	if ${PORTFMT} -t <"${t}.expected" >"${t}.actual2"; then
		if diff -L "${t}.expected" -L "${t}.actual" -u "${t}.expected" "${t}.actual2"; then
			echo -n . >&3
		else
			echo -n X >&3
			echo "format/${t}#2: FAIL" >&2
			tests_failed=$((tests_failed + 1))
		fi
	else
		echo -n X >&3
		echo "format/${t}#2: FAIL" >&2
		tests_failed=$((tests_failed + 1))
	fi
done
rm -f ./*.actual ./*.actual2

cd "${ROOT}/tests/edit" || exit 1
for test in bump-epoch/*.sh bump-revision/*.sh apply/*.sh get/*.sh merge/*.sh set-version/*.sh; do
	t=${test%*.sh}
	tests_run=$((tests_run + 1))
	cd "${ROOT}/tests/edit/$(dirname "${test}")" || exit 1
	if ${SH} -o pipefail -eu "$(basename "${test}")"; then
		echo -n . >&3
	else
		echo -n X >&3
		echo "edit/${t}: FAIL" >&2
		tests_failed=$((tests_failed + 1))
	fi
done

cd "${ROOT}/tests/clippy" || exit 1
for test in *.in; do
	t=${test%*.in}
	tests_run=$((tests_run + 1))
	${PORTCLIPPY} "${t}.in" >"${t}.actual"
	if diff -L "${t}.expected" -L "${t}.actual" -u "${t}.expected" "${t}.actual"; then
		echo -n . >&3
	else
		echo -n X >&3
		echo "clippy/${t}: FAIL" >&2
		tests_failed=$((tests_failed + 1))
		continue
	fi
done
rm -f ./*.actual

cd "${ROOT}/tests" || exit 1
for t in reject/*.in; do
	tests_run=$((tests_run + 1))
	if ${PORTFMT} "${t}"; then
		echo -n X >&3
		echo "${t%*.in}: FAIL" >&2
		tests_failed=$((tests_failed + 1))
	else
		echo -n . >&3
	fi
done

cd "${ROOT}/tests/portscan" || exit 1
for t in *.sh; do
	tests_run=$((tests_run + 1))
	if ${SH} -o pipefail -eu "${t}"; then
		echo -n . >&3
	else
		echo -n X >&3
		echo "portscan/${t%*.sh}: FAIL" >&2
		tests_failed=$((tests_failed + 1))
	fi
done

echo >&3
if [ "${tests_failed}" -gt 0 ]; then
	cat "${LOG}" >&3
	rm -f "${LOG}"
	exit 1
else
	rm -f "${LOG}"
	exit 0
fi
