// PC-only, co-op: finding games on the home network (iNetDisc.h). Pure byte
// and table work, no OS.
#include "iNetDisc.h"

#include "iNetWire.h"

#include <stddef.h>
#include <string.h>

namespace iNet
{

    const uint8_t kDiscQuestionMagic[4] = { 'T', 'S', 'D', 'Q' };
    const uint8_t kDiscAnswerMagic[4] = { 'T', 'S', 'D', 'R' };

    uint8_t disc_this_platform()
    {
#if defined(_WIN32)
        return PLAT_WINDOWS;
#elif defined(__ANDROID__)
        return PLAT_ANDROID;
#elif defined(__SWITCH__)
        return PLAT_SWITCH;
#elif defined(__APPLE__)
        return PLAT_MACOS;
#elif defined(__linux__)
        return PLAT_LINUX;
#else
        return PLAT_OTHER;
#endif
    }

    const char* disc_platform_name(uint8_t p)
    {
        switch (p)
        {
        case PLAT_WINDOWS:
            return "Windows";
        case PLAT_ANDROID:
            return "Android";
        case PLAT_SWITCH:
            return "Switch";
        case PLAT_LINUX:
            return "Linux";
        case PLAT_MACOS:
            return "macOS";
        default:
            return "other device";
        }
    }

    // ---------------------------------------------------------------------------
    // The two messages

    size_t encode_discover(uint8_t* out, size_t cap, const uint8_t nonce[kDiscNonce])
    {
        Writer w(out, cap);
        w.bytes(kDiscQuestionMagic, 4);
        w.u8(kDiscVersion);
        w.u8(0); // flags
        w.bytes(nonce, kDiscNonce);
        w.zeros(kDiscoverSize - 6 - kDiscNonce);
        return w.ok ? w.len : 0;
    }

    bool parse_discover(const uint8_t* d, size_t n, Discover* out)
    {
        if (n != kDiscoverSize)
            return false;
        Reader r(d, n);
        if (memcmp(r.take(4), kDiscQuestionMagic, 4) != 0)
            return false;
        uint8_t version = r.u8();
        uint8_t flags = r.u8();
        Discover q;
        q.version = version;
        r.bytes(q.nonce, kDiscNonce);
        if (version == 0)
            return false;
        if (version == kDiscVersion)
        {
            // Ours: strict, so it re-encodes byte for byte.
            if (flags != 0)
                return false;
            r.zeros(kDiscoverSize - 6 - kDiscNonce);
            if (!r.done())
                return false;
        }
        // A newer version's rest is its own; the size and the prefix are fixed.
        if (!r.ok)
            return false;
        *out = q;
        return true;
    }

    static bool offer_fields_valid(const Offer& o)
    {
        if (o.disc_version != kDiscVersion || o.game_port < 1024 || o.instance == 0 ||
            o.state > OFFER_HAS_PLAYER)
            return false;
        if (o.scene != 0 && !scene_valid(o.scene))
            return false;
        size_t len = strnlen(o.name, kMaxName + 1);
        return name_valid(o.name, len);
    }

    size_t encode_offer(uint8_t* out, size_t cap, const Offer& o)
    {
        if (!offer_fields_valid(o))
            return 0;
        size_t len = strnlen(o.name, kMaxName + 1);
        Writer w(out, cap);
        w.bytes(kDiscAnswerMagic, 4);
        w.u8(o.disc_version);
        w.u16(o.proto);
        w.bytes(o.nonce, kDiscNonce);
        w.bytes(o.build, kIdPrefix);
        w.bytes(o.files, kIdPrefix);
        w.u16(o.game_port);
        w.u32(o.instance);
        w.u8(o.state);
        w.u8(o.platform);
        w.u32(o.scene);
        w.u8((uint8_t)len);
        w.bytes(o.name, len);
        return w.ok ? w.len : 0;
    }

    OfferKind parse_offer(const uint8_t* d, size_t n, Offer* out)
    {
        if (n < kOfferPrefix || n >= kDiscoverSize)
            return OFFER_BAD;
        Reader r(d, n);
        if (memcmp(r.take(4), kDiscAnswerMagic, 4) != 0)
            return OFFER_BAD;
        Offer o;
        memset(&o, 0, sizeof o);
        o.disc_version = r.u8();
        o.proto = r.u16();
        r.bytes(o.nonce, kDiscNonce);
        if (!r.ok || o.disc_version == 0)
            return OFFER_BAD;
        if (o.disc_version != kDiscVersion)
        {
            *out = o;
            return OFFER_NEWER;
        }
        if (n < kOfferMin || n > kOfferMax)
            return OFFER_BAD;
        r.bytes(o.build, kIdPrefix);
        r.bytes(o.files, kIdPrefix);
        o.game_port = r.u16();
        o.instance = r.u32();
        o.state = r.u8();
        o.platform = r.u8();
        o.scene = r.u32();
        uint8_t len = r.u8();
        if (!r.ok || len < 1 || len > kMaxName)
            return OFFER_BAD;
        r.bytes(o.name, len);
        o.name[len] = '\0';
        // A NUL inside the name would shorten it: not canonical.
        if (!r.done() || strnlen(o.name, kMaxName + 1) != len || !offer_fields_valid(o))
            return OFFER_BAD;
        *out = o;
        return OFFER_V1;
    }

