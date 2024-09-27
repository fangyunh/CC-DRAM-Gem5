import m5
from m5.objects import *
from caches import *
import argparse

# parent of all other objects in system
system = System()

# clock set up
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = '1GHz'
system.clk_domain.voltage_domain = VoltageDomain()

# memory
system.mem_mode = 'timing'
system.mem_ranges = [AddrRange('512MB')]

# cpu
system.cpu = X86TimingSimpleCPU()

# memory bus
system.membus = SystemXBar()

# create an I/O controller on the CPU and connect it to the memory bus
system.cpu.createInterruptController()

# connect PIO and interrupt ports to the memory bus is an x86-specific requirement
system.cpu.interrupts[0].pio = system.membus.mem_side_ports
system.cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
system.cpu.interrupts[0].int_responder = system.membus.mem_side_ports
system.system_port = system.membus.cpu_side_ports

# binary = #self define workload
system.workload = SEWorkload.init_compatible(options.binary)

process = Process()
process.cmd = [binary]
system.cpu.workload = process
system.cpu.createThreads()

# L1 Caches
system.cpu.icache = L1ICache(options)
system.cpu.dcache = L1DCache(options)
system.cpu.icache.connectCPU(system.cpu)
system.cpu.dcache.connectCPU(system.cpu)

# L1 connects L2
system.l2bus = L2XBar()
system.cpu.icache.connectBus(system.l2bus)
system.cpu.dcache.connectBus(system.l2bus)

# L2 cache
system.l2cache = L2Cache(options)
system.l2cache.connectCPUSideBus(system.l2bus)

# L2 connects L3
system.l3bus = L3XBar()
system.l2cache.connectMemSideBus(system.l3bus)

# L3 cache
system.l3cache = L3Cache(options)
system.l3cache.connectCPUSideBus(system.l3bus)

# L3 connects Main memory
system.membus = SystemXBar()
system.l3cache.connectMemSideBus(system.membus)

# # Cache Parameter Configuration in Command Line
# parser = argparse.ArgumentParser(description='A simple system with 2-level cache.')
# parser.add_argument("binary", default="", nargs="?", type=str, help="Path to binary to execute")
# parser.add_argument("--l1i_size", help=f"L1 instruction cache size. Default: 16kB.")
# parser.add_argument("--l1d_size", help="L1 data cache size. Default: 64kB.")
# parser.add_argument("--l2-size", help="L2 cache size. Default 256kB.")
# options = parser.parse_args()

# instantiate system and begin execution
root = Root(full_system = False, system = system)
m5.instantiate()

print("Beginning Simulation")
exit_event = m5.simulate()

# finish
print('Exiting @ trick {} because {} '.format(m5.curTick(), exit_event.getCause()))
