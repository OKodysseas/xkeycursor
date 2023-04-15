CC = gcc
CFLAGS = -g -Wall -Wextra -pedantic
LFLAGS = -lX11
OBJ = xkeycursor.o

all: xkeycursor 

xkeycursor: $(OBJ)
	$(CC) $(OBJ) $(CFLAGS) $(LFLAGS) -o xkeycursor 

%.o: %.c
	$(CC) -c $(CFLAGS) $<

clean:
	rm -rf *.o

.PHONY: all clean

