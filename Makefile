LIBNAME=	libportfmt

include Makefile.configure

MKDIR?=		mkdir -p
LN?=		ln

CFLAGS+=	-std=gnu99 -I.
LDADD+=		${LIBDL} -lm libias/libias.a

OBJS=		conditional.o mainutils.o parser.o parser/plugin.o \
		portscanlog.o regexp.o rules.o target.o token.o variable.o
PLUGINS=	parser/plugin.edit.bump-revision.${PLUGINSUFFIX} \
		parser/plugin.edit.merge.${PLUGINSUFFIX} \
		parser/plugin.edit.set-version.${PLUGINSUFFIX} \
		parser/plugin.kakoune.select-object-on-line.${PLUGINSUFFIX} \
		parser/plugin.lint.clones.${PLUGINSUFFIX} \
		parser/plugin.lint.order.${PLUGINSUFFIX} \
		parser/plugin.output.unknown-targets.${PLUGINSUFFIX} \
		parser/plugin.output.unknown-variables.${PLUGINSUFFIX} \
		parser/plugin.output.variable-value.${PLUGINSUFFIX} \
		parser/plugin.refactor.collapse-adjacent-variables.${PLUGINSUFFIX} \
		parser/plugin.refactor.dedup-tokens.${PLUGINSUFFIX} \
		parser/plugin.refactor.remove-consecutive-empty-lines.${PLUGINSUFFIX} \
		parser/plugin.refactor.sanitize-append-modifier.${PLUGINSUFFIX} \
		parser/plugin.refactor.sanitize-cmake-args.${PLUGINSUFFIX} \
		parser/plugin.refactor.sanitize-comments.${PLUGINSUFFIX} \
		parser/plugin.refactor.sanitize-eol-comments.${PLUGINSUFFIX}

default: portclippy portedit portfmt portscan ${PLUGINS}

.c.o:
	${CC} ${CPPFLAGS} -fPIC ${CFLAGS} -o $@ -c $<

${LIBNAME}.${LIBSUFFIX}: ${OBJS} ${PLUGINS}
	${LINK_LIBRARY} ${OBJS}

portclippy: ${LIBNAME}.${LIBSUFFIX} portclippy.o
	${LINK_PROGRAM} portclippy

portedit: ${LIBNAME}.${LIBSUFFIX} portedit.o
	${LINK_PROGRAM} portedit

portfmt: ${LIBNAME}.${LIBSUFFIX} portfmt.o
	${LINK_PROGRAM} portfmt

portscan: ${LIBNAME}.${LIBSUFFIX} portscan.o
	${LINK_PROGRAM} portscan -lpthread

portclippy.o: portclippy.c config.h mainutils.h parser.h parser/plugin.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

portedit.o: portedit.c config.h libias/array.h mainutils.h parser.h parser/plugin.h regexp.h libias/set.h libias/util.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

portfmt.o: portfmt.c config.h mainutils.h parser.h parser/plugin.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

portscan.o: portscan.c config.h libias/array.h conditional.h libias/diff.h mainutils.h parser.h parser/plugin.h portscanlog.h regexp.h libias/set.h token.h libias/util.h
	${CC} ${CPPFLAGS} ${CFLAGS} -o $@ -c $<

parser/plugin_registry.h: ${PLUGINS}
	@echo '${PLUGINS}' | tr ' ' '\n' | sed 's/.${PLUGINSUFFIX}$$/.c/' | xargs grep -h ^PLUGIN | sed 's/;$$//' > parser/plugin_registry.h

