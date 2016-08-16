#
# Makefile for dnc
#

CLEANFILES	 = dnc
CFLAGS		+= -O2 -Werror -Wall -Wextra -m64 -fno-omit-frame-pointer
LDFLAGS		+= -lgen -lsocket -lnsl

dnc: dnc.c
	$(CC) -o $@ $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^

clean:
	rm -f $(CLEANFILES)
