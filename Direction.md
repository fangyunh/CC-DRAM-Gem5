# Direction

On CPU simulator, we simulate a Compression-Capable DRAM (CC-DRAM), to work with CPU. We also detect the latency between its interaction with CPU. We use CXL (PCIe) as interconnect protocol.

## Introduction

Nowadays, DRAM resources is valuable. To expand the limited DRAM and bandwidth, we propose a DRAM, named Compression-Capable DRAM, which could compress data on DRAM. With high compresion rate, more data could be transfered through the connection, which means the bandwidth increases. The technique will also reduce the read/write augmentation during reading and writing. We simulate the device on Gem5 and use latency checker to collect the data of its latency. We want to stimulate full potential of traditional memory data structure, including B+- tree and hash table, on our design.s

## Plan

1. Learn Docker + setting up Gem5
2. 读取或写入DRAM的数据存在读/写放大，需要investigate这方面的原因，以及为什么CC-DRAM可以做到减少读放大写放大