/* Exercise real server orchestration with a recording transport and no assets.
 * Include the TU so production handlers can remain private. */
#include "../src/server/f15server.cpp"
#include <vector>
static int fails;
#define CHECK(x)                                            \
    do {                                                    \
        if (!(x)) {                                         \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); \
            ++fails;                                        \
        }                                                   \
    } while (0)
class MockNet final : public NetTransport {
  public:
    std::vector<NetPeer> sent, closed;
    bool listen(const char *, uint16_t) override { return true; }
    bool connect(const char *) override { return true; }
    void closePeer(NetPeer p, int) override { closed.push_back(p); }
    void shutdown() override {}
    void poll() override {}
    bool recv(NetRecv *) override { return false; }
    void send(NetPeer p, const void *, size_t, int) override { sent.push_back(p); }
    NetPeerState peerState(NetPeer) const override { return NET_PEER_CONNECTED; }
};
int main() {
    static GameComm comm{};
    commData = &comm;
    MockNet net;
    g_net = &net;
    for (int i = 0; i < 2; ++i) {
        g_players[i].used = g_players[i].ready = 1;
        g_players[i].peer = 100 + i;
        g_players[i].ctx.active = 1;
    }
    // A locally closed connection must release its existing player immediately.
    NetHello hello{};
    hello.protoVer = F15_NET_VERSION + 1;
    uint8_t buf[64];
    NetWriter w;
    nwInit(&w, buf, sizeof(buf));
    encHello(&w, &hello);
    NetReader r;
    nrInit(&r, buf, w.len);
    onHello(100, &r);
    CHECK(net.closed.size() == 1 && net.closed[0] == 100);
    CHECK(!g_players[0].used && !g_players[0].ready);
    CHECK(g_players[1].used && g_players[1].ready);
    CHECK(slotFree() == 0);
    g_players[0].used = g_players[0].ready = 1;
    net.sent.clear();
    // Observation generation must not change the pilot or the advertised hash.
    g_players[0].role = NET_ROLE_AI;
    PlayerSim &p = g_players[0].ctx;
    p.viewX_ = p.viewY_ = 0x4000;
    p.targetRange = 123;
    p.targetBearing = 456;
    p.closestThreatIndex = -1;
    g_planeCount = 1;
    g_planeTable.planes[0].mapX = 0x4100;
    g_planeTable.planes[0].mapY = 0x4200;
    g_obsFull = 1;
    const PlayerSim before = p;
    const uint32 hash = worldHash();
    sendObservations();
    CHECK(net.sent.size() == 1 && net.sent[0] == 100);
    CHECK(memcmp(&before, &p, sizeof(p)) == 0);
    CHECK(worldHash() == hash);
    // World threats address the selected victim, then restore the resident peer.
    net.sent.clear();
    g_worldCtxIdx = 0;
    g_residentPlayer = 0;
    g_curPeer = 100;
    playerSwapIn(&g_players[0].ctx);
    swapInVictim(1);
    evHud("Incoming missile");
    swapBackFromVictim();
    evHud("Resident pilot");
    CHECK(net.sent.size() == 2);
    if (net.sent.size() == 2) {
        CHECK(net.sent[0] == 101);
        CHECK(net.sent[1] == 100);
    }
    // Each ownship block transports that pilot's warning state, including clear.
    g_players[0].ctx.threatWarningBits = PLAYER_WARN_IR;
    g_players[1].ctx.threatWarningBits = PLAYER_WARN_RADAR;
    for (int pilot = 0; pilot < 2; ++pilot) {
        uint8_t playerBuf[256];
        NetWriter pw;
        NetReader pr;
        nwInit(&pw, playerBuf, sizeof(playerBuf));
        netEncPlayerFromCtx(&pw, &g_players[pilot].ctx);
        CHECK(!pw.overflow);
        nrInit(&pr, playerBuf, pw.len);
        netApplyPlayerToGlobals(&pr);
        CHECK(!pr.underrun);
        CHECK(g_threatWarningBits == g_players[pilot].ctx.threatWarningBits);
    }
    playerSwapIn(&g_players[0].ctx);
    CHECK(g_threatWarningBits == PLAYER_WARN_IR);
    playerSwapIn(&g_players[1].ctx);
    CHECK(g_threatWarningBits == PLAYER_WARN_RADAR);
    // A new pass with no active shots must clear the previous warning.
    memset(g_projectiles, 0, F15_MAX_PROJECTILES * sizeof(g_projectiles[0]));
    g_hudVisible = 0;
    updateThreatTargeting();
    CHECK(g_threatWarningBits == 0);
    return fails ? 1 : 0;
}
