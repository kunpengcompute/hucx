#!/bin/bash

trap "trap_error" EXIT SIGINT SIGHUP SIGILL
current_dir=$(cd "$(dirname "$0")" || exit 1; pwd)
arch=$(uname -p)
origin_output="${arch}_detail.txt"
summary_output="${arch}_summary.txt"
[ -f "$origin_output" ] && rm -f "$origin_output"
[ -f "$summary_output" ] && rm -f "$summary_output"
mkdir -p "${current_dir}/${arch}"

function usage()
{
    echo "Usage: ${0} NODE_NUMBER PROCESS_PER_NODE"
    echo "Example: ${0} 2 2"
    echo "prerequisite:"
    echo "    1. MPI is installed and configured correctly, command mpicc and mpirun is available."
    echo "    2. hostfile ${arch}_hf_NODE_NUMBER is exists in ${HOME}/hostfile"
}

if [ ${#} -ne 2 ]; then
    usage
    echo "ERROR: 2 paratemeters is required"
    exit 1
elif ! (command -v mpicc > /dev/null 2>&1 && mpicc -v > /dev/null 2>&1); then
    usage
    echo "ERROR: mpi is not available"
    exit 1
fi

NM=${1}
if [ ! -f "${HOME}/hostfile/${arch}_hf_${NM}" ]; then
    usage
    echo "ERROR: hostfile ${HOME}/hostfile/${arch}_hf_${NM} is ont exists"
    exit 1
fi

PPN=${2}
NP=$((NM * PPN))
MPI_RUN="mpirun -np ${NP} -N ${PPN} --hostfile ${HOME}/hostfile/${arch}_hf_${NM}"
MPI_OPT="-mca btl ^openib --bind-to core --map-by socket --rank-by core -x UCX_TLS=sm,ud"

if [[ $arch == "aarch64" ]]; then
    # sed -n '/ud/,+2p;h' print next 2 line after ud is found, sed 's/ //g' remove all spaces
    net_device=$(ucx_info -d | sed -n '/ud/,+2p;h' | grep Device | awk -F: '{printf"%s:%s\n", $2,$3}' | sed 's/ //g' | tail -1)
    MPI_OPT="-mca btl ^openib --bind-to core --map-by socket --rank-by core -x UCX_NET_DEVICES=${net_device} -x UCX_TLS=sm,ud"
fi

allreduce_list="1 2 3 4 5 6 7 8"
barrier_list="1 2 3 4 5 6 7"
bcast_list="1 2 3 4"

total_num=0
fail_num=0
success_num=0

function trap_error()
{
    exit_code=$?
    success_num=$((total_num - fail_num))
    echo -e "--------------------test done--------------------\n"
    echo -e "${total_num} test cases are tested and ${success_num} success\n"
    if [[ $total_num -ne $success_num ]]; then
        echo "${fail_num} failed, failed cases are:"
        for i in $(seq 0 $((fail_num - 1)))
        do
            echo "${failed_files[$i]} with parameter ${failed_alg[$i]}"
        done
    fi
    echo -e "\ndetail output is in ${origin_output}, summary output is in ${summary_output}\n"
    exit $exit_code
}

function run_with_source()
{
    local source_file=$1
    local alg_opt=$2
    echo "----------------------------------------------------------------------------------------------------" | tee -a "$origin_output"
    echo "testing ${source_file} with parameter: \"${alg_opt}\" ..." | tee -a "$summary_output"
    ((total_num++))
    local source_file_path=$(cd "$(dirname "$source_file")" || exit 1; pwd)
    local file_name=$(echo "$source_file" | awk -F/ '{print $NF}')
    exc_file="${source_file_path}/${arch}/${file_name%.*}"
    cc_cmd="mpicc -Wall -lm -o ${exc_file} ${line}"
    echo "$cc_cmd" >> "$origin_output"
    eval "$cc_cmd >> $origin_output 2>&1"
    if [ $? -ne 0 ]; then
        echo "Failed, compile ${source_file} error" | tee -a "$summary_output"
        failed_files[$fail_num]=$source_file
        failed_alg[$fail_num]="${alg_opt}"
        ((fail_num++))
        return 1
    fi

    run_cmd="${MPI_RUN} ${MPI_OPT} ${alg_opt} ${exc_file}"
    echo "$run_cmd" >> "$origin_output"
    eval "timeout 5m $run_cmd >> $origin_output 2>&1"
    err_code=$?
    if [ $err_code -ne 0 ]; then
        if [[ $err_code -eq 124 ]]; then
            echo "Failed, case test timeout, the timeout value is 5 minute" | tee -a "$summary_output"
        else
            echo "Failed, case test error" | tee -a "$summary_output"
        fi
        failed_files[$fail_num]=$source_file
        failed_alg[$fail_num]="${alg_opt}"
        ((fail_num++))
        return 2
    fi
    echo "Success" | tee -a "$summary_output"
}

function algorithm_traversal()
{
    local old_IFS=$IFS
    IFS=' '
    local source=$1
    local algorithm=$2
    # when algorithm=allreduce, command $(eval echo '$'"${algorithm}_list") is equal to $(echo $allreduce_list)
    # this statement traverses allreduce_list, bcast_list, or barrier_list based on the variable "$algorithm".
    for alg in $(eval echo '$'"${algorithm}_list")
    do
        # run the ${algorithm^^} command to change all characters to uppercase.
        local allreduce_opt="-x UCX_BUILTIN_${algorithm^^}_ALGORITHM=${alg}"
        run_with_source "$source" "$allreduce_opt"
    done
    IFS="$old_IFS"
}

function run_limit_np()
{
    local line=$1
    local largest_np_for_matrix=$2
    local largest_nm_for_matrix=$3

    if [ $NP -gt "$largest_np_for_matrix" ]; then
        limit_nm=$NM
        if [ "$limit_nm" -gt "$largest_nm_for_matrix" ]; then
            limit_nm=8
        fi
        limit_ppn=$((largest_np_for_matrix / limit_nm))
        limit_np=$((limit_nm * limit_ppn))
        echo "total process num is too large for case ${line}, use available parameters: np ${limit_np}, ppn ${limit_ppn}, node_num ${limit_nm}"  | tee -a "$summary_output"
        old_MPI_RUN=$MPI_RUN
        MPI_RUN="mpirun -np ${limit_np} -N ${limit_ppn} --hostfile ${HOME}/hostfile/${arch}_hf_${limit_nm}"
        run_with_source "$line" ""
        MPI_RUN=$old_MPI_RUN
    else
        run_with_source "$line" ""
    fi
}

# search for all files with suffix ".c", ".cc" and ".cpp " in the current directory.
files=$(find "$current_dir" -name "*.c" -o -name "*.cc" -o -name "*.cpp")
traversal_files="mpi_barrier.c mpi_allreduce_basic_002.c mpi_allreduce_basic_003.c mpi_bcast_basic_001.c"

# The variable IFS is the default separator. Set IFS for loop to separate lines.
# store variable IFS
old_IFS=$IFS
IFS='
'
largest_np_for_matrix=8
for line in $files; do
    case=$(echo "$line" | awk -F/ '{print $NF}')
    if [[ $case =~ "barrier" ]]; then
        if [[ $traversal_files =~ $case ]]; then
            algorithm_traversal "$line" "barrier"
        else
            run_with_source "$line" ""
        fi
    fi
done
if [[ $fail_num -ne 0 ]]; then
    echo "some barrier cases failed, test abort"
    exit 1
fi
for line in $files; do
    case=$(echo "$line" | awk -F/ '{print $NF}')
    if [[ $case =~ "allreduce" ]]; then
        if [[ $case == "mpi_allreduce_basic_006.c" ]]; then
            # for this case, the np is limited to 8 and the number of node is limited to 8
            run_limit_np "$line" 8 8
        elif [[ $traversal_files =~ $case ]]; then
            algorithm_traversal "$line" "allreduce"
        else
            run_with_source "$line" ""
        fi
    elif [[ $case =~ "bcast" ]]; then
        if [[ $traversal_files =~ $case ]]; then
            algorithm_traversal "$line" "bcast"
        else
            run_with_source "$line" ""
        fi
    elif [[ ! $case =~ "barrier" ]]; then
        if [[ $case == "mpi_comm_split.c" ]]; then
            # for this case, the np is limited to 256 and the number of node is limited to 8
            run_limit_np "$line" 256 8
        elif [[ $case == "mpi_comm_create.c" ]]; then
            # for this case, the np is limited to 256 and the number of node is limited to 8
            run_limit_np "$line" 256 8
        else
            run_with_source "$line" ""
        fi
    fi
done
IFS="$old_IFS"

exit 0
