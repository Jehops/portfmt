${PORTEDIT} merge -e 'LDFLAGS_i386=-Wl,-znotext' 16.in | diff -L 16.expected -L 16.actual -u 16.expected -
