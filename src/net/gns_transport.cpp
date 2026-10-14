/*
 * gns_transport.cpp - GameNetworkingSockets backend for NetTransport.
 *
 * Native-only (plan §13/§15): browsers get a separate WebSocket leg later.
 * Wire messages are length-prefixed payloads; the F15 protocol layer above
 * does its own magic/version typing inside each payload.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <deque>
#include <unordered_map>
#include <vector>

#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>

#include "transport.h"

namespace {

/* GNS can invoke its debug callback under an internal lock on its service
 * thread. Never do terminal/file IO or wait for our own mutex there. Keep a
 * bounded queue and drain a small batch from poll(), outside GNS calls. */
struct DebugMessage {
    int type;
    char text[1024];
};
static std::mutex debugMutex;
static DebugMessage debugMessages[32];
static unsigned debugHead, debugCount;
static std::atomic<unsigned> debugDropped{0};

static void gnsDebugSpew(ESteamNetworkingSocketsDebugOutputType type,
                         const char *msg) {
    std::unique_lock<std::mutex> lock(debugMutex, std::try_to_lock);
    if (!lock.owns_lock() || debugCount == 32) {
        debugDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    DebugMessage &entry = debugMessages[(debugHead + debugCount) % 32];
    entry.type = (int)type;
    snprintf(entry.text, sizeof(entry.text), "%.*s", (int)sizeof(entry.text) - 1, msg);
    ++debugCount;
}

static void drainDebugMessages(void) {
    for (int i = 0; i < 8; ++i) {
        DebugMessage entry;
        {
            std::unique_lock<std::mutex> lock(debugMutex, std::try_to_lock);
            if (!lock.owns_lock() || !debugCount)
                break;
            entry = debugMessages[debugHead];
            debugHead = (debugHead + 1) % 32;
            --debugCount;
        }
        fprintf(stderr, "GNS[%d] %s\n", entry.type, entry.text);
    }
    const unsigned dropped = debugDropped.exchange(0, std::memory_order_relaxed);
    if (dropped)
        fprintf(stderr, "GNS: dropped %u debug messages (log queue busy/full)\n", dropped);
}

/* Optional wall-time profiling, enabled with F15_GNS_PROF=1. GNS "lock held"
 * warnings measure real time and therefore include any period where the OS
 * preempted a thread while it held the internal lock; these counters time the
 * same entry points from our side so our own work can be separated from
 * external thread starvation. Reported to stderr every PROF_REPORT_NS; maxNs
 * resets each window so it tracks the worst recent call, not history. */
static const uint64_t PROF_REPORT_NS = 5000000000ULL;

struct ProfStats {
    uint64_t calls = 0, bytes = 0, totalNs = 0, maxNs = 0;
};

static bool g_profOn;
static ProfStats g_profSendRel, g_profSendUnrel, g_profPoll, g_profRecv;
static uint64_t g_profWindowStart;

static uint64_t profNowNs(void) {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static void profAdd(ProfStats &s, uint64_t ns, uint64_t bytes) {
    if (!g_profOn)
        return;
    s.calls++;
    s.bytes += bytes;
    s.totalNs += ns;
    if (ns > s.maxNs)
        s.maxNs = ns;
}

static void profReport(void) {
    if (!g_profOn)
        return;
    const uint64_t now = profNowNs();
    if (now - g_profWindowStart < PROF_REPORT_NS)
        return;
    const double secs = (double)(now - g_profWindowStart) / 1e9;
    const ProfStats *rows[] = {&g_profSendRel, &g_profSendUnrel, &g_profPoll,
                               &g_profRecv};
    const char *names[] = {"send-rel", "send-unrel", "poll", "recv"};
    for (int i = 0; i < 4; i++) {
        const ProfStats &s = *rows[i];
        if (!s.calls)
            continue;
        fprintf(stderr,
                "GNSPROF %s: %llu calls (%.0f/s) %llu B (%.0f B/s) avg %.1fus "
                "max %.1fms\n",
                names[i], (unsigned long long)s.calls, s.calls / secs,
                (unsigned long long)s.bytes, s.bytes / secs,
                (double)s.totalNs / (double)s.calls / 1e3, s.maxNs / 1e6);
        const_cast<ProfStats &>(s) = ProfStats{};
    }
    g_profWindowStart = now;
}

struct QueuedMsg {
    NetPeer peer;
    std::vector<uint8_t> bytes;
};

class GnsTransport final : public NetTransport {
  public:
    GnsTransport() = default;
    ~GnsTransport() override { shutdown(); }

    bool listen(const char *bindAddr, uint16_t port) override {
        if (!initLib())
            return false;
        SteamNetworkingIPAddr addr;
        SteamNetworkingConfigValue_t opt[3];
        addr.Clear();
        /* NULL/empty = wildcard (all interfaces); an IP literal binds only
         * that interface so a host can pick which network it serves */
        if (bindAddr && *bindAddr && !addr.ParseString(bindAddr)) {
            fprintf(stderr, "GNS: bad bind address '%s'\n", bindAddr);
            return false;
        }
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
        /* linger=true flushes queued reliable messages (e.g. a HELLO_NAK sent
         * right before the close) before the connection dies */
        if (c != k_HSteamNetConnection_Invalid)
            iface_->CloseConnection(c, reason, "f15 close", true);
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
        if (iface_) {
            const uint64_t t0 = profNowNs();
            iface_->RunCallbacks();
            profAdd(g_profPoll, profNowNs() - t0, 0);
        }
        drainDebugMessages();
        profReport();
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
                const uint64_t t0 = profNowNs();
                int n = iface_->ReceiveMessagesOnPollGroup(pollGroup_, &m, 1);
                profAdd(g_profRecv, profNowNs() - t0, 0);
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
                const uint64_t t0 = profNowNs();
                int n = iface_->ReceiveMessagesOnConnection(serverConn_, &m, 1);
                profAdd(g_profRecv, profNowNs() - t0, 0);
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
        /* Tick-state traffic (snapshots, input axes) is superseded every tick,
         * so it bypasses Nagle and drops rather than queueing when the pipe is
         * backed up (UnreliableNoDelay) - GNS batches Nagle messages and does
         * the flush work while holding its internal lock. Reliable control
         * messages stay ordered but also skip the Nagle timer so commands
         * leave promptly. */
        int st = (flags & NET_SEND_RELIABLE)
                     ? k_nSteamNetworkingSend_ReliableNoNagle
                     : k_nSteamNetworkingSend_UnreliableNoDelay;
        const uint64_t t0 = profNowNs();
        if (isServer_) {
            HSteamNetConnection c = (HSteamNetConnection)peer;
            iface_->SendMessageToConnection(c, data, (uint32)len, st, nullptr);
        } else if (serverConn_ != k_HSteamNetConnection_Invalid) {
            iface_->SendMessageToConnection(serverConn_, data, (uint32)len, st, nullptr);
        }
        profAdd((flags & NET_SEND_RELIABLE) ? g_profSendRel : g_profSendUnrel,
                profNowNs() - t0, len);
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
        g_profOn = getenv("F15_GNS_PROF") && getenv("F15_GNS_PROF")[0] == '1';
        g_profWindowStart = profNowNs();
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

    /* gnsSetFakeNet backend: fake loss/dup/reorder/lag on the client conn */
    void fakeNet(int lossSendPct, int lossRecvPct, int dupSendPct,
                 int reorderMs, int lagMs) {
        ISteamNetworkingUtils *u;
        if (!iface_ || serverConn_ == k_HSteamNetConnection_Invalid)
            return;
        u = SteamNetworkingUtils();
        u->SetConnectionConfigValueInt32(serverConn_,
            k_ESteamNetworkingConfig_FakePacketLoss_Send, lossSendPct);
        u->SetConnectionConfigValueInt32(serverConn_,
            k_ESteamNetworkingConfig_FakePacketLoss_Recv, lossRecvPct);
        u->SetConnectionConfigValueInt32(serverConn_,
            k_ESteamNetworkingConfig_FakePacketDup_Send, dupSendPct);
        u->SetConnectionConfigValueInt32(serverConn_,
            k_ESteamNetworkingConfig_FakePacketDup_Recv, dupSendPct);
        u->SetConnectionConfigValueInt32(serverConn_,
            k_ESteamNetworkingConfig_FakePacketReorder_Send,
            reorderMs > 0 ? 100 : 0);
        u->SetConnectionConfigValueInt32(serverConn_,
            k_ESteamNetworkingConfig_FakePacketReorder_Time, reorderMs);
        u->SetConnectionConfigValueInt32(serverConn_,
            k_ESteamNetworkingConfig_FakePacketLag_Send, lagMs);
    }

  private:
    static GnsTransport *instance_;
};

GnsTransport *GnsTransport::instance_ = nullptr;

} /* namespace */

NetTransport *createGnsTransport() {
    return GnsTransport::create();
}

void gnsSetFakeNet(NetTransport *t, int lossSendPct, int lossRecvPct,
                   int dupSendPct, int reorderMs, int lagMs) {
    GnsTransport *g = dynamic_cast<GnsTransport *>(t);
    if (g)
        g->fakeNet(lossSendPct, lossRecvPct, dupSendPct, reorderMs, lagMs);
}
