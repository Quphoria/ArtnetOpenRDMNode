
#ifndef __OPENRDM_H__
#define __OPENRDM_H__

#include <ftdi.h>

#define OPENRDM_VID 0x0403
#define OPENRDM_PID 0x6001

#define BAUDRATE 250000 //250kBaud

// UDEV rule: SUBSYSTEM=="usb", ATTR{idProduct}=="6001", ATTRS{idVendor}=="0403", MODE="0666"

int findOpenRDMDevices(int verbose);
int initOpenRDM(int verbose, struct ftdi_context *ftdi, const char* description);
void deinitOpenRDM(int verbose, struct ftdi_context *ftdi);
void writeRDM(int verbose, struct ftdi_context *ftdi, unsigned char *data, int size, unsigned char *rx_data, int *rx_length);

#endif // __OPENRDM_H__