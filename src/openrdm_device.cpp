
#include <algorithm>

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

void OpenRDMDevice::findDevices(bool verbose) {
    findOpenRDMDevices(verbose);
}

void OpenRDMDevice::writeDMX(uint8_t *data, int len) {
    if (!initialized) return;
    writeDMXOpenRDM(verbose, &ftdi, data, len);
}

UIDList OpenRDMDevice::fullRDMDiscovery() {
    if (discovery_in_progress || !rdm_enabled) return UIDList();

    discovery_in_progress = true;
    lost = UIDList();
    proxies = UIDList();

    bool NA = false;
    sendMute(RDM_UID_BROADCAST, true, NA); // Unmute everything
    tod = discover(0, RDM_UID_MAX);

    discovery_in_progress = false;
    return tod;
}

std::pair<UIDList, UIDList> OpenRDMDevice::incrementalRDMDiscovery() {
    if (discovery_in_progress || !rdm_enabled) return std::make_pair(UIDList(), UIDList());
    discovery_in_progress = true;
    auto found = UIDList();
    auto new_lost = UIDList();
    auto new_proxies = UIDList();
    // Check tod devices are still there and lost devices are still lost
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
        size_t resp_len = writeRDMOpenRDM(verbose, &ftdi,
            disc_msg_packet.begin(), msg_len, true, response.begin());
        
        if (resp_len == 0) return UIDList(); // Nothing
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

    RDMPacket resp = sendRDMPacket(proxy_tod_msg);
    if (!resp.isValid()) return UIDList();

    // TODO: Handle the response

    return UIDList();
}

bool OpenRDMDevice::hasProxyTODChanged(UID addr) {
    auto proxy_tod_changed_msg = RDMPacket(addr, uid, rdm_transaction_number++, 0x1, 0, 0,
        RDM_CC_GET_COMMAND, RDM_PID_PROXY_DEV_COUNT, 0, RDMPacketData());

    RDMPacket resp = sendRDMPacket(proxy_tod_changed_msg);
    if (!resp.isValid()) return false;

    // TODO: Handle the response

    return false;
}

bool OpenRDMDevice::sendMute(UID addr, bool unmute, bool &is_proxy) {
    auto mute_msg = RDMPacket(addr, uid, rdm_transaction_number++, 0x1, 0, 0,
        RDM_CC_DISCOVER, unmute ? RDM_PID_DISC_UNMUTE : RDM_PID_DISC_MUTE,
        0, RDMPacketData());

    RDMPacket resp = sendRDMPacket(mute_msg);
    if (!resp.isValid()) return false;

    // TODO: Handle the response

    return false;
}

RDMPacket OpenRDMDevice::sendRDMPacket(RDMPacket pkt, unsigned int retries, unsigned int max_time_ms) {
    auto msg = RDMData();
    size_t msg_len = pkt.writePacket(msg);
    
    // TODO: Send the message and handle responses/retries
    // See ANSI E1.20: 6.3 Response Type Field Values
    // As certain command classes only support certain response types
    (void)msg_len;

    return RDMPacket(); // Return invalid response packet
}