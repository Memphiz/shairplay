lib_LTLIBRARIES = libshairplay.la

libshairplay_la_CPPFLAGS = -I$(abs_top_srcdir)/include

libshairplay_la_SOURCES = \
	alac/alac.c \
	crypto/aes.c \
	crypto/bigint.c \
	crypto/hmac.c \
	crypto/md5.c \
	crypto/rc4.c \
	crypto/sha1.c \
	base64.c \
	digest.c \
	http_parser.c \
	http_request.c \
	http_response.c \
	httpd.c \
	logger.c \
	netutils.c \
	raop.c \
	raop_buffer.c \
	raop_rtp.c \
	rsakey.c \
	rsapem.c \
	sdp.c \
	utils.c
if USE_DNSSD
libshairplay_la_SOURCES+=dnssd.c
endif

libshairplay_la_CFLAGS = $(ADDITIONAL_C_FLAGS)
libshairplay_la_LDFLAGS = -version-info 1:0:0 -no-undefined $(ADDITIONAL_LINKER_FLAGS)

