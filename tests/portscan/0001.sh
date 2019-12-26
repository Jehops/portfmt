logdir="$(mktemp -dt portscan-test)"
${PORTSCAN} -p 0001 -l "${logdir}/log"
[ -d "${logdir}/log" ]
[ -L "${logdir}/log/portscan-latest.log" ]
[ "$(readlink "${logdir}/log/portscan-latest.log")" = "/dev/null" ]
[ -L "${logdir}/log/portscan-previous.log" ]
[ "$(readlink "${logdir}/log/portscan-previous.log")" = "/dev/null" ]
