/**
 * @author Gar|k <garik.djan@gmail.com>
 * @copyright (c) 2015, http://c0dedgarik.blogspot.ru/
 * @version 1.0
 */

#ifndef MAIN_H
#define MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ev.h>
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
#include <syslog.h>  /* for syslog() */
#include <fcntl.h> /* для неблокируемых сокетов */
#include <assert.h>
#include <errno.h>
#include <sys/time.h>
#include <pthread.h>


#define MAXLINE  4096 /* максимальная длина текстовой строки */
#define MAXDNSTIME 5. // in seconds
#define MAXPENDING 10
    
#define MAXCONTIME 3.0 // in seconds
#define MAXRECVTIME 3.0 // in seconds

#define DEBUG

#ifdef DEBUG
#define debug(fmt, ...)   do{ \
  fprintf(stderr, "[DEBUG] %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, fmt, ##__VA_ARGS__); \
        if (fmt[strlen(fmt) - 1] != 0x0a) { fprintf(stderr, "\n"); } \
        } while(0)
#else
#define debug(fmt, ...) do {} while(0);
#endif

    typedef struct {
        struct ev_loop * loop;

        struct {
            struct ev_io io;
            struct ev_timer tw;
            ares_channel channel;
            struct ares_options options;

        } ares;

        struct {
            ssize_t domains;
            ssize_t dnsfound;
            ssize_t dnsnotfound;
        } counters;

        struct {
            int fd;
            int out;
            size_t len;
        } file;

        int timeout;
        int pending_requests;

    } options_t;

    typedef struct {
        //size_t domain_len;

        struct ev_io io;
        struct ev_timer tw;
        options_t * options;
        char *domain;
        unsigned int keep_alive;
        unsigned int index_search;
    } domain_t;

    int
    ev_ares_init_options(options_t *options);

    void
    ev_ares_gethostbyname(options_t * options, const char *name);

    void
    err_ret(const char *fmt, ...);

    void
    err_quit(const char *fmt, ...);

    void
    err_sys(const char *fmt, ...);
    
    int
    http_client(domain_t * domain, struct hostent *host);
    
    void
    free_domain(const domain_t *domain);
    
    void
    ev_ares_dns_callback(void *arg, int status, int timeouts, struct hostent *host);


#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */

