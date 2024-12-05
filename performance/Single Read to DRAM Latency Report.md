# Single Read to DRAM Latency Report

**Description**:

We record the latency of a read request in CXL Memory Controller in Gem5 simulation. We count the latency of all read requests that access the DRAM and take an average.

Figure below presents the whole process of how a read request that goes to DRAM is handled. The latency we record of read requests starts when we receive the read request and ends when we response to CPU side (1 to 7).

 ![](D:\RPI\CXL_mem_ctrl\performance\read process.png)

**Configuration**:

CPU: 2 * X86 simple timing CPU, clock = 1GHz

Cache Coherence Protocol: MSI, 16KB

CXL Memory Controller: Compress 4KB data by LZ4 in dynamic way, do trade off on compress rate and access latency. We compress the 4KB in 3 different ways, compare their compress rate and choose the best way to compress. We set static latency 10ns for decompression.

System XBar: A module used to manage multiple devices, we used it with DRAMs to simulate multi-channel DRAM. The addresses in System XBar is interleaved in granularity 1KB.

DRAM:  DDR4 DIMM with 2400 MT/s with row buffer size 2KB. Each DDR4 is 2GB (Maximum support 4GB / channel).

Workloads: 

- A multi-threading test script that divides the computation of element-wise addition of two arrays (`a` and `b`) into multiple threads, each handling a portion of the work, and stores the result in a third array (`c`).
- self-designed test: It firstly write 128 / 256 / 512 MB in the DRAM and then randomly read it to simulate the access on DRAM

**Useful Timings:**

CXL Front and back Latency = 25 ns

DRAM Front and back Latency = 44 ns

| Workloads              | CC Memory Ctrl | DRAM Memory Ctrls      |
| ---------------------- | -------------- | ---------------------- |
| 128 MB write then Read | 150.282 ns     | 124.187 ns, 126.003 ns |
| 256 MB                 | 161.699 ns     | 120.704 ns, 119.008 ns |
| 512 MB                 | 163.542 ns     | 118.031 ns, 117.054 ns |

