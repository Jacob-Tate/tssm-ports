// PC-only: local co-op, two copies of the game on one PC (iCoopLocal.h,
// docs/MULTIPLAYER.md 12).

#include "iCoopLocal.h"

#include "iBoot.h"
#include "iCoop.h"
#include "iCoopInternal.h"
#include "iCoopLocalCore.h"
#include "iConfig.h"
#include "iFile.h"
#include "iHost.h"
#include "iNetCrypto.h"
#include "iSaveStore.h"
#include "iWindow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

namespace
{
    const U64 kNsPerMs = 1000000ull;
    const U32 kHelloWaitMs = 10000; // Player 2: the session's line, before co-op starts
    const U64 kEndGrace = 5000 * kNsPerMs; // End local co-op: then the child is ended
    // Out of the session this long while still running: not responding.
    const U64 kOutGrace = 2000 * kNsPerMs;
    // Player 1's exit: Player 2's game may close by itself this long, then
    // the job ends it with this process.
    const U32 kExitGraceMs = 2000;

    enum P1State
    {
        P1_HOSTING, // hosting asked; waiting for the port
        P1_WAIT_LEVEL, // hosting; Player 2 starts once this game is in a level
        P1_RUNNING, // Player 2's game runs
        P1_ENDING, // End local co-op: quit sent, waiting for it to close
        P1_GONE, // Player 2's game closed; Bring back or Play alone
        P1_FAILED, // could not host or start Player 2; the same choices
    };

    iCoopLocalRole sRole;
    char sLayout[8] = "side"; // S3 places the windows by it

    // --- Player 1 -------------------------------------------------------------
    P1State sState;
    char sCode[iLocal::kCodeLen + 1]; // the session's password, until the hello is written
    U16 sPort;
    U32 sPollFrame;
    U32 sLevelFrame;
    U32 sLevelScene;
    iHostChild* sChild;
    U32 sChildId;
    U64 sChildAt;
    bool sJoined, sEverJoined, sOutLogged, sKilled;
    U64 sOutSince, sEndAt;
    const char* sGoneText = "";
    char sProfile[600];
    char sDetail[400];

    // --- Player 2 -------------------------------------------------------------
    bool sHaveHello;
    U16 sHelloPort;
    char sHelloCode[iLocal::kCodeLen + 1];
    char sBoot[8];
    bool sClosing;
    U64 sFirstFrameAt;
    bool sP2Joined;
    iLocal::LineReader sReader;
    U32 sBadLines;

    U64 Now()
    {
        return iCoopNowNs();
    }

    long long Ms(U64 ns)
    {
        return (long long)(ns / kNsPerMs);
    }

    void Say(const char* text)
    {
        printf("tssm: coop: local: %s\n", text);
        fflush(stdout);
    }

    void SceneText(U32 id, char out[5])
    {
        for (int i = 0; i < 4; i++)
        {
            out[i] = (char)(id >> (24 - 8 * i));
        }
        out[4] = '\0';
    }

    bool CopyFile(const char* from, const char* to)
    {
        FILE* in = fopen(from, "rb");
        if (in == NULL)
        {
            return false;
        }
        FILE* out = fopen(to, "wb");
        if (out == NULL)
        {
            fclose(in);
            return false;
        }
        char buf[16384];
        size_t n;
        bool ok = true;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        {
            ok = ok && fwrite(buf, 1, n, out) == n;
        }
        fclose(in);
        return fclose(out) == 0 && ok;
    }

    void Absolute(const char* path, char* out, size_t cap)
    {
        if (!iHostAbsolutePath(path, out, cap))
        {
            snprintf(out, cap, "%s", path);
        }
    }

    void EachVar(void* ctx, const char* nameValue)
    {
        ((std::vector<std::string>*)ctx)->push_back(nameValue);
    }

    void ForgetCode()
    {
        iNet::secure_zero(sCode, sizeof(sCode));
    }

    void Fail(const char* why)
    {
        ForgetCode();
        iCoopStop();
        sState = P1_FAILED;
        snprintf(sDetail, sizeof(sDetail), "%s", why);
        printf("tssm: coop: local: local co-op could not start: %s\n", why);
        fflush(stdout);
        iCoopAddNotice("local co-op could not start; the Co-op tab says why");
    }

