#ifndef F15_NET_TRANSPORT_H
#define F15_NET_TRANSPORT_H
/*
 * transport.h - transport abstraction (plan §14).
 *
 * The game protocol lives above this interface; GameNetworkingSockets is just
 * one backend. Peers are identified by opaque NetPeer handles (a server-side
 * connection id, or a single constant on a client).
 */
#include <stddef.h>
#include <stdint.h>

typedef uint32_t NetPeer;
#define NET_PEER_INVALID 0xFFFFFFFFu
#define NET_PEER_SERVER 0u /* client-side handle for the server */

enum NetSendFlags {
    NET_SEND_UNRELIABLE = 0, /* snapshot-style traffic: drop is fine */
    NET_SEND_RELIABLE = 1,   /* handshake/events: must arrive, ordered */
};

enum NetPeerState {
    NET_PEER_DISCONNECTED = 0,
    NET_PEER_CONNECTING,
    NET_PEER_CONNECTED,
};

enum NetEventKind {
    NET_EV_MESSAGE = 0, /* payload ready in .msg */
    NET_EV_CONNECTED,   /* server: new peer; client: connection up */
    NET_EV_DISCONNECTED,
};

struct NetRecv {
    enum NetEventKind kind;
    NetPeer peer;
    const uint8_t *msg;
    size_t len;
};

class NetTransport {
  public:
    virtual ~NetTransport() = default;

    /* Server: bind a listen port. bindAddr NULL/empty listens on every
     * interface; an IP literal binds just that interface. Client: connect
     * to "host:port". */
    virtual bool listen(const char *bindAddr, uint16_t port) = 0;
    virtual bool connect(const char *hostPort) = 0;
    virtual void closePeer(NetPeer peer, int reason) = 0;
    virtual void shutdown() = 0;

    virtual void poll() = 0; /* pump callbacks/callback queue */

    /* Pull the next queued event. Returns false when the queue is empty.
     * The returned msg buffer stays valid until the next recv()/poll(). */
    virtual bool recv(NetRecv *out) = 0;

    virtual void send(NetPeer peer, const void *data, size_t len, int flags) = 0;

    virtual enum NetPeerState peerState(NetPeer peer) const = 0;
};

/* Factory for the GameNetworkingSockets backend. Returns nullptr if GNS init
 * fails (caller should log and abort startup). */
NetTransport *createGnsTransport();

/* Test/diagnostic: apply GNS fake-network conditions to the transport's
 * client connection (loss %, dup %, reorder window, extra latency ms).
 * Server-side transports ignore it. No-op on non-GNS transports. */
void gnsSetFakeNet(NetTransport *t, int lossSendPct, int lossRecvPct,
                   int dupSendPct, int reorderMs, int lagMs);

#endif /* F15_NET_TRANSPORT_H */
