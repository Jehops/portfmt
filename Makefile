include Makefile.configure

MKDIR?=		mkdir -p
SED?=		sed

CFLAGS+=	-std=c99
LDADD+=		-lm

OBJS=	array.o compats.o portfmt.o rules.o target.o util.o variable.o

all: portfmt

portfmt: ${OBJS}
	${CC} ${LDFLAGS} -o portfmt ${OBJS} ${LDADD}

array.o: array.c array.h
portfmt.o: array.h portfmt.c rules.h target.h util.h variable.h
rules.o: rules.c rules.h util.h variable.h
target.o: target.h util.h
util.o: util.c util.h
variable.o: rules.h util.h variable.c variable.h

install:
	${MKDIR} ${DESTDIR}${PREFIX}/bin \
		${DESTDIR}${MANDIR}/man1
	${INSTALL_MAN} portfmt.1 ${DESTDIR}${MANDIR}/man1
	${INSTALL_PROGRAM} portfmt ${DESTDIR}${PREFIX}/bin
	@if [ ! -L "${DESTDIR}${PREFIX}/bin/portfmt" ]; then \
		${SED} -i '' 's,/usr/local,${PREFIX},' ${DESTDIR}${PREFIX}/bin/portfmt; \
	fi

install-symlinks:
	@${MAKE} INSTALL_MAN="install -l as" \
		INSTALL_SCRIPT="install -l as" \
		install

clean:
	@rm -f ${OBJS} portfmt config.*.old

debug:
	@${MAKE} CFLAGS="-Wall -std=c99 -O1 -g -fno-omit-frame-pointer" \
		LDFLAGS="-g" \
		portfmt

test: portfmt
	@/bin/sh run-tests.sh

.PHONY: clean debug install install-symlinks test
