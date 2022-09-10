
#include <algorithm>
#include <thread>
#include <chrono>

#include "openrdm_device.hpp"

OpenRDMDevice::OpenRDMDevice() {
    this->ftdi_description = "";
    this->verbose = 0;
    this->rdm_enabled = false;
}

OpenRDMDevice::OpenRDMDevice(std::string ftdi_description, bool verbose, bool rdm_enabled) {
    this->ftdi_description = ftdi_description;
    this->verbose = verbose;
    this->rdm_enabled = rdm_enabled;
}

bool OpenRDMDevice::init() {
    if (initOpenRDM(verbose, &ftdi, ftdi_description.c_str())) {
        uid = generateUID(ftdi_description);
        discovery_in_progress = false;
        rdm_transaction_number = 0;
        tod = UIDList();
        lost = UIDList();
        proxies = UIDList();
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

bool OpenRDMDevice::isInitialized() { return initialized; }

void OpenRDMDevice::findDevices(bool verbose) {
    findOpenRDMDevices(verbose);
}

void OpenRDMDevice::writeDMX(uint8_t *data, int len) {
    if (!initialized) return;
    int ret = writeDMXOpenRDM(verbose, &ftdi, data, len, ftdi_description.c_str());
    if (ret < 0) { // Error occurred
        // -666: USB device unavailable, wait a bit to avoid spam
        if (ret == -666) std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

std::pair<int, RDMData> OpenRDMDevice::writeRDM(uint8_t *data, int len) {
    if (!initialized) return std::make_pair(0, RDMData());
    auto resp = RDMData();
    int resp_len = writeRDMOpenRDM(verbose, &ftdi, data, len, false, resp.begin(), ftdi_description.c_str());
    if (resp_len < 0) { // Error occurred
        // -666: USB device unavailable, wait a bit to avoid spam
        if (resp_len == -666) std::this_thread::sleep_for(std::chrono::seconds(1));
        return std::make_pair(0, RDMData());
    }
    return std::make_pair(resp_len, resp);
}

UIDList OpenRDMDevice::fullRDMDiscovery() {
    if (!initialized) return UIDList();
    if (discovery_in_progress || !rdm_enabled) return UIDList();

    discovery_in_progress = true;
    lost = UIDList();
    proxies = UIDList();

    bool NA = false;
    sendMute(RDM_UID_BROADCAST, true, NA); // Unmute everything
    tod = discover(0, RDM_UID_MAX);

    if (verbose) {
        for (auto &uid : tod) printf("RDM Device Discovered: %06lx\n", uid);
    }

    discovery_in_progress = false;
    return tod;
}

std::pair<UIDList, UIDList> OpenRDMDevice::incrementalRDMDiscovery() {
    if (!initialized) return std::make_pair(UIDList(), UIDList());
    if (discovery_in_progress || !rdm_enabled) return std::make_pair(UIDList(), UIDList());
    discovery_in_progress = true;
    auto found = UIDList();
    auto new_lost = UIDList();
    auto new_proxies = UIDList();
    bool NA;
    sendMute(RDM_UID_BROADCAST, true, NA); // Unmute everything
    // Check tod devices are still there and lost devices are still lost
    // This also mutes devices we know about
    for (auto &uid : tod) {
        bool is_proxy = false;
        if (!sendMute(uid, false, is_proxy)) {
            new_lost.push_back(uid);
            auto proxies_pos = std::find(proxies.begin(), proxies.end(), uid);
            if (proxies_pos != proxies.end()) {
                proxies.erase(proxies_pos);
            }
        } else  {
            auto proxies_pos = std::find(proxies.begin(), proxies.end(), uid);
            if (proxies_pos != proxies.end()) {
                if (!is_proxy) proxies.erase(proxies_pos);
            } else if (is_proxy) {
                new_proxies.push_back(uid);
                proxies.push_back(uid);
            }
        }
    }
    for (auto &uid : lost) {
        bool is_proxy = false;
        if (sendMute(uid, false, is_proxy)) {
            found.push_back(uid);
            if (is_proxy) {
                auto proxies_pos = std::find(proxies.begin(), proxies.end(), uid);
                if (proxies_pos == proxies.end()) {
                    new_proxies.push_back(uid);
                    proxies.push_back(uid);
                }
            }
        }
    }

    auto discovered = discover(0, RDM_UID_MAX);
    
    for (auto &proxy_uid : proxies) {
        // If proxy is in new_proxies, don't bother checking if its TOD has changed as we want to scan anyway
        if (std::find(new_proxies.begin(), new_proxies.end(), proxy_uid) == new_proxies.end()) {
            if (!hasProxyTODChanged(proxy_uid)) continue;
        }
        auto new_proxy_tod = getProxyTOD(proxy_uid);
        for (auto &uid : new_proxy_tod) {
            // Merge into discovered if unique and new
            if (std::find(discovered.begin(), discovered.end(), uid) == discovered.end()) {
                discovered.push_back(uid);
            }
        }
    }

    for (auto &uid : discovered) {
        // If we find a device that has been found as lost, but it isn't, remove it from lost
        auto new_lost_pos = std::find(new_lost.begin(), new_lost.end(), uid);
        if (new_lost_pos != new_lost.end()) {
            new_lost.erase(new_lost_pos);
        }
        // Merge into found if unique and new
        if (std::find(found.begin(), found.end(), uid) == found.end() && 
                std::find(tod.begin(), tod.end(), uid) == tod.end()) {
            found.push_back(uid);
        }
    }

    // Apply changes to tod and lost
    for (auto &uid : new_lost) {
        auto tod_pos = std::find(tod.begin(), tod.end(), uid);
        if (tod_pos != tod.end()) {
            tod.erase(tod_pos);
        }
        lost.push_back(uid);
    }
    for (auto &uid : found) {
        auto lost_pos = std::find(lost.begin(), lost.end(), uid);
        if (lost_pos != lost.end()) {
            lost.erase(lost_pos);
        }
        tod.push_back(uid);
    }

    if (verbose) {
        for (auto &uid : new_lost) printf("RDM Device Lost: %06lx\n", uid);
        for (auto &uid : found) printf("RDM Device Discovered: %06lx\n", uid);
    }

    discovery_in_progress = false;
    return std::make_pair(found, new_lost);
}

UIDList OpenRDMDevice::discover(UID start, UID end) {
    UID mute_uid = start;
    if (start != end) {
        auto disc_msg_data = RDMPacketData();
        writeUID(disc_msg_data.begin(), start);
        writeUID(disc_msg_data.begin()+RDM_UID_LENGTH, end);
        auto disc_msg = RDMPacket(RDM_UID_BROADCAST, uid, rdm_transaction_number++, 0x1, 0, 0,
            RDM_CC_DISCOVER, RDM_PID_DISC_UNIQUE_BRANCH, RDM_UID_LENGTH*2, disc_msg_data);
        auto disc_msg_packet = RDMData();
        size_t msg_len = disc_msg.writePacket(disc_msg_packet);

        auto response = RDMData();
        int resp_len = writeRDMOpenRDM(verbose, &ftdi,
            disc_msg_packet.begin(), msg_len, true, response.begin(), ftdi_description.c_str());
        if (resp_len <= 0) { // Error occurred or no data
            // -666: USB device unavailable, wait a bit to avoid spam
            if (resp_len == -666) std::this_thread::sleep_for(std::chrono::seconds(1));
            return UIDList();
        }

        auto resp = DiscoveryResponseRDMPacket(response, resp_len);
        if (!resp.isValid()) {
            uint64_t lower_half_size = (end-start+1) / 2; // Start and end inclusive
            UID lower_half_max = start+lower_half_size-1; // Start inclusive
            auto lower_half = discover(start, lower_half_max);
            auto upper_half = discover(lower_half_max+1, end);
            for (auto &uid : upper_half) {
                // Merge unique uid's into lower_half
                if (std::find(lower_half.begin(), lower_half.end(), uid) == lower_half.end()) {
                    lower_half.push_back(uid);
                }
            }
            return lower_half;
        }
        mute_uid = resp.getUID();
    }
    bool is_proxy = false;
    // If we don't get a mute response, there is no device with that uid
    if (!sendMute(mute_uid, false, is_proxy)) return UIDList();
    auto discovered_uids = UIDList();
    discovered_uids.push_back(mute_uid);

    if (!is_proxy) return discovered_uids;

    for (auto &uid : getProxyTOD(mute_uid)) {
        // Merge unique uid's from proxy into discovered_uids
        if (std::find(discovered_uids.begin(), discovered_uids.end(), uid) == discovered_uids.end()) {
            discovered_uids.push_back(uid);
        }
    }

    return discovered_uids;
}

UIDList OpenRDMDevice::getProxyTOD(UID addr) {
    auto proxy_tod_msg = RDMPacket(addr, uid, rdm_transaction_number++, 0x1, 0, 0,
        RDM_CC_GET_COMMAND, RDM_PID_PROXIED_DEVICES, 0, RDMPacketData());

    auto proxy_tod = UIDList();
    
    auto resp = sendRDMPacket(proxy_tod_msg);
    if (resp.size() == 0) return proxy_tod;

    for (auto &r : resp) {
        if (r.pdl > 0xe4) continue;
        for (size_t i = 0; i < r.pdl; i += RDM_UID_LENGTH)
            proxy_tod.push_back(getUID(&r.pdata[i]));
    }

    return proxy_tod;
}

bool OpenRDMDevice::hasProxyTODChanged(UID addr) {
    auto proxy_tod_changed_msg = RDMPacket(addr, uid, rdm_transaction_number++, 0x1, 0, 0,
        RDM_CC_GET_COMMAND, RDM_PID_PROXY_DEV_COUNT, 0, RDMPacketData());

    auto resp = sendRDMPacket(proxy_tod_changed_msg);
    if (resp.size() == 0) return false;
    if (resp[0].pdl != 0x03) return false;
    
    return resp[0].pdata[2] != 0;
}

bool OpenRDMDevice::sendMute(UID addr, bool unmute, bool &is_proxy) {
    auto mute_msg = RDMPacket(addr, uid, rdm_transaction_number++, 0x1, 0, 0,
        RDM_CC_DISCOVER, unmute ? RDM_PID_DISC_UNMUTE : RDM_PID_DISC_MUTE,
        0, RDMPacketData());

    auto resp = sendRDMPacket(mute_msg);
    if (resp.size() == 0) return false;
    if (resp[0].getSrc() != addr) return false;

    if (resp[0].pdl == 0x02 || resp[0].pdl == 0x08) {
        uint16_t control_field = ((uint16_t)resp[0].pdata[0] << 8) | (uint16_t)resp[0].pdata[1];
        is_proxy = (control_field & RDM_CONTROL_MANAGED_PROXY_BITMASK) != 0;
    }

    return true;
}

std::vector<RDMPacket> OpenRDMDevice::sendRDMPacket(RDMPacket pkt, unsigned int retries, double max_time_ms) {
    auto resp_packets = std::vector<RDMPacket>();
    double retry_time_ms = max_time_ms;
    auto msg = RDMData();

    auto t_start = std::chrono::high_resolution_clock::now();
    auto pkt_pid = pkt.pid;

    // Don't count first try as a retry
    for (unsigned int pkt_try = 0; pkt_try <= retries; pkt_try++) {
        if (pkt_try != 0) {
            pkt.transaction_number = rdm_transaction_number++;
        }
        size_t msg_len = pkt.writePacket(msg);
        auto t_now = std::chrono::high_resolution_clock::now();
        double elapsed_time_ms = std::chrono::duration<double, std::milli>(t_now-t_start).count();
        if (pkt_try > 0 && elapsed_time_ms > max_time_ms) break;

        auto response = RDMData();
        int resp_len = writeRDMOpenRDM(verbose, &ftdi,
            msg.begin(), msg_len, false, response.begin(), ftdi_description.c_str());
        if (resp_len < 0) { // Error occurred
            // -666: USB device unavailable, wait a bit to avoid spam
            if (resp_len == -666) std::this_thread::sleep_for(std::chrono::seconds(1));
            return std::vector<RDMPacket>();
        }
        if (resp_len == 0) continue;

        auto resp = RDMPacket(uid, response, resp_len);
        if (!resp.isValid()) continue;
        if (resp.cc != pkt.cc) continue; // Check transaction id's match
        if (resp.pid != pkt_pid) continue; // Check PID is correct (so we ignore stray queued messages)

        if (resp.cc == RDM_CC_DISCOVER_RESP || pkt.cc == RDM_CC_DISCOVER) {
            if (resp.getRespType() == RDM_RESP_ACK) {
                resp_packets.push_back(resp);
                break;
            }
        } else if (resp.cc == RDM_CC_GET_COMMAND_RESP || resp.cc == RDM_CC_SET_COMMAND_RESP) {
            switch (resp.getRespType()) {
                case RDM_RESP_ACK:
                    resp_packets.push_back(resp);
                    return resp_packets;
                case RDM_RESP_ACK_OVERFL:
                    resp_packets.push_back(resp);
                    break;
                case RDM_RESP_ACK_TIMER:
                    if (resp.pdl != 2) continue;
                    retry_time_ms = 100 * (double)(((uint16_t)resp.pdata[0] << 8) | (uint16_t)resp.pdata[1]);
                    pkt.cc = RDM_CC_GET_COMMAND;
                    pkt.pid = RDM_PID_QUEUED_MESSAGE;
                    pkt.pdl = 1;
                    pkt.pdata[0] = RDM_STATUS_ERROR;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1)*std::min(max_time_ms, retry_time_ms));
                    break;
                case RDM_RESP_NACK:
                    break;
            }
        }
    }

    return resp_packets;
}