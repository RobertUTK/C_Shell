PROG = jsh

LIBFDR = /home/cosc360/libfdr
GS_DIR = /home/cosc360/lab7/gs

CC = gcc
INCLUDES = -I$(LIBFDR)/include/
CFLAGS = -g -Wall $(INCLUDES)

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
LIBS = $(LIBFDR)/lib/libfdr.a

all: $(PROG)

$(PROG): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

.PHONY: gs ga clean

gs: $(PROG)
	$(GS_DIR)/gradescript $(test)

ga: $(PROG)
	$(GS_DIR)/gradeall

clean:
	rm -rf $(PROG) $(OBJ) $(OBJ:.o=.d) tmp-* f*.txt

-include $(OBJ:.o=.d)
