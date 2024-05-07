#ifndef TRANSPORT_RX
#define TRANSPORT_RX

#include "DataPkt_m.h"
#include "FeedbackPkt_m.h"
#include <algorithm>
#include <deque>
#include <omnetpp.h>
#include <string.h>

using namespace omnetpp;

class TransportRx : public cSimpleModule {
private:
    std::deque<DataPkt*> buffer;
    cMessage* endServiceEvent = NULL;
    simtime_t serviceTime;
    unsigned int windowStart = 0;
    unsigned int windowSize = 0; // aka buffer size

public:
    TransportRx();
    ~TransportRx() override;

protected:
    void initialize() override;
    void finish() override;
    void handleMessage(cMessage* msg) override;

private:
    void handleEndServiceMessage();
    void handleDataPacket(DataPkt* pkt);
    void handleFeedbackPacket(FeedbackPkt* pkt);
    void trySlideWindow();
};
Define_Module(TransportRx);

TransportRx::TransportRx() {
}

TransportRx::~TransportRx() {
    cancelAndDelete(endServiceEvent);
}

void TransportRx::initialize() {
    endServiceEvent = new cMessage("endService");
    scheduleAt(simTime(), endServiceEvent);
    windowSize = par("bufferSize");
    buffer.resize(windowSize, nullptr);
}

void TransportRx::finish() {
}

void TransportRx::handleMessage(cMessage* msg) {
    if (msg == endServiceEvent) {
        handleEndServiceMessage();
    } else if (dynamic_cast<DataPkt*>(msg)) {
        handleDataPacket(dynamic_cast<DataPkt*>(msg));
    } else {
        EV_ERROR << "[TRX] error TransportRx: unknown packet" << std::endl;
    }
}

void TransportRx::handleEndServiceMessage() {
    auto pkt = buffer.front();
    if (pkt == nullptr) {
        return;
    }

    EV_TRACE << "[TRX] sending packet " << pkt->getSeqNumber()
             << " to sink and moving receive window one to the right" << std::endl;

    send(pkt, "toOut$o");
    buffer.pop_front();
    buffer.push_back(nullptr);
    windowStart++;

    serviceTime = pkt->getDuration();
    scheduleAt(simTime() + serviceTime, endServiceEvent);
}

// From TransportTx
void TransportRx::handleDataPacket(DataPkt* pkt) {
    auto seqNumber = pkt->getSeqNumber();

    EV_TRACE << "[TRX] received data packet " << seqNumber << std::endl;

    if (seqNumber >= windowSize + windowStart) {
        delete pkt;
        this->bubble("packet dropped");
        EV_INFO << "[TRX] packet dropped" << std::endl;
        return;
    }

    if (windowStart <= seqNumber) {
        auto pktIndex = seqNumber - windowStart;
        buffer[pktIndex] = pkt;
        EV_TRACE << "[TRX] storing data packet " << seqNumber << std::endl;
    }

    auto feedback = new FeedbackPkt();
    feedback->setAckNumber(seqNumber);
    feedback->setWindowSize(windowSize);

    auto name = "ack=" + std::to_string(seqNumber);
    feedback->setName(name.c_str());

    send(feedback, "toApp");
    EV_TRACE << "[TRX] sending feedback packet for " << seqNumber << std::endl;

    if (!endServiceEvent->isScheduled()) {
        scheduleAt(simTime(), endServiceEvent);
    }
}

#endif /* TRANSPORT_RX */