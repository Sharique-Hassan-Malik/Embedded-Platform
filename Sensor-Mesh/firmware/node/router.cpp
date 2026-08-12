#include "router.h"
#include "radio.h"
#include "config.h"
#include <string.h>

// ── Routing table ─────────────────────────────────────────────────────────────
struct RouteEntry {
    uint8_t  dest;
    uint8_t  next_hop;
    uint8_t  metric;       // hop count
    uint8_t  dest_seq;     // destination's sequence number (freshness)
    uint32_t updated_at;   // millis() when last refreshed
    bool     valid;
};

// ── Pending TX slot (waiting for routing-layer ACK) ──────────────────────────
struct PendingTX {
    Packet   pkt;
    uint8_t  next_hop;
    uint8_t  retries;
    uint32_t sent_at;
    bool     active;
};

// ── Reverse route cache (for forwarding RREP back toward RREQ originator) ────
struct ReverseEntry {
    uint8_t  origin;      // who originated the RREQ
    uint8_t  prev_hop;    // who we received it from (send RREP back here)
    uint32_t created_at;
    bool     valid;
};

static RouteEntry   g_routes[ROUTE_TABLE_SIZE]   = {};
static ReverseEntry g_reverse[ROUTE_TABLE_SIZE]  = {};
static PendingTX    g_pending                    = {};

// ── Deduplication cache ───────────────────────────────────────────────────────
struct DedupEntry {
    uint8_t origin;
    uint8_t seq;
};
static DedupEntry g_dedup[DEDUP_TABLE_SIZE] = {};
static uint8_t    g_dedup_head = 0;

// ── Module state ──────────────────────────────────────────────────────────────
static uint8_t              g_my_id    = 0;
static uint8_t              g_seq      = 0;     // per-node outgoing sequence counter
static Router::DataCallback g_data_cb  = nullptr;

// ── Helpers ───────────────────────────────────────────────────────────────────

static uint8_t nextSeq() { return ++g_seq; }

static bool isDuplicate(uint8_t origin, uint8_t seq) {
    for (uint8_t i = 0; i < DEDUP_TABLE_SIZE; ++i) {
        if (g_dedup[i].origin == origin && g_dedup[i].seq == seq) return true;
    }
    return false;
}

static void recordSeen(uint8_t origin, uint8_t seq) {
    g_dedup[g_dedup_head] = {origin, seq};
    g_dedup_head = (g_dedup_head + 1) % DEDUP_TABLE_SIZE;
}

// Find or allocate a routing table slot for dest.
static RouteEntry *findRoute(uint8_t dest) {
    for (uint8_t i = 0; i < ROUTE_TABLE_SIZE; ++i)
        if (g_routes[i].valid && g_routes[i].dest == dest) return &g_routes[i];
    return nullptr;
}

static RouteEntry *allocRoute() {
    // Prefer an invalid slot; otherwise evict the oldest valid entry.
    int8_t oldest = -1;
    uint32_t oldest_t = UINT32_MAX;
    for (uint8_t i = 0; i < ROUTE_TABLE_SIZE; ++i) {
        if (!g_routes[i].valid) return &g_routes[i];
        if (g_routes[i].updated_at < oldest_t) {
            oldest_t = g_routes[i].updated_at;
            oldest   = i;
        }
    }
    return &g_routes[oldest];
}

static void updateRoute(uint8_t dest, uint8_t next_hop, uint8_t metric, uint8_t dest_seq) {
    RouteEntry *r = findRoute(dest);
    if (!r) r = allocRoute();
    r->dest       = dest;
    r->next_hop   = next_hop;
    r->metric     = metric;
    r->dest_seq   = dest_seq;
    r->updated_at = millis();
    r->valid      = true;
}

static void invalidateRoute(uint8_t dest) {
    RouteEntry *r = findRoute(dest);
    if (r) r->valid = false;
}

static ReverseEntry *findReverse(uint8_t origin) {
    for (uint8_t i = 0; i < ROUTE_TABLE_SIZE; ++i)
        if (g_reverse[i].valid && g_reverse[i].origin == origin) return &g_reverse[i];
    return nullptr;
}

static void recordReverse(uint8_t origin, uint8_t prev_hop) {
    ReverseEntry *e = findReverse(origin);
    if (!e) {
        for (uint8_t i = 0; i < ROUTE_TABLE_SIZE; ++i) {
            if (!g_reverse[i].valid) { e = &g_reverse[i]; break; }
        }
        if (!e) e = &g_reverse[0];  // evict first entry as last resort
    }
    e->origin     = origin;
    e->prev_hop   = prev_hop;
    e->created_at = millis();
    e->valid      = true;
}

