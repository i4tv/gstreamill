# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([gstreamill], [1.0.0-rc5], [BUG-REPORT-ADDRESS])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_MACRO_DIR([m4])

# check whether to build with debugging enabled
AC_MSG_CHECKING([whether to do a debug build])
AC_ARG_ENABLE(debug,
              AC_HELP_STRING([--enable-debug],
                             [turn on debugging [default=no]]),
              [],[enable_debug="no"])
AC_MSG_RESULT([$enable_debug])

# Add '-g' flag to gcc if it's debug build
if test "$enable_debug" = "yes"; then
   CXXFLAGS="$CXXFLAGS -g"
   CFLAGS="$CFLAGS -g -Wall"
   AC_DEFINE([DEBUG],[1],[enable debug message])
else
   AC_DEFINE([DEBUG],[0],[disable debug message])
fi

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_LIBTOOL

# Checks for libraries.
# AC_CHECK_LIB([pthread], [pthread_create], HAVE_PTHREAD=yes, HAVE_PTHREAD=no)

PKG_CHECK_MODULES([gstreamer], [gstreamer-1.0])
PKG_CHECK_MODULES([gstreamerapp], [gstreamer-app-1.0])
PKG_CHECK_MODULES([gstreamerpluginsbase], [gstreamer-plugins-base-1.0])
PKG_CHECK_MODULES([gstreamerpluginsbad], [gstreamer-plugins-bad-1.0])
PKG_CHECK_MODULES([gio], [gio-2.0])

PKG_CHECK_MODULES([augeas], [augeas])

# Checks for header files.


# Checks for library functions.

AC_CONFIG_FILES([
Makefile
src/Makefile
etc/Makefile
usr/Makefile
gstreamill.spec
])

AC_OUTPUT
