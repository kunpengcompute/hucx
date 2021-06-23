. $(dirname "$0")/common.sh

echo "BUILD_PATH:" $BUILD_PATH
echo "CC:" $CC
echo "CC VERSION:" `$CC --version`
echo "CXX:" $CXX
echo "CXX VERSION:" `$CXX --version`
echo "RELEASE:" ; cat /etc/*release
echo "KERNEL:" ; uname -a
echo "CPU:" ; cat /proc/cpuinfo
echo "MEMORY:" ; cat /proc/meminfo
echo "NET DEVICES:" ; ibv_devinfo
echo "MODULES:" ; module avail
echo "ENVIRONMENT:" ; env
