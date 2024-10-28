#include "cxl_mem/cxl_mem_ctrl.hh"

#include "base/trace.hh"
#include "debug/DRAM.hh"
#include "debug/CXLMemCtrl.hh"
#include "mem/mem_ctrl.hh"
#include "sim/system.hh"

namespace gem5
{

namespace memory
{

// Constructor
CXLMemCtrl::CXLMemCtrl(const CXLMemCtrlParams &p) :
    ClockedObject(p), 
    cpu_side_port(name() + ".cpu_side_port", *this),
    memctrl_side_port(name() + ".memctrl_side_port", *this),
    requestQueueSize(p.request_buffer_size),
    responseQueueSize(p.response_buffer_size),
    prevArrival(0),
    stats(*this)
{
    DPRINTF(CXLMemCtrl, "Setting up CXL Memory Controller\n");
}

void
CXLMemCtrl::init()
{
    if (!cpu_side_port.isConnected()) {
        fatal("CXLMemCtrl %s is unconnected on CPU side port!\n", name());
    }

    if (!memctrl_side_port.isConnected()) {
        fatal("CXLMemCtrl %s is unconnected on MemCtrl side port!\n", name());
    }
}


Port &
CXLMemCtrl::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "cpu_side_port") {
        return cpu_side_port;
    }
    else if (if_name == "memctrl_side_port"){
        return memctrl_side_port;
    }
    else {
        // Pass it to the base class
        return ClockedObject::getPort(if_name, idx);
    }
}

bool
CXLMemCtrl::handleRequest(PacketPtr pkt)
{
    DPRINTF(CXLMemCtrl, "Received timing request: %s addr %#x size %d\n",
            pkt->cmdString(), pkt->getAddr(), pkt->getSize());
    
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    panic_if(!(pkt->isRead() || pkt->isWrite()),
             "Should only see read and writes at memory controller\n");

    packetLatency[pkt->id] = curTick();
    
    // Calc avg gap between requests
    if (prevArrival != 0) {
        stats.totGap += curTick() - prevArrival;
    }
    prevArrival = curTick();

    // Store initial tick of packet
    if (reqQueueFull()) {
        DPRINTF(CXLMemCtrl, "Request queue full, not accepting\n");
        return false;
    }

    reqQueue.push_back(pkt);

    // Forward the packet to the memory controller side port
    memctrl_side_port.processRequestQueue();

    return true;
}

bool
CXLMemCtrl::handleResponse(PacketPtr pkt)
{
    DPRINTF(CXLMemCtrl, "Received timing response: %s addr %#x size %d\n",
            pkt->cmdString(), pkt->getAddr(), pkt->getSize());
    
    auto it = packetLatency.find(pkt->id);
    if (it != packetLatency.end()) {
        Tick latency = curTick() - it->second;
        stats.totalLatency += latency;
        stats.latencyHistogram.sample(latency);

        int requestorID = pkt->req->requestorId();
        stats.perRequestorLatency[requestorID] += latency;
        stats.perRequestorAccesses[requestorID]++;
        
        packetLatency.erase(it);
    }

    if (respQueueFull()) {
        DPRINTF(CXLMemCtrl, "Response queue full, cannot accept packet\n");
        needRetry = true;
        return false;
    }

    respQueue.push_back(pkt);

    // Forward the packet to the CPU side port
    cpu_side_port.processResponseQueue();

    // retry if need it
    cpu_side_port.retryResp();

    return true;
}


AddrRangeList
CXLMemCtrl::getAddrRanges() const
{
    // Obtain address ranges from the memory controller
    AddrRangeList ranges;
    if (mem_ctrl_side_port.isConnected()) {
        ranges = mem_ctrl_side_port.getAddrRanges();
    }
    return ranges;
}

void
CXLMemCtrl::sendRangeChange()
{
    cpu_side_port.sendRangeChange();
}

bool
CXLMemCtrl::reqQueueFull() const
{
    return reqQueue.size() >= requestQueueSize; 
}

bool
CXLMemCtrl::respQueueFull() const
{
    return respQueue.size() >= responseQueueSize; 
}

void
CXLMemCtrl::printQs() const
{
#if TRACING_ON
    DPRINTF(CXLMemCtrl, "Request Queue:\n");
    for (const auto& pkt : reqQueue) {
        DPRINTF(CXLMemCtrl, "  Request: %s addr %#x size %d\n",
                pkt->pkt->cmdString(), pkt->pkt->getAddr(), pkt->pkt->getSize());
    }

    DPRINTF(CXLMemCtrl, "Response Queue:\n");
    for (const auto& pkt : respQueue) {
        DPRINTF(CXLMemCtrl, "  Response: %s addr %#x size %d\n",
                pkt->pkt->cmdString(), pkt->pkt->getAddr(), pkt->pkt->getSize());
    }
#endif // TRACING_ON
}

void
CXLMemCtrl::calculateAvgLatency()
{
    // This method can be used to calculate average latency over time
    if (stats.latencyHistogram.sampleCount() > 0) {
        stats.avgLatency = stats.totalLatency / stats.latencyHistogram.sampleCount();
    } else {
        stats.avgLatency = 0;
    }
}

