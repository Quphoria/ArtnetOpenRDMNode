
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
            }
            devices++;
        }

        dp = dp->next;
    }

    ftdi_list_free(&devlist);
    return devices;
}