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

            return ++end;
        }
    }

    return NULL;
}

static long mtime() {
    struct timeval t;

    gettimeofday(&t, NULL);
    long mt = (long) t.tv_sec * 1000 + t.tv_usec / 1000;
    return mt;
}

static inline void
print_stat(options_t *options, const long *time_start) {
    fprintf(stderr, "\r\nDNS checked domains: %d; found: %d; not found: %d (%d%%); time: %ld milliseconds\n",
            options->counters.domains,
            options->counters.dnsfound,
            options->counters.dnsnotfound,
            (options->counters.dnsnotfound > 0 ? ((options->counters.dnsnotfound * 100) / options->counters.domains) : 0),
            mtime() - *time_start);
}

int
main(void) {
    const long time_start = mtime();


    int status;
    options_t options;
    domain_t domains[MAXPENDING];
    const size_t page_size = (size_t) sysconf(_SC_PAGESIZE);
    size_t pending_requests = 0;

    sigterm = 0;
    (void) signal(SIGHUP, sig_handler);
    (void) signal(SIGINT, sig_handler);
    (void) signal(SIGTERM, sig_handler);

    bzero(&options, sizeof (options_t));

    options.loop = EV_DEFAULT;

    if (ARES_SUCCESS == (status = ares_library_init(ARES_LIB_INIT_ALL))) {

        if (ARES_SUCCESS == (status = ev_ares_init_options(&options))) {

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

                                CHECK_TERM();

                                if (line_size > 0 && (line[line_size - 1] == 'U' || line[line_size - 1] == 'u')) {

                                    domains[pending_requests].options = &options;
                                    domains[pending_requests].domain = line;
                                    //domains[pending_requests].domain_len = line_size;

                                    ev_ares_gethostbyname(&domains[pending_requests]);

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

                        CHECK_TERM();

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

    print_stat(&options, &time_start);

    return status;
}