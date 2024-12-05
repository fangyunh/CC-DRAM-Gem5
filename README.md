# Compression Capable DRAM

Yunhua Fang

## Abstract

We are hitting the Memory Wall that the development of DRAM cannot follow the step of CPU. CPU turns to be more advanced, but DRAM plays as a bottleneck to restrict the whole performance of the system. Applying compression on the main memory is one way to expanding the bandwidth and increasing the capacity of storage space before new evolutional  techniques occur on main memory. In this project, we implement a Compression-Capable design on the memory controller based in Gem5 simulated system. We also test the read latency between its interaction with CPU to figure out the performance degradation it brings to the system. 

## Introduction

Nowadays, DRAM resources is valuable. To expand the limited DRAM and bandwidth of memory bus, we propose a compression capable design on the memory controller, which could compress/decompress data and send to or fetch from DRAM. With high compression rate, the bandwidth increases because more data could be transferred through the connection at same time. We simulate the device on Gem5 and use latency checker to collect the data of its access latency. We want to leverage full potential of **traditional memory data structure**, including B+- tree and hash table in the future works.

## Works

Our major works focus on building the compression capable DRAM memory controller. Related files of it are located at folder gem5/src/cxl_mem and gem5/configs/cxl_mem:

**gem5/src/cxl_mem**: 

- lz4: Contains the lz4 compression algorithm
- cxl_mem_ctrl.cc / .hh: The implementation of our works including compression, latency collection, etc.
- CXLMemCtrl.py: Configuration of our memory controller
- Sconscript: File that help Gem5 senses our works and compile it with the whole system

**gem5/configs/cxl_mem**: 

- **For all configs, you can adjust the test script you assigned in it**

- cxl_mcore_mchannel.py: System configuration of 2 cores with 2 DDR4 channels (also can be adjusted to DDR5) with our memory controller.
- mcore_mchannel_no_cxl.py: System configuration of 2 cores with 2 DDR4 channels (also can be adjusted to DDR5) without our memory controller.
- msi_caches.py: Configuration of cache supports MSI coherence protocol.

Results and statistics are collected in the m5out after running the simulation.

To run the simulation, follow instructions in **cmds.md**.

A performance report on read latency is recorded in the **performance** folder. Please check it out for our test results.
