dnl Process this file with autoconf to produce a configure script.
AC_PREREQ(2.61)
AC_INIT([fossil], [20110528], [joerg@NetBSD.org])
AC_CONFIG_SRCDIR([src/schema.c])
AC_CONFIG_AUX_DIR([autoconf])
AC_CONFIG_MACRO_DIR([autoconf])
AC_CONFIG_HEADER(autoconfig.h)

AC_CANONICAL_HOST

# Checks for programs.
AC_PROG_MAKE_SET
AC_PROG_CC

case $host_os in
dragonfly|freebsd|netbsd|openbsd)	AC_DEFINE([USE_PREAD], [1], [Use pread/pwrite system calls in place of seek + read/write])
esac

AC_CHECK_LIB(z, inflateEnd, [zlib_cv_libz=yes], [zlib_cv_libz=no])
AC_CHECK_HEADER(zlib.h, [zlib_cv_zlib_h=yes], [zlib_cv_zlib_h=no])

if test "$zlib_cv_libz" != yes || test "$zlib_cv_zlib_h" != yes; then
        AC_MSG_ERROR(zlib not found, please install it or add the necessary flags to CPPFLAGS / LDFLAGS)
fi
LIBS="$LIBS -lz"

enable_openssl=check
AC_ARG_ENABLE([openssl], [AS_HELP_STRING([--enable-openssl], [Include HTTPS support using for OpenSSL])], [enable_openssl=yes], [enable_openssl=no])
if test "$enable_openssl" != no; then
	AX_CHECK_OPENSSL([AC_DEFINE([FOSSIL_ENABLE_SSL], [1], [Support HTTPS])],
	    [ if test "$enable_openssl" = yes; then
	        AC_MSG_RESULT(failed)
                AC_MSG_ERROR(HTTPS support requested, but OpenSSL not found)
              fi
	    ]
	)
fi

# Network functions on Solaris
AC_SEARCH_LIBS([gethostbyname], [nsl network])
AC_SEARCH_LIBS([socket],
               [socket],
               [],
               [AC_CHECK_LIB([socket],
                             [socket],
                             [LIBS="-lsocket -lnsl $LIBS"],
                             [],
                             [-lnsl])])


# Check for getpassphrase() for Solaris 10 where getpass() truncates to 10 chars
AC_CHECK_FUNCS(getpassphrase)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
