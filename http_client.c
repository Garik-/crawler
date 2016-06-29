/**
 * @author Gar|k <garik.djan@gmail.com>
 * @copyright (c) 2015, http://c0dedgarik.blogspot.ru/
 * @version 1.0
 */

#include "main.h"
#include "picohttpparser.h"

char http_get[] = "GET %s HTTP/1.1\r\n\
Host: %s\r\n\
Connection: close\r\n\
Accept: text/plain,text/html;q=0.9\r\n\
User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/47.0.2526.106 Safari/537.36\r\n\
\r\n";

char search_path[][256]={"/includes/init.php","/modules/clauses/clauses.sitemap.php?start=1","/plugins/kcaptcha/kcaptcha.php?kcaptcha=1"};

static void write_callback(struct ev_loop *loop, domain_t * domain);

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
success_checked(const domain_t *domain) {
    char buffer[MAXLINE];
    size_t len;
    
    debug("%s FOUND\n", domain->domain);
    __sync_fetch_and_add(&domain->options->counters.cmsfound, 1);
    
    len = snprintf(buffer,sizeof(buffer),
            "%s%s;\n", domain->domain, search_path[domain->index_search]);
    
    writen(domain->options->file.out, buffer, len);
}

static void
recv_handler(struct ev_loop *loop, struct ev_io *watcher, int events) {
    //debug("recv_handler");

    domain_t *domain = (domain_t *) watcher;
    ev_io_stop(loop, &domain->io);
    ev_timer_stop(loop, &domain->tw);

    char buf[MAXLINE], *msg;
    int pret = -1, minor_version, status, i;
    size_t msg_len, num_headers, buflen = 0, prevbuflen = 0;
    struct phr_header headers[100];
    ssize_t rret;

    while (1) {
        rret = readn(domain->io.fd, buf + buflen, sizeof (buf) - buflen);
        if (rret <= 0) {
            err_ret("recv socket %s", domain->domain);
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
                        follow_location(domain, headers[i].value, headers[i].value_len);
                        return;
                        //break;
                    }
                }
                break;
            }

            case 200:
            {
                if (NULL != my_memmem(&buf[pret], buflen - pret, "diafan", 6) || NULL != my_memmem(&buf[pret], buflen - pret, "DIAFAN", 6)) {
                    success_checked(domain);
                }
                else {
  
                    if(++domain->index_search < (sizeof(search_path)/sizeof(search_path[0]))) {
                        //ares_gethostbyname(domain->options->ares.channel, domain->domain, AF_INET, ev_ares_dns_callback, (void *) domain);
                        http_client(domain);
                        return;
                    }
                }
                break;
            }


        }
    }

    debug("-- %s checked", domain->domain);

    free_domain(domain);
}

static void
timeout_handler(struct ev_loop *loop, struct ev_timer *watcher, int events);

static void write_callback(struct ev_loop *loop, domain_t * domain) {
    
    debug("write_callback %s, search = %s", domain->domain, search_path[domain->index_search]);
    
    char buf[MAXLINE];

    ev_io_set(&domain->io, domain->io.fd, EV_READ);
    ev_set_cb(&domain->io, recv_handler);

    ev_timer_set(&domain->tw, MAXRECVTIME, 0);

    
    size_t len = snprintf(buf, sizeof (buf), http_get, search_path[domain->index_search],domain->domain); /* this is safe */
    
    

    if (len == writen(domain->io.fd, buf, len)) {
        ev_io_start(loop, &domain->io);
        ev_timer_start(loop, &domain->tw);

    } else {
        free_domain(domain);
    }
}

static void
connect_handler(struct ev_loop *loop, struct ev_io *watcher, int events) {
    // socket is connected: kill the associated timer and proceed

    //debug("connect_handler");



    domain_t * domain = (domain_t *) watcher;
    ev_io_stop(loop, &domain->io);
    ev_timer_stop(loop, &domain->tw);

    debug("-- connected %s", domain->domain);


    int error;
    socklen_t len = sizeof (error);
    if (getsockopt(domain->io.fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error) {
        free_domain(domain);
        errno = error;

    } else {
        write_callback(loop,domain);
    }

}

static void
timeout_handler(struct ev_loop *loop, struct ev_timer *watcher, int events) {
    // connection timeout occurred: close the associated socket and bail


    domain_t * domain = (domain_t *) (((char *) watcher) - offsetof(domain_t, tw)); // C magic

    debug("- timeout_handler %s", domain->domain);

    ev_io_stop(loop, &domain->io);
    ev_io_set(&domain->io, -1, 0);

    free_domain(domain);
    errno = ETIMEDOUT;
}

int
http_client(domain_t * domain) {

    
    int sockfd = Socket(AF_INET, SOCK_STREAM, 0);
    int n;

    int flags = Fcntl(sockfd, F_GETFL, 0);
    Fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    
    ev_io_init(&domain->io, connect_handler, sockfd, EV_WRITE);
    ev_io_start(domain->options->loop, &domain->io);

    ev_timer_init(&domain->tw, timeout_handler, MAXCONTIME, 0);
    ev_timer_start(domain->options->loop, &domain->tw);

    if ((n = connect(sockfd, (struct sockaddr *) &domain->servaddr, sizeof (domain->servaddr))) < 0)
        if (errno != EINPROGRESS)
            return (-1);

    return 0;
}

inline bool
is_valid_location(const char *location, size_t len) {
    // "http://1000heads.comhttp://1000HEADS.RU/includes/init.php" 
    bool valid = false;

    if (NULL != location) {
        if (len > 0 && NULL != my_memmem(location, len, "init.php", 8)) {

            char *p = NULL;
            if (NULL != (p = my_memmem(location, len, "http://", 7))) {
                p = p + 7;
                if (NULL == my_memmem(p, len - (p - location), "http://", 7)) {
                    if (NULL == my_memmem(p, len - (p - location), "http/", 5)) {
                        valid = true;
                    }
                }
            }
        }
    }

    if(false == valid) {
        debug("- invalid location");
    }
    
    return valid;
} 

void
follow_location(domain_t * domain, const char *location, size_t len) {
    debug("-- follow location %.*s", (int) len, location);
    
    __sync_fetch_and_add(&domain->options->counters.follow, 1);

    char *host;
    size_t host_len;
    
    if(false == is_valid_location(location,len)) return;

    if (phr_parse_host(location, len, &host, &host_len) > 0) {
        host[host_len] = 0;
        free(domain->domain);
        
        domain->domain = strdup(host);
        
        ares_gethostbyname(domain->options->ares.channel, domain->domain, AF_INET, ev_ares_dns_callback, (void *) domain);
    }

}