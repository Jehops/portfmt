#!/bin/sh
set -u
: ${AWK:=awk}
PORTFMT="${PWD}/portfmt"
status=0
cd tests
: ${TESTS:=*.in}
for test in ${TESTS}; do
	t=${test%*.in}
	${PORTFMT} < ${t}.in > ${t}.actual
	diff -L ${t}.expected -L ${t}.actual -u ${t}.expected ${t}.actual
	if [ $? -ne 0 ]; then
		status=1
		continue
	fi
	${PORTFMT} < ${t}.expected > ${t}.actual2
	diff -L ${t}.expected -L ${t}.actual -u ${t}.expected ${t}.actual2
	if [ $? -ne 0 ]; then
		status=1
	fi
done
rm -f *.actual *.actual2
exit ${status}
