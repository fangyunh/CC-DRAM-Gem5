# Latency Test Notice

### **Read Latency**

Process:

- CPU sends read request to the CXL MemCtrl
- CXL MemCtrl transfer the request to DRAM MemCtrl
  - may exist a mapping between CPU requets address - compressed data address
  - Transfer CXL requests to DRAM requests
- DRAMMemCtrl send the request to DDR
- DDR sends the compressed response data to the DRAM MemCtrl
- DRAM MemCtrl sends the compressed response data to CXL MemCtrl
- CXL MemCtrl decompress it and sends back to CPU

Latency:

- (Mapping) + DRAM MemCtrl Latency + Decompress Latency



### Write Latency

Process:

- CPU sends write request to the CXL MemCtrl
- CXL MemCtrl compressed the data that needs to be flushed
- CXL MemCtrl sends the compressed data to the DRAM MemCtrl
- DRAM MemCtrl writes down the data to the DDR
-  

