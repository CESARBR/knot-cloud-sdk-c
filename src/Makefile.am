lib_headers = knot_cloud.h
lib_sources = knot_cloud.c parser.c parser.h mq.c mq.h log.c log.h

modules_libadd = @ELL_LIBS@ @JSON_LIBS@ @RABBITMQ_LIBS@ @KNOTPROTO_LIBS@
modules_cflags = @ELL_CFLAGS@ @JSON_CFLAGS@ @RABBITMQ_CFLAGS@ @KNOTPROTO_CFLAGS@

libknotcloudsdkc_includedir = $(includedir)/knot
libknotcloudsdkc_include_HEADERS = $(lib_headers)

lib_LTLIBRARIES = libknotcloudsdkc.la
libknotcloudsdkc_la_SOURCES = $(lib_headers) $(lib_sources)
libknotcloudsdkc_la_LIBADD = $(modules_libadd) -lm
libknotcloudsdkc_la_CFLAGS = $(AM_CFLAGS) $(modules_cflags) -Werror
libknotcloudsdkc_la_LDFLAGS = $(AM_LDFLAGS)

pkgconfigdir = $(libdir)/pkgconfig

pkgconfig_DATA = knotcloudsdkc.pc
