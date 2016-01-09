/**
 * @author Gar|k <garik.djan@gmail.com>
 * @copyright (c) 2015, http://c0dedgarik.blogspot.ru/
 * @version 1.0
 */

#include <ev.h>

#include <ares.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>  /* for syslog() */
#include <fcntl.h> /* для неблокируемых сокетов */
#include <assert.h>
#include <pthread.h>

#include "picohttpparser.h"

#define debug(fmt, ...)   do{ \
  fprintf(stderr, "[DEBUG] %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, fmt, ##__VA_ARGS__); \
        if (fmt[strlen(fmt) - 1] != 0x0a) { fprintf(stderr, "\n"); } \
        } while(0)



//#define ctx_by_t(ptr) (ctx*)( (char*)ptr - (int) &( (ctx *)0 )->t )

#define EVARES_MAXIO 8
#define MAXLINE  4096 /* максимальная длина текстовой строки */

#define MAXCONTIME 3.0 // in seconds
#define MAXRECVTIME 3.0 // in seconds
#define MAXDNSTIME 2 // in seconds

char http_get[] = "GET /includes/init.php HTTP/1.1\r\n\
Host: %s\r\n\
Connection: close\r\n\
Accept: text/plain,text/html;q=0.9\r\n\
User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/47.0.2526.106 Safari/537.36\r\n\
\r\n";

typedef struct {
    ev_io io;
    ev_timer tw;
    struct ev_loop * loop;

    struct {
        ares_channel channel;
        struct ares_options options;
    } ares;
    struct timeval timeout;
} ev_ares;

typedef struct {
    ev_io io;
    ev_timer tw;
    ev_ares *eares;
    char * host;
} ev_connect;

static void err_doit(int, int, const char *, va_list);

static void
err_doit(int errnoflag, int level, const char *fmt, va_list ap) {
    int errno_save, n;
    char buf[MAXLINE];

    errno_save = errno; /* value caller might want printed */
#ifdef HAVE_VSNPRINTF
    vsnprintf(buf, sizeof (buf), fmt, ap); /* this is safe */
#else
    vsprintf(buf, fmt, ap); /* this is not safe */
#endif
    n = strlen(buf);
    if (errnoflag)
        snprintf(buf + n, sizeof (buf) - n, "%s:%d - %s", __FILE__, __LINE__, strerror(errno_save));
    strcat(buf, "\n");


    fflush(stdout); /* in case stdout and stderr are the same */
    fputs(buf, stderr);
    fflush(stderr);

    return;
}

/* Nonfatal error related to a system call.
 * Print a message and return. */

void
err_ret(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    err_doit(1, LOG_INFO, fmt, ap);
    va_end(ap);
    return;
}

/* Fatal error related to a system call.
 * Print a message and terminate. */

void
err_sys(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    err_doit(1, LOG_ERR, fmt, ap);
    va_end(ap);
    exit(1);
}

/* Fatal error unrelated to a system call.
 * Print a message and terminate. */

void
err_quit(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    err_doit(0, LOG_ERR, fmt, ap);
    va_end(ap);
    exit(1);
}

int
Socket(int family, int type, int protocol) {
    int n;

    if ((n = socket(family, type, protocol)) < 0)
        err_sys("socket error");
    return (n);
}

int
Fcntl(int fd, int cmd, int arg) {
    int n;

    if ((n = fcntl(fd, cmd, arg)) == -1)
        err_sys("fcntl error");
    return (n);
}

static void 
ev_ares_io_cb(EV_P_ ev_io *w, int revents) {
    //debug("io cb");
    ev_ares * eares = (ev_ares *) w;

    ares_socket_t rfd = ARES_SOCKET_BAD, wfd = ARES_SOCKET_BAD;

    if (revents & EV_READ)
        rfd = w->fd;
    if (revents & EV_WRITE)
        wfd = w->fd;

    ares_process_fd(eares->ares.channel, rfd, wfd);
}

static void 
ev_ares_sock_state_cb(void *data, int s, int read, int write) {
    struct timeval *tvp, tv;
    memset(&tv, 0, sizeof (tv));
    ev_ares * eares = (ev_ares *) data;
    //if( !ev_is_active( &eares->tw ) && (tvp = ares_timeout(eares->ares.channel, &eares->timeout, &tv)) ) {
    if (!ev_is_active(&eares->tw) && (tvp = ares_timeout(eares->ares.channel, NULL, &tv))) {
        double timeout = (double) tvp->tv_sec + (double) tvp->tv_usec / 1.0e6;
        //debug("Set timeout to %0.8lf", timeout);
        if (timeout > 0) {
            //ev_timer_set(&eares->tw,timeout,0.);
            //ev_timer_start()
        }
    }
    //debug("[%p] Change state fd %d read:%d write:%d; max time: %u.%u (%p)", data, s, read, write, tv.tv_sec, tv.tv_usec, tvp);
    if (ev_is_active(&eares->io) && eares->io.fd != s) return;
    if (read || write) {
        ev_io_set(&eares->io, s, (read ? EV_READ : 0) | (write ? EV_WRITE : 0));
        ev_io_start(eares->loop, &eares->io);
    } else {
        ev_io_stop(eares->loop, &eares->io);
        ev_io_set(&eares->io, -1, 0);
    }
}

ssize_t /* Read "n" bytes from a descriptor. */
readn(int fd, void *vptr, size_t n) {
    size_t nleft;
    ssize_t nread;
    char *ptr;

    ptr = vptr;
    nleft = n;
    while (nleft > 0) {
        if ((nread = read(fd, ptr, nleft)) < 0) {
            if (errno == EINTR)
                nread = 0; /* and call read() again */
            else
                return (-1);
        } else if (nread == 0)
            break; /* EOF */

        nleft -= nread;
        ptr += nread;
    }
    return (n - nleft); /* return >= 0 */
}

/* end readn */

ssize_t /* Write "n" bytes to a descriptor. */
writen(int fd, const void *vptr, size_t n) {
    size_t nleft;
    ssize_t nwritten;
    const char *ptr;

    ptr = vptr;
    nleft = n;
    while (nleft > 0) {
        if ((nwritten = write(fd, ptr, nleft)) <= 0) {
            if (errno == EINTR)
                nwritten = 0; /* and call write() again */
            else
                return (-1); /* error */
        }

        nleft -= nwritten;
        ptr += nwritten;
    }
    return (n);
}

static void *
my_memmem(const char *buf, size_t buflen, const char *pattern, size_t len) {
    size_t i, j;
    char *bf = (char *) buf, *pt = (char *) pattern;

    if (len > buflen)
        return (void *) NULL;

    for (i = 0; i <= (buflen - len); ++i) {
        for (j = 0; j < len; ++j) {
            if (pt[j] != bf[i + j])
                break;
        }
        if (j == len)
            return (bf + i);
    }
    return NULL;
}

static void
ev_connect_free(ev_connect * con) {
    if (NULL == con) return;
    
    debug("- remove connection %s", con->host);

    if (con->io.fd > 0)
        close(con->io.fd);

    if (NULL != con->host) {
        free(con->host);
    }

    free(con);
}

static void
follow_location(const ev_connect *con, const char *location, size_t len);

static void
recv_handler(struct ev_loop *loop, struct ev_io *watcher, int events) {
    //debug("recv_handler");

    ev_connect * con = (ev_connect *) watcher;
    ev_io_stop(loop, &con->io);
    ev_timer_stop(loop, &con->tw);
    
    

    char buf[MAXLINE];
    const char *msg;
    int pret = -1, minor_version, status, i;
    size_t msg_len, num_headers, buflen = 0, prevbuflen = 0;
    struct phr_header headers[100];
    ssize_t rret;

    while (1) {
        rret = readn(con->io.fd, buf + buflen, sizeof (buf) - buflen);
        if (rret <= 0) {
            err_ret("recv socket %s", con->host);
            break;
        }
        prevbuflen = buflen;
        buflen += rret;

        num_headers = sizeof (headers) / sizeof (headers[0]);
        pret = phr_parse_response(buf, buflen, &minor_version, &status, &msg, &msg_len, headers, &num_headers, prevbuflen);

        if (pret > 0)
            break; /* successfully parsed the request */

        else if (pret == -1) { // ParseError;
            break;
        }

        assert(pret == -2);
        if (buflen == sizeof (buf)) { // RequestIsTooLongError
            break;
        }
    }

    if (pret > 0) {

        switch (status) {
            case 301:
            case 302:
            {

                for (i = 0; i != num_headers; ++i) {
                    if (NULL != my_memmem(headers[i].name, headers[i].name_len, "Location", 8)) {
                        follow_location(con, headers[i].value, headers[i].value_len);
                        break;
                    }
                }
                break;
            }

            case 200:
            {
                if (NULL != my_memmem(&buf[pret], buflen - pret, "diafan", 6) || NULL != my_memmem(&buf[pret], buflen - pret, "DIAFAN", 6)) {
                    printf("%s FOUND\n", con->host);
                }
                break;
            }


        }

        /*printf("response is %d bytes long\n", pret);
        printf("msg is %.*s\n", (int) msg_len, msg);
        printf("status is %d\n", status);
        printf("HTTP version is 1.%d\n", minor_version);
        printf("headers:\n");
        for (i = 0; i != num_headers; ++i) {
            printf("%.*s: %.*s\n", (int) headers[i].name_len, headers[i].name,
                    (int) headers[i].value_len, headers[i].value);
        }*/
    }

    debug("-- %s checked",con->host);

    ev_connect_free(con);
}

static void
timeout_handler(struct ev_loop *loop, struct ev_timer *watcher, int events);

static void
connected_handler(struct ev_loop *loop, struct ev_io *watcher, int events) {
    // socket is connected: kill the associated timer and proceed

    //debug("connect_handler");

    char buf[MAXLINE];

    ev_connect * con = (ev_connect *) watcher;
    ev_io_stop(loop, &con->io);
    ev_timer_stop(loop, &con->tw);
    
    debug("-- connected %s", con->host);


    int error;
    socklen_t len = sizeof (error);
    if (getsockopt(con->io.fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error) {
        ev_connect_free(con);
        errno = error;

    } else {

        ev_io_set(&con->io, con->io.fd, EV_READ);
        ev_set_cb(&con->io, recv_handler);

        ev_timer_set(&con->tw, MAXRECVTIME, 0);


        snprintf(buf, sizeof (buf), http_get, con->host); /* this is safe */
        size_t len = strlen(buf);

        if (len == writen(con->io.fd, buf, len)) {
            ev_io_start(loop, &con->io);
            ev_timer_start(loop, &con->tw);

        } else {
            ev_connect_free(con);
        }

    }

}

static void
timeout_handler(struct ev_loop *loop, struct ev_timer *watcher, int events) {
    // connection timeout occurred: close the associated socket and bail
    

    ev_connect * con = (ev_connect *) (((char *) watcher) - offsetof(ev_connect, tw)); // C magic
    
    debug("- timeout_handler %s",con->host);

    ev_io_stop(loop, &con->io);
    ev_io_set(&con->io, -1, 0);

    ev_connect_free(con);
    errno = ETIMEDOUT;
}

int
http_client(ev_ares *eares, struct hostent *host) {

    struct sockaddr_in servaddr;
    int sockfd = Socket(AF_INET, SOCK_STREAM, 0);
    int n;

    int flags = Fcntl(sockfd, F_GETFL, 0);
    Fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    bzero(&servaddr, sizeof (servaddr));
    memcpy(&servaddr.sin_addr, host->h_addr_list[0], host->h_length);
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(80);

    ev_connect * con = malloc(sizeof (ev_connect));
    if (NULL == con) {
        err_sys("malloc");
    }
    //debug("con %p", con);
    con->host = strdup(host->h_name);
    con->eares = eares;

    ev_io_init(&con->io, connected_handler, sockfd, EV_WRITE);
    ev_io_start(eares->loop, &con->io);

    ev_timer_init(&con->tw, timeout_handler, MAXCONTIME, 0);
    ev_timer_start(eares->loop, &con->tw);

    if ((n = connect(sockfd, (struct sockaddr *) &servaddr, sizeof (servaddr))) < 0)
        if (errno != EINPROGRESS)
            return (-1);

    return 0;
}

static void
dns_callback(void *arg, int status, int timeouts, struct hostent *host) {
    //debug("arg: %p", arg);
    if (!host || status != ARES_SUCCESS) {
        debug("- failed to lookup %s\n", ares_strerror(status));
        return;
    }

    debug("- found address name %s\n", host->h_name);

    if (http_client((ev_ares *) arg, host) < 0) {
        err_ret("http_client");
    }
}

void
check_domain(const ev_ares * eares, const char *domain) {
    debug("Check domain %s", domain);
    ares_gethostbyname(eares->ares.channel, domain, AF_INET, dns_callback, (void *) eares);
}

static void
follow_location(const ev_connect * con, const char *location, size_t len) {
    debug("-- follow location %.*s", (int) len, location);

    char *host;
    size_t host_len;


    if (phr_parse_host(location, len, (const char **)&host, &host_len) > 0) {

        host[host_len] = 0;
        check_domain(con->eares, host);

    }

}

static void
configure_ares(ev_ares *eares, struct ev_loop *loop) {

    int status;

    eares->loop = loop;
    eares->ares.options.sock_state_cb_data = eares;
    eares->timeout.tv_sec = MAXDNSTIME;
    eares->timeout.tv_usec = 0;

    eares->ares.options.sock_state_cb = ev_ares_sock_state_cb;
    ev_init(&eares->io, ev_ares_io_cb);

    if ((status = ares_init_options(&eares->ares.channel, &eares->ares.options, ARES_OPT_SOCK_STATE_CB)) != ARES_SUCCESS) {
        err_quit("Ares init error: %s", ares_strerror(status));
    }

}

static void *
work_thread(void *vptr_args) {
    
}

int
main(void) {
    struct ev_loop * loop = EV_DEFAULT;

    int status;

    if ((status = ares_library_init(ARES_LIB_INIT_ALL)) != ARES_SUCCESS) {
        err_quit("Ares error: %s", ares_strerror(status));

    }
    
    ev_ares eares;
    configure_ares(&eares, loop);
    
    check_domain(&eares, "diafan.ru");
    check_domain(&eares, "google.com");

    ev_run(loop, 0);


    ares_library_cleanup();

    return 0;
}
