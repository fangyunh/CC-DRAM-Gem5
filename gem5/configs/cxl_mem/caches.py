from m5.objects import Cache

class L1Cache(Cache):
    assoc = 2
    tag_latency = 2
    data_latency = 2
    response_latency = 2
    mshrs = 4
    tgts_per_mshr = 20

    # connect CPU to the cache
    def connectCPU(self, cpu):
        raise NotImplementedError

    # connect cache to bus
    def connectBus(self, bus):
        self.mem_side = bus.cpu_side_ports

# L1 instruction cache
class L1ICache(L1Cache):
    size = '16kB'

    def connectCPU(self, cpu):
        self.cpu_side = cpu.icache_port
    
    def __init__(self, size='16kB', assoc=2):
        super(L1ICache, self).__init__()
        self.size = size
        self.assoc = assoc

# L1 data cache
class L1DCache(L1Cache):
    size = '64kB'

    def connectCPU(self, cpu):
        self.cpu_side = cpu.dcache_port
    
    def __init__(self, size='64kB', assoc=2):
        super(L1ICache, self).__init__()
        self.size = size
        self.assoc = assoc

# L2 cache
class L2Cache(Cache):
    size = '256kB'
    assoc = 8
    tag_latency = 20
    data_latency = 20
    response_latency = 20
    mshrs = 20
    tgts_per_mshr = 12

    def connectCPUSideBus(self, bus):
        self.cpu_side = bus.mem_side_ports
    
    def connectBus(self, bus):
        self.mem_side = bus.cpu_side_ports
    
    def __init__(self, size='256kB', assoc=8):
        super(L1ICache, self).__init__()
        self.size = size
        self.assoc = assoc
    
# L3 cache
class L3Cache(Cache):
    size = '1MB'
    assoc = 16
    tag_latency = 40
    data_latency = 40
    response_latency = 40
    mshrs = 32
    tgts_per_mshr = 16

    def connectCPUSideBus(self, bus):
        self.cpu_side = bus.mem_side_ports
    
    def connectMemSideBus(self, bus):
        self.mem_side = bus.cpu_side_ports
    
    def __init__(self, size='1MB', assoc=16):
        super(L1ICache, self).__init__()
        self.size = size
        self.assoc = assoc

def __init__(self, options=None):
    super(L1Cache, self).__init__()
    pass