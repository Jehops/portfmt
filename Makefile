PREFIX?=	/usr/local
MANDIR?=	${PREFIX}/man

.if defined(PORTFMT_INSTALL_SYMLINKS)
BSD_INSTALL_MAN=	install -l as
BSD_INSTALL_SCRIPT=	install -l as
.else
BSD_INSTALL_MAN?=	install -m 444
BSD_INSTALL_SCRIPT?=	install -m 555
.endif

MKDIR?=	mkdir -p
SED?=	sed

install:
	${MKDIR} ${DESTDIR}${PREFIX}/bin \
		${DESTDIR}${PREFIX}/libexec/portfmt \
		${DESTDIR}${MANDIR}/man1
	${BSD_INSTALL_MAN} portfmt.1 ${DESTDIR}${MANDIR}/man1
	${BSD_INSTALL_SCRIPT} portfmt ${DESTDIR}${PREFIX}/bin
.if !defined(PORTFMT_INSTALL_SYMLINKS)
	${SED} -i '' 's,/usr/local,${PREFIX},' ${DESTDIR}${PREFIX}/bin/portfmt
.endif
	${BSD_INSTALL_SCRIPT} portfmt.awk ${DESTDIR}${PREFIX}/libexec/portfmt

