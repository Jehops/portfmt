logdir="$(mktemp -dt portscan-test.XXXXXXX)"
${PORTSCAN} -p 0002 -l "${logdir}/log"
set +e
${PORTSCAN} -p 0002 -l "${logdir}/log"
# no changes
[ $? -eq 2 ] || exit 1
