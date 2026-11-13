// PC-only, co-op (docs/MULTIPLAYER.md): bounded little-endian serialisation.
//
// Nothing goes over the wire as a struct. Every field is written and read one
// at a time, in a fixed byte order, through these two classes. Both are
// sticky: the first out-of-bounds access sets ok = false, every later access
// returns zero and writes nothing, and the caller checks ok once at the end.
// A Reader can never touch a byte outside [buf, buf + len).
#ifndef INETWIRE_H
#define INETWIRE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace iNet {

struct Writer
{
    uint8_t* buf;
    size_t cap;
    size_t len = 0;
    bool ok = true;

    Writer(uint8_t* b, size_t c) : buf(b), cap(c) {}

    uint8_t* reserve(size_t n)
    {
        if (!ok || n > cap - len) { ok = false; return nullptr; }
        uint8_t* p = buf + len;
        len += n;
        return p;
    }
    size_t room() const { return ok ? cap - len : 0; }
    void u8(uint8_t v) { if (uint8_t* p = reserve(1)) p[0] = v; }
    void u16(uint16_t v) { if (uint8_t* p = reserve(2)) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); } }
    void u32(uint32_t v)
    {
        if (uint8_t* p = reserve(4))
            for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
    }
    void i8(int8_t v) { u8((uint8_t)v); }
    void i16(int16_t v) { u16((uint16_t)v); }
    void f32(float f) { uint32_t u; memcpy(&u, &f, 4); u32(u); }
    void bytes(const void* s, size_t n) { if (uint8_t* p = reserve(n)) memcpy(p, s, n); }
    void zeros(size_t n) { if (uint8_t* p = reserve(n)) memset(p, 0, n); }
};

struct Reader
{
    const uint8_t* buf;
    size_t len;
    size_t pos = 0;
    bool ok = true;

    Reader(const uint8_t* b, size_t n) : buf(b), len(n) {}

    const uint8_t* take(size_t n)
    {
        if (!ok || n > len - pos) { ok = false; return nullptr; }
        const uint8_t* p = buf + pos;
        pos += n;
        return p;
    }
    void fail() { ok = false; }
    size_t left() const { return ok ? len - pos : 0; }
    bool done() const { return ok && pos == len; }
    uint8_t u8() { const uint8_t* p = take(1); return p ? p[0] : 0; }
    uint16_t u16() { const uint8_t* p = take(2); return p ? (uint16_t)(p[0] | p[1] << 8) : 0; }
    uint32_t u32()
    {
        const uint8_t* p = take(4);
        return p ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 : 0;
    }
    int8_t i8() { return (int8_t)u8(); }
    int16_t i16() { return (int16_t)u16(); }
    void bytes(void* d, size_t n)
    {
        const uint8_t* p = take(n);
        if (p) memcpy(d, p, n);
        else memset(d, 0, n);
    }
    // A float that must be finite and within [-limit, limit]. NaN fails both
    // comparisons, so it is rejected along with the infinities.
    float f32(float limit)
    {
        uint32_t u = u32();
        float f;
        memcpy(&f, &u, 4);
        if (!(f >= -limit && f <= limit)) { ok = false; return 0.0f; }
        return f;
    }
    // A u8 / u16 that must be < bound.
    uint8_t u8_below(unsigned bound) { uint8_t v = u8(); if (v >= bound) fail(); return ok ? v : 0; }
    uint16_t u16_below(unsigned bound) { uint16_t v = u16(); if (v >= bound) fail(); return ok ? v : 0; }
    // All of the next n bytes must be zero (padding, reserved space).
    void zeros(size_t n)
    {
        const uint8_t* p = take(n);
        if (!p) return;
        uint8_t acc = 0;
        for (size_t i = 0; i < n; i++) acc |= p[i];
        if (acc) fail();
    }
};

} // namespace iNet

#endif
