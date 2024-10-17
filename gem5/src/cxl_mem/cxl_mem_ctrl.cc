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
    cpu_side_port(name() + ".cpu_side_port", *this),
    mem_ctrl_side_port(name() + ".mem_ctrl_side_port", *this),
    processReqEvent([this]{ processRequestQueue(); }, name() + ".processReqEvent"),
    processRespEvent([this]{ processResponseQueue(); }, name() + ".processRespEvent"),
    requestQueueSize(p.request_buffer_size),
    responseQueueSize(p.response_buffer_size),
    prevArrival(0),
    retryReq(false), retryResp(false);
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
    
    packetLatency[pkt->id] = curTick;
    
    // Calc avg gap between requests
    if (prevArrival != 0) {
        stats.totGap += curTick() - prevArrival;
    }
    prevArrival = curTick();

    // Store initial tick of packet
    if (reqQueueFull()) {
        DPRINTF(CXLMemCtrl, "Request queue full, not accepting\n");
        retryReq = true;
        return false;
    }

    reqQueue.push_back(pkt);

    // Forward the packet to the memory controller side port
    if (!processReqEvent.scheduled()) {
        schedule(processReqEvent, curTick());
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

    if (respQueueFull()) {
        DPRINTF(CXLMemCtrl, "Response queue full, cannot accept packet\n");
        retryResp = true;
        return false;
    }

    respQueue.push_back(pkt);

    // Schedule event to process the response queue if not already scheduled
    if (!processRespEvent.scheduled()) {
        schedule(processRespEvent, curTick());
    }

    return true;
}

void
CXLMemCtrl::processRequestQueue()
{
    while (!reqQueue.empty()) {
        PacketPtr pkt = reqQueue.front();

        // Try to send the packet to the memory controller
        if (!mem_ctrl_side_port.sendTimingReq(pkt)) {
            DPRINTF(CXLMemCtrl, "Downstream controller cannot accept packet, will retry\n");
            // Will retry when recvReqRetry is called
            break;
        }

        DPRINTF(CXLMemCtrl, "Forwarded packet to downstream controller\n");
        reqQueue.pop_front();
    }

    // If there are still packets in the queue, schedule the event again
    if (!reqQueue.empty() && !processReqEvent.scheduled()) {
        schedule(processReqEvent, clockEdge(Cycles(1)));
    }
}

void
CXLMemCtrl::processResponseQueue()
{
    while (!respQueue.empty()) {
        PacketPtr pkt = respQueue.front();

        // Try to send the response back to the CPU
        if (!cpu_side_port.sendTimingResp(pkt)) {
            DPRINTF(CXLMemCtrl, "CPU cannot accept response, will retry\n");
            // Will retry when recvRespRetry is called
            break;
        }

        DPRINTF(CXLMemCtrl, "Sent response back to CPU\n");
        respQueue.pop_front();
    }

    // If there are still packets in the queue, schedule the event again
    if (!respQueue.empty() && !processRespEvent.scheduled()) {
        schedule(processRespEvent, clockEdge(Cycles(1)));
    }
}

void
CXLMemCtrl::recvReqRetry()
{
    DPRINTF(CXLMemCtrl, "Received retry request from MemCtrl\n");

    // Attempt to resend packets from the request queue
    if (!processReqEvent.scheduled()) {
        schedule(processReqEvent, curTick());
    }

    // If we previously told the CPU to retry, and we now have space
    if (retryReq && !reqQueueFull()) {
        retryReq = false;
        cpu_side_port.sendRetryReq();
    }
}

void
CXLMemCtrl::recvRespRetry()
{
    DPRINTF(CXLMemCtrl, "Received retry response from CPU\n");

    // Attempt to resend packets from the response queue
    if (!processRespEvent.scheduled()) {
        schedule(processRespEvent, curTick());
    }

    // If we previously told MemCtrl to retry, and we now have space
    if (retryResp && !respQueueFull()) {
        retryResp = false;
        mem_ctrl_side_port.sendRetryResp();
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
    : QueuedResponsePort(name, queue), queue(_ctrl, *this, true),
      ctrl(_ctrl)
{ }

bool
CXLMemCtrl::CPUPort::recvTimingReq(PacketPtr pkt)
{
    return ctrl.recvTimingReq(pkt);
}

AddrRangeList
CXLMemCtrl::CPUPort::getAddrRanges() const
{
    return ctrl.getAddrRanges();
}
void
CXLMemCtrl::CPUPort::recvRespRetry()
{
    ctrl.recvRespRetry();
}

CXLMemCtrl::MemCtrlPort::
MemCtrlPort(const std::string& name, CXLMemCtrl& _ctrl)
    : QueuedRequestPort(name, queue), queue(_ctrl, *this),
      ctrl(_ctrl)
{ }

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

void
CXLMemCtrl::MemCtrlPort::recvReqRetry()
{
    ctrl.recvReqRetry();
}

}   
}