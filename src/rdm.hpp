
#ifndef __RDM_HPP__
#define __RDM_HPP__

#include <cstdint>
#include <string>

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
#define RDM_DISC_UNIQUE_BRANCH_SLOTS 25

typedef uint64_t UID;

#define RDM_UID_BROADCAST 0xFFFFFFFFFFFF
#define RDM_UID_LENGTH 6
#define RDM_UID_MFR 0x7A70 // Open Lighting ETSA Code

#pragma pack(push)                       //push current alignment to stack
#pragma pack(1)                          //set alignment to 1 byte boundary
struct RDM_Packet_Header	{
    unsigned char SSC;
    unsigned char length;
    unsigned char dest[6];
    unsigned char src[6];
    unsigned char transaction_number;
    unsigned char port_id_resp_type;    //Port ID / response type
    unsigned char message_count;
    short unsigned int SubDevice;       //sub device number (root = 0)
    unsigned char cc;
    short unsigned int pid;
    unsigned char pdl;
};
#pragma pack(pop)                        //restore original alignment from stack

class RDMPacket {
    public:
        RDMPacket(UID dest, UID src, uint8_t tn, uint8_t port_id, uint8_t message_count, uint16_t sub_device,
            uint8_t cc, uint16_t pid, uint8_t pdl, uint8_t *pdata);
        RDMPacket(UID uid, uint8_t *data, size_t length);
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
        uint8_t pdata[231];
};

class DiscoveryResponseRDMPacket {
    public:
        DiscoveryResponseRDMPacket(uint8_t *data, size_t length);
        bool isValid();
        UID getUID();
    private:
        bool valid = false;
        UID uid;
};

UID getUID(uint8_t *data);
void writeUID(uint8_t *data, UID uid);
UID generateUID(std::string s);

#endif // __RDM_HPP__