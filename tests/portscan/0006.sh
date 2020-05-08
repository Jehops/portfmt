logdir="$(mktemp -dt portscan-test.XXXXXXX)"
# Make sure it does not crash with an empty directory
mkdir -p "${logdir}/ports"
${PORTSCAN} -p "${logdir}/ports" -l "${logdir}/log" 2>&1 >/dev/null || exit 1
echo "error ok"
# or a ports directory with nonexistent ports listed in categories
${PORTSCAN} -p 0005 -l "${logdir}/log" 2>&1 >/dev/null || exit 1
echo "error ok"
