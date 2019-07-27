cat <<EOD | ${PORTEDIT} merge 14.in | diff -L 14.expected -L 14.actual -u 14.expected -
DISTVERSIONSUFFIX=	-gababab
DISTVERSION=	1
DISTVERSIONPREFIX=	v
EOD
