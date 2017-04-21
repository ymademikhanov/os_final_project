INCLUDES        = -I. -I/usr/include

LIBS		= libsocklib.a -L/usr/ucblib \
			-ldl -lnsl -lpthread -lm

COMPILE_FLAGS   = ${INCLUDES} -c
COMPILE         = gcc ${COMPILE_FLAGS}
LINK            = gcc -o

C_SRCS		= \
		passivesock.c \
		connectsock.c \
		client.c \
		server.c

SOURCE          = ${C_SRCS}

OBJS            = ${SOURCE:.c=.o}

EXEC		= client server

.SUFFIXES       :       .o .c .h

all		:	library client server

.c.o            :	${SOURCE}
			@echo "    Compiling $< . . .  "
			@${COMPILE} $<

library		:	passivesock.o connectsock.o
			ar rv libsocklib.a passivesock.o connectsock.o

server	:	server.o
			${LINK} $@ server.o ${LIBS}

client		:	client.o
			${LINK} $@ client.o ${LIBS}

clean           :
			@echo "    Cleaning ..."
			rm -f tags core *.out *.o *.lis *.a ${EXEC} libsocklib.a
