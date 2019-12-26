logdir="$(mktemp -dt portscan-test.XXXXXXX)"
${PORTSCAN} -p 0002 -l "${logdir}/log"
[ "$(readlink "${logdir}/log/portscan-latest.log")" != "$(readlink "${logdir}/log/portscan-previous.log")" ]
cat <<EOF | diff -u - "${logdir}/log/portscan-latest.log"
V       archivers/arj                            IGNORE_PATCHES
EOF
