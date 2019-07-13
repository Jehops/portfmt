${PORTEDIT} get '(^PORT|CONFIG)' 3.in | diff -L 3.expected -L 3.actual -u 3.expected -
