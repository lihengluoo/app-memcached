#include "bench.h"

#include "memcached.h"
#include "ycsb.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Build note: this file provides the only main() and must be linked with the
 * engine objects, ycsb.o (-lm), and libevent_shim/event.o while the server
 * main() in memcached.c is compiled out. The engine objects are compiled with
 * -Ilibevent_shim so the bundled no-op event API replaces libevent: the
 * benchmark configuration skips libevent detection, and the bench compiles
 * and links against POSIX pthreads only
 * (link with -pthread -lm, no -levent). Knobs are overridable from the build
 * command line.
 */
static uint64_t get_time_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000;
}

/*
 * Claims the worker slot that memcached_thread_init prepared for this thread.
 * Initialize the LRU bump buffer here because worker_libevent() is skipped.
 * The guard makes repeated claims from population and replay idempotent.
 */
static LIBEVENT_THREAD *claim_worker_thread(int worker_id)
{
    LIBEVENT_THREAD *me = get_worker_thread(worker_id);

    if (me->lru_bump_buf == NULL) {
        me->lru_bump_buf = item_lru_bump_buf_create();
    }
    if (me->lru_bump_buf == NULL) {
        abort();
    }
    return me;
}

/*
 * The stub conn supplies per-thread stats and the LRU bump buffer without a
 * real socket. Keep c->sfd invalid.
 */
static void init_stub_conn(conn *c, LIBEVENT_THREAD *me)
{
    memset(c, 0, sizeof(*c));
    c->thread = me;
    c->sfd = -1;
}

/* Renders "key" plus the decimal record id into dst and returns the key
 * length. Digits are produced low to end, then copied back to front. */
static size_t format_record_key(char *dst, uint64_t record_id)
{
    memcpy(dst, "key", 3);
    char digits[20];
    size_t count = 0;
    do {
        digits[count] = (char)('0' + record_id % 10);
        record_id /= 10;
        count++;
    } while (record_id > 0);
    size_t length = 3;
    while (count > 0) {
        count--;
        dst[length] = digits[count];
        length++;
    }
    dst[length] = '\0';
    return length;
}

/* Pre-renders every record key; runs once before population and timing. */
static void build_record_keys(bench_record_key *keys)
{
    for (uint64_t record_id = 0; record_id < bench_record_count; record_id++) {
        keys[record_id].length = format_record_key(keys[record_id].text,
                                                   record_id);
    }
}

/* Stores one full record; false means the allocator refused the item. */
static bool store_record(const char *key_text, size_t key_len, conn *c)
{
    /* item_alloc's key parameter is not const, but the engine only copies
     * the key bytes. */
    item *it = item_alloc((char *)key_text, key_len, 0, 0, bench_record_bytes);
    if (it == NULL) {
        return false;
    }
    memset(ITEM_data(it), 'a', bench_record_bytes - 2);
    memcpy(ITEM_data(it) + bench_record_bytes - 2, "\r\n", 2);
    store_item(it, NREAD_SET, c);
    /* The caller releases the alloc reference for every result, matching
     * complete_nread_ascii(). */
    item_remove(it);
    return true;
}

/* YCSB load phase: fills every record through worker slot zero so READs
 * start warm; excluded from the timed replay. */
static bool populate_records(const bench_record_key *keys)
{
    LIBEVENT_THREAD *me = claim_worker_thread(0);

    conn c;
    init_stub_conn(&c, me);

    for (uint64_t record_id = 0; record_id < bench_record_count; record_id++) {
        const bench_record_key *entry = &keys[record_id];
        if (!store_record(entry->text, entry->length, &c)) {
            fprintf(stderr, "bench: cannot allocate record %" PRIu64 "\n",
                    record_id);
            return false;
        }
    }
    return true;
}

/*
 * Replays this worker's round-robin share of the operation array. Each entry
 * executes exactly once overall while threads still contend for the same
 * records.
 */
