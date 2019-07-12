# No value, no variable.  Exit status should be non-zero.
if ${PORTEDIT} get DISTVERSION </dev/null; then
	echo "status: $? expected: 1"
	exit 1
else
	echo "error ok"
fi
