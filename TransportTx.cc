#ifndef TRANSPORT_TX
#define TRANSPORT_TX

#include "DataPkt_m.h"
#include "FeedbackPkt_m.h"
#include "TimeoutMsg_m.h"
#include <algorithm>
#include <deque>
#include <omnetpp.h>
#include <string.h>

enum PacketKind {
    Data,
    Feedback,
    Timeout,
};

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
    void handleEndServiceMessage(cMessage* msg);
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
    scheduleAt(simTime(), endServiceEvent);
}

void TransportTx::finish() {
}

void TransportTx::handleMessage(cMessage* msg) {
    if (msg == endServiceEvent) {
        handleEndServiceMessage(msg);
    } else if (msg->getKind() == PacketKind::Data) {
        handleDataPacket(dynamic_cast<DataPkt*>(msg));
    } else if (msg->getKind() == PacketKind::Feedback) {
        handleFeedbackPacket(dynamic_cast<FeedbackPkt*>(msg));
    } else if (msg->getKind() == PacketKind::Timeout) {
        handleTimeoutMessage(dynamic_cast<TimeoutMsg*>(msg));
    }
}

void TransportTx::handleEndServiceMessage(cMessage* msg) {
    if (inFlightPackets >= windowSize) {
        return;
    }

    auto pktToSend = std::find_if(buffer.begin(), buffer.end(), [](auto p) { return p.status == PacketStatus::Ready; });
    if (pktToSend == buffer.end()) {
        return;
    }

    send(pktToSend->pkt, "toOut$o");
    pktToSend->status = PacketStatus::Sent;

    serviceTime = pktToSend->pkt->getDuration();
    // TODO: make sure that omnet always delivers scheduled self-messages.
    scheduleAt(simTime() + serviceTime, endServiceEvent);
}

// From Generator
void TransportTx::handleDataPacket(DataPkt* pkt) {
    if (buffer.size() >= par("bufferSize").intValue()) {
        delete pkt;
        this->bubble("packet dropped");
    } else {
        auto seqNumber = windowStart + buffer.size();
        pkt->setSeqNumber(seqNumber);

        auto timeoutMsg = new TimeoutMsg("timeout", PacketKind::Timeout);
        scheduleAt(simTime() + par("timeoutTime"), timeoutMsg);

        buffer.push_back(DataPktWithStatus { pkt, PacketStatus::Ready });
    }
}

// From Receiver
void TransportTx::handleFeedbackPacket(FeedbackPkt* pkt) {
    auto ackNumber = pkt->getAckNumber();
    if (ackNumber < windowStart) {
        return;
    }

    auto pktIdx = ackNumber - windowStart;
    if (pktIdx >= windowSize || pktIdx >= buffer.size()) {
        std::cerr << "pktIdx out of bounds (feedback packet)" << std::endl;
        return;
    }

    // MAYBE: windowSize = pkt->getWindowSize();
    buffer[pktIdx].status = PacketStatus::Acked;

    trySlideWindow();
}

void TransportTx::trySlideWindow() {
    auto leadingAckedCount = 0;
    for (auto pkt : buffer) {
        if (pkt.status != PacketStatus::Acked) {
            break;
        }
        leadingAckedCount += 1;
    }

    for (auto i = 0; i < leadingAckedCount; ++i) {
        buffer.pop_front();
    }

    windowStart += leadingAckedCount;
}

void TransportTx::handleTimeoutMessage(TimeoutMsg* msg) {
    auto seqNumber = msg->getSeqNumber();
    if (seqNumber < windowStart) {
        return;
    }

    auto pktIdx = seqNumber - windowStart;
    if (pktIdx >= windowSize || pktIdx >= buffer.size()) {
        std::cerr << "pktIdx out of bounds (timeout message)" << std::endl;
        return;
    }

    auto packet = &buffer[pktIdx];
    if (packet->status != PacketStatus::Acked) {
        packet->status = PacketStatus::Ready;
    }

    delete msg;
}

#endif /* TRANSPORT_TX */
