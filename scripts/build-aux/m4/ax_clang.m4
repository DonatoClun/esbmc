#
# AX_CLANG([ACTION-IF-FOUND,[ACTION-IF-NOT-FOUND]])
#
# If both clang and clang++ are detected
# - Sets shell variable HAVE_CLANG='yes'
# - If ACTION-IF-FOUND is defined: Runs ACTION-IF-FOUND
# - If ACTION-IF-FOUND is undefined:
#   - Runs AC_SUBST for variables CLANG and CLANGXX, setting them to the
#     corresponding paths.
#
# If not both clang and clang++ are detected
# - Sets shell variable HAVE_CLANG='no'
# - Runs ACTION-IF-NOT-FOUND if defined
#

AC_DEFUN([AX_CLANG],
[
        AC_ARG_WITH([clang-libdir],
                AS_HELP_STRING([--with-clang-libdir=LIB_DIR],
                [Force given directory for clang libraries. Note that this will override library path detection, so use this parameter only if default library detection fails and you know exactly where your clang libraries are located.]),
                [
                if test -d "$withval"
                then
                        ac_clang_lib_path="$withval"
                else
                        AC_MSG_ERROR(--with-clang-libdir expected directory name)
                fi
                ],
                [ac_clang_lib_path=""]
        )

    clang_lib_version_req=ifelse([$1], ,3.8.0,$1)
    clang_lib_version_req_shorten=`expr $clang_lib_version_req : '\([[0-9]]*\.[[0-9]]*\)'`
    clang_lib_version_req_major=`expr $clang_lib_version_req : '\([[0-9]]*\)'`
    clang_lib_version_req_minor=`expr $clang_lib_version_req : '[[0-9]]*\.\([[0-9]]*\)'`
    
    WANT_clang_VERSION=`expr $clang_lib_version_req_major \* 100000 \+  $clang_lib_version_req_minor \* 100`
    AC_MSG_CHECKING(for clang >= $clang_lib_version_req)
    succeeded=no

    dnl On 64-bit systems check for system libraries in both lib64 and lib.
    dnl The former is specified by FHS, but e.g. Debian does not adhere to
    dnl this (as it rises problems for generic multi-arch support).
    dnl The last entry in the list is chosen by default when no libraries
    dnl are found, e.g. when only header-only libraries are installed!
    libsubdirs="lib"
    ax_arch=`uname -m`
    case $ax_arch in
      x86_64)
        libsubdirs="lib64 libx32 lib lib64"
        ;;
      ppc64|s390x|sparc64|aarch64|ppc64le)
        libsubdirs="lib64 lib lib64 ppc64le"
        ;;
    esac

    dnl allow for real multi-arch paths e.g. /usr/lib/x86_64-linux-gnu. Give
    dnl them priority over the other paths since, if libs are found there, they
    dnl are almost assuredly the ones desired.
    AC_REQUIRE([AC_CANONICAL_HOST])
    libsubdirs="lib/${host_cpu}-${host_os} $libsubdirs"

    case ${host_cpu} in
      i?86)
        libsubdirs="lib/i386-${host_os} $libsubdirs"
        ;;
    esac

    _version=0
    dnl first we check the system location for clang libraries and version
    if test "$ac_clang_lib_path" != ""; then
        clang_includes_path=$ac_clang_lib_path/include/clang
        for libsubdir in $libsubdirs ; do
            if ls "$ac_clang_lib_path/$libsubdir/libclang"* >/dev/null 2>&1 ; then break; fi
        done

        for i in `ls -d $ac_clang_lib_path/$libsubdir/libclang.so.* 2>/dev/null`; do
            _version_tmp=`echo $i | sed "s#$ac_clang_lib_path/$libsubdir/##" | sed 's/libclang.so.//'`
            V_CHECK=`expr $_version_tmp \> $_version`
            if test "$V_CHECK" != "1" ; then
                    continue
            fi

            _version=$_version_tmp
            succeeded=yes

            clang_libs_path=$ac_clang_lib_path/$libsubdir
            break;
        done
    elif test "$cross_compiling" != yes; then
        for ac_clang_lib_path_tmp in /usr /usr/local /opt /opt/local ; do
            if test -d "$ac_clang_lib_path_tmp/include/clang" && test -r "$ac_clang_lib_path_tmp/include/clang"; then
                for libsubdir in $libsubdirs ; do
                    if ls "$ac_clang_lib_path_tmp/$libsubdir/libclang"* >/dev/null 2>&1 ; then break; fi
                done

                for i in `ls -d $ac_clang_lib_path_tmp/$libsubdir/libclang.so.* 2>/dev/null`; do
                    _version_tmp=`echo $i | sed "s#$ac_clang_lib_path_tmp/$libsubdir/##" | sed 's/libclang.so.//'`
                    V_CHECK=`expr $_version_tmp \> $_version`
                    if test "$V_CHECK" != "1" ; then
                            continue
                    fi

                    _version=$_version_tmp
                    succeeded=yes

                    clang_libs_path=$ac_clang_lib_path_tmp/$libsubdir
                    clang_includes_path=$ac_clang_lib_path_tmp/include
                    break;
                done
            fi
        done
    fi

    if test "$succeeded" != "yes" ; then
        AC_MSG_RESULT(no)
        ifelse([$3], , :, [$3])
    else
        AC_MSG_RESULT(yes)
    fi

    AC_MSG_CHECKING(if we can find libclangTooling)
    if ls "$clang_libs_path/libclangTooling"* >/dev/null 2>&1 ; then
         clang_LIBS="-lclangTooling"
         AC_MSG_RESULT(yes)
    else
         AC_MSG_NOTICE([Can't find libclangTooling])
         ifelse([$3], , :, [$3])
    fi

    AC_MSG_CHECKING(if we can find libclangDriver)
    if ls "$clang_libs_path/libclangDriver"* >/dev/null 2>&1 ; then
         clang_LIBS="$clang_LIBS -lclangDriver"
         AC_MSG_RESULT(yes)
    else
         AC_MSG_NOTICE([Can't find libclangDriver])
         ifelse([$3], , :, [$3])
    fi

    AC_MSG_CHECKING(if we can find libclangFrontend)
    if ls "$clang_libs_path/libclangFrontend"* >/dev/null 2>&1 ; then
         clang_LIBS="$clang_LIBS -lclangFrontend"
         AC_MSG_RESULT(yes)
    else
         AC_MSG_NOTICE([Can't find libclangFrontend])
         ifelse([$3], , :, [$3])
    fi

    AC_MSG_CHECKING(if we can find libclangParse)
    if ls "$clang_libs_path/libclangParse"* >/dev/null 2>&1 ; then
         clang_LIBS="$clang_LIBS -lclangParse"
         AC_MSG_RESULT(yes)
    else
         AC_MSG_NOTICE([Can't find libclangParse])
         ifelse([$3], , :, [$3])
    fi

    AC_MSG_CHECKING(if we can find libclangSerialization)
    if ls "$clang_libs_path/libclangSerialization"* >/dev/null 2>&1 ; then
         clang_LIBS="$clang_LIBS -lclangSerialization"
         AC_MSG_RESULT(yes)
    else
         AC_MSG_NOTICE([Can't find libclangSerialization])
         ifelse([$3], , :, [$3])
    fi

    AC_MSG_CHECKING(if we can find libclangSema)
    if ls "$clang_libs_path/libclangSema"* >/dev/null 2>&1 ; then
         clang_LIBS="$clang_LIBS -lclangSema"
         AC_MSG_RESULT(yes)
    else
         AC_MSG_NOTICE([Can't find libclangSema])
         ifelse([$3], , :, [$3])
    fi

    AC_MSG_CHECKING(if we can find libclangAnalysis)
    if ls "$clang_libs_path/libclangAnalysis"* >/dev/null 2>&1 ; then
         clang_LIBS="$clang_LIBS -lclangAnalysis"
         AC_MSG_RESULT(yes)
    else
         AC_MSG_NOTICE([Can't find libclangAnalysis])
         ifelse([$3], , :, [$3])
    fi

    AC_MSG_CHECKING(if we can find libclangEdit)
    if ls "$clang_libs_path/libclangEdit"* >/dev/null 2>&1 ; then
         clang_LIBS="$clang_LIBS -lclangEdit"
         AC_MSG_RESULT(yes)
    else
         AC_MSG_NOTICE([Can't find libclangEdit])
         ifelse([$3], , :, [$3])
    fi

    AC_MSG_CHECKING(if we can find libclangLex)
    if ls "$clang_libs_path/libclangLex"* >/dev/null 2>&1 ; then
         clang_LIBS="$clang_LIBS -lclangLex"
         AC_MSG_RESULT(yes)
    else
         AC_MSG_NOTICE([Can't find libclangLex])
         ifelse([$3], , :, [$3])
    fi

    AC_MSG_CHECKING(if we can find clangAST)
    if ls "$clang_libs_path/libclangAST"* >/dev/null 2>&1 ; then
         clang_LIBS="$clang_LIBS -lclangAST"
         AC_MSG_RESULT(yes)
    else
         AC_MSG_NOTICE([Can't find libclangAST])
         ifelse([$3], , :, [$3])
    fi

    AC_MSG_CHECKING(if we can find libclangBasic)
    if ls "$clang_libs_path/libclangBasic"* >/dev/null 2>&1 ; then
         clang_LIBS="$clang_LIBS -lclangBasic"
         AC_MSG_RESULT(yes)
    else
         AC_MSG_NOTICE([Can't find libclangBasic])
         ifelse([$3], , :, [$3])
    fi

    clang_CPPFLAGS="-I$clang_includes_path"
    clang_LDFLAGS="-L$clang_libs_path"

    CPPFLAGS_SAVED="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $clang_CPPFLAGS"
    export CPPFLAGS

    LDFLAGS_SAVED="$LDFLAGS"
    LDFLAGS="$LDFLAGS $clang_LDFLAGS"
    export LDFLAGS

    LIBS_SAVED="$LIBS"
    LIBS="$LIBS $clang_LIBS"
    export LIBS

    if test "$succeeded" != "yes" ; then
        if test "$_version" = "0" ; then
            AC_MSG_NOTICE([[We could not detect the clang libraries (version $clang_lib_version_req or higher). If you have a staged clang library (still not installed) please specify \$CLANG_ROOT in your environment and do not give a PATH to --with-clang option.]])
        else
            CPPFLAGS="$CPPFLAGS_SAVED"
            LDFLAGS="$LDFLAGS_SAVED"
            LIBS="$LIBS_SAVED"

            # execute ACTION-IF-NOT-FOUND (if present):
            ifelse([$3], , :, [$3])
        fi
    else
        AC_SUBST(clang_CPPFLAGS)
        AC_SUBST(clang_LDFLAGS)
        AC_SUBST(clang_LIBS)
        AC_DEFINE(HAVE_clang,,[define if the clang library is available])
        # execute ACTION-IF-FOUND (if present):
        ifelse([$2], , :, [$2])
    fi
])
