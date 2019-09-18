echo 'do-install-${D:S/./_/g}:' | ${PORTFMT} -d | awk '
$1 == "target-start" {
	if ($3 != "do-install-${D:S/./_/g}") {
		printf "expected: %s\nactual: %s:\n", "do-install-${D:S/./_/g}", $3
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
