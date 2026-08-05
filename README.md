# Four-core Data Coalescing System and Simplified GPU

本工程实现一个用于研究 GPU Data/Memory Coalescing 行为的四核系统。四个简化核各自
独立产生 32-bit load/store 请求，共享一个 Coalescer；Coalescer 按 32-byte memory
segment 合并当前已经到达的请求，再将读响应路由回对应核心。

工程同时提供一个可编程的四 Lane 简化 GPU。四个 Lane 共享 PC 和指令流，各自拥有
8 个 32-bit 寄存器，通过最小整数 ISA 执行 ALU、地址生成、load 和 store。四个 Lane
执行同一条访存指令时生成四个独立地址，再由同一个 Coalescer 合并。

## 项目边界

工程保留两个顶层，用于区分两种研究模型：

- `data_coalescing_system`：四个独立访存流发生器，用于隔离研究请求到达时序；
- `simple_gpu_system`：一个共享 PC 的四 Lane SIMT 执行簇，用于运行简化 GPU 程序。

独立访存流顶层中的每个核实现：

- 可配置的起始地址、stride、请求数量和读写类型；
- 独立 `valid/ready` 请求握手；
- 一次一个 outstanding request；
- 等待并接收属于自己的内存响应；
- 完成计数、`busy/done` 和最后一次读数据。

这些流发生器没有 ALU、指令集和通用流水线。新增的 SIMT 顶层则补充共享取指、译码、
四 Lane ALU、寄存器堆和地址生成，使系统可以完成“执行程序—生成地址—合并访存—写回
结果”的闭环。

## 架构

```text
 simple core 0 -> request buffer 0 --\
 simple core 1 -> request buffer 1 ---+--> group/select --> 32B memory port
 simple core 2 -> request buffer 2 ---+         |                 |
 simple core 3 -> request buffer 3 --/          |                 v
                                                +--- response routing
                                                      |  |  |  |
                                                     c0 c1 c2 c3
```

四个核可以在不同周期开始和结束请求流。Coalescer 为每核提供一个 one-entry request
buffer，并执行以下策略：

1. 选择当前已经缓冲的最低编号请求作为 seed；
2. 比较其他 buffer 的 `{32B segment address, load/store type}`；
3. 同 segment、同操作类型的请求组成一个 transaction；
4. store 合并 32-bit byte enable 和 8x32-bit write data；
5. load 返回后按地址 `[4:2]` 选择 word，并按 core mask 路由；
6. 不主动等待未来周期可能到达的请求，避免为提高合并率引入不确定等待时间。

这种设计保留了独立请求的语义，也能直接观察请求到达时序对合并率的影响。

## 简化 GPU

```text
              instruction memory
                      |
                shared PC/decode
                      |
           +----------+----------+
           |          |          |
        lane 0      lane 1     lane 2     lane 3
        8 x GPR     8 x GPR    8 x GPR    8 x GPR
        ALU/AGU     ALU/AGU    ALU/AGU    ALU/AGU
           |          |          |          |
           +----------+----------+----------+
                      |
                  Coalescer
                      |
                32-byte memory port
```

四个 Lane 严格执行相同 PC 的指令，目前不实现分支、线程掩码、异常、cache 或多 warp
调度。load/store 会令共享指令流暂停，直到四个 Lane 的请求均被接收且响应均已返回。

### 最小 ISA

32-bit 指令的通用字段为：`opcode[31:28]`、`rd[27:25]`、
`rs1[24:22]`、`rs2[21:19]` 和 `imm[18:0]`。`STORE` 使用 `rd`
字段指定源数据寄存器。

| Opcode | 指令 | 语义 |
|---:|---|---|
| `0x0` | `NOP` | 空操作 |
| `0x1` | `MOVI rd, imm` | 各 Lane 将有符号立即数写入 `rd` |
| `0x2` | `LID rd` | 各 Lane 将自己的 `lane_id` 写入 `rd` |
| `0x3` | `ADD rd, rs1, rs2` | 逐 Lane 32-bit 加法 |
| `0x4` | `ADDI rd, rs1, imm` | 逐 Lane 立即数加法 |
| `0x5` | `SLLI rd, rs1, shamt` | 逐 Lane 左移 |
| `0x6` | `LOAD rd, [rs1+imm]` | 逐 Lane 地址生成、合并读取并写回 |
| `0x7` | `STORE rd, [rs1+imm]` | 逐 Lane 地址生成并合并写入 |
| `0x8` | `SUB rd, rs1, rs2` | 逐 Lane 32-bit 减法 |
| `0x9` | `AND rd, rs1, rs2` | 逐 Lane 按位与 |
| `0xa` | `OR rd, rs1, rs2` | 逐 Lane 按位或 |
| `0xb` | `XOR rd, rs1, rs2` | 逐 Lane 按位异或 |
| `0xc` | `SRLI rd, rs1, shamt` | 逐 Lane 逻辑右移 |
| `0xf` | `HALT` | 结束程序 |

