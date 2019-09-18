echo 'post-patch-CLANG$v-on:' | ${PORTFMT} -d | awk '
$1 == "target-end" {
	if ($3 != "post-patch-CLANG$v-on") {
		printf "expected: post-patch-CLANG$v-on:\nactual: %s\n", $3
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
