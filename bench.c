#include "bench.h"

#include "memcached.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Build note: this file provides the only main() and must be linked with the
 * engine objects while the server main() in memcached.c is compiled out.
 * Knobs are overridable from the build command line.
 */
#ifndef bench_thread_count
#define bench_thread_count 16
#endif
#ifndef bench_op_count
#define bench_op_count 10000000
#endif
#ifndef bench_key_count
#define bench_key_count 100000
#endif
/* Stored record size in bytes including the trailing "\r\n". */
#ifndef bench_record_bytes
#define bench_record_bytes 64
#endif
/* assoc hash table power; must stay above the item lock table power that
 * memcached_thread_init derives from bench_thread_count. */
#ifndef bench_hash_power
#define bench_hash_power 17
#endif
#ifndef bench_max_bytes
#define bench_max_bytes (2ULL * 1024 * 1024 * 1024)
#endif

typedef struct {
    pthread_t thread_id;
    int worker_id;
    uint64_t checksum;
    uint64_t misses;
    uint64_t alloc_fails;
} bench_worker;

static uint64_t next_random(uint64_t *state)
{
    *state ^= *state >> 12;
    *state ^= *state << 25;
    *state ^= *state >> 27;
    return *state * UINT64_C(0x2545f4914f6cdd1d);
}

static uint64_t get_time_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000;
}

/*
 * Claims the worker slot that memcached_thread_init prepared for this thread
 * and runs a mixed get/set loop. The stub conn only needs the fields the
 * engine reads: c->thread (per-thread stats, logger, LRU bump buffer) and
 * c->sfd (logger output only).
 */
static void *run_bench_worker(void *ptr)
{
    bench_worker *worker = ptr;
    LIBEVENT_THREAD *me = get_worker_thread(worker->worker_id);

    /* worker_libevent() sets these when the server owns the threads; take
     * over when the in-process build skipped it. */
    if (me->l == NULL) {
        me->l = logger_create();
    }
    if (me->lru_bump_buf == NULL) {
        me->lru_bump_buf = item_lru_bump_buf_create();
    }
    if (me->l == NULL || me->lru_bump_buf == NULL) {
        abort();
    }

    conn c;
    memset(&c, 0, sizeof(c));
    c.thread = me;
    c.sfd = -1;

    char key[32];
    uint64_t state = (uint64_t)worker->worker_id + 1;
    uint64_t checksum = 0;
    uint64_t misses = 0;
    uint64_t alloc_fails = 0;

    for (uint64_t op = (uint64_t)worker->worker_id; op < bench_op_count;
         op += bench_thread_count) {
        uint64_t key_id = next_random(&state) % bench_key_count;
        int key_len = snprintf(key, sizeof(key), "key%" PRIu64, key_id);
        if (next_random(&state) & 1) {
            item *it = item_get(key, (size_t)key_len, &c, DO_UPDATE);
            if (it != NULL) {
                checksum += (uint64_t)it->nbytes;
                item_remove(it);
            } else {
                misses++;
            }
        } else {
            item *it = item_alloc(key, (size_t)key_len, 0, 0,
                                  bench_record_bytes);
            if (it == NULL) {
                alloc_fails++;
                continue;
            }
            memset(ITEM_data(it), 'a', bench_record_bytes - 2);
            memcpy(ITEM_data(it) + bench_record_bytes - 2, "\r\n", 2);
            store_item(it, NREAD_SET, &c);
            /* The caller releases the alloc reference for every result,
             * matching complete_nread_ascii(). */
            item_remove(it);
        }
    }

    worker->checksum = checksum;
    worker->misses = misses;
    worker->alloc_fails = alloc_fails;
    return NULL;
}

/* Mirrors the init sequence of main() in memcached.c minus sockets, protocol
 * parsing, and the optional background maintenance threads. */
static void init_engine(void)
{
    settings.use_cas = true;
    settings.lru_segmented = true;
    settings.factor = 1.25;
    settings.chunk_size = 48;
    settings.item_size_max = 1024 * 1024;
    settings.maxbytes = bench_max_bytes;
    settings.num_threads = bench_thread_count; /* sizes the item lock table */
    settings.verbose = 0;

    hash_init(MURMUR3_HASH);
    logger_init();
    assoc_init(bench_hash_power);
    slabs_init(settings.maxbytes, settings.factor, false, NULL, NULL, false);
    memcached_thread_init(settings.num_threads, NULL);

    /* current_time is normally advanced by the server clock handler once per
     * second; a constant is enough because nothing here expires. */
    process_started = time(NULL) - 3;
    current_time = 2;
}

int bench_run(void)
{
    bench_worker workers[bench_thread_count];

    init_engine();

    uint64_t start_ms = get_time_ms();
    for (int i = 0; i < bench_thread_count; i++) {
        workers[i].worker_id = i;
        workers[i].checksum = 0;
        workers[i].misses = 0;
        workers[i].alloc_fails = 0;
        if (pthread_create(&workers[i].thread_id, NULL, run_bench_worker,
                           &workers[i]) != 0) {
            fprintf(stderr, "bench: cannot create worker %d\n", i);
            return EXIT_FAILURE;
        }
    }

    uint64_t checksum = 0;
    uint64_t misses = 0;
    uint64_t alloc_fails = 0;
    for (int i = 0; i < bench_thread_count; i++) {
        pthread_join(workers[i].thread_id, NULL);
        checksum += workers[i].checksum;
        misses += workers[i].misses;
        alloc_fails += workers[i].alloc_fails;
    }
    uint64_t elapsed_ms = get_time_ms() - start_ms;
    if (elapsed_ms == 0) {
        elapsed_ms = 1;
    }

    printf("threads=%d ops=%" PRIu64 " ms=%" PRIu64 " ops_per_sec=%.0f "
           "checksum=%" PRIu64 " misses=%" PRIu64 " alloc_fails=%" PRIu64 "\n",
           bench_thread_count, (uint64_t)bench_op_count, elapsed_ms,
           (double)bench_op_count * 1000.0 / (double)elapsed_ms,
           checksum, misses, alloc_fails);
    return EXIT_SUCCESS;
}

int main(void)
{
    return bench_run();
}