    bool HostNew()
    {
        char why[200];
        if (!iLocal::new_code(sCode))
        {
            Fail("this PC gave no random numbers for the session's password");
            return false;
        }
        if (!iCoopHostLocal(sCode, iCoopDefaultName(), why, sizeof(why)))
        {
            Fail(why);
            return false;
        }
        sState = P1_HOSTING;
        sLevelFrame = 0;
        sDetail[0] = '\0';
        return true;
    }

    // Player 2's folder, its config overlay and the files hash cache.
    bool PrepareProfile(char* why, size_t whySize)
    {
        char dir[600];
        if (!iLocal::profile_dir(iSaveStoreRoot(), dir, sizeof(dir)))
        {
            snprintf(why, whySize,
                     "Player 2's folder could not be worked out from the save folder");
            return false;
        }
        Absolute(dir, sProfile, sizeof(sProfile));
        char saves[700];
        snprintf(saves, sizeof(saves), "%s/saves", sProfile);
        if (!iHostMakeDir(sProfile) || !iHostMakeDir(saves))
        {
            snprintf(why, whySize, "Player 2's folder %s could not be made", sProfile);
            return false;
        }
        // The game-files hash cache, so Player 2's first check takes no time.
        char from[700], to[700];
        snprintf(from, sizeof(from), "%s/coop_files.cache", iSaveStoreRoot());
        snprintf(to, sizeof(to), "%s/coop_files.cache", saves);
        if (iHostPathExists(from) && !CopyFile(from, to))
        {
            printf(
                "tssm: coop: local: the game-files cache could not be copied for Player 2 (it checks them "
                "itself)\n");
        }
        char ini[700];
        snprintf(ini, sizeof(ini), "%s/player2.ini", sProfile);
        FILE* f = iHostCreateNewFile(ini);
        if (f != NULL)
        {
            fprintf(
                f,
                "; Player 2's own settings in local co-op: name, character, costumes, name tag,\n"
                "; camera and stick feel, rumble. Everything else is config.ini's, which\n"
                "; Player 2's game reads and never writes.\n");
            fclose(f);
        }
        return true;
    }

    bool Spawn(char* why, size_t whySize)
    {
        if (!PrepareProfile(why, whySize))
        {
            return false;
        }
        char exe[600];
        if (!iHostExePath(exe, sizeof(exe)))
        {
            snprintf(why, whySize, "the game's own file could not be found");
            return false;
        }
        std::vector<std::string> parent;
        if (!iHostEnvironment(EachVar, &parent))
        {
            snprintf(why, whySize, "this game's environment could not be read");
            return false;
        }
        iHostChild* c = iHostChildCreate();
        if (c == NULL)
        {
            snprintf(why, whySize, "this PC could not start a second copy of the game");
            return false;
        }
        char scene[5];
        SceneText(sLevelScene, scene);
        char config[600], assets[600];
        const char* configPath = iConfigPath();
        std::vector<std::string> set = {
            "TSSM_LOCAL_COOP=2",
            std::string("TSSM_LOCAL_PIPE=") + iHostChildPipeValue(c),
            std::string("TSSM_LOCAL_BOOT=") + scene,
            std::string("TSSM_CONFIG_READONLY=") + sProfile + "/player2.ini",
            std::string("TSSM_SAVE_DIR=") + sProfile + "/saves",
            std::string("TSSM_LOG=") + sProfile + "/last_run.log",
            "TSSM_GUI_LAUNCHER=never",
        };
        // So both hash the same files; with no folder set, both read the
        // same config.ini and start in the same working directory.
        if (iFileAssetRoot()[0] != '\0')
        {
            Absolute(iFileAssetRoot(), assets, sizeof(assets));
            set.push_back(std::string("TSSM_ASSETS=") + assets);
        }
        if (configPath != NULL)
        {
            Absolute(configPath, config, sizeof(config));
            set.push_back(std::string("TSSM_CONFIG=") + config);
        }
        std::vector<char> block;
        if (!iLocal::env_block(iLocal::child_env(parent, set), &block))
        {
            iHostChildFree(c);
            snprintf(why, whySize, "Player 2's environment could not be made");
            return false;
        }
        // The session goes into the pipe before the child exists, so its
        // first read finds it; the password is in no environment, command
        // line, file or log.
        iLocal::Line hello;
        memset(&hello, 0, sizeof(hello));
        hello.kind = iLocal::LINE_HELLO;
        hello.port = sPort;
        memcpy(hello.code, sCode, sizeof(hello.code));
        char line[iLocal::kMaxLine + 1];
        size_t n = iLocal::format_line(hello, line, sizeof(line));
        bool sent = n != 0 && iHostChildSend(c, line, n);
        iNet::secure_zero(&hello, sizeof(hello));
        iNet::secure_zero(line, sizeof(line));
        ForgetCode();
        if (!sent || !iHostChildStart(c, exe, block.data()))
        {
            iHostChildFree(c);
            snprintf(why, whySize, "Player 2's game could not be started");
            return false;
        }
        sChild = c;
        sChildId = iHostChildId(c);
        sChildAt = Now();
        sJoined = sEverJoined = sOutLogged = sKilled = false;
        sOutSince = 0;
        sState = P1_RUNNING;
        snprintf(sDetail, sizeof(sDetail), "Player 2's log: %s/last_run.log", sProfile);
        printf("tssm: coop: local: Player 2's game started (process %u) in %s at clock ms %lld\n",
               (unsigned)sChildId, scene, Ms(sChildAt));
        printf("tssm: coop: local: Player 2's log: %s/last_run.log\n", sProfile);
        fflush(stdout);
        return true;
    }

