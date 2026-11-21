// xsave_selftest: the other consoles' saves, without the game (docs/SAVES.md
// sections 6 to 8).
//
//   xsave_selftest <scratch folder> [<Xbox files> [<GameCube files> [<saves>
//                  [<PlayStation 2 files>]]]]
//
// What it checks:
//   - SHA-1 against FIPS 180-1's answers and HMAC-SHA1 against RFC 2202's;
//   - the Xbox save folder's name for the game's three save names, and the
//     two .xbx text files;
//   - the signature: the title key and the 20 bytes, with a made-up keys.bin
//     (bytes 1..32) and a made-up XBE, against HMAC written out from its
//     definition here and against values worked out with another program;
//   - the little-endian conversion: a made-up save, written by a reference
//     encoder that follows xSerial's rule for each console, converts to the
//     other console's bytes exactly, and back; with packages made here in both
//     consoles' byte order saying what the made-up level saves;
//   - the store: a slot exported as an Xbox save with either signature has the
//     layout the game writes, and comes back byte for byte from the save's
//     folder, the game's, a UDATA, an emulator's data folder and a zip,
//     whatever its signature; what is missing is said plainly; no message ever
//     holds a key;
//   - the command line does the same;
//   - the PlayStation 2 side: the folder's and files' names, the card's clock,
//     icon.sys, the filler and the port's own icon; the card's error-correcting
//     code against known answers; .psu and .psv files written and read; a
//     memory card image formatted here, written to and read back, with and
//     without its spare bytes, another game's folder on it staying intact; and
//     through the store and the command line: export to a .psu, a card file
//     (copied first) and PCSX2's folder-type card, and import from each;
//   - the PlayStation 3: AES-128 against the standards' answers; a .psv
//     written with the PS3's layout and read back; its signature, the
//     stand-in and one made with a made-up key file, against the public
//     description written out here (and tests/psv_sign_check.py, which
//     drives --psv-sign below from an implementation of its own); key files
//     of the wrong size refused; through the store and the command line,
//     .psv files written, added to and imported, each saying which signature
//     it carries; a .VM2 card; and no key file's bytes in anything written
//     or said.
//
// With --psv-sign KEYS IN OUT it reads the .psv IN, writes it again with the
// port's writer (iSavePS2PsvBuild, its seed kept) signed with the key file
// KEYS ("-" for the stand-in) to OUT, and says only whether it did; with
// --psv-check KEYS IN, exit 0 when IN's signature is KEYS's, 3 when not.
//
// Everything above is made in the test: no game file and no real key. With the
// player's folders it also reads their own DEFAULT.XBE (name, pictures) and
// every package (only the levels and MNUC hold saved objects, and both discs
// agree), converts each save in <saves> there and back, reads each real
// PlayStation 2 save file there (.max, .psv, .psu, a card) and compares what
// the game wrote beside it with what the port writes, and finds the game's
// icon in the PlayStation 2 disc's PLAT.HIP. A real PS3 .psv there is written
// again, and imported and exported again, and compared byte for byte.

#include "iHost.h"
#include "iSaveAES.h"
#include "iSaveCLI.h"
#include "iSaveFormat.h"
#include "iSaveLE.h"
#include "iSavePS2.h"
#include "iSaveSHA1.h"
#include "iSaveStore.h"
#include "iSaveXbox.h"
#include "iSaveZip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

// --- what the store calls outside itself --------------------------------------------------

static std::string sConfigXbe;
static std::string sConfigKeys;
static std::string sConfigPs2Icon;
static std::string sConfigPsvKeys;

const char* iFileModName()
{
    return "";
}

const char* iFileModRoot()
{
    return "";
}

const char* iFileAssetRoot()
{
    return "";
}

const char* iConfigGetString(const char* key, const char* def)
{
    if (strcmp(key, "game.xbox_xbe") == 0)
    {
        return sConfigXbe.c_str();
    }
    if (strcmp(key, "game.xbox_keys") == 0)
    {
        return sConfigKeys.c_str();
    }
    if (strcmp(key, "game.ps2_icon") == 0)
    {
        return sConfigPs2Icon.c_str();
    }
    if (strcmp(key, "game.psv_keys") == 0)
    {
        return sConfigPsvKeys.c_str();
    }
    return def;
}

bool iSaveStoreAutoSaveSlot(S32*, S32*)
{
    return false;
}

// --- the test's own tools ---------------------------------------------------------------

static S32 sFailures;
static S32 sChecks;

static void check(bool ok, const char* what)
{
    sChecks++;
    if (!ok)
    {
        sFailures++;
        printf("FAIL: %s\n", what);
    }
}

static std::string sDir;

static std::string at(const char* name)
{
    return sDir + "/" + name;
}

static bool readFile(const std::string& path, std::vector<U8>& out)
{
    return iSaveStoreReadFile(path.c_str(), out, NULL, 0);
}

static bool writeFile(const std::string& path, const std::vector<U8>& data)
{
    return iSaveStoreWriteFile(path.c_str(), data.empty() ? NULL : &data[0], data.size(), false,
                               NULL, 0);
}

static void removeTree(const std::string& dir)
{
    iHostDir* d = iHostDirOpen(dir.c_str());
    if (d == NULL)
    {
        return;
    }
    std::vector<std::string> names;
    for (const char* n = iHostDirNext(d); n != NULL; n = iHostDirNext(d))
    {
        names.push_back(n);
    }
    iHostDirClose(d);

    for (size_t i = 0; i < names.size(); i++)
    {
        std::string p = dir + "/" + names[i];
        iHostFileInfo st;
        if (iHostStat(p.c_str(), &st) && st.is_dir)
        {
            removeTree(p);
            iHostRemoveDir(p.c_str());
        }
        else
        {
            iHostRemoveFile(p.c_str());
        }
    }
}

static std::string hex(const U8* p, size_t n)
{
    std::string s;
    for (size_t i = 0; i < n; i++)
    {
        char b[4];
        snprintf(b, sizeof(b), "%02x", p[i]);
        s += b;
    }
    return s;
}

static std::vector<U8> unhex(const char* text)
{
    std::vector<U8> v;
    for (size_t i = 0; text[i] != '\0' && text[i + 1] != '\0'; i += 2)
    {
        char b[3] = { text[i], text[i + 1], 0 };
        v.push_back((U8)strtoul(b, NULL, 16));
    }
    return v;
}

static void put32be(std::vector<U8>& v, U32 x)
{
    v.push_back((U8)(x >> 24));
    v.push_back((U8)(x >> 16));
    v.push_back((U8)(x >> 8));
    v.push_back((U8)x);
}

static void put32(std::vector<U8>& v, U32 x, bool big)
{
    if (big)
    {
        put32be(v, x);
    }
    else
    {
        v.push_back((U8)x);
        v.push_back((U8)(x >> 8));
        v.push_back((U8)(x >> 16));
        v.push_back((U8)(x >> 24));
    }
}

static void put16(std::vector<U8>& v, U32 x, bool big)
{
    if (big)
    {
        v.push_back((U8)(x >> 8));
        v.push_back((U8)x);
    }
    else
    {
        v.push_back((U8)x);
        v.push_back((U8)(x >> 8));
    }
}

// --- SHA-1 and HMAC ----------------------------------------------------------------------

static std::string sha1Hex(const void* p, size_t n)
{
    U8 d[ISHA1_SIZE];
    iSHA1Of(p, n, d);
    return hex(d, sizeof(d));
}

static std::string hmacHex(const std::vector<U8>& key, const std::vector<U8>& data)
{
    U8 d[ISHA1_SIZE];
    iHMACSHA1(key.empty() ? NULL : &key[0], key.size(), data.empty() ? NULL : &data[0],
              data.size(), NULL, 0, d);
    return hex(d, sizeof(d));
}

// HMAC from RFC 2104's definition, with nothing of iHMACSHA1 in it:
// H((K ^ opad) | H((K ^ ipad) | text)), a key longer than a block hashed first.
static void hmacByDefinition(const U8* key, size_t keysize, const U8* text, size_t n, U8* out)
{
    U8 k[64];
    memset(k, 0, sizeof(k));
    if (keysize > 64)
    {
        iSHA1Of(key, keysize, k);
    }
    else
    {
        memcpy(k, key, keysize);
    }

    std::vector<U8> inner;
    for (int i = 0; i < 64; i++)
    {
        inner.push_back((U8)(k[i] ^ 0x36));
    }
    inner.insert(inner.end(), text, text + n);
    U8 ih[ISHA1_SIZE];
    iSHA1Of(&inner[0], inner.size(), ih);

    std::vector<U8> outer;
    for (int i = 0; i < 64; i++)
    {
        outer.push_back((U8)(k[i] ^ 0x5C));
    }
    outer.insert(outer.end(), ih, ih + ISHA1_SIZE);
    iSHA1Of(&outer[0], outer.size(), out);
}

static std::vector<U8> bytesOf(const char* s)
{
    return std::vector<U8>((const U8*)s, (const U8*)s + strlen(s));
}

static void testHash()
{
    // FIPS 180-1, appendix A and B, and the empty message.
    check(sha1Hex("abc", 3) == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA-1 of \"abc\"");
    const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    check(sha1Hex(two, strlen(two)) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1",
          "SHA-1 of the 56-character message");
    check(sha1Hex("", 0) == "da39a3ee5e6b4b0d3255bfef95601890afd80709", "SHA-1 of nothing");

    std::vector<U8> million(1000000, (U8)'a');
    check(sha1Hex(&million[0], million.size()) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f",
          "SHA-1 of a million a's");

    // The same in pieces of every size up to a block and a bit.
    bool pieces = true;
    for (size_t step = 1; step <= 70 && pieces; step++)
    {
        iSHA1 s;
        iSHA1Init(&s);
        for (size_t i = 0; i < 1000; i += step)
        {
            iSHA1Update(&s, &million[i], (i + step <= 1000) ? step : 1000 - i);
        }
        U8 d[ISHA1_SIZE];
        iSHA1Final(&s, d);
        U8 whole[ISHA1_SIZE];
        iSHA1Of(&million[0], 1000, whole);
        pieces = memcmp(d, whole, ISHA1_SIZE) == 0;
    }
    check(pieces, "SHA-1 taken in pieces of any size is the same");

    // RFC 2202, section 3: all seven cases.
    check(hmacHex(std::vector<U8>(20, 0x0b), bytesOf("Hi There")) ==
              "b617318655057264e28bc0b6fb378c8ef146be00",
          "HMAC-SHA1, RFC 2202 case 1");
    check(hmacHex(bytesOf("Jefe"), bytesOf("what do ya want for nothing?")) ==
              "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79",
          "HMAC-SHA1, RFC 2202 case 2");
    check(hmacHex(std::vector<U8>(20, 0xaa), std::vector<U8>(50, 0xdd)) ==
              "125d7342b9ac11cd91a39af48aa17b4f63f175d3",
          "HMAC-SHA1, RFC 2202 case 3");
    std::vector<U8> key4;
    for (U8 i = 1; i <= 25; i++)
    {
        key4.push_back(i);
    }
    check(hmacHex(key4, std::vector<U8>(50, 0xcd)) == "4c9007f4026250c6bc8414f9bf50c86c2d7235da",
          "HMAC-SHA1, RFC 2202 case 4");
    check(hmacHex(std::vector<U8>(20, 0x0c), bytesOf("Test With Truncation")) ==
              "4c1a03424b55e07fe7f27be1d58bb9324a9a5a04",
          "HMAC-SHA1, RFC 2202 case 5");
    check(hmacHex(std::vector<U8>(80, 0xaa),
                  bytesOf("Test Using Larger Than Block-Size Key - Hash Key First")) ==
              "aa4ae5e15272d00e95705637ce8a3b55ed402112",
          "HMAC-SHA1, RFC 2202 case 6");
    check(hmacHex(std::vector<U8>(80, 0xaa),
                  bytesOf("Test Using Larger Than Block-Size Key and Larger Than One Block-Size "
                          "Data")) == "e8e99d0f45237d786d6bbaa7965c7808bbff1a91",
          "HMAC-SHA1, RFC 2202 case 7");

    // Two parts are one message.
    U8 a[ISHA1_SIZE];
    U8 b[ISHA1_SIZE];
    iHMACSHA1("key", 3, "Hello, ", 7, "world", 5, a);
    iHMACSHA1("key", 3, "Hello, world", 12, NULL, 0, b);
    check(memcmp(a, b, ISHA1_SIZE) == 0, "HMAC of a message in two parts");
}

// --- names -------------------------------------------------------------------------------

static void testNames()
{
    static const char* const kFolders[3] = { "7B317197822D", "7B317197822E", "7B317197822F" };
    for (S32 g = 0; g < 3; g++)
    {
        char name[32];
        char folder[16];
        iSaveXboxSaveName(g, name, sizeof(name));
        iSaveXboxFolderName(name, folder);
        char what[96];
        snprintf(what, sizeof(what), "save %s is folder %s", name, kFolders[g]);
        check(strcmp(folder, kFolders[g]) == 0 && iSaveXboxGameOfName(name) == g, what);
    }
    check(iSaveXboxGameOfName("SpongeBobMovie04") < 0 && iSaveXboxGameOfName("Other01") < 0,
          "other names are no slot");

    std::vector<U8> meta;
    iSaveXboxMeta("Name", "SpongeBobMovie01", meta);
    check(meta.size() == 48 && meta[0] == 0xFF && meta[1] == 0xFE && meta[2] == 'N' &&
              meta[3] == 0 && meta[46] == '\n' && meta[44] == '\r',
          "SaveMeta.xbx is 48 bytes of UTF-16 with a byte-order mark and CR LF");
    char value[64];
    check(iSaveXboxMetaValue(&meta[0], meta.size(), "Name", value, sizeof(value)) &&
              strcmp(value, "SpongeBobMovie01") == 0,
          "and gives its name back");

    iSaveXboxMeta("TitleName", "The SpongeBob SquarePants Movie", meta);
    check(meta.size() == 88, "TitleMeta.xbx is 88 bytes");
    check(!iSaveXboxMetaValue(&meta[0], meta.size(), "Name", value, sizeof(value)),
          "a key that is not there is not found");
}

// --- a made-up XBE and keys.bin --------------------------------------------------------------

static const char* const kTitle = "The SpongeBob SquarePants Movie";

static std::vector<U8> makeXbe(U32 titleId)
{
    const U32 base = 0x10000;
    std::vector<U8> x(0x1000, 0);
    memcpy(&x[0], "XBEH", 4);

    struct Put
    {
        static void at(std::vector<U8>& v, size_t off, U32 val)
        {
            v[off] = (U8)val;
            v[off + 1] = (U8)(val >> 8);
            v[off + 2] = (U8)(val >> 16);
            v[off + 3] = (U8)(val >> 24);
        }
    };

    const U32 cert = 0x180;
    const U32 sections = 0x400;
    const U32 names = 0x500;
    Put::at(x, 0x104, base);
    Put::at(x, 0x118, base + cert);
    Put::at(x, 0x11C, 3);
    Put::at(x, 0x120, base + sections);

    Put::at(x, cert + 0, 0x1D0);
    Put::at(x, cert + 8, titleId);
    for (size_t i = 0; kTitle[i] != '\0'; i++)
    {
        x[cert + 0xC + i * 2] = (U8)kTitle[i];
    }
    // The signature-key field: made up, 0xA0..0xAF.
    for (int i = 0; i < 16; i++)
    {
        x[cert + 0xC0 + i] = (U8)(0xA0 + i);
    }

    static const char* const kNames[3] = { ".text", "$$XTIMAGE", "$$XSIMAGE" };
    static const U32 kSizes[3] = { 0x40, 0x300, 0x80 };
    U32 raw = 0x1000;
    U32 nameAt = names;
    for (int i = 0; i < 3; i++)
    {
        U32 sh = sections + (U32)i * 0x38;
        Put::at(x, sh + 4, base + raw);
        Put::at(x, sh + 8, kSizes[i]);
        Put::at(x, sh + 0xC, raw);
        Put::at(x, sh + 0x10, kSizes[i]);
        Put::at(x, sh + 0x14, base + nameAt);
        memcpy(&x[nameAt], kNames[i], strlen(kNames[i]) + 1);
        nameAt += (U32)strlen(kNames[i]) + 1;

        for (U32 k = 0; k < kSizes[i]; k++)
        {
            x.push_back((U8)(k * 7 + i * 31 + 1));
        }
        raw += kSizes[i];
    }
    return x;
}

// bytes 1..32: the EEPROM key's place, then the certificate key's.
static std::vector<U8> makeKeys()
{
    std::vector<U8> k;
    for (U8 i = 1; i <= 32; i++)
    {
        k.push_back(i);
    }
    return k;
}

static void testSignature()
{
    char err[300] = "";
    std::vector<U8> xbe = makeXbe(ISX_TITLE_ID);
    iSaveXboxTitle title;
    check(iSaveXbeParse(&xbe[0], xbe.size(), &title, err, sizeof(err)), "the made-up XBE parses");
    check(title.titleId == ISX_TITLE_ID && strcmp(title.titleName, kTitle) == 0,
          "its certificate gives the title ID and name");
    check(title.titleImage.size() == 0x300 && title.saveImage.size() == 0x80 &&
              memcmp(&title.titleImage[0], &xbe[0x1040], 0x300) == 0 &&
              memcmp(&title.saveImage[0], &xbe[0x1340], 0x80) == 0,
          "its two image sections are found by name");

    std::vector<U8> other = makeXbe(0x4D530004);
    iSaveXboxTitle t2;
    check(!iSaveXbeParse(&other[0], other.size(), &t2, err, sizeof(err)) &&
              strstr(err, "another game") != NULL,
          "another game's XBE is refused, and says so");
    check(!iSaveXbeParse((const U8*)"MZ", 2, &t2, err, sizeof(err)), "a file that is no XBE is refused");
    std::vector<U8> cut(xbe.begin(), xbe.begin() + 0x200);
    check(!iSaveXbeParse(&cut[0], cut.size(), &t2, err, sizeof(err)), "a cut-short XBE is refused");

    std::vector<U8> keys = makeKeys();
    U8 cert[ISX_KEY_SIZE];
    check(iSaveXboxKeysParse(&keys[0], keys.size(), cert, err, sizeof(err)) && cert[0] == 17 &&
              cert[15] == 32,
          "keys.bin's second half is the certificate key");
    check(!iSaveXboxKeysParse(&keys[0], 16, cert, err, sizeof(err)) && strstr(err, "32 bytes") != NULL,
          "a keys.bin of the wrong size is refused, with the size it should be");
    std::vector<U8> blank(32, 0);
    check(!iSaveXboxKeysParse(&blank[0], blank.size(), cert, err, sizeof(err)),
          "a keys.bin with no certificate key is refused");
    iSaveXboxKeysParse(&keys[0], keys.size(), cert, err, sizeof(err));

    // The title key and the signature, against the definition written out.
    const char* message = "TSSM made-up payload";
    const U8* m = (const U8*)message;
    size_t mn = strlen(message);

    for (int real = 0; real < 2; real++)
    {
        U8 zero[ISX_KEY_SIZE];
        memset(zero, 0, sizeof(zero));
        const U8* ck = real ? cert : zero;

        U8 expectKey[ISHA1_SIZE];
        hmacByDefinition(ck, ISX_KEY_SIZE, &xbe[0x180 + 0xC0], ISX_KEY_SIZE, expectKey);
        U8 expectSig[ISHA1_SIZE];
        hmacByDefinition(expectKey, ISX_KEY_SIZE, m, mn, expectSig);

        U8 key[ISX_KEY_SIZE];
        iSaveXboxTitleKey(&title, real ? cert : NULL, key);
        U8 sig[ISX_SIGNATURE_SIZE];
        iSaveXboxSign(key, m, mn, sig);

        check(memcmp(key, expectKey, ISX_KEY_SIZE) == 0,
              real ? "the real title key is HMAC(certificate key, certificate field), 16 bytes" :
                     "the emulator title key is the same with a zero certificate key");
        check(memcmp(sig, expectSig, ISX_SIGNATURE_SIZE) == 0,
              real ? "the real signature is HMAC(title key, payload)" :
                     "the emulator signature is HMAC(title key, payload)");

        // And against Python's hmac and hashlib, for the same made-up inputs.
        check(hex(sig, sizeof(sig)) == (real ? "8689c857881f1319d83d8fd17aa2eacb8088867e" :
                                               "b999a68ffebfffbee4ccb19523e36449bd77bbd8"),
              real ? "the real signature matches another program's" :
                     "the emulator signature matches another program's");

        std::vector<U8> file;
        iSaveXboxFile(key, m, mn, file);
        check(file.size() == mn + 20 && memcmp(&file[0], m, mn) == 0 &&
                  memcmp(&file[mn], expectSig, 20) == 0 && iSaveXboxVerify(key, &file[0], file.size()),
              "GameData.xsv is the payload, then its signature");
        file[3] ^= 1;
        check(!iSaveXboxVerify(key, &file[0], file.size()), "one changed bit fails the signature");
    }
}

// --- a reference encoder -----------------------------------------------------------------
//
// xSerial's rule, written here from its description and not from iSaveLE.cpp:
// the stream is a run of bits; Write_b1/Write_bn add an integer's low bits,
// least significant first; Write(U8/S16/S32/U32/F32) adds the value's bytes in
// the console's memory order, bits 0..7 of each; stream bit k is bit k%32 of
// word k/32, and the words are stored in the console's order.

struct Item
{
    int bytes; // 0: `bits` bits of `value`; else a value of that many bytes
    int bits;
    U32 value;
};

static void addBits(std::vector<Item>& s, int n, U32 v)
{
    Item i = { 0, n, v };
    s.push_back(i);
}

static void addValue(std::vector<Item>& s, int bytes, U32 v)
{
    Item i = { bytes, 0, v };
    s.push_back(i);
}

static std::vector<U8> encode(const std::vector<Item>& stream, bool big, size_t room)
{
    std::vector<U8> bits;
    for (size_t i = 0; i < stream.size(); i++)
    {
        const Item& it = stream[i];
        if (it.bytes == 0)
        {
            for (int b = 0; b < it.bits; b++)
            {
                bits.push_back((U8)((it.value >> b) & 1));
            }
        }
        else
        {
            for (int k = 0; k < it.bytes; k++)
            {
                // Memory order: the most significant byte first on the
                // GameCube, the least on the others.
                int which = big ? (it.bytes - 1 - k) : k;
                U32 byte = (it.value >> (8 * which)) & 0xFF;
                for (int b = 0; b < 8; b++)
                {
                    bits.push_back((U8)((byte >> b) & 1));
                }
            }
        }
    }

    std::vector<U8> out(room, 0);
    for (size_t k = 0; k < bits.size() && k / 8 < room; k++)
    {
        if (bits[k])
        {
            size_t word = k / 32;
            size_t bit = k % 32;
            size_t byte = big ? (word * 4 + 3 - bit / 8) : (word * 4 + bit / 8);
            out[byte] |= (U8)(1 << (bit % 8));
        }
    }
    return out;
}

static U32 streamBits(const std::vector<Item>& stream)
{
    U32 n = 0;
    for (size_t i = 0; i < stream.size(); i++)
    {
        n += stream[i].bytes ? (U32)stream[i].bytes * 8 : (U32)stream[i].bits;
    }
    return n;
}

static U32 sSeed = 12345;

static U32 rnd()
{
    sSeed = sSeed * 1664525u + 1013904223u;
    return sSeed >> 8;
}

static U32 rnd32()
{
    return (rnd() << 16) ^ rnd();
}

// What the made-up level and the made-up MNUC hold (see makeLevel/makeMnuc).
enum
{
    kTaskCounters = 5,
    kTriggers = 3, // two in the HIP, one in the HOP
    kPickups = 4,
    kTimers = 1,
    kCounters = 3, // two of the level's and MNUC's one
    kDispatchers = 1,
    kTaskBoxes = 1,
    kFlythroughs = 1,
    kTweaks = 1,
    kTeleports = 2,
    kRaceTimers = 1
};

static std::vector<Item> playerStream()
{
    std::vector<Item> s;
    addBits(s, 1, 1);
    for (int i = 0; i < 4; i++)
    {
        addValue(s, 4, rnd32());
    }
    addValue(s, 2, rnd32() & 0xFFFF);
    for (int i = 0; i < 40; i++)
    {
        addValue(s, 4, rnd32());
    }
    for (int i = 0; i < 10; i++)
    {
        addValue(s, 1, rnd() & 0xFF);
    }
    return s;
}

static std::vector<Item> counterStream()
{
    std::vector<Item> s;
    addBits(s, 1, 1);
    for (int i = 0; i < kTaskCounters; i++)
    {
        addBits(s, 1, rnd() & 1);
        addValue(s, 1, rnd() & 0xFF);
        addValue(s, 2, rnd32() & 0xFFFF);
    }
    addValue(s, 4, rnd32());
    return s;
}

static std::vector<Item> levelStream()
{
    std::vector<Item> s;
    addBits(s, 1, 1);
    addValue(s, 4, rnd32()); // the two F32 offsets
    addValue(s, 4, rnd32());
    for (int i = 0; i < kTriggers; i++)
    {
        addBits(s, 2, rnd());
    }
    for (int i = 0; i < kPickups; i++)
    {
        addBits(s, 2, rnd());
        addBits(s, 8, rnd());
        addBits(s, 1, rnd());
    }
    for (int i = 0; i < kTimers; i++)
    {
        addBits(s, 1, rnd());
        addValue(s, 1, rnd() & 0xFF);
        addValue(s, 4, rnd32());
    }
    for (int i = 0; i < kCounters; i++)
    {
        addBits(s, 1, rnd());
        addValue(s, 1, rnd() & 0xFF);
        addValue(s, 2, rnd32() & 0xFFFF);
    }
    for (int i = 0; i < kDispatchers; i++)
    {
        addBits(s, 1, rnd());
    }
    for (int i = 0; i < kTaskBoxes; i++)
    {
        addValue(s, 1, rnd() & 0xFF);
    }
    for (int i = 0; i < kFlythroughs + kTweaks; i++)
    {
        addBits(s, 1, rnd());
    }
    for (int i = 0; i < kTeleports; i++)
    {
        addBits(s, 3, rnd());
        addValue(s, 4, rnd32());
    }
    for (int i = 0; i < kRaceTimers; i++)
    {
        addValue(s, 4, rnd32());
    }
    return s;
}

