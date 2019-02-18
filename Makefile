include Makefile.configure

LLVM_PREFIX?=	/usr/local/llvm80
MKDIR?=		mkdir -p
SED?=		sed

CFLAGS+=	-std=c99
LDADD+=		-lm

OBJS=	array.o compats.o portfmt.o rules.o util.o variable.o

portfmt: ${OBJS}
	${CC} ${LDFLAGS} -o portfmt ${OBJS} ${LDADD}

array.o: array.c array.h
portfmt.o: array.h portfmt.c rules.h util.h variable.h
rules.o: rules.c rules.h util.h variable.h
util.o: util.c util.h
variable.o: rules.h util.h variable.c variable.h

install:
	${MKDIR} ${DESTDIR}${PREFIX}/bin \
		${DESTDIR}${PREFIX}/libexec/portfmt \
		${DESTDIR}${MANDIR}/man1
	${INSTALL_MAN} portfmt.1 ${DESTDIR}${MANDIR}/man1
	${INSTALL_SCRIPT} portfmt ${DESTDIR}${PREFIX}/bin
	${INSTALL_SCRIPT} portfmt.awk ${DESTDIR}${PREFIX}/libexec/portfmt
	if [ ! -L "${DESTDIR}${PREFIX}/bin/portfmt" ]; then \
		${SED} -i '' 's,/usr/local,${PREFIX},' ${DESTDIR}${PREFIX}/bin/portfmt; \
	fi

install-symlinks:
	@${MAKE} INSTALL_MAN="install -l as" \
		INSTALL_SCRIPT="install -l as" \
		install

clean:
	@rm -f ${OBJS} portfmt config.*.old

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
