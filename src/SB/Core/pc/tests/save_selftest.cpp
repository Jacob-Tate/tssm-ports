// save_selftest: the PC save path end to end, through the game's own save code.
//
// Compiled with the game's xsavegame.cpp, xserializer.cpp, xutil.cpp and
// xordarray.cpp, and the port's isavegame.cpp and save store, so a save made
// here is made the way the game makes one: xSGInit, the save clients,
// xSerial_svgame_register, xSGSetup/xSGProcess -- and loaded back the same way.
// The game's own clients for ROOM and PREF are in zSaveLoad.cpp, which this
// cannot link; the ones below are copies of them with fixed values.
//
// What it checks:
//   1. the bit stream a save carries is laid out as the console lays it out
//      (the shared XSER_ELEMENT_BYTE change), against a reference that
//      follows the console's algorithm;
//   2. the save file is the console's payload: big-endian, its CRC right;
//   3. the game loads it back, values and leader intact, and refuses one
//      that is damaged;
//   4. .gci export, then import, then export again, is byte for byte the same;
//      the entry and head are as gc/isavegame.cpp and the SDK write them;
//   5. a raw card image with that file (and another game's) imports;
//   6. a zip of every save carries them all to an empty folder and back;
//   7. backups rotate and restore;
//   8. the command line does the same;
//   9. what a crash during a save can leave (an empty, zeroed, cut-short or
//      damaged file, a temporary) is put right from the newest good copy at
//      the next start, a whole newer save left unrenamed goes in, and nothing
//      the game can load is lost; the load screen skips the slot choice when
//      one folder has saves, its "no saves" hook is told which folder does,
//      and the save folder can be claimed by one process only.
//
// Run: save_selftest [scratch folder]. Everything it writes is in that
// folder (default: the system temp folder).

#include <types.h>

#include "iHost.h"
#include "iSaveCLI.h"
#include "iSaveFormat.h"
#include "iSaveStore.h"
#include "iSaveZip.h"
#include "isavegame.h"
#include "xsavegame.h"
#include "xserializer.h"
#include "xutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

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

// --- the game's state, as the clients below save and load it ------------------------

struct GameState
{
    U32 points;
    S16 skill;
    F32 timer;
    U8 powerup;
    S32 sceneCounter;
    U32 bits6;
    F32 music;
    F32 sfx;
    S32 sndMode;
    S32 vibration;
    S32 subtitles;
};

static GameState sState;
static char sSceneRead[32];

// zSceneSave's shape: a "scene exists" bit, then the values, in clients
// 'PLYR' and one per scene.
static void saveSerial(const GameState& g)
{
    xSerial s;
    s.setClient('PLYR');
    s.Write_b1(1);
    s.Write(g.points);
    s.Write(g.skill);
    s.Write(g.timer);
    s.Write(g.powerup);

    s.setClient('BB01');
    s.Write_b1(1);
    s.Write(g.sceneCounter);
    s.Write_bn(g.bits6, 6);
}

static void loadSerial(GameState& g)
{
    xSerial s;
    S32 exists = 0;
    s.setClient('PLYR');
    s.Read_b1(&exists);
    check(exists == 1, "the PLYR client's first bit loads");
    s.Read(&g.points);
    s.Read(&g.skill);
    s.Read(&g.timer);
    s.Read(&g.powerup);

    exists = 0;
    s.setClient('BB01');
    s.Read_b1(&exists);
    check(exists == 1, "the scene client's first bit loads");
    s.Read(&g.sceneCounter);
    g.bits6 = 0;
    s.Read_bn(&g.bits6, 6);
}

// Copies of zSaveLoad.cpp's ROOM and PREF clients, on fixed values.
static S32 roomInfo(void*, st_XSAVEGAME_DATA*, S32* need, S32* most)
{
    *need = xSGWriteStrLen("BB01");
    *most = *need << 1;
    return 1;
}

static S32 roomProc(void*, st_XSAVEGAME_DATA* xsg, st_XSAVEGAME_WRITECONTEXT* w)
{
    return xSGWriteData(xsg, w, (char*)"BB01", 1, 4) + 1;
}

static S32 roomLoad(void*, st_XSAVEGAME_DATA* xsg, st_XSAVEGAME_READCONTEXT* r, U32, S32)
{
    char buf[32] = {};
    xSGReadData(xsg, r, buf, 1, 4);
    strcpy(sSceneRead, buf);
    return 1;
}

static S32 prefInfo(void*, st_XSAVEGAME_DATA*, S32* i, S32* j)
{
    *i = 20;
    *j = *i * 2;
    return 1;
}

static S32 prefProc(void*, st_XSAVEGAME_DATA* xsg, st_XSAVEGAME_WRITECONTEXT* w)
{
    S32 sum = 0;
    sum += xSGWriteData(xsg, w, &sState.sndMode, 1);
    sum += xSGWriteData(xsg, w, &sState.music, 1);
    sum += xSGWriteData(xsg, w, &sState.sfx, 1);
    sum += xSGWriteData(xsg, w, &sState.vibration, 1);
    sum += xSGWriteData(xsg, w, &sState.subtitles, 1);
    return sum + 1;
}

static S32 prefLoad(void*, st_XSAVEGAME_DATA* xsg, st_XSAVEGAME_READCONTEXT* r, U32, S32)
{
    xSGReadData(xsg, r, &sState.sndMode, 1);
    xSGReadData(xsg, r, &sState.music, 1);
    xSGReadData(xsg, r, &sState.sfx, 1);
    xSGReadData(xsg, r, &sState.vibration, 1);
    xSGReadData(xsg, r, &sState.subtitles, 1);
    return 1;
}

// zSaveLoad_SaveGame's sequence.
static bool gameSave(S32 target, S32 game, const char* label, S32 progress, S32 level)
{
    saveSerial(sState);

    st_XSAVEGAME_DATA* xsg = xSGInit(XSG_MODE_SAVE);
    if (xsg == NULL)
    {
        return false;
    }
    xSGTgtSelect(xsg, target);
    xSGGameSet(xsg, game);
    xSGAddSaveClient(xsg, 'ROOM', NULL, roomInfo, roomProc);
    xSGAddSaveClient(xsg, 'PREF', NULL, prefInfo, prefProc);
    xSerial_svgame_register(xsg, XSG_MODE_SAVE);

    bool ok = xSGSetup(xsg, game, (char*)label, progress, 0, level) != 0;
    if (ok)
    {
        ok = xSGProcess(xsg) != 0 && xSGAsyncStatus(xsg, 1, NULL, NULL) == XSG_ASTAT_SUCCESS;
    }
    if (ok)
    {
        ok = xSGWrapup(xsg) != 0;
    }
    xSGDone(xsg);
    return ok;
}

