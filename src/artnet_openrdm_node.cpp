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
#include <memory>

#include <artnet/artnet.h>
#include <argparse/argparse.hpp>

#include "rdm.hpp"
#include "dmx.h"
#include "openrdm_device.hpp"
#include "openrdm_device_thread.hpp"

#define SEMA_MAX 0xffff
#define DMX_REFRESH_MS 50
#define RDM_SEMA_TIMEOUT_MS 1000
#define RDM_INCREMENTAL_SCAN_INTERVAL_MS 5*60*1000 // 5 minutes
static const unsigned int THREAD_REINIT_TIMEOUT_MS = 1000; // 1 second

bool verbose = 0;
bool rdm_enabled = 0;
int num_ports = 0;
bool incremental_scan = false;
artnet_node node;
auto ordm_dev = std::array<OpenRDMDevice, ARTNET_MAX_PORTS>();

bool thread_exit = false;
auto dmx_thread_sema = std::array<std::shared_ptr<std::counting_semaphore<SEMA_MAX>>, ARTNET_MAX_PORTS>();
auto rdm_thread_sema = std::array<std::shared_ptr<std::counting_semaphore<SEMA_MAX>>, ARTNET_MAX_PORTS>();
auto dmx_mutex = std::array<std::mutex, ARTNET_MAX_PORTS>(); // Use a seperate mutex for dmx so we don't lock the dmx unnecessarily
auto data_mutex = std::array<std::mutex, ARTNET_MAX_PORTS>();
auto data_dmx = std::array<DMXMessage, ARTNET_MAX_PORTS>();
auto data_rdm = std::array<std::queue<RDMMessage>, ARTNET_MAX_PORTS>();



void dmx_thread(int port) {
    auto *dev = &ordm_dev[port];
    auto sema = dmx_thread_sema[port];
    if (!dev->isInitialized()) return;
    
    int length;
    uint8_t data[DMX_MAX_LENGTH];
    bool dmx_changed = false;
    auto t_last = std::chrono::high_resolution_clock::now();

    while (!thread_exit) {
        bool sema_acquired = sema->try_acquire_for(std::chrono::milliseconds(DMX_REFRESH_MS));
        if (!dev->isInitialized()) {
            std::cerr << "OPENRDM DMX Thread: Port " << std::to_string(port+1)
                << " (" << dev->getDescription() << ") not Initialized";
            if (thread_exit) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(THREAD_REINIT_TIMEOUT_MS));
            if (thread_exit) break;
            dev->init();
            continue;
        }
        
        if (sema_acquired) {
            dmx_mutex[port].lock();
            dmx_changed = data_dmx[port].changed;
            if (dmx_changed) {
                length = data_dmx[port].length;
                std::copy_n(data_dmx[port].data.data(), length, data);
                data_dmx[port].changed = false;
            }
            dmx_mutex[port].unlock();

            if (dmx_changed) {
                dev->writeDMX(data, length);
                t_last = std::chrono::high_resolution_clock::now();
            }
        }
        
        auto t_now = std::chrono::high_resolution_clock::now();
        double elapsed_time_ms = std::chrono::duration<double, std::milli>(t_now-t_last).count();
        if (!sema_acquired || elapsed_time_ms > DMX_REFRESH_MS) {
            // Timed out, DMX refresh
            dmx_mutex[port].lock();
            length = data_dmx[port].length;
            std::copy_n(data_dmx[port].data.data(), length, data);
            dmx_mutex[port].unlock();

            dev->writeDMX(data, length);
            t_last = std::chrono::high_resolution_clock::now();
        }
    }
}

