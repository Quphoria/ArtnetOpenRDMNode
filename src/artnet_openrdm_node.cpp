#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <iostream>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>
#include <semaphore>
#include <array>
#include <queue>
#include <chrono>

#include <artnet/artnet.h>
#include <argparse/argparse.hpp>

#include "rdm.hpp"
#include "dmx.h"
#include "openrdm_device.hpp"
#include "openrdm_device_thread.hpp"

bool verbose = 0;
bool rdm_enabled = 0;
int num_ports = 0;
artnet_node node;
auto ordm_dev = std::array<OpenRDMDevice, ARTNET_MAX_PORTS>();

auto thread_exit = std::array<bool, ARTNET_MAX_PORTS>();
auto thread_sema = std::array<std::counting_semaphore, ARTNET_MAX_PORTS>();
auto dmx_mutex = std::array<std::mutex, ARTNET_MAX_PORTS>(); // Use a seperate mutex for dmx so we don't lock the dmx unnecessarily
auto data_mutex = std::array<std::mutex, ARTNET_MAX_PORTS>();
auto data_dmx = std::array<DMXMessage, ARTNET_MAX_PORTS>();
auto data_rdm = std::array<std::queue<RDMMessage>, ARTNET_MAX_PORTS>();

void device_thread(int port) {
    auto *dev = &ordm_dev[port];
    if (!dev->isInitialized()) return;
    
    while (!thread_exit[port]) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    dev->deinit();
}

int rdm_handler(artnet_node n, int address, uint8_t *rdm, int length, void *d) {
    if (length == 0) return;
    if (verbose)
        printf("got rdm data for address %d, of length %d\n", address, length);

    // Just in case multiple ports have the same address, do it like this
    for (int port = 0; port < std::min(num_ports, (int)ARTNET_MAX_PORTS); port++) {
        if (artnet_get_universe_addr(n, port, ARTNET_OUTPUT_PORT) != address) continue;

        if (!ordm_dev[port].rdm_enabled) continue;

        RDMMessage msg;
        msg.address = address;
        msg.length = length;
        std::copy_n(rdm, length, msg.data.begin());

        data_mutex[port].lock();
        data_rdm[port].push(msg);
        data_mutex[port].unlock();

        thread_sema[port].release();

        // auto resp = ordm_dev[port].writeRDM(rdm, length);
        // if (resp.first > 0) {
        //     artnet_send_rdm(n, address, resp.second.begin(), resp.first);
        // }
    }

    return 0;
}

int rdm_initiate(artnet_node n, int port, void *d) {
    if (port >= num_ports) return 0;

    if (ordm_dev[port].rdm_enabled)
        std::cout << "Starting Full RDM Discovery on Port: " << port << std::endl;

    auto tod = ordm_dev[port].fullRDMDiscovery();
    if (tod.size() == 0) return 0;
    auto uids = std::vector<uint8_t>();
    auto num_uids = 0;
    for (auto &uid : tod) {
        auto uid_raw = std::array<uint8_t, RDM_UID_LENGTH>();
        writeUID(uid_raw.data(), uid);
        uids.insert(uids.end(), uid_raw.begin(), uid_raw.end());
        num_uids++;
    }
    if (num_uids > 0) artnet_add_rdm_devices(n, port, uids.data(), num_uids);
    
    return 0;
}

int dmx_handler(artnet_node n, int port, void *d) { 
    if (port >= num_ports) return 0;

    int len;
    uint8_t *data = artnet_read_dmx(n, port, &len);
    // ordm_dev[port].writeDMX(data, len);

    data_mutex[port].lock();
    std::copy_n(data, len, data_dmx[port].data.begin());
    data_dmx[port].changed = true;
    data_mutex[port].unlock();

    thread_sema[port].release();

    return 0;
}

/*
 * called when to node configuration changes,
 * we need to save the configuration to a file
 */
int program_handler(artnet_node n, void *d) {
    artnet_node_config_t config;
    artnet_get_config(n, &config);
  
    if (verbose)
        printf("Program: %s, %s, Subnet: %d, PortAddr: %d\n",
            config.short_name, config.long_name, config.subnet, config.out_ports[0]);

    return 0;
}

int main(int argc, char *argv[]) {
    argparse::ArgumentParser program(PACKAGE_NAME, PACKAGE_VERSION);
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
    program.add_argument("--rdm-debug")
        .help("Output debugging information about RDM commands")
        .default_value(false)
        .implicit_value(true);
    
    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        std::exit(1);
    }

    verbose = program.get<bool>("--verbose");
    rdm_enabled = program.get<bool>("--rdm");
    bool rdm_debug = program.get<bool>("--rdm-debug");

    auto dev_strings = program.get<std::vector<std::string>>("--devices");
    for (size_t i = 0; i < ARTNET_MAX_PORTS && i < dev_strings.size(); i++) {
        // Skip 0 length device strings
        if (dev_strings.at(i).size() == 0) continue;
        for (size_t j = i+1; j < ARTNET_MAX_PORTS && j < dev_strings.size(); j++) {
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
        ordm_dev[i] = OpenRDMDevice(dev_strings.at(i), verbose, rdm_enabled, rdm_debug);
        device_connected |= ordm_dev[i].init();
        num_ports++;
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

    auto ordm_threads = std::vector<std::thread>();
    for (int i = 0; i < num_ports; i++)
        ordm_threads.push_back(std::thread(device_thread, i));
    
    char *ip_addr = NULL;
    auto ip_addr_string = program.get<std::string>("--address");
    if (ip_addr_string.size() > 0)
        ip_addr = (char*)ip_addr_string.c_str();

    node = artnet_new(ip_addr, verbose);

    artnet_set_short_name(node, "OpenRDM-Node");
    artnet_set_long_name(node, PACKAGE_NAME);
    artnet_set_node_type(node, ARTNET_NODE);

    // set the first port to output dmx data
    for (int i = 0; i < num_ports; i++)
        artnet_set_port_type(node, i, ARTNET_ENABLE_OUTPUT, ARTNET_PORT_DMX);
    artnet_set_subnet_addr(node, 0x00);

    // we want to be notified when the node config changes
    artnet_set_program_handler(node, program_handler, NULL);
    artnet_set_dmx_handler(node, dmx_handler, NULL);

    // set the universe address of the first port
    for (int i = 0; i < num_ports; i++)
        artnet_set_port_addr(node, i, ARTNET_OUTPUT_PORT, i);

    // set poll reply handler
    if (rdm_enabled) {
        artnet_set_rdm_initiate_handler(node, rdm_initiate, NULL);
        artnet_set_rdm_handler(node, rdm_handler, NULL);
    }
    artnet_start(node);
    
    // loop until control C
    while(1) {
        artnet_read(node, 1);
    }
    // never reached
    artnet_destroy(node);

    for (size_t i = 0; i < ordm_threads.size(); i++) thread_exit[i] = true;
    for (auto &ordm_thread : ordm_threads) ordm_thread.join();

    return 0;	
}