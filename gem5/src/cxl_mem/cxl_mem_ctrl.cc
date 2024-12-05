#include "cxl_mem/cxl_mem_ctrl.hh"

#include "base/trace.hh"
#include "debug/DRAM.hh"
#include "debug/CXLMemCtrl.hh"
#include "mem/mem_ctrl.hh"
#include "sim/system.hh"

#include <vector>

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
    cpu_side_ports(name() + ".cpu_side_ports", *this),
    memctrl_side_port(name() + ".memctrl_side_port", *this),
    reqEvent([this] {processRequestEvent();}, name()),
    respEvent([this] {processResponseEvent();}, name()),
    readQueueSize(p.read_buffer_size),
    writeQueueSize(p.write_buffer_size),
    responseQueueSize(p.response_buffer_size),
    prevArrival(0),
    delay(p.delay),
    blockSize(p.compressed_size),
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
    if (!cpu_side_ports.isConnected()) {
        fatal("CXLMemCtrl %s is unconnected on CPU side port!\n", name());
    }

    if (!memctrl_side_port.isConnected()) {
        fatal("CXLMemCtrl %s is unconnected on MemCtrl side port!\n", name());
    }
}


Port &
CXLMemCtrl::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "cpu_side_ports") {
        return cpu_side_ports;
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
            stats.totalReadPacketsSize += size;
            stats.totalReadPacketsNum += 1;
            handleReadRequest(pkt);
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
        // Check if this packet is a response to our compressed read request
        auto it = compressedReadMap.find(pkt);
        if (it != compressedReadMap.end()) {
            // This is our 2KB read response
            PacketPtr original_pkt = it->second;
            // Record read copy time
            Tick start_point = curTick();

            compressedReadMap.erase(it);

            // Extract the requested 64B from the 2KB block
            Addr original_addr = original_pkt->getAddr();
            unsigned original_size = original_pkt->getSize();

            Addr offset = original_addr - pkt->getAddr();
            assert(offset + original_size <= pkt->getSize());

            memcpy(original_pkt->getPtr<uint8_t>(), pkt->getConstPtr<uint8_t>() + offset, original_size);

            // Delete the 2KB packet since we're done with it
            delete pkt;

            // add read copy latency
            stats.totalReadCopyLatency += curTick() - start_point;

            respQueue.push_back(original_pkt);
            
        } else {
            respQueue.push_back(pkt);
        }

        // Forward the packet to the CPU side port
        if (!respEvent.scheduled()) {
            DPRINTF(CXLMemCtrl, "Response scheduled with decompression delay\n");
            schedule(respEvent, curTick() + delay);
        }
        return true;

    } else {
        
        DPRINTF(CXLMemCtrl, "Resp not required for write");
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
CXLMemCtrl::handleReadRequest(PacketPtr pkt)
{
    Addr addr = pkt->getAddr();
    unsigned size = pkt->getSize();

    // Align the address to the start of a 2KB block
    // const unsigned blockSize = 2 * 1024; // 1KB
    // Addr blockStartAddr = addr & ~(blockSize - 1);

    // The size of the block to read is 2KB
    // unsigned blockSizeToRead = blockSize;
    unsigned int cmpSize;
    auto it = compressedBlockSizes.find(addr);
    if (it != compressedBlockSizes.end()) {
        cmpSize = it->second;
        // Align start address to interleaving boundary
        unsigned int interleaveSize = blockSize; // 2KB
        
        Addr startAddr = addr;
        Addr endAddr = startAddr + cmpSize - 1;

        DPRINTF(CXLMemCtrl, "Start addr: %#x, end addr: %#x, compressed size: %d\n", startAddr, endAddr, cmpSize);
        DPRINTF(CXLMemCtrl, "Interleave: %d\n", interleaveSize);
        // Calculate the interleaving regions for the start and end addresses
        unsigned int startRegion = startAddr / interleaveSize;
        unsigned int endRegion = endAddr / interleaveSize;

        DPRINTF(CXLMemCtrl, "Start region: %d, end region: %d\n", startRegion, endRegion);

        if (startRegion != endRegion) {
            // Calculate the new start address aligned to the interleaving boundary
            unsigned int shift = (endAddr % interleaveSize) + 1;
            
            startAddr = addr - shift;

            DPRINTF(CXLMemCtrl, "Shift is: %d\n", shift);
        }

        DPRINTF(CXLMemCtrl, "Creating %d read request from addr %#x to %#x\n",
            cmpSize, startAddr, startAddr + cmpSize - 1);

        // Create a new read request for the 2KB block
        RequestPtr new_req = std::make_shared<Request>(
            startAddr, cmpSize, pkt->req->getFlags(), pkt->req->requestorId());

        PacketPtr new_pkt = new Packet(new_req, MemCmd::ReadReq);
        new_pkt->allocate();

        // Map the original pkt to the new_pkt for later use
        compressedReadMap[new_pkt] = pkt;

        // Add the new packet to the read queue
        readQueue.push_back(new_pkt);
    } else {
        readQueue.push_back(pkt);
        stats.totalNonDRAMReadPacketsNum += 1;
    }

    // Schedule the request event if not already scheduled
    if (!reqEvent.scheduled()) {
        DPRINTF(CXLMemCtrl, "Request scheduled for compressed data packet\n");
        schedule(reqEvent, curTick());
    }
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

    // send response pkt back to cpu
    if (pkt->isResponse()) {
        Tick response_time = curTick() + static_latency;
        cpu_side_ports.schedTimingResp(pkt, response_time);
        return;
    }

    // Mainly for write packets response
    bool needsResponse = pkt->needsResponse();

    if (needsResponse) {
        pkt->makeResponse();

        Tick response_time = curTick() + static_latency;
        cpu_side_ports.schedTimingResp(pkt, response_time);

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
            cmpBlockSizes = LZ4Compression();
            stats.totalCompressionTimes += 1;
        }

        // Update the state to WRITE
        RWState = WRITE;

        if (cmpBlockSizes.empty()) {
            // Compression fail
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

                writeQueue.pop_front();
                cmpedPkt++; // Increment compressed packet count
            }
        } else {
            // Compression succeeded
            unsigned int packetsPerBlock = writePktThreshold / cmpBlockSizes.size();
            unsigned int pktIndex = 0; // Index of the packet being processed
            unsigned int blockIndex = 0; // Index of the current block in cmpBlockSizes

            while (cmpedPkt < writePktThreshold && !writeQEmpty()) {
                PacketPtr pkt = writeQueue.front();

                // Assign the compressed size to the packet address
                compressedBlockSizes[pkt->getAddr()] = cmpBlockSizes[blockIndex];

                // Try to send the packet to mem ctrl
                if (!memctrl_side_port.sendTimingReq(pkt)) {
                    DPRINTF(CXLMemCtrl, "Downstream controller cannot accept packet, will retry\n");
                    // Will retry when recvReqRetry is called
                    resendReq = true;
                    nextRWState = WRITE;
                    return;
                }
                DPRINTF(CXLMemCtrl, "Forwarded packet to downstream controller\n");

                writeQueue.pop_front();
                cmpedPkt++; // Increment compressed packet count
                pktIndex++;

                // Update blockIndex when we've processed packetsPerBlock packets
                if (pktIndex % packetsPerBlock == 0 && blockIndex < cmpBlockSizes.size() - 1) {
                    blockIndex++;
                }
            }
        }

        // Identify next state
        if (cmpedPkt >= writePktThreshold || writeQEmpty()) {
            nextRWState = START;
            cmpedPkt = 0;
            cmpBlockSizes.clear();
        } else {
            nextRWState = WRITE;
        }

    }

    if (!reqEvent.scheduled() && (!(writeQEmpty() && readQEmpty()))) {
        // avoid stuck in loop of waiting, add delay
        schedule(reqEvent, curTick());
    }

    // Request queue is free to accept previous failed packets
    if ((retryWrReq  && !writeQueueFull())) {
        retryWrReq = false;
        cpu_side_ports.sendRetryReq();
    } 
    
    if ((retryRdReq && !readQueueFull())) {
        retryRdReq = false;
        cpu_side_ports.sendRetryReq();
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

        // Record read packets latency from DRAM
        auto readIt = compressedBlockSizes.find(pkt->getAddr());
        if (readIt != compressedBlockSizes.end()) {
            stats.totalDRAMReadLatency += latency;
            stats.totalDRAMReadPacketsNum += 1;
        }
        packetLatency.erase(it);
    }

    // Try to send the packet to the memory controller
    accessAndRespond(pkt, frontendLatency + backendLatency);

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