// zSaveLoad_LoadGame's sequence. TRUE when the game accepted the save.
static bool gameLoad(S32 target, S32 game)
{
    xSerialWipeMainBuffer();
    memset(&sState, 0, sizeof(sState));
    sSceneRead[0] = '\0';

    st_XSAVEGAME_DATA* xsg = xSGInit(XSG_MODE_LOAD);
    if (xsg == NULL)
    {
        return false;
    }
    xSGTgtSelect(xsg, target);
    xSGGameSet(xsg, game);
    xSGAddLoadClient(xsg, 'ROOM', NULL, roomLoad);
    xSGAddLoadClient(xsg, 'PREF', NULL, prefLoad);
    xSerial_svgame_register(xsg, XSG_MODE_LOAD);

    bool ok = xSGSetup(xsg) != 0;
    if (ok)
    {
        ok = xSGProcess(xsg) != 0 && xSGAsyncStatus(xsg, 1, NULL, NULL) == XSG_ASTAT_SUCCESS;
    }
    if (ok)
    {
        ok = xSGWrapup(xsg) > 0;
    }
    xSGDone(xsg);

    if (ok)
    {
        GameState loaded = sState;
        loadSerial(loaded);
        sState = loaded;
    }
    return ok;
}

// --- the console's bit stream, by its own rule ------------------------------------------

// What a big-endian xSerial::Write does: a value's bytes in memory order, which
// on the console is most significant first; each byte's bits 0..7; bit k of the
// stream is bit k%32 of word k/32.
struct RefStream
{
    std::vector<U32> words;
    S32 bits;

    RefStream() : bits(0)
    {
    }

    void bit(U32 on)
    {
        if ((size_t)(bits / 32) >= words.size())
        {
            words.push_back(0);
        }
        if (on)
        {
            words[bits / 32] |= 1u << (bits % 32);
        }
        bits++;
    }

    void bytesBE(U32 v, S32 n)
    {
        for (S32 b = n - 1; b >= 0; b--)
        {
            U8 byte = (U8)(v >> (8 * b));
            for (S32 k = 0; k < 8; k++)
            {
                bit((byte >> k) & 1);
            }
        }
    }

    void bitsLow(U32 v, S32 n)
    {
        for (S32 k = 0; k < n; k++)
        {
            bit((v >> k) & 1);
        }
    }

    // The console stores the words big-endian.
    std::vector<U8> canonical() const
    {
        std::vector<U8> out(words.size() * 4);
        for (size_t i = 0; i < words.size(); i++)
        {
            iSavePutU32(&out[i * 4], words[i]);
        }
        return out;
    }
};

static U32 floatBits(F32 f)
{
    U32 u;
    memcpy(&u, &f, 4);
    return u;
}

static const iSaveBlock* findBlock(const iSavePayloadInfo& info, U32 tag)
{
    for (size_t i = 0; i < info.blocks.size(); i++)
    {
        if (info.blocks[i].tag == tag)
        {
            return &info.blocks[i];
        }
    }
    return NULL;
}

// --- the tests ---------------------------------------------------------------------------

static void testFormatUnits()
{
    printf("- formats\n");
    fflush(stdout);
    // The CRC is xUtil_crc_update's.
    const char* text = "123456789";
    check(iSaveCRC(0xFFFFFFFFu, (const U8*)text, 9) ==
              xUtil_crc_update(0xFFFFFFFFu, (char*)text, 9),
          "iSaveCRC is the game's CRC");
    check(iSaveCRC(0xFFFFFFFFu, (const U8*)text, 9) == 0x0376E6E7u,
          "and it is CRC-32/MPEG-2 without the final xor");

    // The clock: a round trip through the console's seconds-since-2000.
    S64 t = 1792000000; // 2026-10-15
    check(iSaveUnixFromGCTime(iSaveGCTimeFromUnix(t)) == t, "the card clock round trip");

    char stamp[32];
    iSaveFormatStamp(0, false, stamp, sizeof(stamp));
    check(strcmp(stamp, "01/01/2000 00:00:00") == 0, "the NTSC date stamp");
    iSaveFormatStamp(86400 * 31 + 3661, true, stamp, sizeof(stamp));
    check(strcmp(stamp, "01.02.2000 01:01:01") == 0, "the PAL date stamp");

    // The leader between two host layouts and back: one like the console's and
    // one like 32-bit Linux's, where an S64 in a struct is 4-aligned.
    std::vector<U8> canon(0x200, 0);
    memcpy(&canon[0], "GDAT", 4);
    iSavePutU32(&canon[4], 1);
    iSavePutU32(&canon[8], 0x10 + 0xC + 0x100);
    memcpy(&canon[0x10], "LEDR", 4);
    iSavePutU32(&canon[0x14], 0x100);
    iSavePutU32(&canon[0x18], 0x6E);
    memset(&canon[0x1C], 0xBF, 0x100);
    memset(&canon[0x1C], 0, 0x58);
    strcpy((char*)&canon[0x1C], "Jellyfish Fields");
    iSavePutU32(&canon[0x1C + 0x40], 77);
    canon[0x1C + 0x50] = 5;
    memcpy(&canon[0x1C + 0x58], "--TakeMeToYourLeader--", 22);
    canon.resize(0x10 + 0xC + 0x100);
    iSavePutU32(&canon[12], iSaveCRC(0xFFFFFFFFu, &canon[16], canon.size() - 16));

    iSaveHostLayout alignedBy4 = { 0x50, 0x40, 0x44, 0x4C };
    std::vector<U8> host(canon.size());
    std::vector<U8> back(canon.size());
    check(iSavePayloadConvert(ISF_TO_HOST, &canon[0], &host[0], canon.size(), &alignedBy4),
          "a leader converts to a 4-aligned host layout");
    check(iSavePayloadConvert(ISF_TO_CANONICAL, &host[0], &back[0], host.size(), &alignedBy4),
          "and back");
    check(back == canon, "and comes back byte for byte, CRC included");

    S32 progress;
    memcpy(&progress, &host[0x1C + 0x40], 4);
    check(progress == 77 && host[0x1C + 0x4C] == 5, "the host layout has the fields where it says");

    // A CRC that fails before conversion fails after it.
    canon[0x30] ^= 1;
    iSavePayloadConvert(ISF_TO_HOST, &canon[0], &host[0], canon.size(), NULL);
    U32 hostCRC;
    memcpy(&hostCRC, &host[12], 4);
    check(hostCRC != iSaveCRC(0xFFFFFFFFu, &host[16], host.size() - 16),
          "a damaged save stays damaged in the host's byte order");

    // The port's own art is drawn, and the head carries the strings the game checks.
    std::vector<U8> head(ISF_HEAD_SIZE);
    iSaveComposeHead(&head[0], "comment", NULL, NULL);
    check(memcmp(&head[0], ISF_GAME_NAME, strlen(ISF_GAME_NAME)) == 0 &&
              memcmp(&head[ISF_FOOTER_OFFSET], ISF_FOOTER, strlen(ISF_FOOTER)) == 0 &&
              strcmp((char*)&head[0x20], "comment") == 0,
          "the head's title, comment and footer");
    check(iSaveGetU16(&head[ISF_BANNER_OFFSET]) & 0x8000, "the default banner is drawn");
    check(iSaveGetU16(&head[ISF_ICON_OFFSET + 0x400]) & 0x8000, "the default icon is drawn");

    // Zip, both ways.
    std::vector<iSaveZipEntry> entries(2);
    entries[0].name = "A/one.gci";
    entries[0].data.assign(1000, 7);
    entries[0].mtime = 0;
    entries[1].name = "two.bin";
    entries[1].data.assign(3, 9);
    entries[1].mtime = 0;
    std::vector<U8> zip;
    iSaveZipBuild(entries, zip);
    std::vector<iSaveZipEntry> parsed;
    char err[256];
    check(iSaveZipParse(&zip[0], zip.size(), parsed, err, sizeof(err)) && parsed.size() == 2 &&
              parsed[0].name == "A/one.gci" && parsed[0].data == entries[0].data &&
              parsed[1].data == entries[1].data,
          "a zip round trip");
}

