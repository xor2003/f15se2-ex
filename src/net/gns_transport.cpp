/*
 * gns_transport.cpp - GameNetworkingSockets backend for NetTransport.
 *
 * Native-only (plan §13/§15): browsers get a separate WebSocket leg later.
 * Wire messages are length-prefixed payloads; the F15 protocol layer above
 * does its own magic/version typing inside each payload.
 */
#include <stdio.h>
#include <string.h>

#include <deque>
#include <unordered_map>
#include <vector>

#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>

#include "transport.h"

namespace {

static void gnsDebugSpew(ESteamNetworkingSocketsDebugOutputType type,
                         const char *msg) {
    fprintf(stderr, "GNS[%d] %s", (int)type, msg);
}

struct QueuedMsg {
    NetPeer peer;
    std::vector<uint8_t> bytes;
};

class GnsTransport final : public NetTransport {
  public:
    GnsTransport() = default;
    ~GnsTransport() override { shutdown(); }

    bool listen(uint16_t port) override {
        if (!initLib())
            return false;
        SteamNetworkingIPAddr addr;
        SteamNetworkingConfigValue_t opt[3];
        addr.Clear();
        addr.m_port = port;
        /* without the status-changed callback inbound connections are never
         * surfaced for accept; AllowWithoutAuth+Unencrypted = LAN/Internet
         * server with no cert machinery (v1 protocol, plan §13) */
        opt[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                      (void *)statusCallback);
        opt[1].SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 1);
        opt[2].SetInt32(k_ESteamNetworkingConfig_Unencrypted, 2);
        listenSock_ = iface_->CreateListenSocketIP(addr, 3, opt);
        if (listenSock_ == k_HSteamListenSocket_Invalid) {
            fprintf(stderr, "GNS: CreateListenSocketIP failed\n");
            return false;
        }
        pollGroup_ = iface_->CreatePollGroup();
        isServer_ = true;
        return true;
    }

