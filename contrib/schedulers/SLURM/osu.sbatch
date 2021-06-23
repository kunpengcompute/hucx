#!/bin/sh
#SBATCH --job-name=UCX-OSU-Benchmark
#SBATCH --time=04:00:00
#SBATCH --output=ucx_osu.log

echo "SLURM benchmarking with OSU started on" $SLURM_JOB_NODELIST

cd $SLURM_SUBMIT_DIR # Assume it is the top-dir of UCX
. ./contrib/schedulers/common/common.sh

export NODES=$(hostlist -d -e -s, $SLURM_JOB_NODELIST)
export MAX_PPN=$SLURM_CPUS_ON_NODE

if [ -z "$BUILD_PATH" ]
then
	echo "WARNING: \$BUILD_PATH is empty - setting to \$HOME/build"
	export BUILD_PATH=$HOME/build
fi

cmd="/usr/bin/time `which python3` ./contrib/schedulers/common/osu.py $NODES $MAX_PPN $BUILD_PATH $OSU_TIMEOUT_SECONDS"
echo $cmd
$cmd

echo "DONE."
