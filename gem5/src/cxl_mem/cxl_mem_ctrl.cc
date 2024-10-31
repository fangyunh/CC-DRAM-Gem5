#include "cxl_mem/cxl_mem_ctrl.hh"

#include "base/trace.hh"
#include "debug/DRAM.hh"
#include "debug/CXLMemCtrl.hh"
#include "mem/mem_ctrl.hh"
#include "sim/system.hh"

namespace gem5
{


// Constructor
CXLMemCtrl::CXLMemCtrl(const CXLMemCtrlParams &p) :
    ClockedObject(p), 
    cpu_side_port(name() + ".cpu_side_port", *this),
    memctrl_side_port(name() + ".memctrl_side_port", *this),
    reqEvent([this] {processRequestEvent();}, name()),
    respEvent([this] {processResponseEvent();}, name()),
    requestQueueSize(p.request_buffer_size),
    responseQueueSize(p.response_buffer_size),
    prevArrival(0),
    delay(p.delay),
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
    
    // Calc avg gap between requests
    // if (prevArrival != 0) {
    //     stats.totGap += curTick() - prevArrival;
    // }
    // prevArrival = curTick();

    // Store initial tick of packet
    if (reqQueueFull()) {
        DPRINTF(CXLMemCtrl, "Request queue full, not accepting\n");
        cpu_side_port.needResend = true;
        return false;
    }

    packetLatency[pkt->id] = curTick();

    reqQueue.push_back(pkt);

    // Forward the packet to the memory controller side port
    if (!reqEvent.scheduled()) {
        DPRINTF(CXLMemCtrl, "Request scheduled immediately\n");
        schedule(reqEvent, curTick());
    }

    return true;
}

bool
CXLMemCtrl::handleResponse(PacketPtr pkt)
{
    DPRINTF(CXLMemCtrl, "Received timing response: %s addr %#x size %d\n",
            pkt->cmdString(), pkt->getAddr(), pkt->getSize());

    if (respQueueFull()) {
        DPRINTF(CXLMemCtrl, "Response queue full, cannot accept packet\n");
        memctrl_side_port.needResend = true;
        return false;
    }

    auto it = packetLatency.find(pkt->id);
    if (it != packetLatency.end()) {
        Tick latency = curTick() - it->second;
        stats.totalLatency += latency;
        stats.latencyHistogram.sample(latency);
        
        packetLatency.erase(it);
    }

    respQueue.push_back(pkt);

    // Forward the packet to the CPU side port
    if (!respEvent.scheduled()) {
        DPRINTF(CXLMemCtrl, "Response scheduled immediately\n");
        schedule(respEvent, curTick());
    }

    // retry if need it
    // cpu_side_port.retryResp();

    return true;
}

void
CXLMemCtrl::handleFunctional(PacketPtr pkt)
{
    memctrl_side_port.sendFunctional(pkt);
}

void
CXLMemCtrl::recvReqRetry() {
    if (memctrl_side_port.needRetry) {
        memctrl_side_port.needRetry = false;

        if (!reqEvent.scheduled()) {
            schedule(reqEvent, curTick());
        }
    }
}

void
CXLMemCtrl::recvRespRetry() {
    // If ther is still have blocked packet
    // The blocked packet will always keep in the front of the queue
    // if (needRetry && blockedPacket != nullptr) {
    if (cpu_side_port.needRetry) {
        cpu_side_port.needRetry = false;

        if (!respEvent.scheduled()) {
            schedule(respEvent, curTick());
        }
        
    }
}

// Send request to memory controller
void
CXLMemCtrl::processRequestEvent()
{
    while (!reqQueue.empty()) {
        PacketPtr pkt = reqQueue.front();

        // Try to send the response back to the CPU
        if (!memctrl_side_port.sendTimingReq(pkt)) {
            DPRINTF(CXLMemCtrl, "Downstream controller cannot accept packet, will retry\n");
            // Will retry when recvReqRetry is called
            memctrl_side_port.needRetry = true;
            // memctrl_side_port.blockedPacket = pkt;
            break;
        }

        DPRINTF(CXLMemCtrl, "Forwarded packet to downstream controller\n");
        reqQueue.pop_front();
    }

    // Request queue is free to accept previous failed packets
    if (!reqQueueFull() && cpu_side_port.needResend) {
        cpu_side_port.needResend = false;
        cpu_side_port.sendRetryReq();
    }

    // If there are still packets in the queue, schedule the event again
    // if (!reqQueue.empty() && !reqEvent.scheduled()) {
    //     schedule(reqEvent, clockEdge(Cycles(1)));
    // }
}

