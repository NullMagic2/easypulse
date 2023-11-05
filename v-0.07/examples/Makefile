CC = gcc
CFLAGS = -Wall -Wextra -g -O0

LIB_DIR = ../
LIB_SRC = $(LIB_DIR)*.c

EXAMPLES_DIR = .
EXAMPLES_SRC = $(wildcard $(EXAMPLES_DIR)/*.c)
EXAMPLES_OUT = $(patsubst $(EXAMPLES_DIR)/%.c,$(EXAMPLES_DIR)/%,$(EXAMPLES_SRC))

all: $(EXAMPLES_OUT)

$(EXAMPLES_DIR)/%: $(EXAMPLES_DIR)/%.c
	$(CC) $(CFLAGS) $< $(LIB_SRC) -o $@ -lpulse -lasound

clean:
	rm -f $(EXAMPLES_DIR)/*~ $(EXAMPLES_OUT)

.PHONY: all clean