// --- made-up packages ------------------------------------------------------------------------

struct Asset
{
    U32 type;
    std::vector<U8> data;
};

static std::vector<U8> chunk(const char* tag, const std::vector<U8>& body)
{
    std::vector<U8> c(tag, tag + 4);
    put32be(c, (U32)body.size());
    c.insert(c.end(), body.begin(), body.end());
    return c;
}

static void append(std::vector<U8>& to, const std::vector<U8>& from)
{
    to.insert(to.end(), from.begin(), from.end());
}

// An asset that starts with xBaseAsset: id, type, links, flags.
static Asset baseAsset(const char* type, U32 id, U32 flags, bool big)
{
    Asset a;
    a.type = ISF_TAG(type[0], type[1], type[2], type[3]);
    put32(a.data, id, big);
    a.data.push_back(0);
    a.data.push_back(0);
    put16(a.data, flags, big);
    for (int i = 0; i < 24; i++)
    {
        a.data.push_back((U8)(0x40 + i)); // the rest of it: not read
    }
    return a;
}

static Asset dynAsset(const char* name, U32 id, U32 flags, U8 firstByte, bool big)
{
    Asset a;
    a.type = ISF_TAG('D', 'Y', 'N', 'A');
    put32(a.data, id, big);
    a.data.push_back(0);
    a.data.push_back(0);
    put16(a.data, flags, big);
    put32(a.data, iSaveStrHash(name), big);
    put16(a.data, 1, big);
    put16(a.data, 0, big);
    a.data.push_back(firstByte);
    for (int i = 0; i < 15; i++)
    {
        a.data.push_back(0);
    }
    return a;
}

static Asset groupAsset(U32 id, U32 flags, U32 items, bool big)
{
    Asset a;
    a.type = ISF_TAG('G', 'R', 'U', 'P');
    put32(a.data, id, big);
    a.data.push_back(0);
    a.data.push_back(0);
    put16(a.data, flags, big);
    put16(a.data, items, big);
    put16(a.data, 0, big);
    for (U32 i = 0; i < items; i++)
    {
        put32(a.data, 0x1000 + i, big);
    }
    return a;
}

// A package the way the packer lays one out: HIPA, PACK (with PLAT), DICT
// (ATOC of AHDRs, LTOC), STRM (DHDR, DPAK). The container is big-endian on
// every console; the assets are in the console's order.
static std::vector<U8> makePackage(const std::vector<Asset>& assets, bool big)
{
    std::vector<U8> pack;
    append(pack, chunk("PVER", std::vector<U8>(12, 0)));
    append(pack, chunk("PFLG", std::vector<U8>(4, 0)));
    append(pack, chunk("PCNT", std::vector<U8>(20, 0)));
    append(pack, chunk("PCRT", std::vector<U8>(30, 0)));
    append(pack, chunk("PMOD", std::vector<U8>(4, 0)));
    const char plat[] = "GC\0\0US\0\0NTSC\0\0Test\0";
    std::vector<U8> platBody(plat, plat + sizeof(plat) - 1);
    if (!big)
    {
        platBody[0] = 'B';
        platBody[1] = 'X';
    }
    append(pack, chunk("PLAT", platBody));

    std::vector<U8> out;
    for (int pass = 0; pass < 2; pass++)
    {
        // The second pass knows where the assets start.
        U32 dataAt = pass ? (U32)out.size() + 8 + 12 + 8 : 0;
        out.clear();
        append(out, chunk("HIPA", std::vector<U8>()));
        append(out, chunk("PACK", pack));

        std::vector<U8> atoc;
        append(atoc, chunk("AINF", std::vector<U8>(4, 0)));
        U32 offset = dataAt;
        for (size_t i = 0; i < assets.size(); i++)
        {
            std::vector<U8> ahdr;
            put32be(ahdr, 0x5000 + (U32)i);
            put32be(ahdr, assets[i].type);
            put32be(ahdr, offset);
            put32be(ahdr, (U32)assets[i].data.size());
            put32be(ahdr, 0);
            put32be(ahdr, 2);
            std::vector<U8> adbg(4, 0);
            const char* name = "asset\0\0\0file\0\0\0\0";
            adbg.insert(adbg.end(), name, name + 16);
            adbg.insert(adbg.end(), 4, 0);
            append(ahdr, chunk("ADBG", adbg));
            append(atoc, chunk("AHDR", ahdr));
            offset += (U32)assets[i].data.size();
        }

        std::vector<U8> dict;
        append(dict, chunk("ATOC", atoc));
        append(dict, chunk("LTOC", chunk("LINF", std::vector<U8>(4, 0))));
        append(out, chunk("DICT", dict));
    }

    std::vector<U8> dpak;
    for (size_t i = 0; i < assets.size(); i++)
    {
        append(dpak, assets[i].data);
    }
    std::vector<U8> strm;
    append(strm, chunk("DHDR", std::vector<U8>(4, 0xFF)));
    append(strm, chunk("DPAK", dpak));
    append(out, chunk("STRM", strm));
    return out;
}

// The level TS01: what it saves is kTriggers.. above, among things it does not.
static std::vector<U8> makeLevel(bool big)
{
    std::vector<Asset> a;
    Asset model;
    model.type = ISF_TAG('M', 'O', 'D', 'L');
    model.data.assign(5, 0xEE); // shorter than a header: not an object
    a.push_back(model);

    a.push_back(baseAsset("TRIG", 1, 0x1F, big)); // saved
    a.push_back(baseAsset("TRIG", 2, 0x1D, big));
    a.push_back(baseAsset("TRIG", 3, 0x02, big)); // saved
    for (U32 i = 0; i < 5; i++)
    {
        a.push_back(baseAsset("PKUP", 10 + i, i == 2 ? 0x0D : 0x0F, big)); // four saved
    }
    a.push_back(baseAsset("TIMR", 20, 0x1F, big)); // saved
    a.push_back(baseAsset("TIMR", 21, 0x1D, big));
    a.push_back(baseAsset("CNTR", 30, 0x1F, big)); // saved
    a.push_back(baseAsset("CNTR", 31, 0x1F, big)); // saved
    a.push_back(baseAsset("DPAT", 40, 0x1F, big)); // saved
    a.push_back(baseAsset("DPAT", 41, 0x1D, big));
    a.push_back(baseAsset("SNDI", 50, 0x1F, big)); // not a kind that saves
    a.push_back(baseAsset("PARE", 51, 0x1F, big));
    a.push_back(groupAsset(60, 0x1D, 3, big));

    a.push_back(dynAsset("game_object:task_box", 70, 0x1D, 1, big)); // persistent: saved
    a.push_back(dynAsset("game_object:task_box", 71, 0x1D, 0, big));
    a.push_back(dynAsset("game_object:Flythrough", 72, 0x1D, 0, big)); // always saved
    a.push_back(dynAsset("game_object:Camera_Tweak", 73, 0x1F, 0, big)); // flagged: saved
    a.push_back(dynAsset("game_object:Camera_Tweak", 74, 0x1D, 0, big));
    a.push_back(dynAsset("game_object:Teleport", 75, 0x1D, 0, big)); // always saved
    a.push_back(dynAsset("game_object:Teleport", 76, 0x1D, 0, big));
    a.push_back(dynAsset("game_object:RaceTimer", 77, 0x1D, 0, big)); // always saved
    a.push_back(dynAsset("effect:spotlight", 78, 0x1F, 1, big)); // writes nothing
    return makePackage(a, big);
}

static std::vector<U8> makeLevelHop(bool big)
{
    std::vector<Asset> a;
    a.push_back(baseAsset("TRIG", 100, 0x1F, big)); // saved: the HOP's are in the list too
    a.push_back(baseAsset("TRIG", 101, 0x1D, big));
    return makePackage(a, big);
}

static std::vector<U8> makeMnuc(bool big)
{
    std::vector<Asset> a;
    a.push_back(groupAsset(0x77, 0x1D, 9, big));
    a.push_back(groupAsset(iSaveStrHash("TASK COUNTER GROUP"), 0x1D, kTaskCounters, big));
    a.push_back(baseAsset("CNTR", 200, 0x1F, big)); // saved, and in every level's list
    for (U32 i = 0; i < kTaskCounters; i++)
    {
        a.push_back(baseAsset("CNTR", 0x1000 + i, 0x1D, big));
    }
    return makePackage(a, big);
}

static void makeAssets(const std::string& root, bool big)
{
    removeTree(root);
    writeFile(root + "/MN/mnuc.HIP", makeMnuc(big));
    writeFile(root + "/TS/ts01.HIP", makeLevel(big));
    writeFile(root + "/TS/ts01.HOP", makeLevelHop(big));
}

// --- a made-up save ---------------------------------------------------------------------------

static void putWord(std::vector<U8>& p, U32 v, bool big)
{
    put32(p, v, big);
}

static void block(std::vector<U8>& p, const char* tag, U32 room, U32 used,
                  const std::vector<U8>& data, bool big)
{
    putWord(p, ISF_TAG(tag[0], tag[1], tag[2], tag[3]), big);
    putWord(p, room, big);
    putWord(p, used, big);
    size_t start = p.size();
    p.insert(p.end(), data.begin(), data.end());
    p.resize(start + room, 0xBF);
}

// The whole payload as one console writes it, from the same values.
static std::vector<U8> makePayload(bool big, const std::vector<Item>& player,
                                   const std::vector<Item>& counters,
                                   const std::vector<Item>& level, bool visitedOther)
{
    std::vector<U8> p;
    putWord(p, ISF_TAG('G', 'D', 'A', 'T'), big);
    putWord(p, 1, big);
    putWord(p, 0, big);
    putWord(p, 0, big);

    // The leader: label, S32 progress at 0x40, S64 play time at 0x48, a byte
    // at 0x50; then the 22 characters after it.
    std::vector<U8> leader(0x58, 0);
    memcpy(&leader[0], "Test Level", 10);
    std::vector<U8> word;
    put32(word, 42, big);
    memcpy(&leader[0x40], &word[0], 4);
    const U64 time = 0x0102030405060708ull;
    for (int i = 0; i < 8; i++)
    {
        leader[0x48 + i] = (U8)(time >> (big ? 56 - 8 * i : 8 * i));
    }
    leader[0x50] = 3;
    const char* fun = "--TakeMeToYourLeader--";
    leader.insert(leader.end(), fun, fun + 22);
    block(p, "LEDR", 0x100, 0x6E, leader, big);

    block(p, "ROOM", 12, 4, bytesOf("TS01"), big);

    std::vector<U8> pref;
    put32(pref, 1, big);
    put32(pref, 0x3F400000, big); // 0.75f
    put32(pref, 0x3F000000, big); // 0.5f
    put32(pref, 1, big);
    put32(pref, 0, big);
    block(p, "PREF", 40, 20, pref, big);

    std::vector<U8> svid;
    put32(svid, 1, big);
    block(p, "SVID", 4, 4, svid, big);

    // The serializer's clients, in tag order.
    block(p, "CNTR", 400, 400, encode(counters, big, 400), big);
    block(p, "PLYR", 0x148, 0x148, encode(player, big, 0x148), big);
    block(p, "TS01", 400, 400, encode(level, big, 400), big);
    if (visitedOther)
    {
        // A level entered and not left yet: nothing written, all zeros. Its
        // package is not among the made-up files, and none is needed.
        block(p, "ZZ09", 400, 400, std::vector<U8>(), big);
        memset(&p[p.size() - 400], 0, 400);
    }

    std::vector<U8> fill = bytesOf("RyanNeil");
    block(p, "SFIL", 64, 64, fill, big);

    std::vector<U8> size;
    put32(size, (U32)p.size(), big);
    memcpy(&p[8], &size[0], 4);
    std::vector<U8> crc;
    put32(crc, iSaveCRC(0xFFFFFFFFu, &p[16], p.size() - 16), big);
    memcpy(&p[12], &crc[0], 4);
    return p;
}

static bool noMaps(void*, U32, iSaveFieldMap*, char*, size_t)
{
    return false;
}

static void testConversion(std::vector<U8>& canonicalOut)
{
    char err[400] = "";

    // The reports' example: one bit, then the S32 0x11223344.
    {
        std::vector<Item> s;
        addBits(s, 1, 1);
        addValue(s, 4, 0x11223344);
        std::vector<U8> gc = encode(s, true, 8);
        std::vector<U8> le = encode(s, false, 8);
        check(gc[0] == 0x88 && gc[1] == 0x66 && gc[2] == 0x44 && gc[3] == 0x23,
              "the reference encoder writes the GameCube's 88 66 44 23");
        check(le[0] == 0x89 && le[1] == 0x66 && le[2] == 0x44 && le[3] == 0x22,
              "and the Xbox's 89 66 44 22");

        // The GameCube's bytes in little-endian form, then the value turned.
        std::vector<U8> work(8);
        for (int i = 0; i < 8; i++)
        {
            work[i] = gc[(i / 4) * 4 + 3 - i % 4];
        }
        iSaveFieldMap map;
        iSaveStreamField f = { 1, 4 };
        map.fields.push_back(f);
        map.bits = 33;
        check(iSaveStreamReverse(&work[0], work.size(), &map) && work == le,
              "turning the value's bytes in the stream gives the Xbox's bytes");
        check(iSaveStreamLastBit(&work[0], work.size()) == 29, "the last set bit is found");
        map.fields[0].bit = 40;
        check(!iSaveStreamReverse(&work[0], work.size(), &map),
              "a value that does not fit the block is refused");
    }

    // The fixed maps.
    iSaveFieldMap map;
    std::vector<Item> player = playerStream();
    iSaveFieldsPlayer(&map);
    check(map.bits == 1505 && map.fields.size() == 45 && map.bits == streamBits(player),
          "PLYR is 1505 bits with 45 values to turn");
    iSaveFieldsCounters(114, &map);
    check(map.bits == 1 + 114 * 25 + 32 && map.fields.size() == 115,
          "CNTR with the discs' 114 task counters is 2883 bits");

    for (int big = 0; big < 2; big++)
    {
        std::string root = at(big ? "assets-gc" : "assets-xbox");
        makeAssets(root, big != 0);
        iSaveStoreSetAssetRoot(root.c_str());

        // What the made-up packages say, read in each console's byte order.
        iSaveObjectCounts c;
        iSaveObjectCountsClear(&c);
        std::vector<U8> level = makeLevel(big != 0);
        std::vector<U8> hop = makeLevelHop(big != 0);
        std::vector<U8> mnuc = makeMnuc(big != 0);
        bool scanned = iSaveHipScanData(&level[0], level.size(), &c, err, sizeof(err)) &&
                       iSaveHipScanData(&hop[0], hop.size(), &c, err, sizeof(err)) &&
                       iSaveHipScanData(&mnuc[0], mnuc.size(), &c, err, sizeof(err));
        check(scanned && c.n[ISLE_TRIGGER] == kTriggers && c.n[ISLE_PICKUP] == kPickups &&
                  c.n[ISLE_TIMER] == kTimers && c.n[ISLE_COUNTER] == kCounters &&
                  c.n[ISLE_DISPATCHER] == kDispatchers && c.n[ISLE_TASKBOX] == kTaskBoxes &&
                  c.n[ISLE_FLYTHROUGH] == kFlythroughs && c.n[ISLE_CAMERATWEAK] == kTweaks &&
                  c.n[ISLE_TELEPORT] == kTeleports && c.n[ISLE_RACETIMER] == kRaceTimers &&
                  c.n[ISLE_GROUP] == 0 && c.taskCounters == kTaskCounters,
              big ? "a GameCube package's saved objects are counted (big-endian assets)" :
                    "an Xbox package's saved objects are counted (little-endian assets)");

        std::vector<Item> lvl = levelStream();
        iSaveFieldsScene(&c, &map);
        check(map.bits == streamBits(lvl), "the level's map is as long as what the level writes");

        std::vector<U8> cut(level.begin(), level.begin() + 200);
        iSaveObjectCounts c2;
        iSaveObjectCountsClear(&c2);
        check(!iSaveHipScanData(&cut[0], cut.size(), &c2, err, sizeof(err)) &&
                  !iSaveHipScanData((const U8*)"nonsense, not a package", 23, &c2, err, sizeof(err)),
              "a cut-short package and a file that is none are refused");

        // The same values written as each console writes them...
        std::vector<Item> counters = counterStream();
        std::vector<U8> gc = makePayload(true, player, counters, lvl, true);
        std::vector<U8> le = makePayload(false, player, counters, lvl, true);
        check(iSavePayloadInspect(&gc[0], gc.size(), NULL, err, sizeof(err)),
              "the made-up GameCube save is one the game loads");
        check(iSavePayloadIsLE(&le[0], le.size()) && !iSavePayloadIsLE(&gc[0], gc.size()) &&
                  iSavePayloadLESize(&le[0]) == le.size() && memcmp(&le[0], "TADG", 4) == 0,
              "the made-up Xbox save starts TADG");

        // ...convert into each other exactly.
        std::vector<U8> toLE;
        std::vector<U8> back;
        bool ok = iSaveStoreToLE(&gc[0], gc.size(), toLE, NULL, err, sizeof(err));
        if (!ok)
        {
            printf("  (%s)\n", err);
        }
        check(ok && toLE == le,
              big ? "GameCube -> Xbox is the Xbox's bytes exactly (GameCube packages)" :
                    "GameCube -> Xbox is the Xbox's bytes exactly (Xbox packages)");
        ok = iSaveStoreFromLE(&le[0], le.size(), back, NULL, err, sizeof(err));
        check(ok && back == gc, "Xbox -> GameCube is the GameCube's bytes exactly");

        // A plain word swap would not have been.
        std::vector<U8> swapped(gc.size());
        iSaveHostLayout same = { ISF_LEADER_SIZE, 0x40, 0x48, 0x50 };
        (void)same;
        bool differs = false;
        for (size_t i = 0; i + 4 <= gc.size(); i += 4)
        {
            for (int k = 0; k < 4; k++)
            {
                swapped[i + k] = gc[i + 3 - k];
            }
        }
        {
            // Compare the level's block only: its header is 12 bytes before.
            size_t at = 0;
            for (size_t i = 16; i + 12 <= le.size(); i++)
            {
                if (memcmp(&le[i], "10ST", 4) == 0)
                {
                    at = i + 12;
                }
            }
            differs = at != 0 && memcmp(&swapped[at], &le[at], 400) != 0;
        }
        check(differs, "and a plain word swap of the level's block is not the Xbox's bytes");

        // Padding a card file leaves after the payload is not part of it.
        std::vector<U8> padded = gc;
        padded.resize(gc.size() + 100, 0);
        ok = iSaveStoreToLE(&padded[0], padded.size(), toLE, NULL, err, sizeof(err));
        check(ok && toLE == le, "what follows the payload is left out");

        // A save whose CRC is wrong keeps a wrong CRC.
        std::vector<U8> bad = le;
        bad[0x20] ^= 0x40; // a letter of the level's name
        ok = iSaveStoreFromLE(&bad[0], bad.size(), back, NULL, err, sizeof(err));
        check(ok && !iSavePayloadInspect(&back[0], back.size(), NULL, err, sizeof(err)) &&
                  strstr(err, "checksum") != NULL,
              "a save failing its checksum still fails it after conversion");

        // Refusals: cut short, a level whose package is missing, a block
        // holding more than its level saves, nothing to say what a block is.
        check(!iSaveStoreFromLE(&le[0], le.size() - 1, back, NULL, err, sizeof(err)),
              "a cut-short save is refused");

        std::vector<U8> gcOther = gc;
        {
            size_t at = 0;
            for (size_t i = 16; i + 12 <= gcOther.size(); i++)
            {
                if (memcmp(&gcOther[i], "ZZ09", 4) == 0)
                {
                    at = i + 12;
                }
            }
            gcOther[at + 3] = 1; // the level was left: its first bit is set
            iSavePutU32(&gcOther[12], iSaveCRC(0xFFFFFFFFu, &gcOther[16], gcOther.size() - 16));
        }
        check(!iSaveStoreToLE(&gcOther[0], gcOther.size(), toLE, NULL, err, sizeof(err)) &&
                  strstr(err, "ZZ/ZZ09.HIP") != NULL && strstr(err, root.c_str()) != NULL,
              "a level whose package is missing is refused, naming the file and the folder");

        std::vector<U8> gcLong = gc;
        {
            size_t at = 0;
            for (size_t i = 16; i + 12 <= gcLong.size(); i++)
            {
                if (memcmp(&gcLong[i], "TS01", 4) == 0)
                {
                    at = i + 12;
                }
            }
            gcLong[at + 396] = 0x80; // a bit far past what TS01 saves
        }
        {
            // Converted all the same: what the map knows is turned, the rest
            // kept, a note says so, and it comes back exactly.
            std::string notes;
            std::vector<U8> longLE;
            std::vector<U8> longBack;
            bool longOK = iSaveStoreToLE(&gcLong[0], gcLong.size(), longLE, &notes, err,
                                         sizeof(err)) &&
                          iSaveStoreFromLE(&longLE[0], longLE.size(), longBack, NULL, err,
                                           sizeof(err));
            check(longOK && notes.find("TS01 holds") != std::string::npos &&
                      notes.find("more bits") != std::string::npos,
                  "a block holding more than its level saves is converted, with a note");
            check(longOK && longBack.size() == gcLong.size() &&
                      memcmp(&longBack[16], &gcLong[16], gcLong.size() - 16) == 0,
                  "and comes back bit for bit");
        }

        check(!iSavePayloadLE(ISLE_TO_LE, &gc[0], gc.size(), toLE, noMaps, NULL, NULL, err, sizeof(err)),
              "with nothing to say what a block holds, nothing is converted");

        // A save that has only all-zero blocks needs no package at all.
        std::vector<Item> none;
        std::vector<U8> emptyGC = makePayload(true, none, none, none, true);
        std::vector<U8> emptyLE = makePayload(false, none, none, none, true);
        check(iSavePayloadLE(ISLE_TO_LE, &emptyGC[0], emptyGC.size(), toLE, noMaps, NULL, NULL, err,
                             sizeof(err)) &&
                  toLE == emptyLE,
              "blocks that are all zeros convert without any package");

        if (big)
        {
            canonicalOut = gc;
        }
    }
}

// --- the store ---------------------------------------------------------------------------------

static bool sameFile(const std::string& path, const std::vector<U8>& expect)
{
    std::vector<U8> got;
    return readFile(path, got) && got == expect;
}

static void expectNoKeys(const char* text, const std::vector<U8>& keys, const char* what)
{
    // No message may hold key bytes, as hex in any case.
    std::string t = text;
    for (size_t i = 0; i < t.size(); i++)
    {
        if (t[i] >= 'A' && t[i] <= 'F')
        {
            t[i] = (char)(t[i] - 'A' + 'a');
        }
    }
    check(t.find(hex(&keys[16], 4)) == std::string::npos &&
              t.find(hex(&keys[0], 4)) == std::string::npos,
          what);
}

