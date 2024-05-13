#ifndef TRANSPORT_TX
#define TRANSPORT_TX

#include "DataPkt_m.h"
#include "FeedbackPkt_m.h"
#include "TimeoutMsg_m.h"
#include <algorithm>
#include <deque>
#include <omnetpp.h>
#include <string.h>

using namespace omnetpp;

constexpr double ALPHA = 7.0 / 8.0;
constexpr double BETA = 3.0 / 4.0;
constexpr double STD_DEV_COEFFICIENT = 4;

enum PacketStatus {
    Ready,
    Sent,
    Acked,
};

enum CongestionStatus {
    SlowStart,
    AdditiveIncrease,
};

struct DataPktWithStatus {
    DataPkt* pkt;
    PacketStatus status;
    simtime_t sendTimestamp;
};

class TransportTx : public cSimpleModule {
private:
    std::deque<DataPktWithStatus> buffer;
    cMessage* endServiceEvent = NULL;
    simtime_t serviceTime;
    unsigned int windowStart = 0;
    unsigned int windowSize = 0;
    unsigned int inFlightPackets = 0;
    CongestionStatus congStatus = CongestionStatus::SlowStart;
    unsigned int ssthresh = UINT32_MAX;
    double cwnd = 1.0;

    double estimatedRtt = 1.0;
    double estimatedRttStdDev = 0.0;
    double timeoutTime = 3;
    
    cOutVector packetsSentVector;
    unsigned int packetsSent = 0;

    cOutVector cwndVector;
    cOutVector ssthreshVector;
    cOutVector timeoutTimeVector;

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
    cwndVector.setName("cwnd");
    ssthreshVector.setName("ssthresh");
    timeoutTimeVector.setName("timeoutTime");
    cwndVector.record(cwnd);
    ssthreshVector.record(ssthresh);
    timeoutTimeVector.record(timeoutTime);
    packetsSentVector.setName("Packets Sent");
    packetsSent = 0;
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

    if (inFlightPackets >= cwnd) {
        EV_TRACE << "[TTX] congestion window is full (" << cwnd << "), skipping" << std::endl;
        return;
    }

    size_t i = 0;
    for (auto pkt : buffer) {
        if (pkt.status == PacketStatus::Ready) {
            break;
        }
        i++;
    }

    if (i == buffer.size()) {
        EV_TRACE << "[TTX] no packet to send" << std::endl;
        return;
    }

    if (i >= windowSize) {
        EV_TRACE << "[TTX] window is full, skipping" << std::endl;
        return;
    }

    auto pktToSend = &buffer[i];

    EV_TRACE << "[TTX] sending packet " << pktToSend->pkt->getSeqNumber() << std::endl;

    auto dupPkt = pktToSend->pkt->dup();

    // Send packet
    packetsSentVector.record(++packetsSent);
    send(dupPkt, "toOut$o");
    pktToSend->status = PacketStatus::Sent;
    inFlightPackets++;

    // Start timeout
    auto timeoutMsg = new TimeoutMsg("timeout");
    timeoutMsg->setSeqNumber(pktToSend->pkt->getSeqNumber());
    scheduleAt(simTime() + timeoutTime, timeoutMsg);

    // Schedule next end service event
    serviceTime = dupPkt->getDuration();
    scheduleAt(simTime() + serviceTime, endServiceEvent);

    pktToSend->sendTimestamp = simTime() + serviceTime;
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

    buffer.push_back(DataPktWithStatus { pkt, PacketStatus::Ready });

    if (!endServiceEvent->isScheduled()) {
        scheduleAt(simTime(), endServiceEvent);
    }
}

// From Receiver
void TransportTx::handleFeedbackPacket(FeedbackPkt* feedbackPkt) {
    auto ackNumber = feedbackPkt->getAckNumber();
    delete feedbackPkt;

    EV_TRACE << "[TTX] received feedback packet for " << ackNumber << std::endl;

    switch (congStatus) {
    case CongestionStatus::SlowStart:
        cwnd += 1.0;
        if (cwnd >= ssthresh) {
            congStatus = CongestionStatus::AdditiveIncrease;
        }
        break;
    case CongestionStatus::AdditiveIncrease:
        cwnd += 1.0 / cwnd;
        break;
    default:
        assert(false);
        break;
    }

    cwndVector.record(cwnd);

    EV_TRACE << "[TTX] cwnd:" << cwnd << std::endl;

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
    auto& pkt = buffer[pktIdx];
    if (pkt.status != PacketStatus::Acked) {
        // If a timeout occurred (*) and the packet was queued for retransmission,
        // but a delayed ack arrived before we actually sent the retransmission,
        // then we have already decremented inFlightPackets in the timeout handler,
        // and therefore we must skip the subsequent decrement statement.
        // In the case we have already retransmitted the packet but the delayed ack
        // arrived, there is no problem because if a later ack arrives, we will ignore
        // it, and thus inFlightPackets will not be decremented again.
        // (*) if a timeout hasn't occurred for this packet, its status will be 'Sent'.
        if (pkt.status == PacketStatus::Sent) {
            inFlightPackets--;
        }

        auto measuredRtt = (simTime() - pkt.sendTimestamp).dbl();
        estimatedRtt = ALPHA * estimatedRtt + (1.0 - ALPHA) * measuredRtt;
        estimatedRttStdDev = BETA * estimatedRttStdDev + (1.0 - BETA) * std::abs(measuredRtt - estimatedRtt);
        timeoutTime = estimatedRtt + STD_DEV_COEFFICIENT * estimatedRttStdDev;
        timeoutTimeVector.record(timeoutTime);

        buffer[pktIdx].status = PacketStatus::Acked;
    }

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
        delete buffer.front().pkt;
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

    assert(pktIdx < windowSize);
    assert(pktIdx < buffer.size());

    auto packet = &buffer[pktIdx];
    if (packet->status == PacketStatus::Acked) {
        // Packet already acked.
        return;
    }

    assert(packet->pkt->getSeqNumber() == seqNumber);

    assert(packet->status != PacketStatus::Ready);

    ssthresh = cwnd / 2;
    cwnd = 1.0;
    congStatus = CongestionStatus::SlowStart;

    ssthreshVector.record(ssthresh);
    cwndVector.record(cwnd);

    EV_TRACE << "[TTX] packet " << seqNumber << " timeout" << std::endl;

    packet->status = PacketStatus::Ready;
    assert(inFlightPackets != 0); // TODO: estar convencidos de que esto nunca pasa.
    inFlightPackets--; // Asumimos que se perdio.

    if (!endServiceEvent->isScheduled()) {
        scheduleAt(simTime(), endServiceEvent);
    }
}

#endif /* TRANSPORT_TX */
