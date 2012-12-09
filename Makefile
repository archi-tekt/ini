CC=gcc
LFLAGS=-lncurses -lreadline

ini: ini.c
	$(CC) $(LFLAGS) -o $@ $<
clean:
	-$(RM) ini
