#!/bin/bash


REVISION="5173"
NAME="ksmppd"
SVN=`which svn`
PATCH=`which patch`
CURL=`which curl`

function safe_exec {
    if ! $1 ; then
        echo "Fatal error occurred executing $1"       
        exit
    fi
}


if [ -z "$SVN" ]; then
    echo "You need svn on your \$PATH to build $NAME"
    exit 1;
fi

if [ -z "$PATCH" ]; then
    echo "You need patch on your \$PATH to build $NAME"
    exit 1;
fi

if [ -z "$CURL" ]; then
    echo "You need curl on your \$PATH to build $NAME"
    exit 1;
fi

CMD="$SVN checkout -r $REVISION https://svn.kannel.org/gateway/trunk kannel-svn-trunk"

safe_exec "$CMD"

BUILD_PATH=`pwd`/build
BUILD_PATH=$(printf %q "$BUILD_PATH")

mkdir -p $BUILD_PATH

cd kannel-svn-trunk

safe_exec "./configure --prefix=$BUILD_PATH --with-mysql --enable-ssl --enable-start-stop-daemon"

patch -p0 < "../kannel-svn-r${REVISION}.patch"

safe_exec "make libgw.a libgwlib.a libwap.a gw-config"

cd $BUILD_PATH
safe_exec "curl -O -L ftp://ftp.gnu.org/gnu/shtool/shtool-2.0.8.tar.gz"

tar zxvf shtool-2.0.8.tar.gz
cd shtool-2.0.8

./configure
make

cd ..

safe_exec "curl -O -L  https://github.com/libevent/libevent/releases/download/release-2.0.22-stable/libevent-2.0.22-stable.tar.gz"

tar zxvf libevent-2.0.22-stable.tar.gz

safe_exec "cd libevent-2.0.22-stable"

safe_exec "./configure --prefix=$BUILD_PATH"

safe_exec "make"

make install

safe_exec "cd $BUILD_PATH"

ln -s libevent-2.0.22-stable libevent

safe_exec "cd libevent/.libs"

rm *.so

echo "Your environment is now ready to build ${NAME}."

echo "Run make to build."

