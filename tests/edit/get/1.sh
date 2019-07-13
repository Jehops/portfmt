# No value but exit status should be 0 since DISTVERSION is available
if echo "DISTVERSION=" | ${PORTEDIT} get '^DISTVERSION$'; then
	exit 0
else
	echo "status: $? expected: 0"
	exit 1
fi