static void *run_bench_worker(void *ptr)
{
    bench_worker *worker = ptr;
    LIBEVENT_THREAD *me = claim_worker_thread(worker->worker_id);

    conn c;
    init_stub_conn(&c, me);

    const bench_record_key *keys = worker->keys;
    const ycsb_workload *workload = worker->workload;
    uint64_t checksum = 0;
    uint64_t misses = 0;
    uint64_t alloc_fails = 0;

    for (size_t op_id = (size_t)worker->worker_id;
         op_id < workload->operation_count; op_id += bench_thread_count) {
        const ycsb_operation *operation = &workload->operations[op_id];
        const bench_record_key *entry = &keys[operation->key];
        if (operation->is_read) {
            item *it = item_get(entry->text, entry->length, &c, DO_UPDATE);
            if (it != NULL) {
                checksum += (uint64_t)it->nbytes;
                item_remove(it);
            } else {
                misses++;
            }
        } else if (!store_record(entry->text, entry->length, &c)) {
            alloc_fails++;
        }
    }

    /* Publish once, avoiding writes to adjacent worker structs during replay. */
    worker->checksum = checksum;
    worker->misses = misses;
    worker->alloc_fails = alloc_fails;
    return NULL;
}

/* Mirrors the init sequence of main() in memcached.c minus sockets, protocol
 * parsing, and the optional background maintenance and logger threads. */
static void init_engine(void)
{
    settings.use_cas = true;
    settings.lru_segmented = true;
    settings.factor = 1.25;
    settings.chunk_size = 48;
    settings.item_size_max = 1024 * 1024;
    /* slabs_init divides by these; the server defaults come from
     * settings_init() in memcached.c, which the in-process build skips. */
    settings.slab_page_size = 1024 * 1024;
    settings.slab_chunk_size_max = settings.slab_page_size / 2;
    settings.maxbytes = bench_max_bytes;
    settings.num_threads = bench_thread_count; /* sizes the item lock table */
    settings.verbose = 0;

    hash_init(MURMUR3_HASH);
    assoc_init(bench_hash_power);
    slabs_init(settings.maxbytes, settings.factor, false, NULL, NULL, false);
    memcached_thread_init(settings.num_threads, NULL);

    /* current_time is normally advanced by the server clock handler once per
     * second; a constant is enough because nothing here expires. */
    process_started = time(NULL) - 3;
    current_time = 2;
}

static void print_workload(const ycsb_options *options,
                           const ycsb_workload *workload)
{
    fprintf(stderr,
            "records=%" PRIu64 " operations=%zu reads=%zu updates=%zu"
            " read_percent=%u distribution=%s theta=%.17g rng_state=%" PRIu64 "\n",
            options->record_count, workload->operation_count,
            workload->read_count, workload->operation_count - workload->read_count,
            options->read_percent,
            options->distribution == distribution_zipfian ? "zipfian" : "uniform",
            options->theta, options->rng_state);
}

int bench_run(void)
{
    bench_worker workers[bench_thread_count];
    /* Static because worker threads still read the table during the join. */
    static bench_record_key record_keys[bench_record_count];

    init_engine();

    ycsb_options options = {
        .record_count = bench_record_count,
        .operation_count = bench_op_count,
        .rng_state = bench_rng_state,
        .read_percent = bench_read_percent,
        .distribution = bench_key_distribution,
        .theta = bench_theta,
    };
    ycsb_workload workload = {0};
    if (!generate_ycsb_workload(&options, &workload)) {
        fprintf(stderr, "bench: invalid workload knobs or allocation failure\n");
        return EXIT_FAILURE;
    }
    print_workload(&options, &workload);

    build_record_keys(record_keys);

    if (!populate_records(record_keys)) {
        free_ycsb_workload(&workload);
        return EXIT_FAILURE;
    }

    uint64_t start_ms = get_time_ms();
    for (int i = 0; i < bench_thread_count; i++) {
        workers[i].worker_id = i;
        workers[i].keys = record_keys;
        workers[i].workload = &workload;
        workers[i].checksum = 0;
        workers[i].misses = 0;
        workers[i].alloc_fails = 0;
        if (pthread_create(&workers[i].thread_id, NULL, run_bench_worker,
                           &workers[i]) != 0) {
            /* Already started workers still replay; freeing the workload
             * here would race them, so let process exit release it. */
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

    printf("threads=%d records=%u ops=%zu reads=%zu updates=%zu ms=%" PRIu64
           " ops_per_sec=%.0f checksum=%" PRIu64 " misses=%" PRIu64
           " alloc_fails=%" PRIu64 "\n",
           bench_thread_count, (unsigned)bench_record_count,
           workload.operation_count, workload.read_count,
           workload.operation_count - workload.read_count, elapsed_ms,
           (double)workload.operation_count * 1000.0 / (double)elapsed_ms,
           checksum, misses, alloc_fails);
    free_ycsb_workload(&workload);
    return EXIT_SUCCESS;
}

int main(void)
{
    return bench_run();
}