    // ---------------------------------------------------------------------------
    // Addresses and interfaces

    bool v4_of(const NetAddr& a, uint8_t ip[4])
    {
        if (!addr_is_v4(a))
            return false;
        memcpy(ip, a.ip + 12, 4);
        return true;
    }

    bool v4_private(const uint8_t ip[4])
    {
        return ip[0] == 10 || (ip[0] == 172 && (ip[1] & 0xF0) == 16) ||
               (ip[0] == 192 && ip[1] == 168) || (ip[0] == 169 && ip[1] == 254);
    }

    bool v4_loopback(const uint8_t ip[4])
    {
        return ip[0] == 127;
    }

    static uint32_t be32(const uint8_t ip[4])
    {
        return (uint32_t)ip[0] << 24 | (uint32_t)ip[1] << 16 | (uint32_t)ip[2] << 8 | ip[3];
    }

    static uint32_t mask_of(uint8_t prefix)
    {
        return prefix == 0 ? 0 : prefix >= 32 ? 0xFFFFFFFFu : 0xFFFFFFFFu << (32 - prefix);
    }

    bool v4_in_subnet(const uint8_t ip[4], const IfV4& i)
    {
        uint32_t m = mask_of(i.prefix);
        return (be32(ip) & m) == (be32(i.ip) & m);
    }

    void v4_broadcast(const IfV4& i, uint8_t out[4])
    {
        uint32_t b = be32(i.ip) | ~mask_of(i.prefix);
        out[0] = (uint8_t)(b >> 24);
        out[1] = (uint8_t)(b >> 16);
        out[2] = (uint8_t)(b >> 8);
        out[3] = (uint8_t)b;
    }

    NetAddr v4_addr(const uint8_t ip[4], uint16_t port)
    {
        NetAddr a;
        memset(&a, 0, sizeof a);
        a.ip[10] = a.ip[11] = 0xff;
        memcpy(a.ip + 12, ip, 4);
        a.port = port;
        return a;
    }

    size_t disc_ask_targets(const IfV4& i, uint16_t disc_port, NetAddr out[kAskTargets])
    {
        const uint8_t all[4] = { 255, 255, 255, 255 };
        uint8_t b[4];
        v4_broadcast(i, b);
        out[0] = v4_addr(all, disc_port);
        out[1] = v4_addr(b, disc_port);
        return kAskTargets;
    }

    static bool usable_v4(const uint8_t ip[4], uint8_t prefix)
    {
        if (prefix < 8 || prefix > 30 || !v4_private(ip))
            return false;
        IfV4 i;
        memcpy(i.ip, ip, 4);
        i.prefix = prefix;
        uint8_t b[4];
        v4_broadcast(i, b);
        // Neither the network's own address nor its broadcast.
        uint32_t host = be32(ip) & ~mask_of(prefix);
        return host != 0 && memcmp(b, ip, 4) != 0;
    }

    size_t if_filter(const IfRecord* recs, size_t n, IfV4* out, size_t cap)
    {
        size_t k = 0;
        for (size_t r = 0; r < n; r++)
        {
            const IfRecord& c = recs[r];
            if (!c.up || !c.hardware || c.loopback || c.point_to_point || !c.broadcast)
                continue;
            size_t count = c.count < 4 ? c.count : 4;
            // A 169.254 address is the network only when the adapter has nothing
            // else (two devices on a cable, no router).
            bool other = false;
            for (size_t a = 0; a < count; a++)
                other =
                    other || (c.addr[a].preferred && usable_v4(c.addr[a].ip, c.addr[a].prefix) &&
                              !(c.addr[a].ip[0] == 169 && c.addr[a].ip[1] == 254));
            for (size_t a = 0; a < count && k < cap; a++)
            {
                const uint8_t* ip = c.addr[a].ip;
                if (!c.addr[a].preferred || !usable_v4(ip, c.addr[a].prefix))
                    continue;
                if (other && ip[0] == 169 && ip[1] == 254)
                    continue;
                bool dup = false;
                for (size_t j = 0; j < k; j++)
                    dup = dup || memcmp(out[j].ip, ip, 4) == 0;
                if (dup)
                    continue;
                memcpy(out[k].ip, ip, 4);
                out[k].prefix = c.addr[a].prefix;
                k++;
            }
        }
        return k;
    }

