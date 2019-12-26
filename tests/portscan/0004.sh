logdir="$(mktemp -dt portscan-test.XXXXXXX)"
${PORTSCAN} -p 0001 -l "${logdir}"
[ -d "${logdir}" ]
[ -L "${logdir}/portscan-latest.log" ]
[ "$(readlink "${logdir}/portscan-latest.log")" = "/dev/null" ]
[ -L "${logdir}/portscan-previous.log" ]
[ "$(readlink "${logdir}/portscan-previous.log")" = "/dev/null" ]
