CC = gcc
AS = as
CFLAGS = -g -Wall -std=gnu99  -DHAMM_ASM -DASM
# enable Assembler version of Hamming distance
#  -DHAMM_ASM
# enable Assembler interface to low-level functions:
# -DASM

OBJS = pin-crackin.o lcd-fcts.o aux.o lcd-binary.o hamming.o
LIBS = -lm

all: pin-cracking

# Link the object files to give the program
pin-cracking: $(OBJS)
	$(CC) $(CFLAGS) -o cw2 $(OBJS) $(LIBS)

# Compile a C source file to an object file
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile an assembler source file to an object file
%.o: %.s
	$(AS) -c -o $@ $<


# Header file dependencies

pin-cracking.o: config.h aux.h lcd-binary.h lcd-fcts.h
aux.o: aux.h
lcd-fcts.o: config.h lcd-fcts.h lcd-binary.h
lcd-binary.o: lcd-binary.h

clean:
	rm -f pin-cracking $(OBJS)
