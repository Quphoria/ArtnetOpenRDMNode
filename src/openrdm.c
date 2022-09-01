
#include <stdio.h>

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
    ftdi_set_line_property2(ftdi, BITS_8, STOP_BIT_2, NONE, BREAK_ON);
}

void FT_SetBreakOff(struct ftdi_context *ftdi) {
    ftdi_set_line_property2(ftdi, BITS_8, STOP_BIT_2, NONE, BREAK_OFF);
}

int initOpenRDM(int verbose, struct ftdi_context *ftdi, const char* description) {
    if (verbose) printf("Initialising OpenRDM Device...\n");

    int ret = ftdi_init(ftdi);
    if (ret != 0) {
        fprintf(stderr, "FTDI ERROR %d: %s\n", ret, ftdi->error_str);
        return 0;
    }

    ret = ftdi_usb_open_string(ftdi, description);
    if (ret != 0) {
        fprintf(stderr, "FTDI ERROR %d: %s\n", ret, ftdi->error_str);
        return 0;
    }
    ftdi_usb_reset(ftdi);
    ftdi_set_baudrate(ftdi, BAUDRATE);
    ftdi_set_line_property(ftdi, BITS_8, STOP_BIT_2, NONE);
    ftdi_setflowctrl(ftdi, SIO_DISABLE_FLOW_CTRL);
    ftdi_usb_purge_rx_buffer(ftdi);
    ftdi_usb_purge_tx_buffer(ftdi);
    FT_SetTimeouts(ftdi, 50, 50);

    if (verbose) printf("Initialised OpenRDM Device: %s\n", description);
    return 1;
}

void deinitOpenRDM(int verbose, struct ftdi_context *ftdi) {
    // Check we have usb device handle before we try to close it
    if (ftdi->usb_dev) ftdi_usb_close(ftdi);
    ftdi_deinit(ftdi);
}

void writeRDM(int verbose, struct ftdi_context *ftdi, unsigned char *data, int size, unsigned char *rx_data, int *rx_length) {
    ftdi_usb_purge_rx_buffer(ftdi);
    ftdi_usb_purge_tx_buffer(ftdi);
    FT_SetBreakOn(ftdi);
    FT_SetBreakOff(ftdi);
    int ret = ftdi_write_data(ftdi, data, size);
    if (ret != 0) {
        fprintf(stderr, "RDM TX ERROR %d: %s\n", ret, ftdi->error_str);
        return;
    }
    unsigned char i;
    ftdi_read_data(ftdi, &i, 1);
    *rx_length = ftdi_read_data(ftdi, rx_data, 512);
}