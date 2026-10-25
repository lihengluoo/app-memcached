# Memcached

## In-process benchmark

`make benchmark` builds `build/memcached-bench`. Run it directly to replay the
default YCSB workload: 100,000 records and 10,000,000 operations across 16
threads. The driver loads records before timing and uses the engine's normal
item, slab, hash-table, and locking code. It owns worker creation; the server
main, notification threads, and background logger are excluded from this build.
Engine logging hooks are compiled out; logger keys and buffers are not
initialized. The bundled libevent shim supplies setup calls without starting an
event loop, so the benchmark needs pthreads and libm but no installed libevent
development package. `make test` checks that benchmark logging evaluates no
arguments and requires no logger initialization or linkage.

Override workload constants with `CPPFLAGS`, using a separate build directory
when changing them, for example:

```sh
make benchmark build_dir=build/small \
    CPPFLAGS='-Dbench_thread_count=4 -Dbench_record_count=1000 -Dbench_op_count=10000'
build/small/memcached-bench
```

In the Delphi checkout, capture the same application through the shared script:

```sh
../../scripts/build.sh --make-dir . --make-target benchmark \
    --executable wllvm/memcached-bench --name memcached-bench
../../compiler/build/delphi-compiler ../IR/memcached-bench.ll
```

`make ir` also uses the shared script and writes `build/ir/memcached-bench.ll`.
All executables, configuration checks, objects, and IR are generated files.

Memcached is a high performance multithreaded event-based key/value cache
store intended to be used in a distributed system.

See: https://memcached.org/about

A fun story explaining usage: https://memcached.org/tutorial

If you're having trouble, try the wiki: https://memcached.org/wiki

If you're trying to troubleshoot odd behavior or timeouts, see:
https://memcached.org/timeouts

https://memcached.org/ is a good resource in general. Please use the mailing
list to ask questions, github issues aren't seen by everyone!

## Dependencies

* libevent - https://www.monkey.org/~provos/libevent/ (libevent-dev)
* libseccomp (optional, experimental, linux) - enables process restrictions for
  better security. Tested only on x86-64 architectures.
* openssl (optional) - enables TLS support. need relatively up to date
  version.

## Environment

Be warned that the -k (mlockall) option to memcached might be
dangerous when using a large cache. Just make sure the memcached machines
don't swap.  memcached does non-blocking network I/O, but not disk.  (it
should never go to disk, or you've lost the whole point of it)

## Build status

See https://build.memcached.org/ for multi-platform regression testing status.

## Bug reports

Feel free to use the issue tracker on github.

**If you are reporting a security bug** please contact a maintainer privately.
We follow responsible disclosure: we handle reports privately, prepare a
patch, allow notifications to vendor lists. Then we push a fix release and your
bug can be posted publicly with credit in our release notes and commit
history.

## Website

* https://www.memcached.org

## Contributing

See https://github.com/memcached/memcached/wiki/DevelopmentRepos
