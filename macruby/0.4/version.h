#define RUBY_VERSION "1.9.1"
#define RUBY_RELEASE_DATE "2008-06-03"
#define RUBY_VERSION_CODE 190
#define RUBY_RELEASE_CODE 20080603
#define RUBY_PATCHLEVEL 0

#define RUBY_VERSION_MAJOR 1
#define RUBY_VERSION_MINOR 9
#define RUBY_VERSION_TEENY 0
#define RUBY_RELEASE_YEAR 2008
#define RUBY_RELEASE_MONTH 6
#define RUBY_RELEASE_DAY 3

#ifdef RUBY_EXTERN
RUBY_EXTERN const int ruby_version_code;
RUBY_EXTERN const char ruby_version[];
RUBY_EXTERN const char ruby_release_date[];
RUBY_EXTERN const char ruby_platform[];
RUBY_EXTERN const int ruby_patchlevel;
RUBY_EXTERN const char ruby_description[];
RUBY_EXTERN const char ruby_copyright[];
#endif

#define RUBY_AUTHOR "Yukihiro Matsumoto"
#define RUBY_BIRTH_YEAR 1993
#define RUBY_BIRTH_MONTH 2
#define RUBY_BIRTH_DAY 24

#ifndef RUBY_REVISION
/*#include "revision.h"*/
#endif
#ifndef RUBY_REVISION
#define RUBY_REVISION 0
#endif

#if RUBY_VERSION_TEENY > 0 && RUBY_PATCHLEVEL < 5000 && !RUBY_REVISION
#define RUBY_RELEASE_STR "patchlevel"
#define RUBY_RELEASE_NUM RUBY_PATCHLEVEL
#else
#ifdef RUBY_BRANCH_NAME
#define RUBY_RELEASE_STR RUBY_BRANCH_NAME
#else
#define RUBY_RELEASE_STR "revision"
#endif
#define RUBY_RELEASE_NUM RUBY_REVISION
#endif

#if WITH_OBJC
# define RUBY_ENGINE "macruby"
#else
# define RUBY_ENGINE "ruby"
#endif

#if WITH_OBJC
# define MACRUBY_VERSION 0.4
# if defined(__LP64__)
#   if BYTE_ORDER == BIG_ENDIAN
#     define RUBY_ARCH "ppc64"
#   else
#     define RUBY_ARCH "x86_64"
#   endif
# else
#   if BYTE_ORDER == BIG_ENDIAN
#     define RUBY_ARCH "ppc"
#   else
#     define RUBY_ARCH "i386"
#   endif
# endif
# define RUBY_DESCRIPTION	    \
    "MacRuby version " STRINGIZE(MACRUBY_VERSION) \
    " (ruby "RUBY_VERSION	    \
    ") ["RUBY_PLATFORM", "RUBY_ARCH"]"
#else
# define RUBY_DESCRIPTION	    \
    "ruby "RUBY_VERSION		    \
    " ("RUBY_RELEASE_DATE" "	    \
    RUBY_RELEASE_STR" "		    \
    STRINGIZE(RUBY_RELEASE_NUM)") " \
    "["RUBY_PLATFORM"]"
#endif
# define RUBY_COPYRIGHT 	    \
    "ruby - Copyright (C) "	    \
    STRINGIZE(RUBY_BIRTH_YEAR)"-"   \
    STRINGIZE(RUBY_RELEASE_YEAR)" " \
    RUBY_AUTHOR
