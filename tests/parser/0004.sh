echo 'do-install: do-install-${D:S/./_/g}' | ${PORTFMT} -d | awk '
$1 == "target-end" {
	if ($3 != "do-install") {
		printf "expected: do-install:\nactual: %s\n", $3
		print "not ok"
		exit 1
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