static void testStore(const std::vector<U8>& canonical)
{
    char err[768] = "";
    std::string root = at("saves");
    removeTree(root);
    iSaveStoreSetRoot(root.c_str());
    iSaveStoreSetAssetRoot(at("assets-gc").c_str());

    std::vector<U8> xbe = makeXbe(ISX_TITLE_ID);
    std::vector<U8> keys = makeKeys();
    std::string xbePath = at("xbox/default.xbe");
    std::string keysPath = at("xbox/keys.bin");
    removeTree(at("xbox"));
    writeFile(xbePath, xbe);
    writeFile(keysPath, keys);
    sConfigXbe = "";
    sConfigKeys = "";

    check(iSaveStoreWrite(0, 1, &canonical[0], canonical.size(), err, sizeof(err)),
          "the made-up save goes into slot A2");
    char slotPath[512];
    iSaveStoreSlotPath(0, 1, slotPath, sizeof(slotPath));
    const S64 when = 1100000000; // 2004-11-09
    iHostSetFileTime(slotPath, when);

    std::vector<U8> le;
    iSaveStoreToLE(&canonical[0], canonical.size(), le, NULL, err, sizeof(err));

    // What is missing is said plainly.
    iSaveXboxExportOptions o;
    iSaveXboxExportDefaults(&o);
    std::string where;
    std::string out = at("export-emu");
    removeTree(out);
    check(!iSaveStoreExportXbox(0, 1, &o, out.c_str(), &where, err, sizeof(err)) &&
              strstr(err, "DEFAULT.XBE") != NULL && strstr(err, "your own") != NULL,
          "with no XBE the export says it needs DEFAULT.XBE from the player's own game");

    snprintf(o.xbe, sizeof(o.xbe), "%s", xbePath.c_str());
    o.signature = ISS_XBOX_REAL;
    check(!iSaveStoreExportXbox(0, 1, &o, out.c_str(), &where, err, sizeof(err)) &&
              strstr(err, "keys.bin") != NULL && strstr(err, "your own Xbox") != NULL,
          "a real signature with no keys file says it needs the player's keys.bin");

    std::string shortKeys = at("xbox/short.bin");
    writeFile(shortKeys, std::vector<U8>(keys.begin(), keys.begin() + 16));
    snprintf(o.keys, sizeof(o.keys), "%s", shortKeys.c_str());
    check(!iSaveStoreExportXbox(0, 1, &o, out.c_str(), &where, err, sizeof(err)) &&
              strstr(err, "32 bytes") != NULL,
          "a keys file of the wrong size is refused");
    expectNoKeys(err, keys, "and the message holds no key bytes");
    check(!iHostPathExists(out.c_str()), "a refused export writes nothing");

    // The emulator signature: the XBE alone.
    iSaveXboxExportDefaults(&o);
    snprintf(o.xbe, sizeof(o.xbe), "%s", xbePath.c_str());
    bool ok = iSaveStoreExportXbox(0, 1, &o, out.c_str(), &where, err, sizeof(err));
    if (!ok)
    {
        printf("  (%s)\n", err);
    }
    check(ok && where == out + "/UDATA/54510027/7B317197822E",
          "slot A2 exports to UDATA/54510027/7B317197822E under the folder chosen");

    std::string title = out + "/UDATA/54510027";
    std::vector<U8> meta;
    iSaveXboxMeta("TitleName", kTitle, meta);
    check(sameFile(title + "/TitleMeta.xbx", meta) && meta.size() == 88,
          "TitleMeta.xbx is the title's name from the XBE, 88 bytes");
    check(sameFile(title + "/TitleImage.xbx",
                   std::vector<U8>(xbe.begin() + 0x1040, xbe.begin() + 0x1340)) &&
              sameFile(title + "/SaveImage.xbx",
                       std::vector<U8>(xbe.begin() + 0x1340, xbe.begin() + 0x13C0)),
          "TitleImage.xbx and SaveImage.xbx are the XBE's two image sections");
    iSaveXboxMeta("Name", "SpongeBobMovie02", meta);
    check(sameFile(where + "/SaveMeta.xbx", meta) && meta.size() == 48,
          "SaveMeta.xbx names the save, 48 bytes");

    std::vector<U8> xsv;
    readFile(where + "/GameData.xsv", xsv);
    check(xsv.size() == le.size() + 20 && memcmp(&xsv[0], &le[0], le.size()) == 0,
          "GameData.xsv is the little-endian payload and 20 bytes more");

    U8 zero[16];
    memset(zero, 0, sizeof(zero));
    U8 tk[ISHA1_SIZE];
    U8 sig[ISHA1_SIZE];
    hmacByDefinition(zero, 16, &xbe[0x180 + 0xC0], 16, tk);
    hmacByDefinition(tk, 16, &le[0], le.size(), sig);
    check(xsv.size() == le.size() + 20 && memcmp(&xsv[le.size()], sig, 20) == 0,
          "signed as an emulator with no keys file signs it");

    iHostFileInfo st;
    check(iHostStat((where + "/GameData.xsv").c_str(), &st) && st.mtime >= when - 2 &&
              st.mtime <= when + 2,
          "GameData.xsv carries the save's date");

    {
        std::vector<std::string> names;
        iHostDir* d = iHostDirOpen(where.c_str());
        for (const char* n = d ? iHostDirNext(d) : NULL; n != NULL; n = iHostDirNext(d))
        {
            names.push_back(n);
        }
        if (d != NULL)
        {
            iHostDirClose(d);
        }
        check(names.size() == 2, "the save's folder holds its two files and nothing else");
    }

    check(!iSaveStoreExportXbox(0, 1, &o, out.c_str(), &where, err, sizeof(err)) &&
              strstr(err, "already there") != NULL,
          "a save already there is not written over unasked");
    o.replace = true;
    check(iSaveStoreExportXbox(0, 1, &o, out.c_str(), &where, err, sizeof(err)),
          "and is when asked");
    o.replace = false;
    o.number = 0;
    check(iSaveStoreExportXbox(0, 1, &o, out.c_str(), &where, err, sizeof(err)) &&
              where == title + "/7B317197822D",
          "the same save exports as the Xbox's save 1 when asked");
    o.number = -1;

    // The real signature: the XBE and the player's keys.bin, by setting.
    std::string outReal = at("export-real/EmuData");
    removeTree(at("export-real"));
    writeFile(outReal + "/EmuDisk/Partition1/readme.txt", bytesOf("an emulator's data folder"));
    iSaveXboxExportDefaults(&o);
    o.signature = ISS_XBOX_REAL;
    sConfigXbe = at("xbox"); // a folder holding DEFAULT.XBE will do
    sConfigKeys = keysPath;
    ok = iSaveStoreExportXbox(0, 1, &o, outReal.c_str(), &where, err, sizeof(err));
    if (!ok)
    {
        printf("  (%s)\n", err);
    }
    check(ok && where == outReal + "/EmuDisk/Partition1/UDATA/54510027/7B317197822E",
          "an emulator's data folder gets the save in EmuDisk/Partition1/UDATA");
    std::vector<U8> xsvReal;
    readFile(where + "/GameData.xsv", xsvReal);
    hmacByDefinition(&keys[16], 16, &xbe[0x180 + 0xC0], 16, tk);
    hmacByDefinition(tk, 16, &le[0], le.size(), sig);
    check(xsvReal.size() == le.size() + 20 && memcmp(&xsvReal[le.size()], sig, 20) == 0 &&
              memcmp(&xsvReal[le.size()], &xsv[le.size()], 20) != 0,
          "the real signature is made with the keys file's certificate key, and differs");

    char found[600];
    check(iSaveStoreXboxProgram(NULL, found, sizeof(found), err, sizeof(err)) &&
              iHostStrCaseCmp(found, xbePath.c_str()) == 0 &&
              iSaveStoreXboxKeys(NULL, found, sizeof(found), err, sizeof(err)) &&
              strcmp(found, keysPath.c_str()) == 0,
          "the settings' two paths are where the files are looked for");

    // Destinations named UDATA and 54510027 are used as what they are.
    std::string outU = at("export-u/UDATA");
    removeTree(at("export-u"));
    iHostMakeDir(at("export-u").c_str());
    iHostMakeDir(outU.c_str());
    check(iSaveStoreExportXbox(0, 1, &o, outU.c_str(), &where, err, sizeof(err)) &&
              where == outU + "/54510027/7B317197822E",
          "a folder named UDATA gets 54510027 inside it");
    std::string outT = outU + "/54510027";
    o.number = 2;
    check(iSaveStoreExportXbox(0, 1, &o, outT.c_str(), &where, err, sizeof(err)) &&
              where == outT + "/7B317197822F",
          "a folder named 54510027 is the game's folder");
    o.number = -1;

    // Import: every shape of folder, any signature.
    struct Shape
    {
        std::string path;
        const char* what;
    };
    Shape shapes[] = {
        { at("export-emu/UDATA/54510027/7B317197822E"), "the save's own folder" },
        { at("export-emu/UDATA/54510027/7B317197822E/GameData.xsv"), "GameData.xsv itself" },
        { outReal, "an emulator's data folder" },
    };
    for (size_t i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i++)
    {
        iSaveImportOptions imp;
        iSaveImportDefaults(&imp);
        imp.target = 1; // folder B
        imp.force = true;
        std::vector<iSaveImportItem> items;
        ok = iSaveStoreImport(shapes[i].path.c_str(), &imp, items, err, sizeof(err));
        std::vector<U8> got;
        bool same = ok && items.size() == 1 && items[0].game == 1 &&
                    iSaveStoreRead(1, 1, got, err, sizeof(err)) && got == canonical;
        if (!same)
        {
            printf("  (%s; %s)\n", err, items.empty() ? "" : items[0].message.c_str());
        }
        char what[160];
        snprintf(what, sizeof(what), "%s imports to the slot its name says, byte for byte",
                 shapes[i].what);
        check(same, what);

        if (i == 0)
        {
            check(items.size() == 1 &&
                      items[0].source.find("emulator signature") != std::string::npos &&
                      items[0].source.find("SpongeBobMovie02") != std::string::npos,
                  "and is reported as an Xbox save with an emulator signature");
            iSaveSlotInfo info;
            iSaveStoreSlot(1, 1, &info);
            check(info.mtime >= when - 2 && info.mtime <= when + 2 && !info.fromCard,
                  "and keeps the date of its file");
        }
        if (i == 2)
        {
            check(items.size() == 1 &&
                      items[0].source.find("real signature (matches your keys.bin)") !=
                          std::string::npos,
                  "the real one is reported as matching the player's keys file");
            expectNoKeys(items[0].source.c_str(), keys, "and the report holds no key bytes");
        }
        iSaveStoreDelete(1, 1, err, sizeof(err));
    }

    // The game's folder with two saves in it, and the choice among them.
    {
        iSaveImportOptions imp;
        iSaveImportDefaults(&imp);
        imp.dryRun = true;
        imp.force = true;
        std::vector<iSaveImportItem> items;
        ok = iSaveStoreImport(title.c_str(), &imp, items, err, sizeof(err));
        check(ok && items.size() == 2 && items[0].game != items[1].game,
              "the game's folder lists both of its saves, each for its own slot");

        imp.dryRun = false;
        imp.pick = 2;
        imp.target = 2;
        imp.game = 2;
        ok = iSaveStoreImport(title.c_str(), &imp, items, err, sizeof(err));
        std::vector<U8> got;
        check(ok && items.size() == 1 && iSaveStoreRead(2, 2, got, err, sizeof(err)) &&
                  got == canonical,
              "one of them is picked and sent to any slot");
        iSaveStoreDelete(2, 2, err, sizeof(err));
    }

    // A signature nobody made, and none at all: still read.
    {
        std::vector<U8> forged = xsv;
        for (size_t i = 0; i < 20; i++)
        {
            forged[le.size() + i] = (U8)(0x55 + i);
        }
        std::string dir = at("forged/7B317197822D");
        removeTree(at("forged"));
        writeFile(dir + "/GameData.xsv", forged);

        iSaveImportOptions imp;
        iSaveImportDefaults(&imp);
        imp.target = 3;
        std::vector<iSaveImportItem> items;
        ok = iSaveStoreImport(at("forged").c_str(), &imp, items, err, sizeof(err));
        std::vector<U8> got;
        check(ok && items.size() == 1 && items[0].game == 0 &&
                  items[0].source.find("signature not recognised") != std::string::npos &&
                  iSaveStoreRead(3, 0, got, err, sizeof(err)) && got == canonical,
              "a save with an unknown signature imports all the same, by its folder's name");
        iSaveStoreDelete(3, 0, err, sizeof(err));

        // Without the XBE there is nothing to check it with, and nothing asked.
        sConfigXbe = "";
        ok = iSaveStoreImport(at("forged").c_str(), &imp, items, err, sizeof(err));
        check(ok && items.size() == 1 &&
                  items[0].source.find("signature not checked") != std::string::npos,
              "with no XBE at hand the signature is simply not checked");
        iSaveStoreDelete(3, 0, err, sizeof(err));
        sConfigXbe = at("xbox");

        // The payload alone, under the game's file name (what a PlayStation 2
        // card holds): the same save.
        std::string bare = at("forged/SpongeBobMovie03");
        writeFile(bare, le);
        imp.target = 3;
        ok = iSaveStoreImport(bare.c_str(), &imp, items, err, sizeof(err));
        check(ok && items.size() == 1 && items[0].game == 2 &&
                  iSaveStoreRead(3, 2, got, err, sizeof(err)) && got == canonical,
              "a bare little-endian payload imports by its file name");
        iSaveStoreDelete(3, 2, err, sizeof(err));
    }

    // A zip of the folders, as a file picker's bytes.
    {
        std::vector<U8> zip;
        check(iSaveStoreReadFolder(at("export-emu").c_str(), zip, err, sizeof(err)) &&
                  iSaveZipIs(&zip[0], zip.size()),
              "a folder's saves are gathered into one bundle");

        iSaveImportOptions imp;
        iSaveImportDefaults(&imp);
        imp.target = 4;
        std::vector<iSaveImportItem> items;
        ok = iSaveStoreImportData(&zip[0], zip.size(), "saves.zip", &imp, items, err, sizeof(err));
        std::vector<U8> a;
        std::vector<U8> b;
        check(ok && items.size() == 2 && iSaveStoreRead(4, 0, a, err, sizeof(err)) &&
                  iSaveStoreRead(4, 1, b, err, sizeof(err)) && a == canonical && b == canonical,
              "and a zip of them imports each to its slot");
        iSaveStoreDelete(4, 0, err, sizeof(err));
        iSaveStoreDelete(4, 1, err, sizeof(err));

        check(!iSaveStoreReadFolder(at("xbox").c_str(), zip, err, sizeof(err)) &&
                  strstr(err, "GameData.xsv") != NULL,
              "a folder with no save in it says what a save looks like");
    }

    // The round trip the other way: export what was imported, same bytes.
    {
        iSaveImportOptions imp;
        iSaveImportDefaults(&imp);
        imp.target = 5;
        std::vector<iSaveImportItem> items;
        iSaveStoreImport(at("export-emu/UDATA/54510027/7B317197822E").c_str(), &imp, items, err,
                         sizeof(err));
        iSaveXboxExportDefaults(&o);
        std::string again = at("export-again");
        removeTree(again);
        ok = iSaveStoreExportXbox(5, 1, &o, again.c_str(), &where, err, sizeof(err));
        check(ok && sameFile(where + "/GameData.xsv", xsv) &&
                  sameFile(where + "/SaveMeta.xbx", meta),
              "export, import, export gives the same Xbox files");

        std::vector<iSaveExportItem> eitems;
        std::string all = at("export-all");
        removeTree(all);
        S32 n = iSaveStoreExportXboxFolder(5, &o, all.c_str(), eitems);
        check(n == 1 && eitems.size() == 1 && eitems[0].ok &&
                  strcmp(eitems[0].name, "F2") == 0,
              "a folder's saves export together");
        iSaveStoreDelete(5, 1, err, sizeof(err));
    }

    // The GameCube side is as it was: the same slot still exports as a .gci
    // holding the canonical payload.
    {
        std::vector<U8> gci;
        check(iSaveStoreExportGCIData(0, 1, NULL, gci, err, sizeof(err)) &&
                  gci.size() > ISF_DENTRY_SIZE + ISF_HEAD_SIZE + canonical.size() &&
                  memcmp(&gci[ISF_DENTRY_SIZE + ISF_HEAD_SIZE], &canonical[0], canonical.size()) == 0,
              "the slot still exports as a .gci with the canonical payload");
    }
}

// --- the command line ----------------------------------------------------------------------

static S32 cli(std::vector<std::string> args)
{
    std::vector<char*> argv;
    static char name[] = "tssm";
    argv.push_back(name);
    for (size_t i = 0; i < args.size(); i++)
    {
        argv.push_back(&args[i][0]);
    }
    return iSaveCLIRunArgs((S32)argv.size(), &argv[0]);
}

static void testCLI(const std::vector<U8>& canonical)
{
    char err[512] = "";
    std::string root = at("saves-cli");
    removeTree(root);
    iSaveStoreSetRoot(root.c_str());
    iSaveStoreWrite(0, 0, &canonical[0], canonical.size(), err, sizeof(err));
    sConfigXbe = "";
    sConfigKeys = "";

    std::string out = at("cli-out");
    removeTree(out);
    std::string xbe = at("xbox/default.xbe");
    std::string keys = at("xbox/keys.bin");

    const char* a0[] = { "--save-dir", root.c_str(), "--export-save", "A1", out.c_str(), "--xbox",
                         "emu" };
    check(cli(std::vector<std::string>(a0, a0 + 7)) == 1,
          "--export-save --xbox emu with no XBE fails");

    const char* a1[] = { "--save-dir", root.c_str(), "--export-save", "A1", out.c_str(), "--xbox",
                         "emu", "--xbe", xbe.c_str() };
    check(cli(std::vector<std::string>(a1, a1 + 9)) == 0 &&
              iHostPathExists((out + "/UDATA/54510027/7B317197822D/GameData.xsv").c_str()),
          "--export-save A1 FOLDER --xbox emu --xbe writes the save");

    const char* a2[] = { "--save-dir", root.c_str(), "--export-save", "A1", out.c_str(), "--xbox",
                         "real", "--xbe", xbe.c_str() };
    check(cli(std::vector<std::string>(a2, a2 + 9)) == 1, "--xbox real with no --keys fails");

    const char* a3[] = { "--save-dir", root.c_str(), "--export-save", "A1", out.c_str(), "--xbox",
                         "real", "--xbe", xbe.c_str(), "--keys", keys.c_str(), "--as", "3" };
    check(cli(std::vector<std::string>(a3, a3 + 13)) == 0 &&
              iHostPathExists((out + "/UDATA/54510027/7B317197822F/GameData.xsv").c_str()),
          "--xbox real --keys --as 3 writes the Xbox's third save");

    const char* a4[] = { "--save-dir", root.c_str(), "--export-save", "A1", out.c_str(), "--xbox",
                         "sideways" };
    check(cli(std::vector<std::string>(a4, a4 + 7)) == 2, "--xbox takes emu or real");

    const char* a5[] = { "--save-dir", root.c_str(), "--check-save", out.c_str() };
    check(cli(std::vector<std::string>(a5, a5 + 4)) == 0, "--check-save lists a folder's Xbox saves");

    std::string third = out + "/UDATA/54510027/7B317197822F";
    const char* a6[] = { "--save-dir", root.c_str(), "--import-save", third.c_str(), "--slot", "B" };
    std::vector<U8> got;
    check(cli(std::vector<std::string>(a6, a6 + 6)) == 0 &&
              iSaveStoreRead(1, 2, got, err, sizeof(err)) && got == canonical,
          "--import-save FOLDER --slot B puts the Xbox's third save in B3");

    const char* a7[] = { "--save-dir", root.c_str(), "--export-save", "all", at("cli-all").c_str(),
                         "--xbox", "emu", "--xbe", xbe.c_str(), "--slot", "B" };
    std::string allDir = at("cli-all");
    removeTree(allDir);
    a7[4] = allDir.c_str();
    check(cli(std::vector<std::string>(a7, a7 + 11)) == 0 &&
              iHostPathExists((allDir + "/UDATA/54510027/7B317197822F/GameData.xsv").c_str()),
          "--export-save all FOLDER --xbox emu --slot B writes folder B's saves");
}

// --- PlayStation 2 ---------------------------------------------------------------------------

static U32 get32le(const U8* p)
{
    return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24);
}

static void put32le(std::vector<U8>& v, U32 x)
{
    put32(v, x, false);
}

static std::vector<U8> pattern(size_t n, U32 seed)
{
    std::vector<U8> v(n);
    for (size_t i = 0; i < n; i++)
    {
        seed = seed * 1103515245u + 12345u;
        v[i] = (U8)(seed >> 16);
    }
    return v;
}

static iSavePS2File ps2File(const char* name, const std::vector<U8>& data, S64 when)
{
    iSavePS2File f;
    f.name = name;
    f.data = data;
    f.mode = IPS2_MODE_FILE;
    f.attr = 0;
    f.created = when;
    f.modified = when;
    return f;
}

static bool sameFolder(const iSavePS2Folder& a, const iSavePS2Folder& b)
{
    if (a.name != b.name || a.files.size() != b.files.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.files.size(); i++)
    {
        const iSavePS2File* f = iSavePS2Find(&b, a.files[i].name.c_str());
        if (f == NULL || f->data != a.files[i].data)
        {
            return false;
        }
    }
    return true;
}

// The smallest package IconFromPackage walks: HIPA, DICT(ATOC(AHDR...)), STRM.
static std::vector<U8> makeIconPackage(U32 id, const std::vector<U8>& asset)
{
    std::vector<U8> out;
    for (int pass = 0; pass < 2; pass++)
    {
        U32 dataAt = pass ? (U32)out.size() + 8 : 0;
        out.clear();
        append(out, chunk("HIPA", std::vector<U8>()));

        std::vector<U8> atoc;
        append(atoc, chunk("AINF", std::vector<U8>(4, 0)));
        std::vector<U8> other;
        put32be(other, 0x1234);
        put32be(other, ISF_TAG('R', 'A', 'W', ' '));
        put32be(other, dataAt);
        put32be(other, 4);
        put32be(other, 0);
        put32be(other, 0);
        append(atoc, chunk("AHDR", other));
        std::vector<U8> ahdr;
        put32be(ahdr, id);
        put32be(ahdr, ISF_TAG('R', 'A', 'W', ' '));
        put32be(ahdr, dataAt + 4);
        put32be(ahdr, (U32)asset.size());
        put32be(ahdr, 0);
        put32be(ahdr, 0);
        append(atoc, chunk("AHDR", ahdr));

        std::vector<U8> dict;
        append(dict, chunk("ATOC", atoc));
        append(out, chunk("DICT", dict));
    }
    std::vector<U8> body(4, 0xEE);
    append(body, asset);
    append(out, chunk("STRM", body));
    return out;
}

