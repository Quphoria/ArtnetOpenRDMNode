
#ifndef __RDM_H__
#define __RDM_H__

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

#endif // __RDM_H__