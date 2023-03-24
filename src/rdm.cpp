
#include <functional>
#include <algorithm>

#include "rdm.hpp"
#include "dmx.h"

UID getUID(const uint8_t *data) {
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

RDMPacket::RDMPacket() {}

RDMPacket::RDMPacket(UID dest, UID src, uint8_t tn, uint8_t port_id, uint8_t message_count, uint16_t sub_device,
        uint8_t cc, uint16_t pid, uint8_t pdl, const RDMPacketData &pdata) {
    this->dest = dest;
    this->src = src;
    this->transaction_number = tn;
    this->port_id_resp_type = port_id;
    this->message_count = message_count;
    this->sub_device = sub_device;
    this->cc = cc;
    this->pid = pid;
    this->pdl = pdl;
    this->pdata = RDMPacketData();
    if (pdl > 0)
        std::copy_n(pdata.begin(), std::min(RDM_MAX_PDL, (unsigned int)pdl), this->pdata.begin());
    this->valid = true;
}

RDMPacket::RDMPacket(UID uid, const RDMData &data, size_t length) { // The first byte of data is Start Code
    if (length < 26) return; // Invalid packet length
    if (data[0] != RDM_START_CODE) return; // Incorrect sub start code
    if (data[1] != RDM_SUB_START_CODE) return; // Incorrect sub start code
    if (data[2] > length-2) return; // Incorrect length field
    length = data[2] + 2; // Trim extra data as if the checksum is ok the message is probably ok
    if (getUID(&data[3]) != uid) return; // Message isn't for us
    uint16_t checksum = 0;
    for (size_t i = 0; i < length-2; i++) {
        checksum += data[i];
    }
    if (((checksum >> 8) & 0xff) != data[length-2] ||
        (checksum & 0xff) != data[length-1]) return; // Invalid checksum
    this->dest = getUID(&data[4]);
    this->src = getUID(&data[9]);
    this->transaction_number = data[15];
    this->port_id_resp_type = data[16];
    this->message_count = data[17];
    this->sub_device = ((uint16_t)data[18] << 8) | data[19];
    this->cc = data[20];
    this->pid = ((uint16_t)data[21] << 8) | data[22];
    this->pdl = data[23];
    if (this->pdl > 0)
        std::copy_n(&data[24], std::min(RDM_MAX_PDL, (unsigned int)pdl), this->pdata.begin());
    this->valid = true;
}

RDMPacket::RDMPacket(const uint8_t *data, size_t length) { // The first byte of data is Start Code
    if (length < 26) return; // Invalid packet length
    if (data[0] != RDM_START_CODE) return; // Incorrect sub start code
    if (data[1] != RDM_SUB_START_CODE) return; // Incorrect sub start code
    if (data[2] > length-2) return; // Incorrect length field
    length = data[2] + 2; // Trim extra data as if the checksum is ok the message is probably ok
    uint16_t checksum = 0;
    for (size_t i = 0; i < length-2; i++) {
        checksum += data[i];
    }
    if (((checksum >> 8) & 0xff) != data[length-2] ||
        (checksum & 0xff) != data[length-1]) return; // Invalid checksum
    this->dest = getUID(&data[4]);
    this->src = getUID(&data[9]);
    this->transaction_number = data[15];
    this->port_id_resp_type = data[16];
    this->message_count = data[17];
    this->sub_device = ((uint16_t)data[18] << 8) | data[19];
    this->cc = data[20];
    this->pid = ((uint16_t)data[21] << 8) | data[22];
    this->pdl = data[23];
    if (this->pdl > 0)
        std::copy_n(&data[24], std::min(RDM_MAX_PDL, (unsigned int)pdl), this->pdata.begin());
    this->valid = true;
}

size_t RDMPacket::writePacket(RDMData &data) {
    unsigned int length = 25 + std::min(RDM_MAX_PDL, (unsigned int)pdl);
    data[0] = RDM_SUB_START_CODE;
    data[1] = length;
    writeUID(&data[2], dest);
    writeUID(&data[8], src);
    data[14] = transaction_number;
    data[15] = port_id_resp_type;
    data[16] = message_count;
    data[17] = sub_device >> 8;
    data[18] = sub_device & 0xff;
    data[19] = cc;
    data[20] = pid >> 8;
    data[21] = pid & 0xff;
    data[22] = pdl;
    if (pdl > 0)
        std::copy_n(pdata.begin(), std::min(RDM_MAX_PDL, (unsigned int)pdl), &data[23]);
    uint16_t checksum = RDM_START_CODE;
    for (size_t i = 0; i < length-1; i++) {
        checksum += data[i];
    }
    // We don't have start code so -1
    data[length-1] = checksum >> 8;
    data[length] = checksum & 0xff;
    return length + 1; // Include checksum but subtract start code
}

bool RDMPacket::isValid() { return valid; }
uint8_t RDMPacket::getRespType() { return port_id_resp_type; }
UID RDMPacket::getSrc() { return src; }
UID RDMPacket::getDest() { return dest; }
UID RDMPacket::hasRx() { return dest != (UID)RDM_UID_BROADCAST; }

DiscoveryResponseRDMPacket::DiscoveryResponseRDMPacket(const RDMData &data, size_t length) {
    if (length < 17) return;
    size_t i = 0;
    if (data[i] == RDM_START_CODE) i++;
    for (size_t j = 0; j < 7; j++) {
        if (data[i] != 0xFE) break;
        i++;
    }
    if (length-i < 17) return;
    if (data[i] != 0xAA) return;
    uid = 0;
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