static void testPS2Format()
{
    char err[400] = "";

    // Names.
    bool pal = true;
    check(iSavePS2IsGameFolder("BASLUS-20904SBMovie ", &pal) && !pal,
          "the NTSC folder's name, with its space");
    check(iSavePS2IsGameFolder("BASLUS-20904SBMovie_", &pal) && !pal &&
              iSavePS2IsGameFolder("BASLUS-20904SBMovie", &pal),
          "and as a host keeps it: '_' for the space, or without it");
    check(iSavePS2IsGameFolder("BESLES-52895SBMovie ", &pal) && pal, "the PAL folder's name");
    check(!iSavePS2IsGameFolder("BASLUS-20904SBMovieX", NULL) &&
              !iSavePS2IsGameFolder("BASLUS-20905SBMovie ", NULL) &&
              !iSavePS2IsGameFolder("", NULL),
          "other folders are not the game's");
    check(iSavePS2GameOfName("SpongeBobMovie01") == 0 && iSavePS2GameOfName("SpongeBobMovie03") == 2 &&
              iSavePS2GameOfName("SpongeBobMovie04") < 0 && iSavePS2GameOfName("SpongeBobMovie1") < 0 &&
              iSavePS2GameOfName("SpongeBobMovie01.tga") < 0,
          "the three save files' names");
    check(iSavePS2HostName("BASLUS-20904SBMovie ") == "BASLUS-20904SBMovie_" &&
              iSavePS2HostName("icon.sys") == "icon.sys" && iSavePS2HostName("a:b. .") == "a_b___",
          "a card name as a host can hold it");

    // Times: the card's clock is Japan's.
    {
        U8 t[8];
        iSavePS2TimePack(1100000000, t); // 2004-11-09 11:33:20 UTC
        const U8 want[8] = { 0, 20, 33, 20, 9, 11, 0xD4, 0x07 };
        check(memcmp(t, want, 8) == 0, "a time packs as Japan's clock: 2004-11-09 20:33:20");
        check(iSavePS2TimeUnpack(t) == 1100000000, "and unpacks to the same moment");
        iSavePS2TimePack(951782400 + 15 * 3600, t); // 2000-02-29 15:00 UTC = 03-01 00:00 JST
        check(t[4] == 1 && t[5] == 3 && t[3] == 0 && iSavePS2TimeUnpack(t) == 951782400 + 15 * 3600,
              "across a leap day");
        const U8 bad[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        check(iSavePS2TimeUnpack(bad) == 0, "an empty time is no time");
    }

    // icon.sys and the filler.
    {
        std::vector<U8> sys;
        iSavePS2IconSys(sys);
        const U8 title[] = { 0x82, 0x72, 0x82, 0x90, 0x82, 0x8F, 0x82, 0x8E, 0x82, 0x87, 0x82,
                             0x85, 0x82, 0x61, 0x82, 0x8F, 0x82, 0x82, 0x81, 0x40, 0x81, 0x7C,
                             0x81, 0x40, 0x82, 0x73, 0x82, 0x88, 0x82, 0x85, 0x81, 0x40, 0x82,
                             0x6C, 0x82, 0x8F, 0x82, 0x96, 0x82, 0x89, 0x82, 0x85, 0x00, 0x00 };
        check(sys.size() == IPS2_ICON_SYS_SIZE && memcmp(&sys[0], "PS2D", 4) == 0 &&
                  sys[6] == 22 && sys[7] == 0 && get32le(&sys[0x0C]) == 0x70,
              "icon.sys: 964 bytes, PS2D, the line break after 22 bytes");
        check(memcmp(&sys[0xC0], title, sizeof(title)) == 0,
              "its title is \"SpongeBob - The Movie\" in full-width Shift-JIS");
        check(strcmp((const char*)&sys[0x104], IPS2_ICON_FILE) == 0 &&
                  strcmp((const char*)&sys[0x144], IPS2_ICON_FILE) == 0 &&
                  strcmp((const char*)&sys[0x184], IPS2_ICON_FILE) == 0,
              "and it names SpongeIcon01.ico for the list, copy and delete icons");
        bool zero = true;
        for (size_t i = 0x1C4; i < sys.size(); i++)
        {
            zero = zero && sys[i] == 0;
        }
        check(zero && get32le(&sys[0x10]) == 65 && get32le(&sys[0x30]) == 10 &&
                  get32le(&sys[0x50]) == 0x3F800000 && get32le(&sys[0x74]) == 0x40000000,
              "with the game's colours and lights, and zeros after");

        std::vector<U8> filler;
        iSavePS2Filler(filler);
        bool shape = filler.size() == IPS2_FILLER_SIZE;
        for (size_t i = 0; shape && i < filler.size(); i++)
        {
            shape = filler[i] == (i < 640 ? 0 : 0xBF);
        }
        check(shape, "the filler: 992 bytes, twenty empty lines, then 0xBF");
    }

    // The port's own icon, and the game's out of a package.
    {
        std::vector<U8> icon;
        iSavePS2OwnIcon(icon);
        check(icon.size() == 20 + 36 * 24 + 36 + 128 * 128 * 2 &&
                  iSavePS2IconCheck(&icon[0], icon.size(), err, sizeof(err)),
              "the port's own icon is a well-formed icon file");
        check(get32le(&icon[0]) == 0x010000 && get32le(&icon[4]) == 1 && get32le(&icon[16]) == 36 &&
                  (icon[icon.size() - 1] & 0x80) != 0,
              "one shape, twelve triangles, an opaque picture");
        check(!iSavePS2IconCheck(&icon[0], icon.size() - 1, err, sizeof(err)) &&
                  !iSavePS2IconCheck(&icon[0], 19, err, sizeof(err)),
              "a cut icon is not one");
        std::vector<U8> bad = icon;
        bad[16] = 37;
        check(!iSavePS2IconCheck(&bad[0], bad.size(), err, sizeof(err)),
              "nor one whose counts do not add up");

        std::vector<U8> other = icon;
        other[other.size() - 2] ^= 0x1F; // a different picture
        std::vector<U8> pkg = makeIconPackage(IPS2_ICON_ASSET, other);
        std::vector<U8> got;
        check(iSavePS2IconFromPackage(&pkg[0], pkg.size(), got) && got == other,
              "the icon asset is found in a package by its id");
        pkg = makeIconPackage(0x11112222, other);
        check(!iSavePS2IconFromPackage(&pkg[0], pkg.size(), got) && got.empty(),
              "a package without it has none");
        pkg = makeIconPackage(IPS2_ICON_ASSET, std::vector<U8>(300, 7));
        check(!iSavePS2IconFromPackage(&pkg[0], pkg.size(), got),
              "an asset of that id that is not an icon is not taken");
        pkg = makeIconPackage(IPS2_ICON_ASSET, other);
        pkg.resize(pkg.size() - 100);
        check(!iSavePS2IconFromPackage(&pkg[0], pkg.size(), got), "nor one from a cut package");
    }

    // The card's error-correcting code: answers from PCSX2's own tables (the
    // top bit of the line bytes masked, as the console does).
    {
        struct Case
        {
            std::vector<U8> chunk;
            U8 want[3];
        };
        std::vector<Case> cases(5);
        cases[0].chunk.assign(128, 0);
        cases[0].want[0] = 0x77, cases[0].want[1] = 0x7F, cases[0].want[2] = 0x7F;
        cases[1].chunk.assign(128, 0xFF);
        cases[1].want[0] = 0x77, cases[1].want[1] = 0x7F, cases[1].want[2] = 0x7F;
        cases[2].chunk.assign(128, 0);
        cases[2].chunk[5] = 1;
        cases[2].want[0] = 0x70, cases[2].want[1] = 0x05, cases[2].want[2] = 0x7A;
        cases[3].chunk.resize(128);
        for (int i = 0; i < 128; i++)
        {
            cases[3].chunk[(size_t)i] = (U8)(i * 37 + 11);
        }
        cases[3].want[0] = 7, cases[3].want[1] = 94, cases[3].want[2] = 33;
        const char* magic = "Sony PS2 Memory Card Format 1.2.0.0";
        cases[4].chunk.assign(128, 0);
        memcpy(&cases[4].chunk[0], magic, strlen(magic));
        cases[4].want[0] = 34, cases[4].want[1] = 116, cases[4].want[2] = 116;

        bool all = true;
        for (size_t i = 0; i < cases.size(); i++)
        {
            U8 ecc[3];
            iSavePS2Ecc(&cases[i].chunk[0], ecc);
            all = all && memcmp(ecc, cases[i].want, 3) == 0;
        }
        check(all, "the card's error-correcting code: five known answers");

        // One flipped bit anywhere changes the code.
        bool sensitive = true;
        U8 base[3];
        iSavePS2Ecc(&cases[3].chunk[0], base);
        for (int bit = 0; bit < 128 * 8; bit += 13)
        {
            std::vector<U8> c = cases[3].chunk;
            c[(size_t)(bit / 8)] ^= (U8)(1 << (bit % 8));
            U8 ecc[3];
            iSavePS2Ecc(&c[0], ecc);
            sensitive = sensitive && memcmp(ecc, base, 3) != 0;
        }
        check(sensitive, "and any single flipped bit changes it");
    }

    const S64 when = 1100000000;

    // .psu
    iSavePS2Folder folder;
    iSavePS2FolderClear(&folder);
    folder.name = IPS2_FOLDER_NTSC;
    folder.created = when;
    folder.modified = when + 60;
    folder.files.push_back(ps2File("empty", std::vector<U8>(), when));
    folder.files.push_back(ps2File("one", pattern(1, 1), when + 1));
    folder.files.push_back(ps2File("exact", pattern(1024, 2), when + 2));
    folder.files.push_back(ps2File(IPS2_FOLDER_NTSC, pattern(1500, 3), when + 3));
    {
        std::vector<U8> psu;
        iSavePS2PsuBuild(&folder, psu);
        check(psu.size() == 3 * 512 + 4 * 512 + 0 + 1024 + 1024 + 2048 &&
                  iSavePS2PsuIs(&psu[0], psu.size()),
              "a .psu: an entry for the folder, . and .., then each file's entry and its "
              "bytes in whole kilobytes");
        check(get32le(&psu[0]) == IPS2_MODE_DIR && get32le(&psu[4]) == 6 &&
                  memcmp(&psu[0x40], IPS2_FOLDER_NTSC, 21) == 0 &&
                  get32le(&psu[3 * 512]) == IPS2_MODE_FILE,
              "with the card's own modes, and the folder's name with its space");

        iSavePS2Folder back;
        check(iSavePS2PsuParse(&psu[0], psu.size(), &back, err, sizeof(err)) &&
                  sameFolder(folder, back) && back.files.size() == 4 &&
                  back.files[3].modified == when + 3 && back.created == when &&
                  back.modified == when + 60 && back.files[1].mode == IPS2_MODE_FILE,
              "and reads back the same: names, bytes, times");

        check(!iSavePS2PsuParse(&psu[0], psu.size() - 2048, &back, err, sizeof(err)) &&
                  strstr(err, "cut short") != NULL,
              "a cut .psu is refused");
        std::vector<U8> junk = pattern(4096, 9);
        check(!iSavePS2PsuIs(&junk[0], junk.size()) && !iSavePS2PsuIs(&psu[0], 1000),
              "other bytes are not a .psu");
    }

    // .psv, made here the way a PlayStation 3 lays one out.
    {
        std::vector<U8> psv(0xA0, 0);
        memcpy(&psv[0], "\0VSP", 4);
        psv[0x38] = 0x2C;
        psv[0x3C] = 2;
        psv[0x64] = 2;
        iSavePS2TimePack(when, &psv[0x68]);
        iSavePS2TimePack(when + 5, &psv[0x70]);
        psv[0x78] = 4;
        psv[0x7C] = 0x27;
        psv[0x7D] = 0x84;
        memcpy(&psv[0x80], IPS2_FOLDER_NTSC, 20);
        std::vector<U8> a = pattern(700, 4);
        std::vector<U8> b = pattern(33, 5);
        size_t dataAt = 0xA0 + 2 * 0x3C;
        for (int i = 0; i < 2; i++)
        {
            const std::vector<U8>& d = i ? b : a;
            std::vector<U8> e(0x3C, 0);
            iSavePS2TimePack(when + 10 + i, &e[0]);
            iSavePS2TimePack(when + 20 + i, &e[8]);
            e[0x10] = (U8)d.size();
            e[0x11] = (U8)(d.size() >> 8);
            e[0x14] = 0x97;
            e[0x15] = 0x84;
            memcpy(&e[0x18], i ? "SpongeBobMovie01" : "icon.sys", i ? 16 : 8);
            e[0x38] = (U8)dataAt;
            e[0x39] = (U8)(dataAt >> 8);
            psv.insert(psv.end(), e.begin(), e.end());
            dataAt += d.size();
        }
        append(psv, a);
        append(psv, b);

        iSavePS2Folder back;
        check(iSavePS2PsvIs(&psv[0], psv.size()) &&
                  iSavePS2PsvParse(&psv[0], psv.size(), &back, err, sizeof(err)) &&
                  back.name == IPS2_FOLDER_NTSC && back.files.size() == 2 &&
                  back.files[0].name == "icon.sys" && back.files[0].data == a &&
                  back.files[1].data == b && back.files[1].modified == when + 21,
              "a .psv's folder and files are read");
        check(!iSavePS2PsvParse(&psv[0], psv.size() - 10, &back, err, sizeof(err)),
              "a cut .psv is refused");
    }

    // .max: nothing here can pack one, so only that junk is refused and does
    // not hang (a real one is read when the player's saves are at hand).
    {
        std::vector<U8> max = pattern(5000, 6);
        memcpy(&max[0], "Ps2PowerSave", 12);
        max[0x50] = 0x00, max[0x51] = 0x10, max[0x52] = 0, max[0x53] = 0; // 4096 packed
        max[0x54] = 2, max[0x55] = 0, max[0x56] = 0, max[0x57] = 0;
        max[0x58] = 0x00, max[0x59] = 0x00, max[0x5A] = 0x40, max[0x5B] = 0; // 4 MB unpacked
        iSavePS2Folder back;
        bool ok = iSavePS2MaxIs(&max[0], max.size()) &&
                  iSavePS2MaxParse(&max[0], max.size(), &back, err, sizeof(err));
        check(!ok, "a .max of noise is refused");
        max[0x52] = 0x10; // a packed size past the file's end
        check(!iSavePS2MaxParse(&max[0], max.size(), &back, err, sizeof(err)) &&
                  strstr(err, "header") != NULL,
              "and one whose header overruns the file");
    }

    // A card image.
    for (int withEcc = 1; withEcc >= 0; withEcc--)
    {
        std::vector<U8> card;
        iSavePS2CardFormat(card, withEcc != 0, when);
        U32 freeKB = 0;
        check(card.size() == (size_t)IPS2_CARD_CLUSTERS * 2 * (withEcc ? 528 : 512) &&
                  iSavePS2CardIs(&card[0], card.size()) &&
                  iSavePS2CardCheck(&card[0], card.size(), &freeKB, err, sizeof(err)) &&
                  freeKB == 8134,
              withEcc ? "a blank formatted card with the spare bytes: 8,650,752 bytes, 8134 KB free" :
                        "a blank formatted card without them: 8,388,608 bytes, 8134 KB free");

        std::vector<std::string> names;
        check(iSavePS2CardList(&card[0], card.size(), names, err, sizeof(err)) && names.empty(),
              "it has no folders");

        // Another game's folder first: it must come through everything untouched.
        iSavePS2Folder other;
        iSavePS2FolderClear(&other);
        other.name = "BASLUS-99999OTHER";
        other.files.push_back(ps2File("icon.sys", pattern(964, 11), when));
        other.files.push_back(ps2File("data", pattern(70000, 12), when));
        other.files.push_back(ps2File("tiny", pattern(3, 13), when));
        check(iSavePS2CardWriteFolder(card, &other, when, err, sizeof(err)),
              "another game's folder goes on");

        check(iSavePS2CardWriteFolder(card, &folder, when + 100, err, sizeof(err)),
              "the game's folder goes on");

        // root: 4 entries, 2 clusters. other: 5 entries (3 clusters), 1 + 69 + 1
        // of data. the game's: 6 entries (3 clusters), 0 + 1 + 1 + 2 of data.
        U32 expectFree = 8134 - 1 - (3 + 71) - (3 + 4);
        bool okCheck = iSavePS2CardCheck(&card[0], card.size(), &freeKB, err, sizeof(err));
        if (!okCheck)
        {
            printf("  (%s)\n", err);
        }
        check(okCheck && freeKB == expectFree,
              "the card checks out: every written page's code, every chain, the free space");

        iSavePS2Folder back;
        check(iSavePS2CardList(&card[0], card.size(), names, err, sizeof(err)) &&
                  names.size() == 2 && names[1] == IPS2_FOLDER_NTSC,
              "both folders are listed, the game's under its name with the space");
        check(iSavePS2CardReadFolder(&card[0], card.size(), IPS2_FOLDER_NTSC, &back, err,
                                     sizeof(err)) &&
                  sameFolder(folder, back) && back.files[3].modified == when + 3,
              "the game's folder reads back the same");
        check(iSavePS2CardReadFolder(&card[0], card.size(), other.name.c_str(), &back, err,
                                     sizeof(err)) &&
                  sameFolder(other, back),
              "and so does the other game's");

        // Replace one file with a longer and then a shorter one, add another.
        iSavePS2Folder change;
        iSavePS2FolderClear(&change);
        change.name = IPS2_FOLDER_NTSC;
        change.files.push_back(ps2File("exact", pattern(52000, 21), when + 200));
        change.files.push_back(ps2File("SpongeBobMovie03", pattern(51560, 22), when + 200));
        std::vector<U8> before = card;
        check(iSavePS2CardWriteFolder(card, &change, when + 200, err, sizeof(err)),
              "a file is replaced by a longer one and another added");

        iSavePS2Folder expect = folder;
        iSavePS2Find(&expect, "exact")->data = change.files[0].data;
        expect.files.push_back(change.files[1]);
        expectFree -= (51 - 1) + 51 + 1; // one more entry cluster for the seventh entry
        check(iSavePS2CardCheck(&card[0], card.size(), &freeKB, err, sizeof(err)) &&
                  freeKB == expectFree &&
                  iSavePS2CardReadFolder(&card[0], card.size(), IPS2_FOLDER_NTSC, &back, err,
                                         sizeof(err)) &&
                  sameFolder(expect, back) &&
                  iSavePS2CardReadFolder(&card[0], card.size(), other.name.c_str(), &back, err,
                                         sizeof(err)) &&
                  sameFolder(other, back),
              "the folder holds the new files, the old ones and the other game's are intact");

        // Only whole pages changed, and none of the other game's.
        {
            size_t raw = withEcc ? 528 : 512;
            size_t changed = 0;
            for (size_t page = 0; page < card.size() / raw; page++)
            {
                if (memcmp(&card[page * raw], &before[page * raw], raw) != 0)
                {
                    changed++;
                }
            }
            // 2 x (51 + 51) of data, and a handful of table and directory pages.
            check(changed >= 200 && changed <= 204 + 24,
                  "only the pages of the new files, their entries and the table changed");
        }

        change.files.clear();
        change.files.push_back(ps2File("exact", pattern(10, 23), when + 300));
        iSavePS2Find(&expect, "exact")->data = change.files[0].data;
        expectFree += 51 - 1;
        check(iSavePS2CardWriteFolder(card, &change, when + 300, err, sizeof(err)) &&
                  iSavePS2CardCheck(&card[0], card.size(), &freeKB, err, sizeof(err)) &&
                  freeKB == expectFree &&
                  iSavePS2CardReadFolder(&card[0], card.size(), IPS2_FOLDER_NTSC, &back, err,
                                         sizeof(err)) &&
                  sameFolder(expect, back),
              "and by a shorter one, its space given back");

        // A card too full: refused, and not one byte changed.
        change.files.clear();
        change.files.push_back(ps2File("big1", pattern(4 * 1024 * 1024, 31), when));
        change.files.push_back(ps2File("big2", pattern(4 * 1024 * 1024, 32), when));
        before = card;
        check(!iSavePS2CardWriteFolder(card, &change, when, err, sizeof(err)) &&
                  strstr(err, "full") != NULL && card == before,
              "a write that does not fit is refused and leaves the card exactly as it was");

        if (withEcc)
        {
            // A flipped bit in a written page is caught by its code.
            std::vector<U8> hurt = card;
            hurt[(size_t)41 * 2 * 528 + 100] ^= 0x04;
            check(!iSavePS2CardCheck(&hurt[0], hurt.size(), NULL, err, sizeof(err)) &&
                      strstr(err, "page 82") != NULL,
                  "a flipped bit in a written page is caught by the page's code");

            // The spare bytes: three per quarter page, then four zeros.
            const U8* page0 = &card[0];
            U8 ecc[3];
            iSavePS2Ecc(page0 + 128, ecc);
            check(memcmp(page0 + 512 + 3, ecc, 3) == 0 && page0[512 + 12] == 0 &&
                      page0[512 + 15] == 0,
                  "the spare bytes hold each quarter's code, then four zeros");
        }

        // A file of the wrong size is not taken for a card.
        std::vector<U8> cut(card.begin(), card.end() - 512);
        check(!iSavePS2CardCheck(&cut[0], cut.size(), NULL, err, sizeof(err)) &&
                  !iSavePS2CardWriteFolder(cut, &folder, when, err, sizeof(err)),
              "a card file of the wrong size is refused");

        // A chain cut in the table is found, not followed.
        {
            std::vector<U8> hurt = card;
            // The table's first cluster is 9; entry 0 is the root directory's.
            size_t raw = withEcc ? 528 : 512;
            memset(&hurt[(size_t)9 * 2 * raw], 0x7F, 3); // not in use
            hurt[(size_t)9 * 2 * raw + 3] = 0x7F;
            std::vector<std::string> n2;
            check(!iSavePS2CardReadFolder(&hurt[0], hurt.size(), IPS2_FOLDER_NTSC, &back, err,
                                          sizeof(err)),
                  "a card whose table is damaged is refused, not followed");
        }
    }
}

static void testPS2Store(const std::vector<U8>& canonical)
{
    char err[768] = "";
    std::string root = at("saves-ps2");
    removeTree(root);
    iSaveStoreSetRoot(root.c_str());
    iSaveStoreSetAssetRoot(at("assets-gc").c_str());
    sConfigPs2Icon = "";

    const S64 when = 1100000000;
    iSaveStoreWrite(0, 1, &canonical[0], canonical.size(), err, sizeof(err));
    char slotPath[512];
    iSaveStoreSlotPath(0, 1, slotPath, sizeof(slotPath));
    iHostSetFileTime(slotPath, when);

    std::vector<U8> le;
    iSaveStoreToLE(&canonical[0], canonical.size(), le, NULL, err, sizeof(err));

    std::vector<U8> ownIcon;
    iSavePS2OwnIcon(ownIcon);
    std::vector<U8> sys;
    iSavePS2IconSys(sys);
    std::vector<U8> filler;
    iSavePS2Filler(filler);

    removeTree(at("ps2"));
    iSavePS2ExportOptions o;
    iSavePS2ExportDefaults(&o);
    std::string where;

    // --- a .psu
    std::string psuPath = at("ps2/out/save.psu");
    bool ok = iSaveStoreExportPS2(0, 1, &o, psuPath.c_str(), &where, err, sizeof(err));
    if (!ok)
    {
        printf("  (%s)\n", err);
    }
    std::vector<U8> psu;
    iSavePS2Folder got;
    iSavePS2FolderClear(&got);
    check(ok && where == psuPath && readFile(psuPath, psu) &&
              iSavePS2PsuParse(&psu[0], psu.size(), &got, err, sizeof(err)) &&
              got.name == IPS2_FOLDER_NTSC && got.files.size() == 4,
          "slot A2 exports as a .psu holding the game's folder and four files");
    if (got.files.size() == 4)
    {
        check(got.files[0].name == IPS2_FOLDER_NTSC && got.files[0].data == filler &&
                  got.files[1].name == IPS2_ICON_SYS && got.files[1].data == sys &&
                  got.files[2].name == IPS2_ICON_FILE && got.files[2].data == ownIcon &&
                  got.files[3].name == "SpongeBobMovie02",
              "the filler, icon.sys, the icon (the port's own) and SpongeBobMovie02");
        const std::vector<U8>& d = got.files[3].data;
        check(d == le && get32le(&d[8]) == d.size() &&
                  iSaveCRC(0xFFFFFFFF, &d[0x10], d.size() - 0x10) == get32le(&d[0x0C]),
              "the save file is the little-endian payload and nothing else, exactly its "
              "size, its CRC running to the end of the file");
        check(got.files[3].modified == when && got.files[3].mode == IPS2_MODE_FILE &&
                  got.mode == IPS2_MODE_DIR,
              "with the save's date and the card's own modes");
    }

    check(!iSaveStoreExportPS2(0, 1, &o, psuPath.c_str(), &where, err, sizeof(err)) &&
              strstr(err, "already there") != NULL,
          "a save already in the .psu is not replaced unasked");
    o.number = 2;
    check(iSaveStoreExportPS2(0, 1, &o, psuPath.c_str(), &where, err, sizeof(err)) &&
              readFile(psuPath, psu) &&
              iSavePS2PsuParse(&psu[0], psu.size(), &got, err, sizeof(err)) &&
              got.files.size() == 5 && got.files[4].name == "SpongeBobMovie03" &&
              got.files[3].data == le,
          "exported again as save 3, the .psu holds both");
    o.number = -1;
    o.replace = true;
    check(iSaveStoreExportPS2(0, 1, &o, psuPath.c_str(), &where, err, sizeof(err)),
          "and with replacing allowed the same number goes in again");
    o.replace = false;

    // Import it: both saves, by their numbers, with their date.
    {
        iSaveImportOptions imp;
        iSaveImportDefaults(&imp);
        imp.target = 3;
        std::vector<iSaveImportItem> items;
        ok = iSaveStoreImport(psuPath.c_str(), &imp, items, err, sizeof(err));
        std::vector<U8> a;
        std::vector<U8> b;
        check(ok && items.size() == 2 && items[0].game == 1 && items[1].game == 2 &&
                  iSaveStoreRead(3, 1, a, err, sizeof(err)) && a == canonical &&
                  iSaveStoreRead(3, 2, b, err, sizeof(err)) && b == canonical,
              "the .psu imports: both saves, each to the slot of its number, byte for byte");
        check(items.size() == 2 &&
                  items[0].source.find("a PlayStation 2 save") != std::string::npos &&
                  items[0].source.find(".psu") != std::string::npos,
              "and is reported as a PlayStation 2 save from a .psu");
        iSaveSlotInfo info;
        iSaveStoreSlot(3, 1, &info);
        check(info.mtime >= when - 2 && info.mtime <= when + 2, "and keeps its date");

        imp.pick = 2;
        imp.game = 0;
        imp.force = true;
        check(iSaveStoreImport(psuPath.c_str(), &imp, items, err, sizeof(err)) &&
                  items.size() == 1 && items[0].game == 0,
              "one of them can be picked and put in another slot");
        for (S32 g = 0; g < 3; g++)
        {
            iSaveStoreDelete(3, g, err, sizeof(err));
        }
    }

    // A folder that is not a card gets a .psu under the game's name.
    {
        std::string dir = at("ps2/plain");
        iHostMakeDir(dir.c_str());
        check(iSaveStoreExportPS2(0, 1, &o, dir.c_str(), &where, err, sizeof(err)) &&
                  where == dir + "/BASLUS-20904SBMovie.psu" && iHostPathExists(where.c_str()),
              "into a plain folder: BASLUS-20904SBMovie.psu");
        o.pal = true;
        check(iSaveStoreExportPS2(0, 1, &o, dir.c_str(), &where, err, sizeof(err)) &&
                  where == dir + "/BESLES-52895SBMovie.psu" && readFile(where, psu) &&
                  iSavePS2PsuParse(&psu[0], psu.size(), &got, err, sizeof(err)) &&
                  got.name == IPS2_FOLDER_PAL && iSavePS2Find(&got, IPS2_FOLDER_PAL) != NULL,
              "the PAL game's folder and filler have its name");
        o.pal = false;
    }

    // --- the icon: the game's own when the player's files have it
    {
        std::vector<U8> gameIcon = ownIcon;
        gameIcon[gameIcon.size() - 2] ^= 0x1F;
        std::string disc = at("ps2/disc");
        writeFile(disc + "/PLAT.HIP", makeIconPackage(IPS2_ICON_ASSET, gameIcon));

        std::vector<U8> icon;
        std::string from;
        check(iSaveStorePS2Icon(NULL, icon, &from, err, sizeof(err)) && icon == ownIcon &&
                  from.empty(),
              "with nothing named, the icon is the port's own");
        check(iSaveStorePS2Icon((disc + "/PLAT.HIP").c_str(), icon, &from, err, sizeof(err)) &&
                  icon == gameIcon && from == disc + "/PLAT.HIP",
              "named, PLAT.HIP gives the game's icon");
        check(iSaveStorePS2Icon(disc.c_str(), icon, &from, err, sizeof(err)) && icon == gameIcon,
              "and so does the folder it is in");
        sConfigPs2Icon = disc;
        check(iSaveStorePS2Icon(NULL, icon, &from, err, sizeof(err)) && icon == gameIcon,
              "or the setting");
        sConfigPs2Icon = "";
        check(!iSaveStorePS2Icon(at("ps2/nowhere").c_str(), icon, &from, err, sizeof(err)) &&
                  strstr(err, "PLAT.HIP") != NULL && strstr(err, "your own") != NULL,
              "a place with no icon is said plainly, naming PLAT.HIP from the player's own disc");

        snprintf(o.icon, sizeof(o.icon), "%s", disc.c_str());
        std::string p = at("ps2/icon/game.psu");
        check(iSaveStoreExportPS2(0, 1, &o, p.c_str(), &where, err, sizeof(err)) &&
                  readFile(p, psu) && iSavePS2PsuParse(&psu[0], psu.size(), &got, err, sizeof(err)) &&
                  iSavePS2Find(&got, IPS2_ICON_FILE) != NULL &&
                  iSavePS2Find(&got, IPS2_ICON_FILE)->data == gameIcon,
              "an export with it carries the game's icon");

        // And a save the game made is a source of it too.
        snprintf(o.icon, sizeof(o.icon), "%s", p.c_str());
        std::string p2 = at("ps2/icon/again.psu");
        check(iSaveStoreExportPS2(0, 1, &o, p2.c_str(), &where, err, sizeof(err)) &&
                  readFile(p2, psu) && iSavePS2PsuParse(&psu[0], psu.size(), &got, err, sizeof(err)) &&
                  iSavePS2Find(&got, IPS2_ICON_FILE)->data == gameIcon,
              "as does a save that has it");

        snprintf(o.icon, sizeof(o.icon), "%s", at("ps2/nowhere").c_str());
        std::string p3 = at("ps2/icon/none.psu");
        check(!iSaveStoreExportPS2(0, 1, &o, p3.c_str(), &where, err, sizeof(err)) &&
                  !iHostPathExists(p3.c_str()),
              "and an export naming a place without one writes nothing");
        o.icon[0] = '\0';
    }

    // --- a memory card file
    for (int withEcc = 1; withEcc >= 0; withEcc--)
    {
        std::string cardPath = at(withEcc ? "ps2/cards/Mcd001.ps2" : "ps2/cards/raw.bin");
        std::vector<U8> card;
        iSavePS2CardFormat(card, withEcc != 0, when);
        iSavePS2Folder other;
        iSavePS2FolderClear(&other);
        other.name = "BASLUS-99999OTHER";
        other.files.push_back(ps2File("data", pattern(30000, 41), when));
        iSavePS2CardWriteFolder(card, &other, when, err, sizeof(err));
        writeFile(cardPath, card);
        std::vector<U8> original = card;

        ok = iSaveStoreExportPS2(0, 1, &o, cardPath.c_str(), &where, err, sizeof(err));
        if (!ok)
        {
            printf("  (%s)\n", err);
        }
        std::vector<U8> bak;
        check(ok && readFile(cardPath + ".bak", bak) && bak == original &&
                  where.find(cardPath + ".bak") != std::string::npos,
              withEcc ? "into a card file: first a copy of the card as it was, beside it" :
                        "into a card file without spare bytes: the same");

        iSavePS2Folder back;
        check(readFile(cardPath, card) && card.size() == original.size() &&
                  iSavePS2CardCheck(&card[0], card.size(), NULL, err, sizeof(err)) &&
                  iSavePS2CardReadFolder(&card[0], card.size(), IPS2_FOLDER_NTSC, &back, err,
                                         sizeof(err)) &&
                  back.files.size() == 4 && iSavePS2Find(&back, "SpongeBobMovie02") != NULL &&
                  iSavePS2Find(&back, "SpongeBobMovie02")->data == le &&
                  iSavePS2Find(&back, IPS2_ICON_SYS)->data == sys &&
                  iSavePS2Find(&back, IPS2_FOLDER_NTSC)->data == filler,
              "the card then holds the game's folder with the save and its three files");
        check(iSavePS2CardReadFolder(&card[0], card.size(), other.name.c_str(), &back, err,
                                     sizeof(err)) &&
                  sameFolder(other, back),
              "and the other game's save is as it was");

        check(!iSaveStoreExportPS2(0, 1, &o, cardPath.c_str(), &where, err, sizeof(err)) &&
                  strstr(err, "already there") != NULL &&
                  !iHostPathExists((cardPath + ".bak2").c_str()),
              "a save already on the card is not replaced unasked, and no copy is made for "
              "nothing");
        o.number = 0;
        check(iSaveStoreExportPS2(0, 1, &o, cardPath.c_str(), &where, err, sizeof(err)) &&
                  iHostPathExists((cardPath + ".bak2").c_str()) && readFile(cardPath, card) &&
                  iSavePS2CardReadFolder(&card[0], card.size(), IPS2_FOLDER_NTSC, &back, err,
                                         sizeof(err)) &&
                  back.files.size() == 5 && readFile(cardPath + ".bak", bak) && bak == original,
              "a second save goes in beside it, with a second copy; the first copy is kept");
        o.number = -1;

        // Import from the card: nothing is written to it.
        std::vector<U8> cardBefore;
        readFile(cardPath, cardBefore);
        iSaveImportOptions imp;
        iSaveImportDefaults(&imp);
        imp.target = 3;
        std::vector<iSaveImportItem> items;
        std::vector<U8> a;
        ok = iSaveStoreImport(cardPath.c_str(), &imp, items, err, sizeof(err));
        check(ok && items.size() == 2 && iSaveStoreRead(3, 0, a, err, sizeof(err)) &&
                  a == canonical && iSaveStoreRead(3, 1, a, err, sizeof(err)) && a == canonical &&
                  items[0].source.find("memory card") != std::string::npos,
              "the card imports: both saves, byte for byte");
        check(readFile(cardPath, card) && card == cardBefore, "and the card is not touched");
        for (S32 g = 0; g < 3; g++)
        {
            iSaveStoreDelete(3, g, err, sizeof(err));
        }
    }

    // A card PCSX2 has just made is blank: said, and left alone.
    {
        std::string blank = at("ps2/cards/blank.ps2");
        std::vector<U8> ff((size_t)IPS2_CARD_CLUSTERS * 2 * 528, 0xFF);
        writeFile(blank, ff);
        o.kind = ISS_PS2_CARD;
        std::vector<U8> after;
        check(!iSaveStoreExportPS2(0, 1, &o, blank.c_str(), &where, err, sizeof(err)) &&
                  strstr(err, "not a formatted") != NULL && readFile(blank, after) && after == ff &&
                  !iHostPathExists((blank + ".bak").c_str()),
              "a blank unformatted card is refused and left alone");
        o.kind = ISS_PS2_AUTO;

        // A new name ending .ps2: a new formatted card.
        std::string fresh = at("ps2/cards/new.ps2");
        check(iSaveStoreExportPS2(0, 1, &o, fresh.c_str(), &where, err, sizeof(err)) &&
                  readFile(fresh, after) && after.size() == ff.size() &&
                  iSavePS2CardCheck(&after[0], after.size(), NULL, err, sizeof(err)) &&
                  !iHostPathExists((fresh + ".bak").c_str()),
              "a new name ending .ps2 becomes a new formatted card holding the save");
    }

    // --- PCSX2's folder-type card
    {
        std::string cardDir = at("ps2/folder/Mcd002.ps2");
        writeFile(cardDir + "/_pcsx2_superblock", std::vector<U8>()); // as PCSX2 makes one
        ok = iSaveStoreExportPS2(0, 1, &o, cardDir.c_str(), &where, err, sizeof(err));
        if (!ok)
        {
            printf("  (%s)\n", err);
        }
        std::string game = cardDir + "/BASLUS-20904SBMovie_";
        check(ok && where == game, "into PCSX2's folder card: the folder BASLUS-20904SBMovie_");

        std::vector<U8> sb;
        check(readFile(cardDir + "/_pcsx2_superblock", sb) && sb.size() == 8192 &&
                  iSavePS2CardIs(&sb[0], sb.size()) && sb[0x16] == 0x6F,
              "the card's first block is formatted, so PCSX2 shows what is in it");

        std::vector<U8> meta;
        check(readFile(game + "/_pcsx2_meta_directory", meta) && meta.size() == 512 &&
                  get32le(&meta[0]) == IPS2_MODE_DIR &&
                  strcmp((const char*)&meta[0x40], IPS2_FOLDER_NTSC) == 0,
              "the folder's card entry keeps its real name, space and all");
        check(readFile(game + "/_pcsx2_meta/BASLUS-20904SBMovie_", meta) && meta.size() == 512 &&
                  get32le(&meta[0]) == IPS2_MODE_FILE && get32le(&meta[4]) == IPS2_FILLER_SIZE &&
                  strcmp((const char*)&meta[0x40], IPS2_FOLDER_NTSC) == 0,
              "and so does the filler file's");

        std::vector<U8> f;
        check(readFile(game + "/SpongeBobMovie02", f) && f == le &&
                  readFile(game + "/icon.sys", f) && f == sys &&
                  readFile(game + "/SpongeIcon01.ico", f) && f == ownIcon &&
                  readFile(game + "/BASLUS-20904SBMovie_", f) && f == filler,
              "the four files are there as plain files");

        std::vector<U8> index;
        readFile(game + "/_pcsx2_index", index);
        std::string text(index.begin(), index.end());
        char stamp[64];
        snprintf(stamp, sizeof(stamp), "  timeModified: %lld\n", (long long)(when + 9 * 3600));
        check(text.find("$ROOT:\n") == 0 && text.find("\nSpongeBobMovie02:\n  order: 4\n") !=
                                                 std::string::npos &&
                  text.find("\nBASLUS-20904SBMovie_:\n  order: 1\n") != std::string::npos &&
                  text.find(stamp) != std::string::npos,
              "the index lists each file's order and times, on the card's clock");

        check(!iSaveStoreExportPS2(0, 1, &o, cardDir.c_str(), &where, err, sizeof(err)) &&
                  strstr(err, "already there") != NULL,
              "a save already in the folder card is not replaced unasked");

        o.kind = ISS_PS2_FOLDER_CARD;
        check(!iSaveStoreExportPS2(0, 1, &o, at("ps2/cards").c_str(), &where, err, sizeof(err)) &&
                  strstr(err, "_pcsx2_superblock") != NULL &&
                  !iHostPathExists(at("ps2/cards/_pcsx2_superblock").c_str()),
              "the folder the cards are kept in is not taken for a folder card");
        std::string made = at("ps2/folder/New.ps2");
        check(iSaveStoreExportPS2(0, 1, &o, made.c_str(), &where, err, sizeof(err)) &&
                  iHostPathExists((made + "/_pcsx2_superblock").c_str()) &&
                  iHostPathExists((made + "/BASLUS-20904SBMovie_/SpongeBobMovie02").c_str()),
              "a new folder named like a card becomes a folder card");
        o.kind = ISS_PS2_AUTO;

        // Import: the card's folder, and the game's folder inside it.
        const std::string shapes[2] = { cardDir, game };
        for (int i = 0; i < 2; i++)
        {
            iSaveImportOptions imp;
            iSaveImportDefaults(&imp);
            imp.target = 3;
            imp.force = true;
            std::vector<iSaveImportItem> items;
            std::vector<U8> a;
            ok = iSaveStoreImport(shapes[i].c_str(), &imp, items, err, sizeof(err));
            check(ok && items.size() == 1 && items[0].game == 1 &&
                      iSaveStoreRead(3, 1, a, err, sizeof(err)) && a == canonical &&
                      items[0].source.find("a PlayStation 2 save") != std::string::npos,
                  i ? "the game's folder inside it imports too" :
                      "PCSX2's folder card imports: the save, byte for byte");
            iSaveStoreDelete(3, 1, err, sizeof(err));
        }

        // An index PCSX2 wrote on one line is read, kept and added to.
        std::string cardDir2 = at("ps2/folder/Mcd003.ps2");
        std::string game2 = cardDir2 + "/BASLUS-20904SBMovie_";
        std::vector<U8> sb2;
        iSavePS2CardFormat(sb2, false, when);
        sb2.resize(8192);
        writeFile(cardDir2 + "/_pcsx2_superblock", sb2);
        const char* flow = "{$ROOT: {timeCreated: 5, timeModified: 6}, SpongeBobMovie01: "
                           "{order: 7, timeCreated: 11, timeModified: 12}}\n";
        writeFile(game2 + "/_pcsx2_index", bytesOf(flow));
        writeFile(game2 + "/SpongeBobMovie01", le);
        check(iSaveStoreExportPS2(0, 1, &o, cardDir2.c_str(), &where, err, sizeof(err)) &&
                  readFile(game2 + "/_pcsx2_index", index),
              "a folder card that already has a save takes another");
        text.assign(index.begin(), index.end());
        check(text.find("$ROOT:\n  timeCreated: 5\n") == 0 &&
                  text.find("\nSpongeBobMovie01:\n  order: 7\n  timeCreated: 11\n  "
                            "timeModified: 12\n") != std::string::npos &&
                  text.find("\nSpongeBobMovie02:\n  order: 11\n") != std::string::npos,
              "its index keeps what was there and adds the new files after");
        check(readFile(cardDir2 + "/_pcsx2_superblock", sb) && sb == sb2,
              "and its formatted first block is left as it was");

        // An index nobody can read is left alone.
        std::string cardDir3 = at("ps2/folder/Mcd004.ps2");
        std::string game3 = cardDir3 + "/BASLUS-20904SBMovie_";
        writeFile(cardDir3 + "/_pcsx2_superblock", sb2);
        std::vector<U8> noise = bytesOf("{{{ not: [yaml");
        writeFile(game3 + "/_pcsx2_index", noise);
        check(iSaveStoreExportPS2(0, 1, &o, cardDir3.c_str(), &where, err, sizeof(err)) &&
                  readFile(game3 + "/_pcsx2_index", index) && index == noise &&
                  iHostPathExists((game3 + "/SpongeBobMovie02").c_str()),
              "an index that cannot be read is left alone; the save still goes in");
    }

    // A copy of the game's folder, as any tool unpacks it.
    {
        std::string dir = at("ps2/unpacked/BASLUS-20904SBMovie_");
        writeFile(dir + "/SpongeBobMovie03", le);
        writeFile(dir + "/icon.sys", sys);
        writeFile(dir + "/SpongeBobMovie01", canonical); // a PC save file: not a PS2 one
        iSaveImportOptions imp;
        iSaveImportDefaults(&imp);
        imp.target = 3;
        std::vector<iSaveImportItem> items;
        std::vector<U8> a;
        check(iSaveStoreImport(dir.c_str(), &imp, items, err, sizeof(err)) && items.size() == 1 &&
                  items[0].game == 2 && iSaveStoreRead(3, 2, a, err, sizeof(err)) && a == canonical,
              "a plain copy of the game's folder imports its PlayStation 2 saves");
        iSaveStoreDelete(3, 2, err, sizeof(err));

        check(!iSaveStoreImport(at("ps2/disc").c_str(), &imp, items, err, sizeof(err)) &&
                  strstr(err, "PlayStation 2") != NULL,
              "a folder with no save says what a save folder looks like");
    }

    // A card with no save of this game, and a damaged .psu.
    {
        std::vector<U8> card;
        iSavePS2CardFormat(card, true, when);
        std::string p = at("ps2/cards/empty.ps2");
        writeFile(p, card);
        iSaveImportOptions imp;
        iSaveImportDefaults(&imp);
        std::vector<iSaveImportItem> items;
        check(!iSaveStoreImport(p.c_str(), &imp, items, err, sizeof(err)) &&
                  strstr(err, "holds no SpongeBob Movie saves") != NULL,
              "a card with no save of this game says so");

        std::vector<U8> psu;
        readFile(psuPath, psu);
        psu.resize(psu.size() - 30000);
        p = at("ps2/out/cut.psu");
        writeFile(p, psu);
        check(!iSaveStoreImport(p.c_str(), &imp, items, err, sizeof(err)) &&
                  strstr(err, "cut short") != NULL,
              "a cut .psu is refused with the reason");
    }

    // All three of a folder at once.
    {
        iSaveStoreWrite(0, 0, &canonical[0], canonical.size(), err, sizeof(err));
        std::vector<iSaveExportItem> items;
        std::string p = at("ps2/out/all.psu");
        std::vector<U8> psu;
        iSavePS2Folder got2;
        check(iSaveStoreExportPS2Folder(0, &o, p.c_str(), items, &where, err, sizeof(err)) == 2 &&
                  items.size() == 2 && items[0].ok && items[1].ok && readFile(p, psu) &&
                  iSavePS2PsuParse(&psu[0], psu.size(), &got2, err, sizeof(err)) &&
                  got2.files.size() == 5 && iSavePS2Find(&got2, "SpongeBobMovie01") != NULL &&
                  iSavePS2Find(&got2, "SpongeBobMovie02") != NULL,
              "a folder's saves go into one .psu together");
        check(iSaveStoreExportPS2Folder(5, &o, p.c_str(), items, &where, err, sizeof(err)) == 0 &&
                  items.empty(),
              "an empty folder exports nothing");
        iSaveStoreDelete(0, 0, err, sizeof(err));
    }
}

static void testPS2CLI(const std::vector<U8>& canonical)
{
    char err[512] = "";
    std::string root = at("saves-ps2cli");
    removeTree(root);
    iSaveStoreSetRoot(root.c_str());
    iSaveStoreWrite(0, 0, &canonical[0], canonical.size(), err, sizeof(err));
    iSaveStoreWrite(0, 2, &canonical[0], canonical.size(), err, sizeof(err));
    sConfigPs2Icon = "";

    std::string out = at("ps2cli");
    removeTree(out);
    std::string psu = out + "/one.psu";
    std::string card = out + "/card.ps2";

    const char* a0[] = { "--save-dir", root.c_str(), "--export-save", "A1", psu.c_str(), "--ps2",
                         "psu" };
    check(cli(std::vector<std::string>(a0, a0 + 7)) == 0 && iHostPathExists(psu.c_str()),
          "--export-save A1 FILE.psu --ps2 psu writes the .psu");
    check(cli(std::vector<std::string>(a0, a0 + 7)) == 1,
          "the same again fails: the save is already there");

    const char* a1[] = { "--save-dir", root.c_str(), "--export-save", "A1", psu.c_str(), "--ps2",
                         "psu", "--as", "2", "--pal" };
    check(cli(std::vector<std::string>(a1, a1 + 10)) == 1,
          "--pal into an NTSC .psu fails: it is another folder");
    check(cli(std::vector<std::string>(a1, a1 + 9)) == 0, "--as 2 adds it as the second save");

    const char* a2[] = { "--save-dir", root.c_str(), "--export-save", "A1", psu.c_str(), "--ps2",
                         "sideways" };
    check(cli(std::vector<std::string>(a2, a2 + 7)) == 2, "--ps2 takes psu, folder, card or auto");

    const char* a3[] = { "--save-dir", root.c_str(), "--export-save", "A1", psu.c_str(), "--ps2",
                         "psu", "--xbox", "emu" };
    check(cli(std::vector<std::string>(a3, a3 + 9)) == 2, "--ps2 and --xbox together are refused");

    const char* a4[] = { "--save-dir", root.c_str(), "--check-save", psu.c_str() };
    check(cli(std::vector<std::string>(a4, a4 + 4)) == 0, "--check-save lists a .psu's saves");

    const char* a5[] = { "--save-dir", root.c_str(), "--import-save", psu.c_str(), "--slot", "B" };
    std::vector<U8> got;
    check(cli(std::vector<std::string>(a5, a5 + 6)) == 0 &&
              iSaveStoreRead(1, 0, got, err, sizeof(err)) && got == canonical &&
              iSaveStoreRead(1, 1, got, err, sizeof(err)) && got == canonical,
          "--import-save FILE.psu --slot B puts its two saves in B1 and B2");

    const char* a6[] = { "--save-dir", root.c_str(), "--export-save", "all", card.c_str(), "--ps2",
                         "auto" };
    std::vector<U8> image;
    iSavePS2Folder folder;
    check(cli(std::vector<std::string>(a6, a6 + 7)) == 0 && readFile(card, image) &&
              iSavePS2CardCheck(&image[0], image.size(), NULL, err, sizeof(err)) &&
              iSavePS2CardReadFolder(&image[0], image.size(), IPS2_FOLDER_NTSC, &folder, err,
                                     sizeof(err)) &&
              folder.files.size() == 5,
          "--export-save all NEW.ps2 --ps2 auto makes a card holding folder A's saves");

    const char* a7[] = { "--save-dir", root.c_str(), "--export-save", "B2", card.c_str(), "--ps2",
                         "card", "--replace" };
    check(cli(std::vector<std::string>(a7, a7 + 8)) == 0 &&
              iHostPathExists((card + ".bak").c_str()),
          "--ps2 card --replace into that card copies it to .bak first");

    const char* a8[] = { "--save-dir", root.c_str(), "--export-save", "A1", (out + "/x.psu").c_str(),
                         "--ps2", "psu", "--ps2-icon", at("ps2/nowhere").c_str() };
    std::string x = out + "/x.psu";
    std::string nowhere = at("ps2/nowhere");
    a8[4] = x.c_str();
    a8[8] = nowhere.c_str();
    check(cli(std::vector<std::string>(a8, a8 + 9)) == 1 && !iHostPathExists(x.c_str()),
          "--ps2-icon naming a place with no icon fails and writes nothing");
}

// The player's own PlayStation 2 files and saves, when they are there.
static void testPS2PlayersFiles(const char* xboxDir, const char* savesDir, const char* ps2Dir)
{
    char err[400] = "";
    std::vector<U8> discIcon;

    if (ps2Dir != NULL && ps2Dir[0] != '\0' && iHostPathExists(ps2Dir))
    {
        std::string from;
        bool ok = iSaveStorePS2Icon(ps2Dir, discIcon, &from, err, sizeof(err));
        check(ok && !from.empty() && discIcon.size() == 83544,
              "the player's PlayStation 2 PLAT.HIP holds the game's icon, 83,544 bytes");
    }
    else
    {
        printf("skipped: no PlayStation 2 files\n");
    }

    if (xboxDir != NULL && xboxDir[0] != '\0' && iHostPathExists(xboxDir))
    {
        std::vector<U8> icon;
        std::string from;
        iSaveStorePS2Icon(xboxDir, icon, &from, err, sizeof(err));
        printf("the Xbox files hold the PlayStation 2 icon: %s\n", from.empty() ? "no" : "yes");
    }

    if (savesDir == NULL || savesDir[0] == '\0' || !iHostPathExists(savesDir))
    {
        return;
    }

    std::vector<std::string> names;
    iHostDir* d = iHostDirOpen(savesDir);
    for (const char* n = d ? iHostDirNext(d) : NULL; n != NULL; n = iHostDirNext(d))
    {
        names.push_back(n);
    }
    if (d != NULL)
    {
        iHostDirClose(d);
    }

    std::vector<U8> sys;
    iSavePS2IconSys(sys);
    if (xboxDir != NULL && xboxDir[0] != '\0')
    {
        iSaveStoreSetAssetRoot(xboxDir);
    }

    S32 done = 0;
    for (size_t i = 0; i < names.size(); i++)
    {
        std::vector<U8> data;
        std::vector<iSavePS2Folder> folders;
        std::string kind;
        if (!readFile(std::string(savesDir) + "/" + names[i], data) || data.empty())
        {
            continue;
        }
        S32 r = iSaveStorePS2Folders(&data[0], data.size(), folders, &kind, err, sizeof(err));
        if (r == 0)
        {
            continue;
        }
        done++;

        char what[240];
        snprintf(what, sizeof(what), "real PlayStation 2 save %s (%s) is read: the game's folder",
                 names[i].c_str(), kind.c_str());
        check(r > 0 && folders.size() == 1 && folders[0].name == IPS2_FOLDER_NTSC, what);
        if (r <= 0 || folders.size() != 1)
        {
            printf("  (%s)\n", err);
            continue;
        }
        const iSavePS2Folder& f = folders[0];

        const iSavePS2File* s = iSavePS2Find(&f, IPS2_ICON_SYS);
        snprintf(what, sizeof(what), "%s: the game's icon.sys is byte for byte what the port writes",
                 names[i].c_str());
        check(s != NULL && s->data == sys, what);

        const iSavePS2File* fill = iSavePS2Find(&f, IPS2_FOLDER_NTSC);
        bool tail = fill != NULL && fill->data.size() == IPS2_FILLER_SIZE;
        for (size_t k = 640; tail && k < fill->data.size(); k++)
        {
            tail = fill->data[k] == 0xBF;
        }
        snprintf(what, sizeof(what), "%s: the filler is 992 bytes ending in 0xBF from byte 640",
                 names[i].c_str());
        check(tail, what);

        const iSavePS2File* icon = iSavePS2Find(&f, IPS2_ICON_FILE);
        snprintf(what, sizeof(what), "%s: its icon is a well-formed icon%s", names[i].c_str(),
                 discIcon.empty() ? "" : ", the same as the disc's");
        check(icon != NULL && iSavePS2IconCheck(&icon->data[0], icon->data.size(), NULL, 0) &&
                  (discIcon.empty() || icon->data == discIcon),
              what);

        // The save itself: there and back, and through a .psu and a card.
        for (size_t k = 0; k < f.files.size(); k++)
        {
            if (iSavePS2GameOfName(f.files[k].name.c_str()) < 0)
            {
                continue;
            }
            const std::vector<U8>& le = f.files[k].data;
            std::vector<U8> canonical;
            std::vector<U8> back;
            bool ok = iSaveStoreFromLE(&le[0], le.size(), canonical, NULL, err, sizeof(err)) &&
                      iSavePayloadInspect(&canonical[0], canonical.size(), NULL, err, sizeof(err)) &&
                      iSaveStoreToLE(&canonical[0], canonical.size(), back, NULL, err, sizeof(err)) &&
                      back == le;
            snprintf(what, sizeof(what), "%s: %s converts to a save the game loads, and back",
                     names[i].c_str(), f.files[k].name.c_str());
            if (!ok)
            {
                printf("  (%s)\n", err);
            }
            check(ok, what);
        }

        std::vector<U8> psu;
        iSavePS2PsuBuild(&f, psu);
        iSavePS2Folder again;
        std::vector<U8> card;
        iSavePS2CardFormat(card, true, 1100000000);
        iSavePS2Folder fromCard;
        snprintf(what, sizeof(what), "%s: its folder goes through a .psu and a card image whole",
                 names[i].c_str());
        check(iSavePS2PsuParse(&psu[0], psu.size(), &again, err, sizeof(err)) &&
                  sameFolder(f, again) &&
                  iSavePS2CardWriteFolder(card, &f, 1100000000, err, sizeof(err)) &&
                  iSavePS2CardCheck(&card[0], card.size(), NULL, err, sizeof(err)) &&
                  iSavePS2CardReadFolder(&card[0], card.size(), IPS2_FOLDER_NTSC, &fromCard, err,
                                         sizeof(err)) &&
                  sameFolder(f, fromCard),
              what);
    }
    printf("real PlayStation 2 save files read: %d\n", (int)done);
}

// --- PlayStation 3: .psv and .VM2 ------------------------------------------------------------

// AES-128 against FIPS 197's worked examples (Appendix B, C.1) and SP 800-38A's
// first ECB and CBC blocks. These are the standards' public test vectors.
static void testAES()
{
    struct Answer
    {
        const char* key;
        const char* plain;
        const char* cipher;
    };
    static const Answer kAnswers[] = {
        { "000102030405060708090a0b0c0d0e0f", "00112233445566778899aabbccddeeff",
          "69c4e0d86a7b0430d8cdb78070b4c55a" },
        { "2b7e151628aed2a6abf7158809cf4f3c", "3243f6a8885a308d313198a2e0370734",
          "3925841d02dc09fbdc118597196a0b32" },
        { "2b7e151628aed2a6abf7158809cf4f3c", "6bc1bee22e409f96e93d7e117393172a",
          "3ad77bb40d7a3660a89ecaf32466ef97" },
    };
    for (size_t i = 0; i < sizeof(kAnswers) / sizeof(kAnswers[0]); i++)
    {
        std::vector<U8> key = unhex(kAnswers[i].key);
        std::vector<U8> plain = unhex(kAnswers[i].plain);
        iAES128 a;
        iAES128Init(&a, &key[0]);
        U8 out[16];
        U8 back[16];
        iAES128Encrypt(&a, &plain[0], out);
        iAES128Decrypt(&a, out, back);
        char what[96];
        snprintf(what, sizeof(what), "AES-128 known answer %d: encrypts and decrypts", (int)i + 1);
        check(hex(out, 16) == kAnswers[i].cipher && memcmp(back, &plain[0], 16) == 0, what);
    }

    // CBC, as the .psv's key is made: SP 800-38A F.2.1, first block, decrypted
    // with the IV laid over it.
    {
        std::vector<U8> key = unhex("2b7e151628aed2a6abf7158809cf4f3c");
        std::vector<U8> iv = unhex("000102030405060708090a0b0c0d0e0f");
        std::vector<U8> cipher = unhex("7649abac8119b246cee98e9b12e9197d");
        iAES128 a;
        iAES128Init(&a, &key[0]);
        U8 out[16];
        iAES128Decrypt(&a, &cipher[0], out);
        for (int i = 0; i < 16; i++)
        {
            out[i] ^= iv[i];
        }
        check(hex(out, 16) == "6bc1bee22e409f96e93d7e117393172a",
              "AES-128 CBC (SP 800-38A F.2.1): the first block decrypts");
        iAES128Clear(&a);
        bool clear = true;
        for (int r = 0; r < 11; r++)
        {
            for (int k = 0; k < 16; k++)
            {
                clear = clear && a.round[r][k] == 0;
            }
        }
        check(clear, "and the expanded key can be cleared");
    }
}

// Obviously made-up key files, made here, never a real one: the text "MADE-UP
// PSV KEY n" and "MADE-UP PSV IV n" (16 bytes each).
static std::vector<U8> makePsvKeys(int n)
{
    char text[40];
    snprintf(text, sizeof(text), "MADE-UP PSV KEY%dMADE-UP PSV IV %d", n % 10, n % 10);
    return std::vector<U8>((const U8*)text, (const U8*)text + 32);
}

// The .psv signature from its public description, written out here: the
// seed and 44 zero bytes, all four blocks decrypted in CBC mode (each block's
// AES decryption with the block before it laid over it, the IV over the
// first), the first 20 bytes the key; then HMAC-SHA1 by RFC 2104's definition
// over the whole file with the signature's 20 bytes zero. `keys` NULL is 32
// zero bytes (the stand-in).
static void psvSignByDescription(const std::vector<U8>& file, const std::vector<U8>* keys,
                                 U8 out[20])
{
    std::vector<U8> zero(32, 0);
    const std::vector<U8>& k = keys != NULL ? *keys : zero;

    U8 buf[64];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, &file[8], 20);

    iAES128 a;
    iAES128Init(&a, &k[0]);
    U8 made[64];
    for (int b = 0; b < 4; b++)
    {
        iAES128Decrypt(&a, buf + 16 * b, made + 16 * b);
        const U8* before = b == 0 ? &k[16] : buf + 16 * (b - 1);
        for (int i = 0; i < 16; i++)
        {
            made[16 * b + i] ^= before[i];
        }
    }

    std::vector<U8> body = file;
    memset(&body[0x1C], 0, 20);
    hmacByDefinition(made, 20, &body[0], body.size(), out);
}

