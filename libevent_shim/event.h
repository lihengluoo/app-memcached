#ifndef LIBEVENT_SHIM_EVENT_H
#define LIBEVENT_SHIM_EVENT_H

/*
 * POSIX-only stand-in for the slice of libevent's classic <event.h> API that
 * the engine references. The in-process bench build compiles the engine with
 * this header on the include path instead of libevent: the driver never runs
 * an event loop, so the shim only records what thread setup stores and answers
 * success. This is not a general libevent replacement; the linked engine
 * objects decide which declarations are needed.
 */

/* libevent's event.h pulled these in transitively; the engine relies on
   that (va_list in logger.h, stderr/snprintf in the protocol files). */
#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>

/* Modern enough for the LIBEVENT_VERSION_NUMBER guards in the engine. */
#define LIBEVENT_VERSION_NUMBER 0x02000101

typedef int evutil_socket_t;
typedef void (*event_callback_fn)(evutil_socket_t fd, short events, void *arg);

#define EV_TIMEOUT 0x01
#define EV_READ 0x02
#define EV_WRITE 0x04
#define EV_PERSIST 0x08

#define EVLOOP_ONCE 0x01

#define EVENT_BASE_FLAG_NOLOCK 0x08

/* Opaque here; completed in event.c. */
struct event_base;
struct event_config;

/* The engine embeds events by value in conn and LIBEVENT_THREAD. */
struct event {
    struct event_base *ev_base;
    evutil_socket_t ev_fd;
    short ev_events;
    event_callback_fn ev_callback;
    void *ev_arg;
};

struct event_base *event_base_new_with_config(struct event_config *cfg);
void event_base_free(struct event_base *base);
int event_base_set(struct event_base *base, struct event *ev);
int event_base_loop(struct event_base *base, int flags);
int event_base_loopexit(struct event_base *base, const struct timeval *timeout);

void event_set(struct event *ev, evutil_socket_t fd, short events,
               event_callback_fn callback, void *arg);
int event_add(struct event *ev, const struct timeval *timeout);
int event_del(struct event *ev);

struct event_config *event_config_new(void);
void event_config_free(struct event_config *cfg);
int event_config_set_flag(struct event_config *cfg, int flag);

const char *event_get_version(void);

/* Classic libevent spelled timers through these aliases. */
#define evtimer_set(ev, cb, arg) event_set((ev), -1, 0, (cb), (arg))
#define evtimer_add(ev, tv) event_add((ev), (tv))
#define evtimer_del(ev) event_del((ev))

#endif /* LIBEVENT_SHIM_EVENT_H */
