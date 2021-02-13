include Makefile.configure

MKDIR?=		mkdir -p
LN?=		ln

CFLAGS+=	-std=gnu99 -I.
LDADD+=		-lm libias/libias.a

OBJS=		conditional.o \
		mainutils.o \
		parser.o \
		parser/plugin.edit.bump-revision.o \
		parser/plugin.edit.merge.o \
		parser/plugin.edit.set-version.o \
		parser/plugin.kakoune.select-object-on-line.o \
		parser/plugin.lint.clones.o \
		parser/plugin.lint.order.o \
		parser/plugin.output.unknown-targets.o \
		parser/plugin.output.unknown-variables.o \
		parser/plugin.output.variable-value.o \
		parser/plugin.refactor.collapse-adjacent-variables.o \
		parser/plugin.refactor.dedup-tokens.o \
		parser/plugin.refactor.remove-consecutive-empty-lines.o \
		parser/plugin.refactor.sanitize-append-modifier.o \
		parser/plugin.refactor.sanitize-cmake-args.o \
		parser/plugin.refactor.sanitize-comments.o \
		parser/plugin.refactor.sanitize-eol-comments.o \
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

portclippy: libportfmt.a portclippy.o
	${CC} ${LDFLAGS} -o portclippy portclippy.o libportfmt.a ${LDADD}

portedit: libportfmt.a portedit.o
	${CC} ${LDFLAGS} -o portedit portedit.o libportfmt.a ${LDADD}

portfmt: libportfmt.a portfmt.o
	${CC} ${LDFLAGS} -o portfmt portfmt.o libportfmt.a ${LDADD}

portscan: libportfmt.a portscan.o
	${CC} ${LDFLAGS} -o portscan portscan.o libportfmt.a ${LDADD} -lpthread

portclippy.o: portclippy.c config.h mainutils.h parser.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

portedit.o: portedit.c config.h libias/array.h mainutils.h parser.h regexp.h libias/set.h libias/util.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

portfmt.o: portfmt.c config.h mainutils.h parser.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

portscan.o: portscan.c config.h libias/array.h conditional.h libias/diff.h mainutils.h parser.h portscanlog.h regexp.h libias/set.h token.h libias/util.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

parser/plugin_registry.h:
	@echo '${PLUGINS}' | tr ' ' '\n' | sed 's/.${PLUGINSUFFIX}$$/.c/' | xargs grep -h ^PLUGIN | sed 's/;$$//' > parser/plugin_registry.h

array.o: config.h libias/array.h libias/diff.h libias/util.h
conditional.o: config.h conditional.h regexp.h rules.h libias/util.h
diff.o: config.h libias/diff.h
diffutil.o: config.h libias/array.h libias/diff.h libias/diffutil.h libias/util.h
mainutils.o: config.h libias/array.h mainutils.h parser.h libias/util.h
map.o: config.h libias/array.h map.h libias/util.h
parser.o: config.h libias/array.h conditional.h libias/diffutil.h parser.h parser/constants.c regexp.h rules.h libias/set.h target.h token.h libias/util.h variable.h
parser/plugin.edit.bump-revision.o: config.h libias/array.h parser.h rules.h token.h libias/util.h variable.h
parser/plugin.edit.merge.o: config.h libias/array.h parser.h rules.h token.h libias/util.h variable.h
parser/plugin.edit-set-version.o: config.h libias/array.h parser.h rules.h token.h libias/util.h variable.h
parser/plugin.kakoune.select-object-on-line.o: config.h libias/array.h parser.h token.h libias/util.h
parser/plugin.lint.clones.o: config.h libias/array.h conditional.h parser.h libias/set.h token.h libias/util.h variable.h
parser/plugin.lint.order.o: config.h libias/array.h conditional.h libias/diff.h parser.h rules.h target.h token.h libias/util.h variable.h
parser/plugin.output.unknown-targets.o: config.h libias/array.h parser.h rules.h libias/set.h target.h token.h libias/util.h
parser/plugin.output.unknown-variables.o: config.h libias/array.h parser.h rules.h libias/set.h token.h libias/util.h variable.h
parser/plugin.output.variable-value.o: config.h libias/array.h parser.h token.h libias/util.h variable.h
parser/plugin.refactor.collapse-adjacent-variables.o: config.h libias/array.h parser.h libias/set.h token.h libias/util.h variable.h
parser/plugin.refactor.dedup-tokens.o: config.h libias/array.h parser.h libias/set.h token.h libias/util.h variable.h
parser/plugin.refactor.sanitize-append-modifier.o: config.h libias/array.h parser.h rules.h libias/set.h token.h variable.h
parser/plugin.refactor.sanitize-cmake-args.o: config.h libias/array.h parser.h rules.h token.h libias/util.h variable.h
parser/plugin.refactor.sanitize-comments.o: config.h libias/array.h parser.h token.h libias/util.h
parser/plugin.refactor.sanitize-eol-comments.o: config.h libias/array.h parser.h rules.h token.h libias/util.h variable.h
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
	@rm -f ${OBJS} ${PLUGINS} parser/*.o *.o libportfmt.a portclippy portedit \
		portfmt portscan config.*.old

debug:
	@${MAKE} CFLAGS="-Wall -std=c99 -O1 -g -fno-omit-frame-pointer" \
		LDFLAGS="-g" portfmt

lint: all
	@/bin/sh tests/lint.sh

test: all
	@/bin/sh tests/run.sh

.PHONY: all clean debug install install-symlinks lint regen-rules test
