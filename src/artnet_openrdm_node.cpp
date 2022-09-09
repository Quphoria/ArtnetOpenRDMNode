#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <iostream>
#include <string>
#include <cstring>

#include <artnet/artnet.h>
#include <argparse/argparse.hpp>

#include "rdm.hpp"
#include "dmx.h"
#include "openrdm_device.hpp"

bool verbose = 0 ;
bool rdm_enabled = 0;
OpenRDMDevice ordm_dev[ARTNET_MAX_PORTS] = {OpenRDMDevice(),OpenRDMDevice(),OpenRDMDevice(),OpenRDMDevice()};

int rdm_handler(artnet_node n, int address, uint8_t *rdm, int length, void *d) {
    if (verbose)
        printf("got rdm data for address %d, of length %d\n", address, length);

    int port;
    for (port = 0; port < ARTNET_MAX_PORTS; port++)
        if (artnet_get_universe_addr(n, port, ARTNET_OUTPUT_PORT) == address)
            break;
    if (port == ARTNET_MAX_PORTS) return 0;

    if(port == 0) {
        auto resp = ordm_dev[port].writeRDM(rdm, length);
        if (resp.first > 0) {
            artnet_send_rdm(n, address, resp.second.begin(), resp.first);
        }
    }

    return 0;
}

int rdm_initiate(artnet_node n, int port, void *d) {

    if(port == 0) {
        if (ordm_dev[port].rdm_enabled)
            std::cout << "Starting Full RDM Discovery on Port: " << port << std::endl;
        auto tod = ordm_dev[port].fullRDMDiscovery();
        auto uids = std::vector<uint8_t>();
        auto num_uids = 0;
        for (auto &uid : tod) {
            auto uid_raw = std::array<uint8_t, RDM_UID_LENGTH>();
            writeUID(uid_raw.data(), uid);
            uids.insert(uids.end(), uid_raw.begin(), uid_raw.end());
            num_uids++;
        }
        artnet_add_rdm_devices(n, port, uids.data(), num_uids) ;
    }
    
    return 0;
}

/*
 * Called when we have dmx data pending
 */
int dmx_handler(artnet_node n, int port, void *d) {
    uint8_t *data;
    int len;

    if(port == 0) {
        data = artnet_read_dmx(n, port, &len) ;
        ordm_dev[port].writeDMX(data, len);
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
  
    if (verbose)
        printf("Program: %s, %s, Subnet: %d, PortAddr: %d\n",
            config.short_name, config.long_name, config.subnet, config.out_ports[0]);

    return 0 ;
}

void openrdm_deinit_all() {
    for (int i = 0; i < ARTNET_MAX_PORTS; i++){
        ordm_dev[i].deinit();
    }
}

int main(int argc, char *argv[]) {
    argparse::ArgumentParser program("Artnet OpenRDM Node", "1.0.0");
    program.add_argument("-V", "--verbose")
        .help("Show debugging information")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-r", "--rdm")
        .help("Enable RDM")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("-a", "--address")
        .default_value(std::string(""))
        .help("Set the address to listen on");
    program.add_argument("-d", "--devices")
        .help("List of up to 4 OpenRDM FTDI device strings to connect to (empty string to skip node ports), omit this argument to list all OpenRDM devices")
        .nargs(1,ARTNET_MAX_PORTS);
    
    try {
        program.parse_args(argc, argv);                  // Example: ./main -abc 1.95 2.47
    } catch (const std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        std::exit(1);
    }

    verbose = program.get<bool>("--verbose");
    rdm_enabled = program.get<bool>("--rdm");

    auto dev_strings = program.get<std::vector<std::string>>("--devices");
    for (size_t i = 0; i < ARTNET_MAX_PORTS && i < dev_strings.size(); i++) {
        // Skip 0 length device strings
        if (dev_strings.at(i).size() == 0) continue;
        for (size_t j = 1; j < ARTNET_MAX_PORTS && j < dev_strings.size(); j++) {
            if (dev_strings.at(i) != dev_strings.at(j)) continue;
            std::cerr << "Device string argument repeated, please ensure all values for -d/--devices are unique" << std::endl;
            std::exit(1);
        }
    }

    bool device_connected = false;

    // Initialize openrdm devices
    for (size_t i = 0; i < ARTNET_MAX_PORTS && i < dev_strings.size(); i++) {
        // Skip 0 length device strings
        if (dev_strings.at(i).size() == 0) continue;
        ordm_dev[i] = OpenRDMDevice(dev_strings.at(i), verbose, rdm_enabled);
        device_connected |= ordm_dev[i].init();
    }

    if (!device_connected) {
        std::cerr << "No OpenRDM Devices found, please specify FTDI device strings for each device using -d" << std::endl;
        // Example device string: s:0x0403:0x6001:00418TL8
        OpenRDMDevice::findDevices(1);
        return 0;
    }

    if (rdm_enabled && verbose) {
        std::cout << "RDM Enabled" << std::endl;
    }

    
    char *ip_addr = NULL;
    auto ip_addr_string = program.get<std::string>("--address");
    if (ip_addr_string.size() > 0)
        ip_addr = (char*)ip_addr_string.c_str();

    artnet_node node = artnet_new(ip_addr, verbose);

    artnet_set_short_name(node, "OpenRDM-Node");
    artnet_set_long_name(node, "ArtNet OpenRDM Node");
    artnet_set_node_type(node, ARTNET_NODE);

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
        artnet_set_rdm_initiate_handler(node, rdm_initiate , NULL ) ;
        artnet_set_rdm_handler(node, rdm_handler, NULL ) ;;
    }
    artnet_start(node) ;
    
    // loop until control C
    while(1) {
        artnet_read(node, 1) ;
    }
    // never reached
    artnet_destroy(node) ;

    openrdm_deinit_all();

    return 0 ;	
}