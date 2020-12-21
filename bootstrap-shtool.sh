set -e

curl -O -L "ftp://ftp.gnu.org/gnu/shtool/shtool-2.0.8.tar.gz"

tar zxvf shtool-2.0.8.tar.gz
cd shtool-2.0.8

./configure
make

sudo make install