## 目录结构

```text
.
├── rtl/
│   ├── simple_memory_core.sv       # 单个独立访存核
│   ├── data_coalescer.sv           # 请求缓冲、分组、合并和返回路由
│   ├── data_coalescing_system.sv   # 独立四核访存系统顶层
│   ├── simple_simt_core.sv         # 共享 PC 的四 Lane 可编程执行核
│   └── simple_gpu_system.sv        # 简化 GPU 顶层
├── tb/
│   ├── benchmark.cpp               # 独立访存流测试
│   └── gpu_benchmark.cpp           # SIMT 向量加法测试
├── chisel/
│   ├── src/main/scala/dco/          # 与上述 RTL 对应的 Chisel 实现
│   └── src/test/scala/dco/          # ChiselTest 合并/响应路由测试
├── build/
│   ├── benchmark.csv               # 独立访存流结果
│   └── gpu_benchmark.csv           # 简化 GPU 结果
└── Makefile
```

## 运行

依赖 GNU Make、C++17 编译器和 Verilator：

```bash
make lint
make benchmark
make gpu-lint
make gpu-benchmark
make chisel-test
make chisel-generate
```

`chisel-test` 使用 ChiselTest 验证四路连续 load 的 32-byte 合并、load 返回路由和
store byte-enable 合并；`chisel-generate` 将 `DataCoalescingSystem` 与
`SimpleGpuSystem` 生成到 `build/chisel-generated/`。Chisel 版本与当前 SystemVerilog
模型保持同一套接口语义，生成结果可继续用 Verilator 做 lint 或仿真。

工程路径含空格，因此 Makefile 将 Verilator 中间文件放到
`/tmp/data_coalescer_build_<uid>`，CSV 仍写入工程内的 `build/benchmark.csv`。

功能测试覆盖连续 load 返回、4:1 store 合并和读回、四核错开启动的独立请求，以及
SIMT 核的基础整数/逻辑 ALU 指令。

## Benchmark

内存模型始终接受请求，并在接受 32-byte transaction 后下一周期返回。前三组 workload
中每核执行 1000 次访问；最后一组让四核分别延迟 0、2、4、6 周期启动。周期数包含简化
核、输入缓冲、Coalescer 状态机和响应路径的全部开销。

| Pattern | Core requests | Memory transactions | Req/txn | 事务减少 | Req/cycle |
|---|---:|---:|---:|---:|---:|
| 连续，1 segment | 4000 | 1000 | 4.000 | 75.00% | 0.800 |
| 两两相邻，2 segments | 4000 | 2000 | 2.000 | 50.00% | 0.666 |
| stride=32B，4 segments | 4000 | 4000 | 1.000 | 0.00% | 0.333 |
| 四核错开启动 | 4000 | 3000 | 1.333 | 25.00% | 0.364 |

连续模式相对完全分散模式将下游事务减少 75%，系统有效请求吞吐从约 0.333 提高到
0.800 req/cycle，约为 2.4 倍。错开启动后，即使四核访问模式在地址上连续，也会因为
请求没有同时出现在缓冲区中而降低合并率。

这些结果是 RTL 和理想化 memory model 的周期级仿真数据，不代表芯片频率或外部 DRAM
带宽。真实系统还会受到 cache hit、bank conflict、memory latency、仲裁策略和 outstanding
transaction 数量影响。

## Simplified GPU Benchmark

简化 GPU Benchmark 向指令存储器写入 13 条指令，运行：

```c
C[lane_id] = A[lane_id] + B[lane_id];
```

程序包含两次 `LOAD`、一次整数 `ADD` 和一次 `STORE`。每条访存指令由四个 Lane
生成四个地址，共计 12 个 Lane 请求。测试运行两种布局：

| Pattern | Lane 间距 | Lane 请求 | 内存事务 | Req/txn | 事务减少 | 周期 |
|---|---:|---:|---:|---:|---:|---:|
| vector_add_contiguous | 4 B | 12 | 3 | 4.000 | 75.00% | 29 |
| vector_add_stride_32B | 32 B | 12 | 12 | 1.000 | 0.00% | 56 |
| alu_logic | per-lane 32 B result area | 24 | 24 | 1.000 | 0.00% | 104 |

连续布局中，A 的四次 load、B 的四次 load 和 C 的四次 store 分别合并为一个事务，
因此只产生 3 个内存事务。32 B 跨步布局中，每个 Lane 都落入不同 segment，共产生
12 个事务。在相同程序和理想化一周期内存响应模型下，合并访问将执行周期从 56 降至
29。Benchmark 会逐 Lane 检查 C 的计算结果，而不只检查事务计数。

`alu_logic` 程序还逐 Lane 验证 `ADD/SUB/AND/OR/XOR/SLLI/SRLI`，并将结果写回内存，
从而通过内存结果检查基础 ALU 和指令译码是否正确。
