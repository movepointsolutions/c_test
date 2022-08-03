all: 1 2

1: 1.c
	gcc -g -o $@ -lev $<

2: 2.c
	gcc -g -o $@ -lev $<
