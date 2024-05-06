#ifndef TRANSPORT_TX
#define TRANSPORT_TX

#include <omnetpp.h>
#include <string.h>

enum PacketKind {
    Data,
    Feedback
};

class FeedbackPkt : omnetpp::cPacket {
    
};

using namespace omnetpp;

class TransportTx : public cSimpleModule {
private:
    cQueue buffer;
    cMessage* endServiceEvent = NULL;
    simtime_t serviceTime;

public:
    TransportTx();
    ~TransportTx() override;

protected:
    void initialize() override;
    void finish() override;
    void handleMessage(cMessage* msg) override;
};
Define_Module(TransportTx);

TransportTx::TransportTx() {
}

TransportTx::~TransportTx() {
    cancelAndDelete(endServiceEvent);
}

void TransportTx::initialize() {
    buffer.setName("buffer");
    endServiceEvent = new cMessage("endService");
}

void TransportTx::finish() {
}

void TransportTx::handleMessage(cMessage* msg) {
    // if msg is signaling an endServiceEvent
    if (msg == endServiceEvent) {
        // if packet in buffer, send next one
        if (!buffer.isEmpty()) {
            // dequeue packet
            cPacket* pkt = dynamic_cast<cPacket*>(buffer.pop());
            // send packet
            send(pkt, "toOut$o");
            // start new service
            serviceTime = pkt->getDuration();
            scheduleAt(simTime() + serviceTime, endServiceEvent);
        }
    } else if (msg->getKind() == PacketKind::Data) {
        if (buffer.getLength() >= par("bufferSize").intValue()) {
            delete msg; // drop msg
            this->bubble("packet dropped");
        } else { // if msg is a data packet
            // enqueue the packet
            buffer.insert(msg);
            // if the server is idle
            if (!endServiceEvent->isScheduled()) {
                // start the service
                scheduleAt(simTime(), endServiceEvent);
            }
        }
    } else if (msg->getKind() == PacketKind::Feedback) {
        FeedbackPkt* pkt = dynamic_cast<FeedbackPkt*>(msg);

    }
}

/* Nuestro plan de control de flujo (REPETICION SELECTIVA):
 *  wStart = r
 *  wSize = N
 * 
 */

#endif /* TRANSPORT_TX */
