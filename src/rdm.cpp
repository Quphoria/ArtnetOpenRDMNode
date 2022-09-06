
#include <functional>
#include <cstring>

#include "rdm.hpp"
#include "dmx.h"

UID getUID(uint8_t *data) {
    UID uid = 0;
    for (int i = 0; i < RDM_UID_LENGTH; i++)
        uid |= (UID)data[i] << (8*(RDM_UID_LENGTH-1-i));
    return uid;
}

void writeUID(uint8_t *data, UID uid) {
    for (int i = 0; i < RDM_UID_LENGTH; i++)
        data[i] = (uid >> (8*(RDM_UID_LENGTH-1-i))) & 0xff;
}

UID generateUID(std::string s) {
    UID uid = (UID)RDM_UID_MFR << (4*8); // Set Manufacturer ETSA code
    uint32_t h = std::hash<std::string>{}(s);
    if (h == 0xFFFF) h = 0xFFFE; // Don't allow hash to be manufacturer broadcast UID
    uid |= h;
    return uid; 
}

RDMPacket::RDMPacket(UID dest, UID src, uint8_t tn, uint8_t port_id, uint8_t message_count, uint16_t sub_device,
        uint8_t cc, uint16_t pid, uint8_t pdl, uint8_t *pdata) {
    this->dest = dest;
    this->src = src;
    this->transaction_number = tn;
    this->port_id_resp_type = port_id;
    this->message_count = message_count;
    this->sub_device = sub_device;
    this->cc = cc;
    this->pid = pid;
    this->pdl = pdl;
    if (pdl > 0)
        std::memcpy(this->pdata, pdata, std::min(231, (int)pdl));
    this->valid = true;
}

RDMPacket::RDMPacket(UID uid, uint8_t *data, size_t length) { // The first byte of data is Sub-Start Code
    if (length < 25  || length > 25+231) return; // Invalid packet length
    if (data[0] != RDM_SUB_START_CODE) return; // Incorrect sub start code
    if (data[1] != length-2) return; // Incorrect length field
    if (getUID(&data[2]) != uid) return; // Message isn't for us
    uint16_t checksum = RDM_START_CODE;
    for (size_t i = 0; i < length-2; i++) {
        checksum += data[i];
    }
    if (((checksum >> 8) & 0xff) != data[length-2] ||
        (checksum & 0xff) != data[length-1]) return; // Invalid checksum
    this->dest = getUID(&data[2]);
    this->src = getUID(&data[8]);
    this->transaction_number = data[14];
    this->port_id_resp_type = data[15];
    this->message_count = data[16];
    this->sub_device = ((uint16_t)data[17] << 8) | data[18];
    this->cc = data[19];
    this->pid = ((uint16_t)data[20] << 8) | data[21];
    this->pdl = data[22];
    if (this->pdl > 0)
        std::memcpy(this->pdata, &data[23], std::min(231, (int)this->pdl));
    this->valid = true;
}

bool RDMPacket::isValid() { return valid; }

DiscoveryResponseRDMPacket::DiscoveryResponseRDMPacket(uint8_t *data, size_t length) {
    if (length < 17 || length > 24) return;
    size_t i = 0;
    if (data[i] == RDM_START_CODE) i++;
    for (size_t j = 0; j < 7; j++) {
        if (data[i] != 0xFE) break;
        i++;
    }
    if (length-i != 17) return;
    if (data[i] != 0xAA) return;
    UID uid = 0;
    uid |= (UID)(data[i+1] & data[i+2]) << (5*8);
    uid |= (UID)(data[i+3] & data[i+4]) << (4*8);
    uid |= (UID)(data[i+5] & data[i+6]) << (3*8);
    uid |= (UID)(data[i+7] & data[i+8]) << (2*8);
    uid |= (UID)(data[i+9] & data[i+10]) << (1*8);
    uid |= (UID)(data[i+11] & data[i+12]);
    uint16_t checksum_exp = ((uint16_t)(data[i+13] & data[i+14]) << 8) | (data[i+15] & data[i+16]);
    uint16_t checksum = 0;
    for (size_t j = 0; j < RDM_UID_LENGTH*2; j++) checksum += data[i+1+j];
    if (checksum != checksum_exp) return;
    valid = true;
}

bool DiscoveryResponseRDMPacket::isValid() { return valid; }
UID DiscoveryResponseRDMPacket::getUID() { return uid; }