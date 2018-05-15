# NAME: Christopher Ngai
# EMAIL: cngai1223@gmail.com
# ID: 404795904

CC = gcc
CFLAGS = -Wall -Wextra

default:
	$(CC) $(CFLAGS) -lmraa -lm -o lab4c_tcp lab4c_tcp.c
	$(CC) $(CFLAGS) -lmraa -lm -lssl -lcrypto -o lab4c_tls lab4c_tls.c

clean:
	rm -rf lab4c_tcp lab4c_tls lab4c-404795904.tar.gz

dist:
	tar -czf lab4c-404795904.tar.gz lab4c_tcp.c lab4c_tls.c Makefile README