static bool psvSignedBy(const std::vector<U8>& file, const std::vector<U8>* keys)
{
    U8 sig[20];
    psvSignByDescription(file, keys, sig);
    return file.size() > 0x30 && memcmp(&file[0x1C], sig, 20) == 0;
}

// Every file under `dir` (relative paths).
static void listTree(const std::string& dir, const std::string& rel, std::vector<std::string>& out)
{
    iHostDir* d = iHostDirOpen(dir.c_str());
    if (d == NULL)
    {
        return;
    }
    std::vector<std::string> names;
    for (const char* n = iHostDirNext(d); n != NULL; n = iHostDirNext(d))
    {
        if (strcmp(n, ".") != 0 && strcmp(n, "..") != 0)
        {
            names.push_back(n);
        }
    }
    iHostDirClose(d);
    for (size_t i = 0; i < names.size(); i++)
    {
        std::string path = dir + "/" + names[i];
        std::string r = rel.empty() ? names[i] : rel + "/" + names[i];
        iHostFileInfo st;
        if (iHostStat(path.c_str(), &st) && st.is_dir)
        {
            listTree(path, r, out);
        }
        else
        {
            out.push_back(r);
        }
    }
}

static bool holds(const std::vector<U8>& hay, const U8* needle, size_t n)
{
    for (size_t i = 0; i + n <= hay.size(); i++)
    {
        if (memcmp(&hay[i], needle, n) == 0)
        {
            return true;
        }
    }
    return false;
}

