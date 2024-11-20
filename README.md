# Direction

On Gem5 CPU simulator, we simulate a Compression-Capable design on the memory controller, to work with CPU. We also detect the latency between its interaction with CPU. 

## Introduction

Nowadays, DRAM resources is valuable. To expand the limited DRAM and bandwidth of memory bus, we propose a compression capable design on the memory controller, which could compress/decompress data and send to or fetch from DRAM. With high compresion rate, the bandwidth increases because more data could be transfered through the connection at same time. We simulate the device on Gem5 and use latency checker to collect the data of its latency. We want to leverage full potential of **traditional memory data structure**, including B+- tree and hash table, on our design. (Read/write augmentation)



## Objectives

- Build a basic model that can test the latency when read from DRAM
  - Understand gem5 codes about memory controller
  - Our self-defined CXL_memory_Controller: */src/cxl_mem* & */configs/cxl_mem*
- Estimate the latency if the data block we read is compressed by algorithms (LZ4)
- Support multiple channels and multple DRAMs
- support cache in CXL Memory Controller
