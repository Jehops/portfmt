#!/bin/sh
set -u
status=0
ROOT="${PWD}"
PORTEDIT="${ROOT}/portedit"
PORTFMT="${ROOT}/portfmt"

export PORTEDIT
export PORTFMT
export ROOT

export LD_LIBRARY_PATH="${ROOT}"

cd "${ROOT}/tests/format"
for test in *.in; do
	t=${test%*.in}
	${PORTFMT} -t < ${t}.in > ${t}.actual
	out=$(diff -L ${t}.expected -L ${t}.actual -u ${t}.expected ${t}.actual)
	if [ $? -ne 0 ]; then
		echo "format/${t}#1"
		echo "${out}"
		echo
		status=1
		continue
	fi
	${PORTFMT} -t < ${t}.expected > ${t}.actual2
	out=$(diff -L ${t}.expected -L ${t}.actual -u ${t}.expected ${t}.actual2)
	if [ $? -ne 0 ]; then
		echo "format/${t}#2"
		echo "${out}"
		echo
		status=1
	fi
done
rm -f *.actual *.actual2

cd "${ROOT}/tests/edit"
for test in *.sh; do
	t=${test%*.sh}
	out=$(sh ${test})
	if [ $? -ne 0 ]; then
		echo "edit/${t}"
		echo "${out}"
		echo
		status=1
		continue
	fi
done

cd "${ROOT}/tests"
find reject -name '*.in' | while read t; do
	out=$(${PORTFMT} ${t} 2>&1)
	if [ $? -ne 1 ]; then
		cat <<EOF
${t} not rejected:
${out}

EOF
		exit 1
	fi
done


exit ${status}
