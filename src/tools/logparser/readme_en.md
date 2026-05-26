## 1. Tool Overview 

1.  `rc_ud_timeout_parser`: Parses the timeout logs of the Unreliable Datagram (UD) and Reliable Connection (RC) types generated during MPI communication.
2.  `ucp_timeout_parser`: Parses the sending and receiving timeout logs of the Unified Communication Programming (UCP) type generated during MPI communication.

## 2. Dependency 

1.  **Running environment: Python 2.7 or later must be installed. Both scripts are compatible with Python 2 and Python 3.
2.  **Required tools:**
    
     *  To parse the GID (used to locate InfiniBand/Ethernet devices), ensure that the local and target nodes are`ssh`Login without password.
     *  (Optional) Installing the `ibv_devinfo` tool to parse InfiniBand device information.
	 


## 3. Parameter Description ##

The two tools use the same command parameter set. The parameters are described as follows:

| Parameters           | shorthand | Meaning                                                                                                                  | Value Range/Default Value                                                                       |
| -------------------- | --------- | ------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------- |
| `--file`             | `-f`      | Single MPI log file path (priority is higher than directory)                                                             | Local file path, which must exist and be valid.                                                 |
| `--dir`              | `-d`      | MPI Log Directory Path (by`mpirun --output-filename`Generated)                                                           | Local directory path, which must exist and be valid.                                            |
| `--output`           | `-o`      | Parsing result output directory (The result will be written to the`output.log`)                                          | Local directory path, which is automatically created when the local directory does not exist.   |
| `--upper`            | `-u`      | Maximum number of displayed control nodes and devices, sorted by error number.                                           | Value range: 5--500000. Default value: 5                                                        |
| `--log_level`        | `-l`      | Log Level                                                                                                                | 1 (ERR, output only error information) 2 (DEBUG, output debugging information) Default value: 1 |
| `--skip_gid_parsing` | `-s`      | Skip GID resolution (Reduces the analysis time and applies to scenarios where accurate device locating is not required.) | No parameter. The parameter takes effect after being added.                                     |

> Note: You must specify`--file`or the`--dir`at least one of (if both are specified,`--file`Take effect first)

## 4. Functions and Examples ##

### 4.1`rc_ud_timeout_parser` ###

#### Function Description ####

 *  Parsing timeout Log of Unreliable Datagram (UD) 
 *  Parsing timeout Log of Reliable Connection (RC)
 *  Extracts key information such as local and remote node information, device name, and GID/LID.
 *  Displays the distribution of timeout errors on nodes and devices.
 *  Provides node fault rectification and parameter adjustment suggestions.

#### Use Example ####

**Example 1: Parsing a Single UD/RC Log File**

```bash
python rc_ud_timeout_parser.py -f /path/to/ud_rc_log.txt
```

**Example 2: Parse the log directory and specify the output directory.**

```bash
python rc_ud_timeout_parser.py -d /path/to/mpi_log_dir -o /path/to/output_dir
```

**Example 3: Debug Mode Parsing and Skipping GID Parsing**

```bash
python rc_ud_timeout_parser.py -f /path/to/ud_rc_log.txt -l 2 -s -u 10
```

### 4.2`ucp_timeout_parser` ###

#### Function Description ####

 *  Parsing Unified Communication Programming (UCP) Sending Timeout Logs
 *  Parsing Unified Communication Programming (UCP) Receiving Timeout Logs
 *  Analyze the dependency between _ranks based on the communication domain.
 *  Identify the processes and communication links that may be abnormal.
 *  Provides communication fault locating and resource competition analysis.

#### Use Example ####

**Example 1: Parsing a UCP Log File**

```bash
python ucp_timeout_parser.py -f /path/to/ucp_log.txt
```

**Example 2: Parse the log directory and specify the output upper limit.**

```bash
python ucp_timeout_parser.py -d /path/to/mpi_log_dir -u 20
```

**Example 3: Output the result to a specified file.**

```bash
python ucp_timeout_parser.py -f /path/to/ucp_log.txt -o /path/to/results
```

## 5. Output Description ##

### 5.1`rc_ud_timeout_parser`Output ###

The parsing result contains the following information:

