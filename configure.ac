AC_INIT([libzbxdocker], [0.0.1], [info@monitoringartist.com])
AM_INIT_AUTOMAKE([-Wall -Werror -Wno-portability foreign])
m4_pattern_allow([AM_PROG_AR])
AM_PROG_AR
AC_PROG_LIBTOOL # seem to need this on CentOS 5 (libtool v1.5)
LT_INIT
AC_PROG_CC
AC_CONFIG_HEADERS([config.h])

# check for stdlib header files
AC_HEADER_STDC

# Checking for Zabbix headers
AX_LIB_ZABBIX
if test ! "x$found_zabbix" = "xyes"; then
	AC_MSG_ERROR([Zabbix headers not found])
fi

# output
AC_CONFIG_FILES([
 Makefile
 src/Makefile
])
AC_OUTPUT
