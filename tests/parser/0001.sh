cat <<EOF | ${PORTFMT} -d | grep "^target-start" && (echo "conditional treated as target, not ok" ; exit 1)
.if defined(PACKAGE_BUILDING) && empty(CFLAGS:M-march*)
BLA+=	asdf
.endif
EOF
exit 0