static void testGameSaveAndLoad(std::vector<U8>& payloadOut)
{
    printf("- game save and load\n");
    fflush(stdout);
    sState.points = 0x12345678;
    sState.skill = (S16)0x0ABC;
    sState.timer = 1.5f;
    sState.powerup = 0xA5;
    sState.sceneCounter = -5;
    sState.bits6 = 0x2A;
    sState.sndMode = 1;
    sState.music = 0.75f;
    sState.sfx = 0.5f;
    sState.vibration = 1;
    sState.subtitles = 0;

    check(gameSave(0, 1, "Bikini Bottom", 42, 3), "the game saves to slot A2");

    std::vector<U8> file;
    check(readFile(at("saves/SpongeBobMovie02"), file), "the save file is where the game puts it");
    if (file.empty())
    {
        return;
    }
    payloadOut = file;

    check(memcmp(&file[0], "GDAT", 4) == 0, "it starts GDAT, big-endian, as on the console");

    iSavePayloadInfo info;
    char err[256] = "";
    bool ok = iSavePayloadInspect(&file[0], file.size(), &info, err, sizeof(err));
    if (!ok)
    {
        printf("      (%s)\n", err);
    }
    check(ok, "it is a whole, valid console payload");
    check(info.size == file.size(), "the header's size is the file's");
    check(strcmp(info.label, "Bikini Bottom") == 0 && info.progress == 42 && info.levelIndex == 3,
          "the leader: label, progress, level");
    check(strcmp(info.scene, "BB01") == 0, "the scene it resumes in");
    check(info.blocks.size() >= 6 && info.blocks[0].tag == ISF_TAG('L', 'E', 'D', 'R') &&
              info.blocks[1].tag == ISF_TAG('R', 'O', 'O', 'M') &&
              info.blocks[2].tag == ISF_TAG('P', 'R', 'E', 'F') &&
              info.blocks[3].tag == ISF_TAG('S', 'V', 'I', 'D') &&
              info.blocks.back().tag == ISF_TAG('S', 'F', 'I', 'L'),
          "the blocks in the game's order");
    check(info.blocks[0].used == ISF_LEADER_SIZE + ISF_LEADER_FUN_SIZE,
          "the leader block is the console's size");

    // PREF: S32 1, F32 0.75 -- big-endian.
    const iSaveBlock* pref = findBlock(info, ISF_TAG('P', 'R', 'E', 'F'));
    check(pref != NULL && iSaveGetU32(&file[pref->offset]) == 1 &&
              iSaveGetU32(&file[pref->offset + 4]) == floatBits(0.75f),
          "the PREF block's words are big-endian");

    // The PLYR bit stream, against the console's rule.
    RefStream ref;
    ref.bit(1);
    ref.bytesBE(0x12345678, 4);
    ref.bytesBE(0x0ABC, 2);
    ref.bytesBE(floatBits(1.5f), 4);
    ref.bytesBE(0xA5, 1);
    std::vector<U8> want = ref.canonical();

    const iSaveBlock* plyr = findBlock(info, 'PLYR');
    check(plyr != NULL && plyr->room == 0x148 && plyr->used == 0x148,
          "the PLYR block is the size zMain gives it");
    check(plyr != NULL && memcmp(&file[plyr->offset], &want[0], want.size()) == 0,
          "the PLYR bit stream is laid out as the console lays it out");

    RefStream scene;
    scene.bit(1);
    scene.bytesBE((U32)-5, 4);
    scene.bitsLow(0x2A, 6);
    std::vector<U8> wantScene = scene.canonical();
    const iSaveBlock* bb01 = findBlock(info, 'BB01');
    check(bb01 != NULL && bb01->room == 400 &&
              memcmp(&file[bb01->offset], &wantScene[0], wantScene.size()) == 0,
          "and the scene client's");

    // Back through the game.
    check(gameLoad(0, 1), "the game loads it back");
    check(sState.points == 0x12345678 && sState.skill == (S16)0x0ABC && sState.timer == 1.5f &&
              sState.powerup == 0xA5 && sState.sceneCounter == -5 && sState.bits6 == 0x2A,
          "every serialized value comes back");
    check(sState.sndMode == 1 && sState.music == 0.75f && sState.sfx == 0.5f &&
              sState.vibration == 1 && sState.subtitles == 0,
          "and the preferences");
    check(strcmp(sSceneRead, "BB01") == 0, "and the scene");

    // The leaders, as the save screen reads them (iSGReadLeader's short read).
    st_XSAVEGAME_DATA* xsg = xSGInit(XSG_MODE_LOAD);
    xSGTgtSelect(xsg, 0);
    const char* label = xSGGameLabel(xsg, 1);
    check(label != NULL && strcmp(label, "Bikini Bottom") == 0 && xSGGameProgress(xsg, 1) == 42 &&
              xSGGameThumbIndex(xsg, 1) == 3,
          "the save screen reads the leader");
    check(xSGGameIsEmpty(xsg, 0) && !xSGGameIsEmpty(xsg, 1), "and sees which slots are empty");
    check(iSGIsGameCorrupt(xsg->isgsess, 1) == 0, "and does not call it damaged");
    xSGDone(xsg);

    // A flipped bit: the game refuses it, as the console would.
    std::vector<U8> bad = file;
    bad[bad.size() / 2] ^= 0x10;
    writeFile(at("saves/SpongeBobMovie02"), bad);
    check(!gameLoad(0, 1), "the game refuses a save with a flipped bit");
    std::vector<U8> cut(file.begin(), file.begin() + file.size() / 2);
    writeFile(at("saves/SpongeBobMovie02"), cut);
    xsg = xSGInit(XSG_MODE_LOAD);
    xSGTgtSelect(xsg, 0);
    check(iSGIsGameCorrupt(xsg->isgsess, 1) != 0, "a cut-short save is listed as damaged");
    xSGDone(xsg);
    check(!gameLoad(0, 1), "and does not load");
    writeFile(at("saves/SpongeBobMovie02"), file);
    check(gameLoad(0, 1), "the good one loads again");
}

