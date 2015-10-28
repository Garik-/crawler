/**
 * @author Gar|k <garik.djan@gmail.com>
 * @copyright (c) 2015, http://c0dedgarik.blogspot.ru/
 * @version 0.2
 */
 
#include "main.h"

#include <pthread.h>
#include <linux/limits.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/dns.h>
#include <event2/http.h>
#include <event2/thread.h>
#include <event.h>

#define HTTP_PORT 80

#define DEFAULT_TIMEOUT 3
#define DEFAULT_REQUESTS 20

typedef struct options_t {
    int timeout;
    int pending_requests;

    int out; // out FD
    int error; // error FD
    int fd;
    size_t len;
    off_t page_offset;
    ptrdiff_t buffer_offset;

} options_t;

typedef struct counters_t {
    ssize_t domains;
    ssize_t errors;
    ssize_t found;
} counters_t;

struct searchval {
    LIST_ENTRY(searchval) next;
    char * value;
    size_t len;
};


// Globals
int g_pending_requests = 0;
int g_done = 0;

int fd_valid = 0;

options_t g_options;
counters_t g_counters;

struct event_base *base = NULL;
struct evdns_base *dnsbase = NULL;

LIST_HEAD(searchl, searchval) search_list;

#ifdef DEBUG
int log_to_stderr = 1;
#else
int log_to_stderr = 1; // test
#endif

// ----

void
print_usage(const char * name) {
    err_quit("Usage: %s [KEY]... DOMAIN-LIST\n\n\
\t-t\ttimeout connect in seconds, default %d\n\
\t-n\tnumber asynchronous requests, default %d\n\
\t-o\toutput file found domains, default stdin\n\
\t-e\toutput file error domains, default stderr\n\
\t-c\tcontinue\n", name, DEFAULT_TIMEOUT, DEFAULT_REQUESTS);
}

long mtime() {
    struct timeval t;

    gettimeofday(&t, NULL);
    long mt = (long) t.tv_sec * 1000 + t.tv_usec / 1000;
    return mt;
}

void
print_stat(const long t) {
    log_msg("\nAsynchronous requests: %d; timeout connect: %d;\nChecked: %d; \
errors: %d (%d%%); found: %d; time: %ld milliseconds\n",
            g_options.pending_requests,
            g_options.timeout,
            g_counters.domains,
            g_counters.errors,
            (g_counters.errors > 0 ? ((g_counters.errors * 100) / g_counters.domains) : 0),
            g_counters.found,
            mtime() - t);
}

static void
sighandler(int signum) {
    g_done++;
    //__sync_fetch_and_add(&g_done, 1);
}

inline static void wait_pending_requests() {

#ifdef DEBUG
    log_msg("wait pending_requests = %d", g_pending_requests);
#endif

    while (g_pending_requests > 0) {
        sleep(1);
    }
#ifdef DEBUG
    log_msg("pending_requests = %d", g_pending_requests);
#endif
}

int
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

    search->len = len;

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

int
search_find(struct searchl *search_list, const char * buffer, size_t len) {
    struct searchval *search;

    LIST_FOREACH(search, search_list, next) {
        if (NULL != my_memmem(buffer, len, search->value, search->len)) {
            return (0);
        }
    }
    return (-1);
}

void create_request(const char *url);

inline bool
is_valid_location(const char *location) {
    bool valid = true;
    return valid;
}

inline void write_buf(int fd, const char * buf, ssize_t nr) {
    ssize_t nw, off;
    for (off = 0; nr; nr -= nw, off += nw) {
        if ((nw = write(fd, buf + off, (size_t) nr)) == -1) {
            log_ret("write_buf %s", buf);
        }
    }
}

