# Single Read to DRAM Latency Report

**Description**:

We record the latency of a read request in CXL Memory Controller in Gem5 simulation. We count the latency of all read requests that access the DRAM and take an average.

Figure below presents the whole process of how a read request that goes to DRAM is handled. The latency we record of read requests starts when we receive the read request and ends when we response to CPU side (1 to 7).

 ![](D:\RPI\CXL_mem_ctrl\performance\read process.png)

**Configuration**:

CPU: 2 * X86 simple timing CPU, clock = 1GHz

Cache Coherence Protocol: MSI, 16KB

CXL Memory Controller: Compress 4KB data by LZ4, the average compressed size we observe is 1470B. Therefore, we use 1KB as our compressed data block size (2KB result also presents). CXL memory controller will read 1KB for each read request that needs to be responded by  DRAM. We set static latency 10ns for decompression.

System XBar: A module used to manage multiple devices, we used it with DRAMs to simulate multi-channel DRAM. The addresses in System XBar is interleaved in granularity 1KB.

DRAM:  DDR5 DIMM with two 32 bit channels. Maximum bandwidth for each DDR5 is 4400 MT/s. Each DDR5 is 2GB (Maximum support 2GB / channel)..

Workload: A multi-threading test script that divides the computation of element-wise addition of two arrays (`a` and `b`) into multiple threads, each handling a portion of the work, and stores the result in a third array (`c`).

**Useful Timings:**

DRAM average response time (avgRespTime): 26.872 ns (the delay step 4 takes)

CXL Front Latency = 25 ns

Entire Duration of system CXL + DRAM = 0.011946 s

Entire Duration of system with DRAM = 0.008434 s

Average Compressed size for 4KB: 1469.98B

Compressed data block size: 1KB

*2 values in DRAM for different DRAM controllers*

| Average Latency \ ns    | CXL + DRAM                | DRAM           |
| ----------------------- | ------------------------- | -------------- |
| CXL 2 cores + 2 channel | 213.3822 (81.164, 81.480) | 60.544, 60.583 |



**Additional Test for 2KB Compressed Data Blocks:**

Because the DDR5 interface in Gem5 only has 1KB row buffer size, which does not support 2KB address interleaved in System XBar, the system cannot read 2KB at a time with the XBar that interleaves the whole addresses space in granularity 1KB.

For compressed data block size: 2KB

We choose DDR4 DIMM with 2400 MT/s with row buffer size 2KB. Each DDR4 is 2GB (Maximum support 4GB / channel). 

Entire Duration of system CXL + DRAM = 0.014644 s

Entire Duration of system with DRAM = 0.008261 s

| Average Latency \ ns    | CXL + DRAM                 | DRAM           |
| ----------------------- | -------------------------- | -------------- |
| CXL 2 cores + 2 channel | 322.474 (132.920, 132.879) | 54.096, 53.188 |