static void checkDentry(const std::vector<U8>& gci, S32 game, size_t payload)
{
    iSaveGCI g;
    char err[256] = "";
    check(iSaveGCIParse(&gci[0], gci.size(), &g, err, sizeof(err)), "the .gci parses");

    char name[32];
    snprintf(name, sizeof(name), "SpongeBobMovie%02d", (int)game + 1);
    size_t blocks = 2 + (payload + ISF_BLOCK_SIZE - 1) / ISF_BLOCK_SIZE;

    check(strcmp(g.gamecode, "GGVE") == 0 && strcmp(g.maker, "78") == 0 &&
              strcmp(g.filename, name) == 0,
          "its entry: game GGVE, maker 78, the slot's file name");
    check(g.dentry[6] == 0xFF && g.bannerFormat == 0x02 && g.iconAddr == 0x40 &&
              g.iconFormat == 0x0002 && g.iconSpeed == 0x0002 && g.permission == 0x04 &&
              g.commentAddr == 0 && iSaveGetU16(g.dentry + 0x3A) == 0xFFFF,
          "the banner, icon, comment and permission fields as iSGSaveFile leaves them");
    check(g.length == blocks && gci.size() == ISF_DENTRY_SIZE + blocks * ISF_BLOCK_SIZE,
          "its length: the head's two blocks and the payload's");

    std::vector<U8> p;
    check(iSaveGCIPayload(&g, p, NULL, err, sizeof(err)) && p.size() == payload,
          "its payload is the save");

    bool zeros = true;
    for (size_t i = ISF_DENTRY_SIZE + ISF_HEAD_SIZE + payload; i < gci.size(); i++)
    {
        zeros = zeros && gci[i] == 0;
    }
    check(zeros, "the block padding is zero, as iSGSaveFile's buffer is");
}

static void testGCI(const std::vector<U8>& payload)
{
    printf("- .gci export and import\n");
    fflush(stdout);
    char err[512] = "";

    std::vector<U8> gci1;
    check(iSaveStoreExportGCIData(0, 1, NULL, gci1, err, sizeof(err)), "slot A2 exports");
    if (gci1.empty())
    {
        printf("      (%s)\n", err);
        return;
    }
    checkDentry(gci1, 1, payload.size());
    writeFile(at("out/export1.gci"), gci1);

    // Exporting the same, unchanged, save again gives the same file.
    std::vector<U8> again;
    iSaveStoreExportGCIData(0, 1, NULL, again, err, sizeof(err));
    check(again == gci1, "exporting twice gives the same bytes");

    // Into slot A3, and out again.
    iSaveImportOptions o;
    iSaveImportDefaults(&o);
    o.target = 0;
    o.game = 2;
    std::vector<iSaveImportItem> items;
    bool ok = iSaveStoreImport(at("out/export1.gci").c_str(), &o, items, err, sizeof(err));
    check(ok && items.size() == 1 && items[0].ok, "the .gci imports to slot A3");

    std::vector<U8> a3;
    readFile(at("saves/SpongeBobMovie03"), a3);
    check(a3 == payload, "slot A3 now holds the same payload");

    iSaveSlotInfo info;
    iSaveStoreSlot(0, 2, &info);
    check(info.exists && info.valid && info.fromCard, "and remembers the card file it came in");

    std::vector<U8> gci3;
    iSaveStoreExportGCIData(0, 2, NULL, gci3, err, sizeof(err));
    checkDentry(gci3, 2, payload.size());
    std::vector<U8> gci3b = gci3;
    memcpy(&gci3b[8], "SpongeBobMovie02", 16);
    check(gci3b == gci1, "exported from A3 it differs from the original only in its file name");

    // Replacing a save needs --force, and keeps the old one.
    o.game = 1;
    ok = iSaveStoreImport(at("out/export1.gci").c_str(), &o, items, err, sizeof(err));
    check(!ok && items.size() == 1 && !items[0].ok, "importing over a save needs force");
    o.force = true;
    ok = iSaveStoreImport(at("out/export1.gci").c_str(), &o, items, err, sizeof(err));
    check(ok, "with force it imports over slot A2");

    // export -> import -> export: byte-stable, twice round.
    std::vector<U8> gci2;
    iSaveStoreExportGCIData(0, 1, NULL, gci2, err, sizeof(err));
    check(gci2 == gci1, "export, import, export: the same .gci");
    writeFile(at("out/export2.gci"), gci2);
    ok = iSaveStoreImport(at("out/export2.gci").c_str(), &o, items, err, sizeof(err));
    std::vector<U8> gci2b;
    iSaveStoreExportGCIData(0, 1, NULL, gci2b, err, sizeof(err));
    check(ok && gci2b == gci1, "and again");

    // A card's file keeps its own entry and head through a PC slot: a .gci made
    // elsewhere (a PAL one, other art, another date) comes back as it was.
    std::vector<U8> foreign;
    iSaveGCIBuild b;
    iSaveGCIBuildDefaults(&b);
    memcpy(b.gamecode, "GGVP", 5);
    b.game = 0;
    b.time = 123456789;
    iSaveFormatStamp(b.time, true, b.comment, sizeof(b.comment));
    b.startBlock = 21;
    b.copyTimes = 2;
    std::vector<U8> art(ISF_BANNER_SIZE, 0x5A);
    std::vector<U8> icon(ISF_ICON_SIZE, 0xC3);
    b.banner = &art[0];
    b.icon = &icon[0];
    iSaveGCIMake(&payload[0], payload.size(), &b, foreign);
    writeFile(at("out/pal.gci"), foreign);

    o.game = -1; // its own number: SpongeBobMovie01, slot A1
    ok = iSaveStoreImport(at("out/pal.gci").c_str(), &o, items, err, sizeof(err));
    check(ok && items.size() == 1 && items[0].game == 0, "a PAL .gci imports to the slot it names");
    std::vector<U8> palBack;
    iSaveStoreExportGCIData(0, 0, NULL, palBack, err, sizeof(err));
    check(palBack == foreign, "and exports back byte for byte (region, date, art and all)");

    iHostFileInfo st;
    iHostStat(at("saves/SpongeBobMovie01").c_str(), &st);
    check(iSaveGCTimeFromUnix(st.mtime) == 123456789, "the PC file carries the card's date");

    // Saved over in the game, it keeps the card's art but takes a new date.
    sState.points = 99;
    check(gameSave(0, 0, "Jellyfish Fields", 50, 1), "the game saves over the imported slot");
    std::vector<U8> resaved;
    iSaveStoreExportGCIData(0, 0, NULL, resaved, err, sizeof(err));
    iSaveGCI rg;
    iSaveGCIParse(&resaved[0], resaved.size(), &rg, NULL, 0);
    check(strcmp(rg.gamecode, "GGVP") == 0 && rg.time != 123456789 &&
              memcmp(rg.data + ISF_BANNER_OFFSET, &art[0], ISF_BANNER_SIZE) == 0 &&
              memcmp(rg.data + ISF_ICON_OFFSET, &icon[0], ISF_ICON_SIZE) == 0 &&
              rg.data[0x20 + 2] == '.',
          "re-exported: the card's region and art, a new (PAL-format) date");

    // The region on request.
    iSaveExportOptions eo;
    iSaveExportDefaults(&eo);
    memcpy(eo.gamecode, "GGVE", 5);
    std::vector<U8> ntsc;
    iSaveStoreExportGCIData(0, 0, &eo, ntsc, err, sizeof(err));
    check(memcmp(&ntsc[0], "GGVE", 4) == 0 && ntsc[ISF_DENTRY_SIZE + 0x20 + 2] == '/',
          "--region E writes GGVE and the NTSC date");

    // Not ours: refused with a reason.
    std::vector<U8> other = gci1;
    memcpy(&other[0], "GALE", 4);
    memcpy(&other[4], "01", 2);
    writeFile(at("out/other.gci"), other);
    o.game = 1;
    ok = iSaveStoreImport(at("out/other.gci").c_str(), &o, items, err, sizeof(err));
    check(!ok && items.size() == 1 &&
              items[0].message.find("not a SpongeBob Movie save") != std::string::npos,
          "another game's .gci is refused");

    std::vector<U8> broken = gci1;
    broken[ISF_DENTRY_SIZE + ISF_HEAD_SIZE + 0x40] ^= 1;
    writeFile(at("out/broken.gci"), broken);
    ok = iSaveStoreImport(at("out/broken.gci").c_str(), &o, items, err, sizeof(err));
    check(!ok && items.size() == 1 && items[0].message.find("checksum") != std::string::npos,
          "a .gci whose save fails its checksum is refused");
}

