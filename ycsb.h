#ifndef ycsb_h
#define ycsb_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    workload_a_read_percent = 50,
    workload_b_read_percent = 95,
    workload_c_read_percent = 100
};

typedef enum {
    distribution_uniform,
    distribution_zipfian
} key_distribution;

typedef struct {
    uint64_t record_count;
    uint64_t operation_count;
    uint64_t rng_state;
    unsigned read_percent;
    key_distribution distribution;
    double theta;
} ycsb_options;

static const ycsb_options default_ycsb_options = {
    .rng_state = 1,
    .read_percent = workload_a_read_percent,
    .distribution = distribution_zipfian,
    .theta = 0.99,
};

typedef struct {
    uint64_t key;
    bool is_read;
} ycsb_operation;

typedef struct {
    ycsb_operation *operations;
    size_t operation_count;
    size_t read_count;
} ycsb_workload;

/* Double precision can distinguish every integer up to this count. */
static const uint64_t zipf_record_limit = UINT64_C(1) << 53;
static const uint64_t rng_increment = UINT64_C(0x9e3779b97f4a7c15);
static const uint64_t rng_multiplier_one = UINT64_C(0xbf58476d1ce4e5b9);
static const uint64_t rng_multiplier_two = UINT64_C(0x94d049bb133111eb);

typedef struct {
    double zeta_n;
    double zeta_two;
    double alpha;
    double eta;
} zipf_params;

/* Check counts, allocation size, percentage, distribution, and exponent. */
bool validate_ycsb_options(const ycsb_options *options);
/* The workload must be empty. On failure it stays empty. Generation uses
   local random state and leaves options unchanged. No file I/O is performed. */
bool generate_ycsb_workload(const ycsb_options *options, ycsb_workload *workload);
/* All readers must finish before freeing; resets the workload to empty. */
void free_ycsb_workload(ycsb_workload *workload);

#endif
