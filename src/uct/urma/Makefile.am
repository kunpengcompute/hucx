#
# Copyright (c) Huawei Technologies Co., Ltd. 2026. ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#

if HAVE_UB

module_LTLIBRARIES    = libuct_ub.la
libuct_ub_la_CPPFLAGS = $(BASE_CPPFLAGS) $(UBURMA_CPPFLAGS)
libuct_ub_la_CFLAGS   = $(BASE_CFLAGS)
libuct_ub_la_LIBADD   = $(top_builddir)/src/ucs/libucs.la \
                        $(top_builddir)/src/uct/libuct.la
libuct_ub_la_LDFLAGS = $(UBURMA_LDFLAGS) $(NUMA_LIBS) -version-info $(SOVERSION)

noinst_HEADERS = \
	base/urma_device.h \
	base/urma_iface.h \
	base/urma_ep.h \
	base/urma_md.h \
	base/urma_def.h \
	um_urma/um_urma_iface.h \
	um_urma/um_urma_ep.h \
	um_urma/um_urma_def.h \
	um_urma/um_urma_inl.h

libuct_ub_la_SOURCES = \
	base/urma_device.c \
	base/urma_iface.c \
	base/urma_ep.c \
	base/urma_md.c \
	um_urma/um_urma_iface.c \
	um_urma/um_urma_ep.c

include $(top_srcdir)/config/module.am

endif # HAVE_UB
