CFLAGS=	-D__GPS -D USE_MYSQL -I /usr/include/mysql -L /usr/lib64/mysql 

SRC=	main.c	\
	session.c \
	ws_util.c \
	gomoclient.c jsmn.c

OBJ=	$(SRC:.c=.o)

all:	iris_mysql

iris_mysql: $(OBJ)
	gcc $(CFLAGS) $(OBJ) -lmysqlclient  -lpthread -lcurl -lz -o $@

.c.o:	$(SRC)
	gcc -c $(CFLAGS) $< -o $@

clean:
	-rm -f *.o

