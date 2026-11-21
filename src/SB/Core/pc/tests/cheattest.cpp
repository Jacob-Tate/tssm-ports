// tssm_cheattest: the Cheats tab's tables and state (iCheats.h) and its
// settings, without the game.
//
//   tssm_cheattest <scratch folder> [path of src/SB/Game/zGameExtras.cpp]
//
// What it checks:
//   - the button codes' table: ids, eight presses each, one bit each, which
//     ones toggle, and that only "all health upgrades" is within the bits a
//     save keeps;
//   - with the game's source given: every row against zGameExtras.cpp's
//     cheatList and key arrays, read from the source text (the running game
//     checks the same against its own table, iCheatsGame.cpp);
//   - the Extras screen's costumes and functions: four and seven, by type and
//     id and by word;
//   - the port's cheats through the real config.ini layer: off in a new file,
//     in force only after iCheatsApply, all off again by iCheatsAllPortOff,
//     and saved;
//   - the tab's requests: first in, first out, sixteen at most;
//   - the co-op state: its 24 bytes on the wire, a wrong version or a short
//     buffer refused, a joiner following the host's whatever its own settings
//     say, its requests dropped, and its own back after the release.
//
// The pad table is a stand-in here (below); the config layer is the game's own,
// over a config.ini in the scratch folder.

#include "iCheats.h"
#include "iConfig.h"
#include "iConfigTable.h"
#include "iPadBind.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

// --- stand-ins ---------------------------------------------------------------------

// One binding row: iConfig.cpp writes and reads the binding sections from the
// pad table, which brings the pad backend with it.
const iPadBindButton kPadBindButtons[] = { { "a", 0, "a", "space", NULL, 0 } };
const S32 kPadBindButtonCount = 1;

const iPadBindButton* iPadBindFind(const char* name)
{
    return strcmp(name, "a") == 0 ? &kPadBindButtons[0] : NULL;
}

const char* iPadBindDescribe(const iPadBindButton*)
{
    return NULL;
}

const char* iPadBindPadDefault(const iPadBindButton* button)
{
    return button->pad;
}

// --- checks ------------------------------------------------------------------------

static int sFailures;
static int sChecks;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        sChecks++;                                                                                 \
        if (!(cond))                                                                               \
        {                                                                                          \
            sFailures++;                                                                           \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
        }                                                                                          \
    } while (0)

static bool ReadFile(const char* path, char* out, size_t outSize)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL)
    {
        return false;
    }
    size_t n = fread(out, 1, outSize - 1, f);
    out[n] = '\0';
    fclose(f);
    return true;
}

static void TestCodes()
{
    CHECK(iCheatCodeCount() == 9);
    CHECK(iCheatCodeAt(-1) == NULL);
    CHECK(iCheatCodeAt(iCheatCodeCount()) == NULL);

    U32 all = 0;
    S32 toggles = 0;
    S32 saved = 0;
    for (S32 i = 0; i < iCheatCodeCount(); i++)
    {
        const iCheatCode* c = iCheatCodeAt(i);
        CHECK(c != NULL && c->id != NULL && c->name != NULL && c->what != NULL &&
              c->lasts != NULL);
        CHECK(iCheatCodeFind(c->id) == i);
        CHECK(strlen(c->code) == 8 && strspn(c->code, "XY") == 8);
        // One bit, and nobody else's.
        CHECK(c->flag != 0 && (c->flag & (c->flag - 1)) == 0);
        CHECK((all & c->flag) == 0);
        all |= c->flag;
        toggles += c->toggles ? 1 : 0;
        saved += (c->flag & ICHEATS_SAVED_FLAGS) != 0 ? 1 : 0;
        for (S32 j = 0; j < i; j++)
        {
            CHECK(strcmp(iCheatCodeAt(j)->code, c->code) != 0);
        }
    }
    CHECK(all == 0x1EC60100u);
    CHECK(toggles == 5);
    CHECK(saved == 1);
    CHECK(iCheatCodeFind("max_health") >= 0 &&
          iCheatCodeAt(iCheatCodeFind("max_health"))->flag == 0x100);
    CHECK(iCheatCodeFind("nothing") == -1);
}