array.o: config.h libias/array.h libias/diff.h libias/util.h
conditional.o: config.h conditional.h regexp.h rules.h libias/util.h
diff.o: config.h libias/diff.h
diffutil.o: config.h libias/array.h libias/diff.h libias/diffutil.h libias/util.h
mainutils.o: config.h libias/array.h mainutils.h parser.h libias/util.h
map.o: config.h libias/array.h map.h libias/util.h
parser.o: config.h libias/array.h conditional.h libias/diffutil.h parser.h parser/constants.c parser/plugin.h regexp.h rules.h libias/set.h target.h token.h libias/util.h variable.h
parser/plugin.o: config.h parser.h parser/plugin.h parser/plugin_registry.h libias/util.h
parser/plugin.edit.bump-revision.o: config.h libias/array.h parser.h parser/plugin.h rules.h token.h libias/util.h variable.h
parser/plugin.edit.merge.o: config.h libias/array.h parser.h parser/plugin.h rules.h token.h libias/util.h variable.h
parser/plugin.edit-set-version.o: config.h libias/array.h parser.h parser/plugin.h rules.h token.h libias/util.h variable.h
parser/plugin.kakoune.select-object-on-line.o: config.h libias/array.h parser.h parser/plugin.h token.h libias/util.h
parser/plugin.lint.clones.o: config.h libias/array.h conditional.h parser.h parser/plugin.h libias/set.h token.h libias/util.h variable.h
parser/plugin.lint.order.o: config.h libias/array.h conditional.h libias/diff.h parser.h parser/plugin.h rules.h target.h token.h libias/util.h variable.h
parser/plugin.output.unknown-targets.o: config.h libias/array.h parser.h parser/plugin.h rules.h libias/set.h target.h token.h libias/util.h
parser/plugin.output.unknown-variables.o: config.h libias/array.h parser.h parser/plugin.h rules.h libias/set.h token.h libias/util.h variable.h
parser/plugin.output.variable-value.o: config.h libias/array.h parser.h parser/plugin.h token.h libias/util.h variable.h
parser/plugin.refactor.collapse-adjacent-variables.o: config.h libias/array.h parser.h parser/plugin.h libias/set.h token.h libias/util.h variable.h
parser/plugin.refactor.dedup-tokens.o: config.h libias/array.h parser.h parser/plugin.h libias/set.h token.h libias/util.h variable.h
parser/plugin.refactor.sanitize-append-modifier.o: config.h libias/array.h parser.h parser/plugin.h rules.h libias/set.h token.h variable.h
parser/plugin.refactor.sanitize-cmake-args.o: config.h libias/array.h parser.h parser/plugin.h rules.h token.h libias/util.h variable.h
parser/plugin.refactor.sanitize-comments.o: config.h libias/array.h parser.h parser/plugin.h token.h libias/util.h
parser/plugin.refactor.sanitize-eol-comments.o: config.h libias/array.h parser.h parser/plugin.h rules.h token.h libias/util.h variable.h
portscanlog.o: config.h libias/array.h libias/diff.h portscanlog.h libias/set.h libias/util.h
regexp.o: config.h regexp.h libias/util.h
rules.o: config.h libias/array.h conditional.h parser.h regexp.h rules.h libias/set.h token.h libias/util.h variable.h generated_rules.c
set.o: config.h libias/array.h map.h libias/set.h libias/util.h
target.o: config.h target.h libias/util.h
token.o: config.h conditional.h target.h token.h libias/util.h variable.h
util.o: config.h libias/array.h libias/util.h
variable.o: config.h regexp.h rules.h libias/util.h variable.h

install-programs: portclippy portedit portfmt portscan
	${MKDIR} ${DESTDIR}${BINDIR}
	${INSTALL_PROGRAM} portclippy portedit portfmt portscan ${DESTDIR}${BINDIR}

install-lib: ${LIBNAME}.${LIBSUFFIX} ${PLUGINS}
	${INSTALL_LIB} ${LIBNAME}.${LIBSUFFIX} ${DESTDIR}${LIBDIR}
	${MKDIR} ${DESTDIR}${LIBDIR}/portfmt
	${INSTALL_LIB} ${PLUGINS} ${DESTDIR}${LIBDIR}/portfmt

install-man:
	${MKDIR} ${DESTDIR}${MANDIR}/man1
	${INSTALL_MAN} man/*.1 ${DESTDIR}${MANDIR}/man1

install-symlinks:
	@${MAKE} INSTALL_LIB="install -l as" INSTALL_MAN="install -l as" \
		INSTALL_PROGRAM="install -l as" INSTALL_SCRIPT="install -l as" \
		install

regen-rules:
	@/bin/sh generate_rules.sh

clean:
	@rm -f ${OBJS} ${PLUGINS} parser/*.o *.o libportfmt.${LIBSUFFIX} portclippy portedit portfmt \
		portscan config.*.old

debug:
	@${MAKE} CFLAGS="-Wall -std=c99 -O1 -g -fno-omit-frame-pointer" \
		LDFLAGS="-g" portfmt

test: all
	@/bin/sh run-tests.sh

.PHONY: default clean debug install install-programs install-lib install-man install-symlinks regen-rules test
