#include "common.h"

// _connect_to()
// protocol    IPPROTO_TCP, IPPROTO_UDP
// socktype    SOCK_STREAM, SOCK_DGRAM
// host        the destination hostname or IP address (IPv4 or IPv6) to connect to
//             if it resolves to many IPs, all are tried (IPv4 and IPv6)
// scope_id    the if_index id of the interface to use for connecting (0 = any)
//             (used only under IPv6)
// service     the service name or port to connect to
// timeout     the timeout for establishing a connection

static inline int _connect_to(int protocol, int socktype, const char *host, uint32_t scope_id, const char *service, struct timeval *timeout) {
    struct addrinfo hints;
    struct addrinfo *ai_head = NULL, *ai = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = PF_UNSPEC;   /* Allow IPv4 or IPv6 */
    hints.ai_socktype = socktype;
    hints.ai_protocol = protocol;

    int ai_err = getaddrinfo(host, service, &hints, &ai_head);
    if (ai_err != 0) {
        error("Cannot resolve host '%s', port '%s': %s", host, service, gai_strerror(ai_err));
        return -1;
    }

    int fd = -1;
    for (ai = ai_head; ai != NULL && fd == -1; ai = ai->ai_next) {

        if (ai->ai_family == PF_INET6) {
            struct sockaddr_in6 *pSadrIn6 = (struct sockaddr_in6 *) ai->ai_addr;
            if(pSadrIn6->sin6_scope_id == 0) {
                pSadrIn6->sin6_scope_id = scope_id;
            }
        }

        char hostBfr[NI_MAXHOST + 1];
        char servBfr[NI_MAXSERV + 1];

        getnameinfo(ai->ai_addr,
                    ai->ai_addrlen,
                    hostBfr,
                    sizeof(hostBfr),
                    servBfr,
                    sizeof(servBfr),
                    NI_NUMERICHOST | NI_NUMERICSERV);

        debug(D_CONNECT_TO, "Address info: host = '%s', service = '%s', ai_flags = 0x%02X, ai_family = %d (PF_INET = %d, PF_INET6 = %d), ai_socktype = %d (SOCK_STREAM = %d, SOCK_DGRAM = %d), ai_protocol = %d (IPPROTO_TCP = %d, IPPROTO_UDP = %d), ai_addrlen = %lu (sockaddr_in = %lu, sockaddr_in6 = %lu)",
              hostBfr,
              servBfr,
              (unsigned int)ai->ai_flags,
              ai->ai_family,
              PF_INET,
              PF_INET6,
              ai->ai_socktype,
              SOCK_STREAM,
              SOCK_DGRAM,
              ai->ai_protocol,
              IPPROTO_TCP,
              IPPROTO_UDP,
              (unsigned long)ai->ai_addrlen,
              (unsigned long)sizeof(struct sockaddr_in),
              (unsigned long)sizeof(struct sockaddr_in6));

        switch (ai->ai_addr->sa_family) {
            case PF_INET: {
                struct sockaddr_in *pSadrIn = (struct sockaddr_in *)ai->ai_addr;
                debug(D_CONNECT_TO, "ai_addr = sin_family: %d (AF_INET = %d, AF_INET6 = %d), sin_addr: '%s', sin_port: '%s'",
                      pSadrIn->sin_family,
                      AF_INET,
                      AF_INET6,
                      hostBfr,
                      servBfr);
                break;
            }

            case PF_INET6: {
                struct sockaddr_in6 *pSadrIn6 = (struct sockaddr_in6 *) ai->ai_addr;
                debug(D_CONNECT_TO,"ai_addr = sin6_family: %d (AF_INET = %d, AF_INET6 = %d), sin6_addr: '%s', sin6_port: '%s', sin6_flowinfo: %u, sin6_scope_id: %u",
                      pSadrIn6->sin6_family,
                      AF_INET,
                      AF_INET6,
                      hostBfr,
                      servBfr,
                      pSadrIn6->sin6_flowinfo,
                      pSadrIn6->sin6_scope_id);
                break;
            }

            default: {
                debug(D_CONNECT_TO, "Unknown protocol family %d.", ai->ai_family);
                continue;
            }
        }

        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if(fd != -1) {
            if(timeout) {
                if(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char *) timeout, sizeof(struct timeval)) < 0)
                    error("Failed to set timeout on the socket to ip '%s' port '%s'", hostBfr, servBfr);
            }

            if(connect(fd, ai->ai_addr, ai->ai_addrlen) < 0) {
                error("Failed to connect to '%s', port '%s'", hostBfr, servBfr);
                close(fd);
                fd = -1;
            }

            debug(D_CONNECT_TO, "Connected to '%s' on port '%s'.", hostBfr, servBfr);
        }
    }

    freeaddrinfo(ai_head);

    return fd;
}

