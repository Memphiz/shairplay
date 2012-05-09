AC_PREREQ(2.50)
AC_INIT([libshairplay], [1.0.0], [juhovh@iki.fi])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign])
AC_CANONICAL_HOST

# Work around stupid autoconf default cflags. pt 1
SAVE_CFLAGS="x${CFLAGS}"

AC_PROG_CC
AC_PROG_LIBTOOL

AM_PROG_CC_C_O
PKG_PROG_PKG_CONFIG

# Work around stupid autoconf default cflags. pt 2
if test "$SAVE_CFLAGS" = "x"; then
  CFLAGS=""
fi

#option: bindings
AC_ARG_ENABLE([bindings],
              [AC_HELP_STRING([--enable-bindings],
                              [Install python bindings])],
	      [ENABLE_BINDINGS=$enableval],
	      [ENABLE_BINDINGS="no"])

if test x$ENABLE_BINDINGS = xyes; then
  MAYBE_BINDINGS="bindings"
fi
AC_SUBST(MAYBE_BINDINGS)

AC_CHECK_HEADER([dns_sd.h],[use_dnssd=true],[use_dnssd=false])

case $host in
  *darwin*)
    ;;
  *cygwin*)
    ;;
  *mingw*)
    ADDITIONAL_C_FLAGS="-DWINVER=0x0501"
    ADDITIONAL_LINKER_FLAGS="-lws2_32 -lwinmm"
    ;;
  *solaris*)
    ;;
  *)
    ;;
esac

AM_CONDITIONAL(USE_DNSSD,test x$use_dnssd=xtrue)

AC_SUBST(ADDITIONAL_C_FLAGS)
AC_SUBST(ADDITIONAL_LINKER_FLAGS)
AC_SUBST(LIBNFS_PC_REQ_PRIVATE)

#output
AC_CONFIG_FILES([Makefile]
                [include/Makefile]
                [lib/Makefile]
                [bindings/Makefile]
               )

AC_OUTPUT([shairplay.pc])