static void testCard(const std::vector<U8>& payload)
{
    printf("- card image\n");
    fflush(stdout);
    char err[512] = "";

    std::vector<U8> gci1;
    readFile(at("out/export1.gci"), gci1);

    // Another game's file first, then ours.
    std::vector<U8> other(ISF_DENTRY_SIZE + 3 * ISF_BLOCK_SIZE, 0x11);
    memset(&other[0], 0, ISF_DENTRY_SIZE);
    memcpy(&other[0], "GALE01", 6);
    strcpy((char*)&other[8], "SuperSmashBros0110290334");
    iSavePutU16(&other[0x38], 3);

    std::vector<std::vector<U8> > files;
    files.push_back(other);
    files.push_back(gci1);
    std::vector<U8> card;
    check(iSaveCardCreate(4, files, card, err, sizeof(err)) && card.size() == 64 * ISF_BLOCK_SIZE,
          "a 59-block card image is made");
    writeFile(at("out/card.raw"), card);

    std::vector<std::vector<U8> > back;
    check(iSaveCardFiles(&card[0], card.size(), back, err, sizeof(err)) && back.size() == 2,
          "its directory lists both files");

    iSaveImportOptions o;
    iSaveImportDefaults(&o);
    o.target = 1; // folder B
    std::vector<iSaveImportItem> items;
    bool ok = iSaveStoreImport(at("out/card.raw").c_str(), &o, items, err, sizeof(err));
    check(ok && items.size() == 1 && items[0].target == 1 && items[0].game == 1,
          "the card imports our save (only) to B2, the number it carries");

    std::vector<U8> b2;
    readFile(at("saves/second/SpongeBobMovie02"), b2);
    check(b2 == payload, "slot B2 holds the payload");

    // A damaged directory copy: the other one is used, as the SDK would.
    std::vector<U8> half = card;
    half[2 * ISF_BLOCK_SIZE + 100] ^= 0xFF; // the newer copy
    check(iSaveCardFiles(&half[0], half.size(), back, err, sizeof(err)) && back.size() == 2,
          "a card with one bad directory copy still reads");
    half[1 * ISF_BLOCK_SIZE + 100] ^= 0xFF;
    check(!iSaveCardFiles(&half[0], half.size(), back, err, sizeof(err)),
          "and one with both bad is refused");

    // --pick, and --slot with a card holding one of ours.
    o.target = 0;
    o.game = 2;
    o.force = true;
    o.pick = 1;
    ok = iSaveStoreImport(at("out/card.raw").c_str(), &o, items, err, sizeof(err));
    check(ok && items.size() == 1 && items[0].game == 2, "--pick 1 --slot A3 from the card");
}

static void testZip()
{
    printf("- zip\n");
    fflush(stdout);
    char err[512] = "";

    std::vector<iSaveSlotInfo> before;
    iSaveStoreList(before, true);

    S32 n = iSaveStoreExportZip(NULL, at("out/all.zip").c_str(), err, sizeof(err));
    check(n == 4, "every save goes into the zip (A1, A2, A3, B2)");

    std::vector<std::vector<U8> > payloads;
    for (size_t i = 0; i < before.size(); i++)
    {
        std::vector<U8> p;
        if (before[i].exists)
        {
            iSaveStoreRead(before[i].target, before[i].game, p, NULL, 0);
        }
        payloads.push_back(p);
    }

    // To an empty save folder ("another install"), and back.
    removeTree(at("saves"));
    std::vector<iSaveSlotInfo> empty;
    iSaveStoreList(empty, true);
    bool none = true;
    for (size_t i = 0; i < empty.size(); i++)
    {
        none = none && !empty[i].exists;
    }
    check(none, "the save folder is empty");

    iSaveImportOptions o;
    iSaveImportDefaults(&o);
    std::vector<iSaveImportItem> items;
    bool ok = iSaveStoreImport(at("out/all.zip").c_str(), &o, items, err, sizeof(err));
    check(ok && items.size() == 4, "the zip imports");

    bool same = true;
    for (size_t i = 0; i < before.size(); i++)
    {
        std::vector<U8> p;
        if (before[i].exists)
        {
            iSaveStoreRead(before[i].target, before[i].game, p, NULL, 0);
        }
        same = same && p == payloads[i];
    }
    check(same, "every slot has its save back, in the same slot");

    std::vector<U8> zip2;
    std::vector<U8> zip1;
    readFile(at("out/all.zip"), zip1);
    iSaveStoreExportZipData(NULL, zip2, err, sizeof(err));
    std::vector<iSaveZipEntry> e1;
    std::vector<iSaveZipEntry> e2;
    iSaveZipParse(&zip1[0], zip1.size(), e1, err, sizeof(err));
    iSaveZipParse(&zip2[0], zip2.size(), e2, err, sizeof(err));
    bool entriesSame = e1.size() == e2.size();
    for (size_t i = 0; entriesSame && i < e1.size(); i++)
    {
        entriesSame = e1[i].name == e2[i].name && e1[i].data == e2[i].data;
    }
    check(entriesSame, "and the zip made from them holds the same .gci files");
}

