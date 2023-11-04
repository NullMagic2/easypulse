CC = gcc
CFLAGS = -Wall -g -Wextra

LIB_NAME = easypulse_core
LIB_SRC = easypulse_core.c
LIB_OBJ = easypulse_core.o
LIB_OUT = lib$(LIB_NAME).a

all: $(LIB_OUT)

$(LIB_OUT): $(LIB_OBJ)
	ar rcs $(LIB_OUT) $(LIB_OBJ)

$(LIB_OBJ): $(LIB_SRC)
	$(CC) $(CFLAGS) -c $(LIB_SRC) -o $(LIB_OBJ)

clean:
	rm -f $(LIB_OBJ) $(LIB_OUT)

.PHONY: all clean