// Send response back to CPU
void
CXLMemCtrl::processResponseEvent()
{
    while (!respQueue.empty()) {
        PacketPtr pkt = respQueue.front();

        // Try to send the packet to the memory controller
        if (!cpu_side_port.sendTimingResp(pkt)) {
            DPRINTF(CXLMemCtrl, "CPU cannot accept response, will retry\n");
            // Retry the to send packet
            cpu_side_port.needRetry = true;
            // cpu_side_port.blockedPacket = pkt;
            break;
        }

        DPRINTF(CXLMemCtrl, "Sent response back to CPU\n");
        respQueue.pop_front();
    }

    // Response queue is free to accept previous failed packets
    if (!respQueueFull() && memctrl_side_port.needResend) {
        memctrl_side_port.needResend = false;
        memctrl_side_port.sendRetryResp();
    }
}

AddrRangeList
CXLMemCtrl::getAddrRanges()
{
    // Obtain address ranges from the memory controller
    AddrRangeList ranges;
    if (memctrl_side_port.isConnected()) {
        ranges.push_back(memctrl_side_port.getAddrRanges());
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

// void
// CXLMemCtrl::calculateAvgLatency()
// {
//     // This method can be used to calculate average latency over time
//     if (stats.latencyHistogram.size() > 0) {
//         stats.avgLatency = stats.totalLatency / stats.latencyHistogram.size();
//     } else {
//         stats.avgLatency = 0;
//     }
// }

CXLMemCtrl::CXLStats::
CXLStats(CXLMemCtrl &_cxlmc)
    : statistics::Group(&_cxlmc),
      cxlmc(_cxlmc),

    ADD_STAT(totalLatency, statistics::units::Tick::get(),
            "Total latency of all packets"),
    ADD_STAT(latencyHistogram, statistics::units::Tick::get(),
            "Latency histogram"),
    ADD_STAT(avgLatency, statistics::units::Rate<
                statistics::units::Tick, statistics::units::Count>::get(),
            "Average latency per packet")
{ }

void
CXLMemCtrl::CXLStats::regStats()
{
    using namespace statistics;
    //statistics::Group::regStats();

    // Ensure system pointer is valid
    //System* system = cxlmc._system();
    //const auto max_requestors = cxlmc.system()->maxRequestors();

    // Configure totalLatency
    totalLatency
        .flags(nozero | nonan)
        ;

    // Configure latencyHistogram
    latencyHistogram
        .init(10)
        .flags(nozero | nonan)
        ;

    // Configure avgLatency
    avgLatency.precision(8);

    avgLatency = (totalLatency) / latencyHistogram.size();

}


CXLMemCtrl::CPUPort::
CPUPort(const std::string& name, CXLMemCtrl& _ctrl)
    : ResponsePort(name), needRetry(false), 
      needResend(false), blockedPacket(nullptr),
      ctrl(_ctrl)
{ }

Tick 
CXLMemCtrl::CPUPort::recvAtomic(PacketPtr pkt) {
    DPRINTF(CXLMemCtrl, "recvAtomic called but not implemented\n");
    return 0;
}

void 
CXLMemCtrl::CPUPort::recvFunctional(PacketPtr pkt) {
    return ctrl.handleFunctional(pkt);
}

bool
CXLMemCtrl::CPUPort::recvTimingReq(PacketPtr pkt)
{
    // Just forward to the memctrl.
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
CXLMemCtrl::CPUPort::recvRespRetry()
{
    ctrl.recvRespRetry();
}

CXLMemCtrl::MemCtrlPort::
MemCtrlPort(const std::string& name, CXLMemCtrl& _ctrl)
    : RequestPort(name), needRetry(false), 
      needResend(false), blockedPacket(nullptr),
      ctrl(_ctrl)
{ }

bool
CXLMemCtrl::MemCtrlPort::recvTimingResp(PacketPtr pkt)
{
    if (!ctrl.handleResponse(pkt)) {
        needRetry = true;
        return false;
    } else {
        return true;
    }
}

void
CXLMemCtrl::MemCtrlPort::recvReqRetry()
{
    // If ther is still have blocked packet
    // The blocked packet will always keep in the front of the queue
    // if (needRetry && blockedPacket != nullptr) {
    //     PacketPtr pkt = blockedPacket;
    //     // To avoid send same packets twice
    //     if (pkt == ctrl.reqQueue.front()) {
    //         ctrl.reqQueue.pop_front();

    //         if (!sendTimingReq(pkt)) {
    //             // Failure, re-enqueue it and try again
    //             ctrl.reqQueue.push_front(pkt);
    //         } else {
    //             // Success, change states
    //             needRetry = false;
    //             blockedPacket = nullptr;
    //         }

    //     }

    // }

    ctrl.recvReqRetry();
}

void
CXLMemCtrl::MemCtrlPort::recvRangeChange()
{
    ctrl.sendRangeChange();
}


}