void rdm_thread(int port) {
    auto *dev = &ordm_dev[port];
    auto sema = rdm_thread_sema[port];
    if (!dev->isInitialized()) return;
    
    auto i_scan_last = std::chrono::high_resolution_clock::now();

    while (!thread_exit) {
        bool sema_acquired = sema->try_acquire_for(std::chrono::milliseconds(RDM_SEMA_TIMEOUT_MS));
        if (!dev->isInitialized()) {
            std::cerr << "OPENRDM RDM Thread: Port " << std::to_string(port+1)
                << " (" << dev->getDescription() << ") not Initialized";
            if (thread_exit) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(THREAD_REINIT_TIMEOUT_MS));
            continue;
        }
        if (sema_acquired) {
            data_mutex[port].lock();
            // Handle RDM messages 1 message at a time so we don't halt the dmx too much
            if (!data_rdm[port].empty()) {
                auto msg = data_rdm[port].front();
                data_rdm[port].pop();

                auto actual_len = msg.length;
                // Check SUB START CODE (in case new RDM version has different packet structure)
                if (msg.length > 2 && msg.data[0] == RDM_SUB_START_CODE) {
                    actual_len = std::min(actual_len, 1+msg.data[1]);
                }

                if (msg.length > 0) {
                    auto resp = ordm_dev[port].writeRDM(msg.data.data(), actual_len);
                    if (resp.first > 1) {
                        if (resp.second[0] == RDM_START_CODE) {
                            // Trim off START Code (0xCC)
                            artnet_send_rdm(node, msg.address, resp.second.begin()+1, resp.first-1);
                        }
                    }
                } else { // 0 length means full RDM Discovery
                    if (ordm_dev[port].rdm_enabled)
                        std::cout << "Starting Full RDM Discovery on Port: " << port << std::endl;
                    auto tod = ordm_dev[port].fullRDMDiscovery();
                    if (tod.size() > 0) {
                        auto uids = std::vector<uint8_t>();
                        auto num_uids = 0;
                        for (auto &uid : tod) {
                            auto uid_raw = std::array<uint8_t, RDM_UID_LENGTH>();
                            writeUID(uid_raw.data(), uid);
                            uids.insert(uids.end(), uid_raw.begin(), uid_raw.end());
                            num_uids++;
                        }
                        if (num_uids > 0) artnet_add_rdm_devices(node, port, uids.data(), num_uids);
                    }
                    i_scan_last = std::chrono::high_resolution_clock::now();
                }
            }
            data_mutex[port].unlock();
        }
        
        auto t_now = std::chrono::high_resolution_clock::now();

        if (incremental_scan) {
            auto elapsed_time_ms = std::chrono::duration<double, std::milli>(t_now-i_scan_last).count();
            if (elapsed_time_ms > RDM_INCREMENTAL_SCAN_INTERVAL_MS) {
                if (ordm_dev[port].rdm_enabled)
                    std::cout << "Starting Incremental RDM Discovery on Port: " << port << std::endl;
                auto tod_changes = ordm_dev[port].incrementalRDMDiscovery();
                auto added = tod_changes.first;
                if (added.size() > 0) {
                    auto uids = std::vector<uint8_t>();
                    auto num_uids = 0;
                    for (auto &uid : added) {
                        auto uid_raw = std::array<uint8_t, RDM_UID_LENGTH>();
                        writeUID(uid_raw.data(), uid);
                        uids.insert(uids.end(), uid_raw.begin(), uid_raw.end());
                        num_uids++;
                    }
                    if (num_uids > 0) artnet_add_rdm_devices(node, port, uids.data(), num_uids);
                }
                auto removed = tod_changes.second;
                for (auto &uid : removed) {
                    auto uid_raw = std::array<uint8_t, RDM_UID_LENGTH>();
                    writeUID(uid_raw.data(), uid);
                    artnet_remove_rdm_device(node, port, uid_raw.data());
                }
                i_scan_last = std::chrono::high_resolution_clock::now();
            }
        }
    }
}


int rdm_handler(artnet_node n, int address, uint8_t *rdm, int length, void *d) {
    if (length == 0) return 0;
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

        rdm_thread_sema[port]->release();
    }

    return 0;
}

int rdm_initiate(artnet_node n, int port, void *d) {
    if (port >= num_ports) return 0;

    RDMMessage msg; // Length 0 means full RDM Discovery
    msg.length = 0;

    data_mutex[port].lock();
    data_rdm[port].push(msg);
    data_mutex[port].unlock();

    rdm_thread_sema[port]->release();
    
    return 0;
}

int dmx_handler(artnet_node n, int port, void *d) { 
    if (port >= num_ports) return 0;

    int len;
    uint8_t *data = artnet_read_dmx(n, port, &len);

    data_mutex[port].lock();
    std::copy_n(data, len, data_dmx[port].data.begin());
    data_dmx[port].length = len;
    data_dmx[port].changed = true;
    data_mutex[port].unlock();

    dmx_thread_sema[port]->release();

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
    program.add_argument("-i", "--incremental-scan")
        .help("Enable RDM Incremental Scanning")
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
    incremental_scan = program.get<bool>("--incremental-scan");
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
    if (verbose) std::cout << "Initialising OpenRDM Devices..." << std::endl;
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

    for (int i = 0; i < num_ports; i++) {
        dmx_thread_sema[i] = std::make_shared<std::counting_semaphore<SEMA_MAX>>(0);
        rdm_thread_sema[i] = std::make_shared<std::counting_semaphore<SEMA_MAX>>(0);
    }
       

    auto ordm_dmx_threads = std::vector<std::thread>();
    auto ordm_rdm_threads = std::vector<std::thread>();
    for (int i = 0; i < num_ports; i++) {
        ordm_dmx_threads.push_back(std::thread(dmx_thread, i));
        ordm_rdm_threads.push_back(std::thread(rdm_thread, i));
    }
    
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

    thread_exit = true;
    for (auto &ordm_thread : ordm_rdm_threads) ordm_thread.join();
    for (auto &ordm_thread : ordm_dmx_threads) ordm_thread.join();

    // Deinit openrdm devices
    for (size_t i = 0; i < ARTNET_MAX_PORTS; i++) {
        ordm_dev[i].deinit();
    }

    return 0;	
}