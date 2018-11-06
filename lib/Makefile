CC=gcc
CXX=g++

#CFLAGS=-Werror -g -fPIC -DDEBUG_LOG
CFLAGS=-Werror -g -fPIC

OBJ+= $(patsubst %.c, %.o, $(wildcard ./*.c))

INCLUDE= -I../include
LIB= -pthread

target=libthread_pool.so

$(target):$(OBJ)
	$(CC) -shared $^ -o $@ $(LIB)
	mv $(target) ../

%.o:%.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INCLUDE)

clean:
	rm -rf $(OBJ) ../$(target)