// Build the fixed packet header fields.
static void fillHeader(Packet &p, uint8_t dest, uint8_t type, uint8_t ttl = 0) {
    p.dest      = dest;
    p.src       = g_my_id;
    p.origin    = g_my_id;
    p.seq       = nextSeq();
    p.hop_count = 0;
    p.setType(type);
    p.setTTL(ttl ? ttl : ROUTE_TTL_INIT);
    memset(p.payload, 0, sizeof(p.payload));
}

// ── Send a PKT_ACK back to prev_hop for a received packet ────────────────────
static void sendAck(uint8_t prev_hop, uint8_t ack_seq, uint8_t ack_origin) {
    Packet ack;
    fillHeader(ack, prev_hop, PKT_ACK);
    auto *ap = reinterpret_cast<AckPayload *>(ack.payload);
    ap->ack_for_seq = ack_seq;
    ap->ack_origin  = ack_origin;
    Radio::send(prev_hop, ack);
}

// ── Initiate route discovery toward dest ──────────────────────────────────────
static void broadcastRREQ(uint8_t dest) {
    Packet rreq;
    fillHeader(rreq, BROADCAST_ID, PKT_RREQ);
    auto *rp     = reinterpret_cast<RREQPayload *>(rreq.payload);
    rp->rreq_dest = dest;
    RouteEntry *r = findRoute(dest);
    rp->rreq_seq  = r ? r->dest_seq : 0;
    Radio::broadcast(rreq);
}

// ── Handle received RREQ ──────────────────────────────────────────────────────
static void handleRREQ(const Packet &pkt) {
    if (isDuplicate(pkt.origin, pkt.seq)) return;
    recordSeen(pkt.origin, pkt.seq);

    const auto *rp = reinterpret_cast<const RREQPayload *>(pkt.payload);

    // Record reverse route: packets going back to pkt.origin go via pkt.src.
    recordReverse(pkt.origin, pkt.src);

    // Also update forward route to origin through pkt.src.
    updateRoute(pkt.origin, pkt.src, pkt.hop_count + 1, pkt.seq);

    if (rp->rreq_dest == g_my_id) {
        // We are the destination — send RREP back toward origin.
        ReverseEntry *rev = findReverse(pkt.origin);
        if (!rev) return;

        Packet rrep;
        fillHeader(rrep, pkt.origin, PKT_RREP);
        rrep.dest = rev->prev_hop;  // next hop back toward origin
        auto *resp    = reinterpret_cast<RREPPayload *>(rrep.payload);
        resp->rrep_dest = g_my_id;
        resp->metric    = 0;
        resp->dest_seq  = g_seq;
        Radio::send(rev->prev_hop, rrep);
        sendAck(pkt.src, pkt.seq, pkt.origin);
        return;
    }

    // Check if we have a fresh route to rreq_dest.
    RouteEntry *known = findRoute(rp->rreq_dest);
    if (known && known->valid && known->dest_seq >= rp->rreq_seq) {
        // We can gratuitously reply on behalf of the destination.
        ReverseEntry *rev = findReverse(pkt.origin);
        if (!rev) return;
        Packet rrep;
        fillHeader(rrep, pkt.origin, PKT_RREP);
        rrep.dest = rev->prev_hop;
        auto *resp    = reinterpret_cast<RREPPayload *>(rrep.payload);
        resp->rrep_dest = rp->rreq_dest;
        resp->metric    = known->metric;
        resp->dest_seq  = known->dest_seq;
        Radio::send(rev->prev_hop, rrep);
        sendAck(pkt.src, pkt.seq, pkt.origin);
        return;
    }

    // Forward RREQ if TTL allows.
    if (pkt.ttl() > 1) {
        Packet fwd = pkt;
        fwd.src = g_my_id;
        fwd.hop_count++;
        fwd.setTTL(pkt.ttl() - 1);
        Radio::broadcast(fwd);
        sendAck(pkt.src, pkt.seq, pkt.origin);
    }
}

// ── Handle received RREP ──────────────────────────────────────────────────────
static void handleRREP(const Packet &pkt) {
    const auto *rp = reinterpret_cast<const RREPPayload *>(pkt.payload);

    // Update routing table: we can reach rrep_dest via pkt.src in metric+1 hops.
    updateRoute(rp->rrep_dest, pkt.src, rp->metric + 1, rp->dest_seq);

    sendAck(pkt.src, pkt.seq, pkt.origin);

    // If this RREP is destined for us we're done — the route is now cached.
    if (pkt.dest == g_my_id) return;

    // Forward RREP toward the RREQ originator.
    ReverseEntry *rev = findReverse(pkt.dest);
    if (!rev) return;
    Packet fwd = pkt;
    fwd.src  = g_my_id;
    fwd.dest = rev->prev_hop;
    fwd.hop_count++;
    Radio::send(rev->prev_hop, fwd);
}