void 
CXLMemCtrl::fillSourceBuffer(char* srcBuffer, int startIndex, int packetsToProcess,
                             int packetCount)
{
    int offset = 0;
    for (int i = startIndex; i < startIndex + packetsToProcess; ++i) {
        if (i < packetCount) {
            PacketPtr pkt = writeQueue[i];
            const uint8_t* pktData = pkt->getConstPtr<uint8_t>();
            memcpy(srcBuffer + offset, pktData, pkt->getSize());
        } else {
            // Fill remaining space with zeros
            memset(srcBuffer + offset, 0, 64);
        }
        offset += 64; // Each packet is 64 bytes
    }
}

std::vector<unsigned int> 
CXLMemCtrl::DynamicCompression(int blockSizeInKB) 
{
    // Total number of packets in the write queue
    const int packetCount = writeQueue.size();

    // Total number of packets we plan to process
    const int totalPackets = writePktThreshold;

    // Each packet is 64 bytes
    const int packetSize = 64; // bytes

    // Calculate the number of packets per block based on the granularity
    const int packetsPerBlock = (blockSizeInKB * 1024) / packetSize;

    // Calculate the number of blocks
    const int numBlocks = totalPackets / packetsPerBlock;

    // Source size per block in bytes
    const int srcSizePerBlock = packetsPerBlock * packetSize;

    // Destination capacity per block for compression
    const int dstCapacityPerBlock = LZ4_compressBound(srcSizePerBlock);

    // Vector to hold compressed sizes for each block
    std::vector<unsigned int> compressedSizes;

    // Loop over each block
    for (int block = 0; block < numBlocks; ++block) {
        // Allocate source and destination buffers for this block
        char* src = new char[srcSizePerBlock];
        char* dst = new char[dstCapacityPerBlock];

        // Calculate the starting index for this block in the writeQueue
        int startIndex = block * packetsPerBlock;

        // Fill src with data from the current block
        fillSourceBuffer(src, startIndex, packetsPerBlock, packetCount);

        // Compress the block
        int compressedSize = LZ4_compress_default(
            src, dst, srcSizePerBlock, dstCapacityPerBlock);

        // Check for compression failure or incompressible data
        // Define incompressible as compressed size not smaller than original size
        if (compressedSize <= 0 || compressedSize >= srcSizePerBlock) {
            DPRINTF(CXLMemCtrl, "Compression failed or data is incompressible for block %d\n", block);

            // Clean up allocated memory
            delete[] src;
            delete[] dst;

            // Return empty array to indicate failure
            return std::vector<unsigned int>();
        }

        // Add the compressed size to the vector
        compressedSizes.push_back(compressedSize);

        // Clean up allocated memory
        delete[] src;
        delete[] dst;
    }

    // Return the vector of compressed sizes
    return compressedSizes;
}


