#include "common.h"

static LISTEN_SOCKETS api_sockets = {
        .config_section  = CONFIG_SECTION_WEB,
        .default_bind_to = "*",
        .default_port    = API_LISTEN_PORT,
        .backlog         = API_LISTEN_BACKLOG
};

WEB_SERVER_MODE web_server_mode = WEB_SERVER_MODE_MULTI_THREADED;

#ifdef NETDATA_INTERNAL_CHECKS
static void log_allocations(void)
{
#ifdef HAVE_C_MALLINFO
    static int heap = 0, used = 0, mmap = 0;

    struct mallinfo mi;

    mi = mallinfo();
    if(mi.uordblks > used) {
        int clients = 0;
        struct web_client *w;
        for(w = web_clients; w ; w = w->next) clients++;

        info("Allocated memory: used %d KB (+%d B), mmap %d KB (+%d B), heap %d KB (+%d B). %d web clients connected.",
            mi.uordblks / 1024,
            mi.uordblks - used,
            mi.hblkhd / 1024,
            mi.hblkhd - mmap,
            mi.arena / 1024,
            mi.arena - heap,
            clients);

        used = mi.uordblks;
        heap = mi.arena;
        mmap = mi.hblkhd;
    }
#else /* ! HAVE_C_MALLINFO */
    ;
#endif /* ! HAVE_C_MALLINFO */

#ifdef has_jemalloc
    malloc_stats_print(NULL, NULL, NULL);
#endif
}
#endif /* NETDATA_INTERNAL_CHECKS */

// --------------------------------------------------------------------------------------

WEB_SERVER_MODE web_server_mode_id(const char *mode) {
    if(!strcmp(mode, "none"))
        return WEB_SERVER_MODE_NONE;
    else if(!strcmp(mode, "single") || !strcmp(mode, "single-threaded"))
        return WEB_SERVER_MODE_SINGLE_THREADED;
    else // if(!strcmp(mode, "multi") || !strcmp(mode, "multi-threaded"))
        return WEB_SERVER_MODE_MULTI_THREADED;
}

const char *web_server_mode_name(WEB_SERVER_MODE id) {
    switch(id) {
        case WEB_SERVER_MODE_NONE:
            return "none";

        case WEB_SERVER_MODE_SINGLE_THREADED:
            return "single-threaded";

        default:
        case WEB_SERVER_MODE_MULTI_THREADED:
            return "multi-threaded";
    }
}

// --------------------------------------------------------------------------------------

int api_listen_sockets_setup(void) {
    int socks = listen_sockets_setup(&api_sockets);

    if(!socks)
        fatal("LISTENER: Cannot listen on any API socket. Exiting...");

    return socks;
}

// --------------------------------------------------------------------------------------
// the main socket listener

static inline void cleanup_web_clients(void) {
    struct web_client *w;

    for (w = web_clients; w;) {
        if (w->obsolete) {
            debug(D_WEB_CLIENT, "%llu: Removing client.", w->id);
            // pthread_cancel(w->thread);
            // pthread_join(w->thread, NULL);
            w = web_client_free(w);
#ifdef NETDATA_INTERNAL_CHECKS
            log_allocations();
#endif
        }
        else w = w->next;
    }
}

// 1. it accepts new incoming requests on our port
// 2. creates a new web_client for each connection received
// 3. spawns a new pthread to serve the client (this is optimal for keep-alive clients)
// 4. cleans up old web_clients that their pthreads have been exited

#define CLEANUP_EVERY_EVENTS 100