// "U32 <name>[16] = { 0, ..., Y, X, ... };" in the source: the last eight
// entries as a code, or FALSE.
static bool SourceCode(const char* source, const char* name, char* out)
{
    char decl[96];
    snprintf(decl, sizeof(decl), "U32 %s[16] = {", name);
    const char* at = strstr(source, decl);
    if (at == NULL)
    {
        return false;
    }
    at += strlen(decl);
    char entries[16];
    S32 n = 0;
    for (; *at != '\0' && *at != '}' && n < 16; at++)
    {
        if (*at == '0' || *at == 'X' || *at == 'Y')
        {
            entries[n++] = *at;
        }
    }
    if (n != 16)
    {
        return false;
    }
    memcpy(out, entries + 8, 8);
    out[8] = '\0';
    return true;
}

static void TestSource(const char* path)
{
    static char source[131072];
    CHECK(ReadFile(path, source, sizeof(source)));

    // The source spells the GameCube's buttons as this table does.
    CHECK(strstr(source, "#define Y XPAD_BUTTON_SQUARE") != NULL);
    CHECK(strstr(source, "#define X XPAD_BUTTON_O") != NULL);

    const char* list = strstr(source, "static GECheat cheatList[");
    CHECK(list != NULL);
    if (list == NULL)
    {
        return;
    }

    S32 row = 0;
    const char* at = strchr(list, '{');
    while (at != NULL)
    {
        at = strchr(at + 1, '{');
        if (at == NULL)
        {
            break;
        }
        char name[64], callback[64];
        unsigned flag;
        int mode;
        if (sscanf(at, "{ %63[A-Za-z0-9_], %63[A-Za-z0-9_], %x, %d }", name, callback, &flag,
                   &mode) != 4)
        {
            break; // the empty row that ends the list
        }

        const iCheatCode* c = iCheatCodeAt(row);
        CHECK(c != NULL);
        if (c != NULL)
        {
            char code[9];
            CHECK(SourceCode(source, name, code));
            CHECK(strcmp(code, c->code) == 0);
            CHECK(flag == c->flag);
            CHECK(((mode & 1) != 0) == c->toggles);
        }
        row++;
    }
    CHECK(row == iCheatCodeCount());

    // What a save keeps of the cheat word.
    CHECK(strstr(source, "xser->Write(g_flg_chEnabled & 0xFFFF);") != NULL);
}

static void TestExtras()
{
    CHECK(iCheatExtraCount() == 11);
    S32 costumes = 0;
    S32 functions = 0;
    for (S32 i = 0; i < iCheatExtraCount(); i++)
    {
        const iCheatExtra* e = iCheatExtraAt(i);
        CHECK(e != NULL && e->name != NULL && e->what != NULL);
        CHECK(iCheatExtraFind(e->type, e->id) == e);
        CHECK(iCheatExtraFindWord(e->word) == e);
        costumes += e->type == 4 ? 1 : 0;
        functions += e->type == 5 ? 1 : 0;
        // A costume is put on when a level loads.
        CHECK(e->type != 4 || e->nextLevel);
    }
    CHECK(costumes == 4 && functions == 7);
    for (S32 id = 1; id <= 4; id++)
    {
        CHECK(iCheatExtraFind(4, id) != NULL);
    }
    for (S32 id = 1; id <= 7; id++)
    {
        CHECK(iCheatExtraFind(5, id) != NULL);
    }
    CHECK(iCheatExtraFind(4, 5) == NULL && iCheatExtraFind(1, 1) == NULL);
    CHECK(iCheatExtraFindWord("nothing") == NULL);
}

