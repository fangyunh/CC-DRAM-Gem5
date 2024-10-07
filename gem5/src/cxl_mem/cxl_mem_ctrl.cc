#include "cxl_mem/cxl_mem_ctrl.hh"

#include "base/trace.hh"
#include "debug/DRAM.hh"
#include "debug/CXLMemCtrl.hh"
#include "debug/PacketLatency.hh"
#include "debug/Drain.hh"
#include "mem/dram_interface.hh"
#include "mem/mem_interface.hh"
#include "mem/nvm_interface.hh"
#include "mem/mem_ctrl.hh"
#include "sim/system.hh"

namespace gem5
{

namespace memory
{

// Constructor, inherited form MemCtrl
CXLMemCtrl::CXLMemCtrl(const CXLMemCtrlParams &p) :
    MemCtrl(p)
{
    DPRINTF(CXLMemCtrl, "Setting up CXL Memory Controller\n");
}

bool
CXLMemCtrl::recvTimingReq(PacketPtr pkt)
{
   bool accepted = MemCtrl::recvTimingReq(pkt);

   if (accepted) {
    Tick entryTime = curTick();
    // Store the start time of packet
    packetEntryTime[pkt->id] = entryTime;
    DPRINTF(PacketLatency, "Packet (id: %llu) received at tick %llu\n", pkt->id, entryTime);
   }

   return accepted;     
}

void 
CXLMemCtrl::accessAndRespond(PacketPtr pkt, Tick static_latency,
                                MemInterface* mem_intr)
{
    Tick endTime = curTick();

    auto it = packetEntryTime.find(pkt->id);
    if (it != packetEntryTime.end()) {
        Tick entryTime = it->second;
        Tick latency = endTime - entryTime;
        totalLatency += latency;
        numPackets++;
        packetEntryTime.erase(it);
        packetLatency[it->first] = latency;
        DPRINTF(PacketLatency, "Packet (id: %llu) latency: %llu\n", it->first, latency);
    }
    
    MemCtrl::accessAndRespond(pkt, static_latency, mem_intr);
}

void
CXLMemCtrl::processNextReqEvent(MemInterface* mem_intr,
                          MemPacketQueue& resp_queue,
                          EventFunctionWrapper& resp_event,
                          EventFunctionWrapper& next_req_event,
                          bool& retry_wr_req)
{
    DPRINTF(CXLMemCtrl,
            "processRespondEvent(): Some req has reached its readyTime\n");

    MemPacket* mem_pkt = queue.front();

    // media specific checks and functions when read response is complete
    // DRAM only
    mem_intr->respondEvent(mem_pkt->rank);

    if (mem_pkt->burstHelper) {
        // it is a split packet
        mem_pkt->burstHelper->burstsServiced++;
        if (mem_pkt->burstHelper->burstsServiced ==
            mem_pkt->burstHelper->burstCount) {
            // we have now serviced all children packets of a system packet
            // so we can now respond to the requestor
            // @todo we probably want to have a different front end and back
            // end latency for split packets
            accessAndRespond(mem_pkt->pkt, frontendLatency + backendLatency,
                             mem_intr);
            delete mem_pkt->burstHelper;
            mem_pkt->burstHelper = NULL;
        }
    } else {
        // it is not a split packet
        accessAndRespond(mem_pkt->pkt, frontendLatency + backendLatency,
                         mem_intr);
    }

    queue.pop_front();

    if (!queue.empty()) {
        assert(queue.front()->readyTime >= curTick());
        assert(!resp_event.scheduled());
        schedule(resp_event, queue.front()->readyTime);
    } else {
        // if there is nothing left in any queue, signal a drain
        if (drainState() == DrainState::Draining &&
            !totalWriteQueueSize && !totalReadQueueSize &&
            allIntfDrained()) {

            DPRINTF(Drain, "Controller done draining\n");
            calculateAvgLatency();
            signalDrainDone();
        } else {
            // check the refresh state and kick the refresh event loop
            // into action again if banks already closed and just waiting
            // for read to complete
            // DRAM only
            mem_intr->checkRefreshState(mem_pkt->rank);
        }
    }

    delete mem_pkt;

    // We have made a location in the queue available at this point,
    // so if there is a read that was forced to wait, retry now
    if (retry_rd_req) {
        retry_rd_req = false;
        port.sendRetryReq();
    }
}

void
CXLMemCtrl::calculateAvgLatency()
{
    if (numPackets > 0) {
        Tick avgLatency = totalLatency / numPackets;
        DPRINTF(PacketLatency, "Average Latency: %llu\n", avgLatency);
    } else {
        DPRINTF(PacketLatency, "No packets processed\n");
    }
}

}   
}