void *socket_listen_main_multi_threaded(void *ptr) {
    struct netdata_static_thread *static_thread = (struct netdata_static_thread *)ptr;

    web_server_mode = WEB_SERVER_MODE_MULTI_THREADED;
    info("Multi-threaded WEB SERVER thread created with task id %d", gettid());

    struct web_client *w;
    int retval, counter = 0;

    if(pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL) != 0)
        error("Cannot set pthread cancel type to DEFERRED.");

    if(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0)
        error("Cannot set pthread cancel state to ENABLE.");

    if(!api_sockets.opened)
        fatal("LISTENER: No sockets to listen to.");

    struct pollfd *fds = callocz(sizeof(struct pollfd), api_sockets.opened);

    size_t i;
    for(i = 0; i < api_sockets.opened ;i++) {
        fds[i].fd = api_sockets.fds[i];
        fds[i].events = POLLIN;
        fds[i].revents = 0;

        info("Listening on '%s'", (api_sockets.fds_names[i])?api_sockets.fds_names[i]:"UNKNOWN");
    }

    int timeout = 10 * 1000;

    for(;;) {
        // debug(D_WEB_CLIENT, "LISTENER: Waiting...");
        retval = poll(fds, api_sockets.opened, timeout);

        if(unlikely(retval == -1)) {
            error("LISTENER: poll() failed.");
            continue;
        }
        else if(unlikely(!retval)) {
            debug(D_WEB_CLIENT, "LISTENER: select() timeout.");
            counter = 0;
            cleanup_web_clients();
            continue;
        }

        for(i = 0 ; i < api_sockets.opened ; i++) {
            short int revents = fds[i].revents;

            // check for new incoming connections
            if(revents & POLLIN || revents & POLLPRI) {
                fds[i].revents = 0;

                w = web_client_create(fds[i].fd);
                if(unlikely(!w)) {
                    // no need for error log - web_client_create already logged the error
                    continue;
                }

                if(pthread_create(&w->thread, NULL, web_client_main, w) != 0) {
                    error("%llu: failed to create new thread for web client.", w->id);
                    w->obsolete = 1;
                }
                else if(pthread_detach(w->thread) != 0) {
                    error("%llu: Cannot request detach of newly created web client thread.", w->id);
                    w->obsolete = 1;
                }
            }
        }

        // cleanup unused clients
        counter++;
        if(counter >= CLEANUP_EVERY_EVENTS) {
            counter = 0;
            cleanup_web_clients();
        }
    }

    debug(D_WEB_CLIENT, "LISTENER: exit!");
    listen_sockets_close(&api_sockets);

    freez(fds);

    static_thread->enabled = 0;
    pthread_exit(NULL);
    return NULL;
}

struct web_client *single_threaded_clients[FD_SETSIZE];

static inline int single_threaded_link_client(struct web_client *w, fd_set *ifds, fd_set *ofds, fd_set *efds, int *max) {
    if(unlikely(w->obsolete || w->dead || (!w->wait_receive && !w->wait_send)))
        return 1;

    if(unlikely(w->ifd < 0 || w->ifd >= FD_SETSIZE || w->ofd < 0 || w->ofd >= FD_SETSIZE)) {
        error("%llu: invalid file descriptor, ifd = %d, ofd = %d (required 0 <= fd < FD_SETSIZE (%d)", w->id, w->ifd, w->ofd, FD_SETSIZE);
        return 1;
    }

    FD_SET(w->ifd, efds);
    if(unlikely(*max < w->ifd)) *max = w->ifd;

    if(unlikely(w->ifd != w->ofd)) {
        if(*max < w->ofd) *max = w->ofd;
        FD_SET(w->ofd, efds);
    }

    if(w->wait_receive) FD_SET(w->ifd, ifds);
    if(w->wait_send)    FD_SET(w->ofd, ofds);

    single_threaded_clients[w->ifd] = w;
    single_threaded_clients[w->ofd] = w;

    return 0;
}

static inline int single_threaded_unlink_client(struct web_client *w, fd_set *ifds, fd_set *ofds, fd_set *efds) {
    FD_CLR(w->ifd, efds);
    if(unlikely(w->ifd != w->ofd)) FD_CLR(w->ofd, efds);

    if(w->wait_receive) FD_CLR(w->ifd, ifds);
    if(w->wait_send)    FD_CLR(w->ofd, ofds);

    single_threaded_clients[w->ifd] = NULL;
    single_threaded_clients[w->ofd] = NULL;

    if(unlikely(w->obsolete || w->dead || (!w->wait_receive && !w->wait_send)))
        return 1;

    return 0;
}

