ACLOCAL_AMFLAGS = -I m4
noinst_LTLIBRARIES = libsmpp.la
libsmpp_la_SOURCES = \
libsmpp/smpp_bearerbox.c libsmpp/smpp_database_mysql.c libsmpp/smpp_http_client.c libsmpp/smpp_listener.c \
libsmpp/smpp_plugin.c libsmpp/smpp_queues.c libsmpp/smpp_server.c libsmpp/smpp_uuid.c libsmpp/smpp_database.c \
libsmpp/smpp_database.c libsmpp/smpp_esme.c libsmpp/smpp_http_server.c libsmpp/smpp_pdu_util.c libsmpp/smpp_queued_pdu.c \
libsmpp/smpp_route.c libsmpp/smpp_server_cfg.c
libsmpp_la_CPPFLAGS = -fPIC -I ${top_srcdir}/smpp/libsmpp  ${libevent_CFLAGS} `gw-config --cflags`
libsmpp_la_LDFLAGS = `gw-config --libs` ${libevent_LIBS} -ldl

bin_PROGRAMS = ksmppd
ksmppd_SOURCES =  ksmppd.c
ksmppd_CPPFLAGS = -I ${top_srcdir}/smpp/libsmpp `gw-config --cflags`
ksmppd_LDFLAGS = `gw-config --libs` ${libevent_LIBS} -ldl
ksmppd_LDADD= libsmpp.la ${libevent_LIBS}