// No file the port wrote under `dir` holds the key or the IV of `keys`, as
// bytes or as hex.
static void expectNoKeyFiles(const std::string& dir, const std::vector<U8>& keys, const char* what)
{
    std::vector<std::string> files;
    listTree(dir, "", files);
    std::string keyHex = hex(&keys[0], 16);
    std::string ivHex = hex(&keys[16], 16);
    bool clean = !files.empty();
    for (size_t i = 0; i < files.size(); i++)
    {
        std::vector<U8> d;
        readFile(dir + "/" + files[i], d);
        std::string lower((const char*)(d.empty() ? NULL : &d[0]), d.size());
        for (size_t k = 0; k < lower.size(); k++)
        {
            lower[k] =
                (char)((lower[k] >= 'A' && lower[k] <= 'F') ? lower[k] - 'A' + 'a' : lower[k]);
        }
        if (holds(d, &keys[0], 16) || holds(d, &keys[16], 16) ||
            lower.find(keyHex) != std::string::npos || lower.find(ivHex) != std::string::npos)
        {
            printf("  (a key's bytes are in %s)\n", files[i].c_str());
            clean = false;
        }
    }
    check(clean, what);
}

static void testPsvFormat()
{
    char err[400] = "";
    const S64 when = 1100000000;

    check(iSavePS2PsvName(IPS2_FOLDER_NTSC) == "BASLUS-2090453424D6F76696520.PSV" &&
              iSavePS2PsvName(IPS2_FOLDER_PAL) == "BESLES-5289553424D6F76696520.PSV",
          "a .psv is named as a PS3 names it: the first 12 characters, the rest in hex");

    // A folder as the game keeps it.
    iSavePS2Folder f;
    iSavePS2FolderClear(&f);
    f.name = IPS2_FOLDER_NTSC;
    f.created = when;
    f.modified = when + 9;
    std::vector<U8> filler;
    iSavePS2Filler(filler);
    std::vector<U8> sys;
    iSavePS2IconSys(sys);
    std::vector<U8> icon;
    iSavePS2OwnIcon(icon);
    std::vector<U8> save1 = pattern(51572, 7);
    std::vector<U8> save3 = pattern(40001, 8);
    f.files.push_back(ps2File(IPS2_FOLDER_NTSC, filler, when));
    f.files.push_back(ps2File(IPS2_ICON_SYS, sys, when + 1));
    f.files.push_back(ps2File(IPS2_ICON_FILE, icon, when + 2));
    f.files.push_back(ps2File("SpongeBobMovie01", save1, when + 3));
    f.files.push_back(ps2File("SpongeBobMovie03", save3, when + 9));

    std::vector<U8> psv;
    iSavePS2PsvBuild(&f, NULL, NULL, psv);
    std::vector<U8> psu;
    iSavePS2PsuBuild(&f, psu);

    size_t data = 0xA0 + 5 * 0x3C;
    bool seed = true;
    for (int i = 0; i < 20; i++)
    {
        seed = seed && psv[8 + i] == i;
    }
    check(psv.size() ==
                  data + filler.size() + sys.size() + icon.size() + save1.size() + save3.size() &&
              memcmp(&psv[0], "\0VSP", 4) == 0 && get32le(&psv[4]) == 0 && seed &&
              get32le(&psv[0x30]) == 0 && get32le(&psv[0x34]) == 0 && get32le(&psv[0x38]) == 0x2C &&
              get32le(&psv[0x3C]) == 2 && get32le(&psv[0x64]) == 5,
          "a .psv written: the magic, the seed 00..13, 0x2C, a PS2 save, five files, and "
          "nothing past the last file's bytes");
    check(get32le(&psv[0x40]) == psu.size(), "its size at 0x40 is the folder's size as a .psu");
    const U32 sysAt = (U32)(data + filler.size());
    const U32 iconAt = sysAt + (U32)sys.size();
    check(get32le(&psv[0x44]) == sysAt && get32le(&psv[0x48]) == sys.size() &&
              get32le(&psv[0x4C]) == iconAt && get32le(&psv[0x50]) == icon.size() &&
              get32le(&psv[0x54]) == iconAt && get32le(&psv[0x58]) == icon.size() &&
              get32le(&psv[0x5C]) == iconAt && get32le(&psv[0x60]) == icon.size(),
          "icon.sys and the three icons it names are where they are in the file");
    check(get32le(&psv[0x78]) == 7 && get32le(&psv[0x7C]) == IPS2_MODE_DIR &&
              memcmp(&psv[0x80], IPS2_FOLDER_NTSC, 20) == 0 && psv[0x94] == 0 &&
              iSavePS2TimeUnpack(&psv[0x68]) == when && iSavePS2TimeUnpack(&psv[0x70]) == when + 9,
          "the folder's entry: times, seven entries with . and .., its mode and name");
    {
        const U8* e = &psv[0xA0 + 3 * 0x3C];
        check(iSavePS2TimeUnpack(e) == when + 3 && iSavePS2TimeUnpack(e + 8) == when + 3 &&
                  get32le(e + 0x10) == save1.size() && get32le(e + 0x14) == IPS2_MODE_FILE &&
                  memcmp(e + 0x18, "SpongeBobMovie01", 17) == 0 &&
                  get32le(e + 0x38) == iconAt + icon.size() &&
                  memcmp(&psv[get32le(e + 0x38)], &save1[0], save1.size()) == 0,
              "a file's entry: times, size, mode, name and where its bytes are");
    }

    iSavePS2Folder back;
    check(iSavePS2PsvParse(&psv[0], psv.size(), &back, err, sizeof(err)) && sameFolder(f, back) &&
              back.files[3].created == when + 3 && back.created == when,
          "and it reads back as the same folder");
    std::vector<U8> again;
    iSavePS2PsvBuild(&back, &psv[8], NULL, again);
    check(again == psv, "written again from what was read, it is the same bytes");

    // The signature: the stand-in is the computation with every key value
    // zero; a real one with the key file's.
    check(iSavePS2PsvVerify(&psv[0], psv.size(), NULL) && psvSignedBy(psv, NULL),
          "the stand-in signature checks, and is the public description's with 32 zero bytes");

    std::vector<U8> keyFile = makePsvKeys(1);
    std::vector<U8> otherFile = makePsvKeys(2);
    iSavePS2PsvKeys keys;
    iSavePS2PsvKeys other;
    check(iSavePS2PsvKeysParse(&keyFile[0], keyFile.size(), &keys, err, sizeof(err)) &&
              memcmp(keys.key, &keyFile[0], 16) == 0 && memcmp(keys.iv, &keyFile[16], 16) == 0 &&
              iSavePS2PsvKeysParse(&otherFile[0], otherFile.size(), &other, err, sizeof(err)),
          "a 32-byte key file is the key, then the IV");

    std::vector<U8> real;
    iSavePS2PsvBuild(&f, NULL, &keys, real);
    check(memcmp(&real[0], &psv[0], 0x1C) == 0 &&
              memcmp(&real[0x30], &psv[0x30], real.size() - 0x30) == 0 &&
              memcmp(&real[0x1C], &psv[0x1C], 20) != 0,
          "signed with a made-up key file, only the 20 signature bytes differ");
    check(psvSignedBy(real, &keyFile) && iSavePS2PsvVerify(&real[0], real.size(), &keys) &&
              !iSavePS2PsvVerify(&real[0], real.size(), NULL) &&
              !iSavePS2PsvVerify(&real[0], real.size(), &other) &&
              !iSavePS2PsvVerify(&psv[0], psv.size(), &keys),
          "the made-up key file's signature is the public description's, and only its own "
          "key file checks it");

    // Another seed gives another signature; a changed byte anywhere fails.
    std::vector<U8> seed2 = pattern(20, 77);
    std::vector<U8> reseeded;
    iSavePS2PsvBuild(&f, &seed2[0], &keys, reseeded);
    check(memcmp(&reseeded[8], &seed2[0], 20) == 0 && psvSignedBy(reseeded, &keyFile) &&
              memcmp(&reseeded[0x1C], &real[0x1C], 20) != 0,
          "a seed given is written, and signed with");
    bool caught = true;
    const size_t spots[] = { 0, 8, 0x27, 0x40, 0x90, 0x200, real.size() - 1 };
    for (size_t i = 0; i < sizeof(spots) / sizeof(spots[0]); i++)
    {
        std::vector<U8> bad = real;
        bad[spots[i]] ^= 0x01;
        caught = caught && !iSavePS2PsvVerify(&bad[0], bad.size(), &keys);
    }
    check(caught, "a changed byte anywhere, the signature's own included, fails the check");
    check(!iSavePS2PsvVerify(&real[0], 0x9F, &keys) && !iSavePS2PsvVerify(NULL, 0, NULL),
          "a cut file is not checked");

    // Key files that are not one: told why, with nothing of the file in it.
    const size_t sizes[] = { 0, 1, 16, 31, 33, 64 };
    bool refused = true;
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        std::vector<U8> wrong = pattern(sizes[i] + 1, 3);
        err[0] = '\0';
        char size[32];
        snprintf(size, sizeof(size), "this file is %u", (unsigned)sizes[i]);
        bool ok = iSavePS2PsvKeysParse(&wrong[0], sizes[i], &keys, err, sizeof(err));
        refused = refused && !ok && strstr(err, "32 bytes") != NULL && strstr(err, size) != NULL;
    }
    check(refused, "a key file of 0, 1, 16, 31, 33 or 64 bytes is refused, its size the reason");
    std::vector<U8> zeros(32, 0);
    check(!iSavePS2PsvKeysParse(&zeros[0], 32, &keys, err, sizeof(err)) &&
              strstr(err, "only zeros") != NULL,
          "a key file of zeros is refused: it would be the stand-in again");
    bool cleared = true;
    for (int i = 0; i < 16; i++)
    {
        cleared = cleared && keys.key[i] == 0 && keys.iv[i] == 0;
    }
    check(cleared, "and a refused key file leaves no key values behind");
    iSavePS2PsvKeysClear(&other);
}

static void testPsvStore(const std::vector<U8>& canonical)
{
    char err[768] = "";
    std::string root = at("saves-psv");
    removeTree(root);
    iSaveStoreSetRoot(root.c_str());
    iSaveStoreSetAssetRoot(at("assets-gc").c_str());
    sConfigPs2Icon = "";
    sConfigPsvKeys = "";

    const S64 when = 1100000000;
    iSaveStoreWrite(0, 1, &canonical[0], canonical.size(), err, sizeof(err));
    char slotPath[512];
    iSaveStoreSlotPath(0, 1, slotPath, sizeof(slotPath));
    iHostSetFileTime(slotPath, when);
    std::vector<U8> le;
    iSaveStoreToLE(&canonical[0], canonical.size(), le, NULL, err, sizeof(err));

    // The made-up key files live apart from what the port writes, which is
    // looked through for their bytes at the end.
    std::string keysDir = at("psv-keys");
    removeTree(keysDir);
    iHostMakeDir(keysDir.c_str());
    std::vector<U8> keyFile = makePsvKeys(1);
    std::vector<U8> otherFile = makePsvKeys(2);
    std::string keysPath = keysDir + "/keys.bin";
    std::string otherPath = keysDir + "/other.bin";
    std::string zerosPath = keysDir + "/zeros.bin";
    writeFile(keysPath, keyFile);
    writeFile(otherPath, otherFile);
    writeFile(zerosPath, std::vector<U8>(32, 0));

    std::string out = at("psv");
    removeTree(out);
    iHostMakeDir(out.c_str());

    iSavePS2ExportOptions o;
    iSavePS2ExportDefaults(&o);
    o.kind = ISS_PS2_PSV;
    std::string where;
    std::string said; // every message, looked through for key bytes at the end

    // --- the stand-in signature
    std::string standIn = out + "/standin.psv";
    bool ok = iSaveStoreExportPS2(0, 1, &o, standIn.c_str(), &where, err, sizeof(err));
    if (!ok)
    {
        printf("  (%s)\n", err);
    }
    std::vector<U8> psv;
    iSavePS2Folder got;
    iSavePS2FolderClear(&got);
    check(ok && where == standIn && readFile(standIn, psv) &&
              iSavePS2PsvParse(&psv[0], psv.size(), &got, err, sizeof(err)) &&
              got.name == IPS2_FOLDER_NTSC && got.files.size() == 4 &&
              got.files[3].name == "SpongeBobMovie02" && got.files[3].data == le,
          "slot A2 exports as a .psv: the game's folder, the three files beside the save, and "
          "the save, the little-endian payload");
    check(got.files.size() == 4 && got.files[3].created == when && got.files[3].modified == when &&
              got.modified == when,
          "with the save's date, the folder's being its newest file's");
    check(!psv.empty() && iSavePS2PsvVerify(&psv[0], psv.size(), NULL) && psvSignedBy(psv, NULL),
          "with the stand-in signature");
    said += where;

    {
        std::string dir = out + "/dir";
        iHostMakeDir(dir.c_str());
        check(iSaveStoreExportPS2(0, 1, &o, dir.c_str(), &where, err, sizeof(err)) &&
                  where == dir + "/BASLUS-2090453424D6F76696520.PSV" &&
                  iHostPathExists(where.c_str()),
              "into a folder it gets the name a PS3 gives it");
        o.pal = true;
        check(iSaveStoreExportPS2(0, 1, &o, dir.c_str(), &where, err, sizeof(err)) &&
                  where == dir + "/BESLES-5289553424D6F76696520.PSV",
              "and the European game's for PAL");
        o.pal = false;
    }

    check(!iSaveStoreExportPS2(0, 1, &o, standIn.c_str(), &where, err, sizeof(err)) &&
              strstr(err, "already there") != NULL,
          "a save already in the .psv is not replaced unasked");
    said += err;
    o.number = 2;
    check(iSaveStoreExportPS2(0, 1, &o, standIn.c_str(), &where, err, sizeof(err)) &&
              readFile(standIn, psv) &&
              iSavePS2PsvParse(&psv[0], psv.size(), &got, err, sizeof(err)) &&
              got.files.size() == 5 && got.files[4].name == "SpongeBobMovie03" &&
              iSavePS2PsvVerify(&psv[0], psv.size(), NULL),
          "exported again as save 3, the .psv holds both, signed again");
    o.number = -1;
    o.replace = true;
    check(iSaveStoreExportPS2(0, 1, &o, standIn.c_str(), &where, err, sizeof(err)),
          "and with replacing allowed the same number goes in again");
    o.replace = false;

    // --- the real signature, from the player's file
    std::string real = out + "/real.psv";
    o.psvSignature = ISS_PSV_REAL;
    check(!iSaveStoreExportPS2(0, 1, &o, real.c_str(), &where, err, sizeof(err)) &&
              strstr(err, "PS3 key file") != NULL && !iHostPathExists(real.c_str()),
          "the real signature with no key file is refused, saying what is needed, and nothing "
          "is written");
    said += err;

    const size_t sizes[] = { 0, 16, 31, 33, 64 };
    bool refused = true;
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        char name[600];
        snprintf(name, sizeof(name), "%s/bad%u.bin", keysDir.c_str(), (unsigned)sizes[i]);
        std::vector<U8> wrong(keyFile.begin(), keyFile.end());
        wrong.resize(sizes[i], 0x5A);
        writeFile(name, wrong);
        snprintf(o.psvKeys, sizeof(o.psvKeys), "%s", name);
        err[0] = '\0';
        bool wrote = iSaveStoreExportPS2(0, 1, &o, real.c_str(), &where, err, sizeof(err));
        refused = refused && !wrote && strstr(err, "32 bytes") != NULL &&
                  strstr(err, name) != NULL && !iHostPathExists(real.c_str());
        said += err;
    }
    check(refused, "a key file of the wrong size is refused with its size as the reason, and "
                   "nothing is written");
    snprintf(o.psvKeys, sizeof(o.psvKeys), "%s", zerosPath.c_str());
    check(!iSaveStoreExportPS2(0, 1, &o, real.c_str(), &where, err, sizeof(err)) &&
              strstr(err, "only zeros") != NULL && !iHostPathExists(real.c_str()),
          "so is one of zeros");
    said += err;
    snprintf(o.psvKeys, sizeof(o.psvKeys), "%s", at("psv-keys/missing.bin").c_str());
    check(!iSaveStoreExportPS2(0, 1, &o, real.c_str(), &where, err, sizeof(err)) &&
              strstr(err, "no PS3 key file at") != NULL,
          "and one that is not there");
    said += err;

    o.psvKeys[0] = '\0';
    sConfigPsvKeys = keysPath;
    ok = iSaveStoreExportPS2(0, 1, &o, real.c_str(), &where, err, sizeof(err));
    std::vector<U8> realBytes;
    check(ok && readFile(real, realBytes) && psvSignedBy(realBytes, &keyFile) &&
              !psvSignedBy(realBytes, NULL) && !psvSignedBy(realBytes, &otherFile),
          "with config.ini's key file the .psv carries that file's signature, as the public "
          "description makes it");
    {
        std::vector<U8> a = realBytes;
        std::vector<U8> b;
        readFile(out + "/dir/BASLUS-2090453424D6F76696520.PSV", b);
        check(a.size() == b.size() && a.size() > 0x30 && memcmp(&a[0], &b[0], 0x1C) == 0 &&
                  memcmp(&a[0x30], &b[0x30], a.size() - 0x30) == 0,
              "and is the stand-in one's bytes but for the signature");
    }
    snprintf(o.psvKeys, sizeof(o.psvKeys), "%s", otherPath.c_str());
    std::string real2 = out + "/real2.psv";
    std::vector<U8> real2Bytes;
    check(iSaveStoreExportPS2(0, 1, &o, real2.c_str(), &where, err, sizeof(err)) &&
              readFile(real2, real2Bytes) && psvSignedBy(real2Bytes, &otherFile),
          "a key file named for the export goes ahead of config.ini's");
    o.psvKeys[0] = '\0';
    sConfigPsvKeys = "";
    char found[600];
    check(iSaveStorePsvKeysUsable(keysPath.c_str(), found, sizeof(found), err, sizeof(err)) &&
              keysPath == found,
          "the Saves tab's look at a key file: a usable one");
    std::string bad31 = keysDir + "/bad31.bin";
    check(!iSaveStorePsvKeysUsable(bad31.c_str(), found, sizeof(found), err, sizeof(err)) &&
              strstr(err, "32 bytes") != NULL && bad31 == found,
          "one of the wrong size, said with its path");
    said += err;
    check(!iSaveStorePsvKeys(NULL, found, sizeof(found), err, sizeof(err)) && found[0] == '\0' &&
              strstr(err, "PS3 key file") != NULL,
          "none chosen");
    said += err;

    // --- import: which signature each carries, and never refused for it
    {
        iSaveImportOptions imp;
        iSaveImportDefaults(&imp);
        imp.target = 3;
        imp.dryRun = true;
        imp.force = true;
        std::vector<iSaveImportItem> items;

        iSaveStoreImport(standIn.c_str(), &imp, items, err, sizeof(err));
        check(items.size() == 2 && items[0].ok &&
                  items[0].source.find("stand-in signature") != std::string::npos,
              "a stand-in .psv is said to be one");
        if (!items.empty())
        {
            printf("  %s\n", items[0].source.c_str());
            said += items[0].source + items[0].message;
        }

        iSaveStoreImport(real.c_str(), &imp, items, err, sizeof(err));
        check(items.size() == 1 && items[0].ok &&
                  items[0].source.find("signature unknown") != std::string::npos,
              "a real-signed one, with no key file to tell by, is unknown");
        if (!items.empty())
        {
            printf("  %s\n", items[0].source.c_str());
            said += items[0].source + items[0].message;
        }

        imp.psvKeys = keysPath.c_str();
        iSaveStoreImport(real.c_str(), &imp, items, err, sizeof(err));
        check(items.size() == 1 && items[0].ok &&
                  items[0].source.find("real signature (matches your PS3 key file)") !=
                      std::string::npos,
              "with its key file, it is said to be real");
        if (!items.empty())
        {
            said += items[0].source + items[0].message;
        }

        imp.psvKeys = otherPath.c_str();
        iSaveStoreImport(real.c_str(), &imp, items, err, sizeof(err));
        check(items.size() == 1 && items[0].ok &&
                  items[0].source.find("signature not recognised") != std::string::npos,
              "with another key file, not recognised");

        imp.psvKeys = bad31.c_str();
        iSaveStoreImport(real.c_str(), &imp, items, err, sizeof(err));
        check(items.size() == 1 && items[0].ok &&
                  items[0].source.find("signature unknown") != std::string::npos &&
                  items[0].source.find("cannot be used") != std::string::npos &&
                  items[0].source.find("32 bytes") != std::string::npos,
              "with a key file of the wrong size, unknown, and why the file cannot serve");
        if (!items.empty())
        {
            printf("  %s\n", items[0].source.c_str());
            said += items[0].source + items[0].message;
        }

        sConfigPsvKeys = keysPath;
        imp.psvKeys = NULL;
        iSaveStoreImport(real.c_str(), &imp, items, err, sizeof(err));
        check(items.size() == 1 && items[0].source.find("real signature") != std::string::npos,
              "config.ini's key file serves too");
        sConfigPsvKeys = "";

        // A damaged signature: read all the same, byte for byte.
        std::vector<U8> bad = realBytes;
        bad[0x20] ^= 0xFF;
        std::string badPath = out + "/badsig.psv";
        writeFile(badPath, bad);
        imp.dryRun = false;
        imp.psvKeys = keysPath.c_str();
        std::vector<U8> back;
        ok = iSaveStoreImport(badPath.c_str(), &imp, items, err, sizeof(err));
        check(ok && items.size() == 1 && items[0].ok &&
                  items[0].source.find("signature not recognised") != std::string::npos &&
                  iSaveStoreRead(3, 1, back, err, sizeof(err)) && back == canonical,
              "a .psv whose signature is wrong still imports, byte for byte");
        iSaveSlotInfo info;
        iSaveStoreSlot(3, 1, &info);
        check(info.mtime >= when - 2 && info.mtime <= when + 2, "and keeps its date");
        iSaveStoreDelete(3, 1, err, sizeof(err));
    }

    // --- by the destination's name, and a folder's three in one .psv
    {
        iSavePS2ExportOptions a;
        iSavePS2ExportDefaults(&a);
        std::string autoPath = out + "/auto.psv";
        std::vector<U8> bytes;
        check(iSaveStoreExportPS2(0, 1, &a, autoPath.c_str(), &where, err, sizeof(err)) &&
                  readFile(autoPath, bytes) && iSavePS2PsvIs(&bytes[0], bytes.size()) &&
                  iSavePS2PsvVerify(&bytes[0], bytes.size(), NULL),
              "a new name ending .psv becomes a .psv with the stand-in signature");
        a.number = 0;
        check(iSaveStoreExportPS2(0, 1, &a, autoPath.c_str(), &where, err, sizeof(err)) &&
                  readFile(autoPath, bytes) &&
                  iSavePS2PsvParse(&bytes[0], bytes.size(), &got, err, sizeof(err)) &&
                  got.files.size() == 5,
              "and an existing .psv is added to");

        iSaveStoreWrite(0, 0, &canonical[0], canonical.size(), err, sizeof(err));
        iSaveStoreWrite(0, 2, &canonical[0], canonical.size(), err, sizeof(err));
        std::vector<iSaveExportItem> items;
        std::string all = out + "/all.psv";
        a.number = -1;
        a.kind = ISS_PS2_PSV;
        S32 n = iSaveStoreExportPS2Folder(0, &a, all.c_str(), items, &where, err, sizeof(err));
        if (n != 3)
        {
            printf("  (%s)\n", err);
        }
        check(n == 3 && readFile(all, bytes) &&
                  iSavePS2PsvParse(&bytes[0], bytes.size(), &got, err, sizeof(err)) &&
                  got.files.size() == 6 && iSavePS2PsvVerify(&bytes[0], bytes.size(), NULL),
              "a folder's three saves go into one .psv");
    }

    // --- a PS3's virtual memory card (.VM2): a card without the spare bytes
    {
        iSavePS2ExportOptions a;
        iSavePS2ExportDefaults(&a);
        std::string vm2 = out + "/card.VM2";
        std::vector<U8> card;
        iSavePS2Folder f;
        ok = iSaveStoreExportPS2(0, 1, &a, vm2.c_str(), &where, err, sizeof(err));
        check(ok && readFile(vm2, card) && card.size() == 8388608 &&
                  iSavePS2CardIs(&card[0], card.size()) &&
                  iSavePS2CardCheck(&card[0], card.size(), NULL, err, sizeof(err)) &&
                  iSavePS2CardReadFolder(&card[0], card.size(), IPS2_FOLDER_NTSC, &f, err,
                                         sizeof(err)) &&
                  iSavePS2Find(&f, "SpongeBobMovie02") != NULL &&
                  iSavePS2Find(&f, "SpongeBobMovie02")->data == le,
              "a new name ending .vm2 makes a formatted 8 MB card without spare bytes, holding "
              "the save");
        a.replace = true;
        check(iSaveStoreExportPS2(0, 1, &a, vm2.c_str(), &where, err, sizeof(err)) &&
                  iHostPathExists((vm2 + ".bak").c_str()) && readFile(vm2, card) &&
                  card.size() == 8388608,
              "written to again it is copied first, and stays without spare bytes");

        iSaveImportOptions imp;
        iSaveImportDefaults(&imp);
        imp.target = 4;
        std::vector<iSaveImportItem> items;
        std::vector<U8> back;
        check(iSaveStoreImport(vm2.c_str(), &imp, items, err, sizeof(err)) && items.size() == 1 &&
                  iSaveStoreRead(4, 1, back, err, sizeof(err)) && back == canonical,
              "and imports back byte for byte");
        iSaveStoreDelete(4, 1, err, sizeof(err));
    }

    expectNoKeys(said.c_str(), keyFile, "no message about a .psv holds a key file's bytes");
    expectNoKeys(said.c_str(), otherFile, "nor the other key file's");
    expectNoKeyFiles(out, keyFile, "no file written holds the made-up key or IV");
    expectNoKeyFiles(out, otherFile, "nor the other key file's");
}

