#ifndef QUEUE
#define QUEUE

#include <omnetpp.h>
#include <string.h>

using namespace omnetpp;

class Queue : public cSimpleModule {
private:
    cQueue buffer;
    cMessage* endServiceEvent = NULL;
    simtime_t serviceTime;
    cOutVector bufferSizeVector;
    cOutVector droppedPacketsVector;

public:
    Queue();
    ~Queue() override;

protected:
    void initialize() override;
    void finish() override;
    void handleMessage(cMessage* msg) override;
};

Define_Module(Queue);

Queue::Queue() {
}

Queue::~Queue() {
    cancelAndDelete(endServiceEvent);
}

void Queue::initialize() {
    buffer.setName("buffer");
    endServiceEvent = new cMessage("endService");
    droppedPacketsVector.setName("DroppedPackets");
    bufferSizeVector.setName("BufferSize");
}

void Queue::finish() {
}

void Queue::handleMessage(cMessage* msg) {
    // if msg is signaling an endServiceEvent
    if (msg == endServiceEvent) {
        // if packet in buffer, send next one
        if (!buffer.isEmpty()) {
            // dequeue packet
            cPacket* pkt = dynamic_cast<cPacket*>(buffer.pop());
            // send packet
            send(pkt, "out");
            // start new service
            serviceTime = pkt->getDuration();
            scheduleAt(simTime() + serviceTime, endServiceEvent);
        }
    } else if (buffer.getLength() >= par("bufferSize").intValue()) {
        delete msg; // drop msg
        this->bubble("packet dropped");
        droppedPacketsVector.record(1);
    } else { // if msg is a data packet
        // enqueue the packet
        buffer.insert(msg);
        bufferSizeVector.record(buffer.getLength());
        // if the server is idle
        if (!endServiceEvent->isScheduled()) {
            // start the service
            scheduleAt(simTime(), endServiceEvent);
        }
    }
}

#endif /* QUEUE */
