${PORTEDIT} merge -e 'LDFLAGS_i386=-Wl,-znotext' 18.in | diff -L 18.expected -L 18.actual -u 18.expected -
