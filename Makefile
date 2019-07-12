LIBNAME=	libportfmt

include Makefile.configure

MKDIR?=		mkdir -p
LN?=		ln
SED?=		sed

CFLAGS+=	-std=c99
LDADD+=		-lm

OBJS=		array.o compats.o conditional.o diff.o edit_bump_revision.o \
		edit_merge.o edit_output_unknown_variables.o \
		edit_output_variable_value.o edit_set_version.o lint_order.o \
		mainutils.o parser.o refactor_collapse_adjacent.o \
		refactor_sanitize_append_modifier.o \
		refactor_sanitize_eol_comments.o regexp.o rules.o target.o \
		token.o util.o variable.o

all: portclippy portedit portfmt

.c.o:
	${CC} ${CPPFLAGS} -fPIC ${CFLAGS} -o $@ -c $<

${LIBNAME}.${LIBSUFFIX}: ${OBJS}
	${CC} ${LDFLAGS} ${SHARED_LDFLAGS} -o ${LIBNAME}.${LIBSUFFIX} ${OBJS} \
		${LDADD}

portclippy: ${LIBNAME}.${LIBSUFFIX} portclippy.o
	${CC} ${LDFLAGS} -o portclippy portclippy.o ${LIBNAME}.${LIBSUFFIX}

portedit: ${LIBNAME}.${LIBSUFFIX} portedit.o
	${CC} ${LDFLAGS} -o portedit portedit.o ${LIBNAME}.${LIBSUFFIX}

portfmt: ${LIBNAME}.${LIBSUFFIX} portfmt.o
	${CC} ${LDFLAGS} -o portfmt portfmt.o ${LIBNAME}.${LIBSUFFIX}

portclippy.o: portclippy.c config.h mainutils.h parser.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

portedit.o: portedit.c config.h mainutils.h parser.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

portfmt.o: portfmt.c config.h mainutils.h parser.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

array.o: config.h array.c array.h diff.h
conditional.o: config.h conditional.c conditional.h regexp.h
diff.o: config.h diff.h
mainutils.o: config.h mainutils.c mainutils.h parser.h
regexp.o: config.h
rules.o: config.h conditional.h rules.c regexp.h rules.h token.h util.h variable.h
parser.o: config.h array.h conditional.h regexp.h parser.c parser.h rules.h target.h token.h util.h variable.h
target.o: config.h target.h util.h
token.o: config.h conditional.h target.h token.h util.h variable.h
util.o: config.h util.c util.h
edit_bump_revision.o: config.h array.h parser.h rules.h token.h util.h variable.h edit_bump_revision.c
edit_merge.o: config.h array.h parser.h rules.h token.h util.h variable.h edit_merge.c
edit_output_variable_value.o: config.h array.h parser.h token.h variable.h edit_output_variable_value.c
edit_output_unknown_variables.o: config.h array.h parser.h rules.h token.h variable.h edit_output_variable_value.c
edit_set_version.o: config.h array.h parser.h rules.h token.h util.h variable.h edit_set_version.c
lint_order.o: config.h array.h diff.h parser.h rules.h token.h util.h variable.h lint_order.c
refactor_collapse_adjacent.o: config.h array.h parser.h token.h util.h variable.h refactor_collapse_adjacent.c
refactor_sanitize_append_modifier.o: config.h array.h parser.h rules.h token.h variable.h refactor_sanitize_append_modifier.c
refactor_sanitize_eol_comments.o: config.h array.h parser.h rules.h token.h util.h variable.h refactor_sanitize_eol_comments.c
variable.o: config.h regexp.h rules.h util.h variable.c variable.h

install:
	${MKDIR} ${DESTDIR}${BINDIR} ${DESTDIR}${LIBDIR} ${DESTDIR}${MANDIR}/man1
	${INSTALL_MAN} portfmt.1 ${DESTDIR}${MANDIR}/man1
	${INSTALL_PROGRAM} portclippy portedit portfmt ${DESTDIR}${BINDIR}
	${INSTALL_LIB} ${LIBNAME}.${LIBSUFFIX} ${DESTDIR}${LIBDIR}
	@if [ ! -L "${DESTDIR}${PREFIX}/bin/portfmt" ]; then \
		${SED} -i '' 's,/usr/local,${PREFIX},' ${DESTDIR}${PREFIX}/bin/portfmt; \
	fi

install-symlinks:
	@${MAKE} INSTALL_LIB="install -l as" INSTALL_MAN="install -l as" \
		INSTALL_PROGRAM="install -l as" INSTALL_SCRIPT="install -l as" \
		install

clean:
	@rm -f ${OBJS} portclippy portclippy.o portedit portedit.o portfmt \
		portfmt.o ${LIBNAME}.${LIBSUFFIX} config.*.old

debug:
	@${MAKE} CFLAGS="-Wall -std=c99 -O1 -g -fno-omit-frame-pointer" \
		LDFLAGS="-g" portfmt

test: portedit portfmt
	@/bin/sh run-tests.sh

.PHONY: clean debug install install-symlinks test
