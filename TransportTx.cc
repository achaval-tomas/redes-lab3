#ifndef TRANSPORT_TX
#define TRANSPORT_TX

#include "DataPkt_m.h"
#include "FeedbackPkt_m.h"
#include "TimeoutMsg_m.h"
#include <algorithm>
#include <deque>
#include <omnetpp.h>
#include <string.h>

enum PacketStatus {
    Ready,
    Sent,
    Acked,
};

struct DataPktWithStatus {
    DataPkt* pkt;
    PacketStatus status;
};

using namespace omnetpp;

class TransportTx : public cSimpleModule {
private:
    std::deque<DataPktWithStatus> buffer;
    cMessage* endServiceEvent = NULL;
    simtime_t serviceTime;
    unsigned int windowStart = 0;
    unsigned int windowSize = 0;
    unsigned int inFlightPackets = 0;

public:
    TransportTx();
    ~TransportTx() override;

protected:
    void initialize() override;
    void finish() override;
    void handleMessage(cMessage* msg) override;

private:
    void handleEndServiceMessage();
    void handleDataPacket(DataPkt* pkt);
    void handleFeedbackPacket(FeedbackPkt* pkt);
    void trySlideWindow();
    void handleTimeoutMessage(TimeoutMsg* msg);
};
Define_Module(TransportTx);

TransportTx::TransportTx() {
}

TransportTx::~TransportTx() {
    cancelAndDelete(endServiceEvent);
}

void TransportTx::initialize() {
    endServiceEvent = new cMessage("endService");
    // MAYBE: Dynamically adjust window size
    windowSize = par("windowSize");
}

void TransportTx::finish() {
}

void TransportTx::handleMessage(cMessage* msg) {
    if (msg == endServiceEvent) {
        handleEndServiceMessage();
    } else if (dynamic_cast<DataPkt*>(msg)) {
        handleDataPacket(dynamic_cast<DataPkt*>(msg));
    } else if (dynamic_cast<FeedbackPkt*>(msg)) {
        handleFeedbackPacket(dynamic_cast<FeedbackPkt*>(msg));
    } else if (dynamic_cast<TimeoutMsg*>(msg)) {
        handleTimeoutMessage(dynamic_cast<TimeoutMsg*>(msg));
    } else {
        EV_ERROR << "[TTX] ERROR: unknown packet" << std::endl;
    }
}

void TransportTx::handleEndServiceMessage() {
    EV_TRACE << "[TTX] trying to send a packet" << std::endl;

    if (inFlightPackets >= windowSize) {
        EV_TRACE << "[TTX] window is full, skipping" << std::endl;
        return;
    }

    EV_TRACE << "[TTX] searching for packet to send" << std::endl;

    auto pktToSend = std::find_if(buffer.begin(), buffer.end(), [](const DataPktWithStatus& p) {
        return p.status == PacketStatus::Ready;
    });
    if (pktToSend == buffer.end()) {
        return;
    }

    EV_TRACE << "[TTX] sending packet " << pktToSend->pkt->getSeqNumber() << std::endl;

    send(pktToSend->pkt, "toOut$o");
    pktToSend->status = PacketStatus::Sent;
    inFlightPackets++;

    serviceTime = pktToSend->pkt->getDuration();
    scheduleAt(simTime() + serviceTime, endServiceEvent);
}

// From Generator
void TransportTx::handleDataPacket(DataPkt* pkt) {
    EV_TRACE << "[TTX] received new data packet " << std::endl;

    if (buffer.size() >= par("bufferSize").intValue()) {
        EV_TRACE << "[TTX] buffer is full, dropping packet " << std::endl;

        delete pkt;
        this->bubble("packet dropped");
        return;
    }

    auto seqNumber = windowStart + buffer.size();
    pkt->setSeqNumber(seqNumber);

    auto name = "seq=" + std::to_string(seqNumber);
    pkt->setName(name.c_str());

    EV_TRACE << "[TTX] assigned seq n° " << seqNumber << " to data packet" << std::endl;

    auto timeoutMsg = new TimeoutMsg("timeout");
    scheduleAt(simTime() + par("timeoutTime"), timeoutMsg);

    buffer.push_back(DataPktWithStatus { pkt, PacketStatus::Ready });

    if (!endServiceEvent->isScheduled()) {
        scheduleAt(simTime(), endServiceEvent);
    }
}

// From Receiver
void TransportTx::handleFeedbackPacket(FeedbackPkt* pkt) {
    auto ackNumber = pkt->getAckNumber();
    delete pkt;

    EV_TRACE << "[TTX] received feedback packet for " << ackNumber << std::endl;

    if (ackNumber < windowStart) {
        EV_INFO << "[TTX] packet " << ackNumber << " already acked" << std::endl;
        return;
    }

    auto pktIdx = ackNumber - windowStart;
    if (pktIdx >= windowSize || pktIdx >= buffer.size()) {
        EV_ERROR << "[TTX] pktIdx out of bounds" << std::endl;
        return;
    }

    // MAYBE: windowSize = pkt->getWindowSize();
    buffer[pktIdx].status = PacketStatus::Acked;
    inFlightPackets--;

    EV_TRACE << "[TTX] received ack for packet " << ackNumber << std::endl;

    trySlideWindow();

    if (!endServiceEvent->isScheduled()) {
        scheduleAt(simTime(), endServiceEvent);
    }
}

void TransportTx::trySlideWindow() {
    auto leadingAckedCount = 0;
    for (auto pkt : buffer) {
        if (pkt.status != PacketStatus::Acked) {
            break;
        }
        leadingAckedCount += 1;
    }

    EV_TRACE << "[TTX] sliding window by " << leadingAckedCount << std::endl;

    for (auto i = 0; i < leadingAckedCount; ++i) {
        buffer.pop_front();
    }

    windowStart += leadingAckedCount;

    EV_TRACE << "[TTX] new window is [" << windowStart << ", "
             << windowStart + windowSize << ")" << std::endl;
}

void TransportTx::handleTimeoutMessage(TimeoutMsg* msg) {
    auto seqNumber = msg->getSeqNumber();
    delete msg;

    if (seqNumber < windowStart) {
        // Packet already acked.
        return;
    }

    auto pktIdx = seqNumber - windowStart;
    if (pktIdx >= windowSize || pktIdx >= buffer.size()) {
        EV_ERROR << "[TTX] pktIdx out of bounds (timeout message)" << std::endl;
        return;
    }

    auto packet = &buffer[pktIdx];
    if (packet->status == PacketStatus::Acked) {
        // Packet already acked.
        return;
    }

    EV_TRACE << "[TTX] packet " << packet->pkt->getSeqNumber() << " timeout" << std::endl;

    packet->status = PacketStatus::Ready;
    assert(inFlightPackets != 0); // TODO: estar convencidos de que esto nunca pasa.
    inFlightPackets--; // Asumimos que se perdio.

    if (!endServiceEvent->isScheduled()) {
        scheduleAt(simTime(), endServiceEvent);
    }
}

#endif /* TRANSPORT_TX */
