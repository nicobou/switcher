pluginsdir = $(prefix)/switcher-@LIBSWITCHER_API_VERSION@/plugins

plugins_LTLIBRARIES = \
    libpjsip_plugin.la 

#libpjsip info
libpjsip_plugin_la_SOURCES = \
    pj-call.cpp \
    pj-codec-utils.cpp \
    pj-codec.cpp \
    pj-ice-stream-trans.cpp \
    pj-media-endpt.cpp \
    pj-presence.cpp \
    pj-sip-plugin.cpp \
    pj-sip.cpp \
    pj-stun-turn.cpp \
    pj-whitelist.cpp

EXTRA_DIST = \
    more_codecs.patch \
    pjproject-2.5.1.tar.bz2

nodist_libpjsip_plugin_la_SOURCES = \
    pjproject-2.5.1/install_dir/include/pjsua-lib/pjsua.h

BUILT_SOURCES = \
    pjproject-2.5.1/install_dir/include/pjsua-lib/pjsua.h

libpjsip_plugin_la_include_HEADERS = \
    pj-call.hpp \
    pj-call-utils.hpp \
    pj-codec.hpp \
    pj-codec-utils.hpp \
    pj-ice-stream-trans.hpp \
    pj-media-endpt.hpp \
    pj-presence.hpp \
    pj-sip-plugin.hpp \
    pj-sip.hpp \
    pj-stun-turn.hpp \
    pj-whitelist.hpp

libpjsip_plugin_la_includedir = \
    $(includedir)/switcher-$(LIBSWITCHER_API_VERSION)/switcher

libpjsip_plugin_la_LDFLAGS = \
    $(GLIB_LIBS) \
    $(GST_LIBS) \
    $(JSONGLIB_LIBS) \
    $(SHMDATA_LIBS) \
    -L$(top_builddir)/switcher/.libs \
    -avoid-version \
    -lswitcher-@LIBSWITCHER_API_VERSION@ \
    -module \
    `pkg-config --libs pjproject-2.5.1/install_dir/lib/pkgconfig/libpjproject.pc`

export PJPROJECT_STATIC_LIBS=''

libpjsip_plugin_la_LIBADD = \
    $(PJPROJECT_STATIC_LIBS)

libpjsip_plugin_la_CXXFLAGS = \
    $(GLIB_CFLAGS) \
    $(GST_CFLAGS) \
    $(JSONGLIB_CFLAGS) \
    `pkg-config --cflags pjproject-2.5.1/install_dir/lib/pkgconfig/libpjproject.pc` \
    $(SHMDATA_CFLAGS) \
    -DG_LOG_DOMAIN=\"switcher\" \
    -I$(top_srcdir) \
    -Wno-error=write-strings \
    -Wno-error=unused-parameter

pjproject-2.5.1/install_dir/include/pjsua-lib/pjsua.h: Makefile
	tar -xjf $(srcdir)/pjproject-2.5.1.tar.bz2 \
        && cd pjproject-2.5.1 \
        && mkdir -p install_dir \
        && patch -p0 < ../$(srcdir)/more_codecs.patch \
        && sed -iold 's/isblank/MACROIsBlank/' pjlib/include/pj/compat/ctype.h \
        && sed -iold 's/isblank/MACROIsBlank/' pjlib/include/pj/ctype.h \
        && sed -iold 's/pj_ioqueue_create(icedemo.pool, 16/pj_ioqueue_create(icedemo.pool, 128/' pjsip-apps/src/samples/icedemo.c \
        && sed -iold 's/buffer\[1000\]/buffer\[1000000\]/' pjsip-apps/src/samples/icedemo.c \
        && sed -iold 's/REGC_TSX_TIMEOUT\t33000/REGC_TSX_TIMEOUT  3000/' pjsip/src/pjsip-ua/sip_reg.c \
        && CFLAGS="-DPJ_ICE_ST_MAX_CAND=512 -DPJ_ICE_MAX_CHECKS=512 -DPJ_ICE_COMP_BITS=8 -DPJ_ICE_MAX_CAND=1024 -DPJ_ICE_CAND_TYPE_PREF_BITS=16 -DPJ_IOQUEUE_MAX_EVENTS_IN_SINGLE_POLL=512 -DPJ_IOQUEUE_MAX_HANDLES=512 -DPJSIP_MAX_PKT_LEN=8000 -DPJSIP_TD_TIMEOUT=5000" \
           ./configure --prefix `pwd`/install_dir --disable-video  \
           --disable-sound --disable-g711-codec --disable-g7221-codec  \
           --disable-g722-codec --disable-gsm-codec --disable-ilbc-codec \
           --disable-l16-codec --disable-speex-aec  --disable-speex-codec \
        && CFLAGS="-fPIC" make -j dep \
        && CFLAGS="-fPIC" make -j \
        && make install \
        && PJPROJECT_STATIC_LIBS=`cd ..; ls pjproject-2.5.1/install_dir/lib/*.a`

clean-local:
	-rm -rf pjproject-2.5.1

distclean-local:
	-rm -rf pjproject-2.5.1 

#### plugin unit test
AM_CFLAGS = \
    -I$(top_srcdir)

AM_CXXFLAGS =  \
    $(GLIB_CFLAGS) \
    $(GSOAP_CFLAGS) \
    $(GST_CFLAGS) \
    $(JSONGLIB_CFLAGS) \
    $(SHMDATA_CFLAGS) \
    -I$(top_srcdir) 

AM_LDFLAGS = \
    $(GLIB_LIBS) \
    $(GSOAP_LIBS) \
    $(GST_LIBS) \
    $(JSONGLIB_LIBS) \
    $(SHMDATA_LIBS) \
    $(top_builddir)/switcher/libswitcher-@LIBSWITCHER_API_VERSION@.la

check_PROGRAMS = \
    check_pjsip

check_pjsip_SOURCES = \
    check_pjsip.cpp

TESTS = $(check_PROGRAMS)
