#
# Copyright (c) Huawei Technologies Co., Ltd. 2026. ALL RIGHTS RESERVED.
# See file LICENSE for terms.
#

AC_ARG_WITH([ub],
            [AC_HELP_STRING([--with-ub(=DIR)], [Build ub support, adding DIR/include, DIR/lib,
                                                and DIR/lib64 to the search path for headers and libraries])],
            [],
            [with_ub=/usr])

AC_ARG_WITH([ub-reg],
            [AC_HELP_STRING([--with-ub-reg], [Compile with UB component register])],
            [],
            [with_ub_reg=yes])

AC_ARG_WITH([ub-offload],
            [AS_HELP_STRING([--with-ub-offload], [Reliable Messaging with UB over STARS component register])],
            [ub_offload=yes], [ub_offload=no])

AC_ARG_WITH([uc-offload],
            [AS_HELP_STRING([--with-uc-offload], [Reliable Connection with UB over STARS component register])],
            [uc_offload=yes], [uc_offload=no])

AS_IF([test "x$with_ub" = "xyes"], [with_ub=/usr])
AS_IF([test -d "$with_ub"],
      [with_ub_compile=yes; str="with ub support from $with_ub"],
      [with_ub_compile=no; str="without ub support"])
AS_IF([test -d "$with_ub/lib64"],[libsuff="64"],[libsuff=""])

AC_MSG_NOTICE([Compiling $str])
AS_IF([test "x$with_ub_compile" = "xyes"],
      [AS_IF([test "x$with_ub" = "x/usr"],
             [uburma_incl="-I$with_ub/include/ub/umdk/urma"],
             [uburma_incl="-I$with_ub/include"
              uburma_libs="-L$with_ub/lib$libsuff"])
       LDFLAGS="$uburma_libs $LDFLAGS"
       CFLAGS="$uburma_incl $CFLAGS"
       CPPFLAGS="$uburma_incl $CPPFLAGS"
       AC_CHECK_HEADER([urma_api.h], [], [AC_MSG_WARN([header files not found]); with_ub_compile=no])
       AC_CHECK_HEADERS([urma_ib.h], [urma_with_ib=yes], [urma_with_ib=no])
       AS_IF([test "x$urma_with_ib" = "xyes"], [AC_DEFINE([HAVE_URMA_IB], 1, [URMA SUPPORT IB])])
       AC_CHECK_LIB([urma], [urma_init],
           [
           AC_SUBST(UBURMA_LDFLAGS,  ["$uburma_libs -lurma_common -lurma"])
           AC_SUBST(UBURMA_DIR,      ["$with_ub"])
           AC_SUBST(UBURMA_CPPFLAGS, ["$uburma_incl"])
           AC_SUBST(UBURMA_CFLAGS,   ["$uburma_incl"])
           ],
           [AC_MSG_WARN([liburma.so not found]); with_ub_compile=no],
           ["-lurma_common"])
       AS_IF([test "x$with_ub_reg" != "xno"],
             [AC_DEFINE([HAVE_UB_REG], 1, [UB component register])])

       AC_CHECK_HEADERS([stars_interface.h], [urma_with_stars=yes], [urma_with_stars=no])
       AS_IF([test "x$urma_with_stars" = "xyes"], [AC_SUBST(STARS_LDFLAGS,  ["-lruntime -lsecurec"])])
       AS_IF([test "x$with_ub_compile" != "xno"], [uct_modules="${uct_modules}:ub"])
      ],
      [with_ub_compile=no])

#
# For automake, used in Makefile.am
#
AM_CONDITIONAL([HAVE_UB],      [test "x$with_ub_compile" != xno])
AM_CONDITIONAL([HAVE_UB_REG],  [test "x$with_ub_reg" != xno])
AM_CONDITIONAL([HAVE_UB_OFFLOAD],      [test "x$ub_offload" != xno])
AM_CONDITIONAL([HAVE_UC_OFFLOAD],      [test "x$uc_offload" != xno])

uct_ub_modules=""
AC_DEFINE_UNQUOTED([uct_ub_MODULES], ["${uct_ub_modules}"], [UB loadable modules])
AC_CONFIG_FILES([src/uct/ub/Makefile])
