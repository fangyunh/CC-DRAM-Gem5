# CXL Memory Controller Performance

The report records the performance of architectures with CXL memory controller and without CXL memory controller in the simulation of Gem5.



### Configs:

clk: 1GHz

memory mode: timing

CPU: X86 Timing Simple CPU

Cache: Simple Cache 8kB

DRAM: A single DDR5-4400 32bit channel (4x8 configuration), 4Gbx8 devices (32Gb addressing), Maximum bandwidth of DDR5_4400_4x8 (4400 MT/s) can be 17.6GB/s

- 1 channel for 1 rank with address range 2GB

Test Script: Array Addition



|      | Single Core Single Channel | Single Core Multi Channels | Multi Core Multi Channels |
| ---- | -------------------------- | -------------------------- | ------------------------- |
|      |                            |                            |                           |
|      |                            |                            |                           |
|      |                            |                            |                           |
|      |                            |                            |                           |

