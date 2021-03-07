include Makefile.configure

MKDIR?=		mkdir -p
LN?=		ln
SH?=		/bin/sh

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

libias/libias.a: libias libias/config.h libias/Makefile.configure
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

#
conditional.o: config.h libias/util.h conditional.h regexp.h rules.h
mainutils.o: config.h libias/array.h libias/util.h mainutils.h parser.h
parser.o: config.h libias/array.h libias/diff.h libias/diffutil.h libias/map.h libias/mempool.h libias/set.h libias/util.h conditional.h parser.h parser/edits.h regexp.h rules.h target.h token.h variable.h parser/constants.c
parser/edits/edit/bump_revision.o: config.h libias/array.h libias/util.h parser.h parser/edits.h rules.h token.h variable.h
parser/edits/edit/merge.o: config.h libias/array.h libias/util.h parser.h parser/edits.h rules.h token.h variable.h
parser/edits/edit/set_version.o: config.h libias/array.h libias/util.h parser.h parser/edits.h rules.h token.h variable.h
parser/edits/kakoune/select_object_on_line.o: config.h libias/array.h libias/util.h parser.h parser/edits.h token.h
parser/edits/lint/clones.o: config.h libias/array.h libias/set.h libias/util.h conditional.h parser.h parser/edits.h token.h variable.h
parser/edits/lint/order.o: config.h libias/array.h libias/diff.h libias/util.h conditional.h parser.h parser/edits.h rules.h target.h token.h variable.h
parser/edits/output/unknown_targets.o: config.h libias/array.h libias/set.h libias/util.h parser.h parser/edits.h rules.h target.h token.h
parser/edits/output/unknown_variables.o: config.h libias/array.h libias/set.h libias/util.h parser.h parser/edits.h rules.h token.h variable.h
parser/edits/output/variable_value.o: config.h libias/array.h libias/util.h parser.h parser/edits.h token.h variable.h
parser/edits/refactor/collapse_adjacent_variables.o: config.h libias/array.h libias/set.h libias/util.h parser.h parser/edits.h token.h variable.h
parser/edits/refactor/dedup_tokens.o: config.h libias/array.h libias/set.h libias/util.h parser.h parser/edits.h rules.h token.h variable.h
parser/edits/refactor/remove_consecutive_empty_lines.o: config.h libias/array.h libias/util.h parser.h parser/edits.h rules.h token.h variable.h
parser/edits/refactor/sanitize_append_modifier.o: config.h libias/array.h libias/set.h parser.h parser/edits.h rules.h token.h variable.h
parser/edits/refactor/sanitize_cmake_args.o: config.h libias/array.h libias/util.h parser.h parser/edits.h rules.h token.h variable.h
parser/edits/refactor/sanitize_comments.o: config.h libias/array.h libias/util.h parser.h parser/edits.h token.h
parser/edits/refactor/sanitize_eol_comments.o: config.h libias/array.h libias/util.h parser.h parser/edits.h rules.h token.h variable.h
portclippy.o: config.h mainutils.h parser.h parser/edits.h
portedit.o: config.h libias/array.h libias/set.h libias/util.h mainutils.h parser.h parser/edits.h regexp.h
portfmt.o: config.h mainutils.h parser.h
portscan.o: config.h libias/array.h libias/diff.h libias/map.h libias/set.h libias/util.h conditional.h mainutils.h parser.h parser/edits.h portscanlog.h regexp.h token.h variable.h
portscanlog.o: config.h libias/array.h libias/diff.h libias/set.h libias/util.h portscanlog.h
regexp.o: config.h libias/util.h regexp.h
rules.o: config.h libias/array.h libias/set.h libias/util.h conditional.h regexp.h rules.h parser.h token.h variable.h generated_rules.c
target.o: config.h libias/util.h target.h
token.o: config.h libias/util.h conditional.h target.h token.h variable.h
variable.o: config.h libias/util.h regexp.h rules.h variable.h

deps:
	@for f in $$(git ls-files | grep '.*\.c$$' | sort); do \
	awk '/^#include ("|<libias\/)/ { \
		if (!filename) { \
			printf("%s.o:", substr(FILENAME, 1, length(FILENAME) - 2)); \
			filename = 1; \
		} \
		printf(" %s", substr($$2, 2, length($$2) - 2)) \
	} \
	END { if (filename) { print "" } }' $$f; done > Makefile.deps
	@mv Makefile Makefile.bak
	@awk '/^#$$/ { print; deps = 1 } \
	deps && /^$$/ { deps = 0; system("cat Makefile.deps") } \
	!deps { print; }' Makefile.bak > Makefile
	@rm -f Makefile.bak Makefile.deps

install: all
	${MKDIR} ${DESTDIR}${BINDIR} ${DESTDIR}${MANDIR}/man1
	${INSTALL_PROGRAM} portclippy portedit portfmt portscan ${DESTDIR}${BINDIR}
	${INSTALL_MAN} man/*.1 ${DESTDIR}${MANDIR}/man1

install-symlinks:
	@${MAKE} INSTALL_MAN="/bin/sh -c 'f=\"$$\$$(echo \"$$\$$@\" | xargs realpath)\"; ln -sf $$\$$f' -- SYMLINK " \
		INSTALL_PROGRAM="\$${INSTALL_MAN}" install

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
	@${SH} tests/lint.sh

test: all
	@${SH} tests/run.sh

tag:
	@date=$$(git log -1 --pretty=format:%cd --date=format:%Y-%m-%d $${tag}); \
	tag=g$$(git log -1 --pretty=format:%cd --date=format:%Y%m%d); \
	title="## [$${tag}] - $${date}"; \
	if ! grep -Fq "$${title}" CHANGELOG.md; then \
		echo "# portfmt $${tag}"; \
		awk '/^## Unreleased$$/{x=1;next}x{if($$1=="##"){exit}else if($$1=="###"){$$1="##"};print}' \
			CHANGELOG.md >RELNOTES.md; \
		awk "/^## Unreleased$$/{print;printf\"\n$${title}\n\";next}{print}" \
			CHANGELOG.md >CHANGELOG.md.new; \
		mv CHANGELOG.md.new CHANGELOG.md; \
	fi
	git commit -m "Release $${tag}" CHANGELOG.md; \
	git tag $${tag}

release:
	@tag=$$(git tag --points-at HEAD); \
	if [ -z "$$tag" ]; then echo "create a tag first"; exit 1; fi; \
	git ls-files --recurse-submodules . ':!:libias/tests' | \
		bsdtar --files-from=- -s '/^/portfmt-$${tag}/' --options lzip:compression-level=9 \
			-caf portfmt-$${tag}.tar.lz; \
	sha256 portfmt-$${tag}.tar.lz >portfmt-$${tag}.tar.lz.SHA256 || \
	sha256sum --tag portfmt-$${tag}.tar.lz >portfmt-$${tag}.tar.lz.SHA256; \
	printf "SIZE (%s) = %s\n" portfmt-$${tag}.tar.lz $$(wc -c <portfmt-$${tag}.tar.lz) \
		>>portfmt-$${tag}.tar.lz.SHA256

publish:
	@tag=$$(git tag --points-at HEAD); \
	if [ -z "$$tag" ]; then echo "create a tag first"; exit 1; fi; \
	git push --follow-tags origin; \
	hub release create -F RELNOTES.md $${tag} \
		-a portfmt-$${tag}.tar.lz \
		-a portfmt-$${tag}.tar.lz.SHA256

.PHONY: all clean debug deps install install-symlinks lint publish release regen-rules tag test