static void testBackups()
{
    printf("- backups\n");
    fflush(stdout);
    char err[512] = "";
    std::vector<U8> first;
    iSaveStoreRead(0, 1, first, NULL, 0);

    for (S32 i = 0; i < 4; i++)
    {
        sState.points = 1000 + i;
        gameSave(0, 1, "Bikini Bottom", 60 + i, 3);
    }

    std::vector<iSaveBackupInfo> backups;
    iSaveStoreBackups(0, 1, backups);
    check(backups.size() == 3 && backups[0].progress == 62 && backups[1].progress == 61 &&
              backups[2].progress == 60,
          "three backups, newest first");

    iSaveSlotInfo info;
    iSaveStoreSlot(0, 1, &info);
    check(info.progress == 63, "the slot holds the newest save");

    check(iSaveStoreRestore(0, 1, 2, err, sizeof(err)), "backup 2 restores");
    iSaveStoreSlot(0, 1, &info);
    iSaveStoreBackups(0, 1, backups);
    check(info.progress == 61 && backups[0].progress == 63,
          "it is back, and what it replaced is backup 1");

    check(iSaveStoreDelete(0, 1, err, sizeof(err)), "a slot empties");
    iSaveStoreSlot(0, 1, &info);
    check(!info.exists, "and is empty");
    check(iSaveStoreRestore(0, 1, 1, err, sizeof(err)), "and the save comes back from backup 1");
    iSaveStoreSlot(0, 1, &info);
    check(info.exists && info.progress == 61, "as it was");
}

static S32 cli(const std::vector<std::string>& args)
{
    std::vector<char*> argv;
    static char prog[] = "tssm";
    argv.push_back(prog);
    for (size_t i = 0; i < args.size(); i++)
    {
        argv.push_back((char*)args[i].c_str());
    }
    argv.push_back(NULL);
    printf("    $ tssm");
    for (size_t i = 0; i < args.size(); i++)
    {
        printf(" %s", args[i].c_str());
    }
    printf("\n");
    return iSaveCLIRunArgs((S32)args.size() + 1, &argv[0]);
}

static void testCLI()
{
    printf("- command line\n");
    fflush(stdout);
    std::vector<std::string> a;

    a.assign(1, "--saves");
    check(cli(a) == 0, "--saves");

    a.clear();
    a.push_back("--export-save");
    a.push_back("A2");
    a.push_back(at("out/cli.gci"));
    check(cli(a) == 0, "--export-save A2 file.gci");
    std::vector<U8> exported;
    std::vector<U8> direct;
    readFile(at("out/cli.gci"), exported);
    iSaveStoreExportGCIData(0, 1, NULL, direct, NULL, 0);
    check(!exported.empty() && exported == direct, "writes the slot's .gci");

    a.clear();
    a.push_back("--export-save");
    a.push_back("all");
    a.push_back(at("out/dolphin"));
    check(cli(a) == 0, "--export-save all folder");
    check(iHostPathExists(at("out/dolphin/78-GGVE-SpongeBobMovie02.gci").c_str()) &&
              iHostPathExists(at("out/dolphin/B/78-GGVE-SpongeBobMovie02.gci").c_str()),
          "with Dolphin's names, other folders beside");

    // A dry run, which does not take the save folder's claim: the import below
    // takes it for this process, and after that every import that writes is
    // refused (see the end).
    a.clear();
    a.push_back("--import-save");
    a.push_back(at("out/nothing-here.gci"));
    a.push_back("--dry-run");
    check(cli(a) == 1, "a missing file is a failure");

    a.clear();
    a.push_back("--import-save");
    a.push_back(at("out/cli.gci"));
    a.push_back("--slot");
    a.push_back("B3");
    check(cli(a) == 0, "--import-save file --slot B3");
    a.push_back("--dry-run");
    check(cli(a) == 1, "the same again is refused without --force");
    a.push_back("--force");
    check(cli(a) == 0, "--dry-run --force says it would replace it");

    a.clear();
    a.push_back("--check-save");
    a.push_back(at("out/card.raw"));
    check(cli(a) == 0, "--check-save card.raw");

    a.clear();
    a.push_back("--export-save");
    a.push_back("Z9");
    a.push_back(at("out/x.gci"));
    check(cli(a) == 2, "a bad slot is a usage error");

    a.clear();
    a.push_back("-launcher=x");
    check(cli(a) == -1, "the game's own arguments start the game");

    // The import above took the save folder's claim for this process. A second
    // claim stands for a game running on the folder (as at the end of
    // testRecovery): the commands that change the saves refuse, and the slot
    // keeps its save. The ones that only read still run.
    a.clear();
    a.push_back("--import-save");
    a.push_back(at("out/cli.gci"));
    a.push_back("--slot");
    a.push_back("B3");
    a.push_back("--force");
    check(cli(a) == 1, "--import-save is refused while the folder is held");
    a.clear();
    a.push_back("--delete-save");
    a.push_back("B3");
    check(cli(a) == 1, "--delete-save is refused while the folder is held");
    iSaveSlotInfo held;
    iSaveStoreSlot(1, 2, &held);
    check(held.exists, "and B3 keeps its save");
    a[0] = "--restore-save";
    check(cli(a) == 1, "--restore-save is refused too");
    a.assign(1, "--saves");
    check(cli(a) == 0, "--saves still lists them");
}

