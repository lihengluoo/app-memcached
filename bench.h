#ifndef BENCH_H
#define BENCH_H

/*
 * In-process driver for the memcached storage engine: runs the
 * item/assoc/slabs core from bench_thread_count pthreads that call the
 * thread-safe item API in thread.c directly through stub connections, so no
 * sockets, protocol parsing, or libevent event loops are on the measured
 * path.
 */

int bench_run(void);

#endif /* BENCH_H */
