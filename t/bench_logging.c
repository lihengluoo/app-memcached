#include "memcached.h"

int main(void)
{
    unsigned log_arg_count = 0;
    LOGGER_LOG(NULL, LOG_FETCHERS, LOGGER_ITEM_GET, NULL, ++log_arg_count,
               "key", (size_t)3, 64, 1, -1);
    return log_arg_count != 0;
}
