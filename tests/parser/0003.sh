echo 'do-test: # tests are known to fail: https' | ${PORTFMT} -d | awk '
$1 == "target-end" {
	if ($3 != "do-test") {
		printf "expected: do-test:\nactual: %s\n", $3
		print "not ok"
		exit

	} else {
		ok = 1
	}
}

END {
	if (!ok) {
		exit 1
	}
}
'
