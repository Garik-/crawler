/**
 * @author Gar|k <garik.djan@gmail.com>
 * @copyright (c) 2015, http://c0dedgarik.blogspot.ru/
 * @version 1.0
 */

#include "main.h"

void
free_domain(const domain_t *domain) {   
    
    debug("-- free domain %s",domain->domain);
    
    if (NULL != domain->domain) {
        free(domain->domain);
    }
    
    free(domain);
}

static void
ev_ares_io_handler(EV_P_ ev_io * watcher, int revents) {

    options_t * options = (options_t *) (((char *) watcher) - offsetof(options_t, ares.io));

    ares_socket_t rfd = ARES_SOCKET_BAD, wfd = ARES_SOCKET_BAD;

    if (revents & EV_READ)
        rfd = options->ares.io.fd;
    if (revents & EV_WRITE)
        wfd = options->ares.io.fd;

    ares_process_fd(options->ares.channel, rfd, wfd);

    /*fd_set read_fds, write_fds;
    int nfds;

    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    nfds = ares_fds(options->ares.channel, &read_fds, &write_fds);
    if (nfds == 0) {
        return;
    }

    ares_process(options->ares.channel, &read_fds, &write_fds); */
}

static void
ev_ares_timeout_handler(struct ev_loop *loop, struct ev_timer *watcher, int events) {

    options_t * options = (options_t *) (((char *) watcher) - offsetof(options_t, ares.tw));

    debug("ev_ares_timeout_handler");

    ev_timer_set(&options->ares.tw, MAXDNSTIME, 0);
    ev_timer_start(options->loop, &options->ares.tw);

    /**
     ares_process.c
     * 
     void ares_process_timeouts(ares_channel channel) {
        struct timeval now = ares__tvnow();
        process_timeouts(channel, &now);
        process_broken_connections(channel, &now);
    }
     */ 

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

void
ev_ares_dns_callback(void *arg, int status, int timeouts, struct hostent *host) {

    domain_t *domain = (domain_t *) arg;

    if (!host || status != ARES_SUCCESS) {
        debug("- failed to lookup %s\n", ares_strerror(status));
        __sync_fetch_and_add(&domain->options->counters.dnsnotfound, 1);

        free_domain(domain);
        return;
    }

    debug("- found address name %s\n", host->h_name);
    __sync_fetch_and_add(&domain->options->counters.dnsfound, 1);

    //free_domain(domain);
    
    bzero(&domain->servaddr, sizeof(domain->servaddr));
    memcpy(&domain->servaddr.sin_addr, host->h_addr_list[0], host->h_length);
    domain->servaddr.sin_family = AF_INET;
    domain->servaddr.sin_port = htons(80);

    if (http_client(domain) < 0) {
        err_ret("http_client");
    }
}

int
ev_ares_init_options(options_t *options) {
    options->ares.options.sock_state_cb_data = options;
    options->ares.options.sock_state_cb = ev_ares_sock_state_callback;
    options->ares.options.flags = ARES_FLAG_NOCHECKRESP;

    ev_init(&options->ares.io, ev_ares_io_handler);
    ev_timer_init(&options->ares.tw, ev_ares_timeout_handler, options->timeout, 0);

    return ares_init_options(&options->ares.channel, &options->ares.options, ARES_OPT_SOCK_STATE_CB | ARES_OPT_FLAGS);
}

void
ev_ares_gethostbyname(options_t * options, const char *name) {

    domain_t * domain = (domain_t *) malloc(sizeof (domain_t));
    if (NULL == domain) {
        err_ret("ev_ares_gethostbyname");
        return;
    }

    domain->options = options;
    domain->domain = strdup(name);
    domain->index_search = 0;

    __sync_fetch_and_add(&options->counters.domains, 1);

    ares_gethostbyname(options->ares.channel, domain->domain, AF_INET, ev_ares_dns_callback, (void *) domain);
}