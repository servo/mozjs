dnl
dnl Local autoconf macros used with mozilla
dnl The contents of this file are under the Public Domain.
dnl

builtin(include, ../../build/autoconf/hooks.m4)dnl
builtin(include, ../../build/autoconf/config.status.m4)dnl
builtin(include, ../../build/autoconf/altoptions.m4)dnl

define([__MOZ_AC_INIT_PREPARE], defn([AC_INIT_PREPARE]))
define([AC_INIT_PREPARE],
[if test -z "$srcdir"; then
  srcdir=`dirname "[$]0"`
fi
srcdir="$srcdir/../.."
__MOZ_AC_INIT_PREPARE($1)
])

dnl This won't actually read the mozconfig, but data that configure.py
dnl will have placed for us to read. Configure.py takes care of not reading
dnl the mozconfig where appropriate but can still give us some variables
dnl to read.
MOZ_READ_MOZCONFIG(.)
