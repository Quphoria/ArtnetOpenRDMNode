
#ifndef __OPENRDM_DEVICE_HPP__
#define __OPENRDM_DEVICE_HPP__

#include <string>
#include <vector>

#include "openrdm.h"
#include "rdm.hpp"

class OpenRDMDevice {
    public:
        bool verbose, rdm_enabled;
        OpenRDMDevice();
        OpenRDMDevice(std::string ftdi_description, bool verbose, bool rdm_enabled);
        bool init();
        void deinit();
        static void findDevices(bool verbose);
        void writeDMX(uint8_t *data, int len);
        std::vector<UID> fullRDMDiscovery(); // Returns full TOD
        std::pair<std::vector<UID>, std::vector<UID>> incrementalRDMDiscovery(); // Returns pair: added devices, removed devices
    protected:
        std::vector<UID> discover(UID start, UID end);
        std::vector<UID> getProxyTOD(UID addr);
        bool sendMute(UID addr, bool unmute, bool &is_proxy);
    private:
        bool initialized = false;
        bool discovery_in_progress;
        struct ftdi_context ftdi;
        std::string ftdi_description;
        UID uid;
        uint8_t rdm_transaction_number = 0;
        std::vector<UID> tod;
        std::vector<UID> lost;
};

#endif // __OPENRDM_DEVICE_HPP__