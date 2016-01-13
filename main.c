/**
 * @author Gar|k <garik.djan@gmail.com>
 * @copyright (c) 2015, http://c0dedgarik.blogspot.ru/
 * @version 1.0
 */

#include <ev.h>
#include <ares.h>

#include <ares.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/mman.h>
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


#include "picohttpparser.h"

#define EVARES_MAXIO 8
#define MAXLINE  4096 /* максимальная длина текстовой строки */
#define MAXDNSTIME 5. // in seconds
#define MAXPENDING 10

#define debug(fmt, ...)   do{ \
  fprintf(stderr, "[DEBUG] %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, fmt, ##__VA_ARGS__); \
        if (fmt[strlen(fmt) - 1] != 0x0a) { fprintf(stderr, "\n"); } \
        } while(0)

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

void
err_quit(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    err_doit(0, LOG_ERR, fmt, ap);
    va_end(ap);
    exit(EXIT_FAILURE);
}

typedef struct {
    struct ev_loop * loop;

    struct {
        struct ev_io io;
        struct ev_timer tw;
        ares_channel channel;
        struct ares_options options;

    } ares;
} options_t;

typedef struct {
    options_t * options;

    const char *domain;
    size_t domain_len;

    struct ev_io io;
    struct ev_timer tw;
} domain_t;

static void
ev_ares_io_handler(EV_P_ ev_io * watcher, int revents) {

    options_t * options = (options_t *) (((char *) watcher) - offsetof(options_t, ares.io));


    fd_set read_fds, write_fds;
    int nfds;

    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    nfds = ares_fds(options->ares.channel, &read_fds, &write_fds);
    if (nfds == 0) {
        return;
    }

    ares_process(options->ares.channel, &read_fds, &write_fds);

}

static void
ev_ares_timeout_handler(struct ev_loop *loop, struct ev_timer *watcher, int events) {

    options_t * options = (options_t *) (((char *) watcher) - offsetof(options_t, ares.tw));

    debug("ev_ares_timeout_handler");

    ev_timer_set(&options->ares.tw, MAXDNSTIME, 0);
    ev_timer_start(options->loop, &options->ares.tw);
    
    ares_process_timeouts(options->ares.channel);
    
    errno = ETIMEDOUT;
}

static void
ev_ares_sock_state_callback(void *data, int s, int read, int write) {
    options_t * options = (options_t *) data;

    debug("ev_ares_sock_state_callback %d  [%d.%d]", s, read, write);

    //if (ev_is_active(&options->ares.io) && options->ares.io.fd != s) return;

    ev_io_stop(options->loop, &options->ares.io);
    ev_timer_stop(options->loop, &options->ares.tw);


    if (read || write) {
        ev_io_set(&options->ares.io, s, (read ? EV_READ : 0) | (write ? EV_WRITE : 0));
        ev_timer_set(&options->ares.tw, MAXDNSTIME, 0);

        ev_io_start(options->loop, &options->ares.io);
        ev_timer_start(options->loop, &options->ares.tw);
    }
}

static char *
parse_csv(char *line, size_t len, ptrdiff_t * line_size) {
    char * end = memchr(line, '\n', len);
    *line_size = 0;

    if (NULL != end) {
        char * separator = memchr(line, ';', end - line);
        if (NULL != separator) {

            *separator = 0;
            *line_size = separator - line;

            return ++end;
        }
    }

    return NULL;
}

static void
ev_ares_dns_callback(void *arg, int status, int timeouts, struct hostent *host) {
    //debug("arg: %p", arg);
    domain_t *domain = (domain_t *) arg;

    if (!host || status != ARES_SUCCESS) {
        debug("- failed to lookup %s\n", ares_strerror(status));
        return;
    }

    debug("- found address name %s\n", host->h_name);

    /*if (http_client(data->eares, data->domain, host) < 0) {
        err_ret("http_client");
    }*/
}

int
main(void) {
    int status;
    options_t options;
    domain_t domains[MAXPENDING];
    const size_t page_size = (size_t) sysconf(_SC_PAGESIZE);
    size_t pending_requests = 0;

    bzero(&options, sizeof (options_t));



    options.loop = EV_DEFAULT;


    if (ARES_SUCCESS == (status = ares_library_init(ARES_LIB_INIT_ALL))) {

        options.ares.options.sock_state_cb_data = &options;
        options.ares.options.sock_state_cb = ev_ares_sock_state_callback;
        options.ares.options.flags = ARES_FLAG_NOCHECKRESP;

        ev_init(&options.ares.io, ev_ares_io_handler);
        ev_timer_init(&options.ares.tw, ev_ares_timeout_handler, MAXDNSTIME, 0);


        if (ARES_SUCCESS == (status = ares_init_options(&options.ares.channel, &options.ares.options, ARES_OPT_SOCK_STATE_CB | ARES_OPT_FLAGS))) {

            int fd;
            if ((fd = open("ru_domains.txt", O_RDWR)) > 0) {

                struct stat statbuf;

                if (fstat(fd, &statbuf) < 0 && S_ISREG(statbuf.st_mode)) {
                    err_ret("fstat");
                    status = EXIT_FAILURE;
                } else {
                    off_t offset = 0;

                    while (offset < statbuf.st_size) { // file mapping loop
                        char * buffer = (char *) mmap(0, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, offset);
                        if (MAP_FAILED != buffer) {


                            char * line, *end_line;
                            line = buffer;
                            ptrdiff_t line_size = 0;


                            while (NULL != (end_line = parse_csv(line, page_size - (line - buffer), &line_size))) {

                                if (line_size > 0 && (line[line_size - 1] == 'U' || line[line_size - 1] == 'u')) {

                                    domains[pending_requests].options = &options;
                                    domains[pending_requests].domain = line;
                                    domains[pending_requests].domain_len = line_size;

                                    ares_gethostbyname(options.ares.channel, line, AF_INET, ev_ares_dns_callback, (void *) &domains[pending_requests]);

                                    ++pending_requests;
                                }

                                line = end_line;

                                if (pending_requests == MAXPENDING) {


                                    ev_run(options.loop, 0);
                                    pending_requests = 0;

                                }


                            }

                            munmap(buffer, page_size);
                        }
                        offset += page_size;
                    }
                }

                close(fd);
            } else {
                err_ret("ru_domains.txt");
                status = EXIT_FAILURE;
            }


        } else {
            err_ret("Ares error: %s", ares_strerror(status));
            status = EXIT_FAILURE;
        }

        ares_library_cleanup();
    } else {
        err_quit("Ares error: %s", ares_strerror(status));
    }

    return status;
}