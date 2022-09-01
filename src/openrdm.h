
#ifndef __OPENRDM_H__
#define __OPENRDM_H__

#include <ftdi.h>

#define OPENRDM_VID 0x0403
#define OPENRDM_PID 0x6001

// UDEV rule: SUBSYSTEM=="usb", ATTR{idProduct}=="6001", ATTRS{idVendor}=="0403", MODE="0666"

int findOpenRDMDevices(int verbose);
int initOpenRDM(int verbose);

#endif // __OPENRDM_H__