static void TestConfig(const char* scratch)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/config.ini", scratch);
    remove(path);

    static char env[1100];
    snprintf(env, sizeof(env), "TSSM_CONFIG=%s", path);
    putenv(env);
    iConfigLoad();

    // A new file has the section, every key off.
    static char text[131072];
    CHECK(ReadFile(path, text, sizeof(text)));
    CHECK(strstr(text, "[cheats]") != NULL);
    for (S32 i = 0; i < iCHEAT_PORT_COUNT; i++)
    {
        const iCheatPortInfo* info = iCheatPortAt((iCheatPort)i);
        CHECK(info != NULL && strncmp(info->key, "cheats.", 7) == 0);
        CHECK(strcmp(info->key + 7, info->word) == 0);
        CHECK(iCheatPortFind(info->word) == i);
        const iConfigSetting* row = iConfigTableFind(info->key);
        CHECK(row != NULL && row->kind == ICONFIG_BOOL && strcmp(row->value, "off") == 0);
        char line[96];
        snprintf(line, sizeof(line), "%s = off", info->word);
        CHECK(strstr(text, line) != NULL);
        CHECK(!iCheatsInForce((iCheatPort)i));
    }
    CHECK(iCheatPortAt(iCHEAT_PORT_COUNT) == NULL);
    CHECK(iCheatPortFind("nothing") == -1);

    iCheatsApply();
    CHECK(!iCheatsInForce(iCHEAT_NO_DAMAGE) && !iCheatsInForce(iCHEAT_FLY) &&
          !iCheatsInForce(iCHEAT_UNLOCK_EXTRAS));

    // A changed key is in force once applied, and only that key.
    iConfigSet("cheats.no_damage", "on");
    CHECK(!iCheatsInForce(iCHEAT_NO_DAMAGE));
    iCheatsApply();
    CHECK(iCheatsInForce(iCHEAT_NO_DAMAGE));
    CHECK(!iCheatsInForce(iCHEAT_FLY) && !iCheatsInForce(iCHEAT_UNLOCK_EXTRAS));
    CHECK(iConfigSave());
    CHECK(ReadFile(path, text, sizeof(text)));
    CHECK(strstr(text, "no_damage = on") != NULL);
    CHECK(strstr(text, "fly = off") != NULL);

    iConfigSet("cheats.fly", "on");
    iConfigSet("cheats.unlock_extras", "on");
    iCheatsApply();
    CHECK(iCheatsInForce(iCHEAT_FLY) && iCheatsInForce(iCHEAT_UNLOCK_EXTRAS));

    // The tab's button.
    iCheatsAllPortOff();
    for (S32 i = 0; i < iCHEAT_PORT_COUNT; i++)
    {
        CHECK(!iCheatsInForce((iCheatPort)i));
        CHECK(iConfigGetBool(iCheatPortAt((iCheatPort)i)->key, TRUE) == FALSE);
    }
    CHECK(iConfigSave());
    CHECK(ReadFile(path, text, sizeof(text)));
    CHECK(strstr(text, "no_damage = off") != NULL && strstr(text, "no_damage = on") == NULL);
    CHECK(strstr(text, "fly = off") != NULL && strstr(text, "fly = on") == NULL);
    CHECK(strstr(text, "unlock_extras = off") != NULL &&
          strstr(text, "unlock_extras = on") == NULL);
    CHECK(!iCheatsInForce((iCheatPort)-1) && !iCheatsInForce(iCHEAT_PORT_COUNT));
}

static void TestRequests()
{
    iCheatRequest r;
    CHECK(!iCheatsNextRequest(&r));
    CHECK(iCheatsRequest(iCHEAT_REQUEST_CODE, 3));
    CHECK(iCheatsRequest(iCHEAT_REQUEST_EXTRA, 7));
    CHECK(iCheatsRequest(iCHEAT_REQUEST_REFILL, 0));
    CHECK(iCheatsNextRequest(&r) && r.kind == iCHEAT_REQUEST_CODE && r.value == 3);
    CHECK(iCheatsNextRequest(&r) && r.kind == iCHEAT_REQUEST_EXTRA && r.value == 7);
    CHECK(iCheatsNextRequest(&r) && r.kind == iCHEAT_REQUEST_REFILL);
    CHECK(!iCheatsNextRequest(&r));

    S32 taken = 0;
    for (S32 i = 0; i < 40; i++)
    {
        taken += iCheatsRequest(iCHEAT_REQUEST_CODE, i) ? 1 : 0;
    }
    CHECK(taken == 16);
    S32 n = 0;
    while (iCheatsNextRequest(&r))
    {
        CHECK(r.value == n);
        n++;
    }
    CHECK(n == 16);
}