// The calls the overlay's Saves tab makes (gui/iGuiSaves.cpp) beyond the ones
// above: the look-only pass it opens its import dialog with, one save of a
// file into a slot of the player's choosing, the folder export, the autosave
// slot it warns about, delete and restore.
static void testSavesTab()
{
    printf("- the Saves tab's calls\n");
    fflush(stdout);
    char err[512] = "";

    // The look-only pass: where each save would go, with its level and
    // progress, and nothing written.
    std::vector<U8> before;
    readFile(at("saves/SpongeBobMovie02"), before);
    iSaveImportOptions o;
    iSaveImportDefaults(&o);
    o.dryRun = true;
    o.force = true;
    std::vector<iSaveImportItem> items;
    bool ok = iSaveStoreImport(at("out/export1.gci").c_str(), &o, items, err, sizeof(err));
    check(ok && items.size() == 1 && items[0].ok && items[0].target == 0 && items[0].game == 1,
          "a look-only pass says where a .gci goes");
    check(items.size() == 1 && !items[0].label.empty() && items[0].progress >= 0,
          "and what the save is (its level and progress)");
    std::vector<U8> after;
    readFile(at("saves/SpongeBobMovie02"), after);
    check(before == after, "and writes nothing");

    // One save of a zip into a slot chosen for it (folder C, slot 1).
    iSaveImportDefaults(&o);
    o.pick = 1;
    o.target = 2;
    o.game = 0;
    ok = iSaveStoreImport(at("out/all.zip").c_str(), &o, items, err, sizeof(err));
    check(ok && items.size() == 1 && items[0].ok && items[0].target == 2 && items[0].game == 0,
          "one save of a zip goes to the slot chosen for it");
    iSaveSlotInfo c1;
    iSaveStoreSlot(2, 0, &c1);
    check(c1.exists && c1.valid, "slot C1 holds it");

    // Every save into a folder, as Dolphin names them.
    std::vector<iSaveExportItem> exported;
    S32 n = iSaveStoreExportFolder(NULL, at("out/folder").c_str(), exported);
    S32 valid = 0;
    std::vector<iSaveSlotInfo> slots;
    iSaveStoreList(slots, true);
    for (size_t i = 0; i < slots.size(); i++)
    {
        valid += (slots[i].exists && slots[i].valid) ? 1 : 0;
    }
    bool allThere = true;
    for (size_t i = 0; i < exported.size(); i++)
    {
        allThere = allThere && (!exported[i].ok || iHostPathExists(exported[i].path.c_str()));
    }
    check(n == valid && allThere, "the folder export writes every save there is");
    bool inC = false;
    for (size_t i = 0; i < exported.size(); i++)
    {
        inC = inC || (exported[i].ok && strcmp(exported[i].name, "C1") == 0 &&
                      exported[i].path.find("/folder/C/") != std::string::npos);
    }
    check(inC, "folder C's in C/ inside it");

    // The autosave slot: none until the game has one, then the cache's.
    XSGAutoData* asg = xSGAutoSave_GetCache();
    const S32 flags = asg->flg_autodata;
    const S32 targ = asg->lastTarg;
    const S32 game = asg->lastGame;
    asg->flg_autodata = 0;
    S32 t = -1;
    S32 g = -1;
    check(!iSaveStoreAutoSaveSlot(&t, &g), "no autosave slot before the game has one");
    asg->flg_autodata = 1;
    asg->lastTarg = 0;
    asg->lastGame = 1;
    check(iSaveStoreAutoSaveSlot(&t, &g) && t == 0 && g == 1, "the autosave slot is the cache's");
    asg->flg_autodata = flags;
    asg->lastTarg = targ;
    asg->lastGame = game;

    // Delete keeps the save as backup 1, and restoring it puts it back.
    std::vector<U8> c1Before;
    readFile(c1.path, c1Before);
    check(iSaveStoreDelete(2, 0, err, sizeof(err)), "delete empties slot C1");
    iSaveStoreSlot(2, 0, &c1);
    std::vector<iSaveBackupInfo> backups;
    iSaveStoreBackups(2, 0, backups);
    check(!c1.exists && !backups.empty() && backups[0].which == 1 && backups[0].valid,
          "and keeps the save as backup 1");
    check(iSaveStoreRestore(2, 0, 1, err, sizeof(err)), "restore puts backup 1 back");
    std::vector<U8> c1After;
    readFile(c1.path, c1After);
    check(!c1After.empty() && c1After == c1Before, "the same save as before the delete");
}

static S32 sHookTarget = -2;
static S32 sHookOther = -2;

static void noGamesHook(S32 target, S32 other)
{
    sHookTarget = target;
    sHookOther = other;
}