void *socket_listen_main_single_threaded(void *ptr) {
    struct netdata_static_thread *static_thread = (struct netdata_static_thread *)ptr;

    web_server_mode = WEB_SERVER_MODE_SINGLE_THREADED;

    info("Single-threaded WEB SERVER thread created with task id %d", gettid());

    struct web_client *w;
    int retval;

    if(pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL) != 0)
        error("Cannot set pthread cancel type to DEFERRED.");

    if(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0)
        error("Cannot set pthread cancel state to ENABLE.");

    if(!api_sockets.opened)
        fatal("LISTENER: no listen sockets available.");

    size_t i;
    for(i = 0; i < FD_SETSIZE ; i++)
        single_threaded_clients[i] = NULL;

    fd_set ifds, ofds, efds, rifds, rofds, refds;
    FD_ZERO (&ifds);
    FD_ZERO (&ofds);
    FD_ZERO (&efds);
    int fdmax = 0;

    for(i = 0; i < api_sockets.opened ; i++) {
        if (api_sockets.fds[i] < 0 || api_sockets.fds[i] >= FD_SETSIZE)
            fatal("LISTENER: Listen socket %d is not ready, or invalid.", api_sockets.fds[i]);

        info("Listening on '%s'", (api_sockets.fds_names[i])?api_sockets.fds_names[i]:"UNKNOWN");

        FD_SET(api_sockets.fds[i], &ifds);
        FD_SET(api_sockets.fds[i], &efds);
        if(fdmax < api_sockets.fds[i])
            fdmax = api_sockets.fds[i];
    }

    for(;;) {
        debug(D_WEB_CLIENT_ACCESS, "LISTENER: single threaded web server waiting (fdmax = %d)...", fdmax);

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        rifds = ifds;
        rofds = ofds;
        refds = efds;
        retval = select(fdmax+1, &rifds, &rofds, &refds, &tv);

        if(unlikely(retval == -1)) {
            error("LISTENER: select() failed.");
            continue;
        }
        else if(likely(retval)) {
            debug(D_WEB_CLIENT_ACCESS, "LISTENER: got something.");

            for(i = 0; i < api_sockets.opened ; i++) {
                if (FD_ISSET(api_sockets.fds[i], &rifds)) {
                    debug(D_WEB_CLIENT_ACCESS, "LISTENER: new connection.");
                    w = web_client_create(api_sockets.fds[i]);
                    if (single_threaded_link_client(w, &ifds, &ofds, &ifds, &fdmax) != 0) {
                        web_client_free(w);
                    }
                }
            }

            for(i = 0 ; i <= (size_t)fdmax ; i++) {
                if(likely(!FD_ISSET(i, &rifds) && !FD_ISSET(i, &rofds) && !FD_ISSET(i, &refds)))
                    continue;

                w = single_threaded_clients[i];
                if(unlikely(!w))
                    continue;

                if(unlikely(single_threaded_unlink_client(w, &ifds, &ofds, &efds) != 0)) {
                    web_client_free(w);
                    continue;
                }

                if (unlikely(FD_ISSET(w->ifd, &refds) || FD_ISSET(w->ofd, &refds))) {
                    web_client_free(w);
                    continue;
                }

                if (unlikely(w->wait_receive && FD_ISSET(w->ifd, &rifds))) {
                    if (unlikely(web_client_receive(w) < 0)) {
                        web_client_free(w);
                        continue;
                    }

                    if (w->mode != WEB_CLIENT_MODE_FILECOPY) {
                        debug(D_WEB_CLIENT, "%llu: Processing received data.", w->id);
                        web_client_process_request(w);
                    }
                }

                if (unlikely(w->wait_send && FD_ISSET(w->ofd, &rofds))) {
                    if (unlikely(web_client_send(w) < 0)) {
                        debug(D_WEB_CLIENT, "%llu: Cannot send data to client. Closing client.", w->id);
                        web_client_free(w);
                        continue;
                    }
                }

                if(unlikely(single_threaded_link_client(w, &ifds, &ofds, &efds, &fdmax) != 0)) {
                    web_client_free(w);
                }
            }
        }
        else {
            debug(D_WEB_CLIENT_ACCESS, "LISTENER: single threaded web server timeout.");
#ifdef NETDATA_INTERNAL_CHECKS
            log_allocations();
#endif
        }
    }

    debug(D_WEB_CLIENT, "LISTENER: exit!");
    listen_sockets_close(&api_sockets);

    static_thread->enabled = 0;
    pthread_exit(NULL);
    return NULL;
}
