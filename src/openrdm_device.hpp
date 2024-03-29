
#ifndef __OPENRDM_DEVICE_HPP__
#define __OPENRDM_DEVICE_HPP__

#define RDM_RETRY_DELAY_MS 20

#include <string>
#include <vector>

#include "openrdm.h"
#include "rdm.hpp"

typedef std::vector<UID> UIDList;

class OpenRDMDevice {
    public:
        bool verbose, rdm_enabled, rdm_debug;
        OpenRDMDevice();
        OpenRDMDevice(std::string ftdi_description, bool verbose, bool rdm_enabled, bool rdm_debug);
        bool init();
        void deinit();
        bool isInitialized();
        std::string getDescription();
        static void findDevices(bool verbose);
        void writeDMX(uint8_t *data, int len);
        std::pair<int, RDMData> writeRDM(uint8_t *data, int len);
        UIDList fullRDMDiscovery(); // Returns full TOD
        std::pair<UIDList, UIDList> incrementalRDMDiscovery(); // Returns pair: added devices, removed devices
    protected:
        UIDList discover(UID start, UID end);
        UIDList getProxyTOD(UID addr);
        bool hasProxyTODChanged(UID addr);
        bool sendMute(UID addr, bool unmute, bool &is_proxy);
        std::vector<RDMPacket> sendRDMPacket(RDMPacket pkt, unsigned int retries = 5, double max_time_ms = 2000);
    private:
        bool initialized = false;
        bool discovery_in_progress;
        struct ftdi_context ftdi;
        std::string ftdi_description;
        UID uid;
        uint8_t rdm_transaction_number = 0;
        UIDList tod, lost, proxies;
        std::unique_ptr<std::mutex> dev_mutex;
};

#endif // __OPENRDM_DEVICE_HPP__