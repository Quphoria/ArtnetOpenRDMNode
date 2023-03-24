
#include <stdio.h>
#include <string.h>

#include "dmx.h"
#include "openrdm.h"

int findOpenRDMDevices(int verbose) {
    if (verbose) printf("Finding OpenRDM Devices...\n");
    struct ftdi_context ftdi;
    struct ftdi_device_list *devlist;
    
    ftdi_init(&ftdi);
    int ret = ftdi_usb_find_all(&ftdi, &devlist, OPENRDM_VID, OPENRDM_PID);
    ftdi_deinit(&ftdi);
    if(ret < 0) {
        fprintf(stderr, "FTDI ERROR %d: %s\n", ret, ftdi.error_str);
        ftdi_list_free(&devlist);
        return 0;
    };

    int devices = 0;

    struct ftdi_device_list *dp = devlist;
    while (dp){
        char manufacturer[51];
        char description[51];
        char serial[51];

        ftdi_init(&ftdi);

        ret = ftdi_usb_get_strings(&ftdi, dp->dev,
            manufacturer, 50,
            description, 50,
            serial, 50);

        ftdi_deinit(&ftdi);

        if (ret != 0) {
            fprintf(stderr, "FTDI ERROR %d: %s\n", ret, ftdi.error_str);
        } else {
            if (verbose) {
                printf("OpenRDM Device Found:\n");
                printf("MFR: %s\n", manufacturer);
                printf("DESC: %s\n", description);
                printf("SER: %s\n", serial);
                printf("Device String: s:0x%04x:0x%04x:%s\n\n", OPENRDM_VID, OPENRDM_PID, serial);
            }
            devices++;
        }

        dp = dp->next;
    }

    ftdi_list_free(&devlist);
    return devices;
}

void FT_SetTimeouts(struct ftdi_context *ftdi, int read_timeout, int write_timeout) {
    ftdi->usb_read_timeout = read_timeout;
    ftdi->usb_write_timeout = write_timeout;
}

void FT_SetBreakOn(struct ftdi_context *ftdi) {
    int ret = ftdi_set_line_property2(ftdi, BITS_8, STOP_BIT_2, NONE, BREAK_ON);
    if (ret != 0) printf("Break On Failed: %d\n", ret);
}

void FT_SetBreakOff(struct ftdi_context *ftdi) {
    int ret = ftdi_set_line_property2(ftdi, BITS_8, STOP_BIT_2, NONE, BREAK_OFF);
    if (ret != 0) printf("Break Off Failed: %d\n", ret);
}

void resetUsbAndInitOpenRDM(int verbose, struct ftdi_context *ftdi) {
    ftdi_usb_reset(ftdi);
    ftdi_set_baudrate(ftdi, BAUDRATE);
    ftdi_set_line_property(ftdi, BITS_8, STOP_BIT_2, NONE);
    ftdi_setflowctrl(ftdi, SIO_DISABLE_FLOW_CTRL);
    ftdi_usb_purge_rx_buffer(ftdi);
    ftdi_usb_purge_tx_buffer(ftdi);
    FT_SetTimeouts(ftdi, 50, 50);
}

int initOpenRDM(int verbose, struct ftdi_context *ftdi, const char *description) {
    // if (verbose) printf("Initialising OpenRDM Device...\n");

    int ret = ftdi_init(ftdi);
    if (ret != 0) {
        fprintf(stderr, "FTDI ERROR %d: %s\n", ret, ftdi->error_str);
        if (verbose) printf("Failed to initialise OpenRDM Device: %s\n", description);
        return 0;
    }

    ret = ftdi_usb_open_string(ftdi, description);
    if (ret != 0) {
        fprintf(stderr, "FTDI ERROR %d: %s\n", ret, ftdi->error_str);
        if (verbose) printf("Failed to initialise OpenRDM Device: %s\n", description);
        return 0;
    }
    
    resetUsbAndInitOpenRDM(verbose, ftdi);

    if (verbose) printf("Initialised OpenRDM Device: %s\n", description);
    return 1;
}

void deinitOpenRDM(int verbose, struct ftdi_context *ftdi) {
    // Check we have usb device handle before we try to close it
    if (ftdi->usb_dev) ftdi_usb_close(ftdi);
    ftdi_deinit(ftdi);
}

void reinitOpenRDM(int verbose, struct ftdi_context *ftdi, const char *description) {
    if (!ftdi->usb_dev) return;
    ftdi_usb_close(ftdi);
    initOpenRDM(verbose, ftdi, description);
}

int writeRDMOpenRDM(int verbose, struct ftdi_context *ftdi, unsigned char *data, int size, int is_discover, int has_rx, unsigned char *rx_data, const char *description) {
    ftdi_usb_purge_rx_buffer(ftdi);
    ftdi_usb_purge_tx_buffer(ftdi);
    FT_SetBreakOn(ftdi);
    usleep(92); // Wait for break time of 92us
    FT_SetBreakOff(ftdi);
    unsigned char data_sc[513];
    data_sc[0] = RDM_START_CODE;
    memcpy(&data_sc[1], data, size);
    int ret = ftdi_write_data(ftdi, data_sc, size+1);
    if (ret < 0) {
        fprintf(stderr, "RDM TX ERROR %d: %s\n", ret, ftdi->error_str);
        // libusb: -110: usb bulk write failed
        // libusb: -666: USB device unavailable
        if (ret == -110 || ret == -666) 
            reinitOpenRDM(verbose, ftdi, description);
        return ret;
    }
    if (is_discover) {
        return ftdi_read_data(ftdi, rx_data, 513);
    }
    if (!has_rx) return 0;
    unsigned char i;
    ftdi_read_data(ftdi, &i, 1); // Discard Break
    return ftdi_read_data(ftdi, rx_data, 513);
}

int writeDMXOpenRDM(int verbose, struct ftdi_context *ftdi, unsigned char *data, int size, const char *description) {
    ftdi_usb_purge_rx_buffer(ftdi);
    ftdi_usb_purge_tx_buffer(ftdi);
    FT_SetBreakOn(ftdi);
    usleep(92); // Wait for break time of 92us
    FT_SetBreakOff(ftdi);
    unsigned char data_sc[513];
    data_sc[0] = DMX_START_CODE;
    memcpy(&data_sc[1], data, size);
    int ret = ftdi_write_data(ftdi, data_sc, size+1);
    if (ret < 0) {
        fprintf(stderr, "DMX TX ERROR %d: %s\n", ret, ftdi->error_str);
        // libusb: -110: usb bulk write failed
        // libusb: -666: USB device unavailable
        if (ret == -110 || ret == -666) 
            reinitOpenRDM(verbose, ftdi, description);
        return ret;
    }
    return 0;
}