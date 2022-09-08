
#ifndef __RDM_HPP__
#define __RDM_HPP__

#include <cstdint>
#include <string>
#include <array>

// RDM Constants
#define RDM_SUB_START_CODE 0x01
#define RDM_UID_LENGTH 6
#define RDM_CC_DISCOVER         0x10
#define RDM_CC_DISCOVER_RESP    0x11
#define RDM_CC_GET_COMMAND      0x20
#define RDM_CC_GET_COMMAND_RESP 0x21
#define RDM_CC_SET_COMMAND      0x30
#define RDM_CC_SET_COMMAND_RESP 0x31
#define RDM_RESP_ACK        0x00
#define RDM_RESP_ACK_TIMER  0x01
#define RDM_RESP_NACK       0x02
#define RDM_RESP_ACK_OVERFL 0x03
#define RDM_PID_DISC_UNIQUE_BRANCH  0x0001
#define RDM_PID_DISC_MUTE           0x0002
#define RDM_PID_DISC_UNMUTE         0x0003
#define RDM_PID_QUEUED_MESSAGE      0x0020
#define RDM_PID_PROXIED_DEVICES     0x0010
#define RDM_PID_PROXY_DEV_COUNT     0x0011

typedef uint64_t UID;

#define RDM_UID_BROADCAST 0xFFFFFFFFFFFF
#define RDM_UID_MAX 0xFFFFFFFFFFFE
#define RDM_UID_LENGTH 6
#define RDM_UID_MFR 0x7A70 // Open Lighting ETSA Code

#define RDM_MAX_PDL 231U

typedef std::array<uint8_t, 512> RDMData;
typedef std::array<uint8_t, RDM_MAX_PDL> RDMPacketData;

class RDMPacket {
    public:
        RDMPacket(UID dest, UID src, uint8_t tn, uint8_t port_id, uint8_t message_count, uint16_t sub_device,
            uint8_t cc, uint16_t pid, uint8_t pdl, const RDMPacketData &pdata);
        RDMPacket(UID uid, const RDMData &data, size_t length);
        size_t writePacket(RDMData &data);
        bool isValid();
    private:
        bool valid = false;
        UID dest;
        UID src;
        uint8_t transaction_number;
        uint8_t port_id_resp_type;
        uint8_t message_count;
        uint16_t sub_device;
        uint8_t cc;
        uint16_t pid;
        uint8_t pdl;
        RDMPacketData pdata;
};

class DiscoveryResponseRDMPacket {
    public:
        DiscoveryResponseRDMPacket(const RDMData &data, size_t length);
        bool isValid();
        UID getUID();
    private:
        bool valid = false;
        UID uid;
};

UID getUID(const uint8_t *data);
void writeUID(uint8_t *data, UID uid);
UID generateUID(std::string s);

#endif // __RDM_HPP__