    bool disc_probe_target(const char* address, uint16_t disc_port, bool loopback_test,
                           NetAddr* out)
    {
        NetAddr a;
        uint8_t ip[4];
        if (address == NULL || !addr_parse(address, 1, &a) || !v4_of(a, ip))
            return false;
        if (!(v4_private(ip) || (loopback_test && v4_loopback(ip))))
            return false;
        if (be32(ip) == 0xFFFFFFFFu ||
            (ip[0] == 169 && ip[1] == 254 && ip[2] == 255 && ip[3] == 255))
            return false;
        *out = v4_addr(ip, disc_port);
        return true;
    }

    bool disc_host_visible(bool hosting, bool visible, bool loopback_host, bool local_host,
                           bool loopback_test)
    {
        return hosting && visible && !local_host && (!loopback_host || loopback_test);
    }

    // ---------------------------------------------------------------------------
    // Rate limiting

    bool bucket_take(Bucket* b, uint64_t now, uint32_t per_second, uint32_t burst)
    {
        uint32_t full = burst * 1000;
        if (!b->started)
        {
            b->started = true;
            b->milli = full;
            b->at = now;
        }
        else if (now > b->at)
        {
            uint64_t dt = now - b->at;
            if (dt > 10ull * 1000000000ull)
                dt = 10ull * 1000000000ull;
            uint64_t add = dt * per_second / 1000000; // tokens x 1000 for dt ns
            uint64_t m = b->milli + add;
            if (m >= full)
            {
                b->milli = full;
                b->at = now;
            }
            else
            {
                // Only the time that became tokens is used up, so packets close
                // together still add up to the full rate.
                b->milli = (uint32_t)m;
                b->at += (add * 1000000 + per_second - 1) / per_second;
            }
        }
        if (b->milli < 1000)
            return false;
        b->milli -= 1000;
        return true;
    }

    // ---------------------------------------------------------------------------
    // The host's side

    void Responder::reset()
    {
        Offer keep = offer;
        memset(this, 0, sizeof *this);
        offer = keep;
    }

    static bool source_ok(const uint8_t ip[4], const IfV4* ifs, size_t count, bool loopback_test)
    {
        if (loopback_test)
            return v4_loopback(ip);
        if (!v4_private(ip))
            return false;
        // A forged source must not turn the answer into a broadcast or send it
        // to this device's own sockets.
        bool inside = false;
        for (size_t i = 0; i < count; i++)
        {
            uint8_t bc[4];
            v4_broadcast(ifs[i], bc);
            uint32_t m = mask_of(ifs[i].prefix);
            if (memcmp(ip, ifs[i].ip, 4) == 0 || memcmp(ip, bc, 4) == 0 ||
                (v4_in_subnet(ip, ifs[i]) && (be32(ip) & ~m) == 0))
                return false;
            inside = inside || v4_in_subnet(ip, ifs[i]);
        }
        return inside;
    }

    size_t Responder::on_question(const NetAddr& from, const uint8_t* d, size_t n, uint64_t now,
                                  uint8_t* out, size_t cap)
    {
        if (n != kDiscoverSize)
        {
            st.drop_size++;
            return 0;
        }
        Discover q;
        if (!parse_discover(d, n, &q))
        {
            st.drop_bad++;
            return 0;
        }
        uint8_t ip[4];
        if (!v4_of(from, ip) || from.port < 1024 || !source_ok(ip, ifs, ifs_count, loopback_test))
        {
            st.drop_source++;
            return 0;
        }
        // That source's bucket: its own, a free one, or the one heard from least lately.
        size_t pick = kResponderSources;
        for (size_t i = 0; i < kResponderSources && pick == kResponderSources; i++)
            if (src[i].used && memcmp(src[i].ip, ip, 4) == 0)
                pick = i;
        if (pick == kResponderSources)
        {
            pick = 0;
            for (size_t i = 0; i < kResponderSources; i++)
            {
                if (!src[i].used)
                {
                    pick = i;
                    break;
                }
                if (src[i].seen < src[pick].seen)
                    pick = i;
            }
            memset(&src[pick], 0, sizeof src[pick]);
            memcpy(src[pick].ip, ip, 4);
            src[pick].used = true;
        }
        src[pick].seen = now;
        if (!bucket_take(&src[pick].b, now, kAnswerPerSource, kAnswerBurst) ||
            !bucket_take(&all, now, kAnswerTotal, kAnswerTotal))
        {
            st.drop_rate++;
            return 0;
        }
        Offer o = offer;
        memcpy(o.nonce, q.nonce, kDiscNonce);
        size_t len = encode_offer(out, cap, o);
        if (len == 0 || len >= n)
            return 0; // never larger than the question
        st.answered++;
        return len;
    }

