
#ifndef __OPENRDM_DEVICE_HPP__
#define __OPENRDM_DEVICE_HPP__

#include <string>

#include "openrdm.h"

class OpenRDMDevice {
    public:
        bool verbose;
        OpenRDMDevice();
        OpenRDMDevice(std::string ftdi_description, bool verbose);
        bool init();
        void deinit();
        static void findDevices(bool verbose);
        void writeDMX(uint8_t *data, int len);
    private:
        bool initialized = false;
        struct ftdi_context ftdi;
        std::string ftdi_description;
};

#endif // __OPENRDM_DEVICE_HPP__