    void ChildGone(U32 code)
    {
        U64 now = Now();
        const char* notice;
        if (sState == P1_ENDING)
        {
            sGoneText = sKilled ? "Player 2's game did not close by itself and was ended" :
                                  "Player 2's game closed";
            notice = "local co-op ended; Player 2's game closed";
        }
        else if (code == 0 && sEverJoined)
        {
            sGoneText = "Player 2 left";
            notice = "Player 2 left";
        }
        else if (code == 0)
        {
            sGoneText = "Player 2's game closed before it joined (its log says why)";
            notice = "Player 2's game could not join";
        }
        else
        {
            sGoneText = "Player 2's game stopped unexpectedly (its crash.log may say why)";
            notice = "Player 2's game stopped unexpectedly";
        }
        printf("tssm: coop: local: %s (process %u, exit code 0x%x) at clock ms %lld\n", sGoneText,
               (unsigned)sChildId, (unsigned)code, Ms(now));
        if (code != 0 && sState != P1_ENDING)
        {
            printf("tssm: coop: local: Player 2's crash report, if any: %s/crash.log\n", sProfile);
            snprintf(sDetail, sizeof(sDetail), "Player 2's crash report, if any: %s/crash.log",
                     sProfile);
        }
        fflush(stdout);
        iCoopAddNotice(notice);
        iHostChildFree(sChild);
        sChild = NULL;
        iCoopStop(); // a new session for Bring back: a new port and password
        sState = P1_GONE;
    }

    void PollPlayer1()
    {
        iCoopStatus st;
        iCoopGetStatus(&st);
        U64 now = Now();
        switch (sState)
        {
        case P1_HOSTING:
            if (st.role == ICOOP_ROLE_HOST && st.phase == ICOOP_PHASE_WAITING && st.port != 0)
            {
                sPort = st.port;
                sState = P1_WAIT_LEVEL;
                sLevelFrame = 0;
                Say("hosting for Player 2; its game starts once this game is in a level");
            }
            else if (st.phase == ICOOP_PHASE_ERROR)
            {
                Fail(st.reason);
            }
            break;
        case P1_WAIT_LEVEL:
            if (sLevelFrame != 0 && sLevelFrame + 1 == sPollFrame)
            {
                char why[300];
                if (!Spawn(why, sizeof(why)))
                {
                    Fail(why);
                }
            }
            break;
        case P1_RUNNING:
        case P1_ENDING:
        {
            U32 code = 0;
            if (iHostChildState(sChild, &code) == 0)
            {
                ChildGone(code);
                break;
            }
            if (st.connected && !sJoined)
            {
                sJoined = sEverJoined = true;
                sOutSince = 0;
                printf("tssm: coop: local: Player 2 joined, %lld ms after its game started\n",
                       Ms(now - sChildAt));
                fflush(stdout);
            }
            else if (!st.connected && sJoined)
            {
                sJoined = false;
                sOutSince = now;
            }
            if (sOutSince != 0 && !sOutLogged && now - sOutSince >= kOutGrace &&
                sState == P1_RUNNING)
            {
                sOutLogged = true;
                Say("Player 2's game still runs but is out of the session; End local co-op ends it");
            }
            if (sState == P1_ENDING && !sKilled && now >= sEndAt)
            {
                sKilled = true;
                Say("Player 2's game did not close within 5 s; ending it");
                iHostChildKill(sChild);
            }
            break;
        }
        default:
            break;
        }
    }