    // ---------------------------------------------------------------------------
    // The joiner's side

    void Finder::reset()
    {
        uint8_t b[kIdPrefix], f[kIdPrefix];
        memcpy(b, build, sizeof b);
        memcpy(f, files, sizeof f);
        IfV4 keep[kMaxIfs];
        memcpy(keep, ifs, sizeof keep);
        uint8_t kc = ifs_count;
        bool lt = loopback_test, fk = files_known;
        uint16_t port = disc_port;
        memset(this, 0, sizeof *this);
        memcpy(build, b, sizeof b);
        memcpy(files, f, sizeof f);
        files_known = fk;
        memcpy(ifs, keep, sizeof keep);
        ifs_count = kc;
        loopback_test = lt;
        disc_port = port;
    }

    void Finder::asked(const uint8_t nonce[kDiscNonce])
    {
        memcpy(nonces[nonce_next], nonce, kDiscNonce);
        nonce_next = (uint8_t)((nonce_next + 1) % kNonceWindow);
        if (nonce_count < kNonceWindow)
            nonce_count++;
    }

    bool Finder::on_answer(const NetAddr& from, const uint8_t* d, size_t n, uint64_t now)
    {
        uint8_t ip[4];
        // The last host asked by address may be on another of the home's
        // subnets, so any private source; the internet never. Checked before
        // the rate so strangers cannot use it up.
        if (!v4_of(from, ip) || from.port != disc_port ||
            !(loopback_test ? v4_loopback(ip) : v4_private(ip)))
        {
            st.drop_source++;
            return false;
        }
        if (!bucket_take(&rate, now, kFinderPerSecond, kFinderPerSecond))
        {
            st.drop_rate++;
            return false;
        }
        Offer o;
        OfferKind kind = parse_offer(d, n, &o);
        if (kind == OFFER_BAD)
        {
            st.drop_bad++;
            return false;
        }
        bool known = false;
        for (size_t i = 0; i < nonce_count && !known; i++)
            known = memcmp(nonces[i], o.nonce, kDiscNonce) == 0;
        if (!known)
        {
            st.drop_nonce++;
            return false;
        }
        st.answers++;

        Found f;
        memset(&f, 0, sizeof f);
        f.addr = v4_addr(ip, kind == OFFER_V1 ? o.game_port : 0);
        f.newer = kind == OFFER_NEWER;
        f.last = now;
        if (kind == OFFER_V1)
        {
            f.state = o.state;
            f.platform = o.platform;
            f.scene = o.scene;
            f.instance = o.instance;
            memcpy(f.name, o.name, sizeof f.name);
            memcpy(f.build, o.build, kIdPrefix);
            memcpy(f.files, o.files, kIdPrefix);
            bool same = o.proto == kProtoVersion && memcmp(o.build, build, kIdPrefix) == 0;
            f.kind = !same                                                  ? FOUND_VERSION :
                     !files_known || memcmp(o.files, files, kIdPrefix) == 0 ? FOUND_READY :
                                                                              FOUND_FILES;
        }
        else
        {
            f.kind = FOUND_VERSION;
        }
        // One entry per (address, game port, instance): the same answer over two
        // of this device's interfaces is one game.
        for (size_t i = 0; i < count; i++)
        {
            if (addr_equal(list[i].addr, f.addr) && list[i].instance == f.instance &&
                list[i].newer == f.newer)
            {
                bool changed = memcmp(&list[i], &f, offsetof(Found, last)) != 0;
                list[i] = f;
                return changed;
            }
        }
        // One device cannot fill the list by changing its instance.
        size_t same_ip = 0;
        for (size_t i = 0; i < count; i++)
            same_ip += memcmp(list[i].addr.ip, f.addr.ip, sizeof f.addr.ip) == 0;
        if (count == kMaxFound || same_ip >= kFoundPerAddress)
        {
            st.drop_full++;
            return false;
        }
        list[count++] = f;
        st.added++;
        return true;
    }

    bool Finder::expire(uint64_t now)
    {
        size_t keep = 0;
        for (size_t i = 0; i < count; i++)
        {
            if (now >= list[i].last && now - list[i].last >= kFoundExpireNs)
                continue;
            if (keep != i)
                list[keep] = list[i];
            keep++;
        }
        bool changed = keep != count;
        count = (uint8_t)keep;
        return changed;
    }

} // namespace iNet
