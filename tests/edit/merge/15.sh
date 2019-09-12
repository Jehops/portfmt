cat <<EOD | ${PORTEDIT} merge 15.in | diff -L 15.expected -L 15.actual -u 15.expected -
GH_TAGNAME=	ababab
EOD
