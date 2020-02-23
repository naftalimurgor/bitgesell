// Copyright (c) 2011-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <mapport.h>

#include <clientversion.h>
#include <logging.h>
#include <net.h>
#include <netaddress.h>
#include <netbase.h>
#include <threadinterrupt.h>
#include <util/system.h>

#ifdef USE_UPNP
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>
// The minimum supported miniUPnPc API version is set to 10. This keeps compatibility
// with Ubuntu 16.04 LTS and Debian 8 libminiupnpc-dev packages.
static_assert(MINIUPNPC_API_VERSION >= 10, "miniUPnPc API version >= 10 assumed");
#endif

#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <string>
#include <thread>

#ifdef USE_UPNP
static CThreadInterrupt g_mapport_interrupt;
static std::thread g_mapport_thread;
static std::atomic_uint g_mapport_enabled_protos{MapPortProtoFlag::NONE};
static std::atomic<MapPortProtoFlag> g_mapport_current_proto{MapPortProtoFlag::NONE};

using namespace std::chrono_literals;
static constexpr auto PORT_MAPPING_REANNOUNCE_PERIOD{20min};
static constexpr auto PORT_MAPPING_RETRY_PERIOD{5min};

static bool ProcessUpnp()
{
    bool ret = false;
    std::string port = strprintf("%u", GetListenPort());
    const char * multicastif = nullptr;
    const char * minissdpdpath = nullptr;
    struct UPNPDev * devlist = nullptr;
    char lanaddr[64];

    int error = 0;
#if MINIUPNPC_API_VERSION < 14
    devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, &error);
#else
    devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, 2, &error);
#endif

    struct UPNPUrls urls;
    struct IGDdatas data;
    int r;

    r = UPNP_GetValidIGD(devlist, &urls, &data, lanaddr, sizeof(lanaddr));
    if (r == 1)
    {
        if (fDiscover) {
            char externalIPAddress[40];
            r = UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, externalIPAddress);
            if (r != UPNPCOMMAND_SUCCESS) {
                LogPrintf("UPnP: GetExternalIPAddress() returned %d\n", r);
            } else {
                if (externalIPAddress[0]) {
                    CNetAddr resolved;
                    if (LookupHost(externalIPAddress, resolved, false)) {
                        LogPrintf("UPnP: ExternalIPAddress = %s\n", resolved.ToString());
                        AddLocal(resolved, LOCAL_MAPPED);
                    }
                } else {
                    LogPrintf("UPnP: GetExternalIPAddress failed.\n");
                }
            }
        }

        std::string strDesc = PACKAGE_NAME " " + FormatFullVersion();

        do {
            r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, port.c_str(), port.c_str(), lanaddr, strDesc.c_str(), "TCP", 0, "0");

            if (r != UPNPCOMMAND_SUCCESS) {
                ret = false;
                LogPrintf("AddPortMapping(%s, %s, %s) failed with code %d (%s)\n", port, port, lanaddr, r, strupnperror(r));
                break;
            } else {
                ret = true;
                LogPrintf("UPnP Port Mapping successful.\n");
            }
        } while (g_mapport_interrupt.sleep_for(PORT_MAPPING_REANNOUNCE_PERIOD));
        g_mapport_interrupt.reset();

        r = UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, port.c_str(), "TCP", 0);
        LogPrintf("UPNP_DeletePortMapping() returned: %d\n", r);
        freeUPNPDevlist(devlist); devlist = nullptr;
        FreeUPNPUrls(&urls);
    } else {
        LogPrintf("No valid UPnP IGDs found\n");
        freeUPNPDevlist(devlist); devlist = nullptr;
        if (r != 0)
            FreeUPNPUrls(&urls);
    }

    return ret;
}

static void ThreadMapPort()
{
    bool ok;
    do {
        ok = false;

#ifdef USE_UPNP
        // High priority protocol.
        if (g_mapport_enabled_protos & MapPortProtoFlag::UPNP) {
            g_mapport_current_proto = MapPortProtoFlag::UPNP;
            ok = ProcessUpnp();
            if (ok) continue;
        }
#endif // USE_UPNP

#ifdef USE_NATPMP
        // Low priority protocol.
        if (g_mapport_enabled_protos & MapPortProtoFlag::NAT_PMP) {
            g_mapport_current_proto = MapPortProtoFlag::NAT_PMP;
            ok = ProcessNatpmp();
            if (ok) continue;
        }
#endif // USE_NATPMP

        g_mapport_current_proto = MapPortProtoFlag::NONE;
        if (g_mapport_enabled_protos == MapPortProtoFlag::NONE) {
            return;
        }

    } while (ok || g_mapport_interrupt.sleep_for(PORT_MAPPING_RETRY_PERIOD));
}

void StartThreadMapPort()
{
    if (!g_mapport_thread.joinable()) {
        assert(!g_mapport_interrupt);
        g_mapport_thread = std::thread((std::bind(&TraceThread<void (*)()>, "mapport", &ThreadMapPort)));
    }
}

static void DispatchMapPort()
{
    if (g_mapport_current_proto == MapPortProtoFlag::NONE && g_mapport_enabled_protos == MapPortProtoFlag::NONE) {
        return;
    }

    if (g_mapport_current_proto == MapPortProtoFlag::NONE && g_mapport_enabled_protos != MapPortProtoFlag::NONE) {
        StartThreadMapPort();
        return;
    }

    if (g_mapport_current_proto != MapPortProtoFlag::NONE && g_mapport_enabled_protos == MapPortProtoFlag::NONE) {
        InterruptMapPort();
        StopMapPort();
        return;
    }

    if (g_mapport_enabled_protos & g_mapport_current_proto) {
        // Enabling another protocol does not cause switching from the currently used one.
        return;
    }

    assert(g_mapport_thread.joinable());
    assert(!g_mapport_interrupt);
    // Interrupt a protocol-specific loop in the ThreadUpnp() or in the ThreadNatpmp()
    // to force trying the next protocol in the ThreadMapPort() loop.
    g_mapport_interrupt();
}

static void MapPortProtoSetEnabled(MapPortProtoFlag proto, bool enabled)
{
    if (enabled) {
        g_mapport_enabled_protos |= proto;
    } else {
        g_mapport_enabled_protos &= ~proto;
    }
}

void StartMapPort(bool use_upnp, bool use_natpmp)
{
    MapPortProtoSetEnabled(MapPortProtoFlag::UPNP, use_upnp);
    MapPortProtoSetEnabled(MapPortProtoFlag::NAT_PMP, use_natpmp);
    DispatchMapPort();
}

void InterruptMapPort()
{
    g_mapport_enabled_protos = MapPortProtoFlag::NONE;
    if (g_mapport_thread.joinable()) {
        g_mapport_interrupt();
    }
}

void StopMapPort()
{
    if (g_mapport_thread.joinable()) {
        g_mapport_thread.join();
        g_mapport_interrupt.reset();
    }
}

#else  // #if defined(USE_NATPMP) || defined(USE_UPNP)
void StartMapPort(bool use_upnp, bool use_natpmp)
{
    // Intentionally left blank.
}
void InterruptMapPort()
{
    // Intentionally left blank.
}
void StopMapPort()
{
    // Intentionally left blank.
}
#endif
