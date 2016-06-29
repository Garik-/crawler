/**
 * @author Gar|k <garik.djan@gmail.com>
 * @copyright (c) 2015, http://c0dedgarik.blogspot.ru/
 * @version 1.0
 */

#include "main.h"
#include "picohttpparser.h"

#include <signal.h>

static int sigterm;

static void
sig_handler(int signum) {
    sigterm = 1;
}

#define CHECK_TERM() if(0 != sigterm) break;

static char *
parse_csv(char *line, size_t len, ptrdiff_t * line_size) {
    char * end = memchr(line, '\n', len);
    *line_size = 0;

    if (NULL != end) {
        char * separator = memchr(line, ';', end - line);
        if (NULL != separator) {

            *separator = 0;
            *line_size = separator - line;

        }
        return ++end;
    }

    return NULL;
}

static long mtime() {
    struct timeval t;

    gettimeofday(&t, NULL);
    long mt = (long) t.tv_sec * 1000 + t.tv_usec / 1000;
    return mt;
}

void
print_usage(const char * name) {
    err_quit("Usage: %s [KEY]... DOMAIN-LIST\n\n\
\t-n\tnumber asynchronous requests, default %d\n\
\t-o\toutput file found domains, default stdout\n\n", name, MAXPENDING);
}

static inline void
print_stat(options_t *options, const long *time_start) {
    fprintf(stdout, "DNS checked domains: %d; found: %d; not found: %d (%d%%);\n\
CMS fonud: %d; follow location: %d; parse erros: %d\n\
pending: %d; threads: %d; time: %ld milliseconds\n",
            options->counters.domains,
            options->counters.dnsfound,
            options->counters.dnsnotfound,
            (options->counters.dnsnotfound > 0 ? ((options->counters.dnsnotfound * 100) / options->counters.domains) : 0),
            options->counters.cmsfound,
            options->counters.follow,
            options->counters.error_parse,
            options->pending_requests,
            1,
            mtime() - *time_start);
}

static void *
main_loop(void *vptr_args) {
    options_t *options = (options_t *) vptr_args;

    const size_t page_size = (size_t) sysconf(_SC_PAGESIZE);

    off_t offset = 0;
    size_t pending_requests = 0;

    while (offset < options->file.len) { // file mapping loop
        char * buffer = (char *) mmap(0, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, options->file.fd, offset);
        if (MAP_FAILED != buffer) {

            char * line, *end_line;
            line = buffer;
            ptrdiff_t line_size = 0;

            while (NULL != (end_line = parse_csv(line, page_size - (line - buffer), &line_size))) {

                CHECK_TERM();

                if (line_size > 0 && (line[line_size - 1] == 'U' || line[line_size - 1] == 'u')) {


                    ev_ares_gethostbyname(options, line);

                    ++pending_requests;
                }

                line = end_line;

                if (MAXPENDING == pending_requests) {
                    debug("pending_requests: %d", pending_requests);
                    ev_run(options->loop, 0);
                    pending_requests = 0;
                }

            }

            munmap(buffer, page_size);

        } else {
            err_ret("mmap");
            break;
        }

        CHECK_TERM();

        offset += page_size;
    }

    if (pending_requests > 0) {
        debug("pending_requests: %d", pending_requests);
        ev_run(options->loop, 0);
        pending_requests = 0;
    }


    return NULL;
}

int main(int argc, char** argv) {
    const long time_start = mtime();

    if (1 == argc) {
        print_usage(argv[0]);
    }

    char *opts = "t:n:o:";
    int opt, status;
    options_t options;

    sigterm = 0;
    (void) signal(SIGHUP, sig_handler);
    (void) signal(SIGINT, sig_handler);
    (void) signal(SIGTERM, sig_handler);

    bzero(&options, sizeof (options_t));

    options.loop = EV_DEFAULT;
    options.file.out = STDOUT_FILENO;
    options.timeout = MAXDNSTIME;
    options.pending_requests = MAXPENDING;

    while ((opt = getopt(argc, argv, opts)) != -1) {
        switch (opt) {
            case 't':
                options.timeout = atoi(optarg);
                break;
            case 'n':
                options.pending_requests = atoi(optarg);
                break;
            case 'o':
                if ((options.file.out = open(optarg, O_APPEND | O_CREAT | O_WRONLY,
                        S_IRUSR | S_IWUSR)) < 0) {
                    err_sys("[E] open output file %s", optarg);
                }
                break;
            case '?':
                print_usage(argv[0]);
        }
    }

    if (argc == optind) {
        print_usage(argv[0]);
    }
    
    if ((options.file.parse = open("error_parse.csv", O_CREAT | O_WRONLY,
                        S_IRUSR | S_IWUSR)) < 0) {
                    err_sys("[E] open error file %s", optarg);
    }

    //debug("proc num %d\n", sysconf(_SC_NPROCESSORS_CONF));

    if (ARES_SUCCESS == (status = ares_library_init(ARES_LIB_INIT_ALL))) {

        if (ARES_SUCCESS == (status = ev_ares_init_options(&options))) {

            if ((options.file.fd = open(argv[optind], O_RDWR)) > 0) {

                struct stat statbuf;

                if (fstat(options.file.fd, &statbuf) < 0 && S_ISREG(statbuf.st_mode)) {
                    err_ret("fstat");
                    status = EXIT_FAILURE;
                } else {
                    options.file.len = statbuf.st_size;

                    pthread_t threads;
                    pthread_create(&threads, NULL, (void *(*)(void *)) main_loop, &options);
                    pthread_join(threads, NULL);
                }

                close(options.file.fd);
            } else {
                err_ret("[E] domain list %s", argv[optind]);
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

    print_stat(&options, &time_start);

    return status;

}