// ── Handle received DATA ──────────────────────────────────────────────────────
static void handleData(const Packet &pkt) {
    sendAck(pkt.src, pkt.seq, pkt.origin);

    if (pkt.dest == g_my_id || pkt.dest == BASE_NODE_ID) {
        // Deliver to application layer.
        if (g_data_cb) {
            const auto *dp = reinterpret_cast<const DataPayload *>(pkt.payload);
            g_data_cb(pkt.origin, pkt.hop_count, *dp);
        }
        return;
    }

    // Forward toward destination.
    RouteEntry *r = findRoute(pkt.dest);
    if (!r || !r->valid) {
        broadcastRREQ(pkt.dest);
        return;
    }
    Packet fwd = pkt;
    fwd.src = g_my_id;
    fwd.hop_count++;
    Radio::send(r->next_hop, fwd);
}

// ── Handle received ACK ───────────────────────────────────────────────────────
static void handleACK(const Packet &pkt) {
    if (!g_pending.active) return;
    const auto *ap = reinterpret_cast<const AckPayload *>(pkt.payload);
    if (ap->ack_origin == g_pending.pkt.origin &&
        ap->ack_for_seq == g_pending.pkt.seq) {
        g_pending.active = false;
    }
}

// ── Pending TX retry/timeout logic ───────────────────────────────────────────
static void processPending() {
    if (!g_pending.active) return;

    if (millis() - g_pending.sent_at < ACK_TIMEOUT_MS) return;

    if (g_pending.retries < MAX_RETRIES) {
        g_pending.retries++;
        g_pending.sent_at = millis();
        Radio::send(g_pending.next_hop, g_pending.pkt);
    } else {
        // Give up: invalidate the route and mark slot free.
        invalidateRoute(g_pending.pkt.dest);
        g_pending.active = false;
    }
}

// ── Queue a unicast packet with per-hop ACK ───────────────────────────────────
static void sendWithACK(uint8_t next_hop, const Packet &pkt) {
    g_pending.pkt      = pkt;
    g_pending.next_hop = next_hop;
    g_pending.retries  = 0;
    g_pending.sent_at  = millis();
    g_pending.active   = true;
    Radio::send(next_hop, pkt);
}

// ── Public interface ──────────────────────────────────────────────────────────
namespace Router {

void begin(uint8_t my_id) {
    g_my_id = my_id;
    memset(g_routes,  0, sizeof(g_routes));
    memset(g_reverse, 0, sizeof(g_reverse));
    memset(g_dedup,   0, sizeof(g_dedup));
    memset(&g_pending, 0, sizeof(g_pending));

    // Every node knows its own address trivially (0 hops, self as next_hop).
    updateRoute(my_id, my_id, 0, 0);
}

void update() {
    // Expire stale routes.
    uint32_t now = millis();
    for (uint8_t i = 0; i < ROUTE_TABLE_SIZE; ++i) {
        if (g_routes[i].valid && g_routes[i].dest != g_my_id &&
            now - g_routes[i].updated_at > ROUTE_EXPIRE_MS) {
            g_routes[i].valid = false;
        }
    }

    processPending();

    Packet pkt;
    while (Radio::receive(&pkt)) {
        switch (pkt.type()) {
            case PKT_RREQ: handleRREQ(pkt); break;
            case PKT_RREP: handleRREP(pkt); break;
            case PKT_DATA: handleData(pkt); break;
            case PKT_ACK:  handleACK(pkt);  break;
            default: break;
        }
    }
}

void sendData(uint8_t dest, const DataPayload &data) {
    RouteEntry *r = findRoute(dest);
    if (!r || !r->valid) {
        broadcastRREQ(dest);
        return;
    }

    Packet pkt;
    fillHeader(pkt, dest, PKT_DATA);
    memcpy(pkt.payload, &data, sizeof(DataPayload));
    sendWithACK(r->next_hop, pkt);
}

void onData(DataCallback cb) {
    g_data_cb = cb;
}

bool hasRoute(uint8_t dest) {
    RouteEntry *r = findRoute(dest);
    return r && r->valid;
}

uint8_t hopCount(uint8_t dest) {
    RouteEntry *r = findRoute(dest);
    return (r && r->valid) ? r->metric : 255;
}

} // namespace Router