// connect_to()
//
// definition format:
//
//    [PROTOCOL:]IP[%INTERFACE][:PORT]
//
// PROTOCOL  = tcp or udp
// IP        = IPv4 or IPv6 IP or hostname, optionally enclosed in [] (required for IPv6)
// INTERFACE = for IPv6 only, the network interface to use
// PORT      = port number or service name

int connect_to(const char *definition, int default_port, struct timeval *timeout) {
    char buffer[strlen(definition) + 1];
    strcpy(buffer, definition);

    char default_service[10 + 1];
    snprintfz(default_service, 10, "%d", default_port);

    char *host = buffer, *service = default_service, *interface = "";
    int protocol = IPPROTO_TCP, socktype = SOCK_STREAM;
    uint32_t scope_id = 0;

    if(strncmp(host, "tcp:", 4) == 0) {
        host += 4;
        protocol = IPPROTO_TCP;
        socktype = SOCK_STREAM;
    }
    else if(strncmp(host, "udp:", 4) == 0) {
        host += 4;
        protocol = IPPROTO_UDP;
        socktype = SOCK_DGRAM;
    }

    char *e = host;
    if(*e == '[') {
        e = ++host;
        while(*e && *e != ']') e++;
        if(*e == ']') {
            *e = '\0';
            e++;
        }
    }
    else {
        while(*e && *e != ':' && *e != '%') e++;
    }

    if(*e == '%') {
        *e = '\0';
        e++;
        interface = e;
        while(*e && *e != ':') e++;
    }

    if(*e == ':') {
        *e = '\0';
        e++;
        service = e;
    }

    debug(D_CONNECT_TO, "Attempting connection to host = '%s', service = '%s', interface = '%s', protocol = %d (tcp = %d, udp = %d)", host, service, interface, protocol, IPPROTO_TCP, IPPROTO_UDP);

    if(!*host) {
        error("Definition '%s' does not specify a host.", definition);
        return -1;
    }

    if(*interface) {
        scope_id = if_nametoindex(interface);
        if(!scope_id)
            error("Cannot find a network interface named '%s'. Continuing with limiting the network interface", interface);
    }

    if(!*service)
        service = default_service;


    return _connect_to(protocol, socktype, host, scope_id, service, timeout);
}

int connect_to_one_of(const char *destination, int default_port, struct timeval *timeout, size_t *reconnects_counter, char *connected_to, size_t connected_to_size) {
    int sock = -1;

    const char *s = destination;
    while(*s) {
        const char *e = s;

        // skip separators, moving both s(tart) and e(nd)
        while(isspace(*e) || *e == ',') s = ++e;

        // move e(nd) to the first separator
        while(*e && !isspace(*e) && *e != ',') e++;

        // is there anything?
        if(!*s || s == e) break;

        char buf[e - s + 1];
        strncpyz(buf, s, e - s);
        if(reconnects_counter) *reconnects_counter += 1;
        sock = connect_to(buf, default_port, timeout);
        if(sock != -1) {
            if(connected_to && connected_to_size) {
                strncpy(connected_to, buf, connected_to_size);
                connected_to[connected_to_size - 1] = '\0';
            }
            break;
        }
        s = e;
    }

    return sock;
}

ssize_t recv_timeout(int sockfd, void *buf, size_t len, int flags, int timeout) {
    for(;;) {
        struct pollfd fd = {
                .fd = sockfd,
                .events = POLLIN,
                .revents = 0
        };

        errno = 0;
        int retval = poll(&fd, 1, timeout * 1000);

        if(retval == -1) {
            // failed

            if(errno == EINTR || errno == EAGAIN)
                continue;

            return -1;
        }

        if(!retval) {
            // timeout
            return 0;
        }

        if(fd.events & POLLIN) break;
    }

    return recv(sockfd, buf, len, flags);
}

ssize_t send_timeout(int sockfd, void *buf, size_t len, int flags, int timeout) {
    for(;;) {
        struct pollfd fd = {
                .fd = sockfd,
                .events = POLLOUT,
                .revents = 0
        };

        errno = 0;
        int retval = poll(&fd, 1, timeout * 1000);

        if(retval == -1) {
            // failed

            if(errno == EINTR || errno == EAGAIN)
                continue;

            return -1;
        }

        if(!retval) {
            // timeout
            return 0;
        }

        if(fd.events & POLLOUT) break;
    }

    return send(sockfd, buf, len, flags);
}