static void
http_request_done(struct evhttp_request *req, void *ctx) {
    char buffer[512];
    ev_ssize_t nread;
    struct evkeyval *header;

    char *hostname = (char *) ctx;

    if (req == NULL) {

        g_counters.errors++;

        int errcode = EVUTIL_SOCKET_ERROR();

        switch (errcode) {
                //case EINPROGRESS:
                //    break;
            default:

                nread = snprintf(buffer, sizeof (buffer),
                        (STDERR_FILENO != g_options.error ?
                        "%s;%s (%d);\n" : // запись в файл 
                        "[E] %s, socket error: %s (%d)\n" // вывод в консоль
                        ),
                        hostname,
                        evutil_socket_error_to_string(errcode),
                        errcode);

                write_buf(g_options.error, buffer, nread);


                break;
        }

    } else {

        const int http_code = evhttp_request_get_response_code(req);
        // struct evhttp_connection *evcon = evhttp_request_get_connection(req);

        const char *host = evhttp_find_header(evhttp_request_get_output_headers(req), "Host");
        if (NULL != host) {

#ifdef DEBUG
            log_msg("check host = %s", host);
#endif

            switch (http_code) {
                case HTTP_MOVEPERM:
                case HTTP_MOVETEMP:
                {
                    const char *location = evhttp_find_header(evhttp_request_get_input_headers(req), "Location");

                    if (true == is_valid_location(location)) {
                        create_request(location);
                    }
                    break;
                }
                case HTTP_OK:
                {

                    struct evbuffer *input_buffer = evhttp_request_get_input_buffer(req);

                    int found = 0;
                    while ((nread = evbuffer_remove(input_buffer, buffer, sizeof (buffer))) > 0) {
                        if (0 == search_find(&search_list, buffer, nread)) {
                            found = 1;
                            break;
                        }
                    }

                    if (found) {
                        g_counters.found++;

                        nread = snprintf(buffer, sizeof (buffer),
                                (STDOUT_FILENO != g_options.out ? "%s;\n" : "[!] %s found\n"),
                                host);
                        write_buf(g_options.out, buffer, nread);
                    }

                    //fwrite_unlocked(buffer, nread, 1, g_valid);
                    break;
                }
                default: // error

                    // fprintf(g_error, "%s;%d;\n", host, http_code);
                    break;

            }
        }
    }

    __sync_fetch_and_sub(&g_pending_requests, 1);
    assert(g_pending_requests > -1);

    if (NULL != hostname) free(hostname); // strdup

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


#ifdef DEBUG
            size_t len_host = strlen(host);
            if (NULL != my_memmem(host, len_host, "http", 4)) {
                log_msg("[E] error url %s", url);
            }
#endif


            bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
            evcon = evhttp_connection_base_bufferevent_new(base, dnsbase, bev, host, HTTP_PORT);
            if (NULL != evcon) {

                evhttp_connection_set_timeout(evcon, g_options.timeout);

                req = evhttp_request_new(http_request_done, strdup(host));
                if (NULL != req) {
                    output_headers = evhttp_request_get_output_headers(req);

                    evhttp_add_header(output_headers, "Accept", "text/plain;q=0.8");
                    evhttp_add_header(output_headers, "Host", host);
                    evhttp_add_header(output_headers, "User-Agent", "Mozilla/5.0 \
(Windows NT 6.1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/41.0.2228.0 Safari/537.36");
                    evhttp_add_header(output_headers, "Connection", "close");

                    if (0 == evhttp_make_request(evcon, req, EVHTTP_REQ_GET, url)) {

                        log_msg("create_request: %s", host);

                        g_counters.domains++;

                        //n_pending_requests++;
                        __sync_fetch_and_add(&g_pending_requests, 1);


                    } else {
                        evhttp_request_free(req);
                        log_msg("evhttp_make_request() failed");
                    }
                    //evhttp_request_free(req);
                } else {
                    log_msg("evhttp_request_new() failed");
                }

                //evhttp_connection_free(evcon);
            } else {
                log_msg("evhttp_connection_base_bufferevent_new() failed");
            }

        } else {
            log_msg("url must have a host %s", url);
        }
        evhttp_uri_free(http_uri);
    } else {
        log_msg("malformed url %s", url);
    }

}

/**
 * @param line_size  учитывая конец строки 
 * @return возвращает указатель на следующую строчку или NULL 
 */
char *
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

int get_tmp_filename(char * path, size_t path_len) {
    char * env[] = {
        "TMPDIR", "TMP", "TEMP", "TEMPDIR"
    };

    char *temp_path = NULL;
    int i = 0;
    while (NULL == (temp_path = getenv(env[i])) && ++i < 4);

    if (NULL == temp_path) {
        temp_path = "/tmp";
    }

    return snprintf(path, path_len, "%s/crawler%d", temp_path, getuid());
}

void
read_offset() {

    char path[PATH_MAX];
    if (get_tmp_filename(path, sizeof (path)) > 0) {
        FILE * f = fopen(path, "r");
        if (NULL != f) {

            if (NULL != fgets(path, sizeof (path), f)) {
                g_options.page_offset = atoi(path);
            }
            if (NULL != fgets(path, sizeof (path), f)) {
                g_options.buffer_offset = atoi(path);
            }

#ifdef DEBUG
            log_msg("read offets: page %d, buffer %d",
                    g_options.page_offset,
                    g_options.buffer_offset
                    );
#endif

            fclose(f);
        } else {
            err_ret("fopen %s", path);
        }
    }

}

void
write_offset() {
    char path[PATH_MAX];
    if (get_tmp_filename(path, sizeof (path)) > 0) {
        FILE * f = fopen(path, "w");
        if (NULL != f) {
            fprintf(f, "%jd\n%zu\n", (intmax_t) g_options.page_offset, g_options.buffer_offset);
            fclose(f);
        } else {
            err_ret("fopen %s", path);
        }

    }
}