    void Close(const char* why)
    {
        if (sClosing)
        {
            return;
        }
        sClosing = true;
        printf("tssm: coop: local: %s; closing this game\n", why);
        fflush(stdout);
        iWindowRequestClose();
    }

    void OnLine(void*, bool ok, const iLocal::Line& l)
    {
        if (!ok)
        {
            if (sBadLines++ < 4)
            {
                Say("a line from Player 1's game was not understood; ignored");
            }
            return;
        }
        if (l.kind == iLocal::LINE_QUIT)
        {
            Close("Player 1 ended local co-op");
        }
        else if (l.kind == iLocal::LINE_HELLO && !sHaveHello)
        {
            sHaveHello = true;
            sHelloPort = l.port;
            memcpy(sHelloCode, l.code, sizeof(sHelloCode));
        }
        else if (sBadLines++ < 4)
        {
            Say("a second session line from Player 1's game was ignored");
        }
    }

    void PollPlayer2()
    {
        U64 now = Now();
        if (sFirstFrameAt == 0)
        {
            sFirstFrameAt = now;
            printf("tssm: coop: local: this game's first frame at clock ms %lld\n", Ms(now));
            fflush(stdout);
        }
        if (sClosing)
        {
            // Every frame: opening the window clears a close asked for before it.
            iWindowRequestClose();
            return;
        }
        char buf[512];
        for (int i = 0; i < 8 && !sClosing; i++)
        {
            S32 n = iHostParentRead(buf, sizeof(buf), 0);
            if (n < 0)
            {
                Close("Player 1's game closed");
                break;
            }
            if (n == 0)
            {
                break;
            }
            sReader.feed((const uint8_t*)buf, (size_t)n, OnLine, NULL);
        }
        if (sClosing)
        {
            return;
        }
        iCoopStatus st;
        iCoopGetStatus(&st);
        if (st.connected && !sP2Joined)
        {
            sP2Joined = true;
            printf(
                "tssm: coop: local: joined Player 1's game at clock ms %lld (%lld ms after this game's first "
                "frame)\n",
                Ms(now), Ms(now - sFirstFrameAt));
            fflush(stdout);
        }
        // A join that cannot go on: this copy has nothing else to do.
        if (st.phase == ICOOP_PHASE_REFUSED || st.phase == ICOOP_PHASE_NO_ANSWER ||
            st.phase == ICOOP_PHASE_ERROR || st.phase == ICOOP_PHASE_ENDED ||
            (st.role == ICOOP_ROLE_OFF && sP2Joined))
        {
            char why[400];
            snprintf(why, sizeof(why), "%s Player 1's game: %s",
                     sP2Joined ? "out of" : "could not join",
                     st.reason[0] != '\0' ? st.reason : "the session ended");
            Close(why);
        }
    }

    bool HasArg(const char** value)
    {
        S32 argc = 0;
        char** argv = NULL;
        if (!iHostCommandLine(&argc, &argv) || argv == NULL)
        {
            return false;
        }
        for (S32 i = 1; i < argc; i++)
        {
            if (strcmp(argv[i], "--local-coop") == 0)
            {
                *value = "";
                return true;
            }
            if (strncmp(argv[i], "--local-coop=", 13) == 0)
            {
                *value = argv[i] + 13;
                return true;
            }
        }
        return false;
    }