static void testPsvCLI(const std::vector<U8>& canonical)
{
    char err[512] = "";
    std::string root = at("saves-psvcli");
    removeTree(root);
    iSaveStoreSetRoot(root.c_str());
    iSaveStoreWrite(0, 0, &canonical[0], canonical.size(), err, sizeof(err));
    sConfigPs2Icon = "";
    sConfigPsvKeys = "";

    std::string out = at("psvcli");
    removeTree(out);
    iHostMakeDir(out.c_str());
    std::vector<U8> keyFile = makePsvKeys(3);
    std::string keys = at("psv-keys/cli.bin");
    std::string bad = at("psv-keys/cli31.bin");
    writeFile(keys, keyFile);
    writeFile(bad, std::vector<U8>(keyFile.begin(), keyFile.begin() + 31));
    std::vector<U8> bytes;

    std::string one = out + "/one.psv";
    const char* a0[] = { "--save-dir", root.c_str(), "--export-save", "A1",
                         one.c_str(),  "--psv",      "standin" };
    check(cli(std::vector<std::string>(a0, a0 + 7)) == 0 && readFile(one, bytes) &&
              iSavePS2PsvVerify(&bytes[0], bytes.size(), NULL),
          "--export-save A1 FILE.psv --psv standin writes a stand-in .psv");

    std::string two = out + "/two.psv";
    const char* a1[] = { "--save-dir", root.c_str(), "--export-save", "A1", two.c_str(),
                         "--ps2",      "psv" };
    check(cli(std::vector<std::string>(a1, a1 + 7)) == 0 && readFile(two, bytes) &&
              iSavePS2PsvVerify(&bytes[0], bytes.size(), NULL),
          "--ps2 psv is the same");

    std::string three = out + "/three.psv";
    const char* a2[] = { "--save-dir", root.c_str(), "--export-save", "A1",       three.c_str(),
                         "--psv",      "real",       "--psv-keys",    bad.c_str() };
    check(cli(std::vector<std::string>(a2, a2 + 7)) == 1 && !iHostPathExists(three.c_str()),
          "--psv real with no key file fails and writes nothing");
    check(cli(std::vector<std::string>(a2, a2 + 9)) == 1 && !iHostPathExists(three.c_str()),
          "--psv real with a 31-byte key file fails and writes nothing");
    a2[8] = keys.c_str();
    check(cli(std::vector<std::string>(a2, a2 + 9)) == 0 && readFile(three, bytes) &&
              psvSignedBy(bytes, &keyFile),
          "--psv real --psv-keys FILE signs with that file");

    const char* a3[] = { "--save-dir", root.c_str(), "--export-save", "A1",
                         one.c_str(),  "--psv",      "sideways" };
    check(cli(std::vector<std::string>(a3, a3 + 7)) == 2, "--psv takes standin or real");
    const char* a4[] = { "--save-dir", root.c_str(), "--export-save", "A1",  one.c_str(),
                         "--psv",      "standin",    "--ps2",         "card" };
    check(cli(std::vector<std::string>(a4, a4 + 9)) == 2, "--psv with --ps2 card is refused");
    std::string unused = out + "/unused.psv";
    const char* a4b[] = { "--save-dir", root.c_str(), "--export-save", "A1",
                          unused.c_str(), "--psv", "standin", "--psv-keys", keys.c_str() };
    check(cli(std::vector<std::string>(a4b, a4b + 9)) == 2 && !iHostPathExists(unused.c_str()),
          "--psv-keys with --psv standin is refused, nothing written");
    a4b[5] = "--ps2";
    a4b[6] = "auto";
    check(cli(std::vector<std::string>(a4b, a4b + 9)) == 2 && !iHostPathExists(unused.c_str()),
          "so is --psv-keys with --ps2 auto to a .psv (the stand-in)");

    const char* a5[] = { "--save-dir",  root.c_str(), "--check-save",
                         three.c_str(), "--psv-keys", keys.c_str() };
    check(cli(std::vector<std::string>(a5, a5 + 6)) == 0, "--check-save FILE.psv --psv-keys says "
                                                          "what it holds");
    const char* a6[] = { "--save-dir", root.c_str(), "--import-save", three.c_str(),
                         "--slot",     "B",          "--psv-keys",    bad.c_str() };
    std::vector<U8> got;
    check(cli(std::vector<std::string>(a6, a6 + 8)) == 0 &&
              iSaveStoreRead(1, 0, got, err, sizeof(err)) && got == canonical,
          "--import-save FILE.psv --slot B imports it, whatever the key file given");

    std::string dir = out + "/dir";
    iHostMakeDir(dir.c_str());
    const char* a7[] = { "--save-dir", root.c_str(), "--export-save", "all",
                         dir.c_str(),  "--psv",      "standin" };
    check(cli(std::vector<std::string>(a7, a7 + 7)) == 0 &&
              iHostPathExists((dir + "/BASLUS-2090453424D6F76696520.PSV").c_str()),
          "--export-save all FOLDER --psv standin writes the PS3's file name in it");

    std::string vm2 = out + "/new.vm2";
    const char* a8[] = { "--save-dir", root.c_str(), "--export-save", "A1",
                         vm2.c_str(),  "--ps2",      "auto" };
    check(cli(std::vector<std::string>(a8, a8 + 7)) == 0 && readFile(vm2, bytes) &&
              bytes.size() == 8388608,
          "--export-save A1 NEW.vm2 makes a PS3 virtual memory card");
    expectNoKeyFiles(out, keyFile, "no file the command line wrote holds the made-up key");
}

// The player's own .psv files, when they are there: written again from what
// was read, and exported again after importing, they come back the same but
// for the signature.
static void testPsvPlayersFiles(const char* xboxDir, const char* savesDir)
{
    if (savesDir == NULL || savesDir[0] == '\0' || !iHostPathExists(savesDir))
    {
        return;
    }
    char err[400] = "";
    std::vector<std::string> names;
    listTree(savesDir, "", names);
    S32 done = 0;

    for (size_t i = 0; i < names.size(); i++)
    {
        std::string path = std::string(savesDir) + "/" + names[i];
        std::vector<U8> data;
        if (!readFile(path, data) || !iSavePS2PsvIs(data.empty() ? NULL : &data[0], data.size()))
        {
            continue;
        }
        done++;
        char what[300];

        iSavePS2Folder f;
        bool ok = iSavePS2PsvParse(&data[0], data.size(), &f, err, sizeof(err));
        std::vector<U8> again;
        if (ok)
        {
            iSavePS2PsvBuild(&f, &data[8], NULL, again);
        }
        size_t differ = 0;
        for (size_t k = 0; ok && k < data.size() && k < again.size(); k++)
        {
            differ += (k >= 0x1C && k < 0x30) ? 0 : (data[k] != again[k]);
        }
        snprintf(what, sizeof(what),
                 "real .psv %s: written again from what was read, every byte is the same but "
                 "the 20 of the signature",
                 names[i].c_str());
        check(ok && again.size() == data.size() && differ == 0, what);

        bool pcSeed = true;
        for (int k = 0; k < 20; k++)
        {
            pcSeed = pcSeed && data[8 + k] == k;
        }
        printf("real .psv %s: its seed is 00..13 as PC tools write it: %s\n", names[i].c_str(),
               pcSeed ? "yes" : "no");

        iSaveStorePsvLookWith(NULL);
        std::string kind = iSaveStorePsvSignature(&data[0], data.size());
        printf("real .psv %s: %s\n", names[i].c_str(), kind.c_str());
        snprintf(what, sizeof(what), "real .psv %s: its signature is not the stand-in one",
                 names[i].c_str());
        check(kind.find("signature unknown") == 0, what);

        if (xboxDir == NULL || xboxDir[0] == '\0' || !iHostPathExists(xboxDir))
        {
            continue;
        }

        // Through the store: import it, and export it again.
        std::string root = at("saves-psvreal");
        removeTree(root);
        iSaveStoreSetRoot(root.c_str());
        iSaveStoreSetAssetRoot(xboxDir);
        std::string out = at("psv-real");
        removeTree(out);
        iHostMakeDir(out.c_str());

        iSaveImportOptions imp;
        iSaveImportDefaults(&imp);
        imp.target = 0;
        std::vector<iSaveImportItem> items;
        ok = iSaveStoreImport(path.c_str(), &imp, items, err, sizeof(err));
        snprintf(what, sizeof(what), "real .psv %s imports", names[i].c_str());
        check(ok && !items.empty(), what);
        if (!ok || items.empty())
        {
            printf("  (%s)\n", err);
            continue;
        }

        // Into a copy of itself, the save replaced.
        std::string copy = out + "/copy.psv";
        writeFile(copy, data);
        iSavePS2ExportOptions o;
        iSavePS2ExportDefaults(&o);
        o.kind = ISS_PS2_PSV;
        o.replace = true;
        std::string where;
        bool wrote = true;
        for (size_t k = 0; k < items.size(); k++)
        {
            wrote = wrote && iSaveStoreExportPS2(0, items[k].game, &o, copy.c_str(), &where, err,
                                                 sizeof(err));
        }
        std::vector<U8> back;
        readFile(copy, back);
        differ = 0;
        for (size_t k = 0; k < data.size() && k < back.size(); k++)
        {
            differ += (k >= 0x1C && k < 0x30) ? 0 : (data[k] != back[k]);
        }
        snprintf(what, sizeof(what),
                 "real .psv %s: imported, then exported into a copy of itself, it is the same "
                 "file but for the signature",
                 names[i].c_str());
        check(wrote && back.size() == data.size() && differ == 0, what);

        // To a new .psv, with the game's icon out of the file itself.
        std::string fresh = out + "/fresh.psv";
        o.replace = false;
        snprintf(o.icon, sizeof(o.icon), "%s", path.c_str());
        wrote = true;
        for (size_t k = 0; k < items.size(); k++)
        {
            wrote = wrote && iSaveStoreExportPS2(0, items[k].game, &o, fresh.c_str(), &where, err,
                                                 sizeof(err));
        }
        readFile(fresh, back);

        // What may differ: the signature, the filler's text (the port's
        // lines are empty), and the times of the folder and of the files the
        // port makes (it gives them the save's).
        std::vector<bool> may(data.size(), false);
        for (size_t k = 0x1C; k < 0x30; k++)
        {
            may[k] = true;
        }
        for (size_t k = 0x68; k < 0x78; k++)
        {
            may[k] = true;
        }
        size_t saveBytes = 0;
        size_t saveDiffer = 0;
        for (size_t e = 0; e < f.files.size(); e++)
        {
            const U8* entry = &data[0xA0 + e * 0x3C];
            bool isSave = iSavePS2GameOfName(f.files[e].name.c_str()) >= 0;
            if (!isSave)
            {
                for (size_t k = 0; k < 16; k++)
                {
                    may[0xA0 + e * 0x3C + k] = true;
                }
            }
            if (f.files[e].name == IPS2_FOLDER_NTSC || f.files[e].name == IPS2_FOLDER_PAL)
            {
                U32 from = get32le(entry + 0x38);
                for (size_t k = from; k < from + f.files[e].data.size() && k < may.size(); k++)
                {
                    may[k] = true;
                }
            }
            if (isSave)
            {
                U32 from = get32le(entry + 0x38);
                for (size_t k = from; k < from + f.files[e].data.size() && k < back.size(); k++)
                {
                    saveBytes++;
                    saveDiffer += data[k] != back[k];
                }
                for (size_t k = 0; k < 16; k++)
                {
                    saveDiffer += data[0xA0 + e * 0x3C + k] != back[0xA0 + e * 0x3C + k];
                }
            }
        }
        size_t outside = 0;
        size_t inside = 0;
        for (size_t k = 0; k < data.size() && k < back.size(); k++)
        {
            if (data[k] != back[k])
            {
                (may[k] ? inside : outside)++;
            }
        }
        printf("real .psv %s: exported to a new .psv it differs in %u bytes, all in the "
               "signature, the filler's text and the times of the folder and the files beside "
               "the save (%u elsewhere)\n",
               names[i].c_str(), (unsigned)inside, (unsigned)outside);
        snprintf(what, sizeof(what),
                 "real .psv %s: exported to a new .psv, the save's bytes and times, icon.sys, the "
                 "icon and the whole layout are the PS3's",
                 names[i].c_str());
        check(wrote && back.size() == data.size() && outside == 0 && saveBytes > 0 &&
                  saveDiffer == 0,
              what);
    }
    printf("real .psv files read: %d\n", (int)done);
}

// --- the player's own files, when they are there ---------------------------------------------

static bool isPackage(const std::string& name)
{
    size_t n = name.size();
    return n > 4 && (iHostStrCaseCmp(name.c_str() + n - 4, ".hip") == 0 ||
                     iHostStrCaseCmp(name.c_str() + n - 4, ".hop") == 0);
}

static bool fileRead(void* ctx, U64 offset, void* buf, size_t n)
{
    FILE* f = (FILE*)ctx;
    return offset <= 0x7FFFFFFFu && fseek(f, (long)offset, SEEK_SET) == 0 &&
           fread(buf, 1, n, f) == n;
}

// Every package under `dir`: `out` gets the saved objects of each, by its
// path relative to the root in upper case.
static void scanTree(const std::string& dir, const std::string& rel,
                     std::vector<std::pair<std::string, iSaveObjectCounts> >& out, S32* failed)
{
    std::vector<std::string> names;
    iHostDir* d = iHostDirOpen(dir.c_str());
    if (d == NULL)
    {
        return;
    }
    for (const char* n = iHostDirNext(d); n != NULL; n = iHostDirNext(d))
    {
        names.push_back(n);
    }
    iHostDirClose(d);

    for (size_t i = 0; i < names.size(); i++)
    {
        std::string path = dir + "/" + names[i];
        std::string r = rel.empty() ? names[i] : rel + "/" + names[i];
        iHostFileInfo st;
        if (!iHostStat(path.c_str(), &st))
        {
            continue;
        }
        if (st.is_dir)
        {
            scanTree(path, r, out, failed);
        }
        else if (isPackage(names[i]))
        {
            iSaveObjectCounts c;
            iSaveObjectCountsClear(&c);
            char err[200];
            FILE* f = fopen(path.c_str(), "rb");
            bool ok = f != NULL && iSaveHipScan(fileRead, f, st.size, &c, err, sizeof(err));
            if (f != NULL)
            {
                fclose(f);
            }
            if (!ok)
            {
                (*failed)++;
                printf("  %s: %s\n", r.c_str(), err);
                continue;
            }
            for (size_t k = 0; k < r.size(); k++)
            {
                if (r[k] >= 'a' && r[k] <= 'z')
                {
                    r[k] = (char)(r[k] - 'a' + 'A');
                }
            }
            out.push_back(std::make_pair(r, c));
        }
    }
}

static void testPlayersFiles(const char* xboxDir, const char* gcDir, const char* savesDir)
{
    typedef std::vector<std::pair<std::string, iSaveObjectCounts> > Scan;
    Scan xbox;
    Scan gc;

    std::string xbePath = xboxDir != NULL && xboxDir[0] ? std::string(xboxDir) + "/DEFAULT.XBE" : "";
    std::vector<U8> xbe;
    if (!xbePath.empty() && readFile(xbePath, xbe) && !xbe.empty())
    {
        // The certificate's key field stays in the struct: nothing of it is
        // printed, compared or kept.
        char err[300] = "";
        iSaveXboxTitle title;
        check(iSaveXbeParse(&xbe[0], xbe.size(), &title, err, sizeof(err)) &&
                  strcmp(title.titleName, kTitle) == 0 && title.titleId == ISX_TITLE_ID,
              "the player's DEFAULT.XBE is The SpongeBob SquarePants Movie, title 54510027");
        check(title.titleImage.size() == 67584 && title.saveImage.size() == 4096 &&
                  memcmp(&title.titleImage[0], "XPR0", 4) == 0 &&
                  memcmp(&title.saveImage[0], "XPR0", 4) == 0,
              "its title image is 67,584 bytes and its save image 4,096, both XPR0");
        std::vector<U8> meta;
        iSaveXboxMeta("TitleName", title.titleName, meta);
        check(meta.size() == 88, "its TitleMeta.xbx comes to the game's 88 bytes");
        memset(title.certificateField, 0, sizeof(title.certificateField));
    }
    else
    {
        printf("skipped: no DEFAULT.XBE to read (%s)\n", xbePath.empty() ? "no Xbox folder" :
                                                                           xbePath.c_str());
    }

    for (int which = 0; which < 2; which++)
    {
        const char* dir = which ? gcDir : xboxDir;
        Scan& scan = which ? gc : xbox;
        if (dir == NULL || dir[0] == '\0' || !iHostPathExists(dir))
        {
            printf("skipped: no %s folder to scan\n", which ? "GameCube" : "Xbox");
            continue;
        }

        S32 failed = 0;
        scanTree(dir, "", scan, &failed);
        check(failed == 0 && scan.size() > 100,
              which ? "every GameCube package scans" : "every Xbox package scans");

        // Only the levels' own packages and MNUC hold objects the game saves:
        // the ones that stay loaded beside a level (boot, font, plat, the
        // menus, the players) hold none, which is why they are not read.
        S32 levels = 0;
        S32 strays = 0;
        S32 taskCounters = -1;
        U32 mnucCounters = 0;
        for (size_t i = 0; i < scan.size(); i++)
        {
            const std::string& r = scan[i].first;
            const iSaveObjectCounts& c = scan[i].second;
            U32 any = 0;
            for (int k = 0; k < ISLE_KINDS; k++)
            {
                any += c.n[k];
            }

            bool mnuc = (r == "MN/MNUC.HIP");
            // A level's own HIP: XX/XXnn.HIP.
            bool level = r.size() == 11 && r[2] == '/' && r[0] == r[3] && r[1] == r[4] &&
                         r.compare(7, 4, ".HIP") == 0;
            if (mnuc)
            {
                taskCounters = c.taskCounters;
                mnucCounters = c.n[ISLE_COUNTER];
            }
            else if (any != 0 && level)
            {
                levels++;
            }
            else if (any != 0 || c.taskCounters >= 0)
            {
                strays++;
                printf("  %s holds saved objects\n", r.c_str());
            }
        }
        check(strays == 0 && levels > 20,
              which ? "GameCube: only level packages and MNUC hold saved objects" :
                      "Xbox: only level packages and MNUC hold saved objects");
        check(taskCounters == 114 && mnucCounters == 1,
              which ? "GameCube: MNUC has the 114 task counters and one saved counter" :
                      "Xbox: MNUC has the 114 task counters and one saved counter");
    }

    if (!xbox.empty() && !gc.empty())
    {
        // Both discs save the same things in every level.
        S32 compared = 0;
        S32 different = 0;
        for (size_t i = 0; i < xbox.size(); i++)
        {
            for (size_t k = 0; k < gc.size(); k++)
            {
                if (xbox[i].first == gc[k].first)
                {
                    compared++;
                    if (memcmp(xbox[i].second.n, gc[k].second.n, sizeof(gc[k].second.n)) != 0)
                    {
                        different++;
                        printf("  %s differs between the discs\n", gc[k].first.c_str());
                    }
                }
            }
        }
        check(compared > 100 && different == 0,
              "the Xbox and GameCube packages agree on what every level saves");
    }

    // Real saves: each converts and comes back byte for byte, with either
    // disc's packages, and both give the same little-endian bytes.
    if (savesDir != NULL && savesDir[0] != '\0' && iHostPathExists(savesDir))
    {
        std::vector<std::string> names;
        iHostDir* d = iHostDirOpen(savesDir);
        for (const char* n = d ? iHostDirNext(d) : NULL; n != NULL; n = iHostDirNext(d))
        {
            names.push_back(n);
        }
        if (d != NULL)
        {
            iHostDirClose(d);
        }

        S32 done = 0;
        for (size_t i = 0; i < names.size(); i++)
        {
            std::vector<U8> data;
            if (!readFile(std::string(savesDir) + "/" + names[i], data) || data.size() < 16)
            {
                continue;
            }
            bool le = iSavePayloadIsLE(&data[0], data.size());
            if (!le && iSaveGetU32(&data[0]) != ISF_TAG('G', 'D', 'A', 'T'))
            {
                continue;
            }

            std::vector<U8> first[2];
            bool ok = true;
            char err[400] = "";
            S32 used = 0;
            for (int which = 0; which < 2 && ok; which++)
            {
                const char* dir = which ? gcDir : xboxDir;
                if (dir == NULL || dir[0] == '\0' || !iHostPathExists(dir))
                {
                    continue;
                }
                iSaveStoreSetAssetRoot(dir);
                std::vector<U8> back;
                size_t size = le ? iSavePayloadLESize(&data[0]) : iSaveGetU32(&data[8]);
                std::string notes;
                ok = (le ? iSaveStoreFromLE(&data[0], data.size(), first[which], &notes, err,
                                            sizeof(err)) :
                           iSaveStoreToLE(&data[0], data.size(), first[which], &notes, err,
                                          sizeof(err))) &&
                     (le ? iSaveStoreToLE(&first[which][0], first[which].size(), back, NULL, err,
                                          sizeof(err)) :
                           iSaveStoreFromLE(&first[which][0], first[which].size(), back, NULL,
                                            err, sizeof(err))) &&
                     back.size() == size && memcmp(&back[0], &data[0], size) == 0;
                if (!notes.empty() && which == 0)
                {
                    printf("  %s: %s\n", names[i].c_str(), notes.c_str());
                }
                used++;
            }
            if (used == 2 && ok)
            {
                ok = first[0] == first[1];
            }
            if (used > 0)
            {
                done++;
                char what[200];
                snprintf(what, sizeof(what), "real save %s converts there and back byte for byte",
                         names[i].c_str());
                if (!ok)
                {
                    printf("  (%s)\n", err);
                }
                check(ok, what);
            }
        }
        printf("real saves converted: %d\n", (int)done);
    }
    else
    {
        printf("skipped: no folder of real saves\n");
    }
}

// --- zips as tools pack them, and what the first players' tries turned up ---------------------

static U32 zipCrc(const std::vector<U8>& d)
{
    U32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < d.size(); i++)
    {
        c ^= d[i];
        for (int k = 0; k < 8; k++)
        {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
    }
    return ~c;
}

