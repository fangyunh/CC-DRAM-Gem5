#include "cxl_mem/cxl_mem_ctrl.hh"

#include "base/trace.hh"
#include "debug/DRAM.hh"
#include "debug/CXLMemCtrl.hh"
#include "mem/mem_ctrl.hh"
#include "sim/system.hh"

extern "C" {
    // include lz4
    #include "lz4.h"
}


namespace gem5
{

namespace memory
{


// Constructor
CXLMemCtrl::CXLMemCtrl(const CXLMemCtrlParams &p) :
    ClockedObject(p),
    cpu_side_port(name() + ".cpu_side_port", *this),
    memctrl_side_port(name() + ".memctrl_side_port", *this),
    reqEvent([this] {processRequestEvent();}, name()),
    respEvent([this] {processResponseEvent();}, name()),
    readQueueSize(p.read_buffer_size),
    writeQueueSize(p.write_buffer_size),
    responseQueueSize(p.response_buffer_size),
    prevArrival(0),
    delay(p.delay),
    writePktThreshold(p.write_pkt_threshold),
    frontendLatency(p.static_frontend_latency),
    backendLatency(p.static_backend_latency),
    stats(*this)
{
    DPRINTF(CXLMemCtrl, "Setting up CXL Memory Controller\n");
    RWState = READ;    // current state
    nextRWState = START;

    // retry to receive req
    retryRdReq = false;
    retryWrReq = false;

    // Need resend the packet to CPU
    resendReq = false;

    // loss to receive resp from memory ctrl
    resendMemResp = false;

    // fail to send resp to CPU
    retryMemResp = false;

    cmpedPkt = 0;
    goDraining = false;
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
CXLMemCtrl::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(CXLMemCtrl, "Received timing request: %s addr %#x size %d\n",
            pkt->cmdString(), pkt->getAddr(), pkt->getSize());
    
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    panic_if(!(pkt->isRead() || pkt->isWrite()),
             "Should only see read and writes at memory controller\n");


    if (prevArrival != 0) {
        stats.totGap += curTick() - prevArrival;
    }
    prevArrival = curTick();

    unsigned size = pkt->getSize();
    packetLatency[pkt->id] = curTick();

    stats.totalPacketsNum++;
    stats.totalPacketsSize += size;
    
    // Classify read and write packets
    if (pkt->isWrite()) {
        assert(size != 0);
        
        if (writeQueueFull()) {
            DPRINTF(CXLMemCtrl, "Write queue full, not accepting\n");
            retryWrReq = true;
            return false;
        } else {

            stats.totalWritePacketsNum++;
            stats.totalWritePacketsSize += size;

            // **Coalesce write to existing entry if address matches**
            bool found = false;
            for (auto &write_pkt : writeQueue) {
                if (write_pkt->getAddr() == pkt->getAddr() &&
                    write_pkt->getSize() == pkt->getSize()) {
                    // Update existing write packet with new data
                    memcpy(write_pkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(), pkt->getSize());
                    found = true;

                    /** Erase records in the packetLatency */
                    auto it = packetLatency.find(pkt->id);
                    if (it != packetLatency.end()) {
                        packetLatency.erase(it);
                    }
                    DPRINTF(CXLMemCtrl, "Don't need to enqueue in write, updated\n");
                    break;
                }
            } 

            if (!found) {
                // // **Create a copy of the write packet**
                PacketPtr write_pkt = new Packet(pkt->req, pkt->cmd);
                // **Allocate data storage for the packet**
                write_pkt->allocate();
                memcpy(write_pkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(), pkt->getSize());

                // // remove previous packet and add the write packet
                // auto it = packetLatency.find(pkt->id);
                // if (it != packetLatency.end()) {
                //     packetLatency.erase(it);
                // }
                // packetLatency[write_pkt->id] = curTick();

                // writeQueue.push_back(write_pkt);
                DPRINTF(CXLMemCtrl, "Enqueue in Write queue\n");
                
                writeQueue.push_back(write_pkt);
            }

            // Respond the write request
            accessAndRespond(pkt, frontendLatency);

            if (!reqEvent.scheduled() && 
                ((drainState() == DrainState::Draining && !writeQEmpty()) ||
                writeQueue.size() > writePktThreshold)) {
                DPRINTF(CXLMemCtrl, " write Request scheduled immediately\n");
                schedule(reqEvent, curTick());
            }
        }
    } else {
        assert(pkt->isRead());
        assert(size != 0);
        
        // see if it can be handled in write queue
        if (findInWriteQueue(pkt)) {
            DPRINTF(CXLMemCtrl, "Read to addr %#x serviced by write queue\n", pkt->getAddr());
            // record the packet is read packet
            stats.totalReadPacketsNum++;
            stats.totalReadPacketsSize += size;

            // Record read statistics
            auto it = packetLatency.find(pkt->id);
            if (it != packetLatency.end()) {
                Tick latency = curTick() - it->second;

                // Respectively store stats of read or write packet
                stats.totalReadLatency += latency;
                stats.readLatencyHistogram.sample(latency);
                
                stats.totalLatency += latency;

                stats.latencyHistogram.sample(latency);
                
                packetLatency.erase(it);
            }

            // Respond immediately using data from write queue
            accessAndRespond(pkt, frontendLatency);

            return true;
        }

        if (readQueueFull()) {
            DPRINTF(CXLMemCtrl, "Read queue full, not accepting\n");
            retryRdReq = true;
            return false;
        } else {

            stats.totalReadPacketsNum++;
            stats.totalReadPacketsSize += size;
            readQueue.push_back(pkt);

            if (!reqEvent.scheduled()) {
                DPRINTF(CXLMemCtrl, "Request scheduled immediately\n");
                schedule(reqEvent, curTick());
            }
        }
    }

    return true;
}

bool
CXLMemCtrl::recvTimingResp(PacketPtr pkt)
{
    DPRINTF(CXLMemCtrl, "Received timing response: %s addr %#x size %d\n",
            pkt->cmdString(), pkt->getAddr(), pkt->getSize());

    if (respQueueFull()) {
        DPRINTF(CXLMemCtrl, "Response queue full, cannot accept packet\n");
        resendMemResp = true;
        return false;
    }

    if (pkt->isRead()) {
        respQueue.push_back(pkt);

        // Forward the packet to the CPU side port
        if (!respEvent.scheduled()) {
            DPRINTF(CXLMemCtrl, "Response scheduled immediately\n");
            schedule(respEvent, curTick());
        }
        return true;
    } else {
        
        DPRINTF(CXLMemCtrl, "Resp not required for write");
        
        delete pkt;
        return true;
    }

    return true;
}

void
CXLMemCtrl::handleFunctional(PacketPtr pkt)
{
    memctrl_side_port.sendFunctional(pkt);
}

void
CXLMemCtrl::recvReqRetry() {
    if (resendReq && (!reqEvent.scheduled())) {
        resendReq = false;
        schedule(reqEvent, curTick());
    }
}

void
CXLMemCtrl::accessAndRespond(PacketPtr pkt, Tick static_latency)
{
    DPRINTF(CXLMemCtrl, "Responding to Address %#x.. \n", pkt->getAddr());
    bool needsResponse = pkt->needsResponse();


    if (needsResponse) {
        pkt->makeResponse();
        assert(pkt->isResponse());

        DPRINTF(CXLMemCtrl, "Test if pkt data can be accessed\n");
        const uint8_t* data = pkt->getConstPtr<uint8_t>();
        unsigned size = pkt->getSize();

        Tick response_time = curTick() + static_latency;

        // Currently does not set latency
        cpu_side_port.sendTimingResp(pkt);
    } else {
        DPRINTF(CXLMemCtrl, "No need to respond\n");
    }

    DPRINTF(CXLMemCtrl, "Done\n");
}

void
CXLMemCtrl::recvRespRetry() {
    // If ther is still have blocked packet
    // The blocked packet will always keep in the front of the queue
    // if (needRetry && blockedPacket != nullptr) {
    if (retryMemResp) {
        retryMemResp = false;

        if (!respEvent.scheduled()) {
            schedule(respEvent, curTick());
        }
        
    }
}

bool
CXLMemCtrl::findInWriteQueue(PacketPtr pkt)
{
    Addr addr = pkt->getAddr();
    unsigned size = pkt->getSize();
    unsigned remaining = size;
    unsigned current_addr = addr;
    const unsigned cacheline_size = 64;

    for (auto it = writeQueue.begin(); it != writeQueue.end(); ++it) {
        PacketPtr write_pkt = *it;
        Addr write_addr = write_pkt->getAddr();
        unsigned write_size = write_pkt->getSize();

        // if (current_addr >= write_addr && current_addr < (write_addr + write_size)) {
        //     unsigned offset = current_addr - write_addr;
        //     unsigned copy_size = std::min(remaining, (unsigned)(write_size - offset));
        //     memcpy(pkt->getPtr<uint8_t>() + (size - remaining), write_pkt->getConstPtr<uint8_t>() + offset, copy_size);
        //     remaining -= copy_size;
        //     current_addr += copy_size;

        //     if (remaining == 0) {
        //         return true;
        //     }
        // }
        if (addr == write_addr && size == cacheline_size) {
            memcpy(pkt->getPtr<uint8_t>(), write_pkt->getConstPtr<uint8_t>(), cacheline_size);
            return true;
        }
    }
    return false;
}



// Send request to memory controller
void
CXLMemCtrl::processRequestEvent()
{
    // wait for a resend
    if (resendReq) {
        return;
    }

    // Initialize the nextRWState
    if (nextRWState == START) {
        if ((drainState() == DrainState::Draining && !writeQEmpty()) ||
            writeQueue.size() >= writePktThreshold)
        {
            nextRWState = WRITE;
        } else if (!readQEmpty()) {
            nextRWState = READ;
        } else {
            nextRWState = START;
            return;
        }
    }

    // wait for read request until time out
    DPRINTF(CXLMemCtrl, "The state need to process is %d\n", nextRWState);
    DPRINTF(CXLMemCtrl, "Read queue size: %d, Write queue size: %d\n", readQueue.size(), writeQueue.size());
    if (nextRWState == READ) {
        PacketPtr pkt = readQueue.front();
        // Try to send request to downside memory controller
        if (!memctrl_side_port.sendTimingReq(pkt)) {
            DPRINTF(CXLMemCtrl, "Downstream controller cannot accept packet, will retry\n");
            // Will retry when recvReqRetry is called
            resendReq = true;
            nextRWState = READ;
            return;
        }
        DPRINTF(CXLMemCtrl, "Forwarded packet to downstream controller\n");
        readQueue.pop_front();
        // update this state of processed req
        RWState = READ;

        // Identify next state
        // If write queue size overflow the threshold, execute write req in the next
        // Else read queue is empty, we wait for couple cycles
        // When timed out, we execute remaining write packets
        if ((drainState() == DrainState::Draining && !writeQEmpty()) ||
            writeQueue.size() > writePktThreshold) {
            nextRWState = WRITE;
        } else {
            if (readQEmpty()) {
                nextRWState = START;
            } else {
                nextRWState = READ;
            }
        }

    } else {
        assert(nextRWState == WRITE);
        if (nextRWState != RWState) {
            // If it is first time to write, compress the data
            LZ4Compression();
        }

        // Update the state to WRITE
        RWState = WRITE;

        
        while (cmpedPkt < writePktThreshold && !writeQEmpty()) {
            PacketPtr pkt = writeQueue.front();
            // Try to send the packet to mem ctrl
            if (!memctrl_side_port.sendTimingReq(pkt)) {
                DPRINTF(CXLMemCtrl, "Downstream controller cannot accept packet, will retry\n");
                // Will retry when recvReqRetry is called
                resendReq = true;
                nextRWState = WRITE;
                return;
            }
            DPRINTF(CXLMemCtrl, "Forwarded packet to downstream controller\n");

            // **Record write latency here**
            auto it = packetLatency.find(pkt->id);
            if (it != packetLatency.end()) {
                Tick latency = curTick() - it->second;
                stats.totalWriteLatency += latency;
                stats.writeLatencyHistogram.sample(latency);
                stats.totalLatency += latency;
                stats.latencyHistogram.sample(latency);
                packetLatency.erase(it);
            }

            writeQueue.pop_front();
            // Increment compressed packets number
            stats.totalCompressedPacketsNum += 1;
            cmpedPkt++; // increment compressed pkt num
        }

        // Identify next state
        if (cmpedPkt >= writePktThreshold || writeQEmpty()) {
            nextRWState = START;
            cmpedPkt = 0;
        } else {
            nextRWState = WRITE;
        }

    }

    if (!reqEvent.scheduled() && (!(writeQEmpty() && readQEmpty()))) {
        // avoid stuck in loop of waiting, add delay
        schedule(reqEvent, curTick() + delay);
    }

    // Request queue is free to accept previous failed packets
    if ((retryWrReq  && !writeQueueFull())) {
        retryWrReq = false;
        cpu_side_port.sendRetryReq();
    } 
    
    if ((retryRdReq && !readQueueFull())) {
        retryRdReq = false;
        cpu_side_port.sendRetryReq();
    }

    if (drainState() == DrainState::Draining && writeQEmpty() && readQEmpty() && respQEmpty()) {
        signalDrainDone();
    }

}

// Send response back to CPU
void
CXLMemCtrl::processResponseEvent()
{
    if (respQEmpty()) {
        return;
    }

    PacketPtr pkt = respQueue.front();
    
    auto it = packetLatency.find(pkt->id);
    if (it != packetLatency.end()) {
        Tick latency = curTick() - it->second;

        // Write responses are handled by accessAndResp()
        stats.totalReadLatency += latency;
        stats.readLatencyHistogram.sample(latency);
        
        stats.totalLatency += latency;

        stats.latencyHistogram.sample(latency);
        
        packetLatency.erase(it);
    }

    // Try to send the packet to the memory controller
    if (!cpu_side_port.sendTimingResp(pkt)) {
        DPRINTF(CXLMemCtrl, "CPU cannot accept response, will retry\n");
        // Retry the to send packet
        retryMemResp = true;
        return;
    }

    DPRINTF(CXLMemCtrl, "Sent response back to CPU\n");
    respQueue.pop_front();
    

    // Response queue is free to accept previous failed packets
    if (!respQueueFull() && resendMemResp) {
        resendMemResp = false;
        memctrl_side_port.sendRetryResp();
    }

    // schedule if resp queue still contains packets to send
    if (!respQEmpty() && !respEvent.scheduled()) {
        schedule(respEvent, curTick());
    }

    // After processing, check if we're draining and response queue is empty
    if (drainState() == DrainState::Draining && writeQEmpty() && readQEmpty() && respQEmpty()) {
        signalDrainDone();
    }
}

void CXLMemCtrl::LZ4Compression() {
    // Calculate total source size and destination capacity
    const int srcSize = writePktThreshold * 64; 
    int dstCapacity = LZ4_compressBound(srcSize);
    char* src = new char[srcSize];
    char* dst = new char[dstCapacity];

    int offset = 0;
    int packetCount = writeQueue.size();

    // Loop to gather data from available packets in writeQueue
    for (int i = 0; i < writePktThreshold; ++i) {
        if (i < packetCount) {
            PacketPtr pkt = writeQueue[i];
            const uint8_t* pktData = pkt->getConstPtr<uint8_t>();
            memcpy(src + offset, pktData, pkt->getSize());
            offset += pkt->getSize();
        } else {
            // Fill remaining space with zeros
            memset(src + offset, 0, 64); 
            offset += 64;
        }
    }

    int compressedSize = LZ4_compress_default(src, dst, srcSize, dstCapacity);
    if (compressedSize > 0) {
        DPRINTF(CXLMemCtrl, "Compressed %d packets from %d bytes to %d bytes\n",
                writePktThreshold, srcSize, compressedSize);
        stats.totalCompressedPacketsSize += compressedSize;
    } else {
        DPRINTF(CXLMemCtrl, "Compression failed for packets\n");
    }

    delete[] src;
    delete[] dst;
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
CXLMemCtrl::readQueueFull() const
{
    return readQueue.size() >= readQueueSize; 
}

bool
CXLMemCtrl::writeQueueFull() const
{
    return writeQueue.size() >= writeQueueSize; 
}

bool
CXLMemCtrl::respQueueFull() const
{
    return respQueue.size() >= responseQueueSize; 
}

CXLMemCtrl::CXLStats::
CXLStats(CXLMemCtrl &_cxlmc)
    : statistics::Group(&_cxlmc),
      cxlmc(_cxlmc),

    ADD_STAT(totalLatency, statistics::units::Tick::get(),
            "Total latency of all packets in Tick"),
    ADD_STAT(totalReadLatency, statistics::units::Tick::get(),
            "Total Read latency of all packets in Tick"),
    ADD_STAT(totalWriteLatency, statistics::units::Tick::get(),
            "Total write latency of all packets in Tick"),
    ADD_STAT(totGap, statistics::units::Tick::get(),
            "Total gap between packets in Tick"),

    ADD_STAT(totalPacketsNum, statistics::units::Count::get(),
            "Total number of packets"),
    ADD_STAT(totalCompressedPacketsNum, statistics::units::Count::get(),
            "Total number of compressed packets"),
    ADD_STAT(totalReadPacketsNum, statistics::units::Count::get(),
            "Total number of read packets"),
    ADD_STAT(totalWritePacketsNum, statistics::units::Count::get(),
            "Total number of write packets"),
    
    ADD_STAT(totalPacketsSize, statistics::units::Byte::get(),
            "Total size of packets in Bytes"),
    ADD_STAT(totalReadPacketsSize, statistics::units::Byte::get(),
            "Total size of read packets in Bytes"),
    ADD_STAT(totalWritePacketsSize, statistics::units::Byte::get(),
            "Total size of write packets in Bytes"),
    ADD_STAT(totalCompressedPacketsSize, statistics::units::Byte::get(),
            "Total compressed size of packets in Bytes"),
    
    ADD_STAT(avgRdBWSys, statistics::units::Rate<
                statistics::units::Byte, statistics::units::Second>::get(),
             "Average system read bandwidth in Byte/s"),
    ADD_STAT(avgWrBWSys, statistics::units::Rate<
                statistics::units::Byte, statistics::units::Second>::get(),
             "Average system write bandwidth in Byte/s"),
    

    ADD_STAT(latencyHistogram, statistics::units::Tick::get(),
            "Latency histogram"),
    ADD_STAT(readLatencyHistogram, statistics::units::Tick::get(),
            "Read Latency histogram"),
    ADD_STAT(writeLatencyHistogram, statistics::units::Tick::get(),
            "Write Latency histogram"),

    ADD_STAT(avgLatency, statistics::units::Rate<
                statistics::units::Tick, statistics::units::Count>::get(),
            "Average latency per packet in ns"),
    ADD_STAT(avgReadLatency, statistics::units::Rate<
                statistics::units::Tick, statistics::units::Count>::get(),
            "Average Read latency per packet in ns"),
    ADD_STAT(avgWriteLatency, statistics::units::Rate<
                statistics::units::Tick, statistics::units::Count>::get(),
            "Average Write latency per packet in ns"),
    ADD_STAT(avgCompressedSize, statistics::units::Rate<
                statistics::units::Tick, statistics::units::Count>::get(),
            "Average compressed packet size in Bytes")
{ }

void
CXLMemCtrl::CXLStats::regStats()
{
    using namespace statistics;
    //statistics::Group::regStats();

    // Configure totalLatency
    totalLatency
        .flags(nozero | nonan)
        ;
    
    totalReadLatency
        .flags(nozero | nonan)
        ;
    
    totalWriteLatency
        .flags(nozero | nonan)
        ;


    // Configure latencyHistogram
    latencyHistogram
        .init(10)
        .flags(nozero | nonan)
        ;
    
    readLatencyHistogram
        .init(10)
        .flags(nozero | nonan)
        ;
    
    writeLatencyHistogram
        .init(10)
        .flags(nozero | nonan)
        ;

    // Configure avgLatency
    avgLatency.precision(4);
    avgReadLatency.precision(4);
    avgWriteLatency.precision(4);
    avgRdBWSys.precision(8);
    avgWrBWSys.precision(8);

    // For 1GHz clock
    int tick_to_ns = 1000;
    avgLatency = totalLatency / totalPacketsNum / tick_to_ns;
    avgReadLatency = totalReadLatency / totalReadPacketsNum / tick_to_ns;
    avgWriteLatency = totalWriteLatency / totalWritePacketsNum / tick_to_ns;

    // Configure avg Compressed Size
    avgCompressedSize.precision(4);
    avgCompressedSize = totalCompressedPacketsSize /  totalCompressedPacketsNum;
    // Formula stats
    avgRdBWSys = (totalReadPacketsSize) / simSeconds;
    avgWrBWSys = (totalWritePacketsSize) / simSeconds;

}

CXLMemCtrl::CPUPort::
CPUPort(const std::string& name, CXLMemCtrl& _ctrl)
    : ResponsePort(name), ctrl(_ctrl)
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
    : RequestPort(name), ctrl(_ctrl)
{ }

bool
CXLMemCtrl::MemCtrlPort::recvTimingResp(PacketPtr pkt)
{
    return ctrl.recvTimingResp(pkt);
}

void
CXLMemCtrl::MemCtrlPort::recvReqRetry()
{
    ctrl.recvReqRetry();
}

void
CXLMemCtrl::MemCtrlPort::recvRangeChange()
{
    ctrl.sendRangeChange();
}

DrainState
CXLMemCtrl::drain()
{
    // If there are pending writes or reads, we need to continue processing
    if (!writeQEmpty() || !readQEmpty() || !respQEmpty()) {
        // Schedule events to process remaining requests
        if (!reqEvent.scheduled()) {
            schedule(reqEvent, curTick());
        }
        if (!respEvent.scheduled()) {
            schedule(respEvent, curTick());
        }
        // Indicate that we're draining and will finish later
        return DrainState::Draining;
    } else {
        // No pending operations, we can drain immediately
        return DrainState::Drained;
    }
}

} // namespace memory

}