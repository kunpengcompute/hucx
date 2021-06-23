MPIRUN=$OPT_PATH/bin/mpirun
MPI_ARGS=--display-map -bind-to core -map-by core
OSU_ARGS=-f
TIMER=/usr/bin/time

echo "MPI version:"
echo "============"
echo `$MPIRUN --version`
echo "CC       =" $CC
echo "CXX      =" $CXX
echo "OPT_PATH =" $OPT_PATH
echo "MPIRUN   =" $MPIRUN
echo "MPI_ARGS =" $MPI_ARGS
echo "OSU_ARGS =" $OSU_ARGS

# OSU
# ===
$TIMER $MPIRUN $MPI_ARGS osu_barrier    $OSU_ARGS
$TIMER $MPIRUN $MPI_ARGS osu_reduce     $OSU_ARGS
$TIMER $MPIRUN $MPI_ARGS osu_allreduce  $OSU_ARGS
$TIMER $MPIRUN $MPI_ARGS osu_iallreduce $OSU_ARGS

# ExaMiniMD
# =========
# Parameters example:
#       ExaMiniMD/LAMMPS units=lj; nx, ny, nz=100; Timestep=0.005; Run=18000 (single- and multinode)
#       ExaMiniMD units=SNAP; nx, ny, nz=100; Timestep=0.005; Run=18000 (single-node)
#
# Description:
# ExaMiniMD is a proxy application and research vehicle for Molecular Dynamics (MD) applications
# such as LAMMPS. ExaMiniMD is being used in the ECP Co-design Center for Particle Applications
# (CoPA) and in the ECP Ristra project, which is an ATDM code project at LANL. LAMMPS is
# being used in the ECP Molecular Dynamics at the Exascale with EXAALT (EXascale Atomistics
# for Accuracy, Length and Time) project.
#
# README command-line samples:
# To run 2 MPI tasks, with 12 threads per task: mpirun -np 2 -bind-to socket ./ExaMiniMD -il ../input/in.lj --comm-type MPI --kokkos-threads=12
# To run 2 MPI tasks, with 1 GPU per task: mpirun -np 2 -bind-to socket ./ExaMiniMD -il ../input/in.lj --comm-type MPI --kokkos-ndevices=2
# To run in serial, writing binary output every timestep to ReferenceDir: ./ExaMiniMD -il ../input/in.lj --kokkos-threads=1 --binarydump 1 ReferenceDir
# To run in serial with 2 threads, checking correctness every timestep against ReferenceDir: ./ExaMiniMD -il ../input/in.lj --kokkos-threads=2 --correctness 1 ReferenceDir correctness.dat
spack stage examinimd # Step #1/2 for making "input" available
spack cd examinimd    # Step #2/2 for making "input" available
spack load examinimd  # Make the executable available via $PATH
$TIMER $MPIRUN `which ExaMiniMD` -il `pwd`/input/in.lj --comm-type MPI

# NEKbone
# =======
# Parameters Example:
#       Nekbone: Dim=3; polynomial order=8; spectral multigrid=off max local elements per MPI rank=300
#       Nek5000: eddy uv, with Dim=3; polynomial order=8 max local elements per MPI rank=300
#
# Description:
# Nekbone is a proxy app for Nek5000, which is a spectral element code designed for large eddy simulation (LES) and direct
# numerical simulation (DNS) of turbulence in complex domains. Nek5000 and Nekbone are being used in several ECP projects
# including Multiscale Coupled Urban Systems, ExaSMR, and the Center for Efficient Exascale Descretizations (CEED).
spack load ecp-proxy-apps
spack cd ecp-proxy-apps
$TIMER $MPIRUN

# SWFFT
# =====
# Parameters Example: n_repetitions=100; ngx=1024
#
# Description:
# SWFFT is the 3D, distributed memory, discrete fast Fourier transform from the Hardware Accelerated Cosmology Code (HACC).
# SWFFT and HACC are used in the ECP ExaSky project. The main SWFFT build is an MPI implementation.
# There is also an MPI+OpenMP build that uses the openMP version of the fftw3 library. Currently HACC has three total copies of the
# double complex grid, two of which do the out-of-place backward transform. SWFFT replicates the
# transform and is representative of the computation and communication involved.
# The main parameters to SWFFT are:
# • n repetitions: number of repetitions of FFT.
# • ngx: Number of grid vertexes along one side. Should be a number that is near the cube root of (∼3.5% of total RAM/16) with small prime factors.
# • ngy ngz: optional to run non-cubic DFFT (HACC does not use this feature but useful in creating representative problem space).
# • Python code available (also under development) to suggest grid sizes based off total RAM. The application will scale with the size of the double complex grid
spack load swfft # Make the executable available via $PATH
$TIMER $MPIRUN ./swfft/bin/TestDfft 100 1024


# OpenFoam (motorcycle benchmark)
# ===============================
echo "OpenFoam status: $?"

switch ($1)
	case "install")
		# Install Open UCX
		git clone https://github.com/alex--m/ucx
		pushd ucx
		git checkout topic/ucc
		./autogen.sh
		./contrib/configure-opt --prefix=`pwd`/build --enable-fault-tolerance
		make
		make install
		popd
		
		# Install Open MPI
		git clone https://github.com/alex--m/ompi
		pushd ompi
		git checkout topic/coll_ucx
		./autogen.pl
		./configure --with-platform=contrib/platform/mellanox/optimized --with-ucx=`pwd`/../ucx/build --prefix=`pwd`/build
		make
		make install

		# Create a Spack package for the custom OMPI+UCX 
		echo > ~/.spack/packages.yaml
packages:
  openmpi:
    buildable: False
    paths:
      openmpi@4.1.0 %gcc@7.3.0 arch=linux-ubuntu18.04-x86_64: /home/alex/workspace/ompi/build
EOF
		popd # Open MPI
		

		
		# Install the custom OMPI+UCX via Spack
		spack repo create myrepo coll_ucx.comp
		spack repo add ./myrepo
		spack install openmpi 
		
		# Install the benchmark applications
		spack install osu
		spack install ecp-proxy-apps
		spack install openfoam-com

	case "osu")

esac
