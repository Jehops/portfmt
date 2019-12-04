LIBNAME=	libportfmt

include Makefile.configure

MKDIR?=		mkdir -p
LN?=		ln

CFLAGS+=	-std=c99 -I.
LDADD+=		-ldl -lm

OBJS=		array.o compats.o conditional.o diff.o mainutils.o parser.o \
		regexp.o rules.o target.o token.o util.o variable.o parser/plugin.o
PLUGINS=	parser/plugin.edit.bump-revision.${LIBSUFFIX} \
		parser/plugin.edit.merge.${LIBSUFFIX} \
		parser/plugin.edit.output-unknown-targets.${LIBSUFFIX} \
		parser/plugin.edit.output-unknown-variables.${LIBSUFFIX} \
		parser/plugin.edit.output-variable-value.${LIBSUFFIX} \
		parser/plugin.edit.set-version.${LIBSUFFIX} \
		parser/plugin.lint.order.${LIBSUFFIX} \
		parser/plugin.refactor.collapse-adjacent-variables.${LIBSUFFIX} \
		parser/plugin.refactor.depup-tokens.${LIBSUFFIX} \
		parser/plugin.refactor.sanitize-append-modifier.${LIBSUFFIX} \
		parser/plugin.refactor.sanitize-eol-comments.${LIBSUFFIX}

.SUFFIXES: .${LIBSUFFIX}

all: portclippy portedit portfmt portscan ${PLUGINS}

.c.o:
	${CC} ${CPPFLAGS} -fPIC ${CFLAGS} -o $@ -c $<

.o.${LIBSUFFIX}:
	${CC} ${LDFLAGS} ${PLUGIN_LDFLAGS} -o $@ $< ${LIBNAME}.${LIBSUFFIX}

${LIBNAME}.${LIBSUFFIX}: ${OBJS}
	${CC} ${LDFLAGS} ${SHARED_LDFLAGS} -o ${LIBNAME}.${LIBSUFFIX} ${OBJS} \
		${LDADD}

portclippy: ${LIBNAME}.${LIBSUFFIX} portclippy.o
	${CC} ${LDFLAGS} -o portclippy portclippy.o ${LIBNAME}.${LIBSUFFIX}

portedit: ${LIBNAME}.${LIBSUFFIX} portedit.o
	${CC} ${LDFLAGS} -o portedit portedit.o ${LIBNAME}.${LIBSUFFIX}

portfmt: ${LIBNAME}.${LIBSUFFIX} portfmt.o
	${CC} ${LDFLAGS} -o portfmt portfmt.o ${LIBNAME}.${LIBSUFFIX}

portscan: ${LIBNAME}.${LIBSUFFIX} portscan.o
	${CC} ${LDFLAGS} -o portscan portscan.o -lpthread ${LIBNAME}.${LIBSUFFIX}

portclippy.o: portclippy.c config.h mainutils.h parser.h parser/plugin.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

portedit.o: portedit.c config.h array.h mainutils.h parser.h parser/plugin.h util.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

portfmt.o: portfmt.c config.h mainutils.h parser.h parser/plugin.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

portscan.o: portscan.c config.h array.h parser.h parser/plugin.h util.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

conditional.o: config.h conditional.c conditional.h regexp.h
diff.o: config.h diff.h
mainutils.o: config.h array.h mainutils.c mainutils.h parser.h util.h
parser.o: config.h array.h conditional.h regexp.h parser.c parser.h rules.h target.h token.h util.h variable.h
parser/plugin.edit-output-variable-value.o: config.h array.h parser.h parser/plugin.h token.h variable.h parser/plugin.edit.output-variable-value.c
parser/plugin.edit-set-version.o: config.h array.h parser.h parser/plugin.h rules.h token.h util.h variable.h parser/plugin.edit.set-version.c
parser/plugin.edit.bump-revision.o: config.h array.h parser.h parser/plugin.h rules.h token.h util.h variable.h parser/plugin.edit.bump-revision.c
parser/plugin.edit.merge.o: config.h array.h parser.h parser/plugin.h rules.h token.h util.h variable.h parser/plugin.edit.merge.c
parser/plugin.edit.output-unknown-targets.o: config.h array.h parser.h parser/plugin.h regexp.h rules.h target.h token.h util.h parser/plugin.edit.output-unknown-targets.c
parser/plugin.edit.output-unknown-variables.o: config.h array.h parser.h parser/plugin.h regexp.h rules.h token.h util.h variable.h parser/plugin.edit.output-unknown-variables.c
parser/plugin.lint.order.o: config.h array.h conditional.h diff.h parser.h parser/plugin.h rules.h target.h token.h util.h variable.h parser/plugin.lint.order.c
parser/plugin.o: config.h parser.h parser/plugin.h util.h
parser/plugin.refactor.collapse-adjacent-variables.o: config.h array.h parser.h parser/plugin.h token.h util.h variable.h parser/plugin.refactor.collapse-adjacent-variables.c
parser/plugin.refactor.dedup-tokens.o: config.h array.h parser.h parser/plugin.h token.h util.h variable.h parser/plugin.refactor.dedup-tokens.c
parser/plugin.refactor.sanitize-append-modifier.o: config.h array.h parser.h parser/plugin.h rules.h token.h variable.h parser/plugin.refactor.sanitize-append-modifier.c
parser/plugin.refactor.sanitize-eol-comments.o: config.h array.h parser.h parser/plugin.h rules.h token.h util.h variable.h parser/plugin.refactor.sanitize-eol-comments.c
regexp.o: config.h
rules.o: config.h array.h conditional.h parser.h rules.c regexp.h rules.h token.h util.h variable.h
target.o: config.h target.h util.h
token.o: config.h conditional.h target.h token.h util.h variable.h
util.o: config.h util.c util.h
variable.o: config.h regexp.h rules.h util.h variable.c variable.h

install:
	${MKDIR} ${DESTDIR}${BINDIR} ${DESTDIR}${LIBDIR}/portfmt ${DESTDIR}${MANDIR}/man1
	${INSTALL_MAN} man/*.1 ${DESTDIR}${MANDIR}/man1
	${INSTALL_PROGRAM} portclippy portedit portfmt portscan ${DESTDIR}${BINDIR}
	${INSTALL_LIB} ${LIBNAME}.${LIBSUFFIX} ${DESTDIR}${LIBDIR}
	${INSTALL_LIB} ${PLUGINS} ${DESTDIR}${LIBDIR}/portfmt

install-symlinks:
	@${MAKE} INSTALL_LIB="install -l as" INSTALL_MAN="install -l as" \
		INSTALL_PROGRAM="install -l as" INSTALL_SCRIPT="install -l as" \
		install

clean:
	@rm -f ${OBJS} ${PLUGINS} parser/*.o *.o libportfmt.${LIBSUFFIX} portclippy portedit portfmt \
		portscan config.*.old

debug:
	@${MAKE} CFLAGS="-Wall -std=c99 -O1 -g -fno-omit-frame-pointer" \
		LDFLAGS="-g" portfmt

test: all
	@/bin/sh run-tests.sh

.PHONY: clean debug install install-symlinks test
