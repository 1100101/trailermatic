SUBDIRS = . tests
AM_CPPFLAGS = -I$(top_srcdir)/include/

AM_CFLAGS = $(LIBXML_CFLAGS) $(LIBCURL_CFLAGS) $(PCRE_CFLAGS)

bin_PROGRAMS = trailermatic

if DBG_BUILD
  noinst_PROGRAMS = trailermatic-mw
endif

trailermatic_SOURCES = \
   $(top_srcdir)/src/trailermatic.c      \
   $(top_srcdir)/src/base64.c         \
   $(top_srcdir)/src/config_parser.c  \
   $(top_srcdir)/src/downloads.c      \
   $(top_srcdir)/src/feed_item.c      \
   $(top_srcdir)/src/file.c           \
   $(top_srcdir)/src/list.c           \
   $(top_srcdir)/src/output.c         \
   $(top_srcdir)/src/filters.c        \
   $(top_srcdir)/src/prowl.c          \
   $(top_srcdir)/src/regex.c          \
   $(top_srcdir)/src/rss_feed.c       \
   $(top_srcdir)/src/state.c          \
   $(top_srcdir)/src/urlcode.c        \
   $(top_srcdir)/src/utils.c          \
   $(top_srcdir)/src/web.c            \
   $(top_srcdir)/src/xml_parser.c

noinst_HEADERS =    \
   $(top_srcdir)/include/trailermatic.h      \
   $(top_srcdir)/include/base64.h         \
   $(top_srcdir)/include/config_parser.h  \
   $(top_srcdir)/include/downloads.h      \
   $(top_srcdir)/include/feed_item.h      \
   $(top_srcdir)/include/file.h           \
   $(top_srcdir)/include/list.h           \
   $(top_srcdir)/include/output.h         \
   $(top_srcdir)/include/filters.h        \
   $(top_srcdir)/include/prowl.h          \
   $(top_srcdir)/include/regex.h          \
   $(top_srcdir)/include/rss_feed.h       \
   $(top_srcdir)/include/state.h          \
   $(top_srcdir)/include/urlcode.h        \
   $(top_srcdir)/include/utils.h          \
   $(top_srcdir)/include/web.h            \
   $(top_srcdir)/include/xml_parser.h

trailermatic_mw_SOURCES =  \
   $(trailermatic_SOURCES) \
	 $(top_srcdir)/src/memwatch.c

if DBG_BUILD
  trailermatic_mw_HEADERS =  \
     $(noinst_HEADERS)    \
     $(top_srcdir)/include/memwatch.h

  trailermatic_mwdir = .

  trailermatic_mw_CPPFLAGS = -DMEMWATCH -DDEBUG -O0 -ggdb3
endif

dist_sysconf_DATA = trailermatic.conf-sample

LDADD = \
    $(LIBCURL_LIBS) \
    $(LIBXML_LIBS)  \
    $(PCRE_LIBS)

clean-local:
	rm -f version.h

EXTRA_trailermatic_SOURCES = \
    version.h

BUILT_SOURCES = \
    version.h

version.h:
	echo '#define SVN_REVISION          "'`git rev-parse --short HEAD`'"' > version.h
	echo '#define SHORT_VERSION_STRING  "'@VERSION_NUMBER@'"' >> version.h
	echo '#define LONG_VERSION_STRING   "'@VERSION_NUMBER@' ('`git rev-parse --short HEAD`')"' >> version.h

strip:
	$(STRIP) $(bin_PROGRAMS)
