#ifndef GENERATOR
#define GENERATOR

#include <omnetpp.h>
#include <string.h>
#include "DataPkt_m.h"

using namespace omnetpp;

class Generator : public cSimpleModule {
private:
    cMessage* sendMsgEvent = NULL;
    cStdDev transmissionStats;

public:
    Generator();
    ~Generator() override;

protected:
    void initialize() override;
    void finish() override;
    void handleMessage(cMessage* msg) override;
};
Define_Module(Generator);

Generator::Generator() {
}

Generator::~Generator() {
    cancelAndDelete(sendMsgEvent);
}

void Generator::initialize() {
    transmissionStats.setName("TotalTransmissions");
    // create the send packet
    sendMsgEvent = new cMessage("sendEvent");
    // schedule the first event at random time
    scheduleAt(par("generationInterval"), sendMsgEvent);
}

void Generator::finish() {
}

void Generator::handleMessage(cMessage* msg) {

    // create new packet
    DataPkt* pkt = new DataPkt("packet");
    pkt->setByteLength(par("packetByteSize"));
    // send to the output
    send(pkt, "out");
    // compute the new departure time
    simtime_t departureTime = simTime() + par("generationInterval");
    // schedule the new packet generation
    scheduleAt(departureTime, sendMsgEvent);
}

#endif /* GENERATOR */
