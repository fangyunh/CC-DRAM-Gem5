# Direction

On Gem5 CPU simulator, we simulate a Compression-Capable design on the memory controller, to work with CPU. We also detect the latency between its interaction with CPU. 

## Introduction

Nowadays, DRAM resources is valuable. To expand the limited DRAM and bandwidth of memory bus, we propose a compression capable design on the memory controller, which could compress/decompress data and send to or fetch from DRAM. With high compresion rate, the bandwidth increases because more data could be transfered through the connection at same time. We simulate the device on Gem5 and use latency checker to collect the data of its latency. We want to leverage full potential of **traditional memory data structure**, including B+- tree and hash table, on our design. (Read/write augmentation)

## Setting

CPU: single core X86TimingSimpleCPU()

Caches: 3 cache levels

DRAM: DDR4

Emulation: Syscall emulation mode

### Memory Controller

Source: https://www.gem5.org/documentation/general_docs/memory_system/gem5_memory_system/

*Folder Address: /gem5/src/mem/mem_ctrl.hh*

All objects that connect to the memory system inherit from `MemObject`.

```c++
// Pure virtual functions, returns a port corresponding to the given name and index. 
getMasterPort(const std::string &name, PortID idx);
getSlavePort(const std::string &name, PortID idx);
```

**DRAM Memory Controller**

- Memory controller: port connecting to on-chip fabric (CPU)
  - Recieves command packets from the CPU
  - Enqueues into read and write queues
  - Manage the command scheduling algorithm for read / write requests

- DRAM interface
  - Define architecture and timing parameters of the DRAM
  - Manage media specifc operations (activation, precharge, refresh, low power modes, etc)



## Objectives

- Build a basic model that can test the latency when read from DRAM
  - Understand gem5 codes about memory controller
  - Our self-defined CXL_memory_Controller: */src/cxl_mem* & */configs/cxl_mem*
- Estimate the latency if the data block we read is compressed by algorithms (LZ4)
- *(Support CXL on memory controller)*
- Add cache on CXL-mem-controller to store hot data