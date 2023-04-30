SRC=src
SRCS = $(SRC)/bridge.c $(SRC)/debug.c $(SRC)/getcmd.c $(SRC)/ip.c $(SRC)/init.c $(SRC)/modem_core.c $(SRC)/nvt.c $(SRC)/serial.c $(SRC)/ip232.c $(SRC)/util.c $(SRC)/phone_book.c $(SRC)/tcpser.c $(SRC)/line.c $(SRC)/dce.c
OBJS = $(SRC)/bridge.o $(SRC)/debug.o $(SRC)/getcmd.o $(SRC)/ip.o $(SRC)/init.o $(SRC)/modem_core.o $(SRC)/nvt.o $(SRC)/serial.o $(SRC)/ip232.o $(SRC)/util.o $(SRC)/phone_book.o $(SRC)/tcpser.o $(SRC)/dce.o $(SRC)/line.o

TERMINAL_SUPPORT_SRCS = $(SRC)/serial_port.c $(SRC)/serial_side.c $(SRC)/terminal.c
TERMINAL_SUPPORT_OBJS = $(SRC)/serial_port.o $(SRC)/serial_side.o $(SRC)/terminal.o

SRCS += $(TERMINAL_SUPPORT_SRCS)
OBJS += $(TERMINAL_SUPPORT_OBJS)

CC = gcc
DEF = 
CFLAGS = -O $(DEF) -Wall
LDFLAGS = -lpthread
DEPEND = makedepend $(DEF) $(CFLAGS)

all:	tcpser

#.o.c:
#	$(CC) $(CFLAGS) -c $*.c

$(SRCS):
	$(CC) $(CFLAGS) -c $*.c

tcpser: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -g -o $@

depend: $(SRCS)
	$(DEPEND) $(SRCS)

clean:
	-rm tcpser *.bak $(SRC)/*~ $(SRC)/*.o $(SRC)/*.bak core


# DO NOT DELETE THIS LINE -- make depend depends on it.

