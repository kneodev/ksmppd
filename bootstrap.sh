set -e

autoreconf -iv

./bootstrap-shtool.sh

echo "Bootstrapping done, you can now run ./configure"
