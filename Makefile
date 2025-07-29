CC := clang
SRCS :=\
	src/main.c\
	src/tcp_server.c\

CFLAGS := -Wall -Wpedantic -Werror -march=native -glldb
INC_DIRS := -I./src

ifdef THREADING
CFLAGS += -DTHREADING
endif

server: $(SRCS)
	$(CC) $(CFLAGS) $(INC_DIRS) $^ -o $@
