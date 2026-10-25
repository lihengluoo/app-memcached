#include "event.h"

/*
 * Every routine is a no-op on purpose: the in-process bench driver never
 * starts an event loop, so nothing here has to dispatch anything. The engine
 * only requires that setup_thread's per-thread registration succeeds and that
 * base pointers stay distinct from NULL.
 */

/* Single instances suffice: callers never dereference or compare them. */
static struct event_base shim_base;
static struct event_config shim_config;

struct event_base *event_base_new_with_config(struct event_config *cfg)
{
    (void)cfg;
    return &shim_base;
}

void event_base_free(struct event_base *base)
{
    (void)base;
}

int event_base_set(struct event_base *base, struct event *ev)
{
    ev->ev_base = base;
    return 0;
}

int event_base_loop(struct event_base *base, int flags)
{
    /* Unreachable for the bench; report "loop ended" rather than loop. */
    (void)base;
    (void)flags;
    return -1;
}

int event_base_loopexit(struct event_base *base, const struct timeval *timeout)
{
    (void)base;
    (void)timeout;
    return 0;
}

void event_set(struct event *ev, evutil_socket_t fd, short events,
               event_callback_fn callback, void *arg)
{
    ev->ev_base = NULL;
    ev->ev_fd = fd;
    ev->ev_events = events;
    ev->ev_callback = callback;
    ev->ev_arg = arg;
}

int event_add(struct event *ev, const struct timeval *timeout)
{
    /* setup_thread aborts on -1 when registering the notify event. */
    (void)ev;
    (void)timeout;
    return 0;
}

int event_del(struct event *ev)
{
    (void)ev;
    return 0;
}

struct event_config *event_config_new(void)
{
    return &shim_config;
}

void event_config_free(struct event_config *cfg)
{
    (void)cfg;
}

int event_config_set_flag(struct event_config *cfg, int flag)
{
    (void)cfg;
    (void)flag;
    return 0;
}

const char *event_get_version(void)
{
    return "libevent_shim";
}
