#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include <stdint.h>
#include <artnet/artnet.h>

#include "rdm.h"
#include "dmx.h"
#include "openrdm.h"

#define UID_WIDTH 6

#define UID_COUNT 5
int verbose = 0 ;
int rdm_enabled = 0;
struct ftdi_context openrdm1, openrdm2, openrdm3, openrdm4;

uint8_t *generate_rdm_tod(int count, int iteration) {
    uint8_t *ptr = malloc(count * UID_WIDTH) ;
    int i ;
    
    if(ptr == NULL) {
        printf("malloc failed\n") ;
        exit(1) ;
    }

    memset(ptr, 0x00, UID_WIDTH * count ) ;
    for(i = 0 ; i < count; i++) {
        ptr[i * UID_WIDTH +5] = i ;
        ptr[i * UID_WIDTH +4] = 0;//iteration ;
    }

    return ptr;

}

int rdm_handler(artnet_node n, int address, uint8_t *rdm, int length, void *d) {

    printf("got rdm data for address %d, of length %d\n", address, length) ;

    if (length >= 25) {
        if (rdm[0] == RDM_SUB_START_CODE) {
            printf("rdm %02x%02x%02x%02x%02x%02x cc: 0x%02x, pid: 0x%02x%02x, pdl: 0x%02x\n",
                rdm[2], rdm[3], rdm[4], rdm[5], rdm[6], rdm[7],
                rdm[19], rdm[20], rdm[21], rdm[22]);
        }
    }

    return 0;
}

int rdm_initiate(artnet_node n, int port, void *d) {
    uint8_t *tod ;
    int *count = (int*) d ;
    uint8_t uid[UID_WIDTH] ;
    
    memset(uid, 0x00, UID_WIDTH) ;
        
    tod = generate_rdm_tod(UID_COUNT, *count) ;
    artnet_add_rdm_devices(n, 0, tod, UID_COUNT) ;
    free(tod) ;

    uid[5] = 0xFF ;
    uid[4] = *count ;
    artnet_add_rdm_device(n,0, uid) ;

    uid[5] = 0x03 ;
    artnet_remove_rdm_device(n,0, uid) ;
    
    uid[5] = 0x06 ;
    artnet_remove_rdm_device(n,0, uid) ;

    (*count)++ ;

    return 0;
}

/*
 * Called when we have dmx data pending
 */
int dmx_handler(artnet_node n, int port, void *d) {
  uint8_t *data ;
  int len ;

  if(port == 0) {
    data = artnet_read_dmx(n, port, &len) ;
    writeDMX(verbose, &openrdm1, data, len);
    // pthread_mutex_lock(&mem_mutex) ;
    // memcpy(&ops->dmx[port][1], data, len) ;
    // pthread_mutex_unlock(&mem_mutex) ;
  }
  return 0;
}

/*
 * called when to node configuration changes,
 * we need to save the configuration to a file
 */
int program_handler(artnet_node n, void *d) {
  artnet_node_config_t config ;

  artnet_get_config(n, &config) ;
  
  printf("Program: %s, %s, Subnet: %d, PortAddr: %d\n",
    config.short_name, config.long_name, config.subnet, config.out_ports[0]);

//   ops->short_name = strdup(config.short_name) ;
//   ops->long_name = strdup(config.long_name) ;
//   ops->subnet_addr = config.subnet ;
//   ops->port_addr = config.out_ports[0] ;

  return 0 ;
}

void openrdm_deinit_all() {
    deinitOpenRDM(verbose, &openrdm1);
    deinitOpenRDM(verbose, &openrdm2);
    deinitOpenRDM(verbose, &openrdm3);
    deinitOpenRDM(verbose, &openrdm4);
}

