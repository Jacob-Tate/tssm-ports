// tssm_configtest: config.ini read only, as local co-op's Player 2 reads it
// (iConfig.h, docs/MULTIPLAYER.md 12).
//
//   tssm_configtest [scratch folder]
//
// Player 2's game must never write, append to or create config.ini: its own
// settings come from player2.ini alone, which takes only Player 2's keys.

#include "iConfig.h"
#include "iHost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

static int sChecks, sFailures;

#define CHECK(c, ...)                                                                              \
    do                                                                                             \
    {                                                                                              \
        sChecks++;                                                                                 \
        if (!(c))                                                                                  \
        {                                                                                          \
            sFailures++;                                                                           \
            printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #c);                                \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static void SetEnv(const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s(name, value != NULL ? value : "");
#else
    if (value != NULL)
        setenv(name, value, 1);
    else
        unsetenv(name);
#endif
}

static std::string ReadAll(const char* path)
{
    std::string s;
    FILE* f = fopen(path, "rb");
    if (f == NULL)
        return s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        s.append(buf, n);
    fclose(f);
    return s;
}

static void WriteAll(const char* path, const char* text)
{
    FILE* f = fopen(path, "wb");
    if (f != NULL)
    {
        fputs(text, f);
        fclose(f);
    }
}

static S64 MTime(const char* path)
{
    iHostFileInfo info;
    return iHostStat(path, &info) ? info.mtime : -1;
}

static bool Is(const char* key, const char* want)
{
    const char* v = iConfigGetString(key, "?");
    return v != NULL && strcmp(v, want) == 0;
}

int main(int argc, char** argv)
{
    char dir[512];
    if (argc > 1)
        snprintf(dir, sizeof dir, "%s", argv[1]);
    else
    {
        const char* tmp = getenv("TEMP");
        snprintf(dir, sizeof dir, "%s/tssm_configtest", tmp != NULL ? tmp : ".");
    }
    iHostMakeDir(dir);
    setvbuf(stdout, NULL, _IONBF, 0);

    char config[600], overlay[600], missing[600], control[600];
    snprintf(config, sizeof config, "%s/config.ini", dir);
    snprintf(overlay, sizeof overlay, "%s/player2.ini", dir);
    snprintf(missing, sizeof missing, "%s/not-there.ini", dir);
    snprintf(control, sizeof control, "%s/control.ini", dir);
    iHostRemoveFile(missing);

    // 1. A config.ini that is missing almost every setting, with Player 1's
    // own name and character in it; an overlay with Player 2's own keys and
    // one that is not Player 2's.
    const char* main =
        "; Player 1's settings\r\n[video]\r\nmode = windowed\r\n[coop]\r\nname = Host\r\n"
        "character = patrick\r\n";
    WriteAll(config, main);
    iHostSetFileTime(config, 1000000000); // 2001: any write would move it
    WriteAll(
        overlay,
        "[coop]\r\nname = Guest\r\ncostume_spongebob = caveman\r\n[video]\r\nmode = fullscreen\r\n"
        "[input]\r\nrumble = 40\r\n");
    const std::string before = ReadAll(config);
    const S64 mtime = MTime(config);

    SetEnv("TSSM_CONFIG", config);
    SetEnv("TSSM_CONFIG_READONLY", overlay);
    iConfigTestReset();
    iConfigLoad();
    printf("[1] read only, from a config.ini missing settings\n");
    CHECK(iConfigReadOnly(), "read-only mode on");
    CHECK(ReadAll(config) == before && MTime(config) == mtime,
          "config.ini not appended to at load");
    CHECK(Is("video.mode", "windowed"), "config.ini's own settings read: %s",
          iConfigGetString("video.mode", "?"));
    CHECK(Is("coop.name", "Guest") && Is("coop.costume_spongebob", "caveman") &&
              Is("input.rumble", "40"),
          "Player 2's own keys from player2.ini");
    CHECK(Is("coop.character", "auto"), "Player 1's own character is not Player 2's: %s",
          iConfigGetString("coop.character", "?"));

    iConfigSet("coop.name", "Newname");
    iConfigSet("coop.nametag", "off");
    iConfigSet("video.mode", "borderless");
    iConfigUnset("input.rumble");
    CHECK(iConfigSave(), "saved");
    CHECK(ReadAll(config) == before && MTime(config) == mtime, "config.ini untouched by a save");
    CHECK(Is("video.mode", "borderless"), "a change of another setting lasts the session");
    std::string p2 = ReadAll(overlay);
    CHECK(p2.find("name = Newname") != std::string::npos &&
              p2.find("nametag = off") != std::string::npos,
          "Player 2's own changes in player2.ini:\n%s", p2.c_str());
    CHECK(p2.find("borderless") == std::string::npos && p2.find("rumble") == std::string::npos,
          "nothing else written there, and the removed key gone:\n%s", p2.c_str());

    // 2. TSSM_CONFIG names a file that is not there: not created.
    printf("[2] read only, config.ini missing\n");
    SetEnv("TSSM_CONFIG", missing);
    iConfigTestReset();
    iConfigLoad();
    CHECK(!iHostPathExists(missing) && iConfigPath() == NULL, "no config.ini created");
    CHECK(Is("coop.name", "Newname"), "player2.ini still read");
    iConfigSet("video.width", "800");
    CHECK(iConfigSave() && !iHostPathExists(missing), "a save creates none either");

    // 3. A fresh profile: no player2.ini yet; a save makes one with Player 2's
    // keys only.
    printf("[3] read only, no player2.ini yet\n");
    iHostRemoveFile(overlay);
    SetEnv("TSSM_CONFIG", config);
    iConfigTestReset();
    iConfigLoad();
    CHECK(Is("coop.name", ""), "no name until Player 2 picks one: '%s'",
          iConfigGetString("coop.name", "?"));
    iConfigSet("coop.character", "spongebob");
    iConfigSet("input.preset", "gamecube");
    CHECK(iConfigSave(), "saved");
    p2 = ReadAll(overlay);
    CHECK(p2.find("character = spongebob") != std::string::npos &&
              p2.find("gamecube") == std::string::npos,
          "player2.ini made with the own key only:\n%s", p2.c_str());
    CHECK(ReadAll(config) == before && MTime(config) == mtime, "config.ini still untouched");

    // 4. The control: without the overlay the same file IS appended to.
    printf("[4] the ordinary load (control)\n");
    WriteAll(control, main);
    SetEnv("TSSM_CONFIG_READONLY", NULL);
    SetEnv("TSSM_CONFIG", control);
    iConfigTestReset();
    iConfigLoad();
    CHECK(!iConfigReadOnly() && ReadAll(control).size() > strlen(main),
          "the missing settings appended");
    CHECK(Is("coop.character", "patrick"), "and Player 1's character read");

    printf("tssm_configtest: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