1.  **Timeout type ID: specifies whether the timeout type is UD or RC.**
2.  **Brief information: brief relationships between involved nodes and devices**
3.  **Detailed statistics:**
    
     *  List of nodes involved in the timeout (in descending order of number of errors)
     *  List of devices involved in each node (in descending order of errors)
4.  **Fault suggestion:**
    
     *  Suggestions on Troubleshooting Node Faults
     *  Link delay optimization suggestions (including UCX parameter adjustment)
     *  GID parsing failure prompt and manual parsing method
5.  The parsing result is displayed in the format of "Timeout type + Statistics + Fault suggestion". The following is an example of the output:

```
* UCT UD TIMEOUT 
* Brief Exception Information 
**
local:node01:mlx5_0 -> remote:node02:00:02
local:node01:mlx5_0 -> remote:node03:00:03
* Detailed Exception Information 
**
Timeout involves 3 node(s)(list up to 5): [node01(12), node02(8), node03(5)]
node node01 involves about 2 dev(s)(list up to 5): [mlx5_0(10), mlx5_1(2)]
node node02 involves about 1 dev(s)(list up to 5): [00:02(8)]
node node03 involves about 1 dev(s)(list up to 5): [00:03(5)]
**
This shows the part of the nodes where the timeout log is located, the number of errors is in descending order: 
	If the number of errors on the head node is much greater than that on the subsequent nodes, there is a high probability that the node is faulty. In this case, you can locate the fault on the device of the node.
	If the number of node errors is evenly distributed, link layer may be slow. In this case, You can increase the timeout interval(UCX_UD_TIMEOUT/UCX_UD_TIMER_BACKOFF/UCX_UD_TIMER_TICK), or increase the queue depth(UCX_UD_TX_QUEUE_LEN/UCX_UD_RX_QUEUE_LEN) properly.
*
```

Key information:

 *  **Timeout type**: The first line is marked as`UCT UD TIMEOUT`(UD type timeout) or`UCT RC TIMEOUT`(RC type timeout).
 *  **Brief relationship**: Displays the communication timeout relationship between local nodes/devices and remote nodes/devices (e.g.`node01:mlx5_0 -> node02:00:02`).
 *  **Detailed Statistics**: Sort nodes and devices in descending order of the number of errors. The format is as follows:`node name (number of errors)` and `device name (number of errors)`.
 *  **Fault suggestion**: Provide specific suggestions based on the distribution, such as node fault rectification or UCX parameter adjustment (e.g.`UCX_UD_TIMEOUT`).

### 5.2`ucp_timeout_parser`Output ###

The parsing results include:

1.  **Timeout type ID: specifies whether the UCP sends or receives a message that times out.**
2.  **Communication domain information: Displays analysis results by communication domain.**
3.  **Process dependency analysis:**
    
     *  Identifying the processes that may have abnormalities
     *  Inter-Process communication direction and dependency relationship
     *  _rank pairs involved in communication timeout
4.  **Fault suggestion:**
    
     *  Troubleshooting suggestions for abnormal processes
     *  Link layer problem analysis
     *  Judging resource competition

5. The parsing result is displayed in the structure of "communication domain + process dependency + exception analysis". The following is an output example:

```
* Brief Exception Information 
* UCP TIMEOUT 
rank1(node02) may be abnormal processes.
rank3(node04) may be abnormal processes.
*
* Detailed Exception Information 
* UCP RECV TIMEOUT 
** comm domain 0x7f8a00000000
rank1(node02) <- rank3(node04)
rank5(node06) <- rank3(node04)
* UCP SEND TIMEOUT 
rank0(node01) -> rank1(node02)
rank2(node03) -> rank1(node02)
```

Key information:

 *  **Abnormal process: Identify the process that may be abnormal, for example,**`rank1(node02) may be abnormal processes`).
 *  **Communication domain: Displayed by communication domain (e.g.`comm domain 0x7f8a00000000`), which helps distinguish different communication contexts.
 *  **Process Dependency:**`<-`Indicates the receiving timeout (e.g.`rank5 <- rank3`rank5 receiving from rank3 times out) and use the`->`Indicates that the sending times out (for example,`rank0 -> rank1`It indicates that the sending of rank 0 to rank 1 times out.
 *  **Fault prompt: If no abnormal process is found, a message is displayed indicating possible link layer problems or resource competition. (For example, "The problem may occur at the link layer or resource waiting").**