std::vector<unsigned int> 
CXLMemCtrl::CompressionSelectedSize() 
{
    const unsigned int packetSize = 64; // bytes
    const unsigned int totalPackets = writePktThreshold; // e.g., 64 packets
    const unsigned int totalUncompressedSize = totalPackets * packetSize; // e.g., 4096 bytes

    // Attempt to compress at 1KB granularity
    std::vector<unsigned int> compressedSizes1KB = DynamicCompression(1);
    unsigned int totalCompressedSize1KB = UINT_MAX;
    bool have1KB = false;
    if (!compressedSizes1KB.empty()) {
        totalCompressedSize1KB = 0;
        for (unsigned int size : compressedSizes1KB) {
            totalCompressedSize1KB += size;
        }
        have1KB = true;
    }

    // Attempt to compress at 2KB granularity
    std::vector<unsigned int> compressedSizes2KB = DynamicCompression(2);
    unsigned int totalCompressedSize2KB = UINT_MAX;
    bool have2KB = false;
    if (!compressedSizes2KB.empty()) {
        totalCompressedSize2KB = 0;
        for (unsigned int size : compressedSizes2KB) {
            totalCompressedSize2KB += size;
        }
        have2KB = true;
    }

    // Attempt to compress at 4KB granularity
    std::vector<unsigned int> compressedSizes4KB = DynamicCompression(4);
    unsigned int totalCompressedSize4KB = UINT_MAX;
    bool have4KB = false;
    if (!compressedSizes4KB.empty()) {
        totalCompressedSize4KB = compressedSizes4KB[0]; // Only one block
        have4KB = true;
    }

    // Compare 1KB and 2KB compressed sizes
    std::vector<unsigned int> winnerCompressedSizes;
    unsigned int totalCompressedSizeWinner = UINT_MAX;
    if (have1KB && have2KB) {
        if (totalCompressedSize2KB <= 0.8 * totalCompressedSize1KB) {
            // 2KB is better
            winnerCompressedSizes = compressedSizes2KB;
            totalCompressedSizeWinner = totalCompressedSize2KB;
        } else {
            // 1KB is better
            winnerCompressedSizes = compressedSizes1KB;
            totalCompressedSizeWinner = totalCompressedSize1KB;
        }
    } else if (have1KB) {
        // Only 1KB compression succeeded
        winnerCompressedSizes = compressedSizes1KB;
        totalCompressedSizeWinner = totalCompressedSize1KB;
    } else if (have2KB) {
        // Only 2KB compression succeeded
        winnerCompressedSizes = compressedSizes2KB;
        totalCompressedSizeWinner = totalCompressedSize2KB;
    } else {
        // Neither 1KB nor 2KB compression succeeded
        // Handle as needed, here we proceed to compare with 4KB if available
    }

    // Compare the winner with 4KB compressed size
    if (have4KB) {
        if (totalCompressedSizeWinner != UINT_MAX) {
            // Both winner and 4KB compressed sizes are available
            if (totalCompressedSize4KB <= 0.5 * totalCompressedSizeWinner) {
                // 4KB is significantly better
                return compressedSizes4KB;
            } else {
                // Stick with the winner
                return winnerCompressedSizes;
            }
        } else {
            // Only 4KB compression succeeded
            return compressedSizes4KB;
        }
    } else {
        if (totalCompressedSizeWinner != UINT_MAX) {
            // Return the winner
            return winnerCompressedSizes;
        } else {
            // No compression succeeded
            // Handle as needed, here we return an empty vector
            DPRINTF(CXLMemCtrl, "Compression failed at all granularities\n");
            return std::vector<unsigned int>();
        }
    }
}

