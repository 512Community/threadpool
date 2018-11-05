CC=g++

CFLAGS +=-Werror -g

OBJ= $(patsubst %.c, %.o, $(wildcard *.c))

INCLUDE= -I../include

LIB= -L../ -lthread_pool

target=a.out

$(target):$(OBJ)
	$(CC)  -Wl,-rpath=./ $^ -o $@ $(LIB)
	mv $(target) ../

%.o:%.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INCLUDE)

clean:
	rm -rf $(OBJ) ../$(target)
