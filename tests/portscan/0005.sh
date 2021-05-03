logdir="$(mktemp -dt portscan-test.XXXXXXX)"
${PORTSCAN} --categories -p 0005 -l "${logdir}/log"
[ "$(readlink "${logdir}/log/portscan-latest.log")" != "$(readlink "${logdir}/log/portscan-previous.log")" ]
cat <<EOF | diff -u - "${logdir}/log/portscan-latest.log"
C       archivers                                unsorted category or other formatting issues
Ce      archivers/foo                            entry without existing directory
EOF
