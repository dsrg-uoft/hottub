CFLAGS = -Wall -Werror

.PHONY: all clean

all: java

clean:
	rm java.o java

java: java.o
	gcc -o java java.o -lcrypto -lrt

java.o: java.c java.h
	gcc $(CFLAGS) -c java.c