    void StartPlayer2()
    {
        if (!iHostParentPipeOpen(getenv("TSSM_LOCAL_PIPE")))
        {
            Say("TSSM_LOCAL_COOP=2 comes from Player 1's game only, with its pipe; there is none, so local co-op "
                "is off");
            return;
        }
        sRole = ICOOP_LOCAL_PLAYER2;
        U64 until = iHostMonotonicNs() + (U64)kHelloWaitMs * kNsPerMs;
        char buf[256];
        bool pipeEnded = false;
        while (!sHaveHello && !sClosing)
        {
            U64 t = iHostMonotonicNs();
            if (t >= until)
            {
                break;
            }
            S32 n = iHostParentRead(buf, sizeof(buf), (U32)((until - t) / kNsPerMs) + 1);
            if (n < 0)
            {
                pipeEnded = true;
                break;
            }
            sReader.feed((const uint8_t*)buf, (size_t)n, OnLine, NULL);
        }
        iNet::secure_zero(buf, sizeof(buf));
        // A quit, even one that came with the session: no window yet, so leave now.
        if (sClosing)
        {
            iNet::secure_zero(sHelloCode, sizeof(sHelloCode));
            Say("Player 1 ended local co-op before this game started; closing");
            exit(0);
        }
        if (!sHaveHello)
        {
            Say(pipeEnded ? "Player 1's game closed before it sent the session; closing" :
                            "no word from Player 1's game within 10 s; closing");
            exit(3);
        }
        const char* boot = getenv("TSSM_LOCAL_BOOT");
        if (boot != NULL && iLocal::scene_text_valid(boot))
        {
            snprintf(sBoot, sizeof(sBoot), "%s", boot);
        }
        else
        {
            Say("Player 1's scene was not given; starting where the game starts");
        }
        printf("tssm: coop: local: this is Player 2's game; Player 1 hosts on UDP port %u%s%s\n",
               (unsigned)sHelloPort, sBoot[0] != '\0' ? ", in " : "", sBoot);
        fflush(stdout);
    }
} // namespace

iCoopLocalRole iCoopLocalGetRole()
{
    return sRole;
}

bool iCoopLocalPlayer2()
{
    return sRole == ICOOP_LOCAL_PLAYER2;
}

bool iCoopLocalAvailable()
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

void iCoopLocalInit()
{
    const char* env = getenv("TSSM_LOCAL_COOP");
    if (env != NULL && strcmp(env, "2") == 0)
    {
        StartPlayer2();
        return;
    }
    const char* layout = NULL;
    bool asked = HasArg(&layout) || (env != NULL && strcmp(env, "1") == 0);
    if (!asked)
    {
        return;
    }
    if (!iCoopLocalAvailable())
    {
        Say("local co-op runs on Windows only; it is off");
        return;
    }
    if (layout != NULL && layout[0] != '\0')
    {
        if (strcmp(layout, "side") == 0 || strcmp(layout, "stack") == 0)
        {
            snprintf(sLayout, sizeof(sLayout), "%s", layout);
        }
        else
        {
            Say("--local-coop= takes side or stack; using side");
        }
    }
    sRole = ICOOP_LOCAL_PLAYER1;
    printf(
        "tssm: coop: local: this is Player 1's game (layout %s); [coop] mode, bind, address and port are not "
        "used, and online co-op is off while local co-op runs\n",
        sLayout);
    fflush(stdout);
}

void iCoopLocalApplyBoot()
{
    if (sRole != ICOOP_LOCAL_PLAYER2)
    {
        return;
    }
    if (sBoot[0] != '\0')
    {
        iBootSetScene(sBoot);
    }
    iBootSetIntroMovies(FALSE);
}

void iCoopLocalStartSession()
{
    if (sRole == ICOOP_LOCAL_PLAYER1)
    {
        HostNew();
        return;
    }
    if (sRole != ICOOP_LOCAL_PLAYER2 || !sHaveHello)
    {
        return;
    }
    char address[32];
    snprintf(address, sizeof(address), "127.0.0.1:%u", (unsigned)sHelloPort);
    char why[200];
    bool ok = iCoopJoin(address, sHelloCode, iCoopDefaultName(), why, sizeof(why));
    iNet::secure_zero(sHelloCode, sizeof(sHelloCode));
    if (!ok)
    {
        printf("tssm: coop: local: joining Player 1's game did not start: %s\n", why);
        fflush(stdout);
        Close("could not join Player 1's game");
    }
}

void iCoopLocalPoll()
{
    if (sRole == ICOOP_LOCAL_OFF)
    {
        return;
    }
    sPollFrame++;
    if (sRole == ICOOP_LOCAL_PLAYER1)
    {
        PollPlayer1();
    }
    else
    {
        PollPlayer2();
    }
}

