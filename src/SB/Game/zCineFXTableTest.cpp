#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "zParEmitter.h"

namespace CineFXTableTest
{

    static void zCinFX_Par_CINFX_TEST_PAR_SMOKE_Upd(const zCutsceneMgr*, NCINEntry* fxrec,
                                                    S32 killit)
    {
        if (killit)
        {
            fxrec->flg_stat |= NCIN_STAT_DEAD;
        }
        else if (fxrec->flg_stat & NCIN_STAT_FIRST)
        {
            zParEmitter*& emitter = fxrec->fxdata.pardata.emitter;
            xParEmitterAsset* a;

            emitter = zParEmitterFind("PAREMIT_SMOKE");
            a = emitter->tasset;
            if (a == NULL || a->emit_type != eParEmitterEntityBone)
            {
                emitter = NULL;
            }

            if (emitter == NULL)
            {
                fxrec->flg_stat |= NCIN_STAT_DEAD;
                return;
            }

            if (fxrec->basic.pos_A[1].y < 0.0f)
            {
                fxrec->basic.pos_A[1].y = 0.1f + a->e_entbone.bone;
            }

            fxrec->basic.pos_B[0] = 1e9f;
        }
    }

    static void NCIN_upd_test(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
    {
        if (killit)
        {
            fxrec->flg_stat |= NCIN_STAT_DEAD;
        }
    }

    static void NCIN_anim_test(const zCutsceneMgr*, NCINEntry*, RpAtomic*, RwMatrixTag*, U32, U32)
    {
    }

    static void NCIN_rend_test(const zCutsceneMgr*, NCINEntry*)
    {
    }

    static void NCIN_tweak_test(const char*, NCINEntry*)
    {
    }

    NCINStreakInput g_td_streak_1 = {
        0.24f,
        { 0.25f, 4.0f, 1.0f, 0, { 0xFF, 0xFF, 0x80, 0xFF }, { 0xC4, 0xC4, 0x40, 0xFF }, 1 },
    };

    static NCINEntry g_TR02_cin_test[4] = {
        {
            { 0x1, 0.7983334f, 0.90900004f, "CinFX Test" },
            { (NCINUpdFunc)NCIN_upd_test, (NCINAnimFunc)NCIN_anim_test,
              (NCINRendFunc)NCIN_rend_test, (NCINTweakFunc)NCIN_tweak_test },
            { 0, 0, 0, 0 },
            { { { 8.64f, 9.411f, 1.659f }, { 99.0f, 101.0f, 108.0f } },
              { { -1.0f, -2.0f, -3.0f }, { 2.42f, -3.23f, 4.04f } } },
            { NULL },
        },
        {
            { 0x2, 0.033333335f, 16.666668f, "Pat Pursue 1b" },
            { (NCINUpdFunc)zCinFX_StreakBone_Upd, (NCINAnimFunc)zCinFX_StreakBone_AR, NULL,
              (NCINTweakFunc)zCinFX_StreakBone_Twk },
            { 0, 7, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { &g_td_streak_1 },
        },
        {
            { 0x8002, 0.33333334f, 0.3f, "Smoke1" },
            { (NCINUpdFunc)zCinFX_Par_CINFX_TEST_PAR_SMOKE_Upd,
              (NCINAnimFunc)zCinFX_EntityBonePar_AR, NULL, NULL },
            { 0, 0, 0, 0 },
            { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
              { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
            { NULL },
        },
        { 0 },
    };
} // namespace CineFXTableTest

void zCineFX_Register_TestTable()
{
    static NCINMap cutmap[] = {
        { "cin_test", CineFXTableTest::g_TR02_cin_test, 0 },
        { NULL, NULL, 0 },
    };

    zCineFXRegisterTable(cutmap);
}
