${PORTEDIT} merge -e 'LDFLAGS_i386=-Wl,-znotext' 17.in | diff -L 17.expected -L 17.actual -u 17.expected -
