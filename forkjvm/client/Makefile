ccflags = -Wall -Werror

all: java

java: java.o
	gcc -o java java.o -lcrypto

java.o: java.c
	gcc $(ccflags) -c java.c

clean:
	rm java.o java
