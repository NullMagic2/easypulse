CC = gcc
CFLAGS = -Wall -Wextra -g

LIB_DIR = ../
LIB_SRC = $(LIB_DIR)easypulse_core.c

EXAMPLES_DIR = .
EXAMPLES_SRC = $(wildcard $(EXAMPLES_DIR)/*.c)
EXAMPLES_OUT = $(patsubst $(EXAMPLES_DIR)/%.c,$(EXAMPLES_DIR)/%,$(EXAMPLES_SRC))

all: $(EXAMPLES_OUT)

$(EXAMPLES_DIR)/%: $(EXAMPLES_DIR)/%.c
	$(CC) $(CFLAGS) $< $(LIB_SRC) -o $@ -lpulse

clean:
	rm -f $(EXAMPLES_DIR)/*~ $(EXAMPLES_OUT)

.PHONY: all clean
