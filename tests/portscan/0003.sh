logdir="$(mktemp -dt portscan-test)"
${PORTSCAN} -p 0002 -l "${logdir}/log"
set +e
${PORTSCAN} -p 0002 -l "${logdir}/log"
# no changes
[ $? -eq 2 ] || exit 1
