include Makefile.configure

MKDIR?=		mkdir -p
LN?=		ln
SED?=		sed

CFLAGS+=	-std=c99
LDADD+=		-lm

OBJS=		array.o compats.o conditional.o diff.o edit_bump_revision.o \
		edit_output_unknown_variables.o edit_output_variable_value.o \
		lint_order.o parser.o portfmt.o refactor_collapse_adjacent.o \
		refactor_sanitize_append_modifier.o \
		refactor_sanitize_eol_comments.o rules.o target.o token.o \
		util.o variable.o

all: portedit portfmt

diffwords: array.o diffwords.o diff.o util.o compats.o
	${CC} ${LDFLAGS} -o diffwords array.o util.o compats.o diffwords.o diff.o ${LDADD}

portfmt: ${OBJS}
	${CC} ${LDFLAGS} -o portfmt ${OBJS} ${LDADD}

portedit: portfmt
	${LN} -sf portfmt portedit

array.o: config.h array.c array.h diff.h
conditional.o: config.h conditional.c conditional.h
diff.o: config.h diff.h
portfmt.o: config.h parser.h portfmt.c
rules.o: config.h rules.c rules.h token.h util.h variable.h
parser.o: config.h array.h conditional.h parser.c parser.h rules.h target.h token.h util.h variable.h
target.o: config.h target.h util.h
token.o: config.h conditional.h target.h token.h util.h variable.h
util.o: config.h util.c util.h
edit_bump_revision.o: config.h array.h parser.h rules.h token.h util.h variable.h edit_bump_revision.c
edit_output_variable_value.o: config.h array.h parser.h token.h variable.h edit_output_variable_value.c
edit_output_unknown_variables.o: config.h array.h parser.h rules.h token.h variable.h edit_output_variable_value.c
lint_order.o: config.h array.h conditional.h diff.h parser.h rules.h token.h util.h variable.h lint_order.c
refactor_collapse_adjacent.o: config.h array.h parser.h token.h util.h variable.h refactor_collapse_adjacent.c
refactor_sanitize_append_modifier.o: config.h array.h conditional.h parser.h token.h variable.h refactor_sanitize_append_modifier.c
refactor_sanitize_eol_comments.o: config.h array.h parser.h rules.h token.h util.h variable.h refactor_sanitize_eol_comments.c
variable.o: config.h rules.h util.h variable.c variable.h

install:
	${MKDIR} ${DESTDIR}${PREFIX}/bin \
		${DESTDIR}${MANDIR}/man1
	${INSTALL_MAN} portfmt.1 ${DESTDIR}${MANDIR}/man1
	${INSTALL_PROGRAM} portfmt ${DESTDIR}${PREFIX}/bin
	cd ${DESTDIR}${PREFIX}/bin && ${LN} -sf portfmt portedit
	${INSTALL_SCRIPT} portclippy ${DESTDIR}${PREFIX}/bin
	@if [ ! -L "${DESTDIR}${PREFIX}/bin/portfmt" ]; then \
		${SED} -i '' 's,/usr/local,${PREFIX},' ${DESTDIR}${PREFIX}/bin/portfmt; \
	fi

install-symlinks:
	@${MAKE} INSTALL_MAN="install -l as" \
		INSTALL_PROGRAM="install -l as" \
		INSTALL_SCRIPT="install -l as" \
		install

clean:
	@rm -f ${OBJS} portedit portfmt config.*.old

debug:
	@${MAKE} CFLAGS="-Wall -std=c99 -O1 -g -fno-omit-frame-pointer" \
		LDFLAGS="-g" \
		portfmt

test: portedit portfmt
	@/bin/sh run-tests.sh

.PHONY: clean debug install install-symlinks test
