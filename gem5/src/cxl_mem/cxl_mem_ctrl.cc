#include "cxl_mem/cxl_mem_ctrl.hh"

#include "base/trace.hh"
#include "debug/DRAM.hh"
#include "debug/CXLMemCtrl.hh"
#include "debug/PacketLatency.hh"
#include "debug/Drain.hh"
#include "mem/mem_ctrl.hh"
#include "sim/system.hh"

namespace gem5
{

namespace memory
{

// Constructor
CXLMemCtrl::CXLMemCtrl(const CXLMemCtrlParams &p) :
    ClockedObject(p), 
    cpu_side_port(name() + '.cpu_side_port', *this),
    mem_ctrl_side_port(name() + '.mem_ctrl_side_port', *this),
    requestQueueSize(p.request_buffer_size),
    responseQueueSize(p.response_buffer_size),
    prevArrival(0),
    stats(*this)
{
    DPRINTF(CXLMemCtrl, "Setting up CXL Memory Controller\n");
    reqQueue.resize(p.requestQueueSize);
    respQueue.resize(p.responseQueueSize);
}

void
CXLMemCtrl::init()
{
    if (!cpu_side_port.isConnected()) {
        fatal("CXLMemCtrl %s is unconnected on CPU side port!\n", name());
    }

    if (!mem_ctrl_side_port.isConnected()) {
        fatal("CXLMemCtrl %s is unconnected on MemCtrl side port!\n", name());
    }

    // You may need to send range change notifications
    cpu_side_port.sendRangeChange();
}

Port &
CXLMemCtrl::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "cpu_side") {
        return cpu_side_port;
    } else if (if_name == "mem_side") {
        return mem_ctrl_side_port;
    } else {
        // Pass it to the base class
        return ClockedObject::getPort(if_name, idx);
    }
}

bool
CXLMemCtrl::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(CXLMemCtrl, "Received timing request: %s addr %#x size %d\n",
            pkt->cmdString(), pkt->getAddr(), pkt->getSize());
    
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    panic_if(!(pkt->isRead() || pkt->isWrite()),
             "Should only see read and writes at memory controller\n");
    
    // Store initial tick of packet
    packetLatency[pkt->id] = curTick;
    
    // Calc avg gap between requests
    if (prevArrival != 0) {
        stats.totGap += curTick() - prevArrival;
    }
    prevArrival = curTick();

    // Forward the packet to the memory controller side port
    if (!mem_ctrl_side_port.sendTimingReq(pkt)) {
        DPRINTF(CXLMemCtrl, "Cannot forward request now, will retry\n");
        // Store the packet for retry
        retryPkt = pkt;
        return false;
    }

    return true;
}

bool
CXLMemCtrl::recvTimingResp(PacketPtr pkt)
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

    cpu_side_port.schedTimingResp(pkt, curTick());

    return true;
}

void
CXLMemCtrl::recvRetry()
{
    // Attempt to resend the request packet if blocked earlier
    if (retryPkt) {
        if (mem_ctrl_side_port.sendTimingReq(retryPkt)) {
            retryPkt = nullptr;
        }
    }
}

AddrRangeList
CXLMemCtrl::getAddrRanges() const
{
    // Obtain address ranges from the memory controller
    AddrRangeList ranges;
    if (mem_ctrl_side_port.isConnected()) {
        ranges = mem_ctrl_side_port.peer()->getAddrRanges();
    }
    return ranges;
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
    : QueuedResponsePort(name, queue), queue(_ctrl, *this, true),
      ctrl(_ctrl)
{ }

bool
CXLMemCtrl::CPUPort::recvTimingReq(PacketPtr pkt)
{
    return ctrl.recvTimingReq(pkt);
}

void
CXLMemCtrl::CPUPort::recvRespRetry()
{
    queue.retry();
    ctrl.recvRetry();
}

AddrRangeList
CXLMemCtrl::CPUPort::getAddrRanges() const
{
    return ctrl.getAddrRanges();
}

CXLMemCtrl::MemCtrlPort::
MemCtrlPort(const std::string& name, CXLMemCtrl& _ctrl)
    : QueuedRequestPort(name, queue), queue(_ctrl, *this),
      ctrl(_ctrl)
{ }

void
CXLMemCtrl::MemCtrlPort::recvReqRetry()
{
    queue.retry();
    ctrl.recvRetry();
}

bool
CXLMemCtrl::MemCtrlPort::recvTimingResp(PacketPtr pkt)
{
    return ctrl.recvTimingResp(pkt);
}

bool
CXLMemCtrl::MemCtrlPort::sendTimingReq(PacketPtr pkt)
{
    /** Schedule the request to be sent at the current tick */
    queue.schedSendTiming(pkt, curTick());
    return true;
}

}   
}