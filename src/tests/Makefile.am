AM_CPPFLAGS = -I$(top_srcdir)/include/

check_PROGRAMS = list_test base64_test regex_test http_test parser_test

TESTS = $(check_PROGRAMS)

GLOBAL_SOURCES = \
   $(top_srcdir)/src/output.c     \
   $(top_srcdir)/src/memwatch.c   \
   $(top_srcdir)/src/utils.c


http_test_SOURCES = $(GLOBAL_SOURCES)  \
   $(top_srcdir)/src/file.c            \
   $(top_srcdir)/src/regex.c           \
   $(top_srcdir)/src/urlcode.c         \
   $(top_srcdir)/src/web.c             \
   http_test.c

list_test_SOURCES = $(GLOBAL_SOURCES) \
    $(top_srcdir)/src/list.c          \
    list_test.c

base64_test_SOURCES = $(GLOBAL_SOURCES) \
   $(top_srcdir)/src/base64.c           \
   base64_test.c

regex_test_SOURCES = $(GLOBAL_SOURCES) \
    $(top_srcdir)/src/regex.c          \
    regex_test.c

parser_test_SOURCES = $(GLOBAL_SOURCES) \
    $(top_srcdir)/src/config_parser.c  \
   $(top_srcdir)/src/filters.c          \
    $(top_srcdir)/src/list.c  \
   $(top_srcdir)/src/regex.c            \
    $(top_srcdir)/src/rss_feed.c  \
    parser_test.c

noinst_HEADERS = \
   $(top_srcdir)/include/base64.h   \
   $(top_srcdir)/include/config_parser.h     \
   $(top_srcdir)/include/file.h     \
   $(top_srcdir)/include/list.h     \
   $(top_srcdir)/include/memwatch.h \
   $(top_srcdir)/include/output.h   \
   $(top_srcdir)/include/regex.h    \
   $(top_srcdir)/include/urlcode.h  \
   $(top_srcdir)/include/utils.h    \
   $(top_srcdir)/include/web.h

http_test_LDADD  = $(LIBCURL_LIBS) $(PCRE_LIBS)
http_test_CFLAGS = $(LIBCURL_CFLAGS) $(PCRE_CFLAGS)

regex_test_LDADD  = $(PCRE_LIBS)
regex_test_CFLAGS = $(PCRE_CFLAGS)

parser_test_LDADD = $(PCRE_LIBS)
parser_test_CFLAGS = $(PCRE_CFLAGS)

CFLAGS = -g -O0 -DMEMWATCH -DDEBUG
