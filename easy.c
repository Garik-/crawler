/* 
 * File:   easy.c
 * Author: Gar|l
 *
 * Created on 14 Сентябрь 2015 г., 22:47
 *
 * С этого все начиналось. Но программа работает не так быстро и оптимально как хотелось бы
 * это сейчас я узнал про libev, libuv и https://github.com/h2o/picohttpparser
 */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/queue.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/dns.h>
#include <event2/http.h>
#include <event.h>

#define HTTP_PORT 80
#define TIMEOUT 3

struct event_base *base = NULL;
struct evdns_base *dnsbase = NULL;
int n_pending_requests = 0;

struct searchval {
    LIST_ENTRY(searchval) next;
    char * value;
    size_t len;
};

LIST_HEAD(searchl, searchval) search_list;



void create_request(const char *url);

void * my_memmem(const void *buf, size_t buflen, const void *pattern, size_t len) {
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

static int
search_add(struct searchl *search_list,
        const char *value, size_t len) {
    struct searchval *search = calloc(1, sizeof (struct searchval));
    if (search == NULL) {
        //event_warn("%s: calloc", __func__);
        return (-1);
    }
    if ((search->value = strdup(value)) == NULL) {
        free(search);

        return (-1);
    }


    LIST_INSERT_HEAD(search_list, search, next);
    return (0);
}

void
search_clear(struct searchl *search_list) {
    struct searchval *search;

    for (search = LIST_FIRST(search_list);
            search != NULL;
            search = LIST_FIRST(search_list)) {
        LIST_REMOVE(search, next);

        free(search->value);
        free(search);
    }
}

static int
search_find(struct searchl *search_list, const char * buffer, size_t len) {
    struct searchval *search;

    LIST_FOREACH(search, search_list, next) {
        if (NULL != my_memmem(buffer, len, search->value, search->len)) {
            return (0);
        }
    }
    return (-1);
}

static void
http_request_done(struct evhttp_request *req, void *ctx) {
    char buffer[256];
    ev_ssize_t nread;
    struct evkeyval *header;


    if (req == NULL) {
        /* If req is NULL, it means an error occurred, but
         * sadly we are mostly left guessing what the error
         * might have been.  We'll do our best... */
        struct bufferevent *bev = (struct bufferevent *) ctx;


        int errcode = EVUTIL_SOCKET_ERROR();
        fprintf(stderr, "some request failed - no idea which one though!\n");
        /* Print out the OpenSSL error queue that libevent
         * squirreled away for us, if any. */

        /* If the OpenSSL error queue was empty, maybe it was a
         * socket error; let's try printing that. */

        fprintf(stderr, "socket error = %s (%d)\n",
                evutil_socket_error_to_string(errcode),
                errcode);
        return;
    }

    /*fprintf(stderr, "Response line: %d %s\n",
            evhttp_request_get_response_code(req),
            evhttp_request_get_response_code_line(req));

    struct evkeyvalq *headers = evhttp_request_get_input_headers(req);
    if (NULL != headers) {
        fprintf(stderr, "Response headers:\n");

        TAILQ_FOREACH(header, headers, next) {
            fprintf(stderr, "%s: %s\r\n",
                    header->key, header->value);
        }

        fprintf(stderr, "\n");
    }*/


    const int http_code = evhttp_request_get_response_code(req);
    struct evhttp_connection *evcon = evhttp_request_get_connection(req);


    if (HTTP_MOVEPERM == http_code || HTTP_MOVETEMP == http_code) {

        const char *location = evhttp_find_header(evhttp_request_get_input_headers(req), "Location");
        if (NULL != location) {
            //fprintf(stderr, "Location: %s\n", location);

            create_request(location);
        }
    }

    if (HTTP_OK == http_code) {

        struct evbuffer *input_buffer = evhttp_request_get_input_buffer(req);

        int found = 0;
        while ((nread = evbuffer_remove(input_buffer, buffer, sizeof (buffer))) > 0) {
            if (0 == search_find(&search_list, buffer, nread)) {
                found = 1;
                break;
            }
        }

        fprintf(stderr, "%s: %d\n", evhttp_find_header(evhttp_request_get_output_headers(req), "Host"), found);


    }


    if (--n_pending_requests == 0)
        event_base_loopexit(base, NULL);
}

void
create_request(const char *url) {
    struct evhttp_uri *http_uri = NULL;
    const char *host;
    struct bufferevent *bev;
    struct evhttp_connection *evcon = NULL;
    struct evhttp_request *req;


    struct evkeyvalq *output_headers;

    http_uri = evhttp_uri_parse(url);
    if (NULL != http_uri) {
        host = evhttp_uri_get_host(http_uri);
        if (NULL != host) {

            bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
            evcon = evhttp_connection_base_bufferevent_new(base, dnsbase, bev, host, HTTP_PORT);
            if (NULL != evcon) {

                evhttp_connection_set_timeout(evcon, TIMEOUT);

                req = evhttp_request_new(http_request_done, bev);
                if (NULL != req) {
                    output_headers = evhttp_request_get_output_headers(req);

                    evhttp_add_header(output_headers, "Accept", "text/plain;q=0.8");
                    evhttp_add_header(output_headers, "Host", host);
                    evhttp_add_header(output_headers, "User-Agent", "Mozilla/5.0 (Windows NT 6.1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/41.0.2228.0 Safari/537.36");
                    evhttp_add_header(output_headers, "Connection", "close");

                    if (0 == evhttp_make_request(evcon, req, EVHTTP_REQ_GET, url)) {
                        ++n_pending_requests;
                    } else {
                        evhttp_request_free(req);
                        fprintf(stderr, "evhttp_make_request() failed\n");
                    }
                    //evhttp_request_free(req);
                } else {
                    fprintf(stderr, "evhttp_request_new() failed\n");
                }

                //evhttp_connection_free(evcon);
            } else {
                fprintf(stderr, "evhttp_connection_base_bufferevent_new() failed\n");
            }


        } else {
            fprintf(stderr, "url must have a host %s\n", url);
        }
        evhttp_uri_free(http_uri);
    } else {
        fprintf(stderr, "malformed url %s\n", url);
    }

}

int
main(int argc, char** argv) {



    LIST_INIT(&search_list);

    search_add(&search_list, "123", 3);

    base = event_base_new();

    if (base) {

        dnsbase = evdns_base_new(base, 1);
        if (dnsbase) {

            create_request("http://yandex.ru/");
           
            event_base_dispatch(base);
            
            evdns_base_free(dnsbase, 0);
        } else {
            fprintf(stderr, "evdns_base_new() failed\n");
        }


        event_base_free(base);
    } else {
        perror("event_base_new()");
    }

    search_clear(&search_list);

    return (EXIT_SUCCESS);
}