// What a crash can leave in a save folder, and what the game finds there next
// time (iSaveStoreRecover, run on every slot by iSGStartup). In a folder of its
// own, so nothing the tests above left is in the way.
static void testRecovery()
{
    printf("- after a crash\n");
    fflush(stdout);
    iSaveStoreSetRoot(at("recover").c_str());

    char report[768] = "";
    char path[512];
    iSaveStoreSlotPath(0, 2, path, sizeof(path));
    const std::string slot = path;
    const std::string tmp = slot + ".tmp";
    const std::string bdir = at("recover/backup/");

    // A3 saved four times: the save and three backups.
    for (S32 i = 0; i < 4; i++)
    {
        sState.points = 2000 + i;
        gameSave(0, 2, "Bikini Bottom", 20 + i, 3);
    }
    std::vector<U8> good;
    std::vector<U8> backup1;
    readFile(slot, good);
    readFile(bdir + "SpongeBobMovie03.1", backup1);
    check(!good.empty() && !backup1.empty() && good != backup1, "a save and its backups");

    check(!iSaveStoreRecover(0, 2, report, sizeof(report)) && report[0] == '\0',
          "a good save is left alone");

    // Empty: a system crash after the rename, before the bytes reached the disk.
    writeFile(slot, std::vector<U8>());
    check(iSaveStoreRecover(0, 2, report, sizeof(report)), "an empty save is put right");
    std::vector<U8> now;
    readFile(slot, now);
    check(now == backup1, "from the newest good backup");
    std::vector<U8> kept;
    check(readFile(bdir + "SpongeBobMovie03.damaged", kept) && kept.empty(),
          "and the empty file is kept as backup/SpongeBobMovie03.damaged");
    check(strstr(report, "A3") != NULL && strstr(report, "backup 1") != NULL,
          "and the report says which slot and from where");
    check(gameLoad(0, 2) && sState.points == 2002, "the game loads what was put back");

    // Zeros where the save was, as long as it was; then one byte changed.
    writeFile(slot, std::vector<U8>(good.size(), 0));
    check(iSaveStoreRecover(0, 2, report, sizeof(report)), "a zeroed save is put right");
    check(iHostPathExists((bdir + "SpongeBobMovie03.damaged2").c_str()),
          "keeping it beside the first damaged one");
    std::vector<U8> flipped = good;
    flipped[flipped.size() / 2] ^= 0x40;
    writeFile(slot, flipped);
    check(iSaveStoreRecover(0, 2, report, sizeof(report)), "a save failing its checksum too");
    std::vector<iSaveBackupInfo> backups;
    iSaveStoreBackups(0, 2, backups);
    check(backups.size() == 3, "and the three backups are still there");

    // Cut short, with a whole save in the temporary beside it: the newest.
    writeFile(slot, std::vector<U8>(good.begin(), good.begin() + good.size() / 2));
    writeFile(tmp, good);
    check(iSaveStoreRecover(0, 2, report, sizeof(report)), "a save cut short is put right");
    readFile(slot, now);
    check(now == good && !iHostPathExists(tmp.c_str()),
          "from the whole save that was being written, which is used up");

    // A good save with a half-written temporary beside it: an interrupted
    // write, and the save is still the good one.
    writeFile(tmp, std::vector<U8>(good.begin(), good.begin() + 100));
    check(!iSaveStoreRecover(0, 2, report, sizeof(report)), "a good save beside a half-written "
                                                            "temporary is left alone");
    readFile(slot, now);
    check(now == good, "unchanged");
    iHostRemoveFile(tmp.c_str());

    // An emptied slot stays empty, backups or not.
    char err[256];
    iSaveStoreDelete(0, 2, err, sizeof(err));
    check(!iSaveStoreRecover(0, 2, report, sizeof(report)) && !iHostPathExists(slot.c_str()),
          "a deleted save is not brought back");

    // ...unless its first save was cut off between the write and the rename.
    writeFile(tmp, good);
    check(iSaveStoreRecover(0, 2, report, sizeof(report)), "a whole first save left as the "
                                                           "temporary is put in");
    readFile(slot, now);
    check(now == good, "as written");

    // A good save with a whole temporary beside it, no older than it: the game
    // stopped between writing a new save and renaming it into place. The new
    // one goes in, and the one it replaces is backup 1 -- already, as the
    // write made it, or made now when backup 1 is something else.
    sState.points = 2100;
    gameSave(0, 2, "Bikini Bottom", 30, 3);
    std::vector<U8> newer;
    readFile(slot, newer);
    check(newer != good && readFile(bdir + "SpongeBobMovie03.1", now) && now == good,
          "a newer save, whose write made the older one backup 1");
    writeFile(slot, good);
    writeFile(tmp, newer);
    check(iSaveStoreRecover(0, 2, report, sizeof(report)) && strstr(report, "backup 1") != NULL,
          "a whole temporary beside a good save is put in");
    readFile(slot, now);
    check(now == newer && !iHostPathExists(tmp.c_str()), "the slot holds the newer save");
    check(readFile(bdir + "SpongeBobMovie03.1", now) && now == good,
          "and the one it replaced is backup 1");
    writeFile(slot, good);
    writeFile(bdir + "SpongeBobMovie03.1", flipped);
    writeFile(tmp, newer);
    check(iSaveStoreRecover(0, 2, report, sizeof(report)), "again, with backup 1 not that save");
    std::vector<U8> b2;
    check(readFile(bdir + "SpongeBobMovie03.1", now) && now == good &&
              readFile(bdir + "SpongeBobMovie03.2", b2) && b2 == flipped,
          "the save it replaced is made backup 1 first, the old backup 1 moved down");
    writeFile(slot, good);
    std::vector<U8> older = newer;
    writeFile(tmp, older);
    iHostSetFileTime(tmp.c_str(), 1000000000); // 2001
    check(!iSaveStoreRecover(0, 2, report, sizeof(report)) && strstr(report, "older") != NULL,
          "a whole temporary older than the save is left alone");
    readFile(slot, now);
    check(now == good, "and so is the save");
    iHostRemoveFile(tmp.c_str());

    // Damaged with nothing good to put back: left for the player to see.
    iSaveStoreSlotPath(1, 0, path, sizeof(path));
    writeFile(path, flipped);
    check(!iSaveStoreRecover(1, 0, report, sizeof(report)) && strstr(report, "B1") != NULL,
          "a damaged save with nothing to put back is reported");
    readFile(path, now);
    check(now == flipped, "and left as it is");

    // The load screen's question: which of the two folders holds saves.
    check(iSGOnlyTargetWithGames() == -1, "saves in both folders: ask which");
    iHostRemoveFile(path);
    check(iSGOnlyTargetWithGames() == 0, "saves in folder A only: go to them");
    iSaveStoreDelete(0, 2, err, sizeof(err));
    writeFile(path, good);
    check(iSGOnlyTargetWithGames() == 1, "saves in folder B only: go to them");
    iHostRemoveFile(path);
    check(iSGOnlyTargetWithGames() == -1, "no saves: ask (and the game says there are none)");

    // The load screen's message for an empty folder names the slot, and the
    // other slot when that one has saves (iSGSetNoGamesHook).
    iSGSetNoGamesHook(noGamesHook);
    st_ISGSESSION* s = iSGSessionBegin(NULL, NULL, 0);
    check(iSGTgtSetActive(s, 1) == 1 && iSGSelectGameDir(s, "") == 0,
          "folder B is selected, and has no saves");
    check(sHookTarget == 1 && sHookOther == -1, "the hook is told neither folder has any");
    iSaveStoreSlotPath(0, 0, path, sizeof(path));
    writeFile(path, good);
    check(iSGSelectGameDir(s, "") == 0 && sHookTarget == 1 && sHookOther == 0,
          "with a save in folder A, the hook is told so");
    sHookTarget = -2;
    check(iSGTgtSetActive(s, 0) == 1 && iSGSelectGameDir(s, "") == 1 && sHookTarget == -2,
          "folder A has saves, and the hook is not called");
    iSGSessionEnd(s);
    iSGSetNoGamesHook(NULL);

    // One process per save folder. The second claim stands for a second copy
    // of the game, which is told who holds it (this process, here).
    U32 holder = 0;
    check(iSaveStoreClaimFolder(&holder) == 1, "the save folder is claimed");
    check(iSaveStoreClaimFolder(&holder) == 0 && holder != 0,
          "and a second claim on it is refused, naming the process that holds it");

    iSaveStoreSetRoot("");
}

int main(int argc, char** argv)
{
    char tmp[512];
    if (argc > 1)
    {
        sDir = argv[1];
    }
    else if (iHostTempDir(tmp, sizeof(tmp)))
    {
        sDir = std::string(tmp) + "/tssm_save_selftest";
    }
    else
    {
        sDir = "tssm_save_selftest";
    }
    for (size_t i = 0; i < sDir.size(); i++)
    {
        if (sDir[i] == '\\')
        {
            sDir[i] = '/';
        }
    }

    iHostMakeDir(sDir.c_str());
    removeTree(sDir);
    iHostMakeDir(at("out").c_str());
    iHostSetEnv("TSSM_SAVE_DIR", at("saves").c_str());
    printf("save_selftest in %s\n", sDir.c_str());

    static st_SERIAL_PERCID_SIZE sizes[] = { { 'PLYR', 0x148 }, { 0, 0 } };
    xSerialStartup(0x80, sizes);
    xSGStartup();

    testFormatUnits();

    std::vector<U8> payload;
    testGameSaveAndLoad(payload);
    if (!payload.empty())
    {
        testGCI(payload);
        testCard(payload);
        testZip();
        testBackups();
        testCLI();
        testSavesTab();
        testRecovery();
    }

    printf("save_selftest: %d of %d checks passed\n", (int)(sChecks - sFailures), (int)sChecks);
    return sFailures == 0 ? 0 : 1;
}