    bool connect(const char *hostPort) override {
        if (!initLib())
            return false;
        SteamNetworkingIPAddr addr;
        addr.Clear();
        if (!addr.ParseString(hostPort)) {
            fprintf(stderr, "GNS: bad address '%s'\n", hostPort);
            return false;
        }
        SteamNetworkingConfigValue_t opt[3];
        opt[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                      (void *)statusCallback);
        opt[1].SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 1);
        opt[2].SetInt32(k_ESteamNetworkingConfig_Unencrypted, 2);
        serverConn_ = iface_->ConnectByIPAddress(addr, 3, opt);
        if (serverConn_ == k_HSteamNetConnection_Invalid) {
            fprintf(stderr, "GNS: ConnectByIPAddress failed\n");
            return false;
        }
        isServer_ = false;
        return true;
    }

    void closePeer(NetPeer peer, int reason) override {
        HSteamNetConnection c = isServer_ ? (HSteamNetConnection)peer : serverConn_;
        if (c != k_HSteamNetConnection_Invalid)
            iface_->CloseConnection(c, reason, "f15 close", false);
    }

    void shutdown() override {
        if (!iface_)
            return;
        if (isServer_) {
            for (auto &kv : conns_)
                iface_->CloseConnection(kv.first, 0, "server shutdown", true);
            if (pollGroup_ != k_HSteamNetPollGroup_Invalid)
                iface_->DestroyPollGroup(pollGroup_);
            if (listenSock_ != k_HSteamListenSocket_Invalid)
                iface_->CloseListenSocket(listenSock_);
        } else if (serverConn_ != k_HSteamNetConnection_Invalid) {
            iface_->CloseConnection(serverConn_, 0, "client shutdown", true);
        }
        conns_.clear();
        iface_ = nullptr;
        GameNetworkingSockets_Kill();
        libUp_ = false;
    }

    void poll() override {
        if (iface_)
            iface_->RunCallbacks();
    }

    bool recv(NetRecv *out) override {
        if (!events_.empty()) {
            *out = events_.front();
            events_.pop_front();
            return true;
        }
        /* drain socket messages */
        if (isServer_) {
            if (pollGroup_ == k_HSteamNetPollGroup_Invalid)
                return false;
            for (;;) {
                SteamNetworkingMessage_t *m = nullptr;
                int n = iface_->ReceiveMessagesOnPollGroup(pollGroup_, &m, 1);
                if (n <= 0 || !m)
                    break;
                NetRecv ev{};
                ev.kind = NET_EV_MESSAGE;
                ev.peer = (NetPeer)m->m_conn;
                msgBuf_.assign((const uint8_t *)m->m_pData,
                               (const uint8_t *)m->m_pData + m->m_cbSize);
                m->Release();
                ev.msg = msgBuf_.data();
                ev.len = msgBuf_.size();
                *out = ev;
                return true;
            }
        } else {
            if (serverConn_ == k_HSteamNetConnection_Invalid)
                return false;
            for (;;) {
                SteamNetworkingMessage_t *m = nullptr;
                int n = iface_->ReceiveMessagesOnConnection(serverConn_, &m, 1);
                if (n <= 0 || !m)
                    break;
                NetRecv ev{};
                ev.kind = NET_EV_MESSAGE;
                ev.peer = NET_PEER_SERVER;
                msgBuf_.assign((const uint8_t *)m->m_pData,
                               (const uint8_t *)m->m_pData + m->m_cbSize);
                m->Release();
                ev.msg = msgBuf_.data();
                ev.len = msgBuf_.size();
                *out = ev;
                return true;
            }
        }
        return false;
    }

    void send(NetPeer peer, const void *data, size_t len, int flags) override {
        if (!iface_)
            return;
        int st = (flags & NET_SEND_RELIABLE) ? k_nSteamNetworkingSend_Reliable
                                             : k_nSteamNetworkingSend_Unreliable;
        if (isServer_) {
            HSteamNetConnection c = (HSteamNetConnection)peer;
            iface_->SendMessageToConnection(c, data, (uint32)len, st, nullptr);
        } else if (serverConn_ != k_HSteamNetConnection_Invalid) {
            iface_->SendMessageToConnection(serverConn_, data, (uint32)len, st, nullptr);
        }
    }

    enum NetPeerState peerState(NetPeer peer) const override {
        if (!iface_)
            return NET_PEER_DISCONNECTED;
        HSteamNetConnection c = isServer_ ? (HSteamNetConnection)peer : serverConn_;
        if (c == k_HSteamNetConnection_Invalid)
            return NET_PEER_DISCONNECTED;
        SteamNetConnectionInfo_t info;
        if (!iface_->GetConnectionInfo(c, &info))
            return NET_PEER_DISCONNECTED;
        switch (info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connected:
            return NET_PEER_CONNECTED;
        case k_ESteamNetworkingConnectionState_Connecting:
        case k_ESteamNetworkingConnectionState_FindingRoute:
            return NET_PEER_CONNECTING;
        default:
            return NET_PEER_DISCONNECTED;
        }
    }

  private:
    bool initLib() {
        if (libUp_)
            return true;
        SteamDatagramErrMsg err;
        if (!GameNetworkingSockets_Init(nullptr, err)) {
            fprintf(stderr, "GNS init failed: %s\n", err);
            return false;
        }
        SteamNetworkingUtils()->SetDebugOutputFunction(
            k_ESteamNetworkingSocketsDebugOutputType_Warning, gnsDebugSpew);
        iface_ = SteamNetworkingSockets();
        libUp_ = true;
        return true;
    }

    static void statusCallback(SteamNetConnectionStatusChangedCallback_t *info) {
        /* GNS delivers on the "global" callback when no per-connection one is
         * set; route through the singleton. */
        if (instance_)
            instance_->onStatus(info);
    }

    void onStatus(SteamNetConnectionStatusChangedCallback_t *info) {
        if (isServer_) {
            switch (info->m_info.m_eState) {
            case k_ESteamNetworkingConnectionState_Connecting:
                /* inbound peer: accept everything (auth happens at HELLO) */
                if (iface_->AcceptConnection(info->m_hConn) != k_EResultOK) {
                    iface_->CloseConnection(info->m_hConn, 0, "accept fail", true);
                    break;
                }
                iface_->SetConnectionPollGroup(info->m_hConn, pollGroup_);
                conns_[info->m_hConn] = true;
                pushEvent(NET_EV_CONNECTED, (NetPeer)info->m_hConn);
                break;
            case k_ESteamNetworkingConnectionState_ClosedByPeer:
            case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
                iface_->CloseConnection(info->m_hConn, 0, "peer gone", true);
                conns_.erase(info->m_hConn);
                pushEvent(NET_EV_DISCONNECTED, (NetPeer)info->m_hConn);
                break;
            default:
                break;
            }
        } else {
            switch (info->m_info.m_eState) {
            case k_ESteamNetworkingConnectionState_Connected:
                pushEvent(NET_EV_CONNECTED, NET_PEER_SERVER);
                break;
            case k_ESteamNetworkingConnectionState_ClosedByPeer:
            case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
                pushEvent(NET_EV_DISCONNECTED, NET_PEER_SERVER);
                break;
            default:
                break;
            }
        }
    }

    void pushEvent(enum NetEventKind kind, NetPeer peer) {
        NetRecv ev{};
        ev.kind = kind;
        ev.peer = peer;
        ev.msg = nullptr;
        ev.len = 0;
        events_.push_back(ev);
    }

    bool libUp_ = false;
    bool isServer_ = false;
    ISteamNetworkingSockets *iface_ = nullptr;
    HSteamListenSocket listenSock_ = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup pollGroup_ = k_HSteamNetPollGroup_Invalid;
    HSteamNetConnection serverConn_ = k_HSteamNetConnection_Invalid;
    std::unordered_map<HSteamNetConnection, bool> conns_;
    std::deque<NetRecv> events_;
    std::vector<uint8_t> msgBuf_;

  public:
    static GnsTransport *create() {
        GnsTransport *t = new GnsTransport();
        instance_ = t;
        return t;
    }

  private:
    static GnsTransport *instance_;
};

GnsTransport *GnsTransport::instance_ = nullptr;

} /* namespace */

NetTransport *createGnsTransport() {
    return GnsTransport::create();
}
