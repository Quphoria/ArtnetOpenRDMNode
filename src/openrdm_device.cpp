
#include "openrdm_device.hpp"

OpenRDMDevice::OpenRDMDevice() {
    this->ftdi_description = "";
    this->verbose = 0;
}

OpenRDMDevice::OpenRDMDevice(std::string ftdi_description, bool verbose) {
    this->ftdi_description = ftdi_description;
    this->verbose = verbose;
}

bool OpenRDMDevice::init() {
    if (initOpenRDM(verbose, &ftdi, ftdi_description.c_str())) {
        uid = generateUID(ftdi_description);
        initialized = true;
        return true;
    }
    deinit();
    return false;
}

void OpenRDMDevice::deinit() {
    if (!initialized) return;
    deinitOpenRDM(verbose, &ftdi);
    initialized = false;
}

void OpenRDMDevice::findDevices(bool verbose) {
    findOpenRDMDevices(verbose);
}

void OpenRDMDevice::writeDMX(uint8_t *data, int len) {
    if (!initialized) return;
    writeDMXOpenRDM(verbose, &ftdi, data, len);
}