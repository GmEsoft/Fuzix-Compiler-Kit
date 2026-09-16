all: fmake

fmake: fmake.o
	$(CC) $(CFLAGS) $< -o $@

fmake.c: make.h

clean:
	rm -f fmake.o fmake
