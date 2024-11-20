# How to Run the Scripts

**cxl_mcore_mchannel.py**: multi-cores and multi-channels setting with CXL memory controller module.

**mcore_mchannel_no_cxl.py**: multi-cores and multi-channels setting without CXL memory controller module.

The DRAM interface information is stored at gem5/src/mem/DRAMInterface.py



### Compressed data size:

If the system needs to compress the data into 1KB, the script should use DDR5 option:

```python
options = OptionsDDR5()
```

And modify the parameter "compressed_size=1024" in CXL memory controller setting.

For 2KB, the script should use DDR4 option:

```python
options = OptionsDDR4()
```

And modify the parameter "compressed_size=2048" in CXL memory controller setting.



### Running commands:

Compile at first:

```cmd
scons build/X86_MSI/gem5.opt -j 8
```

Running:

```cmd
build/X86_MSI/gem5.opt config/cxl_mem/cxl_mcore_mchannel.py
```

