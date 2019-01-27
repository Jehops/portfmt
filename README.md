# portfmt

It formats FreeBSD Port Makefiles to what I consider to be an
acceptable format for inclusion in the FreeBSD Ports collection.

This is a giant hack and best used in moderation on only parts of
the Makefile for now.  There are many, many problems, so be careful
and use it wisely.

## Kakoune integration

In your `~/.config/kak/kakrc` mapped to `,1`:
```
map global user 1 '|portfmt.awk<ret>;' -docstring "portfmt on selection"
```

