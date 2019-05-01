include Makefile.configure

MKDIR?=		mkdir -p
LN?=		ln
SED?=		sed

CFLAGS+=	-std=c99
LDADD+=		-lm

OBJS=		array.o compats.o conditional.o parser.o portfmt.o rules.o \
		target.o util.o variable.o

all: portedit portfmt

portfmt: ${OBJS}
	${CC} ${LDFLAGS} -o portfmt ${OBJS} ${LDADD}

portedit: portfmt
	${LN} -sf portfmt portedit

array.o: config.h array.c array.h
conditional.o: config.h conditional.c conditional.h
portfmt.o: config.h parser.h portfmt.c
rules.o: config.h rules.c rules.h util.h variable.h
parser.o: config.h array.h conditional.h parser.c parser.h rules.h target.h util.h variable.h
target.o: config.h target.h util.h
util.o: config.h util.c util.h
variable.o: config.h rules.h util.h variable.c variable.h

install:
	${MKDIR} ${DESTDIR}${PREFIX}/bin \
		${DESTDIR}${MANDIR}/man1
	${INSTALL_MAN} portfmt.1 ${DESTDIR}${MANDIR}/man1
	${INSTALL_PROGRAM} portfmt ${DESTDIR}${PREFIX}/bin
	cd ${DESTDIR}${PREFIX}/bin && ${LN} -sf portfmt portedit
	@if [ ! -L "${DESTDIR}${PREFIX}/bin/portfmt" ]; then \
		${SED} -i '' 's,/usr/local,${PREFIX},' ${DESTDIR}${PREFIX}/bin/portfmt; \
	fi

install-symlinks:
	@${MAKE} INSTALL_MAN="install -l as" \
		INSTALL_SCRIPT="install -l as" \
		install

clean:
	@rm -f ${OBJS} portedit portfmt config.*.old

debug:
	@${MAKE} CFLAGS="-Wall -std=c99 -O1 -g -fno-omit-frame-pointer" \
		LDFLAGS="-g" \
		portfmt

test: portfmt
	@/bin/sh run-tests.sh

.PHONY: clean debug install install-symlinks test
