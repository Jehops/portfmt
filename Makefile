PREFIX?=	/usr/local
MANDIR?=	${PREFIX}/man

BSD_INSTALL_MAN?=	install -m 444
BSD_INSTALL_SCRIPT?=	install -m 555
LLVM_PREFIX?=		/usr/local/llvm80
MKDIR?=			mkdir -p
SED?=			sed

CFLAGS+=	-Wall -std=c99
LIBS+=		-lm

OBJS=		portfmt.o rules.o subr_sbuf.o util.o

portfmt: ${OBJS}
	${CC} ${LDFLAGS} -o portfmt ${OBJS} ${LIBS}

portfmt.c: rules.h sbuf.h util.h
rules.c: rules.h
rules.h: sbuf.h
subr_sbuf.c: sbuf.h
util.c: util.h
util.h: sbuf.h

install:
	${MKDIR} ${DESTDIR}${PREFIX}/bin \
		${DESTDIR}${PREFIX}/libexec/portfmt \
		${DESTDIR}${MANDIR}/man1
	${BSD_INSTALL_MAN} portfmt.1 ${DESTDIR}${MANDIR}/man1
	${BSD_INSTALL_SCRIPT} portfmt ${DESTDIR}${PREFIX}/bin
	${BSD_INSTALL_SCRIPT} portfmt.awk ${DESTDIR}${PREFIX}/libexec/portfmt
	if [ ! -L "${DESTDIR}${PREFIX}/bin/portfmt" ]; then \
		${SED} -i '' 's,/usr/local,${PREFIX},' ${DESTDIR}${PREFIX}/bin/portfmt; \
	fi

install-symlinks:
	@${MAKE} BSD_INSTALL_MAN="install -l as" \
		BSD_INSTALL_SCRIPT="install -l as" \
		install

clean:
	@rm -f ${OBJS} portfmt

debug:
	@${MAKE} CC=${LLVM_PREFIX}/bin/clang \
		CFLAGS="-Wall -std=c99 -O1 -g -fno-omit-frame-pointer -fsanitize=address" \
		LDFLAGS="-g -fsanitize=address" \
		portfmt

test: portfmt
	@env ASAN_SYMBOLIZER_PATH=${LLVM_PREFIX}/bin/llvm-symbolizer \
		ASAN_OPTIONS=check_initialization_order=1 \
		sh run-tests.sh

.PHONY: clean debug install install-symlinks test
