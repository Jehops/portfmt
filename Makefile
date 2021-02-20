include Makefile.configure

MKDIR?=		mkdir -p
LN?=		ln

CFLAGS+=	-std=gnu99 -I.
LDADD+=		-lm

OBJS=		conditional.o \
		mainutils.o \
		parser.o \
		parser/edits/edit/bump_revision.o \
		parser/edits/edit/merge.o \
		parser/edits/edit/set_version.o \
		parser/edits/kakoune/select_object_on_line.o \
		parser/edits/lint/clones.o \
		parser/edits/lint/order.o \
		parser/edits/output/unknown_targets.o \
		parser/edits/output/unknown_variables.o \
		parser/edits/output/variable_value.o \
		parser/edits/refactor/collapse_adjacent_variables.o \
		parser/edits/refactor/dedup_tokens.o \
		parser/edits/refactor/remove_consecutive_empty_lines.o \
		parser/edits/refactor/sanitize_append_modifier.o \
		parser/edits/refactor/sanitize_cmake_args.o \
		parser/edits/refactor/sanitize_comments.o \
		parser/edits/refactor/sanitize_eol_comments.o \
		portscanlog.o \
		regexp.o \
		rules.o \
		target.o \
		token.o \
		variable.o

all: portclippy portedit portfmt portscan

.c.o:
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

libportfmt.a: ${OBJS}
	${AR} rcs libportfmt.a ${OBJS}

libias/config.h: config.h
	cp config.h libias

libias/Makefile.configure: Makefile.configure
	cp Makefile.configure libias

libias/libias.a: libias/config.h libias/Makefile.configure
	@${MAKE} -C libias libias.a

portclippy: portclippy.o libias/libias.a libportfmt.a
	${CC} ${LDFLAGS} -o portclippy portclippy.o libportfmt.a libias/libias.a ${LDADD}

portedit: portedit.o libias/libias.a libportfmt.a
	${CC} ${LDFLAGS} -o portedit portedit.o libportfmt.a libias/libias.a ${LDADD}

portfmt: portfmt.o libias/libias.a libportfmt.a
	${CC} ${LDFLAGS} -o portfmt portfmt.o libportfmt.a libias/libias.a ${LDADD}

portscan: portscan.o libias/libias.a libportfmt.a
	${CC} ${LDFLAGS} -o portscan portscan.o libportfmt.a libias/libias.a ${LDADD} -lpthread

portclippy.o: portclippy.c config.h mainutils.h parser.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

portedit.o: portedit.c config.h libias/array.h mainutils.h parser.h regexp.h libias/set.h libias/util.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

portfmt.o: portfmt.c config.h mainutils.h parser.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

portscan.o: portscan.c config.h libias/array.h conditional.h libias/diff.h mainutils.h parser.h portscanlog.h regexp.h libias/set.h token.h libias/util.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

array.o: config.h libias/array.h libias/diff.h libias/util.h
conditional.o: config.h conditional.h regexp.h rules.h libias/util.h
diff.o: config.h libias/diff.h
diffutil.o: config.h libias/array.h libias/diff.h libias/diffutil.h libias/util.h
mainutils.o: config.h libias/array.h mainutils.h parser.h libias/util.h
map.o: config.h libias/array.h map.h libias/util.h
parser.o: config.h libias/array.h conditional.h libias/diffutil.h libias/mempool.h parser.h parser/constants.c regexp.h rules.h libias/set.h target.h token.h libias/util.h variable.h
parser/edits/edit/bump_revision.o: config.h libias/array.h parser.h rules.h token.h libias/util.h variable.h
parser/edits/edit/merge.o: config.h libias/array.h parser.h rules.h token.h libias/util.h variable.h
parser/edits/edit/set_version.o: config.h libias/array.h parser.h rules.h token.h libias/util.h variable.h
parser/edits/kakoune/select_object_on_line.o: config.h libias/array.h parser.h token.h libias/util.h
parser/edits/lint/clones.o: config.h libias/array.h conditional.h parser.h libias/set.h token.h libias/util.h variable.h
parser/edits/lint/order.o: config.h libias/array.h conditional.h libias/diff.h parser.h rules.h target.h token.h libias/util.h variable.h
parser/edits/output/unknown_targets.o: config.h libias/array.h parser.h rules.h libias/set.h target.h token.h libias/util.h
parser/edits/output/unknown_variables.o: config.h libias/array.h parser.h rules.h libias/set.h token.h libias/util.h variable.h
parser/edits/output/variable_value.o: config.h libias/array.h parser.h token.h libias/util.h variable.h
parser/edits/refactor/collapse_adjacent_variables.o: config.h libias/array.h parser.h libias/set.h token.h libias/util.h variable.h
parser/edits/refactor/dedup_tokens.o: config.h libias/array.h parser.h libias/set.h token.h libias/util.h variable.h
parser/edits/refactor/sanitize_append_modifier.o: config.h libias/array.h parser.h rules.h libias/set.h token.h variable.h
parser/edits/refactor/sanitize_cmake_args.o: config.h libias/array.h parser.h rules.h token.h libias/util.h variable.h
parser/edits/refactor/sanitize_comments.o: config.h libias/array.h parser.h token.h libias/util.h
parser/edits/refactor/sanitize_eol_comments.o: config.h libias/array.h parser.h rules.h token.h libias/util.h variable.h
portscanlog.o: config.h libias/array.h libias/diff.h portscanlog.h libias/set.h libias/util.h
regexp.o: config.h regexp.h libias/util.h
rules.o: config.h libias/array.h conditional.h parser.h regexp.h rules.h libias/set.h token.h libias/util.h variable.h generated_rules.c
set.o: config.h libias/array.h map.h libias/set.h libias/util.h
target.o: config.h target.h libias/util.h
token.o: config.h conditional.h target.h token.h libias/util.h variable.h
util.o: config.h libias/array.h libias/util.h
variable.o: config.h regexp.h rules.h libias/util.h variable.h

install: all
	${MKDIR} ${DESTDIR}${BINDIR} ${DESTDIR}${MANDIR}/man1
	${INSTALL_PROGRAM} portclippy portedit portfmt portscan ${DESTDIR}${BINDIR}
	${INSTALL_MAN} man/*.1 ${DESTDIR}${MANDIR}/man1

install-symlinks:
	@${MAKE} INSTALL_MAN="install -l as" INSTALL_PROGRAM="install -l as" \
		install

regen-rules:
	@/bin/sh generate_rules.sh

clean:
	@${MAKE} -C libias clean
	@rm -f ${OBJS} *.o libportfmt.a portclippy portedit portfmt portscan \
		config.*.old

debug:
	@${MAKE} CFLAGS="-Wall -std=c99 -O1 -g -fno-omit-frame-pointer" \
		LDFLAGS="-g" portfmt

lint: all
	@/bin/sh tests/lint.sh

test: all
	@/bin/sh tests/run.sh

.PHONY: all clean debug install install-symlinks libias/libias.a lint regen-rules test
