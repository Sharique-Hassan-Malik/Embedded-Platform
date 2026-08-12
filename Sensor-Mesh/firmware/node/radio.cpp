#include "radio.h"
#include "config.h"
#include <RF24.h>
#include <SPI.h>

static RF24 rf24(RF_CE_PIN, RF_CSN_PIN);

// Address template: base 0xF0F0F0F0XX — last byte is the node ID.
static uint8_t nodeAddr(uint8_t id) {
    // Returns just the variable last byte; upper 4 bytes are the same for all.
    return id;
}

// Full 5-byte pipe address for a given node.
static void fillAddr(uint8_t addr_buf[5], uint8_t id) {
    addr_buf[0] = id;
    addr_buf[1] = 0xF0;
    addr_buf[2] = 0xF0;
    addr_buf[3] = 0xF0;
    addr_buf[4] = 0xF0;
}

namespace Radio {

bool begin(uint8_t node_id) {
    if (!rf24.begin()) return false;

    rf24.setChannel(RF_CHANNEL);
    rf24.setPALevel(RF_PA_LEVEL);
    rf24.setDataRate(RF_DATA_RATE);
    rf24.setPayloadSize(sizeof(Packet));  // fixed 32-byte payload
    rf24.setAutoAck(false);              // routing layer handles retries
    rf24.setCRCLength(RF24_CRC_16);

    // Pipe 0: broadcast (shared address 0xF0F0F0F0FF, all nodes listen).
    uint8_t bcast_addr[5];
    fillAddr(bcast_addr, BROADCAST_ID);
    rf24.openReadingPipe(0, bcast_addr);

    // Pipe 1: unicast for this node.
    uint8_t my_addr[5];
    fillAddr(my_addr, node_id);
    rf24.openReadingPipe(1, my_addr);

    rf24.startListening();
    return true;
}

bool send(uint8_t dest_node, const Packet &pkt) {
    uint8_t dest_addr[5];
    fillAddr(dest_addr, dest_node);

    rf24.stopListening();
    rf24.openWritingPipe(dest_addr);
    bool ok = rf24.write(&pkt, sizeof(Packet));
    rf24.startListening();
    return ok;
}

bool broadcast(const Packet &pkt) {
    uint8_t bcast_addr[5];
    fillAddr(bcast_addr, BROADCAST_ID);

    rf24.stopListening();
    rf24.openWritingPipe(bcast_addr);
    bool ok = rf24.write(&pkt, sizeof(Packet));
    rf24.startListening();
    return ok;
}

bool receive(Packet *out) {
    if (!rf24.available()) return false;
    rf24.read(out, sizeof(Packet));
    return true;
}

} // namespace Radio