bool iCoopLocalWantsLevel()
{
    return sRole == ICOOP_LOCAL_PLAYER1 && sState == P1_WAIT_LEVEL;
}

void iCoopLocalLevel(U32 scene, bool known)
{
    if (!iCoopLocalWantsLevel())
    {
        return;
    }
    if (known && iNet::scene_valid(scene))
    {
        sLevelScene = scene;
        sLevelFrame = sPollFrame;
    }
}

void iCoopLocalGetView(iCoopLocalView* out)
{
    memset(out, 0, sizeof(*out));
    out->role = sRole;
    if (sRole == ICOOP_LOCAL_PLAYER2)
    {
        out->state = "You are Player 2";
        out->canLeave = !sClosing;
        return;
    }
    if (sRole != ICOOP_LOCAL_PLAYER1)
    {
        out->state = "";
        return;
    }
    snprintf(out->detail, sizeof(out->detail), "%s", sDetail);
    switch (sState)
    {
    case P1_HOSTING:
        out->state = "Starting...";
        break;
    case P1_WAIT_LEVEL:
        out->state = "Player 2's game starts once you are in a level";
        break;
    case P1_RUNNING:
        out->state = sJoined    ? "Player 2 is playing" :
                     sOutLogged ? "Player 2's game is not responding, or left the session" :
                                  "Player 2's game is starting...";
        break;
    case P1_ENDING:
        out->state = "Closing Player 2's game...";
        break;
    case P1_GONE:
        out->state = sGoneText;
        break;
    case P1_FAILED:
        out->state = "Local co-op could not start";
        break;
    }
    out->canEnd = sState == P1_HOSTING || sState == P1_WAIT_LEVEL || sState == P1_RUNNING;
    out->canBringBack = out->canPlayAlone = sState == P1_GONE || sState == P1_FAILED;
}

void iCoopLocalEnd()
{
    if (sRole != ICOOP_LOCAL_PLAYER1)
    {
        return;
    }
    if (sState == P1_HOSTING || sState == P1_WAIT_LEVEL)
    {
        ForgetCode();
        iCoopStop();
        sGoneText = "Local co-op was ended before Player 2's game started";
        sState = P1_GONE;
        Say("local co-op ended before Player 2's game started");
        return;
    }
    if (sState != P1_RUNNING)
    {
        return;
    }
    iLocal::Line quit;
    memset(&quit, 0, sizeof(quit));
    quit.kind = iLocal::LINE_QUIT;
    char line[16];
    size_t n = iLocal::format_line(quit, line, sizeof(line));
    iHostChildSend(sChild, line, n);
    iHostChildClosePipe(sChild);
    sEndAt = Now() + kEndGrace;
    sState = P1_ENDING;
    Say("End local co-op: Player 2's game was asked to close");
}

void iCoopLocalBringBack()
{
    if (sRole != ICOOP_LOCAL_PLAYER1 || (sState != P1_GONE && sState != P1_FAILED))
    {
        return;
    }
    if (HostNew())
    {
        Say("bringing Player 2 back: hosting again, with a new port and password");
    }
}

void iCoopLocalPlayAlone()
{
    if (sRole != ICOOP_LOCAL_PLAYER1 || (sState != P1_GONE && sState != P1_FAILED))
    {
        return;
    }
    ForgetCode();
    sRole = ICOOP_LOCAL_OFF;
    Say("playing alone: local co-op is off until the next start");
}

void iCoopLocalLeave()
{
    if (sRole == ICOOP_LOCAL_PLAYER2)
    {
        Close("Player 2 left local co-op");
    }
}

void iCoopLocalShutdown()
{
    if (sRole != ICOOP_LOCAL_PLAYER1 || sChild == NULL)
    {
        return;
    }
    // The pipe's end, then a moment for Player 2's game to close by itself.
    iHostChildClosePipe(sChild);
    bool closed = iHostChildWait(sChild, kExitGraceMs);
    printf("tssm: coop: local: %s at clock ms %lld\n",
           closed ? "Player 2's game closed with this one" :
                    "Player 2's game did not close within 2 s; it ends with this one",
           Ms(Now()));
    fflush(stdout);
    iHostChildFree(sChild);
    sChild = NULL;
}
