
#ifndef __OPENRDM_DEVICE_THREAD_HPP__
#define __OPENRDM_DEVICE_THREAD_HPP__

#include "rdm.hpp"
#include "dmx.h"

struct RDMMessage {
    int address;
    int length;
    RDMData data;
};

struct DMXMessage {
    bool changed;
    std::array<uint8_t, DMX_MAX_LENGTH> data;
};

#endif // __OPENRDM_DEVICE_THREAD_HPP__