. $(dirname "$0")/common.sh
cd $(dirname "$0")/../../..

# UCX Cleanup, which may fail (therefore preceeds "set -e")
make distclean
find . -name "*.la" | xargs rm
git clean -f -X -d

# Test the presence/location of intended sources
set -e
touch ../ompi
touch ../osu

# Build Open UCX
git clean -f -X -d # Remove all traces of the previous build...
./autogen.sh
./contrib/configure-opt --prefix=$BUILD_PATH
make
make install

# Build Open MPI
pushd ../ompi
./autogen.pl
./configure --prefix=$BUILD_PATH --with-platform=contrib/platform/mellanox/optimized --with-ucx=$BUILD_PATH
make
make install
popd

# Build Open UCX again, due to mutual dependency - this time against Open MPI!
git clean -f -X -d # Remove all traces of the previous build...
./autogen.sh
./contrib/configure-opt  --prefix=$BUILD_PATH --with-mpi=$BUILD_PATH --with-ompi-src=`pwd`/../ompi
make
make install

# Re-build Open MPI (with the new Open UCX)
pushd ../ompi
make
make install
popd

# Build OSU
pushd ../osu
./configure --prefix=$BUILD_PATH CC=$BUILD_PATH/bin/mpicc CXX=$BUILD_PATH/bin/mpicxx
make
make install
popd