static void TestShared()
{
    iCheatsShared a;
    memset(&a, 0, sizeof(a));
    a.version = ICHEATS_SHARED_VERSION;
    a.port = 0x00000005;
    a.gameFlags = 0x12345678;
    a.extrasOn[0] = 0x80000001;
    a.extrasOn[1] = 0x00000100;
    a.pointsMultiplier = 2;

    U8 wire[ICHEATS_SHARED_BYTES + 4];
    memset(wire, 0xEE, sizeof(wire));
    iCheatsSharedPack(&a, wire);
    const U8 expect[ICHEATS_SHARED_BYTES] = { 1,    0,    0,    0,    5, 0, 0, 0, 0x78, 0x56, 0x34,
                                              0x12, 0x01, 0x00, 0x00, 0x80, 0, 1, 0, 0,   2,    0,
                                              0,    0 };
    CHECK(memcmp(wire, expect, ICHEATS_SHARED_BYTES) == 0);
    CHECK(wire[ICHEATS_SHARED_BYTES] == 0xEE);

    iCheatsShared b;
    memset(&b, 0x55, sizeof(b));
    CHECK(iCheatsSharedUnpack(wire, ICHEATS_SHARED_BYTES, &b));
    CHECK(b.version == a.version && b.port == a.port && b.gameFlags == a.gameFlags &&
          b.extrasOn[0] == a.extrasOn[0] && b.extrasOn[1] == a.extrasOn[1] &&
          b.pointsMultiplier == a.pointsMultiplier);

    iCheatsShared c;
    memset(&c, 0x55, sizeof(c));
    CHECK(!iCheatsSharedUnpack(wire, ICHEATS_SHARED_BYTES - 1, &c));
    wire[0] = 2;
    CHECK(!iCheatsSharedUnpack(wire, ICHEATS_SHARED_BYTES, &c));
    CHECK(c.port == 0x55555555u);

    // A joiner: its own settings say fly, the host says no damage and extras.
    iConfigSet("cheats.fly", "on");
    iCheatsApply();
    CHECK(iCheatsInForce(iCHEAT_FLY) && !iCheatsFollowing() && iCheatsSharedFollowed() == NULL);
    CHECK(iCheatsRequest(iCHEAT_REQUEST_CODE, 1));

    iCheatsSharedSet(&b);
    CHECK(iCheatsFollowing());
    CHECK(iCheatsInForce(iCHEAT_NO_DAMAGE) && !iCheatsInForce(iCHEAT_FLY) &&
          iCheatsInForce(iCHEAT_UNLOCK_EXTRAS));
    iCheatRequest r;
    CHECK(!iCheatsNextRequest(&r));                    // dropped with the switch-over
    CHECK(!iCheatsRequest(iCHEAT_REQUEST_CODE, 1));    // and none taken while following
    CHECK(!iCheatsRequest(iCHEAT_REQUEST_REFILL, 0));
    const iCheatsShared* followed = iCheatsSharedFollowed();
    CHECK(followed != NULL && followed->gameFlags == 0x12345678 &&
          followed->pointsMultiplier == 2);

    // The joiner's own settings can change meanwhile; they wait.
    iConfigSet("cheats.fly", "off");
    iConfigSet("cheats.unlock_extras", "off");
    iCheatsApply();
    CHECK(iCheatsInForce(iCHEAT_NO_DAMAGE) && iCheatsInForce(iCHEAT_UNLOCK_EXTRAS));

    // The host changes its mind.
    b.port = 0;
    iCheatsSharedSet(&b);
    CHECK(!iCheatsInForce(iCHEAT_NO_DAMAGE) && !iCheatsInForce(iCHEAT_UNLOCK_EXTRAS));

    iConfigSet("cheats.no_damage", "on");
    iCheatsApply();
    CHECK(!iCheatsInForce(iCHEAT_NO_DAMAGE));
    iCheatsSharedRelease();
    CHECK(!iCheatsFollowing() && iCheatsSharedFollowed() == NULL);
    CHECK(iCheatsInForce(iCHEAT_NO_DAMAGE) && !iCheatsInForce(iCHEAT_FLY));
    CHECK(iCheatsRequest(iCHEAT_REQUEST_REFILL, 0) && iCheatsNextRequest(&r));
    iCheatsAllPortOff();
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("usage: tssm_cheattest <scratch folder> [zGameExtras.cpp]\n");
        return 2;
    }

#ifdef _WIN32
    _mkdir(argv[1]);
#else
    mkdir(argv[1], 0777);
#endif

    TestCodes();
    if (argc > 2)
    {
        TestSource(argv[2]);
    }
    TestExtras();
    TestConfig(argv[1]);
    TestRequests();
    TestShared();

    if (sFailures != 0)
    {
        printf("tssm_cheattest: %d of %d checks failed\n", sFailures, sChecks);
        return 1;
    }
    printf("tssm_cheattest: all %d checks passed\n", sChecks);
    return 0;
}