struct ZipIn
{
    std::string name;
    std::vector<U8> packed; // as it lies in the zip
    std::vector<U8> plain;
    U32 method;
};

// A zip written field by field, with entries packed any way the test likes
// (the port's own writer only stores).
static std::vector<U8> zipOf(const std::vector<ZipIn>& in)
{
    std::vector<U8> out;
    std::vector<U8> central;
    for (size_t i = 0; i < in.size(); i++)
    {
        U32 offset = (U32)out.size();
        U32 crc = zipCrc(in[i].plain);
        for (int pass = 0; pass < 2; pass++)
        {
            std::vector<U8>& to = pass == 0 ? out : central;
            put32(to, pass == 0 ? 0x04034B50 : 0x02014B50, false);
            if (pass == 1)
            {
                put16(to, 20, false);
            }
            put16(to, 20, false);
            put16(to, 0, false);
            put16(to, in[i].method, false);
            put16(to, 0, false);
            put16(to, 0x21, false); // 1980-01-01
            put32(to, crc, false);
            put32(to, (U32)in[i].packed.size(), false);
            put32(to, (U32)in[i].plain.size(), false);
            put16(to, (U32)in[i].name.size(), false);
            put16(to, 0, false);
            if (pass == 1)
            {
                put16(to, 0, false);
                put16(to, 0, false);
                put16(to, 0, false);
                put32(to, 0, false);
                put32(to, offset, false);
            }
            to.insert(to.end(), in[i].name.begin(), in[i].name.end());
        }
        append(out, in[i].packed);
    }

    U32 cdOffset = (U32)out.size();
    append(out, central);
    put32(out, 0x06054B50, false);
    put16(out, 0, false);
    put16(out, 0, false);
    put16(out, (U32)in.size(), false);
    put16(out, (U32)in.size(), false);
    put32(out, (U32)central.size(), false);
    put32(out, cdOffset, false);
    put16(out, 0, false);
    return out;
}

// `d` as a deflate stream of stored blocks: valid deflate, no packing.
static std::vector<U8> deflateStored(const std::vector<U8>& d)
{
    std::vector<U8> out;
    size_t at = 0;
    do
    {
        size_t n = d.size() - at < 40000 ? d.size() - at : 40000;
        out.push_back(at + n == d.size() ? 1 : 0);
        put16(out, (U32)n, false);
        put16(out, (U32)n ^ 0xFFFF, false);
        out.insert(out.end(), d.begin() + at, d.begin() + at + n);
        at += n;
    } while (at < d.size());
    return out;
}

static ZipIn zipEntry(const char* name, const std::vector<U8>& plain, const std::vector<U8>& packed,
                      U32 method)
{
    ZipIn e;
    e.name = name;
    e.plain = plain;
    e.packed = packed;
    e.method = method;
    return e;
}

static void testZipAndFixes(const std::vector<U8>& canonical)
{
    char err[768] = "";

    // --- inflate: the two kinds of packed block, from made-up text. The
    // packed bytes are what zlib makes of the text (level 9, raw).
    {
        static const char* const kWords[8] = { "sponge", "bob",  "movie", "save",
                                               "slot",   "xbox", "card",  "goofy goober" };
        std::string text;
        for (int i = 0; i < 160; i++)
        {
            text += kWords[((i * i + 3 * i) / 5) % 8];
            text += " ";
        }
        static const U8 kDynamic[] = {
            0xed, 0x4f, 0x41, 0x0e, 0x80, 0x30, 0x08, 0xfb, 0xca, 0xbe, 0x36, 0x74, 0x2e, 0x26,
            0x6a, 0x8d, 0x98, 0x65, 0xfe, 0x5e, 0x19, 0x8b, 0x8e, 0x2f, 0x98, 0x5d, 0x4a, 0x68,
            0xa1, 0x50, 0xde, 0xb1, 0xc5, 0xe0, 0x58, 0xcb, 0x8a, 0x34, 0x3f, 0x8d, 0x4f, 0xc1,
            0x65, 0x42, 0xb6, 0xf4, 0xe0, 0x8f, 0xd1, 0x11, 0x48, 0xa5, 0x86, 0xe3, 0x05, 0x67,
            0x11, 0x22, 0x30, 0x5d, 0x82, 0x14, 0x8e, 0x46, 0x52, 0x10, 0x53, 0x85, 0x97, 0x2b,
            0x23, 0x66, 0x49, 0x5c, 0x3e, 0x49, 0x6f, 0x94, 0x73, 0x22, 0x34, 0x5c, 0x7d, 0x4c,
            0x9f, 0x14, 0x53, 0x43, 0xd7, 0x62, 0x9c, 0x4d, 0xc3, 0x3d, 0x75, 0x4f, 0xfd, 0xeb,
            0xd4, 0x37,
        };
        static const U8 kFixed[] = { 0x0b, 0x2e, 0xc8, 0xcf, 0x4b, 0x4f, 0x75, 0xca, 0x4f, 0x52,
                                     0x08, 0xc6, 0xc2, 0xf2, 0xcd, 0x2f, 0xcb, 0x4c, 0x05, 0x00 };

        std::vector<U8> plainA = bytesOf(text.c_str());
        std::vector<U8> plainB = bytesOf("SpongeBob SpongeBob SpongeBob Movie");
        std::vector<U8> packedA(kDynamic, kDynamic + sizeof(kDynamic));
        std::vector<U8> packedB(kFixed, kFixed + sizeof(kFixed));

        std::vector<ZipIn> in;
        in.push_back(zipEntry("a/dynamic.txt", plainA, packedA, 8));
        in.push_back(zipEntry("a/fixed.txt", plainB, packedB, 8));
        in.push_back(zipEntry("a/stored-blocks.bin", canonical, deflateStored(canonical), 8));
        in.push_back(zipEntry("a/stored.txt", plainB, plainB, 0));
        std::vector<U8> zip = zipOf(in);

        std::vector<iSaveZipEntry> got;
        bool ok = iSaveZipParse(&zip[0], zip.size(), got, err, sizeof(err));
        if (!ok)
        {
            printf("  (%s)\n", err);
        }
        check(ok && got.size() == 4 && got[0].data == plainA,
              "a zip entry packed with its own codes unpacks to the text");
        check(ok && got.size() == 4 && got[1].data == plainB,
              "one packed with the fixed codes, repeats and all, unpacks");
        check(ok && got.size() == 4 && got[2].data == canonical && got[3].data == plainB,
              "stored blocks inside a packed entry, and a stored entry, read as before");

        // Damage is refused, never read past.
        std::vector<ZipIn> bad = in;
        bad[0].packed[40] ^= 0x10;
        zip = zipOf(bad);
        check(!iSaveZipParse(&zip[0], zip.size(), got, err, sizeof(err)) &&
                  strstr(err, "damaged zip") != NULL,
              "a packed entry with a flipped bit is refused as damaged");

        bad = in;
        bad[0].packed.resize(50);
        zip = zipOf(bad);
        check(!iSaveZipParse(&zip[0], zip.size(), got, err, sizeof(err)) &&
                  strstr(err, "damaged zip") != NULL,
              "one that ends early is refused as damaged");

        bool held = true;
        // (Not the last byte: its top bits are past the end of the stream.)
        for (size_t cut = 0; cut + 1 < packedA.size(); cut++)
        {
            for (int bit = 0; bit < 8; bit += 3)
            {
                bad = in;
                bad[0].packed[cut] ^= (U8)(1 << bit);
                bad[0].plain = plainA; // the CRC is of the true text
                zip = zipOf(bad);
                held = held && !iSaveZipParse(&zip[0], zip.size(), got, err, sizeof(err));
            }
        }
        check(held, "and so is every other single flipped bit tried, without a crash");

        bad = in;
        bad[1].method = 12;
        zip = zipOf(bad);
        check(!iSaveZipParse(&zip[0], zip.size(), got, err, sizeof(err)) &&
                  strstr(err, "a/fixed.txt") != NULL && strstr(err, "extract the zip") != NULL &&
                  strstr(err, ".gci") == NULL,
              "a way of packing the port does not read is named, with what to do instead");
    }

    // --- the store
    std::string root = at("saves-fix");
    removeTree(root);
    removeTree(at("fix"));
    iSaveStoreSetRoot(root.c_str());
    iSaveStoreSetAssetRoot(at("assets-gc").c_str());
    for (S32 g = 0; g < ISS_GAMES; g++)
    {
        iSaveStoreWrite(0, g, &canonical[0], canonical.size(), err, sizeof(err));
    }

    std::vector<U8> keys = makeKeys();
    std::string xbePath = at("fix/xbox/default.xbe");
    std::string keysPath = at("fix/xbox/keys.bin");
    writeFile(xbePath, makeXbe(ISX_TITLE_ID));
    writeFile(keysPath, keys);
    sConfigXbe = "";
    sConfigKeys = "";

    // A wrong file is told before any export: the Saves tab asks this.
    {
        char found[600] = "";
        check(iSaveStoreXboxProgramUsable(xbePath.c_str(), found, sizeof(found), err, sizeof(err)) &&
                  found[0] != '\0',
              "the game's Xbox program is found usable");

        std::string other = at("fix/xbox/other.xbe");
        writeFile(other, makeXbe(0x12345678));
        err[0] = '\0';
        check(!iSaveStoreXboxProgramUsable(other.c_str(), found, sizeof(found), err, sizeof(err)) &&
                  found[0] != '\0' && err[0] != '\0',
              "another game's program is found, and refused with the reason");

        writeFile(other, bytesOf("not a program at all"));
        check(!iSaveStoreXboxProgramUsable(other.c_str(), found, sizeof(found), err, sizeof(err)) &&
                  found[0] != '\0',
              "a file that is no Xbox program is refused");

        check(iSaveStoreXboxKeysUsable(keysPath.c_str(), found, sizeof(found), err, sizeof(err)),
              "a 32-byte keys file with a certificate key is found usable");

        std::string wrong = at("fix/xbox/eeprom.bin");
        writeFile(wrong, std::vector<U8>(256, 0x5A));
        err[0] = '\0';
        check(!iSaveStoreXboxKeysUsable(wrong.c_str(), found, sizeof(found), err, sizeof(err)) &&
                  found[0] != '\0' && strstr(err, "32 bytes") != NULL &&
                  strstr(err, "256") != NULL,
              "a file of another size picked as keys.bin is refused, with both sizes");

        writeFile(wrong, std::vector<U8>(keys.begin(), keys.begin() + 31));
        check(!iSaveStoreXboxKeysUsable(wrong.c_str(), found, sizeof(found), err, sizeof(err)),
              "a 31-byte keys file is refused");
        expectNoKeys(err, keys, "and the reason holds no key bytes");

        std::vector<U8> zeros(32, 0);
        memcpy(&zeros[0], &keys[0], 16);
        writeFile(wrong, zeros);
        check(!iSaveStoreXboxKeysUsable(wrong.c_str(), found, sizeof(found), err, sizeof(err)),
              "a keys file with no certificate key is refused");
    }

    // The files to tell a signature by can come with the import.
    iSaveXboxExportOptions xo;
    iSaveXboxExportDefaults(&xo);
    snprintf(xo.xbe, sizeof(xo.xbe), "%s", xbePath.c_str());
    std::string whereEmu;
    std::string whereReal;
    bool ok = iSaveStoreExportXbox(0, 1, &xo, at("fix/emu").c_str(), &whereEmu, err, sizeof(err));
    xo.signature = ISS_XBOX_REAL;
    snprintf(xo.keys, sizeof(xo.keys), "%s", keysPath.c_str());
    ok = ok &&
         iSaveStoreExportXbox(0, 1, &xo, at("fix/real").c_str(), &whereReal, err, sizeof(err));
    check(ok, "(a save with each signature to look at)");

    std::vector<U8> xsv;
    std::vector<U8> meta;
    readFile(whereEmu + "/GameData.xsv", xsv);
    readFile(whereEmu + "/SaveMeta.xbx", meta);

    {
        iSaveImportOptions imp;
        iSaveImportDefaults(&imp);
        imp.dryRun = true;
        imp.force = true;
        std::vector<iSaveImportItem> items;

        iSaveStoreImport(whereReal.c_str(), &imp, items, err, sizeof(err));
        check(items.size() == 1 &&
                  items[0].source.find("signature not checked") != std::string::npos,
              "with no files named and none in config, the signature is not checked");

        imp.xboxXbe = xbePath.c_str();
        iSaveStoreImport(whereReal.c_str(), &imp, items, err, sizeof(err));
        check(items.size() == 1 &&
                  items[0].source.find("signature not recognised") != std::string::npos,
              "with the program named for the import, a real one is not recognised yet");

        imp.xboxKeys = keysPath.c_str();
        iSaveStoreImport(whereReal.c_str(), &imp, items, err, sizeof(err));
        check(items.size() == 1 && items[0].ok &&
                  items[0].source.find("real signature (matches your keys.bin)") !=
                      std::string::npos,
              "with the keys file named too, it is reported as real");
        expectNoKeys(items.empty() ? "" : items[0].source.c_str(), keys,
                     "and the report holds no key bytes");

        iSaveStoreImport(whereEmu.c_str(), &imp, items, err, sizeof(err));
        check(items.size() == 1 &&
                  items[0].source.find("emulator signature") != std::string::npos,
              "and the emulator's as the emulator's");

        imp.xboxXbe = NULL;
        imp.xboxKeys = NULL;
        iSaveStoreImport(whereReal.c_str(), &imp, items, err, sizeof(err));
        check(items.size() == 1 &&
                  items[0].source.find("signature not checked") != std::string::npos,
              "the files named for one import are not kept for the next");

        std::string wrong = at("fix/xbox/eeprom.bin");
        const char* a0[] = { "--save-dir",   root.c_str(), "--check-save",   whereReal.c_str(),
                             "--xbe",        xbePath.c_str(), "--keys",      keysPath.c_str() };
        check(cli(std::vector<std::string>(a0, a0 + 8)) == 0,
              "--check-save FOLDER --xbe --keys is taken");
        const char* a1[] = { "--save-dir",   root.c_str(), "--check-save",   whereReal.c_str(),
                             "--xbe",        xbePath.c_str(), "--keys",      wrong.c_str() };
        check(cli(std::vector<std::string>(a1, a1 + 8)) == 0,
              "a keys file that cannot serve is said, and the save still reads");
    }

    // A damaged GameData.xsv is still called an Xbox save.
    {
        iSaveImportOptions imp;
        iSaveImportDefaults(&imp);
        imp.dryRun = true;
        imp.force = true;
        std::vector<iSaveImportItem> items;

        writeFile(at("fix/cut/7B317197822E/GameData.xsv"),
                  std::vector<U8>(xsv.begin(), xsv.begin() + xsv.size() / 2));
        iSaveStoreImport(at("fix/cut").c_str(), &imp, items, err, sizeof(err));
        check(items.size() == 1 && !items[0].ok &&
                  items[0].source.find("an Xbox save") != std::string::npos &&
                  items[0].source.find("PlayStation") == std::string::npos &&
                  items[0].message.find("cut short") != std::string::npos,
              "a GameData.xsv cut short is an Xbox save that is cut short");

        writeFile(at("fix/empty/7B317197822E/GameData.xsv"), std::vector<U8>());
        iSaveStoreImport(at("fix/empty").c_str(), &imp, items, err, sizeof(err));
        check(items.size() == 1 && !items[0].ok &&
                  items[0].source.find("an Xbox save") != std::string::npos,
              "an empty one is an Xbox save too, and refused");

        writeFile(at("fix/nosig/7B317197822E/GameData.xsv"),
                  std::vector<U8>(xsv.begin(), xsv.end() - 5));
        iSaveStoreImport(at("fix/nosig").c_str(), &imp, items, err, sizeof(err));
        check(items.size() == 1 && items[0].ok &&
                  items[0].source.find("an Xbox save") != std::string::npos &&
                  items[0].source.find("no signature") != std::string::npos,
              "one whose signature is cut reads, as an Xbox save with no signature");

        std::vector<U8> flipped = xsv;
        flipped[0x200] ^= 0x40;
        writeFile(at("fix/flipped/7B317197822E/GameData.xsv"), flipped);
        iSaveStoreImport(at("fix/flipped").c_str(), &imp, items, err, sizeof(err));
        check(items.size() == 1 && !items[0].ok &&
                  items[0].source.find("note:") == std::string::npos,
              "a save refused for its checksum carries its reason and no note beside it");
    }

    // A GameData.xsv picked itself brings only its own folder.
    {
        iSaveImportOptions imp;
        iSaveImportDefaults(&imp);
        imp.dryRun = true;
        imp.force = true;
        std::vector<iSaveImportItem> items;
        std::string loose = at("fix/loose");
        writeFile(loose + "/GameData.xsv", xsv);
        writeFile(loose + "/SaveMeta.xbx", meta);
        writeFile(loose + "/below/7B317197822D/GameData.xsv", xsv);

        iSaveStoreImport((loose + "/GameData.xsv").c_str(), &imp, items, err, sizeof(err));
        check(items.size() == 1 && items[0].ok && items[0].game == 1,
              "a GameData.xsv named itself imports alone, as the save its SaveMeta.xbx says");
        iSaveStoreImport(loose.c_str(), &imp, items, err, sizeof(err));
        check(items.size() == 2, "its folder, when that is what is named, brings both");
    }

    // A zip as Windows or 7-Zip makes one of a save's folder.
    {
        std::vector<ZipIn> in;
        in.push_back(zipEntry("7B317197822E/", std::vector<U8>(), std::vector<U8>(), 0));
        in.push_back(zipEntry("7B317197822E/GameData.xsv", xsv, deflateStored(xsv), 8));
        in.push_back(zipEntry("7B317197822E/SaveMeta.xbx", meta, deflateStored(meta), 8));
        std::vector<U8> zip = zipOf(in);

        iSaveImportOptions imp;
        iSaveImportDefaults(&imp);
        imp.target = 3;
        std::vector<iSaveImportItem> items;
        ok = iSaveStoreImportData(&zip[0], zip.size(), "save.zip", &imp, items, err, sizeof(err));
        std::vector<U8> got;
        check(ok && items.size() == 1 && items[0].game == 1 &&
                  iSaveStoreRead(3, 1, got, err, sizeof(err)) && got == canonical,
              "a packed zip of an Xbox save's folder imports, byte for byte");
        iSaveStoreDelete(3, 1, err, sizeof(err));
    }

    // A folder's three to a PlayStation 2 when one is already there: that one
    // is left, with its own reason, and the others go in.
    {
        iSavePS2ExportOptions po;
        iSavePS2ExportDefaults(&po);
        std::string where;
        std::string psu = at("fix/ps2/some.psu");
        ok = iSaveStoreExportPS2(0, 1, &po, psu.c_str(), &where, err, sizeof(err));

        std::vector<iSaveExportItem> items;
        S32 n = iSaveStoreExportPS2Folder(0, &po, psu.c_str(), items, &where, err, sizeof(err));
        std::vector<U8> bytes;
        iSavePS2Folder got;
        check(ok && n == 2 && items.size() == 3 && items[0].ok && !items[1].ok && items[2].ok &&
                  items[1].message.find("SpongeBobMovie02 is already there") != std::string::npos,
              "the save already there is left with its own reason; the other two are written");
        check(readFile(psu, bytes) &&
                  iSavePS2PsuParse(&bytes[0], bytes.size(), &got, err, sizeof(err)) &&
                  iSavePS2Find(&got, "SpongeBobMovie01") != NULL &&
                  iSavePS2Find(&got, "SpongeBobMovie02") != NULL &&
                  iSavePS2Find(&got, "SpongeBobMovie03") != NULL,
              "and the .psu holds all three");

        n = iSaveStoreExportPS2Folder(0, &po, psu.c_str(), items, &where, err, sizeof(err));
        check(n == 0 && items.size() == 3 && !items[0].ok && !items[2].ok &&
                  items[0].message.find("SpongeBobMovie01 is already there") !=
                      std::string::npos &&
                  items[2].message.find("SpongeBobMovie03 is already there") != std::string::npos,
              "with all three there, each is named for itself and nothing is written");

        po.replace = true;
        n = iSaveStoreExportPS2Folder(0, &po, psu.c_str(), items, &where, err, sizeof(err));
        check(n == 3 && items.size() == 3 && items[0].ok && items[1].ok && items[2].ok,
              "with replacing allowed all three are written");
    }

    // The card's backup copies do not pile up when the card has not changed
    // since the newest one.
    {
        iSavePS2ExportOptions po;
        iSavePS2ExportDefaults(&po);
        std::string where;
        std::string card = at("fix/ps2/card.ps2");
        ok = iSaveStoreExportPS2(0, 0, &po, card.c_str(), &where, err, sizeof(err));
        ok = ok && iSaveStoreExportPS2(0, 1, &po, card.c_str(), &where, err, sizeof(err));
        std::vector<U8> before;
        check(ok && readFile(card + ".bak", before) && !iHostPathExists((card + ".bak2").c_str()),
              "(a card, and the copy of it from before the second save went in)");

        // The card put back from that copy, and the export done again.
        writeFile(card, before);
        ok = iSaveStoreExportPS2(0, 1, &po, card.c_str(), &where, err, sizeof(err));
        check(ok && !iHostPathExists((card + ".bak2").c_str()) &&
                  sameFile(card + ".bak", before),
              "a card that is still what the newest copy holds gets no second copy");

        ok = iSaveStoreExportPS2(0, 2, &po, card.c_str(), &where, err, sizeof(err));
        check(ok && iHostPathExists((card + ".bak2").c_str()),
              "a card that has changed since does");
    }

    // --as is one slot's.
    {
        std::string out = at("fix/cli-all");
        const char* a0[] = { "--save-dir", root.c_str(),    "--export-save", "all", out.c_str(),
                             "--xbox",     "emu",           "--xbe",         xbePath.c_str(),
                             "--as",       "2" };
        check(cli(std::vector<std::string>(a0, a0 + 11)) == 2 && !iHostPathExists(out.c_str()),
              "--as with all is refused, not passed over");
    }

    for (S32 g = 0; g < ISS_GAMES; g++)
    {
        iSaveStoreDelete(0, g, err, sizeof(err));
    }
}

// --psv-sign / --psv-check (see the top). Only "signed", "matches", "does
// not match" or the reason a file is refused is printed.
static int psvTool(int argc, char** argv)
{
    bool sign = strcmp(argv[1], "--psv-sign") == 0;
    if (argc != (sign ? 5 : 4))
    {
        printf("usage: xsave_selftest --psv-sign KEYS|- IN OUT, or --psv-check KEYS|- IN\n");
        return 2;
    }
    char err[400] = "";
    iSavePS2PsvKeys keys;
    iSavePS2PsvKeysClear(&keys);
    bool standIn = strcmp(argv[2], "-") == 0;
    if (!standIn)
    {
        std::vector<U8> file;
        bool ok =
            readFile(argv[2], file) && iSavePS2PsvKeysParse(file.empty() ? NULL : &file[0],
                                                            file.size(), &keys, err, sizeof(err));
        if (!file.empty())
        {
            memset(&file[0], 0, file.size());
        }
        if (!ok)
        {
            printf("xsave_selftest: %s\n", err[0] != '\0' ? err : "the key file cannot be read");
            return 1;
        }
    }

    std::vector<U8> in;
    iSavePS2Folder f;
    if (!readFile(argv[3], in) || in.empty() ||
        !iSavePS2PsvParse(&in[0], in.size(), &f, err, sizeof(err)))
    {
        printf("xsave_selftest: %s\n", err[0] != '\0' ? err : "the .psv cannot be read");
        iSavePS2PsvKeysClear(&keys);
        return 1;
    }

    int r;
    if (sign)
    {
        std::vector<U8> out;
        iSavePS2PsvBuild(&f, &in[IPS2_PSV_SEED], standIn ? NULL : &keys, out);
        r = writeFile(argv[4], out) ? 0 : 1;
        printf("%s\n", r == 0 ? "signed" : "xsave_selftest: the file cannot be written");
    }
    else
    {
        r = iSavePS2PsvVerify(&in[0], in.size(), standIn ? NULL : &keys) ? 0 : 3;
        printf("%s\n", r == 0 ? "matches" : "does not match");
    }
    iSavePS2PsvKeysClear(&keys);
    return r;
}

int main(int argc, char** argv)
{
    if (argc >= 2 && (strcmp(argv[1], "--psv-sign") == 0 || strcmp(argv[1], "--psv-check") == 0))
    {
        return psvTool(argc, argv);
    }
    if (argc < 2)
    {
        printf("usage: xsave_selftest <scratch folder> [<Xbox files> [<GameCube files> "
               "[<saves> [<PlayStation 2 files>]]]]\n");
        return 2;
    }

    sDir = argv[1];
    for (size_t i = 0; i < sDir.size(); i++)
    {
        if (sDir[i] == '\\')
        {
            sDir[i] = '/';
        }
    }
    removeTree(sDir);
    iHostMakeDir(sDir.c_str());

    std::vector<U8> canonical;
    testHash();
    testNames();
    testSignature();
    testConversion(canonical);
    if (!canonical.empty())
    {
        testStore(canonical);
        testCLI(canonical);
    }
    testPS2Format();
    testAES();
    testPsvFormat();
    if (!canonical.empty())
    {
        testPS2Store(canonical);
        testPS2CLI(canonical);
        testZipAndFixes(canonical);
        testPsvStore(canonical);
        testPsvCLI(canonical);
    }
    testPlayersFiles(argc > 2 ? argv[2] : NULL, argc > 3 ? argv[3] : NULL,
                     argc > 4 ? argv[4] : NULL);
    testPS2PlayersFiles(argc > 2 ? argv[2] : NULL, argc > 4 ? argv[4] : NULL,
                        argc > 5 ? argv[5] : NULL);
    testPsvPlayersFiles(argc > 2 ? argv[2] : NULL, argc > 4 ? argv[4] : NULL);

    printf("xsave_selftest: %d checks, %d failed\n", (int)sChecks, (int)sFailures);
    return sFailures == 0 ? 0 : 1;
}
