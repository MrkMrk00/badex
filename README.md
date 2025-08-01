# Design


1. TCP server loop: currently `connect()` + `select()` (TODO: kqueue/epoll/poll)

TODO:
2. -> IO in separate thread(s).
3. -> Thread pool and sharded DB per thread count.
