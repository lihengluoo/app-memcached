CC ?= cc
CFLAGS ?= -O2 -g

root_dir := $(abspath ../..)
src_dir := $(CURDIR)
build_dir ?= build
ir_dir ?= $(build_dir)/ir
config_dir := $(abspath $(build_dir)/config)

# These describe the in-process application for both native and IR builds.
# scripts/build.sh owns the capture flags and normalization passes.
compile_flags := -std=gnu11 -D_GNU_SOURCE -DHAVE_CONFIG_H -DNDEBUG \
                 -Dbench_in_process -I$(config_dir) -I. -Ilibevent_shim -pthread
engine_src := memcached.c hash.c jenkins_hash.c murmur3_hash.c slabs.c items.c \
              assoc.c thread.c daemon.c stats_prefix.c util.c cache.c \
              bipbuffer.c base64.c logger.c crawler.c itoa_ljust.c \
              slab_automove.c authfile.c restart.c proto_text.c proto_bin.c
bench_src := bench.c ycsb.c libevent_shim/event.c $(engine_src)
bench_obj := $(addprefix $(build_dir)/,$(bench_src:.c=.o))
test_obj := $(build_dir)/t/bench_logging.o

.PHONY: all benchmark test ir clean
all: benchmark
benchmark: $(build_dir)/memcached-bench

test: $(build_dir)/bench-logging-test
	$<

$(build_dir)/bench-logging-test: $(test_obj)
	$(CC) $(CFLAGS) -pthread $^ $(LDFLAGS) $(LDLIBS) -o $@

$(build_dir)/memcached-bench: $(bench_obj)
	$(CC) $(CFLAGS) -pthread $^ $(LDFLAGS) $(LDLIBS) -lm -o $@

$(build_dir)/%.o: %.c $(config_dir)/config.h
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(compile_flags) -MMD -MP -c $< -o $@

$(config_dir)/config.h: configure config.h.in
	mkdir -p $(config_dir)
	cd $(config_dir) && WLLVM_CONFIGURE_ONLY=1 "$(src_dir)/configure" \
	    CC="$(CC)" CFLAGS="$(CFLAGS)" --disable-extstore --disable-seccomp \
	    --disable-sasl --disable-tls --disable-proxy --disable-docs --without-libevent

ir:
	"$(root_dir)/scripts/build.sh" --make-dir . --make-target benchmark \
	    --executable wllvm/memcached-bench --name memcached-bench \
	    --output-dir "$(ir_dir)"

clean:
	rm -rf "$(build_dir)"

-include $(bench_obj:.o=.d) $(test_obj:.o=.d)
