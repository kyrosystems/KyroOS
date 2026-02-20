# Makefile for hello_lkm

TARGET = x86_64-pc-linux-gnu
CC = $(TARGET)-gcc
K_CFLAGS = -Wall -Wextra -std=c11 -ffreestanding -O2 -I../../src/include \
           -mcmodel=large -mno-red-zone -m64 -c -fno-stack-protector -nostdlib

SRC = hello.c
OBJ = hello.o
KO = hello.ko

all: $(KO)

$(KO): $(OBJ)
	cp -f $(OBJ) $(KO)
	# Signing step would happen here with a userspace tool

$(OBJ): $(SRC)
	$(CC) $(K_CFLAGS) -o $@ $<

clean:
	rm -f *.o *.ko
