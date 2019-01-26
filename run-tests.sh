#!/bin/sh
set -u
PORTFMT="awk -f ${PWD}/portfmt.awk"
status=0
for test in tests/*.in; do
	t=${test%*.in}
	${PORTFMT} < ${test} | diff -L ${t}.expected -L ${t}.actual \
		-u ${t}.expected -
	if [ $? -ne 0 ]; then
		status=1
	fi
done
exit ${status}