void
read_loop() {

    const size_t page_size = (size_t) sysconf(_SC_PAGESIZE);

    off_t offset = g_options.page_offset;
    ptrdiff_t buffer_offset = g_options.buffer_offset;

    bool f_first = true;

    char domain[MAXURL];

    while (offset < g_options.len) { // file mapping loop

        char * buffer = (char *) mmap(0, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, g_options.fd, offset);
        if (MAP_FAILED != buffer) {


            //log_msg("offset %d, max %d\n", offset, options.len);

            // parse buffer size page_size
            char * line, *end_line;
            line = buffer;

            if (true == f_first) {
                line += buffer_offset;
                f_first = false;
            }

            ptrdiff_t line_size = 0;

            while (NULL != (end_line = parse_csv(line, page_size - (line - buffer), &line_size))) {

                if (g_done) break;


                if (line_size > 0 && (line[line_size - 1] == 'U' || line[line_size - 1] == 'u')) {
                    //log_msg("line %s, line size %d, end_line %p\n", line, line_size, end_line);

                    snprintf(domain, sizeof (domain), "http://%s/", line);

                    create_request(domain);

                    if (g_options.pending_requests == g_pending_requests) {
                        wait_pending_requests();
                    }

                }


                line = end_line;
                buffer_offset = line - buffer;
            }

            if (munmap(buffer, page_size) < 0) { // free memory
                err_ret("munmap");
                break;
            }


        } else {
            err_ret("mmap");
            break;
        }

        if (g_done) break;

        offset += page_size;
    }

    g_options.page_offset = offset;
    g_options.buffer_offset = buffer_offset;

    // write page_offset && buffer_offset to file
    write_offset();

#ifdef DEBUG
    log_msg("read_loop end");
#endif
}

static void *
read_thread(void *vptr_args) {

#ifdef DEBUG
    log_msg("read_thread start, event_base = %p", base);
#endif

    read_loop(g_options);

    wait_pending_requests();
    event_base_loopexit(base, NULL);

#ifdef DEBUG
    log_msg("read_thread end");
#endif

    pthread_exit(0);
}

int
main_loop() {
    int result = EXIT_FAILURE;

#ifdef DEBUG
    log_msg("main_loop start");
#endif

    evthread_use_pthreads();

    base = event_base_new();
    if (base) {
        dnsbase = evdns_base_new(base, 1);
        if (dnsbase) {

            pthread_t thread;

            if (0 == pthread_create(&thread, NULL, read_thread, NULL)) {
                event_base_dispatch(base);

                pthread_join(thread, NULL);

                result = EXIT_SUCCESS;
            } else {
                log_msg("pthread_create() failed");
            }

            evdns_base_free(dnsbase, 0);
        } else {
            log_msg("evdns_base_new() failed");
        }
        event_base_free(base);
    } else {
        log_msg("event_base_new() failed");
    }
#ifdef DEBUG
    log_msg("main_loop end");
#endif

    return result;
}

int
main(int argc, char** argv) {

    const long start_time = mtime();

    if (1 == argc) {
        print_usage(argv[0]);
    }

    char *opts = "t:n:o:e:c";

    int opt;

    memset(&g_counters, 0, sizeof (counters_t));

    g_options.pending_requests = DEFAULT_REQUESTS;
    g_options.timeout = DEFAULT_TIMEOUT;
    g_options.out = STDOUT_FILENO;
    g_options.error = STDERR_FILENO;

    g_options.page_offset = 0;
    g_options.buffer_offset = 0;

    while ((opt = getopt(argc, argv, opts)) != -1) {
        switch (opt) {
            case 't':
                g_options.timeout = atoi(optarg);
                break;
            case 'n':
                g_options.pending_requests = atoi(optarg);
                break;
            case 'o':
                if ((g_options.out = open(optarg, O_APPEND | O_CREAT | O_WRONLY,
                        S_IRUSR | S_IWUSR)) < 0) {
                    err_sys("[E] open output file %s", optarg);
                }
                break;
            case 'e':
                if ((g_options.error = open(optarg, O_APPEND | O_CREAT | O_WRONLY,
                        S_IRUSR | S_IWUSR)) < 0) {
                    err_sys("[E] open error file %s", optarg);
                }
                break;
            case 'c':
                read_offset();
                break;
            case '?':
                print_usage(argv[0]);
        }
    }

    if (argc == optind) {
        print_usage(argv[0]);
    }

    struct stat statbuf;

    if ((g_options.fd = open(argv[optind], O_RDWR)) < 0) {
        err_sys("[E] domain list %s", argv[optind]);
    }

    if (fstat(g_options.fd, &statbuf) < 0 && S_ISREG(statbuf.st_mode)) {
        close(g_options.fd);
        err_sys("[E] domain list %s", argv[optind]);
    }

    (void) signal(SIGHUP, sighandler);
    (void) signal(SIGINT, sighandler);
    //   (void) signal(SIGPIPE, sighandler);  
    (void) signal(SIGTERM, sighandler);

    g_options.len = statbuf.st_size;

    // ^ read offset in file to continue

    LIST_INIT(&search_list);

    search_add(&search_list, "123", 3);

    const int ret = main_loop();

    search_clear(&search_list);

    //close(fd_valid);

    close(g_options.fd);

    if (STDOUT_FILENO != g_options.out) {
        close(g_options.out);
    }

    if (STDERR_FILENO != g_options.error) {
        close(g_options.error);
    }

    print_stat(start_time);

    return (ret);
}
