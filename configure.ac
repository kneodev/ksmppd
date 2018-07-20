# Autoconf Minimum Version Required Check
AC_PREREQ([2.68])
# Initilise Autoconf with project Name and Version Details
AC_INIT([ksmppd], [1.4.4], [kneodev@gmail.com], [ksmppd], [https://github.com/kneodev/ksmppd])
# Put autotools auxiliary files in a subdir, so they don't clutter top dir:
AC_CONFIG_AUX_DIR([build-aux])
# Initialise Automake with Default version Number and Default CPP flags
AM_INIT_AUTOMAKE([1.11 -Wall -Werror subdir-objects])
# Check for some Unique file in the Project
AC_CONFIG_SRCDIR([smpp/])
# Check for C Compiler
AC_PROG_CC
AC_PROG_CC_C99
# config.h will be created and default header
AC_CONFIG_HEADERS([config.h])
# all Macro in this folder
AC_CONFIG_MACRO_DIR([m4])
# AR required to create static library
AM_PROG_AR
# Silence warning: ar: 'u' modifier ignored since 'D' is the default
AC_SUBST(AR_FLAGS, [cr])
# libtool Required  
LT_PREREQ([2.2])
LT_INIT([dlopen])
AC_LIBTOOL_DLOPEN
AC_PROG_LIBTOOL
# Check of library created using ranlib
#AC_PROG_RANLIB
AC_ENABLE_SHARED
# Check for pthread
AC_CHECK_LIB(pthread, pthread_create,[AC_DEFINE(ENABLE_THREADS, 1, [Define this to enable threads.])],[AC_MSG_ERROR([required library pthread missing])])
# libtool Required
LT_PREREQ([2.2])
LT_INIT([dlopen])
AC_ENABLE_SHARED
AC_PROG_LIBTOOL(libtool)
AC_MSG_NOTICE([Checking for libevent2 and its requirements.])
PKG_CHECK_MODULES([libxml2], [libxml-2.0], with_libxml2=yes, with_libxml2=no)
if test "$with_libxml2" == "no"
then
	AC_MSG_ERROR([libxml2 library not found ])
	exit 1
fi
# check for libevent library
AC_MSG_NOTICE([Checking for libevent2 and its requirements.])
PKG_CHECK_MODULES([libevent], [libevent >= 2.0], with_libevent=yes, with_libevent=no)
if test "$with_libevent" == "no"
then
        AC_MSG_ERROR([libevent library not found ])
        exit 1
fi

#check for shtool required to extract platform
AC_CHECK_PROG(SHTOOL_CHECK, shtool, yes)
if test x"$SHTOOL_CHECK" != x"yes" ; then
    AC_MSG_ERROR([Please install shtool before installing.])
fi
PLATFORMINFO=`shtool platform -S "-" -F "%<ac>-%<sp>" --lower`
AC_DEFINE_UNQUOTED(PLATFORMINFO,"$PLATFORMINFO",[platform version])

AC_CHECK_PROG(GWCONFIG_CHECK, gw-config, yes)
if test x"$GWCONFIG_CHECK" != x"yes" ; then
    AC_MSG_ERROR([Please install kannel gw-config before installing.])
fi
# define GIT_VERSION
AC_CHECK_PROG(GIT_CHECK, git, yes)
if test x"$GIT_CHECK" != x"yes" ; then
    AC_MSG_ERROR([Please install git before installing.])
fi
GITVERSION=`git describe --always --tags`
AC_DEFINE_UNQUOTED(GITVERSION,"$GITVERSION",[git version])
# Generate These Files for Sucessfully Build
AC_CONFIG_FILES([
                Makefile
                smpp/Makefile
])
AC_OUTPUT

