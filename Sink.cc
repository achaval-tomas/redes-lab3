#ifndef SINK
#define SINK

#include <omnetpp.h>
#include <string.h>

using namespace omnetpp;

class Sink : public cSimpleModule {
private:
    cStdDev delayStats;
    cOutVector delayVector;
    cOutVector packetsReceivedVector;
    unsigned int packetsReceived = 0;

public:
    Sink();
    ~Sink() override;

protected:
    void initialize() override;
    void finish() override;
    void handleMessage(cMessage* msg) override;
};

Define_Module(Sink);

Sink::Sink() {
}

Sink::~Sink() {
}

void Sink::initialize() {
    // stats and vector names
    delayStats.setName("TotalDelay");
    delayVector.setName("Delay");
    packetsReceivedVector.setName("ReceivedPackets");
    packetsReceived = 0;
}

void Sink::finish() {
    // stats record at the end of simulation
    recordScalar("AvgDelay", delayStats.getMean());
    recordScalar("NumberOfPackets", delayStats.getCount());
}

void Sink::handleMessage(cMessage* msg) {
    // compute queuing delay
    simtime_t delay = simTime() - msg->getCreationTime();
    // update stats
    delayStats.collect(delay);
    delayVector.record(delay);
    packetsReceivedVector.record(++packetsReceived);
    // delete msg
    delete msg;
}

#endif /* SINK */