int main(int argc, char *argv[]) {
    artnet_node node ;
    char *ip_addr = NULL ;
    int optc ;
    uint8_t *tod ;
    int tod_refreshes = 0 ;
    char *desc1 = NULL;
    char *desc2 = NULL;
    char *desc3 = NULL;
    char *desc4 = NULL;

    
    // parse options 
    while ((optc = getopt (argc, argv, "vra:d:")) != EOF) {
        switch  (optc) {
             case 'a':
                ip_addr = (char *) strdup(optarg) ;
                break;
            case 'v':
                verbose = 1 ;
                break; 
            case 'r': // Enable RDM
                rdm_enabled = 1;
                printf("RDM Enabled\n");
                break;
            case 'd':
                if (desc1 == NULL) desc1 = (char *) strdup(optarg);
                else if (desc2 == NULL) desc2 = (char *) strdup(optarg);
                else if (desc3 == NULL) desc3 = (char *) strdup(optarg);
                else if (desc4 == NULL) desc4 = (char *) strdup(optarg);
                break;
              default:
                break;
        }
    }

    if ((desc1 && desc2 && strcmp(desc1, desc2)==0) ||
        (desc1 && desc3 && strcmp(desc1, desc3)==0) ||
        (desc1 && desc4 && strcmp(desc1, desc4)==0) ||
        (desc2 && desc3 && strcmp(desc2, desc3)==0) ||
        (desc2 && desc4 && strcmp(desc2, desc4)==0) ||
        (desc3 && desc4 && strcmp(desc3, desc4)==0)) {
            printf("Device string argument repeated, please ensure all values after -d are unique\n");
        return 0;
    }

    int device_connected = 0;

    if (desc1) device_connected |= initOpenRDM(verbose, &openrdm1, desc1);
    if (desc2) device_connected |= initOpenRDM(verbose, &openrdm2, desc2);
    if (desc3) device_connected |= initOpenRDM(verbose, &openrdm3, desc3);
    if (desc4) device_connected |= initOpenRDM(verbose, &openrdm4, desc4);

    if (!device_connected) {
        printf("No OpenRDM Devices found, please specify FTDI device strings for each device using -d\n");
        // Example device string: s:0x0403:0x6001:00418TL8
        findOpenRDMDevices(verbose);
        openrdm_deinit_all();
        return 0;
    }

    node = artnet_new(ip_addr, verbose) ; ;

    artnet_set_short_name(node, "artnet-rdm") ;
    artnet_set_long_name(node, "ArtNet RDM Test, Output Node") ;
    artnet_set_node_type(node, ARTNET_NODE) ;

    // set the first port to output dmx data
    artnet_set_port_type(node, 0, ARTNET_ENABLE_OUTPUT, ARTNET_PORT_DMX) ;
    artnet_set_subnet_addr(node, 0x00) ;

    // we want to be notified when the node config changes
    artnet_set_program_handler(node, program_handler, NULL) ;
    artnet_set_dmx_handler(node, dmx_handler, NULL) ;

    // set the universe address of the first port
    artnet_set_port_addr(node, 0, ARTNET_OUTPUT_PORT, 0x00) ;

    // set poll reply handler
//	artnet_set_handler(node, ARTNET_REPLY_HANDLER, reply_handler, NULL)
    if (rdm_enabled) {
        artnet_set_rdm_initiate_handler(node, rdm_initiate , &tod_refreshes ) ;
        artnet_set_rdm_handler(node, rdm_handler, NULL ) ;

        tod = generate_rdm_tod(UID_COUNT, tod_refreshes++) ;
        artnet_add_rdm_devices(node, 0, tod, UID_COUNT) ;
    
        artnet_start(node) ;

        artnet_send_tod_control(node, 0x10, ARTNET_TOD_FLUSH) ;
        artnet_send_rdm(node, 0x00 , tod, UID_COUNT* UID_WIDTH) ;
        free(tod) ;
    } else {
        artnet_start(node) ;
    }
    
    // loop until control C
    while(1) {
        artnet_read(node, 1) ;
    }
    // never reached
    artnet_destroy(node) ;

    openrdm_deinit_all();

    return 0 ;	
}