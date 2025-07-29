PROGRAM     := server
CC          := clang
DEBUGGER    := lldb

SRCS        := \
	src/main.c \
	src/tcp_server.c

BUILD_DIR   := build
OBJS        := $(patsubst src/%.c, $(BUILD_DIR)/%.o, $(SRCS))
DEPS        := $(OBJS:.o=.d)

CFLAGS      := -Wall -Wpedantic -Werror -march=native -MMD -MP
INC_DIRS    := -Isrc

ifeq ($(THREADING),1)
CFLAGS += -DTHREADING
endif

ifeq ($(DEBUG),1)
CFLAGS += -DDEBUG -g$(DEBUGGER) -O0
else
CFLAGS += -O3
endif

$(PROGRAM): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INC_DIRS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

-include $(DEPS)

.PHONY: all
all: $(PROGRAM)

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	rm $(PROGRAM)