std::vector<unsigned int> 
CXLMemCtrl::LZ4Compression() {
    // Calculate total source size and destination capacity
    std::vector<unsigned int> selectedCompressedSizes = CompressionSelectedSize();

    if (!selectedCompressedSizes.empty()) {
        for (size_t i = 0; i < selectedCompressedSizes.size(); ++i) {
            unsigned int compressedSize = selectedCompressedSizes[i];

            if (compressedSize > 0) {
                // Round up compressed size to the next multiple of 64 bytes
                if (compressedSize % 64 != 0) {
                    compressedSize = ((compressedSize + 63) / 64) * 64;
                }

                // Update the vector with the adjusted compressed size
                selectedCompressedSizes[i] = compressedSize;

                // Update statistics
                stats.totalCompressedPacketsSize += compressedSize;
                stats.totalCompressedPacketsNum += 1;
                stats.compressedSizeHistogram.sample(compressedSize);
            } else {
                DPRINTF(CXLMemCtrl, "Compression failed for block %zu\n", i);
                selectedCompressedSizes[i] = 0;
            }
        }
    }
    return selectedCompressedSizes;
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
    cpu_side_ports.sendRangeChange();
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
    ADD_STAT(totalDRAMReadLatency, statistics::units::Tick::get(),
            "Total Read to DRAM latency of all packets in Tick"),
    ADD_STAT(totalWriteLatency, statistics::units::Tick::get(),
            "Total write latency of all packets in Tick"),
    ADD_STAT(totGap, statistics::units::Tick::get(),
            "Total gap between packets in Tick"),
    ADD_STAT(totalReadCopyLatency, statistics::units::Tick::get(),
            "Total Read Copy latency"),

    ADD_STAT(totalPacketsNum, statistics::units::Count::get(),
            "Total number of packets"),
    ADD_STAT(totalCompressedPacketsNum, statistics::units::Count::get(),
            "Total number of compressed packets"),
    ADD_STAT(totalReadPacketsNum, statistics::units::Count::get(),
            "Total number of read packets"),
    ADD_STAT(totalWritePacketsNum, statistics::units::Count::get(),
            "Total number of write packets"),
    ADD_STAT(totalDRAMReadPacketsNum, statistics::units::Count::get(),
            "Total number of DRAM read packets"),
    ADD_STAT(totalNonDRAMReadPacketsNum, statistics::units::Count::get(),
            "Total number of DRAM read packets"),        
    
    ADD_STAT(totalCompressionTimes, statistics::units::Count::get(),
            "Total number of compression happens"),
    
    
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
    ADD_STAT(compressedSizeHistogram, statistics::units::Tick::get(),
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
                statistics::units::Byte, statistics::units::Count>::get(),
            "Average compressed packet size in Bytes"),
    ADD_STAT(avgDRAMReadLatency, statistics::units::Rate<
                statistics::units::Tick, statistics::units::Count>::get(),
            "Average DRAM read latency per packet in ns"),
    ADD_STAT(avgReadCopyLatency, statistics::units::Rate<
                statistics::units::Tick, statistics::units::Count>::get(),
            "Average DRAM actual read copy latency per packet in ns")
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
    totalDRAMReadLatency
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
    
    compressedSizeHistogram
        .init(10)
        .flags(nozero | nonan)
        ;

    // Configure avgLatency
    avgLatency.precision(4);
    avgReadLatency.precision(4);
    avgWriteLatency.precision(4);
    avgDRAMReadLatency.precision(4);
    avgRdBWSys.precision(8);
    avgWrBWSys.precision(8);

    // For 1GHz clock
    int tick_to_ns = 1000;
    avgLatency = totalLatency / totalPacketsNum / tick_to_ns;
    avgReadLatency = totalReadLatency / totalReadPacketsNum / tick_to_ns;
    avgWriteLatency = totalWriteLatency / totalWritePacketsNum / tick_to_ns;
    avgDRAMReadLatency = totalDRAMReadLatency / totalDRAMReadPacketsNum / tick_to_ns;
    avgReadCopyLatency = totalReadCopyLatency / totalDRAMReadPacketsNum / tick_to_ns;

    // Configure avg Compressed Size
    avgCompressedSize.precision(4);
    avgCompressedSize = totalCompressedPacketsSize /  totalCompressedPacketsNum;
    // Formula stats
    avgRdBWSys = (totalReadPacketsSize) / simSeconds;
    avgWrBWSys = (totalWritePacketsSize) / simSeconds;

}

CXLMemCtrl::CPUPort::
CPUPort(const std::string& name, CXLMemCtrl& _ctrl)
    : QueuedResponsePort(name, queue), queue(_ctrl, *this, true), ctrl(_ctrl)
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