## 一、工具简介

1. `rc_ud_timeout_parser`：解析MPI通信中产生的UD（Unreliable Datagram）和RC（Reliable Connection）类型的超时日志
2. `ucp_timeout_parser`：解析MPI通信中产生的UCP（Unified Communication Programming）类型的发送/接收超时日志


## 二、依赖

1. **运行环境**：需安装Python 2.7及以上版本（两个脚本均兼容Python 2和Python 3）
2. **依赖工具**：
   - 若需解析GID（用于定位InfiniBand/Ethernet设备），需确保本地与目标节点的`ssh`无密码登录
   - 解析InfiniBand设备信息需安装`ibv_devinfo`工具（可选）


## 三、命令参数说明

两个工具使用相同的命令参数集，参数说明如下：

| 参数                | 简写 | 含义                                                                 | 取值范围/默认值                                  |
|---------------------|------|----------------------------------------------------------------------|-------------------------------------------------|
| `--file`            | `-f` | 单个MPI日志文件路径（优先级高于目录）                               | 本地文件路径，需存在且为有效文件                  |
| `--dir`             | `-d` | MPI日志目录路径（由`mpirun --output-filename`生成）                 | 本地目录路径，需存在且为有效目录                  |
| `--output`          | `-o` | 解析结果输出目录（结果将写入该目录下的`output.log`）                 | 本地目录路径，不存在时会自动创建                  |
| `--upper`           | `-u` | 列表输出上限（控制节点、设备信息的展示数量，按错误数排序）           | 范围：5~500000，默认值：5                       |
| `--log_level`       | `-l` | 日志级别                                                             | 1（ERR，仅输出错误信息）、2（DEBUG，输出调试信息），默认值：1 |
| `--skip_gid_parsing`| `-s` | 跳过GID解析（减少分析时间，适用于无需精确设备定位的场景）             | 无参数，添加该参数即生效                        |

> 注意：必须指定`--file`或`--dir`中的至少一个（若同时指定，`--file`优先生效）


## 四、功能与使用示例

### 4.1 `rc_ud_timeout_parser`

#### 功能说明
- 解析UD（Unreliable Datagram）类型超时日志
- 解析RC（Reliable Connection）类型超时日志
- 提取本地/远程节点信息、设备名称、GID/LID等关键信息
- 统计超时错误在节点和设备上的分布情况
- 提供节点故障排查和参数调整建议

#### 使用示例

**示例1：解析单个UD/RC日志文件**
```bash
python rc_ud_timeout_parser.py -f /path/to/ud_rc_log.txt
```

**示例2：解析日志目录并指定输出目录**
```bash
python rc_ud_timeout_parser.py -d /path/to/mpi_log_dir -o /path/to/output_dir
```

**示例3：调试模式解析并跳过GID解析**
```bash
python rc_ud_timeout_parser.py -f /path/to/ud_rc_log.txt -l 2 -s -u 10
```


### 4.2 `ucp_timeout_parser`

#### 功能说明
- 解析UCP（Unified Communication Programming）发送超时日志
- 解析UCP（Unified Communication Programming）接收超时日志
- 基于通信域（comm domain）分析_rank间的依赖关系
- 识别可能存在异常的进程和通信链路
- 提供通信故障定位和资源竞争分析

#### 使用示例

**示例1：解析单个UCP日志文件**
```bash
python ucp_timeout_parser.py -f /path/to/ucp_log.txt
```

**示例2：解析日志目录并指定输出上限**
```bash
python ucp_timeout_parser.py -d /path/to/mpi_log_dir -u 20
```

**示例3：将结果输出到指定文件**
```bash
python ucp_timeout_parser.py -f /path/to/ucp_log.txt -o /path/to/results
```


## 五、输出结果说明

### 5.1 `rc_ud_timeout_parser` 输出

解析结果主要包含：
1. **超时类型标识**：明确标识是UD还是RC类型超时
2. **概要信息**：涉及的节点/设备简要关系
3. **详细统计**：
   - 超时涉及的节点列表（按错误数量降序）
   - 每个节点涉及的设备列表（按错误数量降序）
4. **故障建议**：
   - 节点故障排查建议
   - 链路延迟优化建议（包括相关UCX参数调整）
   - GID解析失败提示及手动解析方法

5. 解析结果以"超时类型+统计信息+故障建议"的结构呈现，以下是输出示例：

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

关键信息说明：
- **超时类型**：首行明确标识为`UCT UD TIMEOUT`（UD类型超时）或`UCT RC TIMEOUT`（RC类型超时）。
- **简要关系**：展示本地节点/设备与远程节点/设备的通信超时关系（如`node01:mlx5_0 -> node02:00:02`）。
- **详细统计**：按错误数量降序排列节点及设备，格式为`节点名(错误数)`和`设备名(错误数)`。
- **故障建议**：根据分布情况给出针对性建议，如节点故障排查或UCX参数调整（如`UCX_UD_TIMEOUT`）。



### 5.2 `ucp_timeout_parser` 输出

解析结果主要包含：
1. **超时类型标识**：明确标识是UCP发送还是接收超时
2. **通信域信息**：按通信域（comm domain）展示分析结果
3. **进程依赖分析**：
   - 可能存在异常的进程识别
   - 进程间通信方向和依赖关系
   - 通信超时涉及的_rank对
4. **故障建议**：
   - 异常进程排查建议
   - 链路层问题分析
   - 资源竞争情况判断

5.解析结果以"通信域+进程依赖+异常分析"的结构呈现，以下是输出示例：

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

关键信息说明：
- **异常进程**：直接标识可能异常的进程（如`rank1(node02) may be abnormal processes`）。
- **通信域**：按通信域（如`comm domain 0x7f8a00000000`）分类展示，便于区分不同通信上下文。
- **进程依赖**：用`<-`表示接收超时（如`rank5 <- rank3`意为rank5从rank3接收超时），用`->`表示发送超时（如`rank0 -> rank1`意为rank0向rank1发送超时）。
- **故障提示**：若未发现明显异常进程，会提示可能的链路层问题或资源竞争（如"问题可能出现在链路层或资源等待"）。
