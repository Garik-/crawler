/**
 * @author Gar|k <garik.djan@gmail.com>
 * @copyright (c) 2015, http://c0dedgarik.blogspot.ru/
 * @version 1.0
 */

#include "main.h"


char http_get[] = "GET %s HTTP/1.1\r\n\
Host: %s\r\n\
Connection: close\r\n\
Accept: text/plain,text/html;q=0.9\r\n\
User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/47.0.2526.106 Safari/537.36\r\n\
\r\n";

char search_path[][256] = {"/includes/init.php", "/modules/clauses/clauses.sitemap.php?start=1", "/plugins/kcaptcha/kcaptcha.php?kcaptcha=1"};

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

    len = snprintf(buffer, sizeof (buffer),
            "%s%s;\n", domain->domain, search_path[domain->index_search]);

    writen(domain->options->file.out, buffer, len);
}

static void
error_parse(const domain_t *domain) {
    char buffer[MAXLINE];
    size_t len;

    debug("error_parse %s", domain->domain);
    __sync_fetch_and_add(&domain->options->counters.error_parse, 1);

    len = snprintf(buffer, sizeof (buffer),
            "%s;\n", domain->domain);

    writen(domain->options->file.parse, buffer, len);
}

static int
parse_response(domain_t *domain) {



    int pret = -1, minor_version, status, i;
    size_t msg_len, buflen = domain->data.len, prevbuflen = 0, num_headers;
    ssize_t rret;
    char *msg;

    num_headers = sizeof (domain->http.headers) / sizeof (domain->http.headers[0]);

    while (1) {
        pret = phr_parse_response(domain->data.buffer, buflen, &minor_version, &status, &msg, &msg_len, domain->http.headers, &num_headers, prevbuflen);

        domain->http.num_headers = num_headers;
        domain->http.status = status;

        if (pret > 0)
            break;

        else if (pret == -1) { // ParseError;
            break;
        }

        assert(pret == -2);
        if (buflen == sizeof (domain->data.buffer)) { // RequestIsTooLongError
            break;
        }
    }

    return pret;
}

static void
recv_handler(struct ev_loop *loop, struct ev_io *watcher, int events) {
    //debug("recv_handler");

    int i;

    domain_t *domain = (domain_t *) watcher;
    ev_io_stop(loop, &domain->io);
    ev_timer_stop(loop, &domain->tw);


    //debug("recv_header %s -- data buffer:%p; data len: %d", domain->domain, domain->data.buffer + domain->data.len, domain->data.len);


    ssize_t len = readn(domain->io.fd, domain->data.buffer + domain->data.len, sizeof (domain->data.buffer) - domain->data.len);

    if (len <= 0) {
        if (EAGAIN == errno) { // сокет занят буфер кончился и прочее
            //err_ret("error read socket %s: ", domain->domain);
            ev_io_start(loop, &domain->io);
            ev_timer_start(loop, &domain->tw);
            return;
        } else { // жесткая ошибка
            err_ret("error read socket %s: ", domain->domain);
            free_domain(domain);
            return;
        }
    } else {

        domain->data.len += len;

        int pret = parse_response(domain);

        debug("parse_response %s:%d", domain->domain,pret);

        if (pret > 0) {

            switch (domain->http.status) {
                case 301:
                case 302:
                {

                    for (i = 0; i != domain->http.num_headers; ++i) {
                        if (NULL != my_memmem(domain->http.headers[i].name, domain->http.headers[i].name_len, "Location", 8)) {
                            follow_location(domain, domain->http.headers[i].value, domain->http.headers[i].value_len);
                            return;
                            //break;
                        }
                    }
                    break;
                }

                case 200:
                {
                    if (NULL != my_memmem(&domain->data.buffer[pret], domain->data.len - pret, "diafan", 6) || NULL != my_memmem(&domain->data.buffer[pret], domain->data.len - pret, "DIAFAN", 6)) {
                        success_checked(domain);
                    } else {

                        if (++domain->index_search < (sizeof (search_path) / sizeof (search_path[0]))) {
                            //ares_gethostbyname(domain->options->ares.channel, domain->domain, AF_INET, ev_ares_dns_callback, (void *) domain);
                            http_client(domain);
                            return;
                        }
                    }
                    break;
                }


            }
        } else {
            error_parse(domain);
        }
    }


    debug("-- %s %d checked", domain->domain, domain->http.status);
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


    size_t len = snprintf(buf, sizeof (buf), http_get, search_path[domain->index_search], domain->domain); /* this is safe */



    if (len == writen(domain->io.fd, buf, len)) {

        //bzero(domain->data.buffer, sizeof(domain->data.buffer));
        domain->data.len = 0;

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
        write_callback(loop, domain);
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

bool
is_valid_location(const domain_t *domain, const char *location, size_t len) {
    // "http://1000heads.comhttp://1000HEADS.RU/includes/init.php" 
    bool valid = false;
    char buffer[MAXLINE];
    size_t s = 0 ;
   

    if (NULL != location) {
        if (len > 0 && location[len - 1] != '/' && NULL != my_memmem(location, len, ".php", 4)) {

            //debug("-- follow location %.*s", 1, &location[len-1]);

            char *p = NULL;


            if (NULL != (p = my_memmem(location, len, "http://", 7))) {
                p = p + 7;
                if (NULL == my_memmem(p, len - (p - location), "http://", 7)) {
                    if (NULL == my_memmem(p, len - (p - location), "http/", 5)) {
                        if (NULL == my_memmem(p, len - (p - location), ".html", 5)) {
                            valid = true;
                        }
                    }
                }
            }

            if (true == valid) {
                valid = false;
                int i = 0;
                for (i = 0; i < sizeof (search_path) / sizeof (search_path[0]); i++) {
                     s = snprintf(buffer, sizeof (buffer),
                        "%s%s", domain->domain, search_path[i]);
                     
                    //debug("-- %s --", buffer[s-1]);
                     
                     
                    if (NULL != my_memmem(location, len, buffer, s) && location[len-1] == buffer[s-1]) {
                        valid = true;
                        break;
                    }
                }
            }

        }
    }

    if (false == valid) {
        debug("- invalid location");
    }

    return valid;
}

void
follow_location(domain_t * domain, const char *location, size_t len) {
    debug("-- follow location %.*s", (int) len, location);

    __sync_fetch_and_add(&domain->options->counters.follow, 1);

    char *host; char c;
    size_t host_len;



    if (phr_parse_host(location, len, &host, &host_len) > 0) {
        
        free(domain->domain);

        c = host[host_len];
        host[host_len] = 0;
        
        domain->domain = strdup(host);
        
        host[host_len] = c;
        
        if (false == is_valid_location(domain, location, len)) {
            free_domain(domain);
            return;
        }

        ares_gethostbyname(domain->options->ares.channel, domain->domain, AF_INET, ev_ares_dns_callback, (void *) domain);
    }

}