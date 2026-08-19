CC = gcc
CFLAGS = -Wall -Wextra -std=gnu99 -g -D_GNU_SOURCE
ARCHSALIDA = sys_shell
SRCS = main.c cat_datos.c cat_memoria.c cat_monitoreo.c cat_util.c cat_entrenamiento.c cat_text_editor.c
OBJS = $(addprefix output/,$(SRCS:.c=.o))

all: $(ARCHSALIDA)

$(ARCHSALIDA): $(OBJS)
	mkdir -p output
	$(CC) $(CFLAGS) -o $(ARCHSALIDA) $(OBJS)
	./$(ARCHSALIDA)

output/%.o: %.c shell.h | output
	$(CC) $(CFLAGS) -c $< -o $@

output:
	mkdir -p output

clean:
	rm -f $(ARCHSALIDA) $(OBJS)

.PHONY: all clean
