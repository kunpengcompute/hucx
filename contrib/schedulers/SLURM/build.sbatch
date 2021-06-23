#!/bin/sh
#SBATCH --job-name=UCX-build
#SBATCH --ntasks=1
#SBATCH --time=02:00:00
#SBATCH --output=ucx_build.log

echo "SLURM Build started on" $SLURM_JOB_NODELIST

# Collect host information
echo Starting $SLURM_SUBMIT_DIR/contrib/schedulers/common/info.sh
cmd="/bin/bash --login $SLURM_SUBMIT_DIR/contrib/schedulers/common/info.sh"
ssh -tt $SLURM_JOB_NODELIST $cmd

# Execute the build script on the chosen machine
echo Starting $SLURM_SUBMIT_DIR/contrib/schedulers/common/info.sh
cmd="/bin/bash --login $SLURM_SUBMIT_DIR/contrib/schedulers/common/build.sh"
ssh -tt $SLURM_JOB_NODELIST $cmd

# Execute a minimal array of performance tests
echo Starting $SLURM_SUBMIT_DIR/contrib/schedulers/common/osu.py
. $SLURM_SUBMIT_DIR/contrib/schedulers/common/common.sh # For $BUILD_PATH
cmd="/bin/bash --login BUILD_PATH=$BUILD_PATH `which python3` $SLURM_SUBMIT_DIR/contrib/schedulers/common/osu.py localhost 2 $BUILD_PATH"
ssh -tt $SLURM_JOB_NODELIST $cmd

echo "DONE."