CXLMemCtrl::CXLStats::
CXLStats(CXLMemCtrl &_cxlmc)
    : statistics::Group(&_cxlmc),
      cxlmc(_cxlmc),
      ADD_STAT(totalLatency, statistics::units::Tick::get(),
               "Total latency of all packets"),
      ADD_STAT(avgLatency, statistics::units::Rate<
                    statistics::units::Tick, statistics::units::Count>::get(),
               "Average latency per packet"),
      ADD_STAT(latencyHistogram, statistics::units::Tick::get(),
               "Latency histogram"),
      ADD_STAT(perRequestorLatency, statistics::units::Tick::get(),
               "Total latency per requestor"),
      ADD_STAT(perRequestorAccesses, statistics::units::Count::get(),
               "Number of accesses per requestor"),
      ADD_STAT(perRequestorAvgLatency, statistics::units::Rate<
                    statistics::units::Tick, statistics::units::Count>::get(),
               "Average latency per requestor")
{ }

void
CXLStats::regStats()
{
    using namespace statistics;

    // Ensure system pointer is valid
    assert(cxlmc.system());
    const auto max_requestors = cxlmc.system()->maxRequestors();

    // Configure totalLatency
    totalLatency
        .flags(nozero | nonan)
        ;

    // Configure avgLatency
    avgLatency
        .flags(nozero | nonan)
        .precision(2)
        ;

    // Configure latencyHistogram
    latencyHistogram
        .init(10)
        .flags(nozero | nonan)
        ;

    // Initialize per-requestor statistics
    perRequestorLatency
        .init(max_requestors)
        .flags(nozero | nonan)
        ;

    perRequestorAccesses
        .init(max_requestors)
        .flags(nozero)
        ;

    perRequestorAvgLatency
        .flags(nonan)
        .precision(2)
        ;

    // Set subnames for per-requestor statistics
    for (int i = 0; i < max_requestors; i++) {
        const std::string requestor = cxlmc.system()->getRequestorName(i);
        perRequestorLatency.subname(i, requestor);
        perRequestorAccesses.subname(i, requestor);
        perRequestorAvgLatency.subname(i, requestor);
    }

    // Define formula statistics
    avgLatency = totalLatency / perRequestorAccesses.sum();
    perRequestorAvgLatency = perRequestorLatency / perRequestorAccesses;
}


CXLMemCtrl::CPUPort::
CPUPort(const std::string& name, CXLMemCtrl& _ctrl)
    : ResponsePort(name), needRetry(false), blockedPacket(nullptr),
      ctrl(_ctrl)
{ }

// Send response back to CPU
void
CXLMemCtrl::CPUPort::processResponseQueue()
{
    while (!respQueue.empty()) {
        PacketPtr pkt = respQueue.front();

        // Try to send the packet to the memory controller
        if (!cpu_side_port.sendTimingResp(pkt)) {
            DPRINTF(CXLMemCtrl, "CPU cannot accept response, will retry\n");
            // Retry the packet
            blockedPacket = pkt;
            break;
        }

        DPRINTF(CXLMemCtrl, "Sent response back to CPU\n");
        respQueue.pop_front();
    }
}

bool
CXLMemCtrl::CPUPort::recvTimingReq(PacketPtr pkt)
{
    // Just forward to the cxlmemctrl.
    if (!ctrl.handleRequest(pkt)) {
        needRetry = true;
        return false;
    } else {
        return true;
    }
}

AddrRangeList
CXLMemCtrl::CPUPort::getAddrRanges() const
{
    return ctrl.getAddrRanges();
}

void
CXLMemCtrl::CPUPort::retryResp()
{
    if (needRetry) {
        needRetry = false;
        DPRINTF(CXLMemCtrl, "Sending retry req for %d\n", id);
        sendRetryReq();
        respQueue.pop_front();
    }    
}

void
CXLMemCtrl::CPUPort::recvRespRetry()
{
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    // Try to resend it. It's possible that it fails again.
    processResponseQueue(pkt);
}

CXLMemCtrl::MemCtrlPort::
MemCtrlPort(const std::string& name, CXLMemCtrl& _ctrl)
    : RequestPort(name), needRetry(false), blockedPacket(nullptr),
      ctrl(_ctrl)
{ }


// Send request to memory controller
void
CXLMemCtrl::MemCtrlPort::processRequestQueue()
{
    while (!reqQueue.empty()) {
        PacketPtr pkt = reqQueue.front();

        // Try to send the response back to the CPU
        if (!memctrl_side_port.sendTimingReq(pkt)) {
            DPRINTF(CXLMemCtrl, "Downstream controller cannot accept packet, will retry\n");
            // Will retry when recvReqRetry is called
            blockedPacket = pkt;
            break;
        }

        DPRINTF(CXLMemCtrl, "Forwarded packet to downstream controller\n");
        reqQueue.pop_front();
    }
}

bool
CXLMemCtrl::MemCtrlPort::recvTimingResp(PacketPtr pkt)
{
    return ctrl.handleResponse(pkt);
}

void
CXLMemCtrl::MemCtrlPort::recvReqRetry()
{
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    // Try to resend it. It's possible that it fails again.
    processRequestQueue();
}

void
CXLMemCtrl::MemCtrlPort::recvRangeChange()
{
    ctrl.sendRangeChange();
}

}   
}