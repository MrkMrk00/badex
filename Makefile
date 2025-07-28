SRCS:=src/main.c src/tcp_server.c

server: $(SRCS)
	cc -Isrc -march=native -O3 -Wall -Wpedantic -Werror -o server $^
