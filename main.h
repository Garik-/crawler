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


#define MAXLINE  4096 /* максимальная длина текстовой строки */
#define MAXDNSTIME 5. // in seconds
#define MAXPENDING 10

//#define DEBUG
    
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
    } options_t;

    typedef struct {
        options_t * options;

        const char *domain;
        //size_t domain_len;

        struct ev_io io;
        struct ev_timer tw;
    } domain_t;

    int
    ev_ares_init_options(options_t *options);
    
    void
    ev_ares_gethostbyname(domain_t *domain);
    
    void
    err_ret(const char *fmt, ...);
    
    void
    err_quit(const char *fmt, ...);


#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */

