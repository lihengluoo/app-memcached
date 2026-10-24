#include "ycsb.h"

#include <math.h>
#include <stdlib.h>

bool validate_ycsb_options(const ycsb_options *options)
{
    return options->record_count > 0 && options->operation_count > 0 &&
           options->operation_count <= SIZE_MAX / sizeof(ycsb_operation) &&
           options->read_percent <= 100 && isfinite(options->theta) &&
           options->theta >= 0.0 && options->theta < 1.0 &&
           (options->distribution == distribution_uniform ||
            (options->distribution == distribution_zipfian &&
             options->record_count <= zipf_record_limit));
}

static uint64_t next_random(uint64_t *state)
{
    /* SplitMix64: fixed-width arithmetic keeps the stream independent of libc. */
    *state += rng_increment;
    uint64_t bits = *state;
    bits = (bits ^ (bits >> 30)) * rng_multiplier_one;
    bits = (bits ^ (bits >> 27)) * rng_multiplier_two;
    return bits ^ (bits >> 31);
}

static uint64_t pick_uniform_key(uint64_t *state, uint64_t record_count)
{
    /* Reject the incomplete remainder interval to avoid modulo bias. */
    uint64_t remainder = (UINT64_C(0) - record_count) % record_count;
    uint64_t bits;
    do {
        bits = next_random(state);
    } while (bits < remainder);
    return bits % record_count;
}

static zipf_params compute_zipf_params(uint64_t record_count, double theta)
{
    zipf_params params = {0};
    for (uint64_t record_id = 0; record_id < record_count; ++record_id) {
        params.zeta_n += 1.0 / pow((double)(record_id + 1), theta);
    }
    params.zeta_two = 1.0 + pow(0.5, theta);
    params.alpha = 1.0 / (1.0 - theta);
    /* One and two records are handled by the first two sampling branches. */
    if (record_count > 2) {
        params.eta = -expm1((1.0 - theta) * log(2.0 / (double)record_count)) /
                     (1.0 - params.zeta_two / params.zeta_n);
    }
    return params;
}

static uint64_t pick_zipf_key(uint64_t *state, uint64_t record_count,
                             const zipf_params *params)
{
    double fraction = (double)(next_random(state) >> 11) * 0x1p-53;
    double scaled = fraction * params->zeta_n;
    if (scaled < 1.0) {
        return 0;
    } else if (scaled < params->zeta_two) {
        return 1;
    } else {
        /* Gray's Zipfian approximation, also used by Limon's generator. */
        double rank = (double)record_count *
                      exp(params->alpha * log1p(params->eta * (fraction - 1.0)));
        /* Floating-point rounding near the upper endpoint must not emit N. */
        return rank < (double)record_count ? (uint64_t)rank : record_count - 1;
    }
}

static void fill_operations(const ycsb_options *options, ycsb_workload *workload)
{
    bool use_zipf = options->distribution == distribution_zipfian &&
                    options->theta > 0.0;
    zipf_params params = {0};
    if (use_zipf) {
        params = compute_zipf_params(options->record_count, options->theta);
    }
    uint64_t state = options->rng_state;
    for (size_t op_id = 0; op_id < workload->operation_count; ++op_id) {
        uint64_t key = use_zipf ? pick_zipf_key(&state, options->record_count, &params)
                               : pick_uniform_key(&state, options->record_count);
        /* Draw the operation independently of the record count and key. Keep
           both draws even at 0/100 percent so mixes share the same key stream. */
        bool is_read = pick_uniform_key(&state, 100) < options->read_percent;
        workload->operations[op_id] = (ycsb_operation){.key = key, .is_read = is_read};
        workload->read_count += is_read;
    }
}

bool generate_ycsb_workload(const ycsb_options *options, ycsb_workload *workload)
{
    if (validate_ycsb_options(options)) {
        size_t operation_count = (size_t)options->operation_count;
        ycsb_operation *operations = malloc(operation_count * sizeof(*operations));
        if (operations != NULL) {
            ycsb_workload generated = {
                .operations = operations,
                .operation_count = operation_count,
            };
            fill_operations(options, &generated);
            *workload = generated;
            return true;
        }
    }
    return false;
}

void free_ycsb_workload(ycsb_workload *workload)
{
    free(workload->operations);
    *workload = (ycsb_workload){0};
}
