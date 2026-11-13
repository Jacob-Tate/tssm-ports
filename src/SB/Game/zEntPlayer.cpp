#include "zEntPlayer.h"

#include "xAnim.h"
#include "xBase.h"
#include "xEnt.h"
#include "xScene.h"

#include "zEntPlayerFX.h"
#include "zFX.h"
#include "zGlobals.h"
#include "zNMECommon.h"
#include "zCamera.h"
#include "zMovePoint.h"

#include "xEntBoulder.h"
#include "xShadow.h"
#include "zConfig.h"
#include "zStreamedSound.h"
#include "zEntTeleportBox.h"
#include "zLasso.h"
#include "xRumbleManager.h"
#include "zSurface.h"
#include "zNMEHazard.h"
#include "zEntPlayerOOBState.h"
#include "zEntPlayerSlideFX.h"
#include "zEntPlayerCar.h"
#include "zCamCar.h"
#include "iModel.h"
#include "iCollide.h"
#include "iAnim.h"
#include "iAnimSKB.h"
#include "zGameExtras.h"
#include "zShrapnel.h"
#include "iScrFX.h"
#include "zEnt.h"
#include "zLightning.h"
#include "zMusic.h"
#include "zSaveLoad.h"
#include "zEGenerator.h"
#include "zThrown.h"
#include "zEntSimpleObj.h"
#include "zEntCruiseBubble.h"
#include "zParEmitter.h"
#include "zParPTank.h"
#include "zNMETypeBossDenToo.h"
#include "zNMETypeTiki.h"
#include "zGoo.h"
#include "zEntPlayerBungeeState.h"
#include "zRenderState.h"
#include "zUICustom.h"
#include "zMenuSB.h"
#include "xLightKit.h"
#include "xTRC.h"
#include "xModelBucket.h"
#include "zPlayerBase.h"
#include "zTalkBox.h"
#include "zDispatcher.h"
#include "zRewards.h"
#include "zCar.h"
#include "xAntenna.h"
#include "xScrFx.h"
#include "xstransvc.h"
#include "xEntDrive.h"

#include <stdlib.h>
#include <string.h>

struct zNMEHazard;

static F32 CATCH_CAPSULE_RAD = 0.45f;
static F32 CATCH_CAPSULE_BIAS = 0.3f;
extern S32 gBusStopIsWaiting;
extern U32 gBusStopIsRunning;
xModelTag sSpongeBobLKnee;
xModelTag sSpongeBobRKnee;
xModelTag sSpongeBobLElbow;
xModelTag sSpongeBobRElbow;
xModelTag sSpongeBobLFoot;
xModelTag sSpongeBobRFoot;
xModelTag sSpongeBobLHand;
xModelTag sSpongeBobRHand;
xModelTag sPatrickLFoot;
xModelTag sPatrickRFoot;
xModelTag sPatrickLHand;
xModelTag sPatrickRHand;
xModelTag sPatrickLKnee;
xModelTag sPatrickRKnee;
xModelTag sPatrickLElbow;
xModelTag sPatrickRElbow;
xModelTag sPatrickBelly;
xModelTag sPatrickHead;
// Defined in zEntPlayer.cpp itself: retail emits the weak TimeOutCB in this file's .text.
struct zPlayerCommentTimer : zTimerBase
{
    _tagePlayerStreamSnd mSound;

    // No symbol: always inlined into zEntPlayer_SNDInit.
    void Init(F32 duration, bool resetToZero, _tagePlayerStreamSnd sound)
    {
        mDuration = duration;
        mTimeOutCB = TimeOutCB;
        mFlags = 0;
        if (resetToZero)
        {
            mFlags |= 2;
        }
        TimerReset();
        mSound = sound;
    }

    static void TimeOutCB(zTimerBase* theTimer)
    {
        zEntPlayer_SNDPlayStream(((zPlayerCommentTimer*)theTimer)->mSound);
    }
};

zPlayerCommentTimer sIdleCommentTimer;
zPlayerCommentTimer sRunningCommentTimer;
zPlayerCommentTimer sPatCarryCommentTimer;
zPlayerCommentTimer sRollingCommentTimer;
xMat4x3 gPlayerAbsMat;
xMat4x3 rendermat;
xMat4x3 sCameraLastMat;
xVec3 sDriveVel;
xVec3 floor_supp[4];
F32 floor_dist[4];
F32 floor_tmr[4];
xVec3 floor_safe_vec;
xAnimTransition patrickHitTran[8];

// Per-character one-liner tables, indexed by _tagePlayerStreamSnd.
static zStreamedSound spongeBobOneLinerData[ePlayerStreamSnd_Total + 1] = {
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "enter_scene_1_grp", 120.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "enter_scene_2_grp", 120.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "enter_scene_3_grp", 120.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "enter_scene_4_grp", 120.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "enter_scene_5_grp", 120.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "spongeball_comment_grp", 240.0f, 0x2, 0.0f, (iSndGroupHandle)-1 },
    { "cruise_comment_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_bowl_hit_sg", 60.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_bowl_hit_sg", 60.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_bowl_hit_sg", 60.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "SBG01030_grp", 90.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "SBG01091_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "SBG01092_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "SBG01093_a_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "SBG01094_b_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "SBG01095_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "SBG01111_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "SBG01021_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "SBG01021_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "combo_grp", 30.0f, 0x40, 0.0f, (iSndGroupHandle)-1 },
    { "SBG01059_a_grp", 30.0f, 0x40, 0.0f, (iSndGroupHandle)-1 },
    { "SBG01083_a_grp", 30.0f, 0x40, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "SBG01041_b_grp", 60.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "SBG01008_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "bungee_death_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_melee_sg", 20.0f, 0x280, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_deflect_sg", 20.0f, 0x1080, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_bash_sg", 25.0f, 0xA00, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_helmet_embed_sg", 30.0f, 0x1080, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_bungee_sg", 30.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_bowl_sg", 30.0f, 0x800, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_macho_bowl_sg", 30.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_guitar_sg", 30.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_macho_guitar_sg", 30.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_rolling_sg", 60.0f, 0xA00, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_waiting_sg", 90.0f, 0x1080, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_jumping_sg", 60.0f, 0x500, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_goo_sg", 20.0f, 0x1080, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_icy_sg", 30.0f, 0x100, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_defeated_sg", 60.0f, 0xC86, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_starting_sg", 120.0f, 0x2580, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_running_sg", 45.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_ledgehang_sg", 30.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_walljump_sg", 45.0f, 0x500, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_upgrade1_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_upgrade2_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_upgrade3_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_upgrade4_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_uphealth4_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_uphealth5_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_uphealth6_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_laugh_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_otherplayer_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_tiki_sg", 30.0f, 0x280, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_tiki_wood_sg", 30.0f, 0x280, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_tiki_lovey_sg", 30.0f, 0x280, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_tiki_shrinky_sg", 30.0f, 0x280, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_tiki_thunder_sg", 30.0f, 0x280, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_tiki_steel_sg", 30.0f, 0x280, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_defeatnme_sg", 30.0f, 0x800, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_see_fogger_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_see_slammer_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_see_popper_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_see_spinner_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_see_flinger_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_see_minimerv_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_see_turret_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_see_jellyfish_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_see_merv_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_see_bot_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_got_manliness_sg", 45.0f, 0x500, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_got_health_sg", 45.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_got_ggtoken_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_sb_got_treasure_sg", 180.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { NULL, 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
};

static zStreamedSound patrickOneLinerData[ePlayerStreamSnd_Total + 1] = {
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "enter_scene_1_grp", 120.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "enter_scene_2_grp", 120.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "enter_scene_3_grp", 120.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "enter_scene_4_grp", 120.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "enter_scene_5_grp", 120.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "push_button_grp", 90.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "belly_smash_grp", 30.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "destroy_tiki_grp", 360.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "destroy_robot_grp", 360.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "PSGB01088_a_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "PSGB01089_a_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "PSGB01090_a_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "PSGB01091_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "PSGB01092_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "PSGB01108_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "PSGB01029_B_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "PSGB01034_a_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "combo_grp", 60.0f, 0x40, 0.0f, (iSndGroupHandle)-1 },
    { "PSGB01039_a_grp", 60.0f, 0x40, 0.0f, (iSndGroupHandle)-1 },
    { "PSGB01041_a_grp", 30.0f, 0x40, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "PSGB01033_c_grp", 520.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "exclaim_grp", 360.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_melee_sg", 20.0f, 0x280, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_deflect_sg", 20.0f, 0x1080, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_cartwheel_sg", 30.0f, 0x800, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_tongue_sg", 30.0f, 0xA00, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_lift_sg", 45.0f, 0xA00, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_carry_sg", 30.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_throw_sg", 45.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_bigthrow_sg", 45.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_buttsmash_sg", 30.0f, 0xA00, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_stun_sg", 30.0f, 0xA00, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_waiting_sg", 90.0f, 0x1080, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_jumping_sg", 60.0f, 0x500, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_goo_sg", 20.0f, 0x1080, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_icy_sg", 30.0f, 0x100, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_defeated_sg", 60.0f, 0xC86, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_starting_sg", 120.0f, 0x2580, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_running_sg", 45.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_ledgehang_sg", 30.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_walljump_sg", 45.0f, 0x500, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_upgrade1_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_upgrade2_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_upgrade3_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_upgrade4_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_uphealth4_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_uphealth5_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_uphealth6_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_laugh_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_otherplayer_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_tiki_sg", 30.0f, 0x280, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_tiki_wood_sg", 30.0f, 0x280, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_tiki_lovey_sg", 30.0f, 0x280, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_tiki_shrinky_sg", 30.0f, 0x280, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_tiki_thunder_sg", 30.0f, 0x280, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_tiki_steel_sg", 30.0f, 0x280, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_defeatnme_sg", 30.0f, 0x800, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_see_fogger_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_see_slammer_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_see_popper_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_see_spinner_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_see_flinger_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_see_minimerv_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_see_turret_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_see_jellyfish_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_see_merv_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_see_bot_sg", 20.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_got_manliness_sg", 45.0f, 0x500, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_got_health_sg", 45.0f, 0xC80, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_got_ggtoken_sg", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "onel_ps_got_treasure_sg", 180.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { NULL, 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
};

static zStreamedSound carEmptyOneLinerData[1] = {
    { NULL, 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
};

static zStreamedSound spongePatOneLinerData[17] = {
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "enter_scene_1_grp", 120.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "enter_scene_2_grp", 120.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "enter_scene_3_grp", 120.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "enter_scene_4_grp", 120.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "enter_scene_5_grp", 120.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "spongeball_comment_grp", 240.0f, 0x2, 0.0f, (iSndGroupHandle)-1 },
    { "cruise_comment_grp", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "", 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { "SBG01030_grp", 90.0f, 0, 0.0f, (iSndGroupHandle)-1 },
    { NULL, 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
};

static zStreamedSound slidingOneLinerData[6] = {
    { "onel_slide_defeatnme_sg", 30.0f, 0xA00, 0.0f, (iSndGroupHandle)-1 },
    { "onel_slide_smash_crate_sg", 30.0f, 0x500, 0.0f, (iSndGroupHandle)-1 },
    { "onel_slide_damaged_sg", 30.0f, 0x1080, 0.0f, (iSndGroupHandle)-1 },
    { "onel_slide_got_health_sg", 30.0f, 0x1080, 0.0f, (iSndGroupHandle)-1 },
    { "onel_slide_landing_sg", 20.0f, 0x1080, 0.0f, (iSndGroupHandle)-1 },
    { NULL, 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
};

static U32 sPlayerSnd[eCurrentPlayerCount][ePlayerSnd_Total] = {};
static iSndHandle sPlayerSndID[eCurrentPlayerCount][ePlayerSnd_Total] = { (iSndHandle)-1 };
static xBase* sPlayerRumble[ePlayerSnd_Total] = {};

static F32 sHackStuckTimer;
static xVec3 sHackStuckDir;
static U32 sHackStuckSetDir;
static xVec3 sHackStuckVel;
static F32 sCatchCapsuleTimer;
S32 gBungeeDeath;
static S32 sPlayerUpdateIndex;
static S32 sPlayerUpdateCount;
static xVec3 sAnimPhysicsIncrement;
static F32 wallStickTimer;
F32 gWallStickTime = 0.8f;
static _zPlayerWallJumpState lastWallJumpState;
static F32 stuck_timer;
static F32 not_stuck_timer;
static xVec3 stuck_start_loc;
static U8 sModelUpdatedOnce;
S32 gSpongeBall;
zParEmitter* gEmitBFX;
WAD_STATIC zStreamedSoundList sPlayerOneLiners[4];
static F32 sLastInvulnEmit;
WAD_STATIC zStreamedSoundList sSlidingOneLiners;
static F32 sPlayerSndSneakDelay;
static S32 sPlayerDiedLastTime;
static S32 sPlayerIgnoreSound;
static S32 sPlayerAttackInAir;
WAD_STATIC F32 sOldMusicVolume;

struct zEntPlayerBowlTarget
{
    zLightning* lightning;
    xEnt* ent;
    F32 timer;
    zEntPlayerBowlTarget* prev;
    zEntPlayerBowlTarget* next;
};

struct Melee_cbData
{
    xEnt* ent;
    zScene* zsc;
    xBound* meleeB;
    S32 hitsomething;
    en_plyratak attackType;
};
static zEntPlayerBowlTarget* sBowlTargetHead;
static zEntPlayerBowlTarget* sBowlTargetTail;
static S32 sNumActiveBowlTargets;
static F32 sBowlBlastRadius;
S32 gBowlButton;
static F32 sStartBubbleBowlTimer;
static S32 sStartBubbleBowlLeftGround;
static F32 sThrowBlastRadius;
F32 startJump;
F32 startDouble;
F32 startBounce;
U8 gInitShadow;
static F32 curVelmag;
static F32 curVelangle;
static zEntPlayerBowlTarget zEntPlayerBowlTargetPool[16];
static _tagLightningAdd sBowlLightningAddInfo;
static xVec3 sThrowBlastPosition;
static F32 minVelmag = 0.01f;
static F32 maxVelmag = 10.0f;
static S32 surfSlickness = 1;
static F32 surfFriction = 1.0f;
static F32 surfDamping = minVelmag;
static S32 lastSlickness = 1;
static xVec3 lastDeltaPos;
static xVec3 lastFloorNorm;
static xEnt* lastFloorEnt;
static U32 surfSticky;
static F32 surfSlideStart = DEG2RAD(20);
static F32 surfSlideStop = DEG2RAD(10);
F32 surfSlickRatio;
static F32 surfSlickTimer;
static F32 surfPeakRatio = 1.25f;
static F32 surfAccelWalk = 4.0f;
static F32 surfAccelRun = 8.0f;
static F32 surfDecelIdle = 2.0f;
static F32 surfDecelSkid = 8.0f;
static F32 surfMaxSpeed;
static F32 surfSlipTimer;
static xEnt* sHitch[32];
static xModelTag sStankTag[3];
static RpAtomic* sReticleModel[4];
static xMat4x3 sReticleMat[2];
static xEnt* sGrabFound;
static S32 sGrabFailed;
static F32 sThrowAngle;
static F32 sPlayerCollAdjust;
static zPlayerLassoInfo* sLassoInfo;
static zLasso* sLasso;
static S32 sNumHitches;
static F32 sHitchAngle;
static F32 sSwingTimeElapsed;
static S32 sLassoCamLinger;
static S32 sGooKnockedToSafety;
static F32 sGooKnockedTimer;
xEntBoulder* boulderVehicle;
static F32 bvTimeToIdle;
WAD_STATIC S32 boulderRollShouldEnd;
WAD_STATIC S32 boulderRollShouldStart;
static zParEmitter* sEmitSpinBubbles;
static zParEmitter* sEmitSundae;
static zParEmitter* sEmitStankBreath;
static F32 sReticleRot[2];
WAD_STATIC F32 sReticleAlpha[2];
WAD_STATIC S32 sTypeOfTarget[2];
static F32 sTimeToRetarget[2];
xEnt* gReticleTarget[2];
static RwRaster* sBowlingLaneRast;
static RwRaster* sThrowArrowRast;
static xVec3 sThrowDecalPos; // unreferenced, kept by retail
static F32 sThrowDecalFactor;
static F32 sShowThrowArrow;
static xVec3 update_motion;
static xVec3 req_motion;
static xVec3 precollide_motion;
static U32 sPlayerKillAnimMovement;
eSNDCurrentPlayer gCurrentPlayer;
ePlayerEnum gCurrentCostume;
F32 floor_safe_tmr;
static F32 bbash_start_ht;
static F32 bbash_end_tmr;
static F32 bbash_tmr;
static F32 bbash_vel;
S32 gBashButton;
static zNMEHazard* sGauntletHaz;
static S32 sGauntletStartHaz;
static F32 sGauntletBlastRadius;
static S32 sGauntletBoundHit;
static F32 sBashSoundTimer;
static S32 bbash_hit;
static S32 bbounce_hit;
static S32 cartwheel_hit;
static S32 cartwheel_first;
S32 gCartwheelButton;
static xEnt* contactEnt;
static S32 hammer_button;
static S32 hammer_start;
static S32 hammer_throw;
static S32 hammer_first;
static F32 sLastHammerTime;
static F32 sThrowChargeTime;
F32 gThrowPower;
static F32 idle_tmr;
static F32 inact_tmr;
static F32 stun_power_tmr;
static xBase* stun_rumble[2][4];
static F32 tslide_maxspd;
static F32 tslide_maxspd_tmr;
static F32 tslide_inair_tmr;
static F32 tslide_dbl_tmr;
static U32 tslide_ground;
static xVec3 tslide_lastrealvel;
static S32 in_goo;
static S32 lin_goo;
static F32 in_goo_tmr;
static S32 sOnBouncBack;
static U32 player_hitlist_anim;
S32 player_hit;
static U32 player_idle_anim;
static U32 sMountType;
static xEnt* sMountObject;
static F32 sMountTimer;
static xVec3 last_center; // unreferenced, kept by retail
static U32 sShouldBubbleBowl;
static F32 sBubbleBowlTimer;
WAD_STATIC U32 sSpatulaGrabbed;
static enum
{
    WallJumpResult_NoJump,
    WallJumpResult_Jump
} sWallJumpResult;
static xVec3 sWallNormal;
static xVec3 sWallContactPos;
static zSurfaceProps* sWallCollisionSurface;
static F32 shrapDeathTimer = -1.0f;
static F32 sPlayerNPC_KnockBackTime = 0.4f;
static F32 sPlayerNPC_KnockBackVel = 8.4f;
char* cBowlingLaneTexture = "bowling_lane";
char* cThrowArrowTexture = "directional";
static F32 update_dt = 1.0f / 60.0f;
static F32 last_update_dt = 1.0f / 60.0f;
eSNDCurrentPlayer lastgCurrentPlayer = eCurrentPlayerCount;
static U8 spongebobCostumeAtomicOrder[11][11] = {
    { 3, 4, 7, 8, 0, 5, 6, 1, 2, 9, 10 }, { 3, 4, 7, 8, 0, 5, 6, 1, 2, 9, 10 },
    { 3, 4, 7, 8, 0, 5, 6, 1, 2, 9, 10 }, { 3, 4, 7, 8, 0, 5, 6, 1, 2, 9, 10 },
    { 3, 4, 7, 8, 0, 5, 6, 1, 2, 9, 10 }, { 3, 4, 7, 8, 0, 5, 6, 1, 2, 9, 10 },
    { 3, 4, 7, 8, 0, 5, 6, 1, 2, 9, 10 }, { 0, 1, 2, 5, 6, 3, 4, 7, 8, 9, 10 },
    { 3, 4, 7, 8, 0, 5, 6, 1, 2, 9, 10 }, { 3, 4, 7, 8, 1, 2, 0, 5, 6, 9, 10 },
    { 3, 4, 7, 8, 0, 1, 2, 5, 6, 9, 10 },
};
char* sDirDecalBlendLabels[11] = {
    "BLENDZERO",      "BLENDONE",          "BLENDSRCCOLOR",    "BLENDINVSRCCOLOR",
    "BLENDSRCALPHA",  "BLENDINVSRCALPHA",  "BLENDDESTALPHA",   "BLENDINVDESTALPHA",
    "BLENDDESTCOLOR", "BLENDINVDESTCOLOR", "BLENDSRCALPHASAT",
};
static const U32 sDirDecalBlendValues[11] = {
    rwBLENDZERO,      rwBLENDONE,          rwBLENDSRCCOLOR,    rwBLENDINVSRCCOLOR,
    rwBLENDSRCALPHA,  rwBLENDINVSRCALPHA,  rwBLENDDESTALPHA,   rwBLENDINVDESTALPHA,
    rwBLENDDESTCOLOR, rwBLENDINVDESTCOLOR, rwBLENDSRCALPHASAT,
};

static const struct
{
    U32 level;
    U32 total;
} TreasureChestsPerLevel[15] = {
    { 'BB02', 1 }, { 'BB03', 2 }, { 'BB01', 3 }, { 'DE01', 3 }, { 'DE02', 3 },
    { 'TT01', 3 }, { 'TT02', 2 }, { 'TR01', 3 }, { 'TR02', 5 }, { 'JK01', 4 },
    { 'GG02', 3 }, { 'SC02', 3 }, { 'PT01', 3 }, { 'PT02', 4 }, { 0, 0 },
};

// Unreferenced, but retail keeps them in .rodata right after TreasureChestsPerLevel.
static const xVec3 velbase = { 4.0f, 1.0f, 4.0f };
static const xVec3 velvar = { 1.0f, 0.25f, 1.0f };

static _tagePlayerStreamSnd sSeeEnemySounds[NME_TYPE_LAST_SEE_TYPE - NME_TYPE_FIRST_SEE_TYPE + 1] = {
    ePlayerStreamSnd_Invalid,        ePlayerStreamSnd_Invalid,
    ePlayerStreamSnd_SeeJellyFish,   ePlayerStreamSnd_SeeJellyFish,
    ePlayerStreamSnd_SeeTurret,      ePlayerStreamSnd_SeeTurret,
    ePlayerStreamSnd_SeeTurret,      ePlayerStreamSnd_SeeTurret,
    ePlayerStreamSnd_SeeTurret,      ePlayerStreamSnd_SeeTurret,
    ePlayerStreamSnd_SeeTurret,      ePlayerStreamSnd_SeeTurret,
    ePlayerStreamSnd_SeeTurret,      ePlayerStreamSnd_Invalid,
    ePlayerStreamSnd_SeeFogger,      ePlayerStreamSnd_SeeFogger,
    ePlayerStreamSnd_SeeFogger,      ePlayerStreamSnd_SeeSlammer,
    ePlayerStreamSnd_SeeSlammer,     ePlayerStreamSnd_SeeSlammer,
    ePlayerStreamSnd_SeeFlinger,     ePlayerStreamSnd_SeeFlinger,
    ePlayerStreamSnd_SeeFlinger,     ePlayerStreamSnd_SeeSpinner,
    ePlayerStreamSnd_SeeSpinner,     ePlayerStreamSnd_SeeSpinner,
    ePlayerStreamSnd_SeePopper,      ePlayerStreamSnd_SeePopper,
    ePlayerStreamSnd_SeePopper,      ePlayerStreamSnd_SeeMiniMerv,
    ePlayerStreamSnd_SeeMiniMerv,    ePlayerStreamSnd_SeeMiniMerv,
    ePlayerStreamSnd_SeeMervyn,      ePlayerStreamSnd_SeeMervyn,
    ePlayerStreamSnd_SeeMervyn,      ePlayerStreamSnd_SeeBucketotron,
    ePlayerStreamSnd_SeeBucketotron, ePlayerStreamSnd_SeeBucketotron,
    ePlayerStreamSnd_SeeBucketotron, ePlayerStreamSnd_SeeBucketotron,
    ePlayerStreamSnd_SeeBucketotron, ePlayerStreamSnd_SeeBucketotron,
};

// zEntPlayer.cpp's own PlayerStreakInfo (zCar.cpp has a different one of the same name)
struct PlayerStreakInfo
{
    U32 activated;
    U32 streakID;
    xModelTag* tagA;
    xModelTag* tagB;
    F32 alphaStart;
    S32 streakTaper;
    xColor_tag colA;
    xColor_tag colB;
    xVec3 a;
    xVec3 b;
};

// clang-format off
static PlayerStreakInfo sStreakInfo[3][4] = {
    {
        { FALSE, 0xDEAD, &sSpongeBobLHand, &sSpongeBobLElbow, 0.7f, 1, { 0xFF, 0xFF, 0x80, 0xFF }, { 0xC4, 0xC4, 0x40, 0xFF } },
        { FALSE, 0xDEAD, &sSpongeBobRHand, &sSpongeBobRElbow, 0.7f, 1, { 0xFF, 0xFF, 0x80, 0xFF }, { 0xC4, 0xC4, 0x40, 0xFF } },
        { FALSE, 0xDEAD, &sSpongeBobLFoot, &sSpongeBobLKnee, 0.7f, 1, { 0xFF, 0xFF, 0x80, 0xFF }, { 0xC4, 0xC4, 0x40, 0xFF } },
        { FALSE, 0xDEAD, &sSpongeBobRFoot, &sSpongeBobRKnee, 0.7f, 1, { 0xFF, 0xFF, 0x80, 0xFF }, { 0xC4, 0xC4, 0x40, 0xFF } },
    },
    {
        { FALSE, 0xDEAD, &sPatrickLHand, &sPatrickLElbow, 0.6f, 1, { 0xFF, 0x85, 0x85, 0xFF }, { 0xFF, 0x85, 0x85, 0xFF } },
        { FALSE, 0xDEAD, &sPatrickRHand, &sPatrickRElbow, 0.6f, 1, { 0xFF, 0x85, 0x85, 0xFF }, { 0xFF, 0x85, 0x85, 0xFF } },
        { FALSE, 0xDEAD, &sPatrickLFoot, &sPatrickLKnee, 0.6f, 1, { 0xFF, 0x85, 0x85, 0xFF }, { 0xFF, 0x85, 0x85, 0xFF } },
        { FALSE, 0xDEAD, &sPatrickRFoot, &sPatrickRKnee, 0.6f, 1, { 0xFF, 0x85, 0x85, 0xFF }, { 0xFF, 0x85, 0x85, 0xFF } },
    },
    {
        { FALSE, 0xDEAD, NULL, NULL, 0.6f, 1, { 0xFF, 0x85, 0x85, 0xFF }, { 0xFF, 0x85, 0x85, 0xFF } },
        { FALSE, 0xDEAD, NULL, NULL, 0.6f, 1, { 0xFF, 0x85, 0x85, 0xFF }, { 0xFF, 0x85, 0x85, 0xFF } },
        { FALSE, 0xDEAD, NULL, NULL, 0.6f, 1, { 0xFF, 0x85, 0x85, 0xFF }, { 0xFF, 0x85, 0x85, 0xFF } },
        { FALSE, 0xDEAD, NULL, NULL, 0.6f, 1, { 0xFF, 0x85, 0x85, 0xFF }, { 0xFF, 0x85, 0x85, 0xFF } },
    },
};
// clang-format on

// Only the name of this type is known (from the mangled zEntPlayer_ReticleRender); the PS2 DWARF
// has no definition. Retail passes the reticle index (0 or 1) and the renderer ignores it.
enum ePlayerReticleType
{
};

static S32 sGauntletBubbleEffectNum = 100;
static F32 sGauntletBubbleEffectRadius = 0.3f;
static F32 sGauntletBubbleEffectVel = 2.0f;
static F32 sGauntletBubbleEffectRandVel = 1.0f;
static F32 sGauntletTweakButtonY = -1.55f;
static F32 sGauntletTweakPlatBashY = -0.8f;
static S32 player_hit_anim = 1;
static U32 player_dead_anim = 1;
static F32 sBubbleBowlLastWindupTime = -1.0f;
static F32 sBubbleBowlMultiplier = 1.0f;

static void PlayerAbsControl(xEnt* ent, F32 x, F32 z, F32 dt);
static void PlayerSwingUpdate(xEnt* ent, F32 mag, F32 angle, F32 dt);
static U32 DblJumpCB(xAnimTransition* tran, xAnimSingle* anim, void* object);
static U32 JumpCB(xAnimTransition* tran, xAnimSingle* anim, void* object);
void zEntPlayerCommonInit(xEnt* ent, xEntAsset* asset);
static void PlayerRotMatchUpdateEnt(xEnt* ent, xScene* sc, F32 dt, void* fdata);
static void zEntPlayer_SNDInit();
static xEnt* GetPatrickTarget(xEnt* ent);
static void zEntPlayer_BubbleBowlLaneRender(zEnt* ent);
static void BoulderVEventCB(xBase*, xBase*, U32, const F32*, xBase*, U32);
static void zEntPlayer_BoulderVehicleUpdate(xEnt* ent, xScene* sc, F32 dt);
static void zEntPlayer_BoulderVehicleMove(xEnt* ent, xScene* sc, F32 dt, xEntFrame* frame);
static void zEntPlayer_BoulderVehicleRender(zEnt* ent);
static void zEntPlayer_PredictionUpdate(xEnt* ent, F32 dt);
static void ReturnTheFavor(zNMEHazard* haz);
zEntPlayerBowlTarget* zEntPlayerBowlTarget_Add(xVec3* target);
void zEntPlayerBowlTarget_Remove(zEntPlayerBowlTarget* target);
static inline S32 zEntPlayerBowlTarget_Exists(xEnt* ent);
static xEnt* PlayerCollCheckOneVillain(xEnt* ent, xScene* sc, void* data);
static U32 CollidePyramidBoxTop(xCollis* coll, xBox* box, F32 height, xSphere* sph);
static xEnt* PlayerCollCheckOneEnt(xEnt* ent, xScene* sc, void* data);
static void PlayerCollsSelectDepen(xEnt* ent, xScene* sc, F32 dt);
static bool HazardCheck(zNMEHazard& haz, void* context);
static S32 CheckObjectAgainstBound(xEnt* cbent, void* cbdata);
static void SlideTrackUpdate(xEnt* ent);
static void zEntPlayerDriveUpdate(xEnt* p, xScene* sc, F32 dt);
static void zEntPlayerSurfDamageUpdate(xEnt* p, xScene* sc, F32 dt);
static void zEntPlayerFloorUpdate(xEnt* p, xScene* sc, F32 dt);
static void zEntPlayerVelUpdate(xEnt* ent, xScene* sc, F32 dt);
static void zEntPlayer_StreakFX(xEnt* ent, F32 dt);
static void PlayerLedgeUpdate(xEnt* ent, xScene* sc, F32 dt);
static void PlayerLedgeInit(zLedgeGrabParams* ledge, xModelInstance* model);
static void zEntPlayer_UpdateVelocityBlur();
static void zEntPlayerEGenUpdate(xEnt* p, xScene* sc, F32);
static void zEntPlayerJumpUpdate(xEnt* ent, xScene* sc, F32 dt);
static void zEntPlayerTSlideUpdate(xEnt* p, xScene* sc, F32 dt);
static void PlayerTeeterCheck(xEnt* ent, xScene* sc, F32 dt);
void zEntPlayerCollide(xEnt* ent, xScene* sc, F32 dt);
void zEntPlayerCollTrigger(xEnt* ent, xScene* sc);
void zEntPlayer_CheckCritterContact(xEnt* player, F32 dt);
void zEntPlayer_Move(xEnt* ent, xScene* sc, F32 dt, xEntFrame* frame);

static F32 CalcJumpImpulse_Smooth(F32 g, F32 j, F32 h, F32 Tgc, F32 Tgs);

static inline void CalcJumpImpulse(zJumpParam* param, const zPlayerSettings* settings)
{
    if (settings == NULL)
    {
        settings = globals.player.s;
    }

    param->ImpulseVel =
        CalcJumpImpulse_Smooth(globals.player.g.Gravity, settings->JumpGravity, param->PeakHeight,
                               param->TimeGravChange, settings->GravSmooth);
}

inline void zEntPlayerJumpStart(xEnt* ent, zJumpParam* jump)
{
    globals.player.Jump_CurrGravity = globals.player.s->JumpGravity;
    globals.player.Jump_HoldTimer = jump->TimeHold;
    globals.player.Jump_ChangeTimer = jump->TimeGravChange;

    if (ent->frame->vel.y > 0.0f && globals.player.JumpState == 2)
    {
        ent->frame->vel.y = jump->ImpulseVel;
    }
    else
    {
        ent->frame->vel.y = jump->ImpulseVel;
    }

    globals.player.JumpState = 2;

    if (ent->model->Anim->Single->State->UserFlags & 0x100)
    {
        globals.player.Bounced = 1;
    }
}

// No GC symbol: inlined into every caller, the out-of-line copy dead-stripped.
static void zEntPlayerJumpAddDriver(xEnt* ent)
{
    if (sDriveVel.y > 0.0f)
    {
        ent->frame->vel.y += sDriveVel.y;
    }
}

static inline U32 RunCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return (globals.player.JumpState == 0 || globals.player.JumpState == 1) &&
           globals.player.Speed == 2 && !surfSlickRatio && oob_state::oob_timer() < 0.0f;
}

static inline U32 BubbleBashCheck(xAnimTransition*, xAnimSingle*, void*)
{
    if (globals.player.cheat_mode)
    {
        return false;
    }

    if (!globals.player.g.PowerUp[PLYR_ATAK_SB_BASH - 1])
    {
        return false;
    }

    if (zEntTeleportBox_playerIn())
    {
        return false;
    }

    if (sPlayerCollAdjust > 0.2f)
    {
        return false;
    }

    if (gBashButton)
    {
        return false;
    }

    return !globals.player.ControlOff && (globals.pad0->on & XPAD_BUTTON_SQUARE);
}

void zEntPlayerKillCarry()
{
    if (globals.player.carry.grabbed)
    {
        sPatCarryCommentTimer.mCurrentTime = 0.0f;

        if (!zThrown_KillFruit(globals.player.carry.grabbed))
        {
            if (globals.player.carry.grabbed->baseType == eBaseTypeDestructObj)
            {
                zEntEvent(globals.player.carry.grabbed, eEventDestroy);
            }
            else if (globals.player.carry.grabbed->baseType == eBaseTypeNPC &&
                     ((zNMECommon*)globals.player.carry.grabbed)->itemType >= NME_TYPE_TIKI &&
                     ((zNMECommon*)globals.player.carry.grabbed)->itemType <= NME_TYPE_TIKI_STONE)
            {
                ((zNMECommon*)globals.player.carry.grabbed)
                    ->Damage(DMGTYP_THUNDER_TIKI_EXPLOSION, NULL, NULL, -1, ePowerupLevelNormal);
            }
            else if (globals.player.carry.grabbed->baseType == eBaseTypeNPC)
            {
                zNMECommon* npc = (zNMECommon*)globals.player.carry.grabbed;
                npc->SetCarryState(zNMECARRY_THROW);

                xMat3x3 targetingMat;
                xMat3x3Identity(&targetingMat);
                xMat3x3RotY(&targetingMat, sThrowAngle);
                zThrown_LaunchDir(globals.player.carry.grabbed, &targetingMat.at);
            }
        }

        globals.player.carry.grabbed = NULL;
    }
}

void zEntPlayerControlOn(zControlOwner owner)
{
    U32 originalValue = globals.player.ControlOff;
    globals.player.ControlOff &= ~owner;

    if (originalValue != globals.player.ControlOff)
    {
        if (globals.player.ControlOff & (CONTROL_OWNER_EVENT | CONTROL_OWNER_FLY_CAM))
        {
            xSndMgrSelectListenerMode(xSndListener_CAMERA);
        }
        else
        {
            xSndMgrSelectListenerMode(xSndListener_PLAYER);
        }
    }

    if (!(owner & (CONTROL_OWNER_TELEPORT_BOX | CONTROL_OWNER_FROZEN | CONTROL_OWNER_REWARDANIM |
                   CONTROL_OWNER_BUNGEE | CONTROL_OWNER_SPRINGBOARD)) &&
        originalValue)
    {
        gBowlButton =
            (!globals.player.ControlOff && (globals.pad0->on & XPAD_MOVE(XPAD_BUTTON_O))) ? TRUE :
                                                                                            FALSE;
        gBashButton =
            (!globals.player.ControlOff && (globals.pad0->on & XPAD_BUTTON_SQUARE)) ? TRUE : FALSE;
        gCartwheelButton =
            (!globals.player.ControlOff && (globals.pad0->on & XPAD_MOVE(XPAD_BUTTON_O))) ? TRUE :
                                                                                            FALSE;
    }
}

void zEntPlayerControlOff(zControlOwner owner)
{
    U32 originalValue = globals.player.ControlOff;
    globals.player.ControlOff |= owner;
    globals.player.ControlOffTimer = 1.0f;

    if (originalValue != globals.player.ControlOff)
    {
        if (globals.player.ControlOff & (CONTROL_OWNER_EVENT | CONTROL_OWNER_FLY_CAM))
        {
            xSndMgrSelectListenerMode(xSndListener_CAMERA);
        }
        else
        {
            xSndMgrSelectListenerMode(xSndListener_PLAYER);
        }
    }

    zEntPlayerKillCarry();
}

void VillainNoticesPlayer(zNMECommon* npc)
{
    en_npctyp ntyp;
    _tagePlayerStreamSnd sound;

    ntyp = (en_npctyp)npc->itemType;

    if (ntyp < NME_TYPE_FIRST_SEE_TYPE || ntyp > NME_TYPE_LAST_SEE_TYPE)
    {
        return;
    }

    sound = sSeeEnemySounds[ntyp - NME_TYPE_FIRST_SEE_TYPE];
    if (sound == ePlayerStreamSnd_Invalid)
    {
        return;
    }

    switch (gCurrentPlayer)
    {
    case eCurrentPlayerSpongeBob:
    case eCurrentPlayerPatrick:
    {
        xMat4x3 camMat = zCamGetMatrix();
        xVec3 vDirCamToEnemy = *(xVec3*)&npc->model->Mat->pos - camMat.pos;

        if (vDirCamToEnemy.dot(camMat.at) > 0.0f)
        {
            zEntPlayer_SNDPlayStream(sound);
        }
        break;
    }
    case eCurrentPlayerCar:
    case eCurrentPlayerSpongePat:
        break;
    }
}

void SetPlayerKillsVillainTimer(F32 time)
{
    switch (gCurrentPlayer)
    {
    case eCurrentPlayerSpongeBob:
    case eCurrentPlayerPatrick:
        zEntPlayer_SNDPlayStream(ePlayerStreamSnd_EnemyDefeated);
        break;
    case eCurrentPlayerSpongePat:
        sSlidingOneLiners.PlaySound(0);
        break;
    }
}

// No GC symbol: inlined into its callers (zEntPlayer_BoulderVehicleUpdate, PlayerAbsControl).
static void DampenControls(F32* angle, F32* mag, F32 x, F32 y)
{
    *angle = xatan2(x, y);

    if (x > -globals.player.g.AnalogMin && x < globals.player.g.AnalogMin)
    {
        x = 0.0f;
    }

    if (y > -globals.player.g.AnalogMin && y < globals.player.g.AnalogMin)
    {
        y = 0.0f;
    }

    if (!x && !y)
    {
        *angle = 0.0f;
        *mag = 0.0f;
    }
    else
    {
        if (xabs(x) > xabs(y))
        {
            *mag = xabs(x);
        }
        else
        {
            *mag = xabs(y);
        }

        *mag = (*mag - globals.player.g.AnalogMin) /
               (globals.player.g.AnalogMax - globals.player.g.AnalogMin);

        if (*mag < 0.0f)
        {
            *mag = 0.0f;
            *angle = 0.0f;
        }
        else if (*mag > 1.0f)
        {
            *mag = 1.0f;
        }
    }
}

// No GC symbol: inlined into its only caller (zEntPlayer_Move).
static void TurnToFace(xEnt* ent, const xVec3* target, F32 speedLimit, F32 dt)
{
    xVec3 currentFacing = ent->frame->mat.at;
    xVec3Normalize(&currentFacing, &currentFacing);

    F32 angle = xVec3Dot(&currentFacing, target);
    const F32 maxAngle = 0.9999f;
    if (angle < maxAngle)
    {
        xVec3Cross(&ent->frame->drot.axis, &currentFacing, target);
        xVec3Normalize(&ent->frame->drot.axis, &ent->frame->drot.axis);

        angle = xacos(angle);
        if (angle > speedLimit * dt)
        {
            angle = speedLimit * dt;
        }

        if (ent->frame->drot.axis.y < 0.0f)
        {
            angle = -angle;
        }

        ent->frame->drot.angle = angle;
        ent->frame->mode |= 0x20;
    }
}

// No symbol on GC (not in the PS2 DWARF either): always inlined.
static inline void CalcAnimSpeed(xEnt* ent, F32 f, F32* pf)
{
    if (pf[0])
    {
        f = f / pf[0];
        if (f < pf[1])
        {
            f = pf[1];
        }
        else if (f > pf[2])
        {
            f = pf[2];
        }
        ent->model->Anim->Single->CurrentSpeed = f;
    }
}

// No symbol on GC (not in the PS2 DWARF either): always inlined.
static inline void LeanUpdate(F32 angle, F32 dt)
{
    F32 lerp;
    F32 absAngle = xabs(angle);
    if (absAngle < 0.08726647f)
    {
        lerp = 0.0f;
    }
    else if (absAngle > 0.2617994f)
    {
        lerp = 1.0f;
    }
    else
    {
        lerp = 5.729578f * (absAngle - 0.08726647f);
    }

    if (angle > 0.0f)
    {
        lerp = -lerp;
    }

    lerp += 1.0f;

    F32 t = 6.0f * (lerp - globals.player.LeanLerp);
    globals.player.LeanLerp += t * dt;
}

// No symbol on GC (not in the PS2 DWARF either): always inlined.
static inline void PlayerArrive(xEnt* ent, xBase* base)
{
    globals.player.AutoMoveSpeed = 0;
    zEntEvent(base, ent, 0x1f);
    if (base->baseType == eBaseTypeMovePoint)
    {
        zEntEvent(ent, base, 0x1f);
    }
}

#define CLAMP_ANGLE(a)                                                                             \
    if (a > PI)                                                                                    \
    {                                                                                              \
        a -= 2 * PI;                                                                               \
    }                                                                                              \
    else if (a < -PI)                                                                              \
    {                                                                                              \
        a = a + 2 * PI;                                                                            \
    }

static void PlayerAbsControl(xEnt* ent, F32 x, F32 z, F32 dt)
{
    U32 animUserFlag;
    U32 blendUserFlag;
    F32 angle;
    F32 mag;
    F32 scalemag;
    F32 dir_dp;
    F32 turnfactor;
    F32 diffAngle;
    F32 autodist2d;

    maxVelmag = 0.0f;
    diffAngle = 0.0f;
    autodist2d = 0.0f;
    dir_dp = 0.0f;
    scalemag = 1.0f;
    turnfactor = 7.0f;

    if (sPlayerUpdateIndex == 0)
    {
        sAnimPhysicsIncrement.x = sAnimPhysicsIncrement.y = sAnimPhysicsIncrement.z = 0.0f;
    }

    if (gTrcPad[0].state != TRC_PadInserted)
    {
        z = x = 0.0f;
    }

    if (globals.player.ControlOff || sHackStuckTimer || sCatchCapsuleTimer > 0.15f)
    {
        z = x = 0.0f;
    }

    xMat4x3* m = &ent->frame->mat;
    xVec3 euler;
    xMat3x3GetEuler(m, &euler);
    ent->frame->rot.angle = euler.x >= 0.0f ? euler.x : euler.x + 2 * PI;

    surfMaxSpeed = 0.0f;

    animUserFlag = ent->model->Anim->Single->State ? ent->model->Anim->Single->State->UserFlags : 0;
    blendUserFlag = ent->model->Anim->Single->Blend && ent->model->Anim->Single->Blend->State ?
                        ent->model->Anim->Single->Blend->State->UserFlags | 0x80000000 :
                        0;

    if (globals.player.KnockIntoAirTimer && (animUserFlag & 0x1e) == 0 && (animUserFlag & 0x1) == 0)
    {
        animUserFlag |= 0x8 | 0x2;
        animUserFlag &= ~0x80;
    }

    memset(&ent->frame->dpos, 0, sizeof(xVec3));

    if (globals.player.KnockBackTimer)
    {
        return;
    }

    if (animUserFlag & 0x100)
    {
        return;
    }

    if (globals.player.WallJumpState == k_WALLJUMP_STUCK)
    {
        return;
    }

    if (globals.player.WallJumpState == k_WALLJUMP_FLIGHT)
    {
        x *= 0.38f;
    }

    if (globals.player.ControlOff & 0x4000)
    {
        if (ent->frame->vel.x * ent->frame->vel.x + ent->frame->vel.z * ent->frame->vel.z > 0.01f)
        {
            ent->frame->mode &= ~0x2;

            diffAngle = xatan2(ent->frame->vel.x, ent->frame->vel.z) - ent->frame->rot.angle;
            CLAMP_ANGLE(diffAngle);

            ent->frame->drot.angle = 4.0f * diffAngle * dt;
            ent->frame->mode |= 0x20;

            if (xabs(ent->frame->drot.angle) < 0.006f)
            {
                if (xabs(diffAngle) > 0.006f)
                {
                    diffAngle = diffAngle > 0.0f ? 0.006f : -0.006f;
                }
                ent->frame->drot.angle = diffAngle;
            }
        }
    }
    else
    {
        DampenControls(&angle, &mag, x, z);
        xMat4x3Copy(&gPlayerAbsMat, &xglobals->cam->mat);

        if (globals.player.carry.grabTarget || globals.player.carry.throwTarget ||
            strcmp(ent->model->Anim->Single->State->Name, "Carry_HammerThrow") == 0)
        {
            if (!(strcmp(ent->model->Anim->Single->State->Name, "Carry_Pickup") == 0 ||
                  strcmp(ent->model->Anim->Single->State->Name, "Carry_Throw") == 0))
            {
                globals.player.carry.grabTarget = 0;
                if (strcmp(ent->model->Anim->Single->State->Name, "Carry_HammerThrow") != 0)
                {
                    globals.player.carry.throwTarget = NULL;
                }
            }
            else
            {
                ent->frame->mode &= ~0x2;

                diffAngle = globals.player.carry.targetRot - ent->frame->rot.angle;
                CLAMP_ANGLE(diffAngle);

                if (globals.player.carry.throwTarget)
                {
                    globals.player.carry.flyingToTarget = NULL;

                    if (diffAngle > 0.0f)
                    {
                        ent->frame->drot.angle = globals.player.carry.throwTargetRotRate * dt;
                    }
                    else
                    {
                        ent->frame->drot.angle = -globals.player.carry.throwTargetRotRate * dt;
                    }

                    if (xabs(ent->frame->drot.angle) > diffAngle)
                    {
                        ent->frame->drot.angle = diffAngle;
                    }

                    F32 dx = globals.player.carry.throwTarget->model->Mat->pos.x -
                             ent->model->Mat->pos.x;
                    F32 dz = globals.player.carry.throwTarget->model->Mat->pos.z -
                             ent->model->Mat->pos.z;
                    // not in the DWARF (dead): the result is unused, but retail keeps xsqrt's
                    // constant loads and isinf test.
                    F32 dist = xsqrt(dx * dx + dz * dz);
                }
                else
                {
                    ent->frame->drot.angle = 4.0f * diffAngle * dt;
                }

                ent->frame->mode |= 0x20;

                if (xabs(ent->frame->drot.angle) < 0.006f)
                {
                    if (xabs(diffAngle) > 0.006f)
                    {
                        diffAngle = diffAngle > 0.0f ? 0.006f : -0.006f;
                    }
                    ent->frame->drot.angle = diffAngle;
                }

                F32 atime = ent->model->Anim->Single->Time;
                if (globals.player.carry.grabLerpLast < globals.player.carry.grabLerpMax &&
                    atime > globals.player.carry.grabLerpMin &&
                    globals.player.carry.grabLerpMax > globals.player.carry.grabLerpMin)
                {
                    // not in the DWARF: retail computes the factor once for both components.
                    F32 lerp = -(
                        (MIN(globals.player.carry.grabLerpMax, atime) -
                         MAX(globals.player.carry.grabLerpMin, globals.player.carry.grabLerpLast)) /
                        (globals.player.carry.grabLerpMax - globals.player.carry.grabLerpMin));
                    ent->frame->dpos.x = lerp * globals.player.carry.grabOffset.x;
                    ent->frame->dpos.z = lerp * globals.player.carry.grabOffset.z;
                    ent->frame->mode |= 0x2;
                    globals.player.carry.grabLerpLast = atime;
                }

                sThrowAngle = xglobals->___player_ent_dont_use_directly->frame->rot.angle;
                // The goto is its own statement in the PS2 line table.
                goto lean;
            }
        }

        if (strcmp(ent->model->Anim->Single->State->Name, "SpatulaGrab01") == 0)
        {
            ent->frame->mode &= ~0x2;

            diffAngle = xatan2(xglobals->cam->mat.pos.x -
                                   xglobals->___player_ent_dont_use_directly->frame->mat.pos.x,
                               xglobals->cam->mat.pos.z -
                                   xglobals->___player_ent_dont_use_directly->frame->mat.pos.z) -
                        ent->frame->rot.angle;
            CLAMP_ANGLE(diffAngle);

            ent->frame->drot.angle = 4.0f * diffAngle * dt;
            ent->frame->mode |= 0x20;

            if (xabs(ent->frame->drot.angle) < 0.006f)
            {
                if (xabs(diffAngle) > 0.006f)
                {
                    diffAngle = diffAngle > 0.0f ? 0.006f : -0.006f;
                }
                ent->frame->drot.angle = diffAngle;
            }
        }
        else
        {
            if (mag)
            {
                F32 camAngle = xatan2(gPlayerAbsMat.right.z, gPlayerAbsMat.right.x);
                angle -= camAngle;
                if (angle > 2 * PI)
                {
                    angle -= 2 * PI;
                }
                else if (angle < 0.0f)
                {
                    angle += 2 * PI;
                }

                if (strcmp(ent->model->Anim->Single->State->Name, "Carry_HammerSpin") == 0 ||
                    strcmp(ent->model->Anim->Single->State->Name, "Carry_HammerThrow") == 0)
                {
                    diffAngle = angle - sThrowAngle;
                    CLAMP_ANGLE(diffAngle);

                    F32 drot = dt * (diffAngle * turnfactor);
                    sThrowAngle += drot;
                    if (sThrowAngle > PI)
                    {
                        sThrowAngle -= 2 * PI;
                    }
                    else if (sThrowAngle < -PI)
                    {
                        sThrowAngle += 2 * PI;
                    }
                }
                else
                {
                    sThrowAngle = xglobals->___player_ent_dont_use_directly->frame->rot.angle;

                    if ((animUserFlag & (0x800 | 0x80)) == 0)
                    {
                        diffAngle = angle - ent->frame->rot.angle;
                        CLAMP_ANGLE(diffAngle);

                        dir_dp = icos(diffAngle);
                        ent->frame->drot.angle = dt * (diffAngle * turnfactor);
                        ent->frame->mode |= 0x20;
                    }

                    if (globals.player.IsBubbleBowling)
                    {
                        diffAngle *= 0.1f;
                        dir_dp = icos(diffAngle);
                        ent->frame->drot.angle = dt * (diffAngle * turnfactor);

                        xMat3x3 rotY;
                        xMat3x3RotY(&rotY, ent->frame->drot.angle);
                        xMat3x3RMulVec(&ent->frame->vel, &rotY, &ent->frame->vel);
                    }
                }

                if (animUserFlag & 0x800)
                {
                    dir_dp = 1.0f;
                }

                if (mag > globals.player.s->MoveSpeed[3])
                {
                    F32 m = globals.player.SpeedMult;
                    if (mag < globals.player.s->MoveSpeed[4])
                    {
                        globals.player.Speed = 1;
                        maxVelmag = globals.player.s->MoveSpeed[1] * m;
                        scalemag = m * (mag * globals.player.s->MoveSpeed[1]) /
                                   globals.player.s->MoveSpeed[4];
                    }
                    else
                    {
                        globals.player.Speed = 2;
                        maxVelmag = globals.player.s->MoveSpeed[2] * m;

                        scalemag =
                            (mag - globals.player.s->MoveSpeed[4]) /
                            (globals.player.s->MoveSpeed[5] - globals.player.s->MoveSpeed[4]);
                        if (scalemag > 1.0f)
                        {
                            scalemag = 1.0f;
                        }

                        scalemag = scalemag * (globals.player.s->MoveSpeed[2] * m -
                                               globals.player.s->MoveSpeed[1] * m) +
                                   globals.player.s->MoveSpeed[1] * m;
                    }
                }
            }
            else
            {
                dir_dp = 0.0f;
                globals.player.Speed = 0;
            }

            ent->frame->mode &= ~0x2;

            if (sLassoInfo->swingTarget)
            {
                PlayerSwingUpdate(ent, mag, angle, dt);

                if (globals.player.HangVel.x * globals.player.HangVel.x +
                        globals.player.HangVel.y * globals.player.HangVel.y +
                        globals.player.HangVel.z * globals.player.HangVel.z >
                    30.0f)
                {
                    xVec3 belly;
                    iModelTagEval(ent->model->Data, &sPatrickBelly, ent->model->Mat, &belly);
                    zFX_SpawnBubbleTrail(&belly, 1);
                }
                return;
            }

            if (sLassoCamLinger)
            {
                F32 curFactor = zCameraGetLassoCamFactor();
                curFactor += dt;
                if (curFactor > 1.0f)
                {
                    zCameraDisableLassoCam();
                    sLassoCamLinger = 0;
                }
                else
                {
                    zCameraSetLassoCamFactor(curFactor);
                }
            }

            zPlayerGlobals* pg = &globals.player;
            if (pg->HangEnt)
            {
                return;
            }

            if ((pg->SlideTrackSliding & 1 && (pg->JumpState == 0 || pg->JumpState == 1)) ||
                (pg->SlideTrackDecay && pg->JumpState != 0 && pg->JumpState != 1))
            {
                F32 slideAccel =
                    (pg->SlideTrackVel.x * pg->SlideTrackDir.x +
                     pg->SlideTrackVel.z * pg->SlideTrackDir.z - pg->g.SlideAccelVelMin) /
                    (pg->g.SlideAccelVelMax - pg->g.SlideAccelVelMin);
                if (slideAccel < 0.0f)
                {
                    slideAccel = pg->g.SlideAccelStart;
                }
                else if (slideAccel > 1.0f)
                {
                    slideAccel = pg->g.SlideAccelEnd;
                }
                else
                {
                    slideAccel = slideAccel * (pg->g.SlideAccelEnd - pg->g.SlideAccelStart) +
                                 pg->g.SlideAccelStart;
                }

                pg->SlideTrackVel.x += slideAccel * (dt * pg->SlideTrackDir.x);
                pg->SlideTrackVel.z += slideAccel * (dt * pg->SlideTrackDir.z);

                F32 accelX = dt * (mag * isin(angle));
                F32 accelZ = dt * (mag * icos(angle));

                F32 fwdComponent = accelX * pg->SlideTrackDir.x + accelZ * pg->SlideTrackDir.z;
                F32 sideComponent = accelZ * pg->SlideTrackDir.x - accelX * pg->SlideTrackDir.z;

                F32 veldown = pg->SlideTrackVel.x * pg->SlideTrackDir.x +
                              pg->SlideTrackVel.z * pg->SlideTrackDir.z;
                if (veldown < 1.0f)
                {
                    if (fwdComponent < 0.0f)
                    {
                        fwdComponent = 0.0f;
                    }
                }
                else if (veldown < 6.0f && fwdComponent < 0.0f)
                {
                    fwdComponent *= (veldown - 1.0f) / 5.0f;
                }

                if (fwdComponent > 0.0f)
                {
                    pg->SlideTrackVel.x +=
                        pg->SlideTrackDir.x * (pg->g.SlideAccelPlayerFwd * fwdComponent);
                    pg->SlideTrackVel.z +=
                        pg->SlideTrackDir.z * (pg->g.SlideAccelPlayerFwd * fwdComponent);
                }
                else
                {
                    pg->SlideTrackVel.x +=
                        pg->SlideTrackDir.x * (pg->g.SlideAccelPlayerBack * fwdComponent);
                    pg->SlideTrackVel.z +=
                        pg->SlideTrackDir.z * (pg->g.SlideAccelPlayerBack * fwdComponent);
                }

                pg->SlideTrackVel.x -=
                    pg->SlideTrackDir.z * (pg->g.SlideAccelPlayerSide * sideComponent);
                pg->SlideTrackVel.z +=
                    pg->SlideTrackDir.x * (pg->g.SlideAccelPlayerSide * sideComponent);

                scalemag = xsqrt(pg->SlideTrackVel.x * pg->SlideTrackVel.x +
                                 pg->SlideTrackVel.z * pg->SlideTrackVel.z);
                if (scalemag >= tslide_maxspd)
                {
                    if (globals.player.SlideTrackDecay == globals.player.g.SlideAirHoldTime)
                    {
                        tslide_maxspd_tmr += dt;
                        if (fwdComponent > 0.0f)
                        {
                            tslide_maxspd_tmr +=
                                globals.player.g.SlideVelMaxIncAccel * (fwdComponent * dt);
                        }
                    }

                    if (tslide_maxspd_tmr <= pg->g.SlideVelMaxIncTime)
                    {
                        tslide_maxspd = (tslide_maxspd_tmr / pg->g.SlideVelMaxIncTime) *
                                            (pg->g.SlideVelMaxEnd - pg->g.SlideVelMaxStart) +
                                        pg->g.SlideVelMaxStart;
                    }
                }
                else if (scalemag > pg->g.SlideVelMaxStart)
                {
                    tslide_maxspd_tmr =
                        (pg->g.SlideVelMaxIncTime * (scalemag - pg->g.SlideVelMaxStart)) /
                        (pg->g.SlideVelMaxEnd - pg->g.SlideVelMaxStart);
                    tslide_maxspd = scalemag;
                }
                else
                {
                    tslide_maxspd_tmr = 0.0f;
                    tslide_maxspd = pg->g.SlideVelMaxStart;
                }

                if (scalemag > tslide_maxspd)
                {
                    pg->SlideTrackVel.x *= tslide_maxspd / scalemag;
                    pg->SlideTrackVel.z *= tslide_maxspd / scalemag;
                }

                diffAngle =
                    xatan2(pg->SlideTrackVel.x, pg->SlideTrackVel.z) - ent->frame->rot.angle;
                CLAMP_ANGLE(diffAngle);

                ent->frame->drot.angle = 4.0f * diffAngle * dt;
                ent->frame->mode |= 0x20;

                if (xabs(ent->frame->drot.angle) < 0.006f)
                {
                    if (xabs(diffAngle) > 0.006f)
                    {
                        diffAngle = diffAngle > 0.0f ? 0.006f : -0.006f;
                    }
                    ent->frame->drot.angle = diffAngle;
                }

                if (animUserFlag & 0x40)
                {
                    F32 targetLean = 0.0f;
                    if (mag)
                    {
                        targetLean = angle - ent->frame->rot.angle;
                        CLAMP_ANGLE(targetLean);
                    }

                    targetLean = -targetLean / 0.87266463f;
                    if (targetLean < -1.0f)
                    {
                        targetLean = -1.0f;
                    }
                    else if (targetLean > 1.0f)
                    {
                        targetLean = 1.0f;
                    }

                    globals.player.SlideTrackLean +=
                        0.04f * (targetLean - globals.player.SlideTrackLean);
                    ent->model->Anim->Single->BilinearLerp[0] =
                        1.0f + globals.player.SlideTrackLean;
                }
                else
                {
                    globals.player.SlideTrackLean = 0.0f;
                }

                ent->frame->dpos.x = dt * globals.player.SlideTrackVel.x;
                ent->frame->dpos.z = dt * globals.player.SlideTrackVel.z;
                ent->frame->mode |= 0x2;
                ent->frame->vel.x = 0.0f;
                ent->frame->vel.z = 0.0f;

                if ((globals.player.SlideTrackSliding & 1) == 0)
                {
                    globals.player.SlideTrackDecay -= dt;
                    if (globals.player.SlideTrackDecay < 0.0f)
                    {
                        globals.player.SlideTrackDecay = 0.0f;
                        ent->frame->vel.x = globals.player.SlideTrackVel.x;
                        ent->frame->vel.z = globals.player.SlideTrackVel.z;
                    }
                }
                else
                {
                    globals.player.SlideTrackDecay = globals.player.g.SlideAirHoldTime;
                }
                return;
            }

            if (gCurrentPlayer == eCurrentPlayerSpongePat)
            {
                if (pg->JumpState != 1)
                {
                    pg->SlideTrackDecay = 0.0f;
                }
                else
                {
                    ent->frame->dpos.x = dt * pg->SlideTrackVel.x;
                    ent->frame->dpos.z = dt * pg->SlideTrackVel.z;
                    ent->frame->mode |= 0x2;
                }
            }
            else
            {
                pg->SlideTrackDecay = 0.0f;
            }

            if ((animUserFlag & 0x1e) != 0 || blendUserFlag & 0x1e)
            {
                U32 moveFlag = animUserFlag & 0x1e;
                if ((animUserFlag & 0x1e) == 0)
                {
                    moveFlag = blendUserFlag & 0x1e;
                }

                // The goto finish jumps are as in BfBB's PlayerAbsControl.
                switch (moveFlag)
                {
                case 0x2:
                    if (dir_dp <= 0.0f || !mag)
                    {
                        break;
                    }
                    mag = scalemag * dir_dp;
                    goto finish;
                case 0x4:
                    if (dir_dp <= 0.0f)
                    {
                        break;
                    }
                    mag = scalemag * dir_dp;
                    goto finish;
                case 0x4 | 0x2:
                    if (mag)
                    {
                        if (xabs(diffAngle) >= PI / 6)
                        {
                            if (diffAngle > 0.0f)
                            {
                                angle = PI / 6 + ent->frame->rot.angle;
                            }
                            else
                            {
                                angle = ent->frame->rot.angle - PI / 6;
                            }
                        }
                    }
                    else
                    {
                        angle = ent->frame->rot.angle;
                    }

                    if (animUserFlag & 0x20)
                    {
                        if (surfSlickRatio)
                        {
                            break;
                        }
                        mag = globals.player.DecelRunSpeed;
                        globals.player.DecelRunSpeed -=
                            4.0f / 3.0f * (globals.player.DecelRun * dt);
                        if (globals.player.DecelRunSpeed < 0.0f)
                        {
                            globals.player.DecelRunSpeed = 0.0f;
                        }
                    }
                    else
                    {
                        if (!mag)
                        {
                            break;
                        }
                        mag = scalemag;
                        globals.player.DecelRun = globals.player.DecelRunSpeed = scalemag;
                    }
                    goto finish;
                case 0x8 | 0x2:
                    if (dir_dp <= 0.0f && !globals.player.cheat_mode)
                    {
                        break;
                    }
                    mag = scalemag * dir_dp;
                    goto finish;
                case 0x8 | 0x4:
                {
                    diffAngle *= 0.18f;
                    dir_dp = icos(diffAngle);
                    ent->frame->drot.angle = dt * (diffAngle * turnfactor);

                    xMat3x3 rotY;
                    xMat3x3RotY(&rotY, ent->frame->drot.angle);
                    xMat3x3RMulVec(&ent->frame->vel, &rotY, &ent->frame->vel);

                    globals.player.DecelRun = globals.player.DecelRunSpeed = mag;
                    goto finish;
                }
                case 0x8 | 0x4 | 0x2:
                    if (dir_dp <= 0.0f && !globals.player.cheat_mode)
                    {
                        break;
                    }

                    if (ent->frame->vel.y < 0.0f && ent->model->Mat->pos.y < 3.0f + bbash_start_ht)
                    {
                        F32 dampen = 3.0f + bbash_start_ht;
                        dampen = (dampen - ent->model->Mat->pos.y) / 3.0f;
                        dampen *= 2.0f;
                        if (dampen < 0.0f)
                        {
                            dampen = 0.0f;
                        }
                        if (dampen > 1.0f)
                        {
                            dampen = 1.0f;
                        }
                        mag = dampen * (scalemag * dir_dp);
                    }
                    else
                    {
                        mag = 0.0f;
                    }
                    // fall through
                finish:
                default:
                    switch (animUserFlag & 0x1e)
                    {
                    case 0x2:
                        if (strcmp(ent->model->Anim->Single->State->Name, "Walk_sneak") == 0 ||
                            strcmp(ent->model->Anim->Single->State->Name, "Walk_blackknight") == 0)
                        {
                            CalcAnimSpeed(ent, mag, globals.player.s->AnimSneak);
                        }
                        break;
                    case 0x4:
                        CalcAnimSpeed(ent, mag, globals.player.s->AnimWalk);
                        if (ent->model->Anim->Single->State->Speed != 1.0f)
                        {
                            ent->model->Anim->Single->CurrentSpeed =
                                ent->model->Anim->Single->State->Speed;
                        }
                        break;
                    case 0x4 | 0x2:
                        CalcAnimSpeed(ent, mag, globals.player.s->AnimRun);
                        break;
                    }

                    if (surfSlickRatio && !globals.player.ControlOff)
                    {
                        if (!mag)
                        {
                            break;
                        }

                        F32 accelMag;
                        F32 peakLerp;
                        F32 slickLerp = 4.0f / 3.0f * surfSlipTimer;
                        F32 s = (1.0f - slickLerp) * surfSlickRatio +
                                20.0f * slickLerp * surfSlickRatio;
                        if (moveFlag == 0x4 || moveFlag == 0x2)
                        {
                            accelMag = surfAccelWalk * s;
                            surfMaxSpeed = maxVelmag * surfPeakRatio;
                        }
                        else
                        {
                            accelMag = surfAccelRun * s;
                            surfMaxSpeed = maxVelmag * surfPeakRatio;
                        }

                        ent->frame->vel.x += dt * (accelMag * isin(angle));
                        ent->frame->vel.z += dt * (accelMag * icos(angle));

                        peakLerp = 2.5f * surfSlipTimer;
                        if (peakLerp >= 1.0f)
                        {
                            surfMaxSpeed = mag;
                        }
                        else
                        {
                            surfMaxSpeed = (1.0f - peakLerp) * surfMaxSpeed + peakLerp * mag;
                        }
                    }
                    else
                    {
                        mag *= dt;

                        F32 s = tslide_inair_tmr - globals.player.g.SlideAirHoldTime;
                        if (s >= 0.0f && s < globals.player.g.SlideAirSlowTime)
                        {
                            mag *= s / globals.player.g.SlideAirSlowTime;
                        }

                        if (tslide_dbl_tmr > 0.0f &&
                            tslide_dbl_tmr < globals.player.g.SlideAirDblHoldTime)
                        {
                            mag *= tslide_dbl_tmr / globals.player.g.SlideAirDblHoldTime;
                        }

                        if (globals.player.cheat_mode)
                        {
                            mag *= 3.0f;
                            if (globals.pad0->on &
                                (XPAD_BUTTON_R1 | XPAD_BUTTON_R2 | XPAD_BUTTON_L1 | XPAD_BUTTON_L2))
                            {
                                mag *= 4.0f;
                            }
                        }

                        ent->frame->dpos.x = mag * isin(angle);
                        ent->frame->dpos.z = mag * icos(angle);
                        ent->frame->mode |= 0x2;

                        xVec3 dposBeforeAnim = ent->frame->dpos;

                        if (!sPlayerKillAnimMovement)
                        {
                            if (ent->model->Anim->Single->State->Data->FileFlags & 0x100000)
                            {
                                ent->frame->vel.y = 0.0f;
                            }

                            if (ent->model->Anim->Single->SingleFlags & 0x4)
                            {
                                // not in the DWARF (probably an inlined helper's local): retail
                                // loads YawDisp once, before the test.
                                F32 yaw = ent->model->Anim->Single->YawDisp;
                                if (0.0f != yaw)
                                {
                                    xMat3x3 rot;
                                    xMat3x3RotY(&rot, yaw);
                                    xMat3x3Mul(&ent->frame->mat, &rot, &ent->frame->mat);
                                }

                                xVec3 disp;
                                xMat3x3RMulVec(&disp, &ent->frame->mat,
                                               &ent->model->Anim->Single->PhysDisp);
                                xVec3AddTo(&ent->frame->dpos, &disp);

                                xVec3Init(&ent->model->Anim->Single->PhysDisp, 0.0f, 0.0f, 0.0f);
                                ent->model->Anim->Single->YawDisp = 0.0f;
                                ent->model->Anim->Single->SingleFlags &= ~0x4;
                            }
                        }

                        if (sPlayerUpdateIndex == 0)
                        {
                            F32 invUpdateCount = 1.0f / sPlayerUpdateCount;
                            sAnimPhysicsIncrement.x =
                                invUpdateCount * (ent->frame->dpos.x - dposBeforeAnim.x);
                            sAnimPhysicsIncrement.y =
                                invUpdateCount * (ent->frame->dpos.y - dposBeforeAnim.y);
                            sAnimPhysicsIncrement.z =
                                invUpdateCount * (ent->frame->dpos.z - dposBeforeAnim.z);
                            ent->frame->dpos = dposBeforeAnim;
                        }

                        ent->frame->dpos.x += sAnimPhysicsIncrement.x;
                        ent->frame->dpos.y += sAnimPhysicsIncrement.y;
                        ent->frame->dpos.z += sAnimPhysicsIncrement.z;

                        if (globals.player.ControlOff && globals.player.AutoMoveSpeed != 0 &&
                            globals.player.AutoMoveSpeed != 4 &&
                            ent->frame->dpos.x * ent->frame->dpos.x +
                                    ent->frame->dpos.z * ent->frame->dpos.z >=
                                autodist2d * autodist2d)
                        {
                            ent->frame->dpos.x =
                                globals.player.AutoMoveTarget.x - ent->model->Mat->pos.x;
                            ent->frame->dpos.z =
                                globals.player.AutoMoveTarget.z - ent->model->Mat->pos.z;

                            PlayerArrive(ent, globals.player.AutoMoveObject);
                        }
                    }
                    break;
                }
            }
        }
    }

lean:
    LeanUpdate(diffAngle, dt);

    if (animUserFlag & 0x40)
    {
        ent->model->Anim->Single->BilinearLerp[0] = globals.player.LeanLerp;
    }

    if (blendUserFlag & 0x40)
    {
        ent->model->Anim->Single->Blend->BilinearLerp[0] = globals.player.LeanLerp;
    }
}

// No GC symbol: inlined into zEntPlayerReset (PS2 calls it). Declared inline: GC/2.6 would not
// auto-inline this body.
static inline void InvReset()
{
    U32 i;
    S32 j;
    S32 world;
    S32 task;

    globals.player.sb.MaxHealth = 3;
    globals.player.patrick.MaxHealth = 3;
    globals.player.spongepat.MaxHealth = 3;
    globals.player.sb.Health = 3;
    globals.player.patrick.Health = 3;
    globals.player.spongepat.Health = 3;
    globals.player.CurrentSpentSkillPoints = -(S16)globals.player.InitialUnusedSkillPoints;
    globals.player.rewardsMgr->IncManlinessPoints(globals.player.g.InitialShinyCount, true);
    globals.player.InvGoofyGoober = globals.player.g.InitialSpatulaCount;
    globals.player.InvTreasureChestTotal = 0;
    globals.player.InvTreasureChestOpened = 0;

    if (globals.player.InvManlinessPoints > 99999)
    {
        globals.player.InvManlinessPoints = 99999;
    }

    for (i = 0; i < 20; i++)
    {
        globals.player.InvTreasureChest[i] = 0;
        globals.player.InvLevelPickups[i] = 0;
        globals.player.InvTreasureChestMax[i] = 0;
    }

    for (j = 0; TreasureChestsPerLevel[j].level != 0; j++)
    {
        zDispatcher_FindWorldTask(TreasureChestsPerLevel[j].level, world, task);
        if (world > 0)
        {
            globals.player.InvTreasureChestMax[world - 1] = TreasureChestsPerLevel[j].total;
        }
    }

    memcpy(globals.player.g.PowerUp, globals.player.g.InitialPowerUp,
           sizeof(globals.player.g.PowerUp));
    globals.player.sb.MaxHealth += globals.player.g.PowerUp[8];
    globals.player.patrick.MaxHealth += globals.player.g.PowerUp[9];
}

static void HealthReset()
{
    globals.player.s->Health = globals.player.s->MaxHealth;
}

void zEntPlayerHealthResetAll()
{
    globals.player.sb.Health = globals.player.sb.MaxHealth;
    globals.player.patrick.Health = globals.player.patrick.MaxHealth;
    globals.player.spongepat.Health = 3;
}

static U32 RunAnyCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return (globals.player.JumpState == 0 || globals.player.JumpState == 1) &&
           globals.player.Speed == 2 && !surfSlickRatio && oob_state::oob_timer() < 0.0f;
}

static U32 RunStoicCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return (globals.player.JumpState == 0 || globals.player.JumpState == 1) &&
           globals.player.Speed == 2 && !surfSlickRatio && oob_state::oob_timer() < 0.0f;
}

static U32 RunSlipCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return (globals.player.JumpState == 0 || globals.player.JumpState == 1) &&
           globals.player.Speed >= 1 && surfSlickRatio;
}

static U32 RunOutOfWorldCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return (globals.player.JumpState == 0 || globals.player.JumpState == 1) &&
           globals.player.Speed >= 1 && !surfSlickRatio && oob_state::oob_timer() >= 0.0f;
}

static U32 WalkCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return (globals.player.JumpState == 0 || globals.player.JumpState == 1) &&
           globals.player.Speed == 1 && !surfSlickRatio && oob_state::oob_timer() < 0.0f;
}

static U32 WalkStoicCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return (globals.player.JumpState == 0 || globals.player.JumpState == 1) &&
           globals.player.Speed == 1 && !surfSlickRatio && oob_state::oob_timer() < 0.0f;
}

static U32 IdleCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return (globals.player.JumpState == 0 || globals.player.JumpState == 1) &&
           globals.player.Speed == 0 && !surfSlickRatio;
}

static U32 IdleStoicCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return (globals.player.JumpState == 0 || globals.player.JumpState == 1) &&
           globals.player.Speed == 0 && !surfSlickRatio;
}

static U32 IdleSlipCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return (globals.player.JumpState == 0 || globals.player.JumpState == 1) &&
           globals.player.Speed == 0 && surfSlickRatio;
}

static U32 AnyMoveCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.Speed != 0;
}

static U32 AnyStopCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.Speed == 0;
}

static U32 SlipRunCB(xAnimTransition*, xAnimSingle*, void*)
{
    zEntPlayer_SNDPlay(ePlayerSnd_SlipLoop, true);
    return 0;
}

static U32 NoSlipCB(xAnimTransition*, xAnimSingle*, void*)
{
    zEntPlayer_SNDStop(ePlayerSnd_SlipLoop);
    return 0;
}

static U32 IdleCB(xAnimTransition*, xAnimSingle*, void*)
{
    zEntPlayer_SNDStop(ePlayerSnd_SlipLoop);
    idle_tmr = 0.0f;
    return 0;
}

static U32 InactiveCheck(xAnimTransition* tran, xAnimSingle*, void*)
{
    return idle_tmr >= 5.0f &&
           (tran->UserFlags & 0xffff) == player_idle_anim % (tran->UserFlags >> 16);
}

static U32 InactiveCB(xAnimTransition*, xAnimSingle*, void*)
{
    idle_tmr = 0.0f;
    player_idle_anim = player_idle_anim + 1;
    player_idle_anim = player_idle_anim % 16;
    return 0;
}

static U32 SlideCruisingCheck(xAnimTransition*, xAnimSingle* anim, void*)
{
    if (anim->Blend->State)
    {
        return false;
    }

    if (!globals.player.ControlOff && !globals.pad0->on &&
        abs(globals.pad0->analog1.y) < globals.player.g.AnalogMax)
    {
        return true;
    }

    return false;
}

static U32 SlideCruisingCB(xAnimTransition*, xAnimSingle*, void*)
{
    return 0;
}

static U32 SlideAccelerateCheck(xAnimTransition*, xAnimSingle* anim, void*)
{
    if (anim->Blend->State)
    {
        return false;
    }

    if (!globals.player.ControlOff && !globals.pad0->on &&
        globals.pad0->analog1.y < -globals.player.g.AnalogMax)
    {
        return true;
    }

    return false;
}

static U32 SlideAccelerateCB(xAnimTransition*, xAnimSingle*, void*)
{
    return 0;
}

static U32 SlideDecelerateCheck(xAnimTransition*, xAnimSingle* anim, void*)
{
    if (anim->Blend->State)
    {
        return false;
    }

    if (!globals.player.ControlOff && !globals.pad0->on &&
        globals.pad0->analog1.y > globals.player.g.AnalogMax)
    {
        return true;
    }

    return false;
}

static U32 SlideDecelerateCB(xAnimTransition*, xAnimSingle*, void*)
{
    return 0;
}

static U32 LandCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.JumpState == 0;
}

static U32 LandTrackCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.JumpState == 0 && globals.player.SlideTrackLand;
}

static U32 LandNoTrackCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.JumpState == 0 && !globals.player.SlideTrackLand;
}

static U32 LandHighCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.JumpState == 0 && globals.player.WasDJumping;
}

static U32 LandRunCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.JumpState == 0 && globals.player.Speed >= 2;
}

static U32 LandNoTrackWalkCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.JumpState == 0 && globals.player.Speed == 1 &&
           !globals.player.SlideTrackLand;
}

static U32 LandSlipIdleCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.JumpState == 0 && globals.player.Speed == 0 && surfSlickRatio;
}

static U32 LandSlipRunCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.JumpState == 0 && globals.player.Speed >= 1 && surfSlickRatio;
}

static U32 LandNoTrackFastCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.JumpState == 0 && globals.player.Speed >= 2 &&
           !globals.player.SlideTrackLand;
}

static U32 LandNoTrackSlipRunCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.JumpState == 0 && globals.player.Speed >= 1 &&
           !globals.player.SlideTrackLand && surfSlickRatio;
}

static U32 LandNoTrackSlipIdleCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.JumpState == 0 && globals.player.Speed == 0 &&
           !globals.player.SlideTrackLand && surfSlickRatio;
}

static U32 LandCallback(xAnimTransition*, xAnimSingle*, void*)
{
    globals.player.WallJumpState = k_WALLJUMP_NOT;
    zCameraEnableTracking(CO_WALLJUMP);
    zCameraDisableWallJump(&globals.oldSkoolCamera);
    globals.player.IsBubbleBashing = 0;
    return 0;
}

static U32 LandSlipRunCallback(xAnimTransition*, xAnimSingle*, void*)
{
    zEntPlayer_SNDPlay(ePlayerSnd_SlipLoop, true);
    globals.player.WallJumpState = k_WALLJUMP_NOT;

    zCameraEnableTracking(CO_WALLJUMP);
    zCameraDisableWallJump(&globals.oldSkoolCamera);

    return 0;
}

// Placeholder for dead-stripped code (name unknown): retail interns this -20.0f among the
// animation callbacks' literals, although no surviving function here uses it.
static void __deadstripped_zEntPlayer4(F32* out)
{
    *out = -20.0f;
}

static U32 BubbleSpinCheck1(xAnimTransition*, xAnimSingle*, void*)
{
    if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_KARATE) > ePowerupLevelNormal)
    {
        return false;
    }

    if (globals.player.cheat_mode)
    {
        return false;
    }

    if (zEntTeleportBox_playerIn())
    {
        return false;
    }

    return !globals.player.ControlOff && (globals.pad0->pressed & XPAD_MOVE(XPAD_BUTTON_TRIANGLE));
}

static U32 BubbleSpinCheck2(xAnimTransition*, xAnimSingle*, void*)
{
    if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_KARATE) == ePowerupLevelNormal)
    {
        return false;
    }

    if (globals.player.cheat_mode)
    {
        return false;
    }

    if (zEntTeleportBox_playerIn())
    {
        return false;
    }

    return !globals.player.ControlOff && (globals.pad0->pressed & XPAD_MOVE(XPAD_BUTTON_TRIANGLE));
}

static U32 BubbleSpinCB(xAnimTransition*, xAnimSingle* anim, void*)
{
    anim->CurrentSpeed = 0.0f;
    zEntPlayer_SNDPlay(ePlayerSnd_BubbleWand, true);
    zEntPlayer_SNDPlayStream(ePlayerStreamSnd_SBKarateSpin);
    sPlayerAttackInAir++;

    zEntPlayer_SNDStop(ePlayerSnd_SlipLoop);
    return 0;
}

static U32 BubbleBashCheckPunch(xAnimTransition* tran, xAnimSingle* anim, void* object)
{
    if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_BASH) > ePowerupLevelNormal)
    {
        return false;
    }

    return BubbleBashCheck(tran, anim, object);
}

static U32 BubbleBashCheckGauntlet(xAnimTransition* tran, xAnimSingle* anim, void* object)
{
    if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_BASH) == ePowerupLevelNormal)
    {
        return false;
    }

    if (sGauntletHaz)
    {
        return false;
    }

    return BubbleBashCheck(tran, anim, object);
}

static U32 BubbleBashCB(xAnimTransition*, xAnimSingle*, void*)
{
    zEntPlayer_SNDPlay(ePlayerSnd_BubbleBashStart, true);
    zEntPlayer_SNDPlayStream(ePlayerStreamSnd_SBHelmetBash);
    zEntPlayer_SNDStop(ePlayerSnd_SlipLoop);

    xglobals->___player_ent_dont_use_directly->frame->vel.y = 0.0f;
    bbash_start_ht =
        xglobals->___player_ent_dont_use_directly->frame->mat.pos.y + sPlayerCollAdjust;
    bbash_tmr = -globals.player.g.BBashDelay;
    bbash_end_tmr = 0.25f;
    globals.player.JumpState = 2;
    bbash_hit = 0;

    zCameraMinTargetHeightSet(bbash_start_ht);

    gBashButton = 1;

    return 0;
}

static U32 BBashStrikeCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return bbash_hit;
}

static U32 BBashStrikeCB(xAnimTransition*, xAnimSingle*, void*)
{
    xglobals->___player_ent_dont_use_directly->frame->vel.y = 0.0f;

    zEntPlayer_SNDStop(ePlayerSnd_BubbleBashStart);
    zEntPlayer_SNDStop(ePlayerSnd_BubbleBashMiss);
    if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_BASH) <= ePowerupLevelNormal)
    {
        zEntPlayer_SNDPlay(ePlayerSnd_BubbleBashHit1, true);
    }

    return 0;
}

static U32 BbashStart01EffectMissCB(U32, xAnimActiveEffect*, xAnimSingle*, void*)
{
    zEntPlayer_SNDPlay(ePlayerSnd_BubbleBashMiss, true);
    return 0;
}

static U32 BubbleBounceCheck(xAnimTransition*, xAnimSingle*, void*)
{
    if (globals.player.cheat_mode)
    {
        return false;
    }

    return false;
}

static U32 BubbleBounceCB(xAnimTransition*, xAnimSingle*, void*)
{
    zCameraSetBbounce(true);

    xglobals->___player_ent_dont_use_directly->frame->vel.x = 0.0f;
    xglobals->___player_ent_dont_use_directly->frame->vel.y = 0.0f;
    xglobals->___player_ent_dont_use_directly->frame->vel.z = 0.0f;

    tslide_inair_tmr = 0.0f;
    tslide_dbl_tmr = 0.0f;
    tslide_ground = 0;
    globals.player.SlideTrackDecay = 0.0f;
    bbounce_hit = 0;

    return 0;
}

static U32 BBounceAttackCB(xAnimTransition*, xAnimSingle*, void*)
{
    xglobals->___player_ent_dont_use_directly->frame->vel.y = -globals.player.g.BBounceSpeed;
    return 0;
}

static U32 BBounceStrikeCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.JumpState == 0 || globals.player.JumpState == 1;
}

static U32 BBounceStrikeCB(xAnimTransition*, xAnimSingle*, void*)
{
    zEntPlayer_SNDStop(ePlayerSnd_BubbleBashStart);
    zEntPlayer_SNDPlay(ePlayerSnd_BounceStrike, true);
    zFX_SpawnBubbleSlam((xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos, 100,
                        0.15f, 12.0f, 2.0f);

    zCameraSetBbounce(false);

    return 0;
}

static U32 BBounceToJumpCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return bbounce_hit;
}

static U32 BBounceToJumpCB(xAnimTransition*, xAnimSingle*, void*)
{
    zEntPlayerJumpStart(xglobals->___player_ent_dont_use_directly, &globals.player.s->Jump);

    startDouble = xglobals->___player_ent_dont_use_directly->frame->mat.pos.y;
    startJump = startDouble;
    globals.player.CanJump = 0;
    globals.player.IsJumping = 1;
    globals.player.Jump_CanDouble = 1;
    globals.player.IsDJumping = 0;

    zCameraSetBbounce(false);
    return 0;
}

static U32 BbowlCheck(xAnimTransition*, xAnimSingle*, void*)
{
    bool doBowl;

    if (globals.player.cheat_mode)
    {
        return false;
    }

    if (zEntTeleportBox_playerIn())
    {
        return false;
    }

    if (sStartBubbleBowlTimer)
    {
        return false;
    }

    if (gBowlButton)
    {
        return false;
    }

    if (cruise_bubble::active())
    {
        return false;
    }

    if (!globals.player.g.PowerUp[PLYR_ATAK_SB_BOWL - 1])
    {
        return false;
    }

    if (globals.player.g.PowerUp[PLYR_ATAK_SB_BOWL - 1] == 1)
    {
        if (!xEntIsVisible(zEntPlayerGetBBowl()) || sStartBubbleBowlLeftGround)
        {
            doBowl = !globals.player.ControlOff && (globals.pad0->on & XPAD_MOVE(XPAD_BUTTON_O));
        }
        else
        {
            doBowl =
                !globals.player.ControlOff && (globals.pad0->pressed & XPAD_MOVE(XPAD_BUTTON_O));
        }

        return doBowl;
    }
    else if (globals.player.g.PowerUp[PLYR_ATAK_SB_BOWL - 1] > 1)
    {
        if (!xEntIsVisible(zEntPlayerGetBBowl()) && !gBowlButton)
        {
            return !globals.player.ControlOff && (globals.pad0->on & XPAD_MOVE(XPAD_BUTTON_O));
        }
    }

    return false;
}

static U32 BbowlCB(xAnimTransition*, xAnimSingle*, void*)
{
    gBowlButton = 0;

    zEntPlayer_SNDPlay(ePlayerSnd_BowlWindup, true);
    zEntPlayer_SNDStop(ePlayerSnd_SlipLoop);

    if (globals.player.g.PowerUp[PLYR_ATAK_SB_BOWL - 1] == 1)
    {
        zEntPlayer_SNDPlayStream(ePlayerStreamSnd_SBBowl);
    }
    else
    {
        zEntPlayer_SNDPlayStream(ePlayerStreamSnd_SBMachoBowl);
    }

    xEntFrame* frame = xglobals->___player_ent_dont_use_directly->frame;
    F32 x = (frame->mat.pos.x - frame->oldmat.pos.x) / last_update_dt;
    F32 z = (frame->mat.pos.z - frame->oldmat.pos.z) / last_update_dt;
    // not in the PS2 DWARF; BfBB's DWARF lists speed2 here
    F32 speed2 = 0.2f * (x * x + z * z);

    if (speed2 < globals.player.g.BubbleBowlMinSpeed * globals.player.g.BubbleBowlMinSpeed)
    {
        globals.player.bbowlInitVel = globals.player.g.BubbleBowlMinSpeed;
    }
    else
    {
        globals.player.bbowlInitVel = xsqrt(speed2);
    }

    frame->vel.x =
        globals.player.bbowlInitVel * xglobals->___player_ent_dont_use_directly->model->Mat->at.x;
    frame->vel.z =
        globals.player.bbowlInitVel * xglobals->___player_ent_dont_use_directly->model->Mat->at.z;

    sShouldBubbleBowl = 0;
    globals.player.IsBubbleBowling = 1;
    sBubbleBowlTimer = 0.0f;
    sBubbleBowlLastWindupTime = -1.0f;

    return 0;
}

static U32 BbowlWindupEndCheck(xAnimTransition*, xAnimSingle* anim, void*)
{
    if (anim->Time < sBubbleBowlLastWindupTime && sShouldBubbleBowl)
    {
        return true;
    }
    sBubbleBowlLastWindupTime = anim->Time;
    return false;
}

static U32 BbowlTossEndCB(xAnimTransition*, xAnimSingle*, void*)
{
    xEntBoulder_BubbleBowl(sBubbleBowlMultiplier);

    zEntPlayerFX::SuperMachoBowlLightning::Start(zEntPlayerGetBBowl()->frame->mat.pos);

    globals.player.IsBubbleBowling = 0;
    sStartBubbleBowlTimer = 0.6f;
    sStartBubbleBowlLeftGround = 0;

    zEntPlayer_SNDStop(ePlayerSnd_BowlWindup);
    zEntPlayer_SNDPlay(ePlayerSnd_BowlRelease, true);

    return 0;
}

static U32 BbowlRecoverWalkCheck(xAnimTransition* tran, xAnimSingle* anim, void* param_3)
{
    return anim->Time > globals.player.g.BubbleBowlMinRecoverTime && WalkCheck(tran, anim, param_3);
}

static U32 BbowlRecoverRunCheck(xAnimTransition* tran, xAnimSingle* anim, void* param_3)
{
    return anim->Time > globals.player.g.BubbleBowlMinRecoverTime && RunCheck(tran, anim, param_3);
}

static U32 BbowlRecoverRunOutOfWorldCheck(xAnimTransition* tran, xAnimSingle* anim, void* param_3)
{
    return anim->Time > globals.player.g.BubbleBowlMinRecoverTime &&
           RunOutOfWorldCheck(tran, anim, param_3);
}

static U32 BbowlRecoverRunSlipCheck(xAnimTransition* tran, xAnimSingle* anim, void* param_3)
{
    return anim->Time > globals.player.g.BubbleBowlMinRecoverTime &&
           RunSlipCheck(tran, anim, param_3);
}

static U32 GooCheck(xAnimTransition*, xAnimSingle*, void*)
{
    if (globals.player.ControlOff & 0x8000)
    {
        return false;
    }

    if (globals.player.cheat_mode)
    {
        return false;
    }

    if (sOnBouncBack && gCurrentPlayer == eCurrentPlayerPatrick)
    {
        return false;
    }

    return in_goo;
}

static U32 GooDeathCB(xAnimTransition*, xAnimSingle*, void*)
{
    globals.player.s->Health = 0;
    globals.player.DamageTimer = 10.0f;
    zGooStopTide();
    sPlayerDiedLastTime = 1;
    zEntPlayerControlOff(CONTROL_OWNER_GLOBAL);
    zEntPlayer_SNDStop(ePlayerSnd_CartwheelLoop);
    zEntPlayer_SNDStop(ePlayerSnd_Cartwheel2Loop);
    return false;
}

static U32 HitCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return player_hit;
}

static U32 HitCB(xAnimTransition*, xAnimSingle*, void*)
{
    player_hit = 0;
    return 0;
}

static U32 Hit01Check(xAnimTransition*, xAnimSingle*, void*)
{
    return player_hit && player_hit_anim == 1;
}

static U32 Hit01CB(xAnimTransition*, xAnimSingle*, void*)
{
    player_hit = 0;
    player_hit_anim = 2;
    return 0;
}

static U32 Hit02Check(xAnimTransition*, xAnimSingle*, void*)
{
    return player_hit && player_hit_anim == 2;
}

static U32 Hit02CB(xAnimTransition*, xAnimSingle*, void*)
{
    player_hit = 0;
    player_hit_anim = 3;
    return 0;
}

static U32 Hit03Check(xAnimTransition*, xAnimSingle*, void*)
{
    return player_hit && player_hit_anim == 3;
}

static U32 Hit03CB(xAnimTransition*, xAnimSingle*, void*)
{
    player_hit = 0;
    player_hit_anim = 4;
    return 0;
}

static U32 Hit04Check(xAnimTransition*, xAnimSingle*, void*)
{
    return player_hit && player_hit_anim == 4;
}

static U32 Hit04CB(xAnimTransition*, xAnimSingle*, void*)
{
    player_hit = 0;
    player_hit_anim = 5;
    return 0;
}

static U32 Hit05Check(xAnimTransition*, xAnimSingle*, void*)
{
    return player_hit && player_hit_anim == 5;
}

static U32 Hit05CB(xAnimTransition*, xAnimSingle*, void*)
{
    player_hit = 0;
    player_hit_anim = 1;
    return 0;
}

static U32 DefeatedCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.s->Health == 0;
}

static U32 Defeated01Check(xAnimTransition* tran, xAnimSingle*, void*)
{
    return (globals.player.s->Health == 0 && player_dead_anim % tran->UserFlags == 0) ||
           sOnBouncBack;
}

static U32 Defeated02Check(xAnimTransition* tran, xAnimSingle*, void*)
{
    return globals.player.s->Health == 0 && player_dead_anim % tran->UserFlags + 1 == 2;
}

static U32 Defeated03Check(xAnimTransition* tran, xAnimSingle*, void*)
{
    return globals.player.s->Health == 0 && player_dead_anim % tran->UserFlags + 1 == 3;
}

static U32 DefeatedCB(xAnimTransition*, xAnimSingle*, void*)
{
    if (sOnBouncBack)
    {
        globals.player.s->Health = 0;
        globals.player.DamageTimer = 10.0f;
        sPlayerDiedLastTime = 1;
        zEntPlayerControlOff(CONTROL_OWNER_GLOBAL);
    }

    player_dead_anim++;

    if (gCurrentPlayer == eCurrentPlayerSpongeBob)
    {
        zGameExtras_CheatFlags();

        if (xurand() < 0.175f || sOnBouncBack || gBungeeDeath)
        {
            shrapDeathTimer = 3.5f;

            zShrapnelAsset* deathShrap =
                (zShrapnelAsset*)xSTFindAsset(xStrHash("spongebob_shrapnel"), NULL);
            if (deathShrap && deathShrap->initCB)
            {
                xEntHide(xglobals->___player_ent_dont_use_directly);
                deathShrap->initCB(deathShrap, xglobals->___player_ent_dont_use_directly->model,
                                   NULL, NULL);
                xglobals->___player_ent_dont_use_directly->frame->vel.x = 0.0f;
                xglobals->___player_ent_dont_use_directly->frame->vel.y = 0.0f;
                xglobals->___player_ent_dont_use_directly->frame->vel.z = 0.0f;
                globals.player.KnockBackTimer = 0.0f;
                globals.player.KnockIntoAirTimer = 0.0f;
            }
        }
    }

    return 0;
}

static U32 SpatulaGrabCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return sSpatulaGrabbed;
}

static U32 SpatulaGrabCB(xAnimTransition*, xAnimSingle*, void* object)
{
    sSpatulaGrabbed = 0;
    tslide_inair_tmr = 0.0f;
    tslide_dbl_tmr = 0.0f;
    globals.player.SlideTrackDecay = 0.0f;
    tslide_ground = 0;

    xEnt* ent = (xEnt*)object;
    ent->frame->vel.x = 0.0f;
    if (ent->frame->vel.y > 0.0f)
    {
        ent->frame->vel.y = 0.0f;
    }
    ent->frame->vel.z = 0.0f;

    globals.player.KnockBackTimer = 0.0f;
    globals.player.KnockIntoAirTimer = 0.0f;

    zCameraSetReward(true);
    zCameraDisableInput();
    return 0;
}

static U32 SpatulaGrabStopCB(xAnimTransition*, xAnimSingle*, void*)
{
    idle_tmr = 0.0f;

    if (globals.autoSaveFeature)
    {
        zSaveLoad_TriggerAutoSave();
    }

    zCameraEnableTracking(CO_REWARDANIM);
    xCameraSetFOV(&xglobals->oldSkoolCamera, 75.0f);
    zCameraSetReward(0);

    if (!(globals.player.ControlOff & CONTROL_OWNER_TALK_BOX))
    {
        zMusicSetVolume(sOldMusicVolume, 0.75f);
    }

    zCameraEnableInput();
    zEntPlayerControlOn(CONTROL_OWNER_REWARDANIM);
    zEntEventAllOfType(eEventTokenPickupComplete, eBaseTypeDispatcher);
    return 0;
}

static void DoWallJumpCheck()
{
    xEnt* ent = xglobals->___player_ent_dont_use_directly;

    sWallJumpResult = WallJumpResult_NoJump;

    static F32 sAtdist = 0.65f;
    static F32 sSweptrad = 0.4f;
    static F32 sVerticalCos = 0.2588f;

    xVec3 start;
    start.x = ent->model->Mat->pos.x;
    start.y = ent->model->Mat->pos.y + ent->bound.cyl.r;
    start.z = ent->model->Mat->pos.z;

    xVec3 end = start;
    end.x += ent->model->Mat->at.x * sAtdist;
    end.z += ent->model->Mat->at.z * sAtdist;

    xSweptSphere sws;
    xSweptSpherePrepare(&sws, &start, &end, sSweptrad);

    if (xSweptSphereToScene(&sws, globals.sceneCur, ent, 0x16, 0))
    {
        xSweptSphereGetResults(&sws);

        xSurface* surf;
        if (sws.optr && sws.mptr)
        {
            surf = sws.mptr->Surf;
        }
        else
        {
            surf = zSurfaceGetSurface(sws.oid);
        }

        if (!surf)
        {
            return;
        }

        zSurfaceProps* surfaceProperties = (zSurfaceProps*)surf->moprops;

        if (!(surfaceProperties->asset->phys_flags & 0x20))
        {
            return;
        }

        if (xabs(sws.worldNormal.y) < sVerticalCos)
        {
            if (xVec3Dot(&sws.worldNormal, &sws.worldNormal) > 0.999f)
            {
                sWallNormal = sws.worldNormal;
                sWallContactPos = sws.worldContact;
                sWallCollisionSurface = surfaceProperties;
                sWallJumpResult = WallJumpResult_Jump;
            }
        }
    }
}

static U32 WallJumpLaunchCheck(xAnimTransition*, xAnimSingle*, void*)
{
    if (globals.player.ControlOff || !(globals.pad0->pressed & XPAD_BUTTON_X) ||
        !globals.player.IsJumping || globals.player.s->Wall.PeakHeight <= 0.0f)
    {
        return false;
    }
    return sWallJumpResult == WallJumpResult_Jump;
}

static U32 WallJumpLaunchCallback(xAnimTransition*, xAnimSingle*, void*)
{
    globals.player.WallJumpState = k_WALLJUMP_LAUNCH;
    zCameraEnableWallJump(&globals.oldSkoolCamera, sWallNormal);
    zCameraDisableTracking(CO_WALLJUMP);
    sWallJumpResult = WallJumpResult_NoJump;
    zEntPlayer_SNDPlayStream(ePlayerStreamSnd_WallJump);
    return 0;
}

static U32 WallJumpCallback(xAnimTransition*, xAnimSingle*, void*)
{
    zJumpParam wallParam = globals.player.s->Wall;
    wallParam.PeakHeight *= sWallCollisionSurface->asset->walljump_scale_y;

    CalcJumpImpulse(&wallParam, NULL);
    zEntPlayerJumpStart(xglobals->___player_ent_dont_use_directly, &wallParam);
    zEntPlayer_SNDPlay(ePlayerSnd_Jump, true);

    globals.player.IsDJumping = 0;
    globals.player.WallJumpState = k_WALLJUMP_FLIGHT;
    zCameraEnableTracking(CO_WALLJUMP);

    xEntFrame* frame = xglobals->___player_ent_dont_use_directly->frame;
    xVec3* velocity = &frame->vel;
    velocity->x = 0.0f;
    velocity->z = 0.0f;
    xVec3 u;
    xVec3SMul(&u, &sWallNormal,
              globals.player.s->WallJumpVelocity * sWallCollisionSurface->asset->walljump_scale_xz);
    xVec3Add(velocity, velocity, &u);

    xVec3Copy(&frame->dvel, &g_O3);
    xVec3Copy(&frame->oldvel, velocity);
    xMat3x3LookAt((xMat3x3*)xglobals->___player_ent_dont_use_directly->model->Mat, &sWallNormal,
                  &g_O3);
    xVec3Copy(&frame->dpos, &g_O3);

    frame->drot.angle = 0.0f;
    xVec3Copy(&frame->oldmat.pos, &frame->mat.pos);

    return 0;
}

static U32 WallJumpFlightLandCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return sWallJumpResult == WallJumpResult_Jump;
}

static U32 WallJumpFlightLandCallback(xAnimTransition*, xAnimSingle*, void*)
{
    globals.player.WallJumpState = k_WALLJUMP_LAND;
    zEntPlayer_SNDPlay(ePlayerSnd_WallJumpLand, true);
    return 0;
}

static U32 WallJumpLandFlightCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return sWallJumpResult != WallJumpResult_Jump;
}

static U32 WallJumpLandFlightCallback(xAnimTransition*, xAnimSingle*, void*)
{
    globals.player.WallJumpState = k_WALLJUMP_FLIGHT;
    return 0;
}

static U32 WallJumpFallCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return wallStickTimer == 0.0f;
}

static U32 WallJumpStickCB(xAnimTransition*, xAnimSingle*, void*)
{
    wallStickTimer = gWallStickTime;
    globals.player.WallJumpState = k_WALLJUMP_STUCK;
    return 0;
}

static U32 WallJumpFallCB(xAnimTransition*, xAnimSingle*, void*)
{
    globals.player.WallJumpState = k_WALLJUMP_FALL;
    return 0;
}

static U32 JumpCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.CanJump && !globals.player.ControlOff &&
           (globals.pad0->pressed & XPAD_BUTTON_X);
}

static U32 JumpCB(xAnimTransition*, xAnimSingle*, void*)
{
    if (globals.player.cheat_mode)
    {
        return 0;
    }

    zEntPlayerJumpStart(xglobals->___player_ent_dont_use_directly, &globals.player.s->Jump);
    zEntPlayerJumpAddDriver(xglobals->___player_ent_dont_use_directly);

    zEntPlayer_SNDStop(ePlayerSnd_CartwheelLoop);
    zEntPlayer_SNDStop(ePlayerSnd_Cartwheel2Loop);
    zEntPlayer_SNDStop(ePlayerSnd_SlipLoop);
    zEntPlayer_SNDPlay(ePlayerSnd_Jump, true);
    zEntPlayer_SNDPlayStream(ePlayerStreamSnd_Jumping);

    startDouble = xglobals->___player_ent_dont_use_directly->frame->mat.pos.y;
    startJump = startDouble;
    globals.player.CanJump = 0;
    globals.player.IsJumping = 1;

    return 0;
}

static U32 JumpApexCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return xglobals->___player_ent_dont_use_directly->frame->vel.y <= 0.001f;
}

static U32 BounceCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.Bounced == 1;
}

static U32 BounceCB(xAnimTransition*, xAnimSingle*, void*)
{
    zCameraSetBbounce(true);
    globals.player.Bounced = 0;
    globals.player.Jump_CanDouble = 1;
    globals.player.Jump_CanFloat = 1;

    return 0;
}

static U32 DblJumpCheck(xAnimTransition*, xAnimSingle*, void*)
{
    if (globals.player.s->Double.PeakHeight <= 0.0f)
    {
        return 0;
    }

    if (globals.player.ControlOff || !(globals.pad0->pressed & XPAD_BUTTON_X) ||
        !globals.player.Jump_CanDouble)
    {
        return 0;
    }

    zEntPlayer_SNDPlay(ePlayerSnd_DoubleJump, true);

    return sWallJumpResult != WallJumpResult_Jump;
}

static U32 DblJumpCB(xAnimTransition*, xAnimSingle*, void*)
{
    if (globals.player.cheat_mode)
    {
        return 0;
    }

    zEntPlayerJumpStart(xglobals->___player_ent_dont_use_directly, &globals.player.s->Double);
    globals.player.Jump_CanDouble = 0;
    startDouble = xglobals->___player_ent_dont_use_directly->frame->mat.pos.y;
    globals.player.IsDJumping = 1;
    globals.player.Bounced = 0;

    // Compiled-out assert of unknown condition: it keeps this callback from being inlined into
    // TongueDblJumpCB, which retail calls with bl.
    xASSERT_PLACEHOLDER();
    sLasso->flags = 0;

    if (tslide_inair_tmr)
    {
        F32 dirx;
        F32 dirz;
        F32 speed;
        F32 len2 = SQR(update_motion.x) + SQR(update_motion.z);

        if (xabs(len2 - 1.0f) <= 0.00001f)
        {
            dirx = update_motion.x;
            dirz = update_motion.z;
            speed = 1.0f;
        }
        else if (xabs(len2) <= 0.00001f)
        {
            dirx = 0.0f;
            dirz = 0.0f;
            speed = 0.0f;
        }
        else
        {
            speed = xsqrt(len2);
            F32 len_inv = 1.0f / speed;
            dirx = update_motion.x * len_inv;
            dirz = update_motion.z * len_inv;
        }

        speed /= update_dt;
        if (globals.player.g.SlideVelDblBoost *
                (dirx * globals.player.SlideTrackDir.x + dirz * globals.player.SlideTrackDir.z) >
            speed)
        {
            xglobals->___player_ent_dont_use_directly->frame->vel.x += dirx * speed;
            xglobals->___player_ent_dont_use_directly->frame->vel.z += dirz * speed;
            tslide_dbl_tmr = update_dt;
        }
    }

    return 0;
}

static F32 sTongueDblSpeedMult;

// static inline, defined here: retail's .sbss has hash_puitem_puanim right after
// sTongueDblSpeedMult, and the out-of-line code follows zEntPlayer_Update. len uses xQuatDot
// directly: xQuatLength2's extra inline level changes the registers.
static inline void CarriedNME_RemoveSpin(xMat4x3* mat, F32 dt, const zNMECommon* npc,
                                         const xAnimState* astate)
{
    if (npc->itemType < NME_TYPE_STANDARD || npc->itemType > NME_TYPE_MERVYN_V3)
    {
        return;
    }

    static U32 hash_puitem_puanim = xStrHash("Carry_PickupItem");

    if (astate->ID != hash_puitem_puanim)
    {
        return;
    }

    xQuat quat;
    xQuat result;
    F32 len;
    F32 dot;
    F32 angle;
    F32 maxAngle;

    xQuatFromMat(&quat, mat);

    len = xsqrt(xQuatDot(&quat, &quat));
    xQuatSMul(&quat, &quat, 1.0f / len);

    dot = xQuatDot(&quat, &g_IQ);
    if (dot < 0.0f)
    {
        dot = -dot;
        xQuatFlip(&quat, &quat);
    }

    if (dot > 1.0f)
    {
        dot = 1.0f;
    }

    angle = 2.0f * xacos(dot);
    maxAngle = 2 * PI * dt;

    if (angle <= maxAngle)
    {
        xMat3x3Identity(mat);
    }
    else
    {
        xQuatSlerp(&result, &quat, &g_IQ, maxAngle / angle);
        xQuatToMat(&result, mat);
    }
}

static S32 sShouldMelee;
U32 patrickHitMax;

static U32 TongueDblJumpCB(xAnimTransition* tran, xAnimSingle* anim, void* object)
{
    DblJumpCB(tran, anim, object);
    sTongueDblSpeedMult =
        (1.1f * (2.0f * anim->State->Data->Duration - anim->Time)) / anim->State->Data->Duration;
    anim->CurrentSpeed *= sTongueDblSpeedMult;
    return 0;
}

static U32 TongueDblSpinCB(xAnimTransition*, xAnimSingle* anim, void*)
{
    anim->CurrentSpeed *= sTongueDblSpeedMult;
    return 0;
}

static U32 FallCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.JumpState != 0 && globals.player.JumpState != 1;
}

static U32 BoulderRollMoveCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return bvTimeToIdle > 0.0f && boulderVehicle;
}

static U32 BoulderRollIdleCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return bvTimeToIdle <= 0.0f && boulderVehicle;
}

U32 BoulderRollCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return boulderRollShouldStart && boulderVehicle;
}

static U32 BoulderRollWindupCB(xAnimTransition*, xAnimSingle*, void*)
{
    zEntPlayer_SNDPlay(ePlayerSnd_BoulderStart, true);
    zEntPlayer_SNDStop(ePlayerSnd_SlipLoop);
    return 0;
}

static U32 BoulderRollCB(xAnimTransition*, xAnimSingle*, void*)
{
    if (!boulderVehicle)
    {
        gSpongeBall = 0;
        boulderRollShouldEnd = 0;
        return 1;
    }

    xEntHide(xglobals->___player_ent_dont_use_directly);

    boulderVehicle->flags &= (U8)~0x40;
    boulderVehicle->asset->flags &= (U8)~0x40;
    boulderVehicle->flags &= (U8)~0x8;

    xEntBoulder_Reset(boulderVehicle, globals.sceneCur);
    xVec3Init(&globals.player.PredictCurrDir, 0.0f, 1.0f, 0.0f);
    globals.player.PredictCurrVel = 0.0f;
    xVec3Copy((xVec3*)&boulderVehicle->model->Mat->pos,
              (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos);

    boulderVehicle->model->Mat->pos.y += boulderVehicle->bound.sph.r;
    xVec3SubFrom((xVec3*)&boulderVehicle->model->Mat->pos,
                 (xVec3*)&boulderVehicle->model->Data->boundingSphere.center);

    xglobals->___player_ent_dont_use_directly->update = zEntPlayer_BoulderVehicleUpdate;
    xglobals->___player_ent_dont_use_directly->move = zEntPlayer_BoulderVehicleMove;
    xglobals->___player_ent_dont_use_directly->render =
        (xEntRenderCallback)zEntPlayer_BoulderVehicleRender;

    boulderVehicle->update = NULL;

    boulderVehicle->vel.y = 0.0f;
    boulderVehicle->vel.x = globals.player.PredictCurrDir.x * globals.player.PredictCurrVel;
    boulderVehicle->vel.z = globals.player.PredictCurrDir.z * globals.player.PredictCurrVel;
    boulderVehicle->rotVec.x = boulderVehicle->vel.z;
    boulderVehicle->rotVec.y = 0.0f;
    boulderVehicle->rotVec.z = -boulderVehicle->vel.x;
    xVec3Normalize(&boulderVehicle->rotVec, &boulderVehicle->rotVec);
    boulderVehicle->angVel = xVec3Length(&boulderVehicle->vel) / boulderVehicle->bound.sph.r;

    xVec3Copy((xVec3*)&boulderVehicle->model->Mat->right,
              (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->right);
    xVec3Copy((xVec3*)&boulderVehicle->model->Mat->at,
              (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->at);
    xVec3Copy((xVec3*)&boulderVehicle->model->Mat->up,
              (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->up);

    xParEmitterCustomSettings info;
    if (gPTankDisable)
    {
        info.custom_flags = 0x35e;
        xVec3Copy(&info.pos, (xVec3*)&boulderVehicle->model->Mat->pos);
        xVec3Copy(&info.vel, &boulderVehicle->vel);

        if (xVec3Normalize(&info.vel, &info.vel) < 0.00001f)
        {
            info.vel.x = 0.0f;
            info.vel.y = 3.0f;
            info.vel.z = 0.0f;
        }
        else
        {
            xVec3SMulBy(&info.vel, 3.0f);
        }

        info.vel_angle_variation = DEG2RAD(270);
        info.rate.set(3000.0f, 3000.0f, 1.0f, 0);
        info.life.set(0.75f, 0.75f, 1.0f, 0);
        info.size_birth.set(0.25f, 0.25f, 1.0f, 0);
        info.size_death.set(0.5f, 0.5f, 1.0f, 0);

        xParEmitterEmitCustom(sEmitSpinBubbles, update_dt, &info);
        xVec3AddScaled(&info.pos, &boulderVehicle->vel, 10.0f * update_dt);
        xParEmitterEmitCustom(sEmitSpinBubbles, update_dt, &info);
    }
    else
    {
        zFX_SpawnBubbleHit((xVec3*)&boulderVehicle->model->Mat->pos, 50);
    }

    boulderRollShouldEnd = 0;
    zEntEvent(xglobals->___player_ent_dont_use_directly, eEventSpongeballOn);
    xEntBeginUpdate(boulderVehicle, globals.sceneCur, 0.00001f);
    xEntEndUpdate(boulderVehicle, globals.sceneCur, 0.00001f);
    xEntBoulder_RealBUpdate(boulderVehicle, &boulderVehicle->frame->mat.pos);
    boulderVehicle->model->LightKit = xglobals->___player_ent_dont_use_directly->model->LightKit;

    return 0;
}

// No GC/PS2 symbol (always inlined). BfBB has it with an extra button/scene check; in TSSM
// zEntPlayer_BoulderVehicleUpdate only tests the flag, unsigned (the U32 return value).
static U32 BoulderRollDoneCheck()
{
    return boulderRollShouldEnd;
}

static U32 BoulderRollDoneCB()
{
    zEntPlayerFX::MotionBlur::Blurring = false;
    zEntPlayerFX::MotionBlur::EntToTrack = NULL;
    xEntShow(xglobals->___player_ent_dont_use_directly);
    zEntPlayer_SNDPlay(ePlayerSnd_BoulderEnd, true);

    xParEmitterCustomSettings info;
    if (gPTankDisable)
    {
        info.custom_flags = 0x35e;
        xVec3Copy(&info.pos, (xVec3*)&boulderVehicle->model->Mat->pos);
        xVec3Copy(&info.vel, &boulderVehicle->vel);

        if (xVec3Normalize(&info.vel, &info.vel) < 0.00001f)
        {
            info.vel.x = 0.0f;
            info.vel.y = 3.0f;
            info.vel.z = 0.0f;
        }
        else
        {
            xVec3SMulBy(&info.vel, 3.0f);
        }

        info.vel_angle_variation = DEG2RAD(270);
        info.rate.set(3000.0f, 3000.0f, 1.0f, 0);
        info.life.set(0.75f, 0.75f, 1.0f, 0);
        info.size_birth.set(0.25f, 0.25f, 1.0f, 0);
        info.size_death.set(0.5f, 0.5f, 1.0f, 0);

        xParEmitterEmitCustom(sEmitSpinBubbles, update_dt, &info);
        xVec3AddScaled(&info.pos, &boulderVehicle->vel, 5.0f * update_dt);
        xParEmitterEmitCustom(sEmitSpinBubbles, update_dt, &info);
    }
    else
    {
        zFX_SpawnBubbleHit((xVec3*)&boulderVehicle->model->Mat->pos, 50);
    }

    xglobals->___player_ent_dont_use_directly->update = zPlayerUpdate;
    xglobals->___player_ent_dont_use_directly->move = zEntPlayer_Move;
    xglobals->___player_ent_dont_use_directly->render = (xEntRenderCallback)zPlayerRender;
    xEntBoulder_Kill(boulderVehicle);
    boulderRollShouldStart = 0;

    zEntEvent(xglobals->___player_ent_dont_use_directly, eEventSpongeballOff);
    idle_tmr = 0.0f;

    return 0;
}

static U32 SlideTrackCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.SlideTrackSliding & 1;
}

static U32 SlideTrackCB(xAnimTransition*, xAnimSingle*, void*)
{
    sLasso->flags = 0;
    globals.player.SlideTrackLean = 0.0f;

    zEntPlayerKillCarry();
    zEntPlayer_SNDStop(ePlayerSnd_SlipLoop);

    return 0;
}

static U32 NoslideTrackCB(xAnimTransition*, xAnimSingle*, void*)
{
    idle_tmr = 0.0f;
    return 0;
}

static U32 NoslideTrackCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return (globals.player.SlideTrackSliding & 1) == 0 && globals.player.JumpState == 0;
}

static U32 TrackFallCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return (globals.player.SlideTrackSliding & 1) == 0 && globals.player.JumpState != 0 &&
           globals.player.JumpState != 1;
}

static U32 TrackFallCB(xAnimTransition*, xAnimSingle*, void*)
{
    globals.player.JumpState = 2;
    globals.player.CanJump = 1;
    return 0;
}

static U32 TrackPrefallJumpCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.CanJump && !globals.player.ControlOff &&
           globals.pad0->pressed & XPAD_BUTTON_X && tslide_inair_tmr != 0.0f &&
           tslide_inair_tmr < 0.5f;
}

static U32 LedgeGrabCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.s->ledge.tmr == -1.0f;
}

static U32 LedgeGrabCB(xAnimTransition*, xAnimSingle*, void* object)
{
    zEntPlayer_SNDStop(ePlayerSnd_Heli);
    zEntPlayer_SNDPlayStream(ePlayerStreamSnd_LedgeHang);
    globals.player.s->ledge.tmr = 0.00001f;
    globals.player.s->ledge.startrot = ((xEnt*)object)->frame->rot.angle;

    if (globals.player.s->ledge.startrot > globals.player.s->ledge.endrot + PI)
    {
        globals.player.s->ledge.startrot -= 2 * PI;
    }
    else if (globals.player.s->ledge.startrot < globals.player.s->ledge.endrot - PI)
    {
        globals.player.s->ledge.startrot += 2 * PI;
    }

    sLasso->flags = 0;
    return 0;
}

static U32 LedgeFinishCB(xAnimTransition*, xAnimSingle*, void*)
{
    idle_tmr = 0.0f;
    globals.player.JumpState = 1;
    globals.player.JumpTimer = 0.0f;
    return 0;
}

static U32 PatrickGrabCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return sGrabFound != NULL;
}

static U32 PatrickGrabFailed(xAnimTransition*, xAnimSingle*, void*)
{
    return sGrabFailed != 0;
}

static U32 PatrickGrabKill(xAnimTransition*, xAnimSingle*, void*)
{
    return globals.player.carry.grabbed == NULL;
}

static U32 PatrickGrabThrowStart(xAnimTransition*, xAnimSingle*, void*)
{
    if (globals.player.cheat_mode)
    {
        return false;
    }

    return !globals.player.ControlOff && (globals.pad0->pressed & XPAD_BUTTON_R1);
}

static U32 PatrickHammerCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return hammer_start;
}

static U32 PatrickHammerThrowCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return hammer_throw;
}

static U32 PatrickAttackCheckMelee1(xAnimTransition*, xAnimSingle*, void*)
{
    if (zEntPlayer_PowerupLevel(PLYR_ATAK_PAT_BELLY) != ePowerupLevelNormal)
    {
        return false;
    }

    if (globals.player.cheat_mode || zEntTeleportBox_playerIn())
    {
        return false;
    }

    return !globals.player.ControlOff && (globals.pad0->pressed & XPAD_MOVE(XPAD_BUTTON_TRIANGLE));
}

static U32 PatrickAttackCheckMelee2(xAnimTransition*, xAnimSingle*, void*)
{
    if (zEntPlayer_PowerupLevel(PLYR_ATAK_PAT_BELLY) <= ePowerupLevelNormal)
    {
        return false;
    }

    if (globals.player.cheat_mode || zEntTeleportBox_playerIn())
    {
        return false;
    }

    return !globals.player.ControlOff && (globals.pad0->pressed & XPAD_MOVE(XPAD_BUTTON_TRIANGLE));
}

static U32 PatrickStunCheckButtslam(xAnimTransition*, xAnimSingle*, void*)
{
    if (zEntPlayer_PowerupLevel(PLYR_ATAK_PAT_FLOP) != ePowerupLevelNormal)
    {
        return false;
    }

    if (globals.player.cheat_mode)
    {
        return false;
    }

    return !globals.player.ControlOff && (globals.pad0->pressed & XPAD_MOVE(XPAD_BUTTON_TRIANGLE));
}

static U32 PatrickStunCheckHeadbutt(xAnimTransition*, xAnimSingle*, void*)
{
    if (zEntPlayer_PowerupLevel(PLYR_ATAK_PAT_FLOP) <= ePowerupLevelNormal)
    {
        return false;
    }

    if (globals.player.cheat_mode)
    {
        return false;
    }

    return !globals.player.ControlOff && (globals.pad0->pressed & XPAD_MOVE(XPAD_BUTTON_TRIANGLE));
}

static U32 PatrickMeleeCB(xAnimTransition*, xAnimSingle* anim, void*)
{
    globals.player.DoMeleeCheck = 1;

    anim->CurrentSpeed = 0.0f;

    zEntPlayer_SNDPlay(ePlayerSnd_BellyMelee, true);
    sPlayerAttackInAir++;

    zEntPlayer_SNDStop(ePlayerSnd_CartwheelLoop);
    zEntPlayer_SNDStop(ePlayerSnd_Cartwheel2Loop);
    zEntPlayer_SNDPlayStream(ePlayerStreamSnd_PatBellyBounce);

    return 0;
}

static U32 PatrickGrabCB(xAnimTransition*, xAnimSingle*, void*)
{
    zEntPlayer_SNDPlay(ePlayerSnd_Lift1, true);
    zEntPlayer_SNDPlayStream(ePlayerStreamSnd_PatLift);

    sPatCarryCommentTimer.mCurrentTime = sPatCarryCommentTimer.mDuration;
    gThrowPower = 0.0f;
    globals.player.carry.grabbed = sGrabFound;
    globals.player.carry.grabTarget = 1;
    globals.player.carry.grabYclear = 0;

    if (sGrabFound->baseType == eBaseTypeBoulder)
    {
        globals.player.carry.targetRot =
            xatan2(sGrabFound->bound.sph.center.x -
                       xglobals->___player_ent_dont_use_directly->frame->mat.pos.x,
                   sGrabFound->bound.sph.center.z -
                       xglobals->___player_ent_dont_use_directly->frame->mat.pos.z);
    }
    else
    {
        globals.player.carry.targetRot =
            xatan2(sGrabFound->model->Mat->pos.x -
                       xglobals->___player_ent_dont_use_directly->frame->mat.pos.x,
                   sGrabFound->model->Mat->pos.z -
                       xglobals->___player_ent_dont_use_directly->frame->mat.pos.z);
    }

    globals.player.carry.grabLerpLast = 0.0f;

    xAnimState* stat = xAnimTableGetState(
        xglobals->___player_ent_dont_use_directly->model->Anim->Table, "Carry_PickupItem");
    if (stat)
    {
        xVec3 tmptran;
        xQuat tmpquat;
        iAnimEval(stat->Data->RawData[0], 0.03336667f, 1, &tmptran, &tmpquat);

        xMat4x3 objMat;
        xQuatToMat(&tmpquat, &objMat);
        xMat4x3 targetMat;
        objMat.pos = tmptran;
        xMat3x3Rot(&targetMat, &g_Y3, globals.player.carry.targetRot);
        targetMat.pos = xglobals->___player_ent_dont_use_directly->frame->mat.pos;

        xMat4x3Mul(&objMat, &objMat, &targetMat);

        if (globals.player.carry.grabbed->baseType == eBaseTypeBoulder)
        {
            globals.player.carry.grabOffset.x = objMat.pos.x - sGrabFound->bound.sph.center.x;
            globals.player.carry.grabOffset.y =
                sGrabFound->bound.sph.r + (objMat.pos.y - sGrabFound->bound.sph.center.y);
            globals.player.carry.grabOffset.z = objMat.pos.z - sGrabFound->bound.sph.center.z;
        }
        else
        {
            globals.player.carry.grabOffset.x = objMat.pos.x - sGrabFound->model->Mat->pos.x;
            globals.player.carry.grabOffset.y = objMat.pos.y - sGrabFound->model->Mat->pos.y;
            globals.player.carry.grabOffset.z = objMat.pos.z - sGrabFound->model->Mat->pos.z;
        }
    }
    else
    {
        globals.player.carry.grabOffset.x = 0.0f;
        globals.player.carry.grabOffset.y = 0.0f;
        globals.player.carry.grabOffset.z = 0.0f;
    }

    xMat3x3Rot(&globals.player.carry.spin, &g_Y3, -globals.player.carry.targetRot);
    xMat3x3Mul(&globals.player.carry.spin, (xMat3x3*)sGrabFound->model->Mat,
               &globals.player.carry.spin);
    xVec3Init(&globals.player.carry.spin.pos, 0.0f, 0.0f, 0.0f);

    sGrabFound->chkby &= ~XENT_COLLTYPE_PLYR;

    zThrown_AddFruit(sGrabFound);

    if (sGrabFound->baseType == eBaseTypeNPC)
    {
        ((zNMECommon*)sGrabFound)->SetCarryState(zNMECARRY_PICKUP);
    }

    sThrowDecalFactor = 0.0f;

    return 0;
}

static U32 PatCartwheelCheck(xAnimTransition*, xAnimSingle*, void*)
{
    if (zEntTeleportBox_playerIn())
    {
        return false;
    }

    if (gBusStopIsWaiting)
    {
        return false;
    }

    if (gCartwheelButton)
    {
        return false;
    }

    if (globals.player.g.PowerUp[PLYR_ATAK_PAT_CARTWHEEL - 1])
    {
        return !globals.player.ControlOff && (globals.pad0->on & XPAD_MOVE(XPAD_BUTTON_O));
    }

    return false;
}

static U32 PatCartwheelCB(xAnimTransition*, xAnimSingle*, void*)
{
    contactEnt = (xEnt*)xglobals->___player_ent_dont_use_directly->collis->colls[6].optr;

    cartwheel_first = 1;
    cartwheel_hit = 0;
    gCartwheelButton = 1;

    if (zEntPlayer_PowerupLevel(PLYR_ATAK_PAT_CARTWHEEL) > ePowerupLevelNormal)
        zEntPlayer_SNDPlay(ePlayerSnd_Cartwheel2Loop, true);
    else
        zEntPlayer_SNDPlay(ePlayerSnd_CartwheelLoop, true);

    zEntPlayer_SNDPlayStream(ePlayerStreamSnd_PatCartwheel);

    return 0;
}

static U32 PatCartwheelNoCartwheelCheck(xAnimTransition*, xAnimSingle*, void*)
{
    if (zEntTeleportBox_playerIn())
    {
        return true;
    }

    if (gBusStopIsRunning || gBusStopIsWaiting)
    {
        return true;
    }

    if (globals.player.Jump_Springboard)
    {
        return true;
    }

    if (cartwheel_first)
    {
        return false;
    }

    return globals.player.ControlOff || !(globals.pad0->on & XPAD_MOVE(XPAD_BUTTON_O));
}

static U32 PatNoCartwheelCB(xAnimTransition*, xAnimSingle*, void*)
{
    zEntPlayer_SNDStop(ePlayerSnd_CartwheelLoop);
    zEntPlayer_SNDStop(ePlayerSnd_Cartwheel2Loop);
    return 0;
}

static U32 PatCartwheelLoopCB(xAnimTransition*, xAnimSingle*, void*)
{
    return 0;
}

// No symbol (the helper and its name are ours): retail fills the floor entity's NULL case from
// result's register, which GC/2.6 does for an inline's return value but not for a named local.
static inline xEnt* GetPatrickTarget_FloorEnt(xCollis* coll)
{
    if (coll->flags & k_HIT_IT)
    {
        return (xEnt*)coll->optr;
    }
    return NULL;
}

static xEnt* GetPatrickTarget(xEnt* ent)
{
    xEnt* result = NULL;
    xCollis* coll = &ent->collis->colls[0];
    xEnt* floorEnt = GetPatrickTarget_FloorEnt(coll);
    zPlatform* plat = (zPlatform*)floorEnt;

    if (plat && plat->baseType == eBaseTypePlatform && (plat->plat_flags & 0x2))
    {
        F32 dx;
        F32 dz;
        F32 ddot;
        xVec3 relpos;
        xVec3 tgtpos;

        xMat4x3Tolocal(&relpos, (xMat4x3*)plat->model->Mat, (xVec3*)&ent->model->Mat->pos);
        relpos.z -= 2.0f;

        if (SQR(relpos.x) + SQR(relpos.z) < 0.5625f)
        {
            tgtpos.x = 0.0f;
            tgtpos.y = 1.229f;
            tgtpos.z = -2.0f;
            xMat4x3Toworld(&tgtpos, (xMat4x3*)plat->model->Mat, &tgtpos);

            dx = tgtpos.x - ent->model->Mat->pos.x;
            dz = tgtpos.z - ent->model->Mat->pos.z;
            ddot = dx * ent->model->Mat->at.x + dz * ent->model->Mat->at.z;

            if (ddot > 0.0f)
            {
                // This copy lets the floor entity's NULL case reuse result's register (see
                // GetPatrickTarget_FloorEnt)
                result = plat;

                globals.player.carry.targetRot =
                    xatan2(tgtpos.x - xglobals->___player_ent_dont_use_directly->frame->mat.pos.x,
                           tgtpos.z - xglobals->___player_ent_dont_use_directly->frame->mat.pos.z);
                globals.player.carry.throwTargetRotRate =
                    globals.player.carry.targetRot - ent->frame->rot.angle;

                CLAMP_ANGLE(globals.player.carry.throwTargetRotRate);
                globals.player.carry.throwTargetRotRate /= 0.2f;
                return result;
            }
        }
    }

    S32 i;
    F32 bestTargetDot = -1.0f;
    xVec3* bestTargetPos;
    zScene* zsc = globals.sceneCur;
    S32 grabbedIsFruit = zThrown_IsFruit(globals.player.carry.grabbed, NULL);

    for (i = 0; i < zsc->num_base; i++)
    {
        F32 maxHeight;
        xEnt* tgtent = (xEnt*)zsc->base[i];

        if (tgtent == globals.player.carry.grabbed)
        {
            continue;
        }

        if ((tgtent->flags & 0x85) != 0x1 && tgtent->baseType != eBaseTypeButton)
        {
            continue;
        }

        maxHeight = globals.player.carry.throwMaxHeight;

        if (tgtent->baseType == eBaseTypeStatic)
        {
            if (!grabbedIsFruit || !(tgtent->moreFlags & 0x8))
            {
                continue;
            }

            if (!zThrown_IsFruit(tgtent, NULL))
            {
                continue;
            }

            maxHeight = globals.player.carry.throwMaxStack;
        }
        else if (tgtent->baseType == eBaseTypeNPC)
        {
            if (!((zNMECommon*)tgtent)->runtimeData.flg_activeOn)
            {
                continue;
            }
        }
        else if (tgtent->baseType == eBaseTypeButton)
        {
            _zEntButton* entButton = (_zEntButton*)tgtent;

            if (zThrown_IsFruit(globals.player.carry.grabbed, NULL))
            {
                // not in the DWARF: the target evaluates the button state before the flag tests
                bool pressed = entButton->state == 3 || entButton->state == 1;

                if (!(entButton->basset->buttonActFlags & (0x10000 | 0x80)) ||
                    !(entButton->chkby & 0x10) || pressed)
                {
                    continue;
                }
            }
            else if (globals.player.carry.grabbed->baseType == eBaseTypeNPC &&
                     ((zNMECommon*)globals.player.carry.grabbed)->SelfType() == NME_TYPE_TIKI_STONE)
            {
                if (!(entButton->basset->buttonActFlags & (0x2000 | 0x40)))
                {
                    continue;
                }
            }
            else if (!(entButton->basset->buttonActFlags & 0x40))
            {
                continue;
            }
        }
        else
        {
            continue;
        }

        xMat3x3 targetingMat;
        xMat3x3Identity(&targetingMat);
        xMat3x3RotY(&targetingMat, sThrowAngle);

        F32 dx = tgtent->model->Mat->pos.x - ent->model->Mat->pos.x;
        F32 dy = tgtent->model->Mat->pos.y - ent->model->Mat->pos.y;
        F32 dz = tgtent->model->Mat->pos.z - ent->model->Mat->pos.z;
        F32 ddot = (dx * targetingMat.at.x + dz * targetingMat.at.z) / xsqrt(SQR(dx) + SQR(dz));
        F32 maxdist2;
        F32 mindist2;

        if (zEntPlayer_PowerupLevel(PLYR_ATAK_PAT_THROW) >= ePowerupLevelNormal && gThrowPower)
        {
            maxdist2 = SQR(globals.player.carry.throwHammerMaxDist);
            mindist2 = SQR(globals.player.carry.throwHammerMinDist);

            xVec3 toTarget;
            toTarget.x = dx;
            toTarget.y = dy;
            toTarget.z = dz;
            xVec3Normalize(&toTarget, &toTarget);

            if (xVec3Dot(&toTarget, &targetingMat.at) < globals.player.carry.throwMaxCosAngle ||
                SQR(dx) + SQR(dz) >= maxdist2 || SQR(dx) + SQR(dz) <= mindist2)
            {
                continue;
            }
        }
        else
        {
            maxdist2 = SQR(globals.player.carry.throwMaxDist);
            mindist2 = SQR(globals.player.carry.throwMinDist);

            if (SQR(dx) + SQR(dz) >= maxdist2 || dy <= globals.player.carry.throwMinHeight ||
                dy >= maxHeight || SQR(dx) + SQR(dz) <= mindist2 ||
                ddot < globals.player.carry.throwMaxCosAngle)
            {
                continue;
            }
        }

        if (bestTargetDot != -1.0f)
        {
            if (tgtent->model->Mat->pos.y > bestTargetPos->y)
            {
                if (ddot + 0.01f < bestTargetDot)
                {
                    continue;
                }
            }
            else if (ddot - 0.01f < bestTargetDot)
            {
                continue;
            }
        }

        result = tgtent;

        globals.player.carry.targetRot = xatan2(
            tgtent->model->Mat->pos.x - xglobals->___player_ent_dont_use_directly->frame->mat.pos.x,
            tgtent->model->Mat->pos.z -
                xglobals->___player_ent_dont_use_directly->frame->mat.pos.z);
        globals.player.carry.throwTargetRotRate =
            globals.player.carry.targetRot - ent->frame->rot.angle;

        CLAMP_ANGLE(globals.player.carry.throwTargetRotRate);
        globals.player.carry.throwTargetRotRate /= 0.2f;

        bestTargetDot = ddot;
        bestTargetPos = (xVec3*)&tgtent->model->Mat->pos;
    }

    return result;
}

static U32 PatrickGrabThrowStartCB(xAnimTransition*, xAnimSingle*, void* object)
{
    hammer_button = 1;
    hammer_start = 0;

    zEnt* ent = (zEnt*)object;

    if (gReticleTarget[0] && sTypeOfTarget[0] == 3)
    {
        globals.player.carry.throwTarget = gReticleTarget[0];
    }
    else
    {
        globals.player.carry.throwTarget = GetPatrickTarget(ent);
    }

    globals.player.carry.flyingToTarget = NULL;
    sPatCarryCommentTimer.mCurrentTime = 0.0f;
    return 0;
}

static U32 PatrickHammerCB(xAnimTransition*, xAnimSingle*, void*)
{
    if (globals.player.carry.grabbed && globals.player.carry.grabbed->baseType == eBaseTypeNPC)
    {
        ((zNMECommon*)globals.player.carry.grabbed)->SetCarryState(zNMECARRY_TWIRL);
    }

    hammer_first = 1;
    hammer_throw = 0;
    sLastHammerTime = 0.0f;

    zEntPlayerFX::HammerThrow::SetDust(true);
    zEntPlayer_SNDPlay(ePlayerSnd_HammerThrowLoop, true);
    zEntPlayer_SNDSetPitch(ePlayerSnd_HammerThrowLoop, 1.0f);
    sPatCarryCommentTimer.mCurrentTime = 0.0f;
    return 0;
}

static U32 PatrickHammerSpinCB(xAnimTransition*, xAnimSingle*, void*)
{
    sThrowChargeTime = 0.0f;
    return 0;
}

static U32 PatrickHammerThrowCB(xAnimTransition*, xAnimSingle*, void* object)
{
    zEnt* ent = (zEnt*)object;

    if (gReticleTarget[0] && sTypeOfTarget[0] == 3)
    {
        globals.player.carry.throwTarget = gReticleTarget[0];
    }
    else
    {
        globals.player.carry.throwTarget = GetPatrickTarget(ent);
    }

    globals.player.carry.flyingToTarget = NULL;
    zEntPlayer_SNDStop(ePlayerSnd_HammerThrowLoop);
    zEntPlayer_SNDPlay(ePlayerSnd_HammerThrow, true);
    zEntPlayer_SNDPlayStream(ePlayerStreamSnd_PatHammerThrowRelease);

    return 0;
}

static U32 LassoSwingGroundedBeginCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return sLassoInfo->swingTarget && globals.player.JumpState == 0;
}

static U32 LassoSwingBeginCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return sLassoInfo->swingTarget && globals.player.JumpState != 0;
}

static U32 LassoSwingReleaseCheck(xAnimTransition*, xAnimSingle*, void*)
{
    return (!globals.player.ControlOff && (globals.pad0->pressed & XPAD_BUTTON_X) &&
            sSwingTimeElapsed > 0.5f) ||
           sLassoInfo->swingTarget == NULL || !xEntIsVisible(sLassoInfo->swingTarget);
}

static U32 LassoSwingBeginCB(xAnimTransition*, xAnimSingle*, void* object)
{
    xEnt* ent = (xEnt*)object;

    gReticleTarget[1] = NULL;

    if (sLasso->flags & 1)
    {
        sLasso->flags = 1;
        zLasso_ResetTimer(sLasso, 0.133f);
        xVec3Copy(&sLasso->tgCenter, &sLasso->stCenter);
    }
    else
    {
        sLasso->flags = 0x1043;
        zLasso_InitTimer(sLasso, 0.133f);
        xVec3Copy(&sLasso->stNormal, (xVec3*)&ent->model->Mat->right);
        zEntPlayer_SNDPlay(ePlayerSnd_Heli, true);
        zEntPlayer_SNDPlayStream(ePlayerStreamSnd_PatTongueSwing);
    }

    sLasso->tgSlack = 0.5f;
    sLasso->tgRadius = sLassoInfo->swingTarget->model->Data->boundingSphere.radius;
    xVec3Copy(&sLasso->tgNormal, (xVec3*)&ent->model->Mat->right);
    xVec3Copy(&globals.player.HangVel, (xVec3*)&ent->frame->vel);
    sSwingTimeElapsed = 0.0f;

    zCameraSetBbounce(false);
    zCameraSetLongbounce(false);
    zCameraSetHighbounce(false);
    return 0;
}

static U32 LassoSwingGroundedBeginCB(xAnimTransition* tran, xAnimSingle* anim, void* object)
{
    xEnt* ent = (xEnt*)object;

    JumpCB(tran, anim, object);
    ent->frame->vel.y *= 0.5f;

    LassoSwingBeginCB(tran, anim, object);
    return 0;
}

static U32 LassoSwingTossCB(xAnimTransition*, xAnimSingle*, void*)
{
    zEntPlayer_SNDStop(ePlayerSnd_Heli);
    zEntPlayer_SNDPlay(ePlayerSnd_LassoThrow, true);

    sLasso->flags = 1;
    zLasso_ResetTimer(sLasso, 0.117f);
    xVec3Copy(&sLasso->tgCenter, xBoundCenter(&sLassoInfo->swingTarget->bound));
    xVec3AddScaled(&sLasso->tgCenter, (xVec3*)&sLassoInfo->swingTarget->model->Mat->up, -0.6f);
    xVec3Copy(&sLasso->tgNormal, (xVec3*)&sLassoInfo->swingTarget->model->Mat->up);

    if (xVec3Dot(&sLasso->tgNormal, &sLasso->stNormal) < 0.0f)
    {
        xVec3Inv(&sLasso->tgNormal, &sLasso->tgNormal);
    }

    sLasso->tgRadius = 0.1f;
    return 0;
}

static U32 LassoSwingCB(xAnimTransition*, xAnimSingle* anim, void*)
{
    sLasso->flags = 0xc21;
    zLasso_ResetTimer(sLasso, 0.0f);

    anim->BilinearLerp[0] = 1.0f;
    anim->Blend->BilinearLerp[0] = 1.0f;

    zCameraEnableLassoCam();
    zCameraSetLassoCamFactor(1.0f);

    xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("PAT_SWING_ATTACH_GRP")), 0,
                (xVec3*)&sLassoInfo->swingTarget->model->Mat->pos, NULL, NULL, NULL, NULL);
    return 0;
}

static U32 LassoSwingGroundedCB(xAnimTransition*, xAnimSingle*, void*)
{
    zEntPlayer_SNDStop(ePlayerSnd_Heli);
    idle_tmr = 0.0f;
    sTimeToRetarget[1] = 0.5f;
    sLassoInfo->swingTarget = NULL;
    sLasso->flags = 0;
    sLassoCamLinger = 1;

    return 0;
}

static U32 LassoSwingReleaseCB(xAnimTransition* tran, xAnimSingle* anim, void* object)
{
    zEntPlayer_SNDStop(ePlayerSnd_Heli);
    idle_tmr = 0.0f;
    sTimeToRetarget[1] = 0.5f;
    globals.player.Jump_CanDouble = 1;
    globals.player.IsDJumping = 0;
    sLassoCamLinger = 1;
    sLassoInfo->swingTarget = NULL;
    sLasso->flags = 0;

    JumpCB(tran, anim, object);

    return 0;
}

// Retail bug: "StunJump" is tested twice ("StunJump02" was probably intended).
static U32 StunBubbleTrail(xAnimSingle* single)
{
    xAnimState* astate = single->State;
    return strcmp(astate->Name, "StunFall") == 0 || strcmp(astate->Name, "StunFall02") == 0 ||
           ((strcmp(astate->Name, "StunJump") == 0 || strcmp(astate->Name, "StunJump") == 0) &&
            single->Time >= 0.6f && single->Time <= 1.0f);
}

static U32 BubbleBashContrails(xAnimSingle* single)
{
    xAnimState* astate = single->State;
    return (strcmp(astate->Name, "BbashStart01") == 0 ||
            strcmp(astate->Name, "BbashStart02") == 0) &&
           single->Time >= 0.25f && single->Time <= 1.0f;
}

static U32 BubbleBounceContrails(xAnimSingle* single)
{
    xAnimState* astate = single->State;
    return (strcmp(astate->Name, "BbounceStart01") == 0 && single->Time >= 0.9f) ||
           strcmp(astate->Name, "BbounceAttack01") == 0;
}

static U32 StunStartFallCB(xAnimTransition*, xAnimSingle*, void*)
{
    stun_power_tmr = 0.0f;
    return 0;
}

// 'upgraded' is not in the PS2 DWARF, but retail computes the index once before the branches.
static U32 StunRadiusCB(xAnimTransition*, xAnimSingle*, void*)
{
    if (zEntPlayer_PowerupLevel(PLYR_ATAK_PAT_FLOP) > ePowerupLevelNormal)
    {
        zEntPlayer_SNDPlay(ePlayerSnd_BellySmash2, true);
    }
    else
    {
        zEntPlayer_SNDPlay(ePlayerSnd_BellySmash, true);
    }

    zEntPlayer_SNDPlayStream(ePlayerStreamSnd_PatBellyFlop);

    S32 upgraded = globals.player.g.PowerUp[PLYR_ATAK_PAT_FLOP - 1] > 1;
    if (tslide_lastrealvel.y > -1.0f)
    {
        if (stun_rumble[upgraded][0])
        {
            zEntEvent(stun_rumble[upgraded][0], eEventStartRumbleEffect, globals.currentActivePad,
                      0.0f, 0.0f, 0.0f);
        }
    }
    else if (stun_power_tmr < 0.4f)
    {
        if (stun_rumble[upgraded][1])
        {
            zEntEvent(stun_rumble[upgraded][1], eEventStartRumbleEffect, globals.currentActivePad,
                      0.0f, 0.0f, 0.0f);
        }
    }
    else if (stun_power_tmr < 1.0f)
    {
        if (stun_rumble[upgraded][2])
        {
            zEntEvent(stun_rumble[upgraded][2], eEventStartRumbleEffect, globals.currentActivePad,
                      0.0f, 0.0f, 0.0f);
        }
    }
    else
    {
        if (stun_rumble[upgraded][3])
        {
            zEntEvent(stun_rumble[upgraded][3], eEventStartRumbleEffect, globals.currentActivePad,
                      0.0f, 0.0f, 0.0f);
        }
    }

    return 0;
}

static S32 zEntPlayerGauntletSetup(xEnt* ent)
{
    if (sGauntletHaz)
    {
        return 0;
    }

    sGauntletHaz = HAZ_Acquire();
    if (sGauntletHaz)
    {
        sGauntletHaz->ConfigHelper(HAZ_TYP_PLYRBANG_V1);
    }
    else
    {
        return 0;
    }

    if (ent)
    {
        sGauntletHaz->custdata.bang.ent = ent;
    }

    zEntPlayer_SNDPlayStream(ePlayerStreamSnd_SBHelmetEmbedded);
    return 1;
}

void zEntPlayerKillGauntlet()
{
    sGauntletHaz = NULL;
    sGauntletBlastRadius = 0.0f;
    zEntPlayerFX::GauntletExplosion::Exploding = false;
    zEntPlayerFX::GauntletExplosion::Sparking = false;
}

// The compiled-out placeholders (conditions unknown) keep this over the inlining limit, as retail
// calls it.
static S32 AttackBoundCollide(xEnt* ent, zScene* zsc, xBound* meleeB, en_plyratak attack)
{
    Melee_cbData cbdata;
    xVec3 pos;
    S32 doHazChk;
    en_plyrpup pup;
    S32 hitSave;

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    cbdata.ent = ent;
    cbdata.zsc = zsc;
    cbdata.meleeB = meleeB;
    cbdata.hitsomething = 0;
    cbdata.attackType = attack;

    xVec3Copy(&pos, &meleeB->sph.center);
    xGridCheckPosition(&colls_grid, &pos, &meleeB->qcd, CheckObjectAgainstBound, &cbdata);
    xGridCheckPosition(&colls_oso_grid, &pos, &meleeB->qcd, CheckObjectAgainstBound, &cbdata);
    xGridCheckPosition(&npcs_grid, &pos, &meleeB->qcd, CheckObjectAgainstBound, &cbdata);

    switch (gCurrentPlayer)
    {
    case eCurrentPlayerSpongeBob:
        pup = zEntPlayer_PowerupLevel(PLYR_ATAK_SB_KARATE);
        doHazChk = pup > ePowerupLevelNormal;
        break;
    case eCurrentPlayerPatrick:
        pup = zEntPlayer_PowerupLevel(PLYR_ATAK_PAT_BELLY);
        doHazChk = pup > ePowerupLevelNormal;
        break;
    default:
        doHazChk = 0;
        break;
    }

    if (doHazChk && (attack == PLYR_ATAK_SB_KARATE || attack == PLYR_ATAK_PAT_BELLY))
    {
        hitSave = cbdata.hitsomething;
        cbdata.hitsomething = 0;
        HAZ_Iterate(HazardCheck, &cbdata, 0x400000);

        if (cbdata.hitsomething)
        {
            zEntPlayer_SNDPlay(ePlayerSnd_MeleeDeflect, true);
            zEntPlayer_SNDPlayStream(ePlayerStreamSnd_MeleeDeflect);
        }

        cbdata.hitsomething = hitSave || cbdata.hitsomething;
    }

    return cbdata.hitsomething;
}

static S32 CheckObjectAgainstBound(xEnt* cbent, void* cbdata)
{
    Melee_cbData* data = (Melee_cbData*)cbdata;
    S32 hitsomething = data->hitsomething;
    xEnt* ent = data->ent;
    en_plyratak attack = data->attackType;
    xBound* meleeB = data->meleeB;
    xCollis meleeColl;
    xBound tempBound;
    zEntSimpleObj* simpleObj;
    en_xEventTags attackEvent;
    en_npcdmg attackNMEDamageType;
    U32 attackButtonActType;

    if (!(cbent->baseFlags & 0x20) ||
        !((cbent->moreFlags & 0x10) || cbent->baseType == eBaseTypeStatic) ||
        !xEntIsVisible(cbent) || !cbent->model)
    {
        return 1;
    }

    // Two specific models (model asset IDs) are tested with a sphere sized from their world
    // bounding sphere instead of their own bound.
    if (cbent->model->modelID == 0xEF21FB86 || cbent->model->modelID == 0x48EA2238)
    {
        tempBound.type = XBOUND_TYPE_SPHERE;
        xVec3Copy(&tempBound.sph.center, &cbent->bound.sph.center);
        tempBound.sph.r = 0.6f * cbent->model->Data->worldBoundingSphere.radius;
        xQuickCullForBound(&tempBound.qcd, &tempBound);
        xBoundHitsBound(meleeB, &tempBound, &meleeColl);
    }
    else
    {
        xBoundHitsBound(meleeB, &cbent->bound, &meleeColl);
    }

    if (!(meleeColl.flags & k_HIT_IT))
    {
        return 1;
    }

    if (cbent->baseType == eBaseTypeStatic)
    {
        if (zThrown_IsFruit(cbent, NULL))
        {
            zThrown_AddFruit(cbent);
            zThrown_KillFruit(cbent);
        }
        else
        {
            simpleObj = (zEntSimpleObj*)cbent;
            if (!(cbent->moreFlags & 0x10) && !simpleObj->destructible)
            {
                return 1;
            }
        }
    }

    attackButtonActType = 0;
    switch (attack)
    {
    case PLYR_ATAK_SB_KARATE:
        attackButtonActType |= 0x1;
        attackEvent = eEventHit_Melee;
        attackNMEDamageType = DMGTYP_SIDE;
        break;
    case PLYR_ATAK_SB_BASH:
        attackButtonActType |= 0x4;
        attackEvent = eEventHit_BubbleBash;
        attackNMEDamageType = DMGTYP_BELOW;
        break;
    case PLYR_ATAK_SB_BOWL:
        attackButtonActType |= 0x8;
        attackEvent = eEventHit_BubbleBowl;
        attackNMEDamageType = DMGTYP_BUBBOWL;
        break;
    case PLYR_ATAK_SB_GUITAR:
        attackButtonActType |= 0x10;
        attackEvent = eEventHit_Cruise;
        attackNMEDamageType = DMGTYP_CRUISEBUBBLE;
        break;
    case PLYR_ATAK_PAT_BELLY:
        attackButtonActType |= 0x8000;
        attackEvent = eEventHit_Melee;
        attackNMEDamageType = DMGTYP_SIDE;
        break;
    case PLYR_ATAK_PAT_CARTWHEEL:
        attackButtonActType |= 0x20000;
        attackEvent = eEventHit_Cartwheel;
        attackNMEDamageType = DMGTYP_CARTWHEEL;
        break;
    case PLYR_ATAK_PAT_FLOP:
        attackButtonActType |= 0x100;
        attackEvent = eEventHit_PatrickSlam;
        attackNMEDamageType = DMGTYP_ABOVE;
        break;
    case PLYR_ATAK_PAT_THROW:
        attackButtonActType |= 0xC0;
        attackEvent = eEventHit_Throw;
        attackNMEDamageType = DMGTYP_HITBYTOSS;
        break;
    case PLYR_ATAK_SLIDE:
        attackButtonActType |= 0x1;
        attackEvent = eEventHit_Melee;
        attackNMEDamageType = DMGTYP_SLIDE;
        break;
    }

    if (cbent->baseType != eBaseTypeNPC && cbent->baseType != eBaseTypeBoulder)
    {
        F32 hitParams[4] = { 1.0f, 9.0f, 0.0f, 0.0f };
        zEntEvent(cbent, eEventHit, hitParams);
    }

    if (cbent->baseType == eBaseTypeNPC)
    {
        zNMECommon* npc = (zNMECommon*)cbent;

        if (globals.player.SundaeTimer > 0.0f)
        {
            npc->Damage(DMGTYP_INSTAKILL, xglobals->___player_ent_dont_use_directly, NULL, -1,
                        ePowerupLevelNormal);
        }
        else
        {
            npc->Damage(attackNMEDamageType, xglobals->___player_ent_dont_use_directly, NULL, -1,
                        ePowerupLevelNormal);
        }

        hitsomething = 1;

        if (npc->itemType >= NME_TYPE_TIKI && npc->itemType <= NME_TYPE_TIKI_STONE)
        {
            zEntPlayer_TikiKilledBy(npc, xglobals->___player_ent_dont_use_directly);
        }
        else if (attackEvent == eEventHit_BubbleBash && !sBashSoundTimer &&
                 zEntPlayer_PowerupLevel(PLYR_ATAK_SB_BASH) == ePowerupLevelNormal)
        {
            sBashSoundTimer = 0.2f;
            zEntPlayer_SNDPlay(ePlayerSnd_BubbleBashHit1, true);
        }
    }
    else if (cbent->baseType == eBaseTypeBoulder)
    {
        xEntBoulder* boul = (xEntBoulder*)cbent;
        xVec3 f;

        if (boul->basset->flags & 0x100)
        {
            zEntEvent(cbent, attackEvent);
        }

        xVec3Sub(&f, (xVec3*)&cbent->model->Mat->pos, (xVec3*)&ent->model->Mat->pos);
        xVec3Normalize(&f, &f);
        xVec3SMulBy(&f, 10.0f);
        xEntBoulder_AddForce(boul, &f);

        hitsomething = 1;
    }
    else if (cbent->baseType == eBaseTypeButton)
    {
        zEntButton_Press((_zEntButton*)cbent, attackButtonActType);

        hitsomething = 1;

        if (attackEvent == eEventHit_BubbleBash && !sBashSoundTimer &&
            zEntPlayer_PowerupLevel(PLYR_ATAK_SB_BASH) == ePowerupLevelNormal)
        {
            sBashSoundTimer = 0.2f;
            zEntPlayer_SNDPlay(ePlayerSnd_BubbleBashHit1, true);
        }
    }
    else if (cbent->baseType == eBaseTypePlatform && cbent->subType == ZPLATFORM_SUBTYPE_PADDLE)
    {
        U32 paddleFlags = ((zPlatform*)cbent)->plat_asset->paddle.paddleFlags;

        if ((attack == PLYR_ATAK_SB_KARATE || attack == PLYR_ATAK_PAT_BELLY) &&
            ((gCurrentPlayer == eCurrentPlayerSpongeBob && (paddleFlags & 0x8)) ||
             (gCurrentPlayer == eCurrentPlayerPatrick && (paddleFlags & 0x80))))
        {
            U32 worldSpaceNorm = 0;
            xRay3 tempray;

            if (gCurrentPlayer == eCurrentPlayerSpongeBob)
            {
                tempray.origin.x = ent->model->Mat->pos.x;
                tempray.origin.y = ent->model->Mat->pos.y + ent->bound.sph.r;
                tempray.origin.z = ent->model->Mat->pos.z;
                xVec3Sub(&tempray.dir, &meleeB->sph.center, &tempray.origin);
                tempray.min_t = 0.0f;
                tempray.max_t = meleeB->sph.r + xVec3Normalize(&tempray.dir, &tempray.dir);
            }
            else
            {
                tempray.dir = *(xVec3*)&ent->model->Mat->at;
                tempray.origin.x = meleeB->sph.center.x;
                tempray.origin.y = meleeB->sph.center.y;
                tempray.origin.z = meleeB->sph.center.z;
                // not in the DWARF (probably an inlined helper's local): retail subtracts into its
                // own stack vector.
                xVec3 v;
                xVec3Sub(&v, (xVec3*)&ent->model->Mat->pos, &tempray.origin);
                tempray.min_t = xVec3Dot(&v, &tempray.dir);
                tempray.max_t = 0.0f;
                if (tempray.min_t > 0.0f)
                {
                    tempray.min_t = -meleeB->sph.r;
                }
            }

            tempray.flags = XRAY3_USE_MIN | XRAY3_USE_MAX;
            meleeColl.flags = k_HIT_0x200;
            xRayHitsModel(&tempray, cbent->model, &meleeColl);

            if (!(meleeColl.flags & k_HIT_IT))
            {
                meleeColl.flags = k_HIT_0x200;
                worldSpaceNorm = 1;
                xSphereHitsModel(&meleeB->sph, cbent->model, &meleeColl);
            }

            if (meleeColl.flags & k_HIT_IT)
            {
                meleeColl.optr = cbent;
                if (zPlatform_PaddleCollide(&meleeColl, (xVec3*)&ent->model->Mat->pos, &tempray.dir,
                                            worldSpaceNorm))
                {
                    hitsomething = 1;
                }
            }
        }
    }
    else
    {
        if (cbent->baseType == eBaseTypeStatic)
        {
            F32 hitParams[4] = { 1.0f, 9.0f, 0.0f, 0.0f };
            zEntEvent(cbent, attackEvent, hitParams);
        }
        else
        {
            zEntEvent(cbent, attackEvent);
        }

        hitsomething = 1;
    }

    if (hitsomething)
    {
        zFX_SpawnBubbleHit(&meleeB->sph.center, 10);
    }

    data->hitsomething = hitsomething;
    return 1;
}

static bool HazardCheck(zNMEHazard& haz, void* context)
{
    Melee_cbData* cbdata = (Melee_cbData*)context;
    xBound* bnd = cbdata->meleeB;

    xVec3 vec_toHaz = haz.pos_hazard - bnd->sph.center;
    if (vec_toHaz.length() - haz.custdata.typical.rad_cur > bnd->sph.r)
    {
        return true;
    }

    ReturnTheFavor(&haz);

    cbdata->hitsomething = 1;
    zFX_SpawnBubbleHit(&bnd->sph.center, 10);

    return true;
}

// Retail bugs kept: without an owner the hazard is restarted with an uninitialized pos_tgt, and the
// owner path computes a length whose result is unused.
static void ReturnTheFavor(zNMEHazard* haz)
{
    xVec3 pos_src = haz->pos_hazard;
    xVec3 pos_tgt;

    if (haz->npc_owner)
    {
        pos_tgt = haz->npc_owner->bound.sph.center;

        xVec3 vec = pos_tgt - pos_src;
        // not in the DWARF (dead): retail keeps xsqrt's constant loads here.
        F32 dist = vec.length();
    }
    else
    {
        // Retail bug: this pos_tgt (second pos_tgt in the DWARF) shadows the outer one, which stays
        // uninitialized on this path.
        xVec3 pos_tgt =
            pos_src - xModelGetFrame(xglobals->___player_ent_dont_use_directly->model)->pos;
        F32 len = pos_tgt.length();
        if (len > 0.00001f)
        {
            pos_tgt /= len;
        }
        else
        {
            pos_tgt = xModelGetFrame(xglobals->___player_ent_dont_use_directly->model)->at;
        }
        pos_tgt *= 20.0f;
        pos_tgt += xModelGetFrame(xglobals->___player_ent_dont_use_directly->model)->pos;
    }

    haz->Reconfigure(HAZ_TYP_REFLECT_V1);
    haz->custdata.lobdata.pos_tgt = pos_tgt;
    haz->Start(&pos_src, -1.0f);
}

// center and dot are not in the PS2 DWARF, but retail copies the center into its own stack slot
// and holds the first dot product across the distance computation.
static S32 CheckNMEAgainstBubbleBowl(xEnt* cbent, void* cbdata)
{
    zNMECommon* npc;
    xVec3 velocityNorm;
    xVec3 center;
    xVec3 toTarget;
    xVec3 toTargetNorm;
    F32 distsqr;
    zEntPlayerBowlTarget* t;

    if (cbent->baseType != eBaseTypeNPC)
    {
        return 1;
    }

    npc = (zNMECommon*)cbent;
    if ((npc->flags & 0x85) != 0x1)
    {
        return 1;
    }

    velocityNorm = *(xVec3*)cbdata;

    xVec3Copy(&center, xBoundCenter(&cbent->bound));
    xVec3Sub(&toTarget, &center, (xVec3*)&zEntPlayerGetBBowl()->model->Mat->pos);
    xVec3Normalize(&toTargetNorm, &toTarget);

    F32 dot = xVec3Dot(&toTargetNorm, &velocityNorm);

    F32 dx__ = zEntPlayerGetBBowl()->model->Mat->pos.x - center.x;
    F32 dy__ = zEntPlayerGetBBowl()->model->Mat->pos.y - center.y;
    F32 dz__ = zEntPlayerGetBBowl()->model->Mat->pos.z - center.z;
    distsqr = dx__ * dx__ + dy__ * dy__ + dz__ * dz__;

    if (dot < 0.0f)
    {
        return 1;
    }

    if (distsqr <= 16.0f)
    {
        dot = xVec3Dot(&velocityNorm, &toTarget);
        F32 chkdistSqu = toTarget.length2() - dot * dot;
        if (chkdistSqu < 2.0f * zEntPlayerGetBBowl()->bound.sph.r)
        {
            return 1;
        }

        if (!zEntPlayerBowlTarget_Exists(cbent))
        {
            t = zEntPlayerBowlTarget_Add(xBoundCenter(&cbent->bound));
            t->ent = cbent;
        }
    }

    return 1;
}

static S32 CheckObjectAgainstGauntlet(xEnt* cbent, void* cbdata)
{
    xBound* meleeB;
    xCollis meleeColl;
    zEntSimpleObj* simpleObj;
    xVec3 boundCenter;

    meleeB = ((Melee_cbData*)cbdata)->meleeB;

    xBoundHitsBound(meleeB, &cbent->bound, &meleeColl);

    if (!(cbent->baseFlags & 0x20) ||
        !((cbent->moreFlags & 0x10) || cbent->baseType == eBaseTypeStatic) ||
        !xEntIsVisible(cbent) || !cbent->model || cbent == zEntPlayerGetBBowl())
    {
        return 1;
    }

    if (cbent->baseType == eBaseTypeStatic)
    {
        if (zThrown_IsFruit(cbent, NULL))
        {
            zThrown_AddFruit(cbent);
            zThrown_KillFruit(cbent);
        }
        else
        {
            simpleObj = (zEntSimpleObj*)cbent;
            if (!simpleObj->destructible)
            {
                return 1;
            }
        }
    }

    xBoundGetCenter(meleeB, &boundCenter);
    if (cbent->bound.box.center.y < boundCenter.y)
    {
        return 1;
    }

    zEntPlayerGauntletSetup(cbent);
    sGauntletBoundHit = 1;

    return 1;
}

// Included here: retail's string pool has the anim tables' strings right after load_player_ini's.
#include "zEntPlayerAnimationTables.h"

static S32 load_talk_filter(U8* filter, xModelAssetParam* params, U32 params_size, S32 max_size)
{
    S32 size = 0;
    F32* non_choices = (F32*)xMemPushTemp(max_size * sizeof(F32));
    // not in the PS2 DWARF (BfBB's DWARF lists it)
    S32 found = zParamGetFloatList(params, params_size, "NonRandomTalkAnims", max_size, non_choices,
                                   non_choices);
    F32* non_choice; // not in the DWARF (nor BfBB's): the inner loop walks a pointer
    S32 i;
    U8 skip;
    S32 j;

    for (i = 0; i < max_size; i++)
    {
        skip = FALSE;
        non_choice = non_choices;
        for (j = 0; j < found; j++)
        {
            if ((S32)*non_choice - 1 == i)
            {
                skip = TRUE;
                break;
            }
            non_choice++;
        }

        if (!skip)
        {
            filter[size] = i;
            size++;
        }
    }

    if (size <= 0)
    {
        filter[0] = 0;
        size = 1;
    }

    xMemPopTemp(non_choices);
    return size;
}

static U32 count_talk_anims(xAnimTable* anims)
{
    xAnimFile* firstData = anims->StateList->Data;
    char talkAnimName[16];
    S32 talkAnimCount = 0;

    sprintf(talkAnimName, "Talk%02d", 1);

    for (xAnimState* state = anims->StateList; state != NULL; state = state->Next)
    {
        if (stricmp(state->Name, talkAnimName) == 0)
        {
            if (state->Data == firstData || ++talkAnimCount >= 4)
            {
                break;
            }
            sprintf(talkAnimName, "Talk%02d", talkAnimCount + 1);
        }
    }

    return talkAnimCount;
}

static void load_player_ini()
{
    if (globals.player.model_spongebob != NULL)
    {
        U16 params_size;
        xModelAssetParam* params = zEntGetModelParams(
            xglobals->___player_ent_dont_use_directly->asset->modelInfoID, &params_size);

        if (globals.player.model_spongebob->Anim != NULL)
        {
            globals.player.sb.talk_anims =
                count_talk_anims(globals.player.model_spongebob->Anim->Table);
            globals.player.sb.talk_filter_size = load_talk_filter(
                globals.player.sb.talk_filter, params, params_size, globals.player.sb.talk_anims);
        }
    }

    if (globals.player.model_patrick != NULL)
    {
        U16 params_size;
        xModelAssetParam* params = zEntGetModelParams(
            xglobals->___player_ent_dont_use_directly->asset->modelInfoID, &params_size);

        if (globals.player.model_patrick->Anim != NULL)
        {
            globals.player.patrick.talk_anims =
                count_talk_anims(globals.player.model_patrick->Anim->Table);
            globals.player.patrick.talk_filter_size =
                load_talk_filter(globals.player.patrick.talk_filter, params, params_size,
                                 globals.player.patrick.talk_anims);
        }
    }
}

void zEntPlayerCommonInit(xEnt* ent, xEntAsset* asset)
{
    if (!gInitShadow)
    {
        xEntInitShadow(*ent, globals.player.entShadow_embedded);
        ent->simpShadow = &globals.player.simpShadow_embedded;
        xShadowSimple_CacheInit(ent->simpShadow, ent, 80);
        gInitShadow = TRUE;
    }

    // The light kit ID follows the entity's links in the asset.
    if (*(U32*)((xLinkAsset*)(asset + 1) + ent->linkCount))
    {
        xLightKit* pLightKit =
            (xLightKit*)xSTFindAsset(*(U32*)((xLinkAsset*)(asset + 1) + ent->linkCount), NULL);

        if (pLightKit && pLightKit->tagID != 'TIKL')
        {
            pLightKit = NULL;
        }

        globals.player.pPlayerLightKit = pLightKit;
    }

    load_player_ini();

    xBaseEnable(ent);
    xEntShow(ent);

    globals.player.Visible = 1;
    globals.player.AutoMoveSpeed = 0;
    ent->pflags &= (U8)~0x4;
    ent->collis->chk &= (U8)~0x1;
    ent->move = zEntPlayer_Move;

    if (ent->linkCount)
    {
        ent->link = (xLinkAsset*)(asset + 1);
    }
    else
    {
        ent->link = NULL;
    }

    xFFXRotMatchState* rms = xFFXRotMatchAlloc();
    if (rms)
    {
        rms->max_decl = globals.player.g.RotMatchMaxAngle;
        rms->tmatch = globals.player.g.RotMatchMatchTime;
        rms->trelax = globals.player.g.RotMatchRelaxTime;
        rms->tmr = 0.0f;
        xFFXAddEffect(ent, PlayerRotMatchUpdateEnt, rms);
    }

    lastgCurrentPlayer = eCurrentPlayerCount;

    globals.player.ControlOff = 0;
    globals.player.PlayerIsFrozen = 0;

    if (oob_state::OOBState::shared.control)
    {
        zEntPlayerControlOff(CONTROL_OWNER_OOB);
        globals.player.ControlOffTimer = 1e38f;
    }

    sBowlBlastRadius = 0.0f;
    gBowlButton = 0;
    sStartBubbleBowlTimer = 0.0f;
    sNumActiveBowlTargets = 0;
    sBowlTargetHead = NULL;
    sBowlTargetTail = NULL;
    sThrowBlastRadius = 0.0f;
    sThrowBlastPosition = g_O3;

    zEntPlayer_SNDInit();

    sPlayerDiedLastTime = 0;

    if (--sPlayerIgnoreSound < 0)
    {
        sPlayerIgnoreSound = 0;
    }

    sBowlLightningAddInfo.type = 1;
    sBowlLightningAddInfo.flags = 0x10;
    sBowlLightningAddInfo.total_points = 12;
    sBowlLightningAddInfo.thickness = 1.0f;
    sBowlLightningAddInfo.color = g_WHITE;
    sBowlLightningAddInfo.rand_radius = 0.1f;
    sBowlLightningAddInfo.arc_height = 0.1f;
    sBowlLightningAddInfo.rot_radius = 0.1f;
    sBowlLightningAddInfo.setup_degrees = 65.0f;
    sBowlLightningAddInfo.move_degrees = 5000.0f;

    zEntPlayerFX::SuperMachoBowlLightning::Init();
    zEntPlayerFX::HammerThrow::Init();
    zEntPlayerFX::Ribbon::Init();
    oob_state::init();
    zEntPlayerFX::MotionBlur::Init();

    sPlayerKillAnimMovement = 0;
    sBowlTargetHead = NULL;
    sBowlTargetTail = NULL;
}

void zEntPlayer_SpongeBobInit(xEnt* ent, xEntAsset* asset, S32 costumeIndex)
{
    static S32 drybob_anim_count;
    static void** drybob_chgData[64];
    static void* drybob_oldData[64];
    static F32* drybob_chgTime[64];
    static F32 drybob_oldTime[64];

    void* sbInfo;
    U32 sbMINFSize;

    globals.player.sb_model_indices = spongebobCostumeAtomicOrder[costumeIndex];

    sbInfo = xSTFindAsset(0x003FE4D5, &sbMINFSize);
    if (!sbInfo)
    {
        sbInfo = xSTFindAsset(0x2CFC789A, &sbMINFSize);
        gCurrentCostume = COSTUME_SB_1;
    }
    if (!sbInfo)
    {
        sbInfo = xSTFindAsset(0x93D10E43, &sbMINFSize);
        gCurrentCostume = COSTUME_SB_2;
    }
    if (!sbInfo)
    {
        sbInfo = xSTFindAsset(0xFAA5A3EC, &sbMINFSize);
        gCurrentCostume = COSTUME_SB_3;
    }
    if (!sbInfo)
    {
        sbInfo = xSTFindAsset(0x617A3995, &sbMINFSize);
        gCurrentCostume = COSTUME_SB_4;
    }
    if (!sbInfo)
    {
        sbInfo = xSTFindAsset(0xC84ECF3E, &sbMINFSize);
        gCurrentCostume = COSTUME_SB_5;
    }
    if (!sbInfo)
    {
        sbInfo = xSTFindAsset(0x2F2364E7, &sbMINFSize);
        gCurrentCostume = COSTUME_SB_6;
    }
    if (!sbInfo)
    {
        sbInfo = xSTFindAsset(0x95F7FA90, &sbMINFSize);
        gCurrentCostume = COSTUME_SB_7;
    }
    if (!sbInfo)
    {
        sbInfo = xSTFindAsset(0x9A45D32A, &sbMINFSize);
        gCurrentCostume = COSTUME_SB_A;
    }
    if (!sbInfo)
    {
        sbInfo = xSTFindAsset(0x011A68D3, &sbMINFSize);
        gCurrentCostume = COSTUME_SB_B;
    }
    if (!sbInfo)
    {
        sbInfo = xSTFindAsset(0x67EEFE7C, &sbMINFSize);
        gCurrentCostume = COSTUME_SB_C;
    }
    if (!sbInfo)
    {
        sbInfo = xSTFindAsset(0xCEC39425, &sbMINFSize);
        gCurrentCostume = COSTUME_SB_D;
    }

    if (sbInfo)
    {
        sLassoInfo = &globals.player.lassoInfo;
        sLasso = &sLassoInfo->lasso;

        // not in the DWARF: saves and restores xModelInstStaticAlloc around the model allocation.
        S32 oldStaticAlloc = xModelInstStaticAlloc;
        xModelInstStaticAlloc = 1;
        globals.player.model_spongebob = zEntRecurseModelInfo(sbInfo, ent);
        xModelInstStaticAlloc = oldStaticAlloc;

        memset(globals.player.sb_models, 0, sizeof(globals.player.sb_models));

        U8 index = 0;
        for (xModelInstance* m = globals.player.model_spongebob; m != NULL; m = m->Next)
        {
            globals.player.sb_models[globals.player.sb_model_indices[index++]] = m;
        }

        if (globals.player.sb_models[10] != NULL)
        {
            globals.player.sb_models[10]->Parent = globals.player.model_spongebob;
            globals.player.sb_models[10]->Flags = 0x2000;
            globals.player.sb_models[10]->Mat = globals.player.model_spongebob->Mat;
        }

        globals.player.model_wand = globals.player.sb_models[5];
        iModelTagSetup(&globals.player.BubbleWandTag[0], globals.player.model_wand->Data, -0.946f,
                       0.471f, 0.1705f);
        iModelTagSetup(&globals.player.BubbleWandTag[1], globals.player.model_wand->Data, -0.682f,
                       0.471f, 0.302f);
        iModelTagSetup(&sSpongeBobLHand, globals.player.sb_models[0]->Data, 0.55f, 0.496f, 0.02f);
        iModelTagSetup(&sSpongeBobRHand, globals.player.sb_models[0]->Data, -0.55f, 0.496f,
                       -0.011f);
        iModelTagSetup(&sSpongeBobRElbow, globals.player.sb_models[0]->Data, -0.442f, 0.458f,
                       -0.007f);
        iModelTagSetup(&sSpongeBobLElbow, globals.player.sb_models[0]->Data, 0.43f, 0.458f,
                       -0.004f);
        iModelTagSetup(&sStankTag[0], xglobals->___player_ent_dont_use_directly->model->Data,
                       -0.014f, 0.546f, 0.168f);
    }

    sEmitSpinBubbles = zParEmitterFind("PAREMIT_GRAB_BUBBLES");
    sEmitSundae = zParEmitterFind("PAREMIT_CLOUD");
    sEmitStankBreath = zParEmitterFind("PAREMIT_STANK");
    gEmitBFX = zParEmitterFind("PAREMIT_BFX");
    sLassoInfo = &globals.player.lassoInfo;
    sLasso = &sLassoInfo->lasso;
    sModelUpdatedOnce = 0;

    F32 bbncvtm = globals.player.g.BBashTime - globals.player.g.BBashCVTime;
    bbash_vel =
        (globals.player.g.BBashHeight + 0.5f * globals.player.g.Gravity * bbncvtm * bbncvtm) /
        globals.player.g.BBashTime;

    // Restores animation data swapped by the BfBB-era "dry SpongeBob" code; nothing in TSSM fills
    // these tables any more, but retail still runs the loop.
    for (S32 i = 0; i < drybob_anim_count; i++)
    {
        *drybob_chgData[i] = drybob_oldData[i];
        *drybob_chgTime[i] = drybob_oldTime[i];
    }

    drybob_anim_count = 0;

    cruise_bubble::init();
    bungee_state::init();

    sNumHitches = 0;
    sHitchAngle = 0.0f;
    U32 trailerHash = xStrHash("trailer_hitch");

    for (S32 i = 0; i < globals.sceneCur->num_base; i++)
    {
        xEnt* hitch = (xEnt*)globals.sceneCur->base[i];
        if ((hitch->baseType == eBaseTypeDestructObj || hitch->baseType == eBaseTypePlatform ||
             hitch->baseType == eBaseTypeStatic) &&
            hitch->asset->modelInfoID == trailerHash)
        {
            sHitch[sNumHitches] = hitch;
            sNumHitches++;
        }
    }

    zEntPlayerSlideFXInit();
    zEntPlayerCommonInit(ent, asset);

    zEntPlayerFX::SuperMachoBowlExplosion::Init();

    gBungeeDeath = 0;
    globals.player.s = &globals.player.sb;
}

void zEntPlayer_PatrickInit(xEnt* ent, xEntAsset* asset, S32)
{
    U32 bufsize;
    void* info;

    info = xSTFindAsset(0x791025AC, &bufsize);
    if (!info)
    {
        info = xSTFindAsset(0xC7225C34, &bufsize);
    }
    if (!info)
    {
        info = xSTFindAsset(0x2DF6F1DD, &bufsize);
    }
    if (!info)
    {
        info = xSTFindAsset(0x94CB8786, &bufsize);
    }
    if (!info)
    {
        info = xSTFindAsset(0xFBA01D2F, &bufsize);
    }
    if (!info)
    {
        info = xSTFindAsset(0x6274B2D8, &bufsize);
    }
    if (!info)
    {
        info = xSTFindAsset(0xC9494881, &bufsize);
    }
    if (!info)
    {
        info = xSTFindAsset(0x301DDE2A, &bufsize);
    }
    if (!info)
    {
        info = xSTFindAsset(0x346BB6C4, &bufsize);
    }
    if (!info)
    {
        info = xSTFindAsset(0x9B404C6D, &bufsize);
    }
    if (!info)
    {
        info = xSTFindAsset(0x0214E216, &bufsize);
    }
    if (!info)
    {
        info = xSTFindAsset(0x68E977BF, &bufsize);
    }

    if (info)
    {
        sLassoInfo = &globals.player.lassoInfo;
        sLasso = &sLassoInfo->lasso;

        // not in the DWARF: oldStaticAlloc and model. Retail restores xModelInstStaticAlloc
        // before storing the new model instance.
        S32 oldStaticAlloc = xModelInstStaticAlloc;
        xModelInstStaticAlloc = 1;
        xModelInstance* model = zEntRecurseModelInfo(info, ent);
        xModelInstStaticAlloc = oldStaticAlloc;
        globals.player.model_patrick = model;

        zLasso_Init(sLasso, globals.player.model_patrick, -0.599f, 0.645f, 0.051f);
        zPatTongue_Init(sLasso, globals.player.model_patrick);

        iModelTagSetup(&sPatrickBelly, globals.player.model_patrick->Data, 0.0f, 0.475f, 0.252f);
        iModelTagSetup(&sPatrickLFoot, globals.player.model_patrick->Data, 0.187f, 0.0f, -0.068f);
        iModelTagSetup(&sPatrickRFoot, globals.player.model_patrick->Data, -0.187f, 0.0f, -0.068f);
        iModelTagSetup(&sPatrickLKnee, globals.player.model_patrick->Data, 0.19f, 0.099f, -0.138f);
        iModelTagSetup(&sPatrickRKnee, globals.player.model_patrick->Data, -0.19f, 0.099f, -0.138f);
        iModelTagSetup(&sPatrickRHand, globals.player.model_patrick->Data, -0.632f, 0.711f,
                       -0.235f);
        iModelTagSetup(&sPatrickLHand, globals.player.model_patrick->Data, 0.684f, 0.694f, -0.215f);
        iModelTagSetup(&sPatrickRElbow, globals.player.model_patrick->Data, -0.475f, 0.733f,
                       -0.269f);
        iModelTagSetup(&sPatrickLElbow, globals.player.model_patrick->Data, 0.475f, 0.733f,
                       -0.269f);
        iModelTagSetup(&sPatrickHead, globals.player.model_patrick->Data, 0.0f, 1.517f, 0.23f);
    }
    else
    {
        globals.player.model_patrick = NULL;
    }

    zEntPlayerCommonInit(ent, asset);

    stun_rumble[0][0] = zSceneFindObject(xStrHash("PAT_STUN_SMALL"));
    stun_rumble[0][1] = zSceneFindObject(xStrHash("PAT_STUN_MEDIUM"));
    stun_rumble[0][2] = zSceneFindObject(xStrHash("PAT_STUN_LARGE"));
    stun_rumble[0][3] = zSceneFindObject(xStrHash("PAT_STUN_LUDICROUS"));
    stun_rumble[1][0] = zSceneFindObject(xStrHash("PAT_STUN_SMALL_SUPER"));
    stun_rumble[1][1] = zSceneFindObject(xStrHash("PAT_STUN_MEDIUM_SUPER"));
    stun_rumble[1][2] = zSceneFindObject(xStrHash("PAT_STUN_LARGE_SUPER"));
    stun_rumble[1][3] = zSceneFindObject(xStrHash("PAT_STUN_LUDICROUS_SUPER"));

    zEntPlayerSlideFXInit();
    zEntPlayerFX::CartShield::Init();

    globals.player.s = &globals.player.patrick;
}

void zEntPlayer_CarInit(xEnt* ent, xEntAsset* asset)
{
    void* info;
    U32 bufsize;

    info = xSTFindAsset(0xBFDDF49F, &bufsize);
    if (info)
    {
        // not in the DWARF: saves and restores xModelInstStaticAlloc around the model allocation.
        S32 oldStaticAlloc = xModelInstStaticAlloc;
        xModelInstStaticAlloc = 1;
        globals.player.model_car = zEntRecurseModelInfo(info, ent);
        xModelInstStaticAlloc = oldStaticAlloc;

        sLassoInfo = &globals.player.lassoInfo;
        sLasso = &sLassoInfo->lasso;
    }
    else
    {
        globals.player.model_car = NULL;
    }

    zEntPlayerCarInit(ent);

    zEntPlayerCommonInit(ent, asset);
}

void zEntPlayer_SpongePatInit(xEnt* ent, xEntAsset* asset)
{
    void* info;
    U32 bufsize;

    info = xSTFindAsset(0xB8F67E08, &bufsize);
    if (!info)
    {
        info = xSTFindAsset(0x1FCB13B1, &bufsize);
    }
    if (!info)
    {
        info = xSTFindAsset(0x869FA95A, &bufsize);
    }

    if (info)
    {
        // not in the DWARF: saves and restores xModelInstStaticAlloc around the model allocation.
        S32 oldStaticAlloc = xModelInstStaticAlloc;
        xModelInstStaticAlloc = 1;
        globals.player.model_spongepat = zEntRecurseModelInfo(info, ent);
        xModelInstStaticAlloc = oldStaticAlloc;

        sLassoInfo = &globals.player.lassoInfo;
        sLasso = &sLassoInfo->lasso;
    }
    else
    {
        globals.player.model_spongepat = NULL;
    }

    zEntPlayerSlideFXInit();

    // retail stores the settings pointer twice (two identical stores in the GC target)
    globals.player.s = &globals.player.spongepat;
    globals.player.s = &globals.player.spongepat;

    zEntPlayerCommonInit(ent, asset);
}

void zEntPlayer_SpongeBallInit(xEnt* ent, xEntAsset* asset)
{
    zEntPlayerCommonInit(ent, asset);

    boulderVehicle = NULL;
    boulderVehicle = (xEntBoulder*)zSceneFindObject(xStrHash("BOULDER_VEHICLE"));
    if (boulderVehicle)
    {
        boulderVehicle->eventFunc = BoulderVEventCB;
        sLassoInfo = &globals.player.lassoInfo;
        sLasso = &sLassoInfo->lasso;
    }

    globals.player.s = &globals.player.sb;
}

// No GC symbol: inlined into zEntPlayer_StreakFX (Bubble Bowl windup ribbon); the plain-curve
// sibling of zEntPlayerFX::Ribbon::EnableRibbonKarateSpin.
static inline void EnableRibbonBowlWindup(xVec3& pos)
{
    xVec3 normal;

    zEntPlayerFX::Ribbon::RibbonConfigs[0][1].curve = zEntPlayerFX::Ribbon::PlayerRibbonCurve;
    zEntPlayerFX::Ribbon::RibbonConfigs[0][1].curve_size = 2;

    normal = *xEntGetPos(xglobals->___player_ent_dont_use_directly) - pos;
    zEntPlayerFX::Ribbon::SetConfig(0, 1);
    zEntPlayerFX::Ribbon::PlayerRibbons[0][1].insert(pos, normal, 1.0f, 1.0f, 0);
}

static void zEntPlayer_StreakFX(xEnt* ent, F32)
{
    S32 i;
    S32 p;
    S32 cp;

    for (i = 0; i < 3; i++)
    {
        for (p = 0; p < 4; p++)
        {
            sStreakInfo[i][p].activated = FALSE;
        }
    }

    cp = 3;
    if (gCurrentPlayer == eCurrentPlayerSpongeBob)
    {
        cp = 0;
    }
    else if (gCurrentPlayer == eCurrentPlayerPatrick)
    {
        cp = 1;
    }
    else if (gCurrentPlayer == eCurrentPlayerSpongePat)
    {
        cp = 2;
    }

    if (cp == 3 || cp == 2)
    {
        return;
    }

    if (globals.player.SlideTrackSliding & 0x1)
    {
        sStreakInfo[cp][2].activated = TRUE;
        sStreakInfo[cp][3].activated = TRUE;
    }
    else if (cp == 0 && (strcmp(ent->model->Anim->Single->State->Name, "TongueJump01") == 0 ||
                         strcmp(ent->model->Anim->Single->State->Name, "TongueJumpXtra01") == 0 ||
                         strcmp(ent->model->Anim->Single->State->Name, "TongueDJumpApex01") == 0))
    {
        sStreakInfo[cp][2].activated = TRUE;
        sStreakInfo[cp][3].activated = TRUE;
    }

    if (globals.player.Jump_Springboard != NULL)
    {
        sStreakInfo[cp][0].activated = TRUE;
        sStreakInfo[cp][1].activated = TRUE;
    }

    if (cp == 0)
    {
        if (strcmp(ent->model->Anim->Single->State->Name, "BbowlWindup01") == 0)
        {
            xVec3 rhand;

            iModelTagEval(ent->model->Data, sStreakInfo[cp][1].tagA, ent->model->Mat, &rhand);
            EnableRibbonBowlWindup(rhand);
        }

        if (globals.player.IsBubbleSpinning)
        {
            xVec3 rhand;
            xVec3 lhand;

            iModelTagEval(ent->model->Data, sStreakInfo[cp][1].tagA, ent->model->Mat, &rhand);
            iModelTagEval(ent->model->Data, sStreakInfo[cp][0].tagA, ent->model->Mat, &lhand);
        }

        if (strcmp(ent->model->Anim->Single->State->Name, "BbashStart01") == 0 ||
            strcmp(ent->model->Anim->Single->State->Name, "BbashStart02") == 0)
        {
            zEntPlayerFX::MotionBlur::Start();
        }
        else if (strcmp(ent->model->Anim->Single->State->Name, "Land01") == 0 ||
                 strcmp(ent->model->Anim->Single->State->Name, "LandRun01") == 0)
        {
            zEntPlayerFX::MotionBlur::Blurring = false;
            zEntPlayerFX::MotionBlur::EntToTrack = NULL;
        }
    }
    else if (cp == 1)
    {
        if (strcmp(ent->model->Anim->Single->State->Name, "StunJump") == 0 ||
            strcmp(ent->model->Anim->Single->State->Name, "StunJump02") == 0)
        {
            zEntPlayerFX::MotionBlur::Start();
        }
        else if (strcmp(ent->model->Anim->Single->State->Name, "StunLand") == 0 ||
                 strcmp(ent->model->Anim->Single->State->Name, "StunLand02") == 0)
        {
            zEntPlayerFX::MotionBlur::Blurring = false;
            zEntPlayerFX::MotionBlur::EntToTrack = NULL;
        }

        if (strcmp(ent->model->Anim->Single->State->Name, "Stunfall") == 0 ||
            strcmp(ent->model->Anim->Single->State->Name, "Stunfall02") == 0 ||
            strcmp(ent->model->Anim->Single->State->Name, "Stunjump") == 0 ||
            strcmp(ent->model->Anim->Single->State->Name, "Stunjump02") == 0 ||
            strcmp(ent->model->Anim->Single->State->Name, "Cartwheel01") == 0)
        {
            xVec3 rhand;
            xVec3 lhand;
            xVec3 rfoot;
            xVec3 lfoot;
            xVec3 head;

            iModelTagEval(ent->model->Data, sStreakInfo[cp][1].tagA, ent->model->Mat, &rhand);
            iModelTagEval(ent->model->Data, sStreakInfo[cp][0].tagA, ent->model->Mat, &lhand);
            iModelTagEval(ent->model->Data, sStreakInfo[cp][3].tagA, ent->model->Mat, &rfoot);
            iModelTagEval(ent->model->Data, sStreakInfo[cp][2].tagA, ent->model->Mat, &lfoot);
            iModelTagEval(ent->model->Data, &sPatrickHead, ent->model->Mat, &head);
            zFX_SpawnBubbleTrail(&head, 1);
        }

        if (globals.player.IsBubbleSpinning)
        {
            xVec3 rhand;
            xVec3 lhand;
            xVec3 rfoot;
            xVec3 lfoot;

            iModelTagEval(ent->model->Data, sStreakInfo[cp][1].tagA, ent->model->Mat, &rhand);
            iModelTagEval(ent->model->Data, sStreakInfo[cp][0].tagA, ent->model->Mat, &lhand);
            iModelTagEval(ent->model->Data, sStreakInfo[cp][3].tagA, ent->model->Mat, &rfoot);
            iModelTagEval(ent->model->Data, sStreakInfo[cp][2].tagA, ent->model->Mat, &lfoot);
        }
    }

    for (i = 0; i < 3; i++)
    {
        for (p = 0; p < 4; p++)
        {
            if (sStreakInfo[i][p].activated && sStreakInfo[i][p].streakID == 0xDEAD)
            {
                sStreakInfo[i][p].streakID =
                    xFXStreakStart(0.0f, 4.0f, sStreakInfo[i][p].alphaStart, 0,
                                   &sStreakInfo[i][p].colA, &sStreakInfo[i][p].colB,
                                   sStreakInfo[i][p].streakTaper);
            }
            else if (!sStreakInfo[i][p].activated && sStreakInfo[i][p].streakID != 0xDEAD)
            {
                xFXStreakStop(sStreakInfo[i][p].streakID);
                sStreakInfo[i][p].streakID = 0xDEAD;
            }

            if (sStreakInfo[i][p].streakID != 0xDEAD)
            {
                if (i == 0)
                {
                    iModelTagEval(ent->model->Data, sStreakInfo[i][p].tagA,
                                  globals.player.model_spongebob->Mat, &sStreakInfo[i][p].a);
                    iModelTagEval(ent->model->Data, sStreakInfo[i][p].tagB,
                                  globals.player.model_spongebob->Mat, &sStreakInfo[i][p].b);
                }
                else
                {
                    iModelTagEval(ent->model->Data, sStreakInfo[i][p].tagA, ent->model->Mat,
                                  &sStreakInfo[i][p].a);
                    iModelTagEval(ent->model->Data, sStreakInfo[i][p].tagB, ent->model->Mat,
                                  &sStreakInfo[i][p].b);
                }

                xFXStreakUpdate(sStreakInfo[i][p].streakID, &sStreakInfo[i][p].a,
                                &sStreakInfo[i][p].b);
            }
        }
    }
}

static void zEntPlayer_SpringboardFX(xEnt* ent, F32 dt)
{
    // not in the DWARF: xVec3Copy/xVec3Add need addressable vectors; probably locals of an inlined
    // helper.
    xVec3 temp1;
    xVec3 temp2;
    xParEmitterCustomSettings info;

    if (globals.player.Jump_Springboard != NULL && ent->frame->vel.y >= 0.0f)
    {
        xVec3Copy(&temp1, (xVec3*)&ent->model->Mat->pos);
        temp1.y += 1.5f;

        xVec3Copy(&temp2, (xVec3*)&ent->model->Mat->right);

        temp2 *= 0.15f;
        xVec3Add(&temp1, &temp1, &temp2);

        if (gPTankDisable)
        {
            static F32 sLastSpringboardBubbleEmit = 0.0f;
            sLastSpringboardBubbleEmit += dt;

            if (!(sLastSpringboardBubbleEmit > 0.02f))
            {
                return;
            }

            sLastSpringboardBubbleEmit = 0.0f;

            info.custom_flags = 0x35E;
            info.pos = temp1;
            info.vel.x = 1.0f;
            info.vel.z = 0.0f;
            info.vel.y = 0.0f;
            info.vel_angle_variation = 2 * PI;
            info.rate.set(100.0f, 100.0f, 1.0f, 0);
            info.life.set(1.0f, 1.0f, 1.0f, 0);

            F32 size = 0.15f * xurand() + 0.05f;

            info.size_birth.set(size, size, 1.0f, 0);

            size *= 1.2f;
            info.size_death.set(size, size, 1.0f, 0);

            xParEmitterEmitCustom(sEmitSpinBubbles, dt, &info);
        }
        else
        {
            zFX_SpawnBubbleTrail(
                (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos, 3);
        }
    }
}

// No GC symbol: inlined into its only caller, zEntPlayer_BoulderVehicleUpdate.
static void getPadDefl(_tagPadAnalog* stick, xVec2* v)
{
    v->x = 0.0f;
    v->y = 0.0f;

    if (stick->x > 45.0f || stick->x < -45.0f)
    {
        if (stick->x > 0.0f)
        {
            v->x = 1.0f;
        }
        else
        {
            v->x = -1.0f;
        }

        if (stick->x < globals.player.g.AnalogMax && stick->x > 0)
        {
            v->x = (stick->x - 45.0f) / (127.0f - globals.player.g.AnalogMin);
        }
        else if (stick->x > -127.0f && stick->x < 0)
        {
            v->x = (45.0f + stick->x) / (127.0f - globals.player.g.AnalogMin);
        }
    }

    if (stick->y > 45.0f || stick->y < -45.0f)
    {
        if (stick->y > 0.0f)
        {
            v->y = 1.0f;
        }
        else
        {
            v->y = -1.0f;
        }

        if (stick->y < 127.0f && stick->y > 0)
        {
            v->y = (stick->y - 45.0f) / 82.0f;
        }
        else if (stick->y > -127.0f && stick->y < 0)
        {
            v->y = (45.0f + stick->y) / 82.0f;
        }
    }
}

static void BoulderVEventCB(xBase*, xBase*, U32, const F32*, xBase*, U32)
{
}

static void zEntPlayer_BoulderVehicleRender(zEnt*)
{
    xShadow_ListAdd(boulderVehicle);
}

static void zEntPlayer_BoulderVehicleMove(xEnt*, xScene*, F32, xEntFrame* frame)
{
    frame->mode = 0x30000;
}

static void zEntPlayer_BoulderVehicleUpdate(xEnt* ent, xScene* sc, F32 dt)
{
    if (dt == 0.0f)
    {
        return;
    }

    xEntBoulder* shouldDamagePlayer = NULL;
    gSpongeBall = TRUE;
    zEntPlayer_PredictionUpdate(ent, dt);
    xVec3Copy(&ent->frame->oldmat.pos, &ent->frame->mat.pos);

    U8 chkBackup = ent->collis->chk;
    U8 penBackup = ent->collis->pen;
    ent->collis->chk = 0;
    ent->collis->pen = 0;

    if (oob_state::update(*sc, dt))
    {
        return;
    }

    if (boulderVehicle && !globals.player.ControlOff)
    {
        bvTimeToIdle -= dt;

        xVec2 inputDefl;
        getPadDefl(&globals.pad0->analog1, &inputDefl);

        if (globals.pad0->on & XPAD_BUTTON_RIGHT)
        {
            inputDefl.x = 1.0f;
        }

        if (globals.pad0->on & XPAD_BUTTON_LEFT)
        {
            inputDefl.x = -1.0f;
        }

        if (globals.pad0->on & XPAD_BUTTON_DOWN)
        {
            inputDefl.y = 1.0f;
        }

        if (globals.pad0->on & XPAD_BUTTON_UP)
        {
            inputDefl.y = -1.0f;
        }

        sIdleCommentTimer.TimerTickElseReset(inputDefl.x == 0.0f && inputDefl.y == 0.0f, dt);
        sRollingCommentTimer.TimerTickElseReset(xVec3Length2(&boulderVehicle->vel) >= 25.0f, dt);

        xMat4x3* pCameraMat = &xglobals->cam->mat;

        // not in the DWARF: the flattened camera right/at directions
        xVec3 tempRight;
        xVec3Copy(&tempRight, &pCameraMat->right);
        tempRight.y = 0.0f;

        if (xVec3Length2(&tempRight) > 0.0001f)
        {
            xVec3SMulBy(&tempRight, 1.0f / xVec3Length(&tempRight));
        }
        else
        {
            xVec3Copy(&tempRight,
                      (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->right);
        }

        xVec3 tempAt;
        xVec3Copy(&tempAt, &pCameraMat->at);
        tempAt.y = 0.0f;

        if (xVec3Length2(&tempAt) > 0.0001f)
        {
            xVec3SMulBy(&tempAt, 1.0f / xVec3Length(&tempAt));
        }
        else
        {
            xVec3Copy(&tempAt, &pCameraMat->up);
        }

        xVec3AddScaled(&boulderVehicle->vel, &tempRight, 60.0f * (-0.3f * inputDefl.x) * dt);
        xVec3AddScaled(&boulderVehicle->vel, &tempAt, 60.0f * (-0.3f * inputDefl.y) * dt);

        if (globals.player.Visible)
        {
            xEntShow(boulderVehicle);
        }
        else
        {
            xEntHide(boulderVehicle);
        }

        xEntCollis collis = { 0 };

        boulderVehicle->collis = &collis;
        xEntBoulder_Update(boulderVehicle, sc, dt);

        U8 collisionWithPlat = FALSE;
        for (S32 i = collis.dyn_sidx; i < collis.dyn_eidx; i++)
        {
            if ((collis.colls[i].flags & 1) && collis.colls[i].optr)
            {
                xEnt* dyn = (xEnt*)collis.colls[i].optr;
                zPlatform* plat;
                xEntBoulder* boul;

                switch (dyn->baseType)
                {
                case eBaseTypeBoulder:
                    boul = (xEntBoulder*)dyn;

                    if (boul->basset->flags & 0x2)
                    {
                        shouldDamagePlayer = boul;
                    }
                    break;
                case eBaseTypePlatform:
                    plat = (zPlatform*)dyn;

                    if (plat->subType == ZPLATFORM_SUBTYPE_BREAKAWAY && plat->state == 0)
                    {
                        if (collisionWithPlat && sMountObject != plat)
                        {
                            zPlatform_Dismount((zPlatform*)sMountObject);
                        }

                        collisionWithPlat = TRUE;
                        sMountObject = plat;
                        zPlatform_Mount(plat);
                    }
                    break;
                }
            }
        }

        if (!collisionWithPlat && sMountObject)
        {
            zPlatform_Dismount((zPlatform*)sMountObject);
            sMountObject = NULL;
        }

        boulderVehicle->collis = NULL;

        // Retail (and the PS2 build) branches from here into the second tail, past its re-test.
        if (shouldDamagePlayer)
        {
            goto boulder_roll_continue;
        }

        if (xVec3Length2(&boulderVehicle->vel) > 4.0f)
        {
            bvTimeToIdle = 2.0f;
        }

        F32 padDefl = inputDefl.x * inputDefl.x + inputDefl.y * inputDefl.y;

        if (boulderVehicle->angVel > 4.0f && padDefl > 0.25f)
        {
            xVec3 rotFudge;
            xVec3Cross(&rotFudge, (xVec3*)&boulderVehicle->model->Mat->right,
                       &boulderVehicle->rotVec);

            F32 ang = xasin(xVec3Normalize(&rotFudge, &rotFudge));

            xMat3x3 rotM;
            xMat3x3Rot(&rotM, &rotFudge, 0.01f * ang);
            xMat3x3Mul((xMat3x3*)boulderVehicle->model->Mat, (xMat3x3*)boulderVehicle->model->Mat,
                       &rotM);
        }

        if (padDefl > 0.1f)
        {
            xVec3 heading;
            xVec3Init(&heading, -pCameraMat->right.z, 0.0f, pCameraMat->right.x);
            xVec3SMulBy(&heading, -1.0f * inputDefl.y);
            xVec3AddScaled(&heading, &pCameraMat->right, -1.0f * inputDefl.x);
            heading.y = 0.0f;

            if (xVec3Normalize(&heading, &heading) > 0.001f)
            {
                xVec3Copy((xVec3*)&ent->model->Mat->at, &heading);
                xVec3Init((xVec3*)&ent->model->Mat->up, 0.0f, 1.0f, 0.0f);
                xVec3Init((xVec3*)&ent->model->Mat->right, ent->model->Mat->at.z, 0.0f,
                          -ent->model->Mat->at.x);
            }
        }

        xVec3Copy((xVec3*)&ent->model->Mat->pos, &boulderVehicle->bound.sph.center);
        ent->model->Mat->pos.y -= ent->bound.sph.r;

        xCameraSetTargetMatrix(&globals.oldSkoolCamera,
                               xEntGetFrame(xglobals->___player_ent_dont_use_directly));
        xCameraSetTargetOMatrix(&globals.oldSkoolCamera,
                                xEntGetFrame(xglobals->___player_ent_dont_use_directly));

        ((zPlayer*)ent)->CollideTrigger(sc);

        F32 angle;
        F32 mag;
        DampenControls(&angle, &mag, -globals.pad0->analog1.x, -globals.pad0->analog1.y);

        if (mag)
        {
            if (mag > globals.player.s->MoveSpeed[3])
            {
                if (mag < globals.player.s->MoveSpeed[4])
                {
                    globals.player.Speed = 1;
                }
                else
                {
                    globals.player.Speed = 2;
                }
            }
        }
        else
        {
            globals.player.Speed = 0;
        }

        xVec3Copy(&ent->frame->mat.pos, (xVec3*)&ent->model->Mat->pos);
        ent->move = zEntPlayer_BoulderVehicleMove;

        if (ent->bupdate)
        {
            ent->bupdate(ent, (xVec3*)&ent->model->Mat->pos);
        }

        zCameraSetPlayerVel(&boulderVehicle->vel);
        sCameraLastMat = *(xMat4x3*)ent->model->Mat;

        if (xVec3Length2(&boulderVehicle->vel) > 81.0f)
        {
            if (!zEntPlayerFX::MotionBlur::Blurring)
            {
                zEntPlayerFX::MotionBlur::Blurring = true;
                zEntPlayerFX::MotionBlur::NormalBlur.activate(
                    0.3f, 1.0f, zEntPlayerFX::MotionBlur::UpgradeBlurConfig, ent->model);
                zEntPlayerFX::MotionBlur::EntToTrack = ent;
            }

            zEntPlayerFX::MotionBlur::Update(dt);
        }
        else
        {
            zEntPlayerFX::MotionBlur::Blurring = false;
            zEntPlayerFX::MotionBlur::EntToTrack = NULL;
        }
    }

    if (!boulderVehicle || globals.player.ControlOff)
    {
        ent->collis->chk = chkBackup;
        ent->collis->pen = penBackup;

        if (globals.player.ControlOff && globals.player.ControlOnEvent)
        {
            zEntPlayerControlOn(CONTROL_OWNER_EVENT);
            globals.player.ControlOnEvent = 0;
        }

        if (shouldDamagePlayer)
        {
            zEntPlayer_DamageNPCKnockBack(shouldDamagePlayer, 1,
                                          &shouldDamagePlayer->bound.sph.center,
                                          ePlayerNMEDamageDefault);
        }
    }
    else
    {
    boulder_roll_continue:
        ent->collis->chk = chkBackup;
        ent->collis->pen = penBackup;

        if (shouldDamagePlayer)
        {
            boulderRollShouldEnd = TRUE;
        }

        if (BoulderRollDoneCheck())
        {
            BoulderRollDoneCB();
            zEntPlayer_Update(ent, sc, dt);

            if (shouldDamagePlayer)
            {
                zEntPlayer_DamageNPCKnockBack(shouldDamagePlayer, 1,
                                              &shouldDamagePlayer->bound.sph.center,
                                              ePlayerNMEDamageDefault);
            }
        }
        else
        {
            zEntPickup_CheckAllPickupsAgainstPlayer(sc, dt);
        }
    }
}

static void zEntPlayer_PredictionUpdate(xEnt* ent, F32 dt)
{
    zPlayerGlobals* g = &globals.player;
    // not in the DWARF: the previous direction must be kept before PredictCurrDir is updated.
    xVec3 lastDir;
    F32 lastVel;

    xVec3Copy(&lastDir, &g->PredictCurrDir);
    lastVel = g->PredictCurrVel;

    xVec3Sub(&g->PredictCurrDir, &ent->frame->mat.pos, &ent->frame->oldmat.pos);
    xVec3SMulBy(&g->PredictCurrDir, 1.0f / last_update_dt);
    g->PredictCurrDir.y = 0.0f;

    g->PredictCurrVel = xVec3Length(&g->PredictCurrDir);

    if (g->PredictCurrVel > 0.05f)
    {
        xVec3SMulBy(&g->PredictCurrDir, 1.0f / g->PredictCurrVel);
    }
    else
    {
        xVec3Copy(&g->PredictCurrDir, (xVec3*)&ent->model->Mat->at);
    }

    g->PredictCurrVel = 0.9f * lastVel + (1.0f - 0.9f) * g->PredictCurrVel;

    if (g->PredictCurrVel > 0.05f)
    {
        xVec3 cross;
        F32 newAngV;

        xVec3Cross(&cross, &lastDir, &g->PredictCurrDir);
        newAngV = xasin(cross.y);

        if (xVec3Dot(&lastDir, &g->PredictCurrDir) < 0.0f)
        {
            if (newAngV > 0.0f)
            {
                newAngV = PI - newAngV;
            }
            else
            {
                newAngV = -PI - newAngV;
            }
        }

        newAngV /= dt;
        g->PredictAngV = 0.9f * g->PredictAngV + (1.0f - 0.9f) * newAngV;

        if (g->PredictAngV > 0.05f || g->PredictAngV < -0.05f)
        {
            F32 r = g->PredictCurrVel / g->PredictAngV;

            xVec3Init(&g->PredictRotate, -g->PredictCurrDir.z, 0.0f, g->PredictCurrDir.x);
            xVec3SMulBy(&g->PredictRotate, r);
            xVec3Sub(&g->PredictTranslate, (xVec3*)&ent->model->Mat->pos, &g->PredictRotate);
        }
    }
    else
    {
        g->PredictAngV = 0.0f;
    }
}

void zEntPlayer_PredictPos(xVec3* pos, F32 timeIntoFuture, F32 leadFactor, S32 useTurn)
{
    zPlayerGlobals* g = &globals.player;
    F32 useVel;
    F32 useAngV;
    xMat3x3 rotMat;

    useVel = g->PredictCurrVel * leadFactor;
    useAngV = g->PredictAngV;

    if (useTurn != 0 && (useAngV > 0.05f || useAngV < -0.05f))
    {
        xMat3x3RotY(&rotMat, leadFactor * (timeIntoFuture * useAngV));
        xMat3x3RMulVec(pos, &rotMat, &g->PredictRotate);
        xVec3AddTo(pos, &g->PredictTranslate);
    }
    else
    {
        xVec3Copy(pos, (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos);
        if (useVel > 0.05f)
        {
            xVec3AddScaled(pos, &g->PredictCurrDir, timeIntoFuture * useVel);
        }
    }
}

static S32 zEntPlayerKnockToSafety(xEnt* ent)
{
    F32 diffX;
    F32 diffY;
    F32 diffZ;
    F32 popheight;
    F32 ttot;
    F32 velXZ;

    if (globals.player.s->Health == 0)
    {
        return 0;
    }

    diffZ = floor_safe_vec.z - ent->model->Mat->pos.z;
    diffX = floor_safe_vec.x - ent->model->Mat->pos.x;
    diffY = floor_safe_vec.y - ent->model->Mat->pos.y;
    velXZ = xsqrt(diffX * diffX + diffZ * diffZ);
    if (diffY < -3.0f || diffY > 5.0f || velXZ > 9.0f)
    {
        return 0;
    }

    ttot = MAX(ent->model->Mat->pos.y, floor_safe_vec.y) + 2.65f;
    diffY = xsqrt(2.0f * (ttot - ent->model->Mat->pos.y) / globals.player.g.Gravity);
    popheight = xsqrt(2.0f * (ttot - floor_safe_vec.y) / globals.player.g.Gravity);
    ttot = diffY + popheight;
    popheight = velXZ / ttot;
    ent->frame->vel.y = diffY * globals.player.g.Gravity;
    if (velXZ < 1e-5f)
    {
        ent->frame->vel.x = 0.0f;
        ent->frame->vel.z = 0.0f;
    }
    else
    {
        velXZ = popheight / velXZ;
        ent->frame->vel.x = diffX * velXZ;
        ent->frame->vel.z = diffZ * velXZ;
    }
    globals.player.KnockBackTimer = ttot;
    globals.player.KnockIntoAirTimer = 0.0f;
    return 1;
}

// Always inlined, no symbol; the name is a guess.
static inline S32 zEntPlayerBowlTarget_Exists(xEnt* ent)
{
    for (zEntPlayerBowlTarget* t = sBowlTargetTail; t; t = t->next)
    {
        if (t->ent == ent)
        {
            return 1;
        }
    }

    return 0;
}

static xEnt* zEntPlayer_FindGrabEnt(xEnt* ent, zScene* zsc, S32* failed)
{
    S32 i;
    xEnt* grabent;
    F32 dx;
    F32 dy;
    F32 dz;

    if (zEntPlayer_PowerupLevel(PLYR_ATAK_PAT_THROW) < ePowerupLevelNormal)
    {
        return NULL;
    }

    for (i = 0; i < zsc->num_base; i++)
    {
        grabent = (xEnt*)zsc->base[i];

        if (!(grabent->baseFlags & 0x20) || (grabent->flags & 0x85) != 0x1 || !grabent->model ||
            !(grabent->chkby & 0x10))
        {
            continue;
        }

        if (grabent->baseType != eBaseTypeBoulder)
        {
            dx = grabent->model->Mat->pos.x - ent->model->Mat->pos.x;
            dy = grabent->model->Mat->pos.y - ent->model->Mat->pos.y;
            dz = grabent->model->Mat->pos.z - ent->model->Mat->pos.z;
        }
        else
        {
            xEntBoulder* boul = (xEntBoulder*)grabent;

            dy = (boul->bound.sph.center.y - boul->bound.sph.r) - ent->model->Mat->pos.y;
            dx = boul->bound.sph.center.x - ent->model->Mat->pos.x;
            dz = boul->bound.sph.center.z - ent->model->Mat->pos.z;
        }

        if (dx * dx + dz * dz >= globals.player.carry.maxDist * globals.player.carry.maxDist ||
            dy >= globals.player.carry.maxHeight || dy <= globals.player.carry.minHeight ||
            dx * dx + dz * dz <= globals.player.carry.minDist * globals.player.carry.minDist)
        {
            continue;
        }

        if ((dx * ent->model->Mat->at.x + dz * ent->model->Mat->at.z) / xsqrt(dx * dx + dz * dz) <
            globals.player.carry.maxCosAngle)
        {
            continue;
        }

        if (zThrown_IsFruit(grabent, NULL) && grabent->model->Scale.x)
        {
            continue;
        }

        if (zThrown_IsStacked(grabent))
        {
            continue;
        }

        if (grabent->baseType == eBaseTypeNPC &&
            !((zNMECommon*)grabent)->SetCarryState(zNMECARRY_ATTEMPTPICKUP))
        {
            continue;
        }

        if ((!(grabent->moreFlags & 0x8) && grabent->baseType != eBaseTypeNPC) ||
            (grabent->baseType != eBaseTypeStatic && grabent->baseType != eBaseTypeNPC &&
             grabent->baseType != eBaseTypeBoulder && grabent->baseType != eBaseTypeDestructObj))
        {
            if (failed)
            {
                *failed = 1;
            }

            continue;
        }

        if (failed)
        {
            *failed = 0;
        }

        return grabent;
    }

    return NULL;
}

static void StartThrow()
{
    if (globals.player.carry.grabbed && globals.player.carry.grabbed->baseType == eBaseTypeNPC)
    {
        zNMECommon* npc = (zNMECommon*)globals.player.carry.grabbed;
        npc->SetCarryState(zNMECARRY_THROW);
    }

    if (globals.player.carry.throwTarget)
    {
        xMat3x3 targetingMat;
        xMat3x3Identity(&targetingMat);
        xMat3x3RotY(&targetingMat, sThrowAngle);

        if (zThrown_IsFruit(globals.player.carry.grabbed, NULL) &&
            zThrown_IsFruit(globals.player.carry.throwTarget, NULL))
        {
            zThrown_LaunchStack(globals.player.carry.grabbed, globals.player.carry.throwTarget);
        }
        else if (globals.player.carry.throwTarget->baseType == eBaseTypePlatform &&
                 (((zPlatform*)globals.player.carry.throwTarget)->plat_flags & 0x2))
        {
            xVec3 tgtpos;
            xVec3Init(&tgtpos, 0.0f, 1.229f, -2.0f);
            xMat4x3Toworld(&tgtpos, (xMat4x3*)globals.player.carry.throwTarget->model->Mat,
                           &tgtpos);

            if (zThrown_LaunchPos(globals.player.carry.grabbed, &tgtpos, &targetingMat.at))
            {
                globals.player.carry.flyingToTarget = globals.player.carry.throwTarget;
            }

            zThrown_PatrickLauncher(globals.player.carry.grabbed, globals.player.carry.throwTarget);
        }
        else
        {
            xVec3 tempPos;
            xMat4x3* tgtmat = (xMat4x3*)globals.player.carry.throwTarget->model->Mat;
            // Not in the TSSM PS2 DWARF (BfBB's lists it); retail adds it as the second operand.
            F32 throwHeightOffset = 1.12f;

            if (globals.player.carry.throwTarget->baseType == eBaseTypeButton)
            {
                F32 cDist =
                    xsqrt(SQR(tgtmat->pos.x -
                              xglobals->___player_ent_dont_use_directly->model->Mat->pos.x) +
                          SQR(tgtmat->pos.z -
                              xglobals->___player_ent_dont_use_directly->model->Mat->pos.z));
                F32 min = globals.player.carry.throwMinDist;
                F32 max = globals.player.carry.throwMaxDist - 5.0f;
                F32 cButtonOutTgtMult = 0.9f;

                if (cDist < min)
                {
                    cDist = min;
                }
                if (cDist > max)
                {
                    cDist = max;
                }

                cDist = (cDist - min) / (max - min);
                cDist *= cButtonOutTgtMult;
                tempPos.x = tgtmat->pos.x + tgtmat->up.x * cDist;
                tempPos.y = tgtmat->pos.y + tgtmat->up.y * cDist - 0.5f;
                tempPos.z = tgtmat->pos.z + tgtmat->up.z * cDist;
            }
            else
            {
                tempPos.x = tgtmat->pos.x;
                tempPos.y = tgtmat->pos.y + throwHeightOffset;
                tempPos.z = tgtmat->pos.z;
            }

            xVec3 dir;
            xVec3Sub(&dir, &tempPos,
                     (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos);
            xVec3Normalize(&dir, &dir);
            dir.y = 0.0f;

            if (zThrown_LaunchPos(globals.player.carry.grabbed, &tempPos, &dir))
            {
                globals.player.carry.flyingToTarget = globals.player.carry.throwTarget;
            }
            else
            {
                globals.player.carry.flyingToTarget = NULL;
            }
        }
    }
    else
    {
        xMat3x3 targetingMat;
        xMat3x3Identity(&targetingMat);
        xMat3x3RotY(&targetingMat, sThrowAngle);
        zThrown_LaunchDir(globals.player.carry.grabbed, &targetingMat.at);
        globals.player.carry.flyingToTarget = NULL;
    }

    globals.player.carry.grabbed = NULL;
    globals.player.carry.throwTarget = NULL;
}

// No GC symbol: auto-inlined into CheckNMEAgainstBubbleBowl (its only caller) and dead-stripped.
// The chained texture assignment keeps it under the inlining limit (the PS2 demo has three lines).
zEntPlayerBowlTarget* zEntPlayerBowlTarget_Add(xVec3* target)
{
    zEntPlayerBowlTarget* result = NULL;
    S32 i;

    if (sNumActiveBowlTargets == 16)
    {
        if (sBowlTargetTail->ent && sBowlTargetTail->ent->baseType == eBaseTypeNPC)
        {
            ((zNMECommon*)sBowlTargetTail->ent)
                ->Damage(DMGTYP_BBOWLZAP, xglobals->___player_ent_dont_use_directly, NULL, -1,
                         ePowerupLevelNormal);
        }

        zEntPlayerBowlTarget_Remove(sBowlTargetTail);
    }

    for (i = 0; i < 16; i++)
    {
        if (!zEntPlayerBowlTargetPool[i].ent)
        {
            result = &zEntPlayerBowlTargetPool[i];
            break;
        }
    }

    if (sBowlTargetHead)
    {
        sBowlTargetHead->next = result;
        result->prev = sBowlTargetHead;
    }
    sBowlTargetHead = result;
    result->next = NULL;

    if (sNumActiveBowlTargets == 0)
    {
        sBowlTargetTail = result;
    }
    sNumActiveBowlTargets++;

    sBowlLightningAddInfo.start = (xVec3*)&zEntPlayerGetBBowl()->model->Mat->pos;
    sBowlLightningAddInfo.end = target;
    sBowlLightningAddInfo.branchTexture = sBowlLightningAddInfo.mainTexture = xStrHash("lightning");
    sBowlLightningAddInfo.segmentsPerMeter = 3.0f;
    result->lightning = zLightningAdd(&sBowlLightningAddInfo);
    result->timer = 0.7f + xrand_RandomFloatRange(-0.2f, 0.2f);
    zEntPlayerFX::SuperMachoBowlLightning::TimeToFlash = 2.0f;
    zEntPlayerFX::SuperMachoBowlLightning::TargetCount++;

    return result;
}

// No GC symbol: auto-inlined into its callers and dead-stripped (PS2 DWARF: a plain function).
void zEntPlayerBowlTarget_Remove(zEntPlayerBowlTarget* target)
{
    if (target == sBowlTargetHead)
    {
        sBowlTargetHead = target->prev;
    }
    if (target == sBowlTargetTail)
    {
        sBowlTargetTail = target->next;
    }
    if (target->next)
    {
        target->next->prev = target->prev;
    }
    if (target->prev)
    {
        target->prev->next = target->next;
    }

    sNumActiveBowlTargets--;
    target->ent = NULL;
    target->timer = 0.0f;
    _zLightningKill(target->lightning);
    target->next = NULL;
    target->prev = NULL;
    zEntPlayerFX::SuperMachoBowlLightning::TargetCount--;
}

void zEntPlayerBowlTargetRemoveAll()
{
    zEntPlayerBowlTarget* curr = sBowlTargetTail;
    zEntPlayerBowlTarget* next;

    while (curr)
    {
        next = curr->next;

        if (curr->ent && curr->ent->baseType == eBaseTypeNPC)
        {
            ((zNMECommon*)curr->ent)
                ->Damage(DMGTYP_BBOWLZAP, xglobals->___player_ent_dont_use_directly, NULL, -1,
                         ePowerupLevelNormal);
        }

        zEntPlayerBowlTarget_Remove(curr);
        curr = next;
    }

    if (!zEntPlayerFX::SuperMachoBowlLightning::Exploding)
    {
        zEntPlayerFX::SuperMachoBowlLightning::Exploding = true;
        zEntPlayerFX::SuperMachoBowlLightning::TimeLeftForExplosion = 1.0f;
    }
}

void zEntPlayerBowlTargetUpdate(F32 dt)
{
    zEntPlayerBowlTarget* curr = sBowlTargetTail;
    zEntPlayerBowlTarget* next;
    xVec3* pos;

    while (curr)
    {
        next = curr->next;

        if (curr->timer > 0.0f)
        {
            curr->timer -= dt;
            if (curr->timer < 0.0f)
            {
                curr->timer = 0.0f;
            }

            if (curr->ent->baseType == eBaseTypeNPC)
            {
                pos = &curr->ent->bound.sph.center;
            }
            else
            {
                pos = (xVec3*)&curr->ent->model->Mat->pos;
            }

            zLightningModifyEndpoints(curr->lightning,
                                      (xVec3*)&zEntPlayerGetBBowl()->model->Mat->pos, pos);

            if (!xEntIsVisible(curr->ent))
            {
                zEntPlayerBowlTarget_Remove(curr);
            }
        }
        else
        {
            if (curr->ent && curr->ent->baseType == eBaseTypeNPC)
            {
                ((zNMECommon*)curr->ent)
                    ->Damage(DMGTYP_BBOWLZAP, xglobals->___player_ent_dont_use_directly, NULL, -1,
                             ePowerupLevelNormal);
            }

            zEntPlayerBowlTarget_Remove(curr);
        }

        curr = next;
    }

    if (globals.player.g.PowerUp[2] > 1 && zEntPlayerGetBBowl())
    {
        zEntPlayerFX::SuperMachoBowlLightning::Update(dt);
    }
}

void zEntPlayerExplodeGauntlet()
{
    if (sGauntletHaz)
    {
        if (sGauntletHaz->custdata.bang.state == 0)
        {
            sGauntletHaz->tmr_remain = 0.0f;
        }

        zEntPlayerFX::GauntletExplosion::Explode(sGauntletHaz->pos_hazard);
    }
}

void zEntPlayerExplodeHammerThrow(xVec3* pos)
{
    sThrowBlastRadius = 1.0f;
    xVec3Copy(&sThrowBlastPosition, pos);

    zNMEHazard* haz = HAZ_Acquire();
    if (haz)
    {
        if (!haz->ConfigHelper(HAZ_TYP_POWERUP_EXPLOSION_V1))
        {
            haz->Discard();
        }
        else
        {
            haz->Start(pos, -1.0f);
        }
    }

    zEntPlayerFX::HammerThrow::SetBlur(false);

    xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("TFRUIT_SPLODE_GRP")), 0, pos, NULL, NULL, NULL,
                NULL);
}

void zEntPlayerExplodeBowl()
{
    if (zEntPlayerGetBBowl() && globals.player.g.PowerUp[2] > 1 && sBowlBlastRadius == 0.0f)
    {
        sBowlBlastRadius = zEntPlayerGetBBowl()->bound.sph.r;
        zEntPlayerFX::SuperMachoBowlExplosion::Explode(zEntPlayerGetBBowl()->frame->mat.pos);
    }
}

void zEntPlayerWallJumpSpawnBubbleRing()
{
    xVec3 pos;
    xVec3 offset;
    xVec3Copy(&pos, &sWallContactPos);
    xVec3SMul(&offset, &sWallNormal, -0.18f);
    xVec3SubFrom(&pos, &offset);
    zFX_SpawnBubbleRing(&pos, 50, 3.5f, 3.4f, 2.2f, &sWallNormal);
}

static void zEntPlayer_Update_HitchSpinAndFade(F32 dt)
{
    xMat3x3 mat_rot;
    S32 i;

    sHitchAngle += 3.14f * dt;
    if (sHitchAngle > 2 * PI)
    {
        sHitchAngle -= 2 * PI;
    }

    xMat3x3RotY(&mat_rot, sHitchAngle);

    for (i = 0; i < sNumHitches; i++)
    {
        xEnt* hitch = sHitch[i];

        if (xEntIsVisible(hitch))
        {
            *(xMat3x3*)hitch->model->Mat = mat_rot;

            xVec3 vec_camToHitch =
                *(xVec3*)&hitch->model->Mat->pos - globals.oldSkoolCamera.mat.pos;

            if (xVec3Dot(&vec_camToHitch, &globals.oldSkoolCamera.mat.at) < 4.5f)
            {
                hitch->model->Alpha -= 3.0f * globals.update_dt;
            }
            else
            {
                hitch->model->Alpha += 3.0f * globals.update_dt;
            }

            hitch->model->Alpha = CLAMP(hitch->model->Alpha, 0.0f, 1.0f);
        }
    }
}

F32 sRingDelay;
static S32 g_doPlayerUpdateAgainDueToPlayerSwitch;

// Not in the PS2 demo: the GC player-switch cheat (d-pad selects the character). Plain inline:
// retail has it weak, emitted after zEntPlayer_Update, which calls it out of line.
inline void zEntPlayer_UpdateCharacterSwitchingInMidPlayerUpdate(xEnt* ent, xScene* sc, F32 dt)
{
    if (globals.player.g.CheatPlayerSwitch && !globals.player.carry.grabbed &&
        globals.player.SundaeTimer < 0.0f)
    {
        if (!globals.player.ControlOff && (globals.pad0->pressed & XPAD_BUTTON_UP))
        {
            gCurrentPlayer = eCurrentPlayerSpongeBob;
            boulderRollShouldEnd = 1;
            if (carCam && carCam->active())
            {
                zCamRemove(*carCam, true);
            }
        }
        else if (!globals.player.ControlOff && (globals.pad0->pressed & XPAD_BUTTON_LEFT) &&
                 globals.player.model_patrick)
        {
            gCurrentPlayer = eCurrentPlayerPatrick;
            boulderRollShouldEnd = 1;
            if (carCam && carCam->active())
            {
                zCamRemove(*carCam, true);
            }
        }
        else if (!globals.player.ControlOff && (globals.pad0->pressed & XPAD_BUTTON_RIGHT) &&
                 globals.player.model_spongepat)
        {
            gCurrentPlayer = eCurrentPlayerSpongePat;
            boulderRollShouldEnd = 1;
            if (carCam && carCam->active())
            {
                zCamRemove(*carCam, true);
            }
        }
        else if (!globals.player.ControlOff && (globals.pad0->pressed & XPAD_BUTTON_DOWN) &&
                 globals.player.model_car)
        {
            gCurrentPlayer = eCurrentPlayerCar;
            boulderRollShouldEnd = 1;
            if (carCam)
            {
                zCamAdd(*carCam, true);
            }
        }
    }

    if (gCurrentPlayer != lastgCurrentPlayer || boulderRollShouldStart)
    {
        switch (gCurrentPlayer)
        {
        case eCurrentPlayerSpongeBob:
            if (!boulderRollShouldStart && !gSpongeBall && globals.player.model_spongebob)
            {
                *globals.player.model_spongebob->Mat = *ent->model->Mat;
                if (ent->frame)
                {
                    ent->frame->mat = *(xMat4x3*)ent->model->Mat;
                    ent->frame->oldmat = *(xMat4x3*)ent->model->Mat;
                }
                ent->model = globals.player.model_spongebob;
                globals.player.s = &globals.player.sb;
                gReticleTarget[0] = NULL;
                gReticleTarget[1] = NULL;
                xModelUpdate(ent->model, dt);
                g_doPlayerUpdateAgainDueToPlayerSwitch = 1;
            }
            else if (BoulderRollCB(NULL, NULL, NULL))
            {
                gCurrentPlayer = lastgCurrentPlayer;
                if (gCurrentPlayer == eCurrentPlayerCar && carCam)
                {
                    zCamAdd(*carCam, true);
                }
            }
            break;
        case eCurrentPlayerPatrick:
            if (globals.player.model_patrick)
            {
                *globals.player.model_patrick->Mat = *ent->model->Mat;
                ent->model = globals.player.model_patrick;
                globals.player.s = &globals.player.patrick;
                gReticleTarget[0] = NULL;
                gReticleTarget[1] = NULL;
                globals.player.IsBubbleBowling = 0;
            }
            break;
        case eCurrentPlayerSpongePat:
            if (globals.player.model_spongepat)
            {
                *globals.player.model_spongepat->Mat = *ent->model->Mat;
                ent->model = globals.player.model_spongepat;
                globals.player.s = &globals.player.spongepat;
                gReticleTarget[0] = NULL;
                gReticleTarget[1] = NULL;
                globals.player.IsBubbleBowling = 0;
            }
            break;
        case eCurrentPlayerCar:
            if (globals.player.model_car)
            {
                *globals.player.model_car->Mat = *ent->model->Mat;
                ent->model = globals.player.model_car;
                globals.player.s = &globals.player.spongepat;
                gReticleTarget[0] = NULL;
                gReticleTarget[1] = NULL;
                globals.player.IsBubbleBowling = 0;
            }
            else
            {
                *globals.player.model_spongebob->Mat = *ent->model->Mat;
                ent->model = globals.player.model_spongebob;
                globals.player.s = &globals.player.sb;
                gReticleTarget[0] = NULL;
                gReticleTarget[1] = NULL;
                gCurrentPlayer = eCurrentPlayerSpongeBob;
            }
            break;
        }
    }

    lastgCurrentPlayer = gCurrentPlayer;
}

// Not in the PS2 demo. static inline: retail has it local but emitted with zEntPlayer_Update's
// out-of-line inlines.
static inline bool zEntPlayer_IsRunning(xEnt* ent)
{
    bool running = false;
    U32 animType = ent->model->Anim->Single->State->UserFlags & 0x1E;
    if (animType == 6 || animType == 8)
    {
        running = true;
    }
    return running;
}

// Placeholder for dead-stripped code (probably a GC-only debug teleport) whose function-local
// pos_putPlayerHere remains, unreferenced, in retail .rodata before zEntPlayer_Update's data.
static void __deadstripped_zEntPlayer5()
{
    static const xVec3 pos_putPlayerHere = { -108.1f, 1.4f, 30.4f };
}

static const U8 SBBBashBones[8] = { 22, 30, 38, 42 };
static const U8 SBBBounceBones[8] = { 22, 30, 38, 42 };

// The (S16) cast on the stuck chain's colls[5] term below is only there for the match (flags is
// U32): its extsh is folded away after register allocation, but it leaves retail's registers for
// that block.
void zEntPlayer_Update(xEnt* ent, xScene* sc, F32 dt)
{
    g_doPlayerUpdateAgainDueToPlayerSwitch = 0;

    xModelInstance* modelInst = ent->model;
    while (modelInst)
    {
        modelInst->LightKit = globals.player.pPlayerLightKit;
        modelInst = modelInst->Next;
    }

    if (gReticleTarget[0])
    {
        xprintf("Reticule Set (T)\n");
    }
    else if (gReticleTarget[1])
    {
        xprintf("Reticule Set (S)\n");
    }

    if (gCurrentPlayer == eCurrentPlayerPatrick && !globals.player.model_patrick)
    {
        gCurrentPlayer = eCurrentPlayerSpongeBob;
    }

    if (dt < 1e-5f)
    {
        return;
    }

    last_update_dt = update_dt;
    update_dt = dt;

    zEntPlayerSlideFXUpdate(dt);

    if (globals.player.PlayerIsFrozen)
    {
        return;
    }

    gSpongeBall = 0;

    if (!sLassoInfo->swingTarget)
    {
        zEntPlayer_UpdateVelocityBlur();
    }

    zEntPlayer_PredictionUpdate(ent, dt);
    zEntPlayerEGenUpdate(ent, sc, dt);

    for (S32 i = 0; i < 2; i++)
    {
        if (gReticleTarget[i])
        {
            sReticleRot[i] += 8.0f * dt;
            if (sReticleRot[i] > 2.0f * PI)
            {
                sReticleRot[i] -= 2.0f * PI;
            }

            sReticleAlpha[i] += 3.0f * dt;
            if (sReticleAlpha[i] > 1.0f)
            {
                sReticleAlpha[i] = 1.0f;
            }
        }
    }

    if (globals.player.ControlOff || !globals.player.s->Health || in_goo)
    {
        if (zEntPlayerGetBBowl() && zEntPlayerGetBBowl()->update)
        {
            zEntEvent(xglobals->___player_ent_dont_use_directly, zEntPlayerGetBBowl(), eEventKill);
            zEntPlayerFX::SuperMachoBowlLightning::RemoveAll();
        }
    }

    if (gCurrentPlayer == eCurrentPlayerSpongeBob && bungee_state::update(sc, dt))
    {
        if (globals.player.sb_models[5]->Flags & 1)
        {
            zEntPlayerPopOffOnAtomic(globals.player.sb_models[5], 40.0f);
        }
        globals.player.sb_models[5]->Flags &= ~1;

        // Retail bug: pops sb_models[5] again instead of sb_models[6].
        if (globals.player.sb_models[6]->Flags & 1)
        {
            zEntPlayerPopOffOnAtomic(globals.player.sb_models[5], 50.0f);
        }
        globals.player.sb_models[6]->Flags &= ~1;

        zEntPlayerFX::SuperMachoBowlLightning::RemoveAll();

        xEntBeginUpdate(ent, sc, dt);
        xEntEndUpdate(ent, sc, dt);
        return;
    }

    if (cruise_bubble::update(sc, dt))
    {
        return;
    }

    if (oob_state::update(*sc, dt))
    {
        return;
    }

    zEntPlayer_SpringboardFX(ent, dt);

    if (gCurrentPlayer >= eCurrentPlayerSpongeBob && gCurrentPlayer < eCurrentPlayerCount)
    {
        if (gCurrentPlayer == eCurrentPlayerSpongeBob)
        {
            U32 total =
                xglobals->___player_ent_dont_use_directly->model->Anim->Single->State->UserFlags &
                0x1e;
            if (total == 2 || total == 4)
            {
                if (sPlayerSndID[gCurrentPlayer][ePlayerSnd_Sneak] == (iSndHandle)-1 &&
                    sPlayerSndSneakDelay == 0.0f)
                {
                    zEntPlayer_SNDPlay(ePlayerSnd_Sneak, true);
                }
            }
            else
            {
                zEntPlayer_SNDStop(ePlayerSnd_Sneak);
                sPlayerSndSneakDelay = 0.3f;
            }
        }
        else
        {
            xSndMgrStop(sPlayerSndID[0][ePlayerSnd_Sneak]);
            sPlayerSndID[0][ePlayerSnd_Sneak] = (iSndHandle)-1;
            sPlayerSndSneakDelay = 0.3f;
        }
    }

    if (strcmp(ent->model->Anim->Single->State->Name, "BbowlWindup01") == 0)
    {
        ent->model->Anim->Single->CurrentSpeed =
            1.0f + sBubbleBowlTimer / globals.player.g.BubbleBowlTimeDelay;
        if (ent->model->Anim->Single->CurrentSpeed > 2.0f)
        {
            ent->model->Anim->Single->CurrentSpeed = 2.0f;
        }

        sBubbleBowlTimer += dt;

        if (!sShouldBubbleBowl)
        {
            if (globals.player.ControlOff || !(globals.pad0->on & XPAD_MOVE(XPAD_BUTTON_O)) ||
                sBubbleBowlTimer > globals.player.g.BubbleBowlTimeDelay)
            {
                if (sBubbleBowlTimer > globals.player.g.BubbleBowlTimeDelay)
                {
                    sBubbleBowlTimer = globals.player.g.BubbleBowlTimeDelay;
                }

                sShouldBubbleBowl = 1;
                sBubbleBowlMultiplier =
                    globals.player.g.BubbleBowlPercentIncrease *
                        (sBubbleBowlTimer / globals.player.g.BubbleBowlTimeDelay) +
                    1.0f;
            }
        }
    }

    if (zEntPlayerGetBBowl() && xEntIsVisible(zEntPlayerGetBBowl()) &&
        zEntPlayer_PowerupLevel(PLYR_ATAK_SB_BOWL) > ePowerupLevelNormal)
    {
        xEntBoulder* bbowl = zEntPlayerGetBBowl();
        xVec3* pos = (xVec3*)&bbowl->model->Mat->pos;
        xVec3 velocityNorm;
        xVec3Normalize(&velocityNorm, &bbowl->vel);

        xBound bowlBound;
        bowlBound.type = XBOUND_TYPE_SPHERE;
        xVec3Copy(&bowlBound.sph.center, pos);
        bowlBound.sph.r = 4.0f;
        xQuickCullForBound(&bowlBound.qcd, &bowlBound);
        xGridCheckPosition(&npcs_grid, pos, &bowlBound.qcd, CheckNMEAgainstBubbleBowl,
                           &velocityNorm);

        if (!gBowlButton && sStartBubbleBowlTimer == 0.0f && !globals.player.ControlOff &&
            (globals.pad0->pressed & XPAD_MOVE(XPAD_BUTTON_O)))
        {
            gBowlButton = 1;
            zEntEvent(zEntPlayerGetBBowl(), eEventKill);
        }
    }

    if (gBowlButton && !globals.player.ControlOff &&
        (globals.pad0->released & XPAD_MOVE(XPAD_BUTTON_O)))
    {
        gBowlButton = 0;
    }

    if (sBowlBlastRadius > 0.0f && zEntPlayerGetBBowl())
    {
        xBound bowlBound;
        bowlBound.type = XBOUND_TYPE_SPHERE;
        sBowlBlastRadius += 10.0f * dt;
        xSphere* bowlSph = &bowlBound.sph;
        xVec3Copy(&bowlSph->center, (xVec3*)&zEntPlayerGetBBowl()->model->Mat->pos);
        bowlSph->r = sBowlBlastRadius;
        xQuickCullForBound(&bowlBound.qcd, &bowlBound);
        AttackBoundCollide(ent, (zScene*)sc, &bowlBound, PLYR_ATAK_SB_BOWL);
        if (sBowlBlastRadius >= 4.0f)
        {
            sBowlBlastRadius = 0.0f;
        }
    }

    zEntPlayerBowlTargetUpdate(dt);

    if (sThrowBlastRadius > 0.0f)
    {
        xBound thrownBound;
        thrownBound.type = XBOUND_TYPE_SPHERE;
        sThrowBlastRadius += 10.0f * dt;
        xSphere* bowlSph = &thrownBound.sph;
        xVec3Copy(&bowlSph->center, &sThrowBlastPosition);
        bowlSph->r = sThrowBlastRadius;
        xQuickCullForBound(&thrownBound.qcd, &thrownBound);
        AttackBoundCollide(ent, (zScene*)sc, &thrownBound, PLYR_ATAK_PAT_THROW);
        if (sThrowBlastRadius >= 4.0f)
        {
            sThrowBlastRadius = 0.0f;
        }
    }

    zEntPickup_CheckAllPickupsAgainstPlayer(sc, dt);

    zEntPlayer_UpdateCharacterSwitchingInMidPlayerUpdate(ent, sc, dt);

    if (gCurrentPlayer == eCurrentPlayerCar)
    {
        zEntPlayerCarUpdate(ent, sc, dt);
        rendermat = *(xMat4x3*)ent->model->Mat;
        return;
    }

    globals.player.InvTreasureChestCurrentLevel =
        globals.player.InvTreasureChest[zSceneGetLevelIndex()];
    globals.player.InvLevelPickupsCurrentLevel =
        globals.player.InvLevelPickups[zSceneGetLevelIndex()];

    sGrabFound = NULL;
    sGrabFailed = 0;

    if (ent->model == globals.player.model_patrick)
    {
        if (globals.player.carry.grabbed)
        {
            xEnt* oldTarget = gReticleTarget[0];
            sTimeToRetarget[0] -= dt;
            if (sTimeToRetarget[0] < 0.0f)
            {
                sTimeToRetarget[0] = 0.25f;
                gReticleTarget[0] = GetPatrickTarget(ent);
                sTypeOfTarget[0] = 3;
                if (gReticleTarget[0] != oldTarget)
                {
                    sReticleAlpha[0] = 0.0f;
                }
            }
            sShowThrowArrow = 1.0f;
        }
        else
        {
            if (sTypeOfTarget[0] != 1)
            {
                xEnt* oldTarget = gReticleTarget[0];
                sTimeToRetarget[0] -= dt;
                if (sTimeToRetarget[0] < 0.0f)
                {
                    sTimeToRetarget[0] = 0.25f;
                    gReticleTarget[0] = zEntPlayer_FindGrabEnt(ent, (zScene*)sc, NULL);
                    sTypeOfTarget[0] = 2;
                    if (gReticleTarget[0] != oldTarget)
                    {
                        sReticleAlpha[0] = 0.0f;
                    }
                }
            }
            sShowThrowArrow = 0.0f;
        }

        if (sShowThrowArrow)
        {
            if (sThrowDecalFactor < 0.5f)
            {
                sThrowDecalFactor += 5.0f * dt;
                if (sThrowDecalFactor > 1.0f)
                {
                    sThrowDecalFactor = 1.0f;
                }
            }
        }
        else if (sThrowDecalFactor > 0.0f)
        {
            sThrowDecalFactor -= 8.0f * dt;
            if (sThrowDecalFactor < 0.0f)
            {
                sThrowDecalFactor = 0.0f;
            }
        }

        if (!globals.player.carry.grabbed && !globals.player.ControlOff &&
            (globals.pad0->pressed & XPAD_BUTTON_R1) && globals.player.g.PowerUp[7])
        {
            if (gReticleTarget[0] && sTypeOfTarget[0] == 2)
            {
                sGrabFound = gReticleTarget[0];
            }
            else
            {
                sGrabFound = zEntPlayer_FindGrabEnt(ent, (zScene*)sc, &sGrabFailed);
            }
        }

        xVec3 pos = *(xVec3*)&ent->model->Mat->pos;

        xAnimSingle* single = xglobals->___player_ent_dont_use_directly->model->Anim->Single;
        xAnimState* astate = single->State;

        if (StunBubbleTrail(single))
        {
            zFX_SpawnBubbleTrail(
                (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos, 1);
        }

        if (sRingDelay > 0.0f)
        {
            F32 s;
            if (dt > sRingDelay)
            {
                s = sRingDelay;
            }
            else
            {
                s = dt;
            }
            sRingDelay -= s;
        }

        if (globals.player.g.PowerUp[6] > 1)
        {
            if ((strcmp(astate->Name, "StunLand") == 0 ||
                 strcmp(astate->Name, "StunLand02") == 0) &&
                xglobals->___player_ent_dont_use_directly->model->Anim->Single->Time < 0.25f)
            {
                if (sRingDelay == 0.0f)
                {
                    // The goto past the slam is in the PS2 build too.
                    if (ent->collis->colls[0].dist > 2.0f)
                    {
                        goto patrick_done;
                    }

                    // The lift comes after all three components, as in the PS2 line table.
                    pos.x += ent->collis->colls[0].tohit.x * ent->collis->colls[0].dist;
                    pos.y += ent->collis->colls[0].tohit.y * ent->collis->colls[0].dist;
                    pos.z += ent->collis->colls[0].tohit.z * ent->collis->colls[0].dist;
                    pos.y += 0.2f;

                    zFXPatrickStun(&pos);
                    sRingDelay = 2.0f;
                }

                zFX_SpawnBubbleSlam(&pos, 24, 0.15f, 12.0f, 2.0f);
            }
        }
        else if (globals.player.g.PowerUp[6] == 1)
        {
            if ((strcmp(astate->Name, "StunLand") == 0 ||
                 strcmp(astate->Name, "StunLand02") == 0) &&
                xglobals->___player_ent_dont_use_directly->model->Anim->Single->Time < 0.25f)
            {
                zFX_SpawnBubbleSlam(&pos, 8, 0.15f, 6.0f, 1.0f);
            }
        }
    }

patrick_done:
    if (globals.player.SundaeTimer >= 0.0f)
    {
        globals.player.SundaeTimer -= dt;
        if (globals.player.SundaeTimer <= 0.0f)
        {
            globals.player.SpeedMult = 1.0f;
            globals.player.SundaeTimer = -1.0f;
        }
        else
        {
            sLastInvulnEmit += dt;
            if (sLastInvulnEmit > 0.02f)
            {
                sLastInvulnEmit = 0.0f;

                xParEmitterCustomSettings info;
                info.custom_flags = 0x352;
                xVec3Copy(&info.pos, xBoundCenter(&ent->bound));

                xVec3 vel;
                xVec3 tmp;
                xVec3SMul(&vel, &globals.player.PredictCurrDir, globals.player.PredictCurrVel);
                xVec3SMul(&tmp,
                          (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->right,
                          1.0f);
                xVec3Add(&vel, &vel, &tmp);
                xVec3SMul(&tmp, (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->at,
                          1.0f);
                xVec3Add(&info.vel, &vel, &tmp);

                info.vel_angle_variation = 0.075f;
                info.rate.set(150.0f, 150.0f, 1.0f, 0);
                info.life.set(1.5f, 2.0f, 1.0f, 2);

                xParEmitterEmitCustom(sEmitStankBreath, 1.0f / 60.0f, &info);
            }
        }
    }

    if ((ent->model->Anim->Single->State->UserFlags & 0x1000) || tslide_ground)
    {
        xVec3 normvel;
        normvel = globals.player.SlideTrackVel;
        normvel.y = -1e-07f;

        xBound slideB;
        slideB.type = XBOUND_TYPE_SPHERE;
        slideB.sph.center.x = ent->bound.sph.center.x + 1.25f * dt * globals.player.SlideTrackVel.x;
        slideB.sph.center.y = ent->bound.sph.center.y + 1.25f * dt * ent->frame->vel.y;
        slideB.sph.center.z = ent->bound.sph.center.z + 1.25f * dt * globals.player.SlideTrackVel.z;
        slideB.sph.r = ent->bound.sph.r + 0.3f;

        xQuickCullForBound(&slideB.qcd, &slideB);
        AttackBoundCollide(ent, (zScene*)sc, &slideB, PLYR_ATAK_SLIDE);
    }

    const U8* bonelist = NULL;

    if (ent->model == globals.player.model_spongebob)
    {
        xAnimSingle* single = xglobals->___player_ent_dont_use_directly->model->Anim->Single;
        if (BubbleBashContrails(single))
        {
            bonelist = SBBBashBones;
        }
        else if (BubbleBounceContrails(single))
        {
            bonelist = SBBBounceBones;
        }
    }

    if (bonelist)
    {
        U32 num = 0;
        const U8* bp = bonelist;
        while (*bp)
        {
            num++;
            bp++;
        }

        xVec3* posbuf = (xVec3*)xMemPushTemp(num * 2 * sizeof(xVec3));
        xVec3* velbuf = posbuf + num;
        if (posbuf)
        {
            xVec3* pp = posbuf;
            xVec3* vp = velbuf;
            bp = bonelist;
            for (U32 j = 0; j < num; j++, pp++, vp++, bp++)
            {
                xMat4x3 mat;
                xMat4x3Mul(&mat, (xMat4x3*)(ent->model->Mat + *bp), (xMat4x3*)ent->model->Mat);
                *pp = mat.pos;
                pp->x += 0.1f * (xurand() - 0.5f);
                pp->y += 0.1f * (xurand() - 0.5f);
                pp->z += 0.1f * (xurand() - 0.5f);
                vp->x = 0.2f * (xurand() - 0.5f);
                vp->y = 0.2f * (xurand() - 0.5f);
                vp->z = 0.2f * (xurand() - 0.5f);
            }

            zParPTankSpawnBubbles(posbuf, velbuf, num, 1.0f);
            xMemPopTemp(posbuf);
        }
    }

    if ((globals.player.ControlOff & ~(CONTROL_OWNER_FROZEN | CONTROL_OWNER_SPRINGBOARD)) ||
        globals.cmgr)
    {
        globals.player.ControlOffTimer = 1.0f;
    }

    globals.player.KnockBackTimer -= dt;
    if (globals.player.KnockBackTimer < 0.0f)
    {
        globals.player.KnockBackTimer = 0.0f;
    }

    globals.player.KnockIntoAirTimer -= dt;
    if (globals.player.KnockIntoAirTimer < 0.0f)
    {
        globals.player.KnockIntoAirTimer = 0.0f;
    }

    globals.player.Sneak = 0;

    xAnimSingle* single = xglobals->___player_ent_dont_use_directly->model->Anim->Single;

    sIdleCommentTimer.MightTick();

    if (!globals.player.ControlOff && !globals.pad0->on &&
        abs(globals.pad0->analog1.x) <= globals.player.g.AnalogMin &&
        abs(globals.pad0->analog1.y) <= globals.player.g.AnalogMin)
    {
        xAnimState* astate = xglobals->___player_ent_dont_use_directly->model->Anim->Single->State;
        if (astate && (astate->UserFlags & 1))
        {
            globals.player.IdleMinorTimer += dt;
            globals.player.IdleMajorTimer += dt;
            if (!surfSticky)
            {
                globals.player.IdleSitTimer += dt;
            }

            if (globals.player.IdleMinorTimer == 0.0f)
            {
                globals.player.IdleMinorTimer = 0.0001f;
            }

            sIdleCommentTimer.TimerTick(dt);
        }
    }
    else
    {
        globals.player.IdleMinorTimer = 0.0f;
        globals.player.IdleMajorTimer = -3.0f;
        globals.player.IdleSitTimer = 0.0f;
    }

    if (!sIdleCommentTimer.DidTick())
    {
        sIdleCommentTimer.TimerReset();
    }

    sPatCarryCommentTimer.TimerTickElseReset(globals.player.carry.grabbed != NULL, dt);
    sRunningCommentTimer.TimerTickElseReset(zEntPlayer_IsRunning(ent), dt);

    if (globals.player.JumpState == 0)
    {
        if (globals.player.Speed >= 1.0f && surfSlickRatio && globals.player.s->Health)
        {
            zEntPlayer_SNDPlayStream(ePlayerStreamSnd_Icy);
        }
    }

    if (globals.player.ScareTimer > 0.0f)
    {
        globals.player.ScareTimer -= dt;
        if (globals.player.ScareTimer < 0.0f)
        {
            globals.player.ScareTimer = 0.0f;
        }
    }

    if (globals.player.DamageTimer > 0.0f)
    {
        globals.player.DamageTimer -= dt;
        if (globals.player.DamageTimer < 0.0f)
        {
            globals.player.DamageTimer = 0.0f;
        }
    }

    if (globals.player.ControlOffTimer > 0.0f)
    {
        globals.player.ControlOffTimer -= dt;
        if (globals.player.ControlOffTimer < 0.0f)
        {
            globals.player.ControlOffTimer = 0.0f;
        }
    }

    if (globals.player.CowerTimer > 0.0f)
    {
        globals.player.CowerTimer -= dt;
        if (globals.player.CowerTimer < 0.0f)
        {
            globals.player.CowerTimer = 0.0f;
        }
    }

    if (globals.player.FallDeathTimer > 0.0f)
    {
        globals.player.FallDeathTimer -= dt;
        if (globals.player.FallDeathTimer < 0.0f)
        {
            globals.player.FallDeathTimer = 0.0f;
        }
    }

    if (globals.player.HelmetTimer > 0.0f)
    {
        globals.player.HelmetTimer -= dt;
        if (globals.player.HelmetTimer < 0.0f)
        {
            globals.player.HelmetTimer = 0.0f;
        }
    }

    if (sPlayerSndSneakDelay > 0.0f)
    {
        sPlayerSndSneakDelay -= dt;
        if (sPlayerSndSneakDelay < 0.0f)
        {
            sPlayerSndSneakDelay = 0.0f;
        }
    }

    if (wallStickTimer > 0.0f)
    {
        wallStickTimer -= dt;
        if (wallStickTimer < 0.0f)
        {
            wallStickTimer = 0.0f;
        }
    }

    if (sStartBubbleBowlTimer > 0.0f)
    {
        sStartBubbleBowlTimer -= dt;
        if (sStartBubbleBowlTimer < 0.0f)
        {
            sStartBubbleBowlTimer = 0.0f;
        }
    }

    if (sBashSoundTimer > 0.0f)
    {
        sBashSoundTimer -= dt;
        if (sBashSoundTimer < 0.0f)
        {
            sBashSoundTimer = 0.0f;
        }
    }

    idle_tmr += dt;
    if (globals.player.ControlOff & CONTROL_OWNER_TALK_BOX)
    {
        idle_tmr = 0.0f;
    }

    if (inact_tmr > 0.0f)
    {
        inact_tmr -= dt;
        if (inact_tmr < 0.0f)
        {
            inact_tmr = 0.0f;
        }
    }

    stun_power_tmr += dt;

    xColor_tag black = { 0, 0, 0, 255 };
    xColor_tag clear = { 0, 0, 0, 0 };

    if (!globals.player.s->Health)
    {
        zEntPlayerFX::SFX::PlayFailure();

        if (globals.player.DamageTimer >= 0.3f &&
            (single->CurrentSpeed == 0.0f || shrapDeathTimer == 0.0f) && !xScrFxIsFading() &&
            (single->State->UserFlags & 0x400))
        {
            if (shrapDeathTimer == 0.0f)
            {
                shrapDeathTimer = -1.0f;
            }

            xScrFxFade(&clear, &black, 0.3f, 1);
            // the fade time plus one 30 Hz frame: the sum folds to retail's 0x3EAAAAA0, which no
            // short decimal literal gives
            globals.player.DamageTimer = 0.3f + 0.033333f;
        }

        if (globals.player.DamageTimer <= 0.0f)
        {
            zGameStateSwitch(2);
            HealthReset();
        }

        if (shrapDeathTimer > 0.0f)
        {
            shrapDeathTimer -= dt;
            if (shrapDeathTimer < 0.0f)
            {
                shrapDeathTimer = 0.0f;
            }
        }
    }

    sCameraLastMat = *(xMat4x3*)ent->model->Mat;
    sWallJumpResult = WallJumpResult_NoJump;

    if ((xglobals->___player_ent_dont_use_directly->model->Anim->Single->State->UserFlags &
         0x81e) == 0xa)
    {
        DoWallJumpCheck();
    }

    if (globals.player.WallJumpState != lastWallJumpState)
    {
        S32 moveCam = 0;
        S32 stopCam = 0;

        switch (globals.player.WallJumpState)
        {
        case k_WALLJUMP_STUCK:
            stopCam = 1;
            break;
        case k_WALLJUMP_NOT:
            moveCam = 1;
            break;
        case k_WALLJUMP_LAND:
            zEntPlayerWallJumpSpawnBubbleRing();
            break;
        case k_WALLJUMP_LAUNCH:
            if (lastWallJumpState == k_WALLJUMP_FLIGHT || lastWallJumpState == k_WALLJUMP_NOT)
            {
                zEntPlayerWallJumpSpawnBubbleRing();
            }
            break;
        }

        switch (lastWallJumpState)
        {
        case k_WALLJUMP_STUCK:
            if (globals.player.WallJumpState == k_WALLJUMP_FALL ||
                globals.player.WallJumpState == k_WALLJUMP_FLIGHT)
            {
                moveCam = 1;
            }
            break;
        case k_WALLJUMP_LAND:
            if (globals.player.WallJumpState == k_WALLJUMP_FLIGHT)
            {
                moveCam = 1;
            }
            break;
        }

        if (moveCam)
        {
            zCameraEnableTracking(CO_WALLJUMP);
            zCameraDisableWallJump(&globals.oldSkoolCamera);
        }
        else if (stopCam)
        {
            zCameraDisableTracking(CO_WALLJUMP);
        }
    }

    lastWallJumpState = globals.player.WallJumpState;

    if (ent->model->Anim->Single->CurrentSpeed == 0.0f && !ent->model->Anim->Single->Blend->State &&
        (strcmp(ent->model->Anim->Single->State->Name, "Bspin01") == 0 ||
         strcmp(ent->model->Anim->Single->State->Name, "Bspin02") == 0 ||
         strcmp(ent->model->Anim->Single->State->Name, "Melee01") == 0 ||
         strcmp(ent->model->Anim->Single->State->Name, "Melee02") == 0))
    {
        ent->model->Anim->Single->CurrentSpeed = ent->model->Anim->Single->State->Speed;
    }

    xEntBeginUpdate(ent, sc, dt);

    xAnimState* astate = xglobals->___player_ent_dont_use_directly->model->Anim->Single->State;

    if (bbash_end_tmr && strncmp(astate->Name, "Bbash", 5) != 0)
    {
        bbash_end_tmr -= dt;
        if (bbash_end_tmr < 0.0f)
        {
            bbash_end_tmr = 0.0f;
            zCameraMinTargetHeightClear();
        }
    }

    S32 anim;
    if (gCurrentPlayer == eCurrentPlayerSpongeBob)
    {
        anim = strcmp(astate->Name, "Bspin01") == 0 || strcmp(astate->Name, "Bspin02") == 0;
    }
    else if (gCurrentPlayer == eCurrentPlayerPatrick)
    {
        anim = strcmp(astate->Name, "Melee01") == 0 || strcmp(astate->Name, "Melee02") == 0;
    }
    else
    {
        anim = 0;
    }

    globals.player.IsBubbleSpinning = (anim ? single->CurrentSpeed != 0.0f : 0) &&
                                      single->Time >= globals.player.g.BSpinMinFrame &&
                                      single->Time <= globals.player.g.BSpinMaxFrame;

    ent->frame->oldmat.pos.y += sPlayerCollAdjust;
    ent->frame->mat.pos.y += sPlayerCollAdjust;

    if (globals.player.g.CheatFlyToggle &&
        ((globals.player.g.FriendlySelectMode && (globals.pad0->pressed & XPAD_BUTTON_Z) &&
          !(globals.pad0->pressed & XPAD_BUTTON_L2) && !(globals.pad0->on & XPAD_BUTTON_L2)) ||
         (!globals.player.g.FriendlySelectMode &&
          ((globals.pad0->pressed & XPAD_BUTTON_L2) || (globals.pad0->on & XPAD_BUTTON_L2)) &&
          (globals.pad0->pressed & XPAD_BUTTON_Z))))
    {
        if (globals.player.cheat_mode)
        {
            ent->collis->chk = 0x2e;
            globals.player.cheat_mode = 0;
            xCameraDoCollisions(1, 3);
        }
        else
        {
            xVec3Copy(&ent->frame->vel, &g_O3);
            ent->collis->chk = 0;
            globals.player.cheat_mode = 1;
            xCameraDoCollisions(0, 3);
        }
    }

    if (globals.player.ShockRadius)
    {
        globals.player.ShockRadius += 6.0f * dt;
        if (globals.player.ShockRadius > 4.0f)
        {
            globals.player.ShockRadius = 4.0f;
        }

        // Dead shock-wave test (its effect on the npcs is gone; the PS2 DWARF still lists its
        // locals). The formulas are a reconstruction: an empty or trivial body would not keep the
        // loop rolled as retail does.
        F32 rad2 = SQR(globals.player.ShockRadius);
        F32 radold2 = SQR(globals.player.ShockRadiusOld);

        for (U32 i = 0; i < sc->num_npcs; i++)
        {
            xEnt* vill = sc->npcs[i];
            F32 sdist2 = SQR(vill->model->Mat->pos.x - ent->model->Mat->pos.x) +
                         SQR(vill->model->Mat->pos.z - ent->model->Mat->pos.z);
            F32 sdistold2 = xabs(sdist2 - radold2);
        }

        if (4.0f == globals.player.ShockRadius)
        {
            globals.player.ShockRadius = 0.0f;
        }
        else
        {
            globals.player.ShockRadiusOld = globals.player.ShockRadius;
        }
    }

    zGooCollsBegin();

    if (globals.player.WallJumpState != k_WALLJUMP_NOT || strncmp(astate->Name, "Bbash", 5) == 0)
    {
        sHackStuckTimer = 0.0f;
    }
    else
    {
        F32 playerVel = xVec3Length2(&ent->frame->vel);
        F32 actualVel = xVec3Length2(&tslide_lastrealvel);

        if (ent->collis->colls[0].flags & 1)
        {
            if (sHackStuckTimer)
            {
                ent->frame->vel = sHackStuckVel;
                ent->frame->oldvel = sHackStuckVel;
            }

            sHackStuckTimer = 0.0f;
        }
        else if (playerVel >= 36.0f && actualVel <= 0.25f && !(ent->collis->colls[0].flags & 1) &&
                 ((ent->collis->colls[2].flags & 1) || (ent->collis->colls[3].flags & 1) ||
                  (ent->collis->colls[4].flags & 1) || (ent->collis->colls[5].flags & 1)))
        {
            if (!sHackStuckTimer)
            {
                sHackStuckVel = ent->frame->vel;
            }

            sHackStuckTimer = 0.15f;
        }
        else if (sHackStuckTimer)
        {
            sHackStuckTimer -= dt;
            if (sHackStuckTimer < 0.0f)
            {
                sHackStuckTimer = 0.0f;
                ent->frame->vel = sHackStuckVel;
                ent->frame->oldvel = sHackStuckVel;
            }
        }

        if (sHackStuckTimer)
        {
            if (!sHackStuckSetDir)
            {
                xRay3 testRay;
                xCollis testColl;

                testRay.origin = ent->frame->oldmat.pos;
                testRay.origin.y += 0.5f;
                testRay.dir.x = 0.0f;
                testRay.dir.y = -1.0f;
                testRay.dir.z = 0.0f;
                testRay.min_t = 0.0f;
                testRay.max_t = 1.5f;
                testRay.flags = 0xc00;
                testColl.flags = 0x200;

                xRayHitsEnv(&testRay, globals.sceneCur->env, &testColl);

                if ((testColl.flags & 1) && xabs(testColl.norm.y) < 0.99985f)
                {
                    sHackStuckDir.x = testColl.norm.x * testColl.norm.y;
                    sHackStuckDir.y =
                        -testColl.norm.x * testColl.norm.x - testColl.norm.z * testColl.norm.z;
                    sHackStuckDir.z = testColl.norm.z * testColl.norm.y;
                    xVec3Normalize(&sHackStuckDir, &sHackStuckDir);
                    sHackStuckSetDir = 1;
                }
            }

            if (sHackStuckSetDir)
            {
                F32 s = xsqrt(playerVel);
                if (s > 10.0f)
                {
                    s = 10.0f;
                }

                ent->frame->vel.x = s * sHackStuckDir.x;
                ent->frame->vel.y = s * sHackStuckDir.y;
                ent->frame->vel.z = s * sHackStuckDir.z;
                ent->frame->oldvel = ent->frame->vel;
            }
            else
            {
                sHackStuckDir.x = 0.0f;
                sHackStuckDir.y = 0.0f;
                sHackStuckDir.z = 0.0f;
            }
        }
    }

    F32 old_yvel = ent->frame->vel.y;

    sPlayerUpdateCount = 1;

    sDriveVel.z = 0.0f;
    sDriveVel.y = 0.0f;
    sDriveVel.x = 0.0f;

    if (!globals.player.HangEnt && !sLassoInfo->swingTarget)
    {
        if (dt > 0.1f)
        {
            sPlayerUpdateCount = 6;
        }
        else if (dt > 1.0f / 12.0f)
        {
            sPlayerUpdateCount = 5;
        }
        else if (dt > 1.0f / 15.0f)
        {
            sPlayerUpdateCount = 4;
        }
        else if (dt > 0.05f)
        {
            sPlayerUpdateCount = 3;
        }
        else
        {
            sPlayerUpdateCount = 2;
        }
    }

    dt = dt / sPlayerUpdateCount;

    static xEntCollis old_collis;

    for (sPlayerUpdateIndex = 0; sPlayerUpdateIndex < sPlayerUpdateCount; sPlayerUpdateIndex++)
    {
        xVec3 suboldpos = ent->frame->mat.pos;

        xEntApplyPhysics(ent, sc, dt);
        xEntMove(ent, sc, dt);

        req_motion = ent->frame->dpos;

        xVec3 predrive_pos = ent->frame->mat.pos;

        zEntPlayerDriveUpdate(ent, sc, dt);
        xEntDriveUpdate(&globals.player.drv, sc, dt, NULL);

        sDriveVel.x += ent->frame->mat.pos.x - predrive_pos.x;
        sDriveVel.y += ent->frame->mat.pos.y - predrive_pos.y;
        sDriveVel.z += ent->frame->mat.pos.z - predrive_pos.z;

        xFFXApply(ent, sc, dt);

        xVec3 motion;
        xVec3Sub(&motion, &ent->frame->mat.pos, &suboldpos);

        if (!globals.player.cheat_mode &&
            (globals.player.JumpState == 0 || globals.player.JumpState == 1) &&
            !globals.player.FallDeathTimer)
        {
            ent->frame->mat.pos.y -= 1.5f * dt;

            F32 ndotm =
                globals.player.floor_norm.x * motion.x + globals.player.floor_norm.z * motion.z;

            if (ndotm > 0.0f && (req_motion.x != 0.0f || req_motion.z != 0.0f || surfSlickRatio))
            {
                globals.player.slope = -1;
                ent->frame->mat.pos.y -= xsqrt(ndotm);
            }
            else if (ndotm < 0.0f)
            {
                globals.player.slope = 1;
            }
            else
            {
                globals.player.slope = 0;
            }
        }

        if (lastFloorEnt)
        {
            F32 sft = globals.player.SlipFadeTimer;
            if (surfSlickRatio)
            {
                F32 fg = 13.0f * dt;
                F32 m = globals.player.floor_norm.x * globals.player.floor_norm.x +
                        globals.player.floor_norm.z * globals.player.floor_norm.z;
                ent->frame->mat.pos.x += fg * globals.player.floor_norm.x;
                ent->frame->mat.pos.y -= fg * xsqrt(m);
                ent->frame->mat.pos.z += fg * globals.player.floor_norm.z;
            }
            else if (sft > 0.0f)
            {
                F32 m = 30.0f * (sft / 0.8f * dt);
                ent->frame->mat.pos.x += m * lastFloorNorm.x;
                ent->frame->mat.pos.z += m * lastFloorNorm.z;
            }
        }

        xVec3Sub(&precollide_motion, &ent->frame->mat.pos, &ent->frame->oldmat.pos);

        zEntPlayerCollide(ent, sc, dt);

        if (globals.player.WallJumpState == k_WALLJUMP_LAND ||
            globals.player.WallJumpState == k_WALLJUMP_STUCK)
        {
            stuck_timer = 0.0f;
        }

        U8 hitting_floor = ent->collis->colls[0].flags & 1;
        // bool, not the PS2 DWARF's unsigned int: retail tests it as a byte
        bool stuckforce = (ent->collis->colls[2].flags & 1) || (ent->collis->colls[3].flags & 1) ||
                          (ent->collis->colls[4].flags & 1) ||
                          ((S16)ent->collis->colls[5].flags & 1);

        if (hitting_floor || (globals.player.ControlOff & ~CONTROL_OWNER_SPRINGBOARD))
        {
            stuck_timer = 0.0f;
            not_stuck_timer = 0.0f;
        }
        else if (!stuckforce)
        {
            not_stuck_timer += dt;
            if (not_stuck_timer > 0.2f)
            {
                stuck_timer = 0.0f;
                not_stuck_timer = 0.0f;
            }
        }
        else
        {
            xVec3& player_loc =
                *(xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos;

            if (stuck_timer == 0.0f)
            {
                not_stuck_timer = 0.0f;
                stuck_start_loc = player_loc;
            }

            F32 dist = (player_loc - stuck_start_loc).length2();

            if (dist > 1.0f)
            {
                stuck_timer = 0.0f;
                not_stuck_timer = 0.0f;
            }
            else
            {
                stuck_timer += dt;
                if (stuck_timer > 1.5f && globals.player.s->Health && !in_goo)
                {
                    oob_state::force_start();
                }
            }

            xprintf("Stuck: %.2f %.2f %.2f\n", stuck_timer, not_stuck_timer, xsqrt(dist));
        }

        memcpy(&old_collis, ent->collis, sizeof(xEntCollis));
        zEntPlayerCollide(ent, sc, dt);
        memcpy(ent->collis, &old_collis, sizeof(xEntCollis));

        zEntPlayer_CheckCritterContact(ent, dt);
        xEntBoulder_ApplyForces(ent->collis);
    }

    xVec3 driveDist = sDriveVel;

    dt = update_dt;

    if (update_dt > 0.0001f)
    {
        sDriveVel.x = sDriveVel.x / update_dt;
        sDriveVel.y = sDriveVel.y / update_dt;
        sDriveVel.z = sDriveVel.z / update_dt;
    }

    xVec3Sub(&update_motion, &ent->frame->mat.pos, &ent->frame->oldmat.pos);

    if (!globals.player.cheat_mode)
    {
        xCollis bbc;
        bbc.flags = 0;

        xBox bbox = *(xBox*)xEnvGetBBox(globals.sceneCur->env);

        if (globals.sceneCur->zen)
        {
            bbox.upper.y += globals.sceneCur->zen->easset->loldHeight;
        }

        xSphereHitsBox(&ent->bound.sph, &bbox, &bbc);

        if (!(bbc.flags & 0x11))
        {
            zEntEvent(ent, eEventOutOfBounds);
        }
    }

    if (sCatchCapsuleTimer)
    {
        sCatchCapsuleTimer -= dt;
        if (sCatchCapsuleTimer <= 0.0f)
        {
            sCatchCapsuleTimer = 0.0f;
        }
    }

    if (!globals.player.cheat_mode)
    {
        sHackStuckSetDir = 0;

        if (update_motion.length() > 1e-5f)
        {
            U32 redo_catchtunnel = 1;

            while (1)
            {
                xVec3 vstart;
                xVec3 vend;
                xSweptSphere sws;

                memset(&sws, 0, sizeof(xSweptSphere));

                vstart = ent->frame->oldmat.pos;
                vstart.y += ent->bound.sph.r - ent->bound.sph.r * CATCH_CAPSULE_BIAS;
                vend = vstart + update_motion;

                xSweptSpherePrepare(&sws, &vstart, &vend, ent->bound.sph.r * CATCH_CAPSULE_RAD);

                // The goto past the reset after the loop is in the PS2 build too.
                if (!xSweptSphereToNonMoving(&sws, globals.sceneCur, ent, ent->collType, 0))
                {
                    goto catchtunnel_done;
                }

                xSweptSphereGetResults(&sws);

                if (!sCatchCapsuleTimer)
                {
                    sCatchCapsuleTimer = 0.3f;
                }

                if (!redo_catchtunnel)
                {
                    break;
                }

                // not in the DWARF order (iter, swsredo, swscurr): swscurr declared first gives
                // retail's loop registers
                xSweptSphere* swscurr;
                U32 iter = 0;
                xSweptSphere swsredo[3];
                xVec3 totalTan;

                memset(swsredo, 0, sizeof(swsredo));
                vend = vstart + precollide_motion;
                memset(swsredo, 0, sizeof(swsredo));

                for (swscurr = swsredo; iter < 3; iter++, swscurr++)
                {
                    xSweptSpherePrepare(swscurr, &vstart, &vend,
                                        ent->bound.sph.r * CATCH_CAPSULE_RAD);

                    if (!xSweptSphereToNonMoving(swscurr, globals.sceneCur, ent, ent->collType, 0))
                    {
                        if (iter >= 1)
                        {
                            vstart = vend;
                            break;
                        }

                        vend = vstart + update_motion;
                        *swscurr = sws;
                    }
                    else
                    {
                        xSweptSphereGetResults(swscurr);
                    }

                    if (iter == 0)
                    {
                        totalTan = swscurr->worldTangent;
                        sHackStuckDir = totalTan;
                        sHackStuckSetDir = 1;
                    }
                    else
                    {
                        xVec3Cross(&totalTan, &swscurr->worldNormal,
                                   &swsredo[iter - 1].worldNormal);

                        // A two-level exit to the reset, as in the PS2 build.
                        if (totalTan.length2() < 1e-07f)
                        {
                            goto catchtunnel_reset;
                        }

                        totalTan.normalize();

                        if (totalTan.x * swscurr->dir.x + totalTan.y * swscurr->dir.y +
                                totalTan.z * swscurr->dir.z <
                            0.0f)
                        {
                            totalTan.x = -totalTan.x;
                            totalTan.y = -totalTan.y;
                            totalTan.z = -totalTan.z;
                        }

                        if (iter == 1)
                        {
                            sHackStuckDir = totalTan;
                            sHackStuckSetDir = 1;
                        }
                    }

                    F32 distremain = swscurr->dist - swscurr->curdist;

                    xVec3 deltaremain;
                    deltaremain.x = distremain * swscurr->dir.x;
                    deltaremain.y = distremain * swscurr->dir.y;
                    deltaremain.z = distremain * swscurr->dir.z;

                    F32 tandot = xVec3Dot(&deltaremain, &totalTan);

                    vstart = swscurr->worldPos;

                    vend.x = tandot * totalTan.x + vstart.x;
                    vend.y = tandot * totalTan.y + vstart.y;
                    vend.z = tandot * totalTan.z + vstart.z;
                }

                ent->frame->mat.pos.x = vstart.x;
                ent->frame->mat.pos.y =
                    vstart.y - (ent->bound.sph.r - ent->bound.sph.r * CATCH_CAPSULE_BIAS);
                ent->frame->mat.pos.z = vstart.z;

                zEntPlayerCollide(ent, sc, dt);

                xVec3Sub(&update_motion, &ent->frame->mat.pos, &ent->frame->oldmat.pos);

                redo_catchtunnel = 0;
            }

        catchtunnel_reset:
            ent->frame->mat.pos = ent->frame->oldmat.pos;
        }
    }

catchtunnel_done:
    xCollis* wcoll = &ent->collis->colls[2];
    xCollis* gcoll = &ent->collis->colls[6];
    for (; wcoll < gcoll; wcoll++)
    {
        if (wcoll->flags & 1)
        {
            F32 hdotm = wcoll->hdng.x * req_motion.x + wcoll->hdng.z * req_motion.z;
            xSurface* wsurf = zSurfaceGetSurface(wcoll);

            if (hdotm > 0.0f && wcoll->hdng.y < 0.0f && zSurfaceGetStep(wsurf))
            {
                ent->frame->mat.pos.y += 1.5f * dt;
                ent->collis->colls[0] = *wcoll;
                wcoll->flags = 0;
            }
        }
    }

    zGooCollsEnd();

    if (globals.player.JumpState == 0 && !in_goo)
    {
        sGooKnockedTimer -= dt;
        if (sGooKnockedTimer < 0.0f)
        {
            sGooKnockedTimer = 0.0f;
            sGooKnockedToSafety = 0;
        }
    }

    lin_goo = in_goo;

    xCollis* fcoll = &ent->collis->colls[0];

    if (fcoll->flags & 1)
    {
        xBase* b = (xBase*)fcoll->optr;
        if (b)
        {
            if (b->baseType == eBaseTypeButton)
            {
                zEntButton_Hold((_zEntButton*)b, 0x400);
            }

            F32 dummy;
            S32 isGoo = zGooIs((xEnt*)fcoll->optr, dummy, 1);
            S32 isBB = ((xEnt*)fcoll->optr)->miscflags & 0x20;
            S32 sliding = (ent->model->Anim->Single->State->UserFlags & 0x1000) || tslide_ground;

            if (isGoo || isBB)
            {
                if (!sGooKnockedToSafety && ((isGoo && globals.player.s->Health > 1) || isBB) &&
                    !sliding && zEntPlayerKnockToSafety(ent))
                {
                    globals.player.DamageTimer = 0.0f;
                    zEntPlayer_Damage((xBase*)fcoll->optr, 1);
                    sGooKnockedToSafety = 1;
                    sGooKnockedTimer = 0.25f;
                    if (!isBB)
                    {
                        zEntPlayer_SNDPlayStream(ePlayerStreamSnd_IntoGoo);
                    }
                }
                else
                {
                    in_goo = 1;
                    if (isBB)
                    {
                        sOnBouncBack = 1;
                    }
                }
            }
            else
            {
                in_goo = 0;
                sOnBouncBack = 0;
            }

            globals.player.CanJump = 1;
            globals.player.Jump_CanDouble = 1;
        }
        else
        {
            in_goo = 0;
            sOnBouncBack = 0;
        }
    }

    if (in_goo)
    {
        if (fcoll->flags & 1)
        {
            in_goo_tmr += dt;
            if ((U8)xrand_RandomInt32() > 0x40)
            {
                // Stored and never read (PS2 DWARF locals); retail keeps their .rodata templates.
                xVec3 pos_rnd = { 0.5f, 0.5f, 0.5f };
                xVec3 vel_rnd = { 0.25f, 0.5f, 0.25f };
                zFX_SpawnBubbleTrail((xVec3*)&ent->model->Mat->pos, 1);
            }
        }
    }
    else
    {
        in_goo_tmr = 0.0f;
    }

    if (globals.player.drv.driver)
    {
        zCameraTranslate(&globals.oldSkoolCamera, &driveDist);
    }

    if (zcam_bbounce)
    {
        if (ent->frame->mat.pos.y > ent->frame->oldmat.pos.y)
        {
            zCameraTranslate(&globals.oldSkoolCamera, 0.0f,
                             ent->frame->mat.pos.y - ent->frame->oldmat.pos.y, 0.0f);
        }
    }

    PlayerLedgeUpdate(ent, sc, dt);

    // Compiled-out block, body reconstructed: the PS2 DWARF declares dposx/dposy/dposz here, and
    // retail's weak emission order needs an xEntGetPos reference at this point.
    if (0)
    {
        F32 dposx = xEntGetPos(ent)->x - ent->frame->oldmat.pos.x;
        F32 dposy = xEntGetPos(ent)->y - ent->frame->oldmat.pos.y;
        F32 dposz = xEntGetPos(ent)->z - ent->frame->oldmat.pos.z;
    }

    if (strcmp(astate->Name, "BbashStart01") == 0 || strcmp(astate->Name, "BbashStart02") == 0)
    {
        F32 animTimeStartDamage;
        F32 boundRadius;

        if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_BASH) > ePowerupLevelNormal)
        {
            animTimeStartDamage = 0.45f;
            boundRadius = 0.3f;
        }
        else
        {
            animTimeStartDamage = 0.35f;
            boundRadius = xglobals->___player_ent_dont_use_directly->bound.sph.r;
        }

        if (ent->model->Anim->Single->Time > animTimeStartDamage && ent->frame->vel.y >= 0.0f)
        {
            xBound punchBound;
            punchBound.type = XBOUND_TYPE_SPHERE;
            xSphere* punchSph = &punchBound.sph;

            xVec3 rhand;
            xModelTag* rhandTag = &sSpongeBobRHand;
            iModelTagEval(ent->model->Data, rhandTag, ent->model->Mat, &rhand);
            xVec3Copy(&punchSph->center, &rhand);
            punchSph->r = boundRadius;
            punchSph->center.y += 0.2f;

            if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_BASH) > ePowerupLevelNormal)
            {
                if (!sGauntletBoundHit)
                {
                    xQuickCullForBound(&punchBound.qcd, &punchBound);

                    Melee_cbData cbdata;
                    cbdata.ent = ent;
                    cbdata.zsc = (zScene*)sc;
                    cbdata.meleeB = &punchBound;
                    cbdata.hitsomething = 0;

                    xGridCheckPosition(&colls_grid, &punchSph->center, &punchBound.qcd,
                                       CheckObjectAgainstGauntlet, &cbdata);
                    xGridCheckPosition(&colls_oso_grid, &punchSph->center, &punchBound.qcd,
                                       CheckObjectAgainstGauntlet, &cbdata);
                    xGridCheckPosition(&npcs_grid, &punchSph->center, &punchBound.qcd,
                                       CheckObjectAgainstGauntlet, &cbdata);
                }
            }
            else
            {
                xQuickCullForBound(&punchBound.qcd, &punchBound);
                AttackBoundCollide(ent, (zScene*)sc, &punchBound, PLYR_ATAK_SB_BASH);
            }
        }

        xCollis* ceil = &ent->collis->colls[1];
        xEnt* destructent = (xEnt*)ceil->optr;
        bool destroyed = (destructent == NULL);

        if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_BASH) == ePowerupLevelNormal)
        {
            if (strcmp(astate->Name, "BbashStrike01") != 0)
            {
                if ((ceil->flags & 1) && destructent && (destructent->moreFlags & 0x10))
                {
                    if (destructent->baseType == eBaseTypePlatform && destructent->subType == 0xc &&
                        (((zPlatform*)destructent)->plat_asset->paddle.paddleFlags & 0x100))
                    {
                        xVec3 bashRay = { 0.0f, 1.0f, 0.0f };
                        zPlatform_PaddleCollide(ceil, &ent->bound.sph.center, &bashRay, 1);
                    }

                    F32 hitParams[4] = { 1.0f, 9.0f, 0.0f, 0.0f };
                    zEntEvent(destructent, eEventHit_BubbleBash, hitParams);
                }
                else if (ceil->flags & 1)
                {
                    xVec3 rhand;
                    xModelTag* rhandTag = &sSpongeBobRHand;
                    iModelTagEval(xglobals->___player_ent_dont_use_directly->model->Data, rhandTag,
                                  xglobals->___player_ent_dont_use_directly->model->Mat, &rhand);
                    zFX_SpawnBubbleSlam(&rhand, 24, 0.15f, 10.0f, 2.0f);
                }
            }

            if ((ceil->flags & 1) && destructent)
            {
                if (destructent->baseType == eBaseTypeBoulder)
                {
                    xEntBoulder* boul = (xEntBoulder*)destructent;
                    if (boul->basset->flags & 0x100)
                    {
                        zEntEvent(boul, eEventHit);
                    }
                    if (boul->basset->flags & 0x100)
                    {
                        zEntEvent(boul, eEventHit);
                    }

                    xVec3 f;
                    xVec3Init(&f, 0.0f, 25.0f, 0.0f);
                    xEntBoulder_AddForce(boul, &f);
                }
                else if (destructent->baseType == eBaseTypeButton)
                {
                    zEntButton_Press((_zEntButton*)destructent, 0x4);
                }
                else if (destructent->baseType == eBaseTypeNPC)
                {
                    zNMECommon* npc = (zNMECommon*)destructent;
                    npc->Damage(DMGTYP_BELOW, xglobals->___player_ent_dont_use_directly,
                                &ceil->tohit, -1, ePowerupLevelNormal);
                    if (!npc->IsHealthy())
                    {
                        destroyed = 1;
                    }
                }
                else if (destructent->baseType == eBaseTypeStatic)
                {
                    if (!(destructent->chkby & 0x10))
                    {
                        destroyed = 1;
                    }
                }
            }
        }

        if (destructent && (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_BASH) == ePowerupLevelNormal ||
                            destructent != zEntPlayerGetBBowl()))
        {
            if (destroyed)
            {
                ceil->flags &= ~1;
                ent->frame->vel.y = old_yvel;
            }
            else
            {
                bbash_hit = 1;
                ent->frame->vel.y = 0.0f;
            }
        }
    }

    if (gBashButton && !globals.player.ControlOff && (globals.pad0->released & XPAD_BUTTON_SQUARE))
    {
        gBashButton = 0;
    }

    if (gCurrentPlayer == eCurrentPlayerSpongeBob)
    {
        if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_BASH) > ePowerupLevelNormal)
        {
            if (strcmp(astate->Name, "BbashStart01") == 0 ||
                strcmp(astate->Name, "BbashStart02") == 0)
            {
                if (bbash_hit)
                {
                    xCollis* ceil = &ent->collis->colls[1];
                    xEnt* destructent = (xEnt*)ceil->optr;
                    if ((ceil->flags & 1) && destructent)
                    {
                        zEntPlayerGauntletSetup(destructent);
                        sGauntletStartHaz = 1;
                    }
                }

                if (sGauntletBoundHit)
                {
                    sGauntletStartHaz = 1;
                }
            }

            if (sGauntletStartHaz &&
                ((strcmp(astate->Name, "BbashStrike01") == 0 &&
                  xglobals->___player_ent_dont_use_directly->model->Anim->Single->Time > 0.1f) ||
                 sGauntletBoundHit))
            {
                xMat3x3Copy((xMat3x3*)sGauntletHaz->mdl_hazard->Mat,
                            (xMat3x3*)xglobals->___player_ent_dont_use_directly->model->Mat);

                xVec3 rhand;
                xModelTag* rhandTag = &sSpongeBobRHand;
                iModelTagEval(xglobals->___player_ent_dont_use_directly->model->Data, rhandTag,
                              xglobals->___player_ent_dont_use_directly->model->Mat, &rhand);

                HAZBang* hazbang = &sGauntletHaz->custdata.bang;
                if (hazbang->ent)
                {
                    xVec3 temp;
                    xVec3Copy(&temp, &rhand);

                    xVec3* entPos = xEntGetPos(hazbang->ent);
                    S32 ignoreYOffset = 0;
                    F32 yOffset = 0.0f;
                    F32 scaleFactor;

                    if (hazbang->ent->baseType == eBaseTypeNPC)
                    {
                        yOffset = -0.8f;
                        scaleFactor = 0.5f;
                    }
                    else if (hazbang->ent->baseType == eBaseTypeButton)
                    {
                        if (((_zEntButton*)hazbang->ent)->basset->buttonActFlags & 4)
                        {
                            yOffset = sGauntletTweakButtonY;
                        }
                        else
                        {
                            ignoreYOffset = 1;
                        }
                        scaleFactor = 0.3f;
                    }
                    else if (hazbang->ent->baseType == eBaseTypePlatform)
                    {
                        S32 isBashMech = 0;
                        for (S32 i = 0; i < hazbang->ent->linkCount; i++)
                        {
                            if (hazbang->ent->link[i].srcEvent == eEventHit_BubbleBash)
                            {
                                isBashMech = 1;
                            }
                        }

                        if (isBashMech)
                        {
                            yOffset = sGauntletTweakPlatBashY;
                        }
                        else
                        {
                            ignoreYOffset = 1;
                        }
                        scaleFactor = 0.0f;
                    }
                    else
                    {
                        ignoreYOffset = 1;
                        scaleFactor = 0.0f;
                    }

                    if (!ignoreYOffset)
                    {
                        temp.y = entPos->y + yOffset;
                    }

                    temp.x += scaleFactor * (entPos->x - temp.x);
                    temp.z += scaleFactor * (entPos->z - temp.z);

                    xMat4x3Tolocal(&hazbang->offset, (xMat4x3*)hazbang->ent->model->Mat, &temp);

                    xMat3x3 tmp_descaled;
                    F32 dummy;

                    xVec3NormalizeMacro(
                        &tmp_descaled.right,
                        &xglobals->___player_ent_dont_use_directly->frame->mat.right, &dummy);
                    xVec3NormalizeMacro(&tmp_descaled.up,
                                        &xglobals->___player_ent_dont_use_directly->frame->mat.up,
                                        &dummy);
                    xVec3NormalizeMacro(&tmp_descaled.at,
                                        &xglobals->___player_ent_dont_use_directly->frame->mat.at,
                                        &dummy);
                    xMat3x3GetEuler(&tmp_descaled, &hazbang->offsetRot);

                    xVec3NormalizeMacro(&tmp_descaled.right,
                                        (xVec3*)&hazbang->ent->model->Mat->right, &dummy);
                    xVec3NormalizeMacro(&tmp_descaled.up, (xVec3*)&hazbang->ent->model->Mat->up,
                                        &dummy);
                    xVec3NormalizeMacro(&tmp_descaled.at, (xVec3*)&hazbang->ent->model->Mat->at,
                                        &dummy);
                    xMat3x3GetEuler(&tmp_descaled, &hazbang->targetOffsetRot);

                    zFX_SpawnBubbleBlast(&rhand, sGauntletBubbleEffectNum,
                                         sGauntletBubbleEffectRadius, sGauntletBubbleEffectVel,
                                         sGauntletBubbleEffectRandVel);
                }

                sGauntletHaz->Start(&rhand, globals.player.g.BBashGauntletDelay);
                zEntPlayerFX::GauntletExplosion::Sparks(
                    *(xVec3*)&sGauntletHaz->mdl_hazard->Mat->pos);
                sGauntletStartHaz = 0;
                sGauntletBoundHit = 0;
                zEntPlayer_SNDPlay(ePlayerSnd_BubbleBashHitEnd, true);
            }
        }

        if (sGauntletHaz && sGauntletHaz->tmr_remain < globals.player.g.BBashGauntletDelay - 0.5f &&
            !globals.player.ControlOff && (globals.pad0->pressed & XPAD_BUTTON_SQUARE))
        {
            gBashButton = 1;
            zEntPlayerExplodeGauntlet();
        }

        if (sGauntletBlastRadius)
        {
            xBound gauntletBound;
            gauntletBound.type = XBOUND_TYPE_SPHERE;
            xSphere* gauntletSph = &gauntletBound.sph;
            xVec3Copy(&gauntletSph->center, (xVec3*)&sGauntletHaz->mdl_hazard->Mat->pos);
            gauntletSph->r = sGauntletBlastRadius;
            xQuickCullForBound(&gauntletBound.qcd, &gauntletBound);
            AttackBoundCollide(ent, (zScene*)sc, &gauntletBound, PLYR_ATAK_SB_BASH);
            zEntPlayerFX::GauntletExplosion::Explode(sGauntletHaz->pos_hazard);
        }
    }

    if (strcmp(astate->Name, "BbounceAttack01") == 0 ||
        strcmp(astate->Name, "BbounceStart01") == 0 || strcmp(astate->Name, "StunJump") == 0 ||
        strcmp(astate->Name, "StunJump02") == 0 || strcmp(astate->Name, "StunFall") == 0 ||
        strcmp(astate->Name, "StunFall02") == 0 || strcmp(astate->Name, "StunLand") == 0 ||
        strcmp(astate->Name, "StunLand02") == 0)
    {
        if (ent->frame->vel.y < 0.0f)
        {
            xBound flopBound;
            flopBound.type = XBOUND_TYPE_SPHERE;
            xSphere* flopSph = &flopBound.sph;
            xVec3Copy(&flopSph->center, xEntGetPos(xglobals->___player_ent_dont_use_directly));

            F32 scale = globals.player.PredictCurrVel / 5.0f;
            if (scale > 1.0f)
            {
                scale = 1.0f;
            }

            F32 xVelScaled = scale * globals.player.PredictCurrDir.x;
            F32 zVelScaled = scale * globals.player.PredictCurrDir.z;
            F32 cMaxOffset = 0.5f * xglobals->___player_ent_dont_use_directly->bound.sph.r;

            flopSph->r = xglobals->___player_ent_dont_use_directly->bound.sph.r - cMaxOffset;
            flopSph->center.x += xVelScaled * cMaxOffset;
            flopSph->center.z += zVelScaled * cMaxOffset;

            xQuickCullForBound(&flopBound.qcd, &flopBound);
            AttackBoundCollide(ent, (zScene*)sc, &flopBound, PLYR_ATAK_PAT_FLOP);
        }

        xCollis* floor = &ent->collis->colls[0];
        xEnt* destructent = (xEnt*)floor->optr;
        bool destroyed = (destructent == NULL);

        if (floor->flags & 1)
        {
            if (destructent && (destructent->moreFlags & 0x10))
            {
                if (destructent->baseType == eBaseTypePlatform && destructent->subType == 0xc &&
                    (((zPlatform*)destructent)->plat_asset->paddle.paddleFlags & 0x200))
                {
                    xVec3 bounceRay = { 0.0f, -1.0f, 0.0f };
                    zPlatform_PaddleCollide(floor, &ent->bound.sph.center, &bounceRay, 1);
                }

                if (strcmp(astate->Name, "StunJump") == 0 ||
                    strcmp(astate->Name, "StunJump02") == 0 ||
                    strcmp(astate->Name, "StunFall") == 0 ||
                    strcmp(astate->Name, "StunFall02") == 0 ||
                    strcmp(astate->Name, "StunLand") == 0 ||
                    strcmp(astate->Name, "StunLand02") == 0)
                {
                    zEntEvent(xglobals->___player_ent_dont_use_directly, destructent,
                              eEventHit_PatrickSlam);
                }
                else
                {
                    zEntEvent(xglobals->___player_ent_dont_use_directly, destructent,
                              eEventHit_BubbleBounce);
                }
            }

            zEntPlayerFX::SlamWarp::Update(dt);
        }
        else
        {
            zEntPlayerFX::SlamWarp::SetWarp(false);
        }

        if ((floor->flags & 1) && destructent)
        {
            if (destructent->baseType == eBaseTypeButton)
            {
                if (gCurrentPlayer == eCurrentPlayerSpongeBob)
                {
                    zEntButton_Press((_zEntButton*)destructent, 0x2);
                }
                else if (gCurrentPlayer == eCurrentPlayerPatrick)
                {
                    zEntButton_Press((_zEntButton*)destructent, 0x100);
                }
            }
            else if (destructent->baseType == eBaseTypeNPC)
            {
                zNMECommon* npc = (zNMECommon*)destructent;
                npc->Damage(DMGTYP_ABOVE, xglobals->___player_ent_dont_use_directly, &floor->tohit,
                            -1, ePowerupLevelNormal);
                if (!npc->IsHealthy())
                {
                    destroyed = 1;
                }
            }
            else if (destructent->baseType == eBaseTypeStatic)
            {
                F32 hitParams[4] = { 1.0f, 9.0f, 0.0f, 0.0f };
                zEntEvent(destructent, eEventHit, hitParams);
                if (!(destructent->chkby & 0x10))
                {
                    destroyed = 1;
                }
            }
        }

        if (destructent)
        {
            if (destroyed)
            {
                floor->flags &= ~1;
            }
            else
            {
                bbounce_hit = 1;
                ent->frame->vel.y = 0.0f;
            }
        }
    }

    if (gCartwheelButton && !globals.player.ControlOff &&
        (globals.pad0->released & XPAD_MOVE(XPAD_BUTTON_O)))
    {
        gCartwheelButton = 0;
    }

    if (strcmp(astate->Name, "Cartwheel01") == 0)
    {
        if (cartwheel_first &&
            xglobals->___player_ent_dont_use_directly->model->Anim->Single->Time > 0.47f)
        {
            cartwheel_first = 0;
        }

        if (zEntPlayer_PowerupLevel(PLYR_ATAK_PAT_CARTWHEEL) > ePowerupLevelNormal)
        {
            xBound cartwheelBound;
            cartwheelBound.type = XBOUND_TYPE_SPHERE;
            xSphere* cartwheelSph = &cartwheelBound.sph;
            xVec3Copy(&cartwheelSph->center,
                      xEntGetCenter(xglobals->___player_ent_dont_use_directly));
            cartwheelSph->r = 2.0f * (0.1f * globals.player.g.CartwheelDamageSize + 1.0f) *
                              xglobals->___player_ent_dont_use_directly->bound.sph.r;
            xQuickCullForBound(&cartwheelBound.qcd, &cartwheelBound);
            AttackBoundCollide(ent, (zScene*)sc, &cartwheelBound, PLYR_ATAK_PAT_CARTWHEEL);
            zEntPlayerFX::CartShield::SetVisible(1);
        }

        zEntPlayerFX::Ribbon::EnableRibbonCartWheel();

        xCollis* col = &ent->collis->colls[6];
        xEnt* destructent = (xEnt*)col->optr;
        bool destroyed = (destructent == NULL);
        S32 hit = 0;

        if ((col->flags & 1) && destructent && (destructent->moreFlags & 0x10))
        {
            if (destructent->baseType == eBaseTypeButton)
            {
                zEntButton_Press((_zEntButton*)destructent, 0x20000);
            }
            else if (destructent->baseType == eBaseTypeNPC)
            {
                if (destructent != contactEnt)
                {
                    hit = 1;
                }

                zNMECommon* npc = (zNMECommon*)destructent;
                npc->Damage(DMGTYP_CARTWHEEL, xglobals->___player_ent_dont_use_directly,
                            &col->tohit, -1, ePowerupLevelNormal);
                if (!npc->IsHealthy())
                {
                    destroyed = 1;
                }
            }
            else if (destructent->baseType == eBaseTypeStatic)
            {
                if (destructent != contactEnt)
                {
                    hit = 1;
                }

                F32 hitParams[4] = { 1.0f, 9.0f, 0.0f, 0.0f };
                zEntEvent(destructent, eEventHit, hitParams);
                if (!(destructent->chkby & 0x10))
                {
                    destroyed = 1;
                }
            }
        }

        if (destructent && destroyed)
        {
            col->flags &= ~1;
        }

        if (zEntPlayer_PowerupLevel(PLYR_ATAK_PAT_CARTWHEEL) <= ePowerupLevelNormal)
        {
            cartwheel_hit = hit;
        }
    }
    else
    {
        zEntPlayerFX::CartShield::SetVisible(0);
    }

    zEntPlayerFX::CartShield::Update(dt);
    zEntPlayerFX::HammerThrow::Update(dt);
    zEntPlayerFX::MotionBlur::Update(dt);

    if (sRingDelay >= 1.1f)
    {
        F32 stunlerp = MIN(1.0f, 1.25f * (2.0f - sRingDelay));
        zNMEMsg_AreaPlayerStun(5.0f, 5.0f * stunlerp + (1.0f - stunlerp), NULL);
    }

    zEntPlayerFloorUpdate(ent, sc, dt);
    PlayerTeeterCheck(ent, sc, dt);
    zEntPlayerSurfDamageUpdate(ent, sc, dt);
    zEntPlayerJumpUpdate(ent, sc, dt);
    zEntPlayerTSlideUpdate(ent, sc, dt);
    zEntPlayerCollTrigger(ent, sc);

    if (globals.player.RootUp.y != globals.player.RootUpTarget.y || 1.0f != globals.player.RootUp.y)
    {
        xVec3 delta;
        xVec3Sub(&delta, &globals.player.RootUpTarget, &globals.player.RootUp);

        F32 mag = xVec3Dot(&delta, &delta);
        F32 lerpspeed;

        if (globals.player.HangElapsed <= 0.25f)
        {
            lerpspeed = 0.07f;
        }
        else if (globals.player.HangElapsed >= 0.5f)
        {
            lerpspeed = 0.5f;
        }
        else
        {
            lerpspeed = 1.72f * (globals.player.HangElapsed - 0.25f);
        }

        if (mag < lerpspeed * lerpspeed)
        {
            globals.player.RootUp = globals.player.RootUpTarget;
        }
        else
        {
            mag = xsqrt(mag);
            xVec3SMul(&delta, &delta, 1.0f / mag * lerpspeed);
            xVec3Add(&globals.player.RootUp, &globals.player.RootUp, &delta);
            xVec3Normalize(&globals.player.RootUp, &globals.player.RootUp);
        }
    }

    zEntPlayerVelUpdate(ent, sc, dt);

    ent->frame->oldmat.pos.y -= sPlayerCollAdjust;
    ent->frame->mat.pos.y -= sPlayerCollAdjust;

    xEntEndUpdate(ent, sc, dt);

    static U32 streakBubbleSpinID = 0xDEAD;

    if (strncmp(astate->Name, "Bspin", 5) == 0 || strncmp(astate->Name, "Melee", 5) == 0)
    {
        xModelTag* rhtag;
        xModelTag* lhtag;
        F32 radius;
        en_plyratak attackType;
        F32 reach;

        if (gCurrentPlayer == eCurrentPlayerSpongeBob)
        {
            attackType = PLYR_ATAK_SB_KARATE;
            rhtag = &sSpongeBobRHand;
            lhtag = &sSpongeBobLHand;
        }
        else if (gCurrentPlayer == eCurrentPlayerPatrick)
        {
            attackType = PLYR_ATAK_PAT_BELLY;
            rhtag = &sPatrickRHand;
            lhtag = &sPatrickLHand;
        }

        if (zEntPlayer_PowerupLevel(attackType) == ePowerupLevelNormal)
        {
            reach = globals.player.g.BSpinReach;
            radius = globals.player.g.BSpinRadius;
        }
        else if (zEntPlayer_PowerupLevel(attackType) > ePowerupLevelNormal)
        {
            reach = globals.player.g.BSpinReachUpgrade;
            radius = 0.3f + globals.player.g.BSpinRadius;
        }

        xVec3 rhand;
        xVec3 lhand;
        xBound rhandB;
        xBound lhandB;

        rhandB.type = XBOUND_TYPE_SPHERE;
        lhandB.type = XBOUND_TYPE_SPHERE;
        xSphere* rightSph = &rhandB.sph;
        xSphere* leftSph = &lhandB.sph;

        iModelTagEval(ent->model->Data, rhtag, ent->model->Mat, &rhand);
        iModelTagEval(ent->model->Data, lhtag, ent->model->Mat, &lhand);

        F32 posRadius = reach - radius;
        xVec3* playerPos = xEntGetCenter(xglobals->___player_ent_dont_use_directly);
        F32 height = (rhand.y + lhand.y) / 2.0f;

        xVec3Copy(&rightSph->center, &rhand);
        rightSph->r = radius;
        xVec3Copy(&leftSph->center, &lhand);
        leftSph->r = radius;

        xVec3 leftRibbonPos;
        xVec3 rightRibbonPos;
        xVec3 toRhand;
        xVec3 toLhand;

        xVec3Sub(&toRhand, &rhand,
                 (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos);
        toRhand.y = 0.0f;
        xVec3Normalize(&toRhand, &toRhand);
        rightRibbonPos = toRhand;
        xVec3SMulBy(&toRhand, posRadius);
        rightRibbonPos *= 0.3f + posRadius;
        xVec3Add(&rightSph->center, playerPos, &toRhand);
        rightRibbonPos += *playerPos;

        xVec3Sub(&toLhand, &lhand,
                 (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos);
        toLhand.y = 0.0f;
        xVec3Normalize(&toLhand, &toLhand);
        leftRibbonPos = toLhand;
        xVec3SMulBy(&toLhand, posRadius);
        leftRibbonPos *= 0.3f + posRadius;
        xVec3Add(&leftSph->center, playerPos, &toLhand);
        leftRibbonPos += *playerPos;

        if (height - radius < 0.01f + xglobals->___player_ent_dont_use_directly->model->Mat->pos.y)
        {
            height =
                radius + (0.01f + xglobals->___player_ent_dont_use_directly->model->Mat->pos.y);
        }

        rightSph->center.y = height;
        leftSph->center.y = height;
        rightRibbonPos.y = height;
        leftRibbonPos.y = height;

        if (globals.player.IsBubbleSpinning)
        {
            xQuickCullForBound(&rhandB.qcd, &rhandB);
            AttackBoundCollide(ent, (zScene*)sc, &rhandB, attackType);
            xQuickCullForBound(&lhandB.qcd, &lhandB);
            AttackBoundCollide(ent, (zScene*)sc, &lhandB, attackType);
        }

        static F32 aTime = 0.48f;

        if (!single->Blend->State && single->Time < aTime)
        {
            if (gCurrentPlayer == eCurrentPlayerSpongeBob)
            {
                zEntPlayerFX::Ribbon::EnableRibbonKarateSpin(rightRibbonPos);
            }
            else if (gCurrentPlayer == eCurrentPlayerPatrick)
            {
                zEntPlayerFX::Ribbon::EnableRibbonMelee(leftRibbonPos, rightRibbonPos);
            }
        }
    }

    xVec3Sub(&lastDeltaPos, &ent->frame->mat.pos, &ent->frame->oldmat.pos);

    if (surfSlickRatio)
    {
        lastFloorNorm = globals.player.floor_norm;
        lastFloorEnt = (xEnt*)ent->collis->colls[0].optr;
        if (lastFloorEnt && lastFloorEnt->id != xStrHash("slipdeck01") &&
            lastFloorEnt->id != xStrHash("slipdeck02") &&
            lastFloorEnt->id != xStrHash("slipdeck03") &&
            lastFloorEnt->id != xStrHash("slipdeck04"))
        {
            lastFloorEnt = NULL;
        }
    }

    if (globals.player.carry.grabbed)
    {
        if (globals.player.carry.grabbed->baseType == eBaseTypeBoulder)
        {
            xEntBoulder* boul = (xEntBoulder*)globals.player.carry.grabbed;
            xVec3Init(&boul->vel, 0.0f, 0.0f, 0.0f);
            boul->angVel = 0.0f;
            boul->chkby = 0;
        }

        xAnimSingle* playerAnim = ent->model->Anim->Single;
        char tmpStateName[256];
        strcpy(tmpStateName, playerAnim->State->Name);
        strcat(tmpStateName, "Item");

        xAnimState* itemAnim = xAnimTableGetState(ent->model->Anim->Table, tmpStateName);
        if (itemAnim)
        {
            xVec3 tmptran;
            xQuat tmpquat;
            iAnimEval(itemAnim->Data->RawData[0], 0.03336667f + ent->model->Anim->Single->Time, 1,
                      &tmptran, &tmpquat);
            tmpquat.s *= -1.0f;

            xMat4x3 objMat;
            xQuatToMat(&tmpquat, &objMat);
            objMat.pos = tmptran;

            xEntBoulder* boul;
            xVec3 rotatedLC;
            if (globals.player.carry.grabbed->baseType == eBaseTypeBoulder)
            {
                boul = (xEntBoulder*)globals.player.carry.grabbed;
                xMat3x3RMulVec(&rotatedLC, (xMat3x3*)boul->model->Mat, &boul->localCenter);
            }

            if (globals.player.carry.grabTarget)
            {
                xMat4x3 targetMat;
                xMat3x3Rot(&targetMat, &g_Y3, globals.player.carry.targetRot);
                targetMat.pos = ent->frame->mat.pos;
                xMat4x3Mul(&objMat, &objMat, &targetMat);

                if (globals.player.carry.grabLerpMin < globals.player.carry.grabLerpMax &&
                    globals.player.carry.grabLerpLast < globals.player.carry.grabLerpMax)
                {
                    F32 lerp =
                        (globals.player.carry.grabLerpLast - globals.player.carry.grabLerpMin) /
                        (globals.player.carry.grabLerpMax - globals.player.carry.grabLerpMin);
                    if (lerp <= 0.0f)
                    {
                        objMat.pos.x -= globals.player.carry.grabOffset.x;
                        objMat.pos.z -= globals.player.carry.grabOffset.z;
                    }
                    else
                    {
                        lerp = 1.0f - lerp;
                        objMat.pos.x -= globals.player.carry.grabOffset.x * lerp;
                        objMat.pos.z -= globals.player.carry.grabOffset.z * lerp;
                    }
                }

                if (!globals.player.carry.grabYclear)
                {
                    F32 bottom;
                    if (globals.player.carry.grabbed->baseType == eBaseTypeBoulder)
                    {
                        bottom = boul->model->Mat->pos.y + rotatedLC.y - boul->bound.sph.r;
                    }
                    else
                    {
                        bottom = globals.player.carry.grabbed->model->Mat->pos.y;
                    }

                    if (bottom > objMat.pos.y)
                    {
                        objMat.pos.y = bottom;
                    }
                    else
                    {
                        globals.player.carry.grabYclear = 1;
                    }
                }
            }
            else
            {
                xMat4x3Mul(&objMat, &objMat, &ent->frame->mat);
            }

            if (globals.player.carry.grabbed->baseType == eBaseTypeNPC)
            {
                CarriedNME_RemoveSpin(&globals.player.carry.spin, dt,
                                      (zNMECommon*)globals.player.carry.grabbed, itemAnim);
            }

            xMat4x3Mul(&objMat, &globals.player.carry.spin, &objMat);

            if (globals.player.carry.grabbed->baseType == eBaseTypeBoulder)
            {
                objMat.pos.y += boul->bound.sph.r;
                xVec3Copy(&boul->bound.sph.center, &objMat.pos);
                xMat3x3RMulVec(&rotatedLC, &objMat, &boul->localCenter);
                xVec3SubFrom(&objMat.pos, &rotatedLC);
            }

            *globals.player.carry.grabbed->model->Mat = *(RwMatrixTag*)&objMat;
            if (globals.player.carry.grabbed->frame)
            {
                globals.player.carry.grabbed->frame->mat = objMat;
            }
        }
        else if (strcmp(playerAnim->State->Name, "Carry_HammerThrow") != 0)
        {
            globals.player.carry.grabbed->chkby |= 0x10;
            if (globals.player.carry.grabbed->baseType == eBaseTypeNPC)
            {
                zNMECommon* npc = (zNMECommon*)globals.player.carry.grabbed;
                if (npc->SelfType() >= NME_TYPE_TIKI && npc->SelfType() <= NME_TYPE_TIKI_STONE &&
                    !npc->IsHealthy())
                {
                    globals.player.carry.grabbed->chkby = 0;
                }
            }
        }

        if (strcmp(playerAnim->State->Name, "Carry_Throw") == 0 ||
            strcmp(playerAnim->State->Name, "Carry_HammerStart") == 0 ||
            strcmp(playerAnim->State->Name, "Carry_HammerSpin") == 0)
        {
            if (globals.player.g.PowerUp[7] > 1)
            {
                if (!globals.player.ControlOff && (globals.pad0->released & XPAD_BUTTON_R1))
                {
                    hammer_button = 0;
                }
            }
            else
            {
                hammer_button = 0;
            }
        }

        if (strcmp(playerAnim->State->Name, "Carry_Throw") == 0)
        {
            gThrowPower = 0.0f;
            if (playerAnim->Time >= 0.3f)
            {
                if (!hammer_button)
                {
                    StartThrow();
                    zEntPlayer_SNDPlay(ePlayerSnd_Throw, true);
                    zEntPlayer_SNDPlayStream(ePlayerStreamSnd_PatThrow);
                    sPatCarryCommentTimer.TimerStop();
                }
                else
                {
                    hammer_start = 1;
                }
            }
        }

        if (strcmp(playerAnim->State->Name, "Carry_HammerSpin") == 0)
        {
            if (hammer_first && playerAnim->Time < sLastHammerTime)
            {
                hammer_first = 0;
            }

            sLastHammerTime = playerAnim->Time;
            sThrowChargeTime += dt;
            if (sThrowChargeTime > 2.2f)
            {
                sThrowChargeTime = 2.2f;
            }

            if (!hammer_first && globals.player.g.PowerUp[7] > 1 &&
                (!hammer_button || 2.2f == sThrowChargeTime))
            {
                hammer_throw = 1;
                zEntPlayerFX::HammerThrow::SetDust(false);
                zEntPlayerFX::HammerThrow::SetBlur(true);
            }

            playerAnim->CurrentSpeed += 1.2f * dt;
            gThrowPower = (0.0f == sThrowChargeTime) ? 0.0f : sThrowChargeTime / 2.2f;
            zEntPlayer_SNDSetPitch(ePlayerSnd_HammerThrowLoop, 4.0f * sThrowChargeTime + 1.0f);
        }

        if (strcmp(playerAnim->State->Name, "Carry_HammerThrow") == 0 && playerAnim->Time > 0.0f)
        {
            StartThrow();
        }
    }

    if (globals.player.ControlOff && globals.player.ControlOnEvent)
    {
        zEntPlayerControlOn(CONTROL_OWNER_EVENT);
        globals.player.ControlOnEvent = 0;
    }

    RwMatrixTag rootOldMat = *ent->model->Mat;

    if (1.0f != globals.player.RootUp.y)
    {
        xMat4x3 tmpMat;
        tmpMat.up = globals.player.RootUp;

        xVec3 ax;
        xVec3Cross(&ax, &globals.player.RootUp, (xVec3*)&ent->model->Mat->up);

        F32 crs = xVec3Normalize(&ax, &ax);
        F32 dot = xVec3Dot((xVec3*)&ent->model->Mat->up, &globals.player.RootUp);

        if (0.0f == crs)
        {
            if (dot < 0.0f)
            {
                xVec3Inv(&tmpMat.at, (xVec3*)&ent->model->Mat->at);
            }
        }
        else
        {
            F32 rads = xasin(crs);
            if (dot < 0.0f)
            {
                rads = PI - rads;
            }

            xMat3x3 rotMat;
            xMat3x3Rot(&rotMat, &ax, rads);
            xMat3x3LMulVec(&tmpMat.right, &rotMat, (xVec3*)&ent->model->Mat->right);
            xMat3x3LMulVec(&tmpMat.at, &rotMat, (xVec3*)&ent->model->Mat->at);
        }

        tmpMat.pos.x = 0.0f;

        if (globals.player.HangEnt)
        {
            tmpMat.pos.y = -ent->bound.sph.r;
        }
        else
        {
            tmpMat.pos.y = 0.0f;
        }

        tmpMat.pos.z = 0.0f;

        xMat3x3RMulVec(&tmpMat.pos, &tmpMat, &tmpMat.pos);

        tmpMat.pos.x = tmpMat.pos.x + ent->model->Mat->pos.x;

        if (globals.player.HangEnt)
        {
            tmpMat.pos.y = tmpMat.pos.y + (ent->model->Mat->pos.y + ent->bound.sph.r);
        }
        else
        {
            tmpMat.pos.y = tmpMat.pos.y + ent->model->Mat->pos.y;
        }

        tmpMat.pos.z = tmpMat.pos.z + ent->model->Mat->pos.z;

        if (globals.player.HangEnt && globals.player.HangStartLerp < 1.0f)
        {
            tmpMat.pos.x =
                globals.player.HangStartLerp * (tmpMat.pos.x - globals.player.HangStartPos.x) +
                globals.player.HangStartPos.x;
            tmpMat.pos.y =
                globals.player.HangStartLerp * (tmpMat.pos.y - globals.player.HangStartPos.y) +
                globals.player.HangStartPos.y;
            tmpMat.pos.z =
                globals.player.HangStartLerp * (tmpMat.pos.z - globals.player.HangStartPos.z) +
                globals.player.HangStartPos.z;
        }

        tmpMat.flags = 0;

        *(xMat4x3*)ent->model->Mat = tmpMat;
        rendermat = tmpMat;
    }
    else
    {
        rendermat = *(xMat4x3*)ent->model->Mat;
    }

    zEntPlayer_Update_HitchSpinAndFade(dt);

    if (ent->model == globals.player.model_patrick)
    {
        if (!globals.player.carry.grabbed)
        {
            if (!(sLasso->flags & 1))
            {
                sLasso->tipLerp = 0.0f;
                sLasso->tipDelay = 0.0f;
                zPatTongue_PopOff();
            }

            if (sLasso->flags & 1)
            {
                if (sLassoInfo->swingTarget)
                {
                    if (strcmp(ent->model->Anim->Single->State->Name, "LassoSwingCatch02") == 0)
                    {
                        if (sLasso->flags & 0x800)
                        {
                            xVec3Sub(&sLasso->crCenter,
                                     xBoundCenter(&sLassoInfo->swingTarget->bound),
                                     (xVec3*)&ent->model->Mat->pos);
                            xVec3AddScaled(&sLasso->crCenter,
                                           (xVec3*)&sLassoInfo->swingTarget->model->Mat->up, -0.6f);
                        }
                        else
                        {
                            xVec3Copy(&sLasso->tgCenter,
                                      xBoundCenter(&sLassoInfo->swingTarget->bound));
                            xVec3AddScaled(&sLasso->tgCenter,
                                           (xVec3*)&sLassoInfo->swingTarget->model->Mat->up, -0.6f);
                        }
                    }
                    else if (strcmp(ent->model->Anim->Single->State->Name, "LassoSwing") == 0)
                    {
                        xVec3Sub(&sLasso->crCenter, xBoundCenter(&sLassoInfo->swingTarget->bound),
                                 (xVec3*)&ent->model->Mat->pos);
                        xVec3AddScaled(&sLasso->crCenter,
                                       (xVec3*)&sLassoInfo->swingTarget->model->Mat->up, -0.6f);
                    }
                }

                zLasso_Update(sLasso, ent, dt);
            }

            if (gReticleTarget[1] && gReticleTarget[1]->baseType == eBaseTypeNPC)
            {
                zNMECommon* npc = (zNMECommon*)gReticleTarget[1];
                if (!npc->CanRope())
                {
                    gReticleTarget[1] = NULL;
                    sTimeToRetarget[1] = 0.0f;
                }
            }

            if (gReticleTarget[1])
            {
                xVec3 disp;
                xVec3Sub(&disp, (xVec3*)&gReticleTarget[1]->model->Mat->pos,
                         (xVec3*)&ent->model->Mat->pos);
                if (xVec3Length2(&disp) > 100.0f)
                {
                    gReticleTarget[1] = NULL;
                    sTimeToRetarget[1] = 0.0f;
                }
            }

            // Retail bug: tests the array (always non-NULL) instead of gReticleTarget[1].
            if (!globals.player.ControlOff && (globals.pad0->pressed & XPAD_BUTTON_R1) &&
                !gReticleTarget)
            {
                sTimeToRetarget[1] = 0.0f;
            }

            sTimeToRetarget[1] -= dt;

            if ((ent->model->Anim->Single->State->UserFlags & 0x1000) && gReticleTarget[1] &&
                sTypeOfTarget[1] != 1)
            {
                gReticleTarget[1] = NULL;
                sTimeToRetarget[1] = 0.25f;
            }

            if (sTimeToRetarget[1] < 0.0f && (!(sLasso->flags & 1) || sLassoInfo->swingTarget))
            {
                xEnt* oldTarget = gReticleTarget[1];
                sTimeToRetarget[1] = 0.25f;

                S32 canSwing = globals.player.JumpState != 0;
                if (!oldTarget)
                {
                    canSwing = 1;
                }

                if (canSwing)
                {
                    // not in the DWARF: len2 holds the length across the dot-product call;
                    // declared before closestDist_sqr for retail's registers
                    F32 len2;
                    F32 closestDist_sqr = 100.0f;
                    xEnt* closest = NULL;
                    for (S32 i = 0; i < sNumHitches; i++)
                    {
                        xEnt* targent = sHitch[i];
                        if (sLassoInfo->swingTarget != targent && xEntIsVisible(targent))
                        {
                            xVec3 toTarget;
                            xVec3Sub(&toTarget, (xVec3*)&targent->model->Mat->pos,
                                     (xVec3*)&ent->model->Mat->pos);
                            if (globals.player.JumpState != 0 || toTarget.y > 1.0f)
                            {
                                len2 = xVec3Length2(&toTarget);
                                if (len2 >= closestDist_sqr)
                                {
                                    continue;
                                }

                                if (xVec3Dot(&toTarget, (xVec3*)&ent->model->Mat->at) > 0.0f)
                                {
                                    closestDist_sqr = len2;
                                    closest = targent;
                                }
                            }
                        }
                    }

                    if (closest)
                    {
                        if (!gReticleTarget[1])
                        {
                            xprintf("Found Lasso Target\n");
                        }

                        gReticleTarget[1] = closest;
                        sTypeOfTarget[1] = 1;
                    }
                }

                if (gReticleTarget[1] != oldTarget)
                {
                    sReticleAlpha[1] = 0.0f;
                }
            }
        }
        else
        {
            gReticleTarget[1] = NULL;
        }

        if (strcmp(ent->model->Anim->Single->State->Name, "DJumpApex01") != 0)
        {
            sShouldMelee = 0;
        }

        if (!(sLasso->flags & 1))
        {
            sLassoInfo->swingTarget = NULL;

            if (!globals.player.ControlOff && (globals.pad0->pressed & XPAD_BUTTON_SQUARE))
            {
                if (gReticleTarget[1])
                {
                    xVec3 disp;
                    xVec3Sub(&disp, (xVec3*)&gReticleTarget[1]->model->Mat->pos,
                             (xVec3*)&ent->model->Mat->pos);
                    F32 dist = xVec3Length(&disp);
                    switch (sTypeOfTarget[1])
                    {
                    case 1:
                        sLassoInfo->swingTarget = gReticleTarget[1];
                        sLassoInfo->dist = dist / 4.0f;
                        break;
                    }
                }
            }
            else if (!globals.player.ControlOff &&
                     (globals.pad0->pressed & XPAD_MOVE(XPAD_BUTTON_TRIANGLE)) &&
                     strcmp(ent->model->Anim->Single->State->Name, "LedgeGrab01") != 0 &&
                     !zEntTeleportBox_playerIn())
            {
                sShouldMelee = 1;
            }
        }
    }

    zEntPlayer_StreakFX(ent, dt);

    *ent->model->Mat = rootOldMat;

    if (dt)
    {
        tslide_lastrealvel.x = (ent->model->Mat->pos.x - ent->frame->oldmat.pos.x) / dt;
        tslide_lastrealvel.y = (ent->model->Mat->pos.y - ent->frame->oldmat.pos.y) / dt;
        tslide_lastrealvel.z = (ent->model->Mat->pos.z - ent->frame->oldmat.pos.z) / dt;
    }

    if (g_doPlayerUpdateAgainDueToPlayerSwitch)
    {
        g_doPlayerUpdateAgainDueToPlayerSwitch = 0;
        zEntPlayer_Update(ent, sc, dt);
    }
}

void zEntPlayerGauntletBlastRadius(F32 radius)
{
    sGauntletBlastRadius = radius;
}

// The register order here depends on zEntPlayer_MoveInfo's repeated 0x1e mask (see there).
void zEntPlayer_CheckCritterContact(xEnt* player, F32 dt)
{
    S32 i;
    xEntCollis* plyrcol = player->collis;

    if (globals.player.DamageTimer > 0.0f)
    {
        return;
    }

    static U32 hashes_ss[3] = { xStrHash("BBounceStrike01"), xStrHash("BBounceAttack01"),
                                xStrHash("BBounceStrike01") };
    static U32 hashes_pa[6] = { xStrHash("StunJump"), xStrHash("StunJump02"),
                                xStrHash("StunFall"), xStrHash("StunFall02"),
                                xStrHash("StunLand"), xStrHash("StunLand02") };
    static U32 hashes_cartwheel[2] = { xStrHash("Cartwheel01"), xStrHash("CartwheelEnd01") };

    for (i = plyrcol->npc_sidx; i < plyrcol->npc_eidx; i++)
    {
        xCollis* colrec = &plyrcol->colls[i];

        if (colrec->optr == NULL)
        {
            continue;
        }

        zNMECommon* npc = (zNMECommon*)colrec->optr;

        if (npc->baseType != eBaseTypeNPC)
        {
            continue;
        }

        en_npctyp ntyp = npc->SelfType();

        if (ntyp >= NME_TYPE_CRITTER && ntyp <= NME_TYPE_BUCKETJELLY)
        {
            U32 anid_player =
                xglobals->___player_ent_dont_use_directly->model->Anim->Single->State->ID;
            S32 found = 0;

            if (gCurrentPlayer == eCurrentPlayerPatrick)
            {
                for (S32 k = 0; k < 2; k++)
                {
                    if (anid_player == hashes_cartwheel[k])
                    {
                        found = 1;
                        break;
                    }
                }
            }

            if (!found)
            {
                zEntPlayer_DamageNPCKnockBack(npc, 1, (xVec3*)&npc->model->Mat->pos,
                                              ePlayerNMEDamageDefault);
                zNMEMsg_SendMsg(NME_MID_UNDERATTACK, npc, -1.0f);
                return;
            }
        }
        else if (ntyp == NME_TYPE_SPINNER_V1 || ntyp == NME_TYPE_SPINNER_V2 ||
                 ntyp == NME_TYPE_SPINNER_V3)
        {
            if (xVec3Dot(&g_NY3, &colrec->hdng) < 0.86f)
            {
                continue;
            }

            xPsyche* psy = npc->psy_self;

            if (psy->GIDOfActive() != NME_GOAL_SPINNER_BATTLE)
            {
                continue;
            }

            F32 tym_inGoal = psy->TimerGet(XPSY_TYMR_CURGOAL);

            if (tym_inGoal < 0.35f)
            {
                continue;
            }

            U32 anid_player =
                xglobals->___player_ent_dont_use_directly->model->Anim->Single->State->ID;
            S32 found = 0;

            if (gCurrentPlayer == eCurrentPlayerSpongeBob)
            {
                for (S32 k = 0; k < 3; k++)
                {
                    if (anid_player == hashes_ss[k])
                    {
                        found = 1;
                        break;
                    }
                }
            }
            else if (gCurrentPlayer == eCurrentPlayerPatrick)
            {
                for (S32 k = 0; k < 6; k++)
                {
                    if (anid_player == hashes_pa[k])
                    {
                        found = 1;
                        break;
                    }
                }
            }

            if (!found)
            {
                zEntPlayer_DamageNPCKnockBack(npc, 1, (xVec3*)&npc->model->Mat->pos,
                                              ePlayerNMEDamageDefault);
                return;
            }
        }
        else if ((ntyp >= NME_TYPE_STANDARD && ntyp <= NME_TYPE_MERVYN_V3) ||
                 (ntyp >= NME_TYPE_MINDY && ntyp <= NME_TYPE_PATRICK) ||
                 (ntyp >= NME_TYPE_BUCKOTRON_V1 && ntyp <= NME_TYPE_BUCKOTRON_V7) ||
                 (ntyp >= NME_TYPE_CRITTER && ntyp <= NME_TYPE_BUCKETJELLY))
        {
            if (xVec3Dot(&g_NY3, &colrec->hdng) < 0.65f)
            {
                continue;
            }

            U32 mvinf = zEntPlayer_MoveInfo();

            if (!(mvinf & 0x1))
            {
                continue;
            }

            xVec3 dir_push;

            // not in the DWARF: the squared distance is computed once and reused for xsqrt
            F32 dsq = npc->XZDstSqToPlayer(&dir_push, NULL);

            if (dsq > 0.00001f)
            {
                dir_push /= xsqrt(dsq);
            }
            else
            {
                dir_push = *(xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->right;
            }

            xVec3 vel_push = dir_push * 3.0f * dt;

            player->frame->mat.pos += vel_push;
            player->frame->mode |= 0x1;
        }
    }
}

void zEntPlayer_PatrickLaunch(xEnt* patLauncher)
{
    globals.player.carry.patLauncher = patLauncher;
}

void zEntPlayer_ShadowModelEnable()
{
    if (xglobals->___player_ent_dont_use_directly->model == globals.player.model_spongebob &&
        gCurrentCostume != COSTUME_SB_X)
    {
        globals.player.sb_models[10]->Flags |= 1;
        globals.player.sb_models[0]->Flags &= 0xfffe;

        xModelInstance* modelInst = xglobals->___player_ent_dont_use_directly->model;
        xBox* combinedAnimBound = &modelInst->combinedAnimBound;
        combinedAnimBound->upper.x = -1e38f;

        while (modelInst)
        {
            if (modelInst->Flags & 1)
            {
                iModelAnimBoundWorld(&modelInst->animBound, combinedAnimBound, modelInst->Data,
                                     modelInst->Mat, &modelInst->Mat[1]);
            }
            modelInst = modelInst->Next;
        }
    }
}

void zEntPlayer_ShadowModelDisable()
{
    if (xglobals->___player_ent_dont_use_directly->model == globals.player.model_spongebob &&
        gCurrentCostume != COSTUME_SB_X)
    {
        globals.player.sb_models[0]->Flags |= 1;
        globals.player.sb_models[10]->Flags &= 0xfffe;
    }
}

static void zEntPlayerDirectionDecalRender(RwMatrixTag* Mat, RwRaster* rwras, F32 factor,
                                           F32 distance)
{
    xVec3 center;
    xMat4x3 matrix;
    xShadowCache cache;
    S32 i;
    xEnt* ep;
    S32 rc;

    xVec3Copy(&center, (xVec3*)&Mat->pos);
    xVec3AddScaled(&center, (xVec3*)&Mat->up, 2.0f);
    xVec3AddScaled(&center, (xVec3*)&Mat->at, distance + factor);

    xVec3Copy(&matrix.pos, &center);
    xVec3Copy(&matrix.right, (xVec3*)&Mat->right);
    xVec3Copy(&matrix.up, (xVec3*)&Mat->at);
    xVec3Inv(&matrix.at, (xVec3*)&Mat->up);

    xVec3SMulBy(&matrix.right, 1.0f);
    xVec3SMulBy(&matrix.up, 1.0f);
    xVec3SMulBy(&matrix.at, 1.0f);
    matrix.flags = 0;

    gShadowObjectRadius = 0.5f * factor + 0.7f;
    xShadowVertical_FillCache(&cache, &center, gShadowObjectRadius, 6.0f, 0.65f);
    xShadowVertical_DrawCache(&cache, factor, 0.0f, 1, (RwMatrixTag*)&matrix, rwras);

    for (i = 0; i < cache.entCount; i++)
    {
        ep = cache.ent[i];
        if (!zThrown_IsFruit(ep, NULL))
        {
            rc = xShadowReceiveShadowSetup(ep);
            if (rc)
            {
                xShadowReceiveShadow(ep, factor, 1, (RwMatrixTag*)&matrix, rwras);
            }
        }
    }
}

static void zEntPlayer_BubbleBowlLaneRender(zEnt* ent)
{
    RwMatrixTag* Mat;
    F32 factor;
    xRay3 ray;
    xCollis coll;
    xEnt* ep;
    U8 alpha;
    _SDRenderState old_rendstat;

    Mat = ent->model->Mat;
    factor = 1.75f * sBubbleBowlTimer;

    if (factor < 0.0f)
    {
        factor = 0.0f;
    }

    if (factor > 1.0f)
    {
        factor = 1.0f;
    }

    static S32 sCount = -1;
    static U8 sRenderDecal = TRUE;

    if (sCount < 0)
    {
        sCount = 5;

        ray.dir.x = 0.0f;
        ray.dir.y = -1.0f;
        ray.dir.z = 0.0f;
        xVec3AddScaled(ray.origin, *(xVec3*)&ent->model->Mat->pos, *(xVec3*)&ent->model->Mat->at,
                       5.0f);
        ray.origin.y += 2.0f;
        ray.min_t = 0.0f;
        ray.max_t = 4.0f;

        xRayHitsScene(globals.sceneCur, &ray, &coll);

        if (!(coll.flags & 1))
        {
            sRenderDecal = FALSE;
        }
        else
        {
            ep = (xEnt*)coll.optr;
            if (ep == NULL)
            {
                sRenderDecal = TRUE;
            }
            else if (!(ep->baseFlags & 0x10))
            {
                sRenderDecal = FALSE;
            }
            else
            {
                // not in the DWARF: out parameter of zGooIs (needs an address).
                F32 depth;
                if (zGooIs(ep, depth, 0))
                {
                    sRenderDecal = FALSE;
                }
                else
                {
                    sRenderDecal = TRUE;
                }
            }
        }
    }
    else
    {
        sCount--;
    }

    if (sRenderDecal)
    {
        zEntPlayerDirectionDecalRender(Mat, sBowlingLaneRast, factor, 4.5f);
    }
    else
    {
        static const xVec3 off_corners[4] = {
            { 1.0f, 0.05f, 6.0f },
            { -1.0f, 0.05f, 6.0f },
            { 1.0f, 0.05f, 4.0f },
            { -1.0f, 0.05f, 4.0f },
        };
        static RxObjSpace3DVertex vtxbuf[4];

        alpha = 255.0f * factor;

        RwIm3DVertexSetPos(&vtxbuf[0], off_corners[0].x, off_corners[0].y,
                           off_corners[0].z + factor);
        RwIm3DVertexSetRGBA(&vtxbuf[0], alpha, alpha, alpha, alpha);
        RwIm3DVertexSetUV(&vtxbuf[0], 1.0f, 0.0f);

        RwIm3DVertexSetPos(&vtxbuf[1], off_corners[1].x, off_corners[1].y,
                           off_corners[1].z + factor);
        RwIm3DVertexSetRGBA(&vtxbuf[1], alpha, alpha, alpha, alpha);
        RwIm3DVertexSetUV(&vtxbuf[1], 0.0f, 0.0f);

        RwIm3DVertexSetPos(&vtxbuf[2], off_corners[2].x, off_corners[2].y, off_corners[2].z);
        RwIm3DVertexSetRGBA(&vtxbuf[2], alpha, alpha, alpha, alpha);
        RwIm3DVertexSetUV(&vtxbuf[2], 1.0f, 1.0f);

        RwIm3DVertexSetPos(&vtxbuf[3], off_corners[3].x, off_corners[3].y, off_corners[3].z);
        RwIm3DVertexSetRGBA(&vtxbuf[3], alpha, alpha, alpha, alpha);
        RwIm3DVertexSetUV(&vtxbuf[3], 0.0f, 1.0f);

        old_rendstat = zRenderStateCurrent();
        if (old_rendstat == SDRS_Unknown)
        {
            old_rendstat = SDRS_Default;
        }

        zRenderState(SDRS_NPCVisual);
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, sBowlingLaneRast);

        if (RwIm3DTransform(vtxbuf, 4, Mat, rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
        {
            RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
            RwIm3DEnd();
        }

        zRenderState(old_rendstat);
    }
}

// No symbol (always inlined); name from BfBB. Retail reads the target through its own
// (enum-typed) index.
static inline void get_reticle_bound(ePlayerReticleType which, xVec3& center, F32& radius)
{
    xEnt* target = gReticleTarget[which];

    center = *(xVec3*)&target->model->Mat->pos;
    center.y += target->model->Data->boundingSphere.center.y;
    radius = target->model->Data->boundingSphere.radius;
    radius *= xVec3Length((xVec3*)&target->model->Mat->up);
}

static void zEntPlayer_ReticleRender(zEnt* ent, ePlayerReticleType type)
{
    _SDRenderState old_RenderState;
    S32 i;
    U32 cheatAlternateTargettingMarker;
    F32 size;
    F32 bounce;
    F32 bound_radius;

    old_RenderState = zRenderStateCurrent();
    zRenderState(SDRS_OpaqueModels);

    xLightKit_GetCurrent(xglobals->currWorld);
    xLightKit_Enable(globals.player.pPlayerLightKit, xglobals->currWorld);

    for (i = 0; i < 2; i++)
    {
        cheatAlternateTargettingMarker =
            zUICustomExtrasManager::IsCheatEnabled(eExtrasScreenCheats_AltTargetHand);
        RpAtomic* model = cheatAlternateTargettingMarker ? sReticleModel[i + 2] : sReticleModel[i];

        if (gReticleTarget[i] && model && sReticleAlpha[i] >= 1.0f / 255.0f)
        {
            iModelSetMaterialAlpha(model, 255.0f * sReticleAlpha[i]);

            size = 4.0f * (1.0f - sReticleAlpha[i]) + 1.0f;
            // Not in the PS2 DWARF; the subtractions are a statement of their own, not an
            // inlined xVec3Dist.
            xVec3 fromcam;
            xVec3Sub(&fromcam, (xVec3*)&gReticleTarget[i]->model->Mat->pos,
                     &globals.oldSkoolCamera.mat.pos);
            size = size * (0.2f * xVec3Length(&fromcam));
            if (size < 1.0f)
            {
                size = 1.0f;
            }

            sReticleMat[i].up.y = size;

            bounce = 0.3f * isin(sReticleRot[i]);
            if (bounce < 0.0f)
            {
                bounce = -bounce;
            }

            sReticleMat[i].at.x = -globals.oldSkoolCamera.mat.right.z;
            sReticleMat[i].at.y = 0.0f;
            sReticleMat[i].at.z = globals.oldSkoolCamera.mat.right.x;
            xVec3SMulBy(&sReticleMat[i].at, size / xVec3Length(&sReticleMat[i].at));

            sReticleMat[i].right.x = sReticleMat[i].at.z;
            sReticleMat[i].right.y = 0.0f;
            sReticleMat[i].right.z = -sReticleMat[i].at.x;

            get_reticle_bound((ePlayerReticleType)i, sReticleMat[i].pos, bound_radius);
            sReticleMat[i].pos.y += bound_radius + bounce;

            {
                xSphere* sphbnd =
                    iModelWorldBoundSphere(model, (RwMatrixTag*)&sReticleMat[i], NULL);
                S32 rc = xModelSphereCull(sphbnd);
                if (!rc)
                {
                    iModelRender(model, (RwMatrixTag*)&sReticleMat[i]);
                }
            }

            sReticleMat[i].up.y = -size;
            sReticleMat[i].right.x = -sReticleMat[i].right.x;
            sReticleMat[i].right.z = -sReticleMat[i].right.z;
            sReticleMat[i].pos.y -= 2.0f * (bound_radius + bounce);

            {
                xSphere* sphbnd =
                    iModelWorldBoundSphere(model, (RwMatrixTag*)&sReticleMat[i], NULL);
                S32 rc = xModelSphereCull(sphbnd);
                if (!rc)
                {
                    iModelRender(model, (RwMatrixTag*)&sReticleMat[i]);
                }
            }

            sReticleMat[i].up.y = 1.0f;
        }
    }

    zRenderState(old_RenderState);
    xLightKit_Enable(globals.player.pPlayerLightKit, xglobals->currWorld);
}

void zEntPlayerPopOffOnAtomic(xModelInstance* model, F32 rate)
{
    if (sModelUpdatedOnce)
    {
        zFXPopOnOffAtomic(model, rate);
    }
}

static void zEntPlayerUpdateModelSB()
{
    xEnt& ent = *xglobals->___player_ent_dont_use_directly;
    xModelInstance** mlist = globals.player.sb_models;
    xModelInstance* m;
    xAnimSingle* asingle = ent.model->Anim->Single;
    xAnimState* tonguestate;

    m = mlist[0];
    m->Flags |= 0x3;
    *m->Mat = *ent.model->Mat;

    if (mlist[1])
    {
        mlist[1]->Flags &= ~0x3;
    }

    mlist[2]->Flags &= ~0x3;

    if (strncmp(asingle->State->Name, "Bspin", 5) == 0)
    {
        if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_KARATE) <= ePowerupLevelNormal)
        {
            m = mlist[5];
            if (m->Flags & 0x2)
            {
                if (!(m->Flags & 0x1))
                {
                    zEntPlayerPopOffOnAtomic(m, 40.0f);
                }
                m->Flags |= 0x1;
            }
            else
            {
                m->Flags |= 0x2;
            }

            m = mlist[6];
            if (m->Flags & 0x1)
            {
                zEntPlayerPopOffOnAtomic(m, 40.0f);
            }
            m->Flags &= ~0x1;
        }
        else
        {
            m = mlist[6];
            if (m->Flags & 0x2)
            {
                if (!(m->Flags & 0x1))
                {
                    zEntPlayerPopOffOnAtomic(m, 40.0f);
                }
                m->Flags |= 0x1;
            }
            else
            {
                m->Flags |= 0x2;
            }

            m = mlist[5];
            if (m->Flags & 0x1)
            {
                zEntPlayerPopOffOnAtomic(m, 40.0f);
            }
            m->Flags &= ~0x1;
        }
    }
    else
    {
        m = mlist[5];
        if (m->Flags & 0x1)
        {
            zEntPlayerPopOffOnAtomic(m, 40.0f);
        }
        m->Flags &= ~0x1;

        m = mlist[6];
        if (m->Flags & 0x1)
        {
            zEntPlayerPopOffOnAtomic(m, 50.0f);
        }
        m->Flags &= ~0x1;
    }

    m = mlist[7];
    if (m->Flags & 0x1)
    {
        zEntPlayerPopOffOnAtomic(m, 200.0f);
    }
    m->Flags &= ~0x1;

    m = mlist[8];
    if (m->Flags & 0x1)
    {
        zEntPlayerPopOffOnAtomic(m, 200.0f);
    }
    m->Flags &= ~0x1;

    m = mlist[9];
    if (m)
    {
        tonguestate = xAnimTableGetState(m->Anim->Table, asingle->State->Name);
        if (tonguestate)
        {
            m->Flags = 0x1;
            m->Anim->Single->State = tonguestate;
            m->Anim->Single->Time = asingle->Time;

            if (m->Anim->Single->State->Data->NumAnims[0] == 3)
            {
                m->Anim->Single->BilinearLerp[0] = 1.0f + globals.player.SlideTrackLean;
            }

            xAnimPlayUpdate(m->Anim, 0.0f);
            xAnimPlayEval(m->Anim);
            xMat4x3Mul((xMat4x3*)m->Mat, (xMat4x3*)&ent.model->Mat[3], (xMat4x3*)ent.model->Mat);
        }
        else
        {
            m->Flags = 0;
        }
    }

    if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_BASH) <= ePowerupLevelNormal)
    {
        m = mlist[4];
        if (m->Flags & 0x1)
        {
            zEntPlayerPopOffOnAtomic(m, 100.0f);
        }
        m->Flags &= ~0x1;

        m = mlist[3];
        if (strcmp(asingle->State->Name, "BbashStrike01") == 0 ||
            (strcmp(asingle->State->Name, "BbashStart01") == 0 && asingle->Time < 0.75f) ||
            strcmp(asingle->State->Name, "BbashStart02") == 0)
        {
            if (m->Flags & 0x2)
            {
                if (!(m->Flags & 0x1))
                {
                    zEntPlayerPopOffOnAtomic(m, 100.0f);
                }
                m->Flags |= 0x1;
                globals.player.IsBubbleBashing = 1;
            }
            else
            {
                m->Flags |= 0x2;
            }
        }
        else
        {
            if (m->Flags & 0x1)
            {
                zEntPlayerPopOffOnAtomic(m, 100.0f);
            }
            m->Flags &= ~0x1;
        }
    }
    else
    {
        m = mlist[3];
        if (m->Flags & 0x1)
        {
            zEntPlayerPopOffOnAtomic(m, 100.0f);
        }
        m->Flags &= ~0x1;

        m = mlist[4];
        if ((strcmp(asingle->State->Name, "BbashStart01") == 0 ||
             strcmp(asingle->State->Name, "BbashStart02") == 0 ||
             (sGauntletStartHaz && strcmp(asingle->State->Name, "BbashStrike01") == 0)) &&
            !asingle->Blend->State && !sGauntletHaz)
        {
            if (m->Flags & 0x2)
            {
                if (!(m->Flags & 0x1))
                {
                    zEntPlayerPopOffOnAtomic(m, 100.0f);
                }
                m->Flags |= 0x1;
                globals.player.IsBubbleBashing = 1;
            }
            else
            {
                m->Flags |= 0x2;
            }
        }
        else
        {
            if (m->Flags & 0x1)
            {
                zEntPlayerPopOffOnAtomic(m, 100.0f);
            }
            m->Flags &= ~0x1;
        }
    }

    sModelUpdatedOnce = TRUE;
}

// No symbol: inlined into its callers (zEntPlayer_Render, oob drop_state_type::start).
void zEntPlayerUpdateModel()
{
    xEnt* ent = xglobals->___player_ent_dont_use_directly;

    if (ent->model == globals.player.model_spongebob)
    {
        zEntPlayerUpdateModelSB();
    }

    for (xModelInstance* m = ent->model; m != NULL; m = m->Next)
    {
        if (iModelNumBones(m->Data) == 0)
        {
            m->Flags &= ~0x7;
        }
    }
}

// No symbol (always inlined); name from BfBB.
static inline void zEntPlayerEmitTongueBubbles()
{
    xModelInstance* model = globals.player.sb_models[9];
    xVec3 vec;

    if (model && (model->Flags & 0x1) && (U8)xrand_RandomInt32() < 0x80)
    {
        xVec3Copy(&vec, (xVec3*)&model->Mat->pos);
        vec.y -= 0.5f;
        zFX_SpawnBubbleTrail(&vec, 1);
    }
}

// No symbol (always inlined); name from BfBB.
static inline void zEntPlayerEmitSlideBubbles()
{
    xModelInstance* model = xglobals->___player_ent_dont_use_directly->model;
    xVec3 vec;

    if ((model->Flags & 0x1) && (U8)xrand_RandomInt32() < 0x80)
    {
        xVec3Copy(&vec, (xVec3*)&model->Mat->pos);
        vec.y -= 0.5f;
        zFX_SpawnBubbleTrail(&vec, 1);
    }
}

void zEntPlayer_Render(zEnt* ent)
{
    if (bungee_state::render())
    {
        return;
    }

    if (cruise_bubble::render())
    {
        return;
    }

    if (gCurrentPlayer != lastgCurrentPlayer)
    {
        return;
    }

    if (!globals.player.Visible)
    {
        xShadowManager_Remove(ent);
        return;
    }

    F32 rootZadjust = 0.0f;
    RwMatrixTag rootOldMat = *ent->model->Mat;
    xAnimSingle* asingle = ent->model->Anim->Single;
    xAnimSingle* ablend = asingle->Blend;

    if (strcmp(asingle->State->Name, "SD0_SetDown") == 0 ||
        strcmp(asingle->State->Name, "SD1_SetDown") == 0)
    {
        if (asingle->Time >= 0.85f)
        {
            rootZadjust = -0.6f;
        }
        else if (asingle->Time > 0.65f)
        {
            rootZadjust = -0.6f * (asingle->Time - 0.65f) / (0.85f - 0.65f);
        }
    }
    else if (ablend->State && (strcmp(ablend->State->Name, "SD0_SetDown") == 0 ||
                               strcmp(ablend->State->Name, "SD1_SetDown") == 0))
    {
        rootZadjust = -0.6f * (1.0f - asingle->BlendFactor * asingle->Tran->BlendRecip);
    }

    if (rootZadjust)
    {
        xVec3 rootZvec = { 0.0f, 0.0f, rootZadjust };
        xMat3x3RMulVec(&rootZvec, (xMat3x3*)ent->model->Mat, &rootZvec);
        ent->model->Mat->pos.x += rootZvec.x;
        ent->model->Mat->pos.z += rootZvec.z;
    }

    *(xMat4x3*)ent->model->Mat = rendermat;

    if (1.0f != globals.player.RootUp.y || rootZadjust)
    {
        xModelInstance* minst = ent->model->Next;
        while (minst)
        {
            *minst->Mat = *ent->model->Mat;
            minst = minst->Next;
        }
    }

    zEntPlayerUpdateModel();

    if (ent->model == globals.player.model_spongebob)
    {
        zEntPlayerEmitTongueBubbles();
    }
    else if (ent->model == globals.player.model_patrick)
    {
        if (globals.player.SlideTrackSliding)
        {
            zEntPlayerEmitSlideBubbles();
        }

        if (sLasso->flags & 0x1)
        {
            if (sLasso->flags & 0x1000)
            {
                sLasso->flags &= ~0x1000;
            }
            else
            {
                zLasso_Render(sLasso);
            }
        }
    }

    F32 orderDot[2];
    for (U32 i = 0; i < 2; i++)
    {
        if (gReticleTarget[i])
        {
            // not in the DWARF: xVec3Sub needs addressable vectors; probably locals of an inlined
            // helper.
            xVec3 toCam;
            xVec3 toTarget;
            xVec3Sub(&toCam, &globals.oldSkoolCamera.mat.pos, (xVec3*)&ent->model->Mat->pos);
            xVec3Sub(&toTarget, (xVec3*)&gReticleTarget[i]->model->Mat->pos,
                     (xVec3*)&ent->model->Mat->pos);
            orderDot[i] = xVec3Dot(&toCam, &toTarget);

            if (orderDot[i] <= 0.0f)
            {
                zEntPlayer_ReticleRender(ent, (ePlayerReticleType)i);
            }
        }
    }

    S32 oldBucketEnabled = xModelBucketEnabled;
    xModelBucketEnabled = 0;
    xEntRender(xglobals->___player_ent_dont_use_directly);
    xModelBucketEnabled = oldBucketEnabled;

    for (U32 i = 0; i < 2; i++)
    {
        if (orderDot[i] > 0.0f)
        {
            zEntPlayer_ReticleRender(ent, (ePlayerReticleType)i);
        }
    }

    *ent->model->Mat = rootOldMat;

    xFXRingRender();

    if (zEntPlayerFX::CartShield::render)
    {
        zEntPlayerFX::CartShield::CartShieldModelInstances[0]->Alpha =
            zEntPlayerFX::CartShield::modelColor[0].a / 255.0f;
        zEntPlayerFX::CartShield::CartShieldModelInstances[0]->RedMultiplier =
            zEntPlayerFX::CartShield::modelColor[0].r / 255.0f;
        zEntPlayerFX::CartShield::CartShieldModelInstances[0]->GreenMultiplier =
            zEntPlayerFX::CartShield::modelColor[0].g / 255.0f;
        zEntPlayerFX::CartShield::CartShieldModelInstances[0]->BlueMultiplier =
            zEntPlayerFX::CartShield::modelColor[0].b / 255.0f;
        xModelBucket_Add(zEntPlayerFX::CartShield::CartShieldModelInstances[0]);
    }

    if (zEntPlayerFX::SuperMachoBowlExplosion::ExplosionSphere &&
        zEntPlayerFX::SuperMachoBowlExplosion::Exploding &&
        zEntPlayerFX::SuperMachoBowlLightning::Scale > 0.0f)
    {
        zEntPlayerFX::SuperMachoBowlExplosion::ExplosionSphere->Surf =
            zEntPlayerFX::SuperMachoBowlExplosion::Surface;
        zEntPlayerFX::SuperMachoBowlExplosion::ExplosionSphere->Scale.assign(
            zEntPlayerFX::SuperMachoBowlLightning::Scale,
            zEntPlayerFX::SuperMachoBowlLightning::Scale,
            zEntPlayerFX::SuperMachoBowlLightning::Scale);
        xModelBucket_Add(zEntPlayerFX::SuperMachoBowlExplosion::ExplosionSphere);
    }
}

void zEntPlayer_RenderEffects(zEnt* ent)
{
    if (globals.player.IsBubbleBowling)
    {
        zEntPlayer_BubbleBowlLaneRender(ent);
    }

    if (sThrowDecalFactor == 0.0f)
    {
        zEntPlayerFX::HammerThrow::SetDust(false);
    }
    else
    {
        xVec3 pos = ent->frame->mat.pos;
        xMat4x3 trans;
        xMat4x3 rot;
        xMat4x3 mat;

        xMat4x3Identity(&mat);
        xMat4x3RotY(&rot, sThrowAngle, &pos);
        xMat4x3Mul(&mat, &rot, &mat);

        xMat3x3Identity(&trans);
        xVec3Copy(&trans.pos, &pos);
        xMat4x3Mul(&mat, &trans, &mat);

        // GC: the decal z bias is applied by pushing the camera near plane out
        RwCamera* cam = RwCameraGetCurrentCamera();
        F32 old_zbias = !cam ? 0.0f : RwCameraGetNearClipPlane(cam);

        if (cam)
        {
            F32 farPlane;
            F32 nearPlane;

            nearPlane = RwCameraGetNearClipPlane(cam);
            farPlane = RwCameraGetFarClipPlane(cam);

            if (0.11f != nearPlane)
            {
                RwCameraEndUpdate(cam);
                RwCameraSetNearClipPlane(cam, 0.11f);
                RwCameraSetFarClipPlane(cam, 0.11f + farPlane - nearPlane);
                RwCameraBeginUpdate(cam);
            }
        }

        zEntPlayerDirectionDecalRender((RwMatrixTag*)&mat, sThrowArrowRast, sThrowDecalFactor,
                                       0.7f);

        cam = RwCameraGetCurrentCamera();

        if (cam)
        {
            F32 farPlane;
            F32 nearPlane;

            nearPlane = RwCameraGetNearClipPlane(cam);
            farPlane = RwCameraGetFarClipPlane(cam);

            if (old_zbias != nearPlane)
            {
                RwCameraEndUpdate(cam);
                RwCameraSetNearClipPlane(cam, old_zbias);
                RwCameraSetFarClipPlane(cam, old_zbias + farPlane - nearPlane);
                RwCameraBeginUpdate(cam);
            }
        }
    }
}

// No GC or PS2 symbol (always inlined into zEntPlayer_Move); BfBB has it out of line.
static inline void dampen_velocity(xVec3& v, const xVec3& damp, F32 dt)
{
    xVec3 old = v;

    v.x -= dt * (v.x * damp.x);
    v.y -= dt * (v.y * damp.y);
    v.z -= dt * (v.z * damp.z);

    if (xnegative(v.x) != xnegative(old.x))
    {
        v.x = 0.0f;
    }

    if (xnegative(v.y) != xnegative(old.y))
    {
        v.y = 0.0f;
    }

    if (xnegative(v.z) != xnegative(old.z))
    {
        v.z = 0.0f;
    }
}

void zEntPlayer_Move(xEnt* ent, xScene* sc, F32 dt, xEntFrame* frame)
{
    frame->mode = 0;
    frame->mode &= ~0x400;
    frame->mode &= ~0x800;

    if (globals.player.s->Health)
    {
        S32 px = -globals.pad0->analog1.x;
        S32 pz = -globals.pad0->analog1.y;

        if (!globals.player.g.CheatPlayerSwitch)
        {
            if (globals.pad0->on & XPAD_BUTTON_LEFT)
            {
                px = globals.player.g.AnalogMax;
            }
            else if (globals.pad0->on & XPAD_BUTTON_RIGHT)
            {
                px = -globals.player.g.AnalogMax;
            }

            if (globals.pad0->on & XPAD_BUTTON_UP)
            {
                pz = globals.player.g.AnalogMax;
            }
            else if (globals.pad0->on & XPAD_BUTTON_DOWN)
            {
                pz = -globals.player.g.AnalogMax;
            }
        }

        PlayerAbsControl(ent, px, pz, dt);
    }

    if (globals.player.cheat_mode)
    {
        if (!globals.player.ControlOff && (globals.pad0->on & XPAD_BUTTON_SQUARE))
        {
            frame->dpos.y += dt * (7.0f * ((globals.pad0->on & (XPAD_BUTTON_L1 | XPAD_BUTTON_L2 |
                                                                XPAD_BUTTON_R1 | XPAD_BUTTON_R2)) ?
                                               4.68f :
                                               1.56f));
            frame->mode |= 0x2;
        }
        else if (!globals.player.ControlOff && (globals.pad0->on & XPAD_MOVE(XPAD_BUTTON_TRIANGLE)))
        {
            frame->dpos.y -= dt * (7.0f * ((globals.pad0->on & (XPAD_BUTTON_L1 | XPAD_BUTTON_L2 |
                                                                XPAD_BUTTON_R1 | XPAD_BUTTON_R2)) ?
                                               4.68f :
                                               1.56f));
            frame->mode |= 0x2;
        }
    }

    if (globals.player.WallJumpState == k_WALLJUMP_LAND)
    {
        xVec3 inverseNormal;
        xVec3Inv(&inverseNormal, &sWallNormal);
        TurnToFace(ent, &inverseNormal, 10.0f, dt);
        frame->vel.y = 0.0f;
    }

    if (globals.player.WallJumpState == k_WALLJUMP_LAUNCH)
    {
        frame->mode |= 0x10000;

        xVec3 inverseNormal;
        xVec3Inv(&inverseNormal, &sWallNormal);
        TurnToFace(ent, &inverseNormal, 10.0f, dt);
    }

    if (!globals.player.JumpState)
    {
        sPlayerAttackInAir = 0;
    }

    if (sPlayerAttackInAir <= 1 && !surfSlickRatio && !globals.player.ControlOff &&
        (globals.player.IsBubbleSpinning ||
         strcmp(ent->model->Anim->Single->State->Name, "Melee01") == 0 ||
         strcmp(ent->model->Anim->Single->State->Name, "Melee02") == 0 ||
         strcmp(ent->model->Anim->Single->State->Name, "JumpMelee01") == 0))
    {
        zPlayerGlobals& p = globals.player;
        const xVec3 damp = { p.sb.spin_damp_xz, p.sb.spin_damp_y, p.sb.spin_damp_xz };

        dampen_velocity(ent->frame->vel, damp, dt);
        dampen_velocity(ent->frame->dpos, damp, dt);
    }
}

void zEntPlayer_setBoulderMode(U32 mode)
{
    if (mode != 0)
    {
        boulderRollShouldStart = 1;
        boulderRollShouldEnd = 0;
    }
    else
    {
        boulderRollShouldStart = 0;
        boulderRollShouldEnd = 1;
    }
}

// No GC symbol: inlined into zEntPlayerEventCB, the out-of-line copy dead-stripped.
void zEntPlayer_DamageKnockIntoAir(F32 height)
{
    zJumpParam jump;
    jump.PeakHeight = height;
    jump.TimeHold = 0.0f;
    jump.TimeGravChange = 0.3f;
    CalcJumpImpulse(&jump, NULL);
    zEntPlayerJumpStart(xglobals->___player_ent_dont_use_directly, &jump);
    zEntPlayerJumpAddDriver(xglobals->___player_ent_dont_use_directly);
    globals.player.KnockIntoAirTimer = 0.75f;
    globals.player.Jump_CanDouble = 1;
    globals.player.Jump_CanFloat = 1;
}

S32 zEntPlayer_Damage(xBase* src, U32 damage, const xVec3* knockback)
{
    if (!zEntPlayer_Damage(src, damage))
    {
        return 0;
    }

    if (knockback)
    {
        xglobals->___player_ent_dont_use_directly->frame->vel.x = knockback->x;
        xglobals->___player_ent_dont_use_directly->frame->vel.y = knockback->y;
        xglobals->___player_ent_dont_use_directly->frame->vel.z = knockback->z;
        globals.player.KnockBackTimer = sPlayerNPC_KnockBackTime;
        globals.player.KnockIntoAirTimer = 0.0f;
    }

    return 1;
}

S32 zEntPlayer_DamageNPCKnockBack(xBase* src, U32 damage, xVec3* npcPos, ePlayerNMEDamage type)
{
    F32 dx;
    F32 dz;
    F32 mag;

    if (gCurrentPlayer == eCurrentPlayerCar)
    {
        return 0;
    }

    if (!zEntPlayer_Damage(src, damage))
    {
        return 0;
    }

    if (src)
    {
        switch (src->baseType)
        {
        case eBaseTypeNPC:
            switch (((zNMECommon*)src)->itemType)
            {
            case NME_TYPE_DENNIS_V2:
                ((zNMEDenToo*)src)->RespondToHittingPlayer();
                break;
            }
            break;
        }
    }

    switch (type)
    {
    case ePlayerNMEDamageDefault:
        dx = xglobals->___player_ent_dont_use_directly->model->Mat->pos.x - npcPos->x;
        dz = xglobals->___player_ent_dont_use_directly->model->Mat->pos.z - npcPos->z;
        mag = xsqrt(dx * dx + dz * dz);

        if (mag < 0.01f)
        {
            mag = 1.5f;
            dx = -xglobals->___player_ent_dont_use_directly->model->Mat->at.x;
            dz = -xglobals->___player_ent_dont_use_directly->model->Mat->at.z;
        }

        mag = sPlayerNPC_KnockBackVel / mag;
        xglobals->___player_ent_dont_use_directly->frame->vel.x = mag * dx;
        xglobals->___player_ent_dont_use_directly->frame->vel.y = 0.0f;
        xglobals->___player_ent_dont_use_directly->frame->vel.z = mag * dz;
        globals.player.KnockIntoAirTimer = 0.0f;
        globals.player.KnockBackTimer = sPlayerNPC_KnockBackTime;
        break;
    case ePlayerNMEDamageEnemyLaserBeam:
    case ePlayerNMEDamageEnemyProjectile:
    default:
        dx = -xglobals->___player_ent_dont_use_directly->model->Mat->at.x;
        dz = -xglobals->___player_ent_dont_use_directly->model->Mat->at.z;
        mag = sPlayerNPC_KnockBackVel / 1.5f;
        xglobals->___player_ent_dont_use_directly->frame->vel.x = mag * dx;
        xglobals->___player_ent_dont_use_directly->frame->vel.y = 0.0f;
        xglobals->___player_ent_dont_use_directly->frame->vel.z = mag * dz;
        globals.player.KnockIntoAirTimer = 0.0f;
        globals.player.KnockBackTimer = sPlayerNPC_KnockBackTime;
        break;
    }

    return 1;
}

S32 zEntPlayer_Damage(xBase* src, U32 damage)
{
    zEntPlayerFX::HammerThrow::SetDust(false);
    zEntPlayerFX::HammerThrow::SetBlur(false);
    zEntPlayerFX::MotionBlur::Blurring = false;
    zEntPlayerFX::MotionBlur::EntToTrack = NULL;

    zEntPlayerKillCarry();

    if (globals.player.cheat_mode)
    {
        if (!(globals.player.DamageTimer > 0.0f))
        {
            globals.player.DamageTimer = globals.player.g.DamageTimeHit;
        }

        return 0;
    }

    if (globals.player.ControlOff & CONTROL_OWNER_BUS_STOP)
    {
        return 0;
    }

    if (!(damage == 666 && (damage = 1, globals.player.s->Health > 1)) &&
        (globals.player.DamageTimer > 0.0f || globals.player.ControlOffTimer > 0.0f ||
         zEntTeleportBox_playerIn()))
    {
        return 0;
    }

    zEntPlayerControlOn(CONTROL_OWNER_SPRINGBOARD);

    zEntPlayer_SNDStop(ePlayerSnd_BowlWindup);
    zEntPlayer_SNDStop(ePlayerSnd_CruiseNavigate);
    zEntPlayer_SNDStop(ePlayerSnd_BubbleWand);

    zEntPlayer_SNDStop(ePlayerSnd_Heli);
    zEntPlayer_SNDStop(ePlayerSnd_SlipLoop);
    zEntPlayer_SNDStop(ePlayerSnd_HammerThrowLoop);
    zEntPlayer_SNDStop(ePlayerSnd_CartwheelLoop);
    zEntPlayer_SNDStop(ePlayerSnd_Cartwheel2Loop);

    zEntPlayer_SNDPlay(ePlayerSnd_Ouch, true);

    globals.player.ScareSource = src;

    if (damage > globals.player.s->Health)
    {
        damage = globals.player.s->Health;
    }

    if (globals.player.g.TakeDamage)
    {
        globals.player.s->Health -= damage;

        if (globals.player.s->Health && gCurrentPlayer == eCurrentPlayerSpongePat)
        {
            sSlidingOneLiners.PlaySound(2);
        }
    }

    if (globals.player.s->Health == 0)
    {
        sPlayerDiedLastTime = 1;

        zEntPlayer_SNDPlay(ePlayerSnd_Death, true);
        zEntPlayer_SNDPlayStream(ePlayerStreamSnd_Defeated);

        globals.player.DamageTimer = 10.0f;
        zEntPlayerControlOff(CONTROL_OWNER_GLOBAL);

        if (xglobals->___player_ent_dont_use_directly->driver)
        {
            xglobals->___player_ent_dont_use_directly->driver = NULL;
            xVec3Copy(&xglobals->___player_ent_dont_use_directly->frame->vel, &g_O3);
            xVec3Copy(&xglobals->___player_ent_dont_use_directly->frame->oldvel, &g_O3);
        }

        globals.player.rewardsMgr->GiveAllRewardsNow();
        globals.player.rewardsMgr->SetPlayerDead();

        zEntEvent(xglobals->___player_ent_dont_use_directly,
                  xglobals->___player_ent_dont_use_directly, eEventPlayerDeath);
    }
    else
    {
        globals.player.DamageTimer = globals.player.g.DamageTimeHit;
    }

    if (globals.player.HangEnt)
    {
        globals.player.HangEnt->grabTimer = globals.player.HangEnt->hangInfo->grabDelay;
        globals.player.HangEnt = NULL;

        globals.player.RootUpTarget.x = 0.0f;
        globals.player.RootUpTarget.y = 1.0f;
        globals.player.RootUpTarget.z = 0.0f;

        globals.player.HangElapsed = 0.0f;
    }

    if (xglobals->___player_ent_dont_use_directly->model == globals.player.model_patrick)
    {
        if (globals.player.s->Health)
        {
            player_hitlist_anim = (player_hitlist_anim + 1) % patrickHitMax;
            xAnimPlayStartTransition(globals.player.model_patrick->Anim,
                                     &patrickHitTran[player_hitlist_anim]);
        }
    }
    else if ((gCurrentPlayer == eCurrentPlayerSpongeBob ||
              gCurrentPlayer == eCurrentPlayerSpongePat) &&
             globals.player.s->Health)
    {
        player_hit = 1;
    }

    idle_tmr = 0.0f;
    globals.player.lassoInfo.lasso.flags = 0;

    if (globals.player.WallJumpState != k_WALLJUMP_NOT)
    {
        globals.player.WallJumpState = k_WALLJUMP_NOT;
    }

    globals.player.SpeedMult = 1.0f;
    globals.player.SundaeTimer = -1.0f;
    globals.player.IsBubbleBowling = 0;

    return 1;
}

en_plyrpup zEntPlayer_PowerupLevel(en_plyratak forThisAttack)
{
    if (forThisAttack == PLYR_ATAK_SLIDE)
    {
        return ePowerupLevelNormal;
    }

    if (forThisAttack <= PLYR_ATAK_NONE)
    {
        return ePowerupLevelDisabled;
    }

    return (en_plyrpup)globals.player.g.PowerUp[forThisAttack - 1];
}

// Placeholder for dead-stripped code (name and body unknown) that interned these anim-state names
// in retail's string pool here; their later users share the pooled copies.
static void __deadstripped_zEntPlayer2(const char** names)
{
    names[0] = "Cartwheel01";
    names[1] = "StunJump";
    names[2] = "StunJump02";
    names[3] = "StunFall";
    names[4] = "StunFall02";
    names[5] = "StunLand";
    names[6] = "StunLand02";
}

U32 zEntPlayer_MoveInfo()
{
    U32 animflags;
    U32 infoflags = 0;
    const char* nam_ast;

    animflags =
        xglobals->___player_ent_dont_use_directly->model->Anim->Single->State->UserFlags & 0x1e;
    nam_ast = xglobals->___player_ent_dont_use_directly->model->Anim->Single->State->Name;

    // The mask is written out again (not BfBB's 'animflags == 0'): that gives retail's registers
    // where this is inlined into zEntPlayer_CheckCritterContact.
    if (!(xglobals->___player_ent_dont_use_directly->model->Anim->Single->State->UserFlags &
          0x1e) ||
        xglobals->___player_ent_dont_use_directly->model->Anim->Single->State->UserFlags & 1)
    {
        infoflags |= 1;
    }

    if (animflags == 4)
    {
        infoflags |= 2;
    }

    if (animflags == 6 || animflags == 8)
    {
        infoflags |= 4;
    }

    if (strcmp(nam_ast, "Bspin01") == 0 || strcmp(nam_ast, "Bspin02") == 0)
    {
        infoflags |= 0x20;
    }

    if (strcmp(nam_ast, "BbashStart01") == 0 || strcmp(nam_ast, "BbashStart02") == 0)
    {
        infoflags |= 8;
    }

    if (strcmp(nam_ast, "BbounceStrike01") == 0 || strcmp(nam_ast, "BbounceStart01") == 0 ||
        strcmp(nam_ast, "BbounceAttack01") == 0)
    {
        infoflags |= 0x10;
    }

    if (animflags == 0xc)
    {
        infoflags |= 0x20;
    }

    if (animflags == 2 || infoflags & 1 || infoflags & 2)
    {
        infoflags |= 0x40;
    }

    return infoflags;
}

// No GC symbol: inlined into zEntPlayerEventCB, the out-of-line copy dead-stripped.
void zEntPlayer_GiveHealth(S32 quantity)
{
    if (quantity < 0 && -quantity > (S32)globals.player.s->Health)
    {
        globals.player.s->Health = 0;
        return;
    }

    globals.player.s->Health += quantity;

    if (globals.player.s->Health > globals.player.s->MaxHealth)
    {
        globals.player.s->Health = globals.player.s->MaxHealth;
    }
}

// No GC symbol: inlined into zEntPlayerEventCB, the out-of-line copy dead-stripped.
void zEntPlayer_GiveSpatula(S32)
{
    sSpatulaGrabbed = 1;

    if (globals.player.ControlOffTimer < 0.1f)
    {
        globals.player.ControlOffTimer = 0.1f;
    }

    zNMEMsg_AreaNotify(NULL, NME_MID_PLYRSPATULA, 30.0f, 0x104, NME_TYPE_UNKNOWN);

    if (!(globals.player.ControlOff & CONTROL_OWNER_TALK_BOX))
    {
        sOldMusicVolume = zMusicGetCurrentVolume(0);
        zMusicSetVolume(gDialogueMusicLevel, 0.2f);
    }
}

// No GC symbol: inlined into zEntPlayerEventCB, the out-of-line copy dead-stripped.
void zEntPlayer_GiveShinyObject(S32 quantity)
{
    if (quantity < 0 && -quantity > (S32)globals.player.InvManlinessPoints)
    {
        globals.player.rewardsMgr->IncManlinessPoints(0, true);
    }
    else if (globals.player.InvManlinessPoints + quantity > SHINY_MAX)
    {
        globals.player.rewardsMgr->IncManlinessPoints(SHINY_MAX, true);
    }
    else
    {
        U32 multiplier = globals.player.InvManlinessPointsMultiplier;
        globals.player.InvManlinessPointsMultiplier = 1;
        globals.player.rewardsMgr->IncManlinessPoints(quantity, false);
        globals.player.InvManlinessPointsMultiplier = multiplier;
    }
}

// No GC symbol: inlined into zEntPlayerEventCB, the out-of-line copy dead-stripped.
void zEntPlayer_GivePatsSocksCurrentLevel(S32 quantity)
{
    U32 level = zSceneGetLevelIndex();

    if (quantity < 0 && -quantity > (S32)globals.player.InvTreasureChestTotal)
    {
        globals.player.InvTreasureChestTotal = 0;
    }
    else
    {
        zUICustomExtrasManager::GivePlayerTreasureChest(quantity);
    }

    if (quantity < 0 && -quantity > (S32)globals.player.InvTreasureChest[level])
    {
        globals.player.InvTreasureChest[level] = 0;
    }
    else
    {
        globals.player.InvTreasureChest[level] += quantity;
    }

    globals.player.InvTreasureChestCurrentLevel = globals.player.InvTreasureChest[level];

    if (quantity > 0)
    {
        zNMEMsg_AreaNotify(NULL, NME_MID_PLYRSPATULA, 30.0f, 0x104, NME_TYPE_UNKNOWN);
    }
}

// No GC symbol: inlined into zEntPlayerEventCB, the out-of-line copy dead-stripped.
void zEntPlayer_GiveLevelPickupCurrentLevel(S32 quantity)
{
    U32 level = zSceneGetLevelIndex();

    if (quantity < 0 && -quantity > (S32)globals.player.InvLevelPickups[level])
    {
        globals.player.InvLevelPickups[level] = 0;
    }
    else
    {
        globals.player.InvLevelPickups[level] += quantity;
    }

    globals.player.InvLevelPickupsCurrentLevel = globals.player.InvLevelPickups[level];

    if (quantity > 0)
    {
        zNMEMsg_AreaNotify(NULL, NME_MID_PLYRSPATULA, 30.0f, 0x104, NME_TYPE_UNKNOWN);
    }
}

// No symbol (the helper and its name are ours): retail's -j / 2 behaves like an inline's return
// value, which gives AS its register in CalcJumpImpulse_Smooth.
static inline F32 HalfNegGravity(F32 grav)
{
    return -grav / 2.0f;
}

// Piecewise jump curve: quadratic b0 + b1*t + AS*t^2 (gravity j) up to Tgc, cubic A/B/Kc/D
// through the smoothing window Tgs, quadratic c0 + Kbf*t + AF*t^2 (gravity g) afterwards;
// returns the launch velocity that peaks at height h. AS, Ts and Tf are Scooby's names; b0 and
// b1 are variables because retail negates a loaded 0.0f, and c0 as its own statement keeps the
// wrapper inlined.
static F32 CalcJumpImpulse_Smooth(F32 g, F32 j, F32 h, F32 Tgc, F32 Tgs)
{
    U32 i;
    U32 solcnt;
    F32 AS;
    F32 A;
    F32 B;
    F32 Kc;
    F32 D;
    F32 AF;
    F32 Kbf;
    F32 Ts;
    F32 Tm[3];
    F32 Tf;
    F32 Tmfound;
    F32 b0;
    F32 b1;
    F32 c0;

    b0 = 0.0f;
    b1 = 0.0f;
    AS = HalfNegGravity(j);
    A = (j - g) / (6.0f * Tgs);
    B = (g * Tgc - j * Tgc - j * Tgs) / (2.0f * Tgs);

    Kc = -(3.0f * A * (Tgc * Tgc) + j * Tgc + 2.0f * B * Tgc);
    D = AS * (Tgc * Tgc) - A * (Tgc * (Tgc * Tgc)) - B * (Tgc * Tgc) - Kc * Tgc;
    AF = -g / 2.0f;
    Kbf = 3.0f * A * ((Tgc + Tgs) * (Tgc + Tgs)) + 2.0f * B * (Tgc + Tgs) + g * (Tgc + Tgs);
    c0 = D + (A * ((Tgc + Tgs) * ((Tgc + Tgs) * (Tgc + Tgs))) + B * ((Tgc + Tgs) * (Tgc + Tgs))) -
         AF * ((Tgc + Tgs) * (Tgc + Tgs)) - Kbf * (Tgc + Tgs);

    Ts = xsqrt((b0 - h) / AS);
    Tf = xsqrt((c0 - h) / AF);

    solcnt = xMathSolveCubic(-2.0f * A, -B, 0.0f, D - h, &Tm[0], &Tm[1], &Tm[2]);

    if (Ts <= Tgc && Ts >= 0.0f)
    {
        return -b1 - 2.0f * AS * Ts;
    }

    if (Tf >= Tgc + Tgs)
    {
        return -(Kbf + Kc) - 2.0f * AF * Tf;
    }

    Tmfound = -1.0f;

    for (i = 0; i < solcnt; i++)
    {
        if (Tm[i] >= Tgc && Tm[i] <= Tgc + Tgs)
        {
            if (Tmfound < 0.0f || Tm[i] < Tmfound)
            {
                Tmfound = Tm[i];
            }
        }
    }

    if (Tmfound != -1.0f)
    {
        return -Kc - 2.0f * B * Tmfound - 3.0f * A * (Tmfound * Tmfound);
    }

    return 1.0f;
}

static void zEntPlayerJumpLand(xEnt* ent)
{
    F32 volumeLandMax;
    F32 volumeLandMin;
    F32 vel;
    F32 diff;
    F32 vol;
    xRumble::emitterBase* rumbleEffect;
    xSndGroupInfo* sndInfo;

    if (gCurrentPlayer >= eCurrentPlayerSpongeBob && gCurrentPlayer < eCurrentPlayerCount)
    {
        globals.player.JumpState = 0;
        globals.player.SlideNotGroundedSinceSlide = 0;
        zEntPlayerControlOn(CONTROL_OWNER_SPRINGBOARD);

        if (gCurrentPlayer == eCurrentPlayerSpongePat)
        {
            volumeLandMin = 3.0f;
            volumeLandMax = 67.0f;
        }
        else
        {
            volumeLandMin = 5.0f;
            volumeLandMax = 15.0f;
        }

        vel = -ent->frame->vel.y;
        diff = vel - volumeLandMin;

        if (diff <= 0.0f)
        {
            vol = 0.0f;
        }
        else if (diff >= volumeLandMax - volumeLandMin)
        {
            vol = 1.0f;
        }
        else
        {
            vol = 0.8f * (diff / (volumeLandMax - volumeLandMin)) + 0.2f;
        }

        if (vol > 0.0f && globals.sceneCur->sceneID != 'MNUS')
        {
            rumbleEffect = (xRumble::emitterBase*)sPlayerRumble[ePlayerSnd_Land];
            if (rumbleEffect && diff > 12.0f)
            {
                rumbleEffect->pEffectAsset->time = 0.008f * diff;
                zEntEvent(rumbleEffect, eEventStartRumbleEffect, globals.currentActivePad, 0.0f,
                          0.0f, 0.0f);
            }

            zEntPlayer_SNDPlay(ePlayerSnd_Land, false);

            sndInfo = ((xSndGroup*)(UPtr)xSndMgrGetSoundGroup(
                           sPlayerSnd[gCurrentPlayer][ePlayerSnd_Land]))
                          ->aSndGroupInfo;
            zEntPlayer_SNDSetVol(ePlayerSnd_Land, vol * sndInfo->fVolume);

            if (vel > 17.0f && gCurrentPlayer == eCurrentPlayerSpongePat)
            {
                sSlidingOneLiners.PlaySound(4);
            }
        }

        zEntPlayerFX::MotionBlur::Blurring = false;
        zEntPlayerFX::MotionBlur::EntToTrack = NULL;
        tslide_inair_tmr = 0.0f;
        tslide_dbl_tmr = 0.0f;
    }
}

static void zEntPlayerJumpUpdate(xEnt* ent, xScene* sc, F32 dt)
{
    F32 lerp;
    xCollis* cfloor;

    if (strcmp(ent->model->Anim->Single->State->Name, "BbashStrike01") == 0 &&
        ent->model->Anim->Single->Time < 5.0f / 30.0f)
    {
        return;
    }

    if ((strcmp(ent->model->Anim->Single->State->Name, "BbashStart01") == 0 ||
         strcmp(ent->model->Anim->Single->State->Name, "BbashStart02") == 0) &&
        bbash_tmr < globals.player.g.BBashCVTime)
    {
        if (bbash_tmr < 0.0f)
        {
            bbash_tmr += dt;

            if (bbash_tmr >= 0.0f)
            {
                ent->frame->vel.y = bbash_vel;
            }
        }
        else
        {
            bbash_tmr += dt;
        }
        return;
    }

    if (globals.player.cheat_mode != 0)
    {
        return;
    }

    if (globals.player.JumpState != globals.player.LastJumpState &&
        globals.player.LastJumpState == 0)
    {
        sStartBubbleBowlLeftGround = 1;
    }

    globals.player.LastJumpState = globals.player.JumpState;

    if (globals.player.Jump_HoldTimer)
    {
        globals.player.Jump_HoldTimer -= dt;

        if (globals.player.Jump_HoldTimer <= 0.0f)
        {
            globals.player.Jump_HoldTimer = 0.0f;
        }
        else if (globals.player.ControlOff || !(globals.pad0->on & XPAD_BUTTON_X))
        {
            globals.player.Jump_HoldTimer = 0.0f;
            globals.player.Jump_ChangeTimer = 0.0f;
            globals.player.Jump_CurrGravity = globals.player.g.Gravity;
        }
    }

    if (globals.player.Jump_ChangeTimer)
    {
        globals.player.Jump_ChangeTimer -= dt;

        if (0.0f == globals.player.Jump_ChangeTimer)
        {
            globals.player.Jump_ChangeTimer = 1e-07f;
        }

        if (globals.player.Jump_ChangeTimer <= -globals.player.s->GravSmooth)
        {
            globals.player.Jump_HoldTimer = 0.0f;
            globals.player.Jump_ChangeTimer = 0.0f;
            globals.player.Jump_CurrGravity = globals.player.g.Gravity;
        }
        else if (globals.player.Jump_ChangeTimer < 0.0f)
        {
            lerp = -globals.player.Jump_ChangeTimer / globals.player.s->GravSmooth;
            globals.player.Jump_CurrGravity =
                (1.0f - lerp) * globals.player.s->JumpGravity + lerp * globals.player.g.Gravity;
        }
    }

    cfloor = &ent->collis->colls[0];

    if ((cfloor->flags & 0x1) &&
        !(cfloor->optr && ((xBase*)cfloor->optr)->baseType == eBaseTypeBoulder) &&
        ent->frame->vel.y <= 0.0f)
    {
        if (globals.player.JumpState != 0)
        {
            zEntPlayerJumpLand(ent);
            globals.player.Jump_CanDouble = 1;
            globals.player.Jump_CanFloat = 1;
            globals.player.Jump_Springboard = NULL;
            globals.player.Jump_SpringboardStart = 0;
            globals.player.Bounced = 0;
            zCameraSetBbounce(0);
            zCameraSetLongbounce(0);
            zCameraSetHighbounce(0);
        }
    }
    else
    {
        switch (globals.player.JumpState)
        {
        case 0:
            if (globals.player.SlideTrackSliding & 0x1)
            {
                if (cfloor->dist >= 0.9f)
                {
                    globals.player.JumpState = 1;
                    globals.player.JumpTimer = 0.0f;
                }
            }
            else
            {
                globals.player.JumpState = 1;
                globals.player.JumpTimer = 0.0f;
            }
            break;
        case 1:
            globals.player.JumpTimer += dt;

            if (globals.player.JumpTimer >= 0.5f)
            {
                globals.player.JumpState = 2;
            }
            break;
        case 2:
            if (ent->frame->vel.y < 16.0f && globals.player.Jump_SpringboardStart)
            {
                globals.player.Jump_SpringboardStart = 0;

                if (0.0f == globals.player.Jump_Springboard->plat_asset->sb.jmpdir.x &&
                    0.0f == globals.player.Jump_Springboard->plat_asset->sb.jmpdir.z)
                {
                    globals.player.Jump_CanDouble = 1;
                }
            }
            break;
        case 3:
            ent->frame->vel.x *= 0.96f;
            ent->frame->vel.z *= 0.96f;
            break;
        }
    }

    if (globals.player.Jump_CurrGravity &&
        (globals.player.JumpState == 2 || globals.player.JumpState == 1))
    {
        if (globals.player.WallJumpState == k_WALLJUMP_STUCK)
        {
            if (wallStickTimer > gWallStickTime / 2.0f)
            {
                ent->frame->vel.y = 0.0f;
                ent->frame->vel.x = 0.0f;
                ent->frame->vel.z = 0.0f;
            }
            else
            {
                ent->frame->vel.y -= dt * (0.1f * globals.player.Jump_CurrGravity);
            }
        }
        else
        {
            ent->frame->vel.y -= globals.player.Jump_CurrGravity * dt;
        }
    }

    if (globals.player.JumpState == 0 || globals.player.JumpState == 1)
    {
        globals.player.CanJump = 1;
        globals.player.IsJumping = 0;
        globals.player.WasDJumping = globals.player.IsDJumping;
        globals.player.IsDJumping = 0;
    }
}

static void zEntPlayerEGenUpdate(xEnt* p, xScene* sc, F32)
{
    xCollis* earc_coll;
    S32 i;
    zEGenerator* eg;
    xIsect isx;
    F32 ra;
    xBase* rumble;

    earc_coll = &globals.player.earc_coll;

    earc_coll->flags &= ~0x1;

    for (i = 0; i < ((zScene*)sc)->baseCount[eBaseTypeEGenerator]; i++)
    {
        eg = (zEGenerator*)((zScene*)sc)->baseList[eBaseTypeEGenerator] + i;

        if (eg->flags & 0x1)
        {
            xLine3VecDist2(&eg->src_pos, &eg->dst_pos, &p->bound.sph.center, &isx);
            ra = p->bound.sph.r;
            if (isx.dist < ra * ra + 2.0f * ra * 0.1f + 0.1f * 0.1f)
            {
                earc_coll->flags |= 0x1;

                if (zEntPlayer_Damage(eg, 1, NULL))
                {
                    if (globals.player.s->Health)
                    {
                        globals.player.DamageTimer = globals.player.g.DamageTimeEGen;
                    }

                    rumble = zSceneFindObject(xStrHash("RUMBLE_EGEN_DAMAGE"));
                    if (rumble)
                    {
                        zEntEvent(rumble, eEventStartRumbleEffect, globals.currentActivePad, 0.0f,
                                  0.0f, 0.0f);
                    }
                }
            }
        }
    }
}

static void zEntPlayerVelUpdate(xEnt* ent, xScene* sc, F32 dt)
{
    xVec3* v;
    F32 min;
    F32 interp;
    F32 speedMult;
    F32 s;
    xCollis* coll;
    F32 velen2;
    xCollis* colls;
    S32 i;
    F32 sft;
    xVec3 boost;

    if (ent->model->Anim->Single->State->UserFlags & 0x100)
    {
        return;
    }

    if (globals.player.SlideTrackSliding)
    {
        return;
    }

    v = &ent->frame->vel;

    if (strcmp(ent->model->Anim->Single->State->Name, "BoulderRoll01") == 0)
    {
        min = 2.5f * xglobals->___player_ent_dont_use_directly->model->Mat->at.x;

        if (min > 0.0f)
        {
            if (v->x < min)
            {
                v->x = min;
            }
        }
        else if (v->x > min)
        {
            v->x = min;
        }

        min = 2.5f * xglobals->___player_ent_dont_use_directly->model->Mat->at.z;

        if (min > 0.0f)
        {
            if (v->z < min)
            {
                v->z = min;
            }
        }
        else if (v->z > min)
        {
            v->z = min;
        }
    }
    else if (globals.player.IsBubbleBowling)
    {
        interp = 1.0f + sBubbleBowlTimer;
        interp *= interp;
        interp *= interp;
        interp = 1.0f / interp;
        speedMult = globals.player.g.BubbleBowlMinSpeed * (1.0f - interp) +
                    interp * globals.player.bbowlInitVel;
        v->x = xglobals->___player_ent_dont_use_directly->model->Mat->at.x * speedMult;
        v->z = xglobals->___player_ent_dont_use_directly->model->Mat->at.z * speedMult;
    }
    else if (!(globals.player.SlideTimer > 0.0f) && !surfSlickRatio &&
             !(globals.player.SlipFadeTimer > 0.0f))
    {
        if (globals.player.JumpState == 0)
        {
            if (globals.player.KnockBackTimer)
            {
                s = globals.player.KnockBackTimer / (globals.player.KnockBackTimer + dt);
                v->x *= s;
                v->z *= s;
            }
            else
            {
                v->x = 0.0f;
                v->z = 0.0f;
            }

            coll = &xglobals->___player_ent_dont_use_directly->collis->colls[0];
            xEnt* flent = (coll->flags & 0x1) ? (xEnt*)coll->optr : NULL;

            if (flent == NULL)
            {
                v->y = 0.0f;
            }
            else if (flent->baseType == eBaseTypePlatform &&
                     flent->subType == ZPLATFORM_SUBTYPE_TEETER)
            {
                if (sDriveVel.y < 0.0f)
                {
                    v->y = sDriveVel.y;
                }
                else
                {
                    v->y = 0.0f;
                }
            }
            else
            {
                v->y = 0.0f;
            }
        }
    }
    else
    {
        if (globals.player.Slide == 0 && globals.player.SlideTimer > 0.0f)
        {
            sft = 2.15f - globals.player.SlideTimer;

            if (globals.player.JumpState == 0 || globals.player.JumpState == 1)
            {
                if (globals.player.JumpState == 0)
                {
                    v->y = 0.0f;
                }

                if (sft < 0.4f)
                {
                    s = (0.4f - sft) / ((0.4f - sft) + dt);
                    v->x *= s;
                    v->z *= s;
                }
                else
                {
                    v->x = 0.0f;
                    v->z = 0.0f;
                    return;
                }
            }
            else if (sft < 2.15f)
            {
                s = (2.15f - sft) / ((2.15f - sft) + dt);
                v->x *= s;
                v->z *= s;
            }
            else
            {
                v->x = 0.0f;
                v->z = 0.0f;
                return;
            }
        }

        velen2 = xVec3Length2(v);

        if (xabs(velen2) < 0.0001f)
        {
            v->x = 0.0f;
            v->y = 0.0f;
            v->z = 0.0f;
        }
        else
        {
            i = 0;
            colls = ent->collis->colls;
            coll = colls;

            if (surfSlickRatio)
            {
                if (colls[0].optr)
                {
                    if (ent->frame->mat.pos.y - ent->frame->oldmat.pos.y > 0.0f)
                    {
                        v->x *= 0.97f;
                        v->z *= 0.97f;
                    }
                }
                else if (globals.player.JumpState == 0)
                {
                    v->y = 0.0f;
                }

                coll = colls + 2;
                i = 2;
            }
            else if (!(globals.player.SlideTimer > 0.0f))
            {
                if (globals.player.SlipFadeTimer > 0.0f)
                {
                    s = globals.player.SlipFadeTimer / (globals.player.SlipFadeTimer + dt);
                    v->x *= s;
                    v->z *= s;
                }
                else
                {
                    v->x = 0.0f;
                    v->z = 0.0f;
                    return;
                }
            }

            for (; i < 6; i++, coll++)
            {
                if ((coll->flags & 0x1) && coll->dist < 0.5f &&
                    (!coll->optr || ((xBase*)coll->optr)->baseType != eBaseTypeVillain))
                {
                    if (xVec3Dot(v, &coll->hdng) < 0.0f)
                    {
                        continue;
                    }

                    s = xVec3Dot(v, &coll->norm);

                    if (s > 0.0f)
                    {
                        continue;
                    }

                    s = -s;

                    if (s >= 0.965926f * velen2)
                    {
                        v->x = 0.0f;
                        v->y = 0.0f;
                        v->z = 0.0f;
                        return;
                    }

                    xVec3SMul(&boost, &coll->norm, 0.5f * s);
                    xVec3AddTo(v, &boost);
                    velen2 = xVec3Length2(v);
                }
            }
        }
    }
}

struct TrackPolyData
{
    xVec3 center;
    xMat4x3* mat;
    xEnt* testEnt;
    S32 triIndex;
    xVec3 vert[3];
    F32 neardist;
    xVec3 nearpt;
    S32 nearvert;
    S32 nearedge;
    xEnt* foundEnt;
};

static RpCollisionTriangle* nearestTrackCB(RpIntersection*, RpCollisionTriangle* collTriangle, F32,
                                           void* data)
{
    TrackPolyData* tpoly = (TrackPolyData*)data;
    F32 currnear = tpoly->neardist;
    xVec3 currpt;
    S32 currvert;
    S32 curredge;
    xVec3 xformVert[4];
    xVec3 xformNorm;
    S32 i;
    F32 pdx[3];
    F32 pdz[3];
    F32 pcross[3];
    F32 numer;
    F32 denom;
    F32 t;
    F32 testdist2;
    xVec3 edgevec;
    xVec3 centvec;
    xVec3 testpt;

    xMat4x3Toworld(&xformVert[0], tpoly->mat, (xVec3*)collTriangle->vertices[0]);
    xMat4x3Toworld(&xformVert[1], tpoly->mat, (xVec3*)collTriangle->vertices[1]);
    xMat4x3Toworld(&xformVert[2], tpoly->mat, (xVec3*)collTriangle->vertices[2]);
    xMat3x3RMulVec(&xformNorm, tpoly->mat, (xVec3*)&collTriangle->normal);

    for (i = 0; i < 3; i++)
    {
        pdx[i] = tpoly->center.x - xformVert[i].x;
        pdz[i] = tpoly->center.z - xformVert[i].z;
    }

    pcross[0] = pdx[0] * pdz[1] - pdz[0] * pdx[1];
    pcross[1] = pdx[1] * pdz[2] - pdz[1] * pdx[2];
    pcross[2] = pdx[2] * pdz[0] - pdz[2] * pdx[0];

    if ((pcross[0] >= 0.0f && pcross[1] >= 0.0f && pcross[2] >= 0.0f) ||
        (pcross[0] <= 0.0f && pcross[1] <= 0.0f && pcross[2] <= 0.0f))
    {
        tpoly->neardist = 0.0f;
        tpoly->vert[0] = xformVert[0];
        tpoly->vert[1] = xformVert[1];
        tpoly->vert[2] = xformVert[2];
        tpoly->triIndex = collTriangle->index;
        tpoly->foundEnt = tpoly->testEnt;
        return NULL;
    }

    xformVert[3] = xformVert[0];

    for (i = 0; i < 3; i++)
    {
        edgevec.x = xformVert[i + 1].x - xformVert[i].x;
        edgevec.y = xformVert[i + 1].y - xformVert[i].y;
        edgevec.z = xformVert[i + 1].z - xformVert[i].z;
        centvec.x = tpoly->center.x - xformVert[i].x;
        centvec.y = tpoly->center.y - xformVert[i].y;
        centvec.z = tpoly->center.z - xformVert[i].z;

        numer = centvec.x * edgevec.x + centvec.y * edgevec.y + centvec.z * edgevec.z;
        denom = edgevec.x * edgevec.x + edgevec.y * edgevec.y + edgevec.z * edgevec.z;

        if (denom < 1e-6f)
        {
            return collTriangle;
        }

        if (numer <= 0.0f)
        {
            testdist2 = SQR(centvec.x) + SQR(centvec.y) + SQR(centvec.z);
            if (testdist2 < currnear)
            {
                currnear = testdist2;
                currpt = xformVert[i];
                currvert = i;
                curredge = -1;
            }
        }
        else if (numer < denom)
        {
            t = numer / denom;
            testpt.x = t * edgevec.x + xformVert[i].x;
            testpt.y = t * edgevec.y + xformVert[i].y;
            testpt.z = t * edgevec.z + xformVert[i].z;
            testdist2 = SQR(tpoly->center.x - testpt.x) + SQR(tpoly->center.y - testpt.y) +
                        SQR(tpoly->center.z - testpt.z);
            if (testdist2 < currnear)
            {
                currnear = testdist2;
                currpt = testpt;
                currvert = -1;
                curredge = i;
            }
        }
    }

    if (currnear != tpoly->neardist)
    {
        tpoly->vert[0] = xformVert[0];
        tpoly->vert[1] = xformVert[1];
        tpoly->vert[2] = xformVert[2];
        tpoly->neardist = currnear;
        tpoly->nearpt = currpt;
        tpoly->nearvert = currvert;
        tpoly->nearedge = curredge;
        tpoly->triIndex = collTriangle->index;
        tpoly->foundEnt = tpoly->testEnt;
    }

    return collTriangle;
}

// No symbol in TSSM (always inlined); name from Ratatouille. Determinant of the 3x3 matrix whose
// top row is all ones.
static inline F32 det3x3top1(F32 m21, F32 m22, F32 m23, F32 m31, F32 m32, F32 m33)
{
    return m22 * m33 - m32 * m23 - m21 * m33 + m31 * m23 + m21 * m32 - m31 * m22;
}

static void SlideTrackUpdate(xEnt* p)
{
    xCollis colltmp;
    xSphere collsph;
    xQCData qcd;
    RpIntersection isect;
    xVec3* center;
    TrackPolyData tpoly;
    S32 triIndex;
    U32 i;
    xEnt* trackEnt;
    RwFrame* frame;

    collsph.center = *(xVec3*)&p->model->Mat->pos;
    collsph.r = 2.0f * p->bound.sph.r;
    xQuickCullForSphere(&xqc_def_ctrl, &qcd, &collsph);

    tpoly.triIndex = -1;
    tpoly.neardist = 1e38f;
    triIndex = -1;
    tpoly.center = *(xVec3*)&p->model->Mat->pos;

    isect.type = rpINTERSECTSPHERE;
    center = (xVec3*)&isect.t.sphere.center;
    center->x = p->model->Mat->pos.x;
    center->y = p->model->Mat->pos.y;
    center->z = p->model->Mat->pos.z;
    isect.t.sphere.radius = 2.0f * p->bound.sph.r;

    for (i = 0; i < globals.player.SlideTrackCount; i++)
    {
        trackEnt = globals.player.SlideTrackEnt[i];

        if (xQuickCullIsects(&qcd, &trackEnt->bound.qcd))
        {
            colltmp.flags = 0;
            xSphereHitsBound(&collsph, &trackEnt->bound, &colltmp);

            if (colltmp.flags & k_HIT_IT)
            {
                frame = RpAtomicGetFrame(trackEnt->model->Data);
                frame->ltm = *trackEnt->model->Mat;

                tpoly.mat = (xMat4x3*)trackEnt->model->Mat;
                tpoly.testEnt = trackEnt;

                RpAtomicForAllIntersections(trackEnt->model->Data, &isect, nearestTrackCB, &tpoly);
            }
        }
    }

    if (tpoly.neardist < 1.1f * p->bound.sph.r)
    {
        triIndex = tpoly.triIndex;
    }

    if (triIndex == -1)
    {
        return;
    }

    if (globals.player.JumpState && globals.player.LastJumpState != 1)
    {
        return;
    }

    if (!globals.player.JumpState && globals.player.LastJumpState != 0 &&
        globals.player.LastJumpState != 1)
    {
        globals.player.SlideTrackLand = 1.0f;
        return;
    }

    trackEnt = tpoly.foundEnt;
    RpGeometry* geom = trackEnt->model->Data->geometry;
    RpTriangle* tri = &geom->triangles[triIndex];
    RwTexCoords* tex = geom->texCoords[0];
    RwV3d* vert = geom->morphTarget->verts;
    U16* triidx = tri->vertIndex;

    xVec3 transformedVert0;
    xVec3 transformedVert1;
    xVec3 transformedVert2;
    xMat4x3Toworld(&transformedVert0, (xMat4x3*)trackEnt->model->Mat, (xVec3*)&vert[triidx[0]]);
    xMat4x3Toworld(&transformedVert1, (xMat4x3*)trackEnt->model->Mat, (xVec3*)&vert[triidx[1]]);
    xMat4x3Toworld(&transformedVert2, (xMat4x3*)trackEnt->model->Mat, (xVec3*)&vert[triidx[2]]);

    // not in the PS2 DWARF (names from Ratatouille); retail reuses both values.
    F32 denom = det3x3top1(transformedVert0.x, transformedVert1.x, transformedVert2.x,
                           transformedVert0.z, transformedVert1.z, transformedVert2.z);

    if (xabs(denom) < 1e-5f)
    {
        return;
    }

    F32 vcenter =
        (-tex[triidx[0]].v * det3x3top1(center->x, transformedVert1.x, transformedVert2.x,
                                        center->z, transformedVert1.z, transformedVert2.z) +
         -tex[triidx[1]].v * det3x3top1(transformedVert0.x, center->x, transformedVert2.x,
                                        transformedVert0.z, center->z, transformedVert2.z) +
         -tex[triidx[2]].v * det3x3top1(transformedVert0.x, transformedVert1.x, center->x,
                                        transformedVert0.z, transformedVert1.z, center->z)) /
        denom;

    F32 dvdx =
        (-tex[triidx[0]].v * det3x3top1(1.0f + center->x, transformedVert1.x, transformedVert2.x,
                                        center->z, transformedVert1.z, transformedVert2.z) +
         -tex[triidx[1]].v * det3x3top1(transformedVert0.x, 1.0f + center->x, transformedVert2.x,
                                        transformedVert0.z, center->z, transformedVert2.z) +
         -tex[triidx[2]].v * det3x3top1(transformedVert0.x, transformedVert1.x, 1.0f + center->x,
                                        transformedVert0.z, transformedVert1.z, center->z)) /
        denom;

    F32 dvdz =
        (-tex[triidx[0]].v * det3x3top1(center->x, transformedVert1.x, transformedVert2.x,
                                        1.0f + center->z, transformedVert1.z, transformedVert2.z) +
         -tex[triidx[1]].v * det3x3top1(transformedVert0.x, center->x, transformedVert2.x,
                                        transformedVert0.z, 1.0f + center->z, transformedVert2.z) +
         -tex[triidx[2]].v * det3x3top1(transformedVert0.x, transformedVert1.x, center->x,
                                        transformedVert0.z, transformedVert1.z, 1.0f + center->z)) /
        denom;

    dvdx -= vcenter;
    dvdz -= vcenter;

    F32 mag = xsqrt(dvdx * dvdx + dvdz * dvdz);

    if (mag < 1e-5f)
    {
        return;
    }

    globals.player.SlideTrackDir.x = dvdx / mag;
    globals.player.SlideTrackDir.z = dvdz / mag;
    globals.player.SlideTrackSliding |= 1;
    globals.player.SlideNotGroundedSinceSlide = 1;
}

static void zEntPlayerTSlideUpdate(xEnt* p, xScene* sc, F32 dt)
{
    globals.player.SlideTrackLand = 0.0f;
    globals.player.SlideTrackSliding = (globals.player.SlideTrackSliding & 1) << 1;

    if (globals.player.SlideTrackCount)
    {
        SlideTrackUpdate(p);
    }

    if ((globals.player.SlideTrackSliding & 1) && !(globals.player.SlideTrackSliding & 2))
    {
        globals.player.SlideTrackVel.x = tslide_lastrealvel.x;
        globals.player.SlideTrackVel.y = 0.0f;
        globals.player.SlideTrackVel.z = tslide_lastrealvel.z;

        p->frame->vel.x = 0.0f;
        p->frame->vel.y = 0.0f;
        p->frame->vel.z = 0.0f;

        tslide_inair_tmr = 0.0f;
        tslide_dbl_tmr = 0.0f;
        tslide_ground = 1;
    }
    else if (!(globals.player.SlideTrackSliding & 1) && (globals.player.SlideTrackSliding & 2) &&
             globals.player.JumpState)
    {
        tslide_inair_tmr = dt;
        tslide_dbl_tmr = 0.0f;
    }

    if (!(globals.player.SlideTrackSliding & 1) && !globals.player.JumpState)
    {
        tslide_ground = 0;
    }

    if (globals.player.JumpState && tslide_ground)
    {
        if (tslide_inair_tmr)
        {
            if (tslide_inair_tmr > globals.player.g.SlideAirHoldTime)
            {
                static F32 tmax =
                    globals.player.g.SlideAirHoldTime + globals.player.g.SlideAirSlowTime;

                if (tslide_inair_tmr >= tmax)
                {
                    p->frame->vel.x = 0.0f;
                    p->frame->vel.z = 0.0f;
                }
                else
                {
                    F32 t = tmax - tslide_inair_tmr;
                    F32 s = (t - dt) / t;
                    p->frame->vel.x *= s;
                    p->frame->vel.z *= s;
                }
            }

            tslide_inair_tmr += dt;
        }

        if (tslide_dbl_tmr)
        {
            if (tslide_dbl_tmr > globals.player.g.SlideAirDblHoldTime)
            {
                static F32 tmax =
                    globals.player.g.SlideAirDblHoldTime + globals.player.g.SlideAirDblSlowTime;

                if (tslide_dbl_tmr >= tmax)
                {
                    p->frame->vel.x = 0.0f;
                    p->frame->vel.z = 0.0f;
                }
                else
                {
                    F32 t = tmax - tslide_dbl_tmr;
                    F32 s = (t - dt) / t;
                    p->frame->vel.x *= s;
                    p->frame->vel.z *= s;
                }
            }

            tslide_dbl_tmr += dt;
        }
    }

    if (!globals.player.JumpState)
    {
        tslide_inair_tmr = 0.0f;
    }

    if (globals.player.cheat_mode)
    {
        globals.player.SlideTrackSliding &= ~1;
        globals.player.SlideTrackSliding |= 2;
        globals.player.JumpState = 2;
        globals.player.SlideTrackVel.x = 0.0f;
        globals.player.SlideTrackVel.y = 0.0f;
        globals.player.SlideTrackVel.z = 0.0f;
        p->frame->vel.x = 0.0f;
        p->frame->vel.y = 0.0f;
        p->frame->vel.z = 0.0f;
        tslide_inair_tmr = 0.0f;
        tslide_dbl_tmr = 0.0f;
        tslide_ground = 0;
        tslide_maxspd_tmr = 0.0f;
        globals.player.SlideTrackDecay = 0.0f;
    }
}

// Declared here for retail's .sbss order; used by zEntPlayerCollide and PlayerCollCheckOneVillain.
static U32 cchkButtbounce;
static U32 cchkSquish;

static void zEntPlayerFloorUpdate(xEnt* p, xScene* sc, F32 dt)
{
    static xVec3 lorigins[4] = {
        { -0.3f, 0.0f, 0.3f },
        { 0.3f, 0.0f, 0.3f },
        { 0.3f, 0.0f, -0.3f },
        { -0.3f, 0.0f, -0.3f },
    };

    xCollis* fcoll;
    xSurface* fsurf;
    xVec3* vel;
    xVec3 dvel;
    F32 velmag;
    F32 decelmag;
    F32 slickLerp;
    xVec3* fnorm;
    xRay3 ray;
    xCollis rcoll;
    xEnt* fent;
    S32 i;
    F32 dave;
    S32 safecount;
    F32 dmin;
    F32 dmax;
    xVec3 sa;
    xVec3 sb;
    xVec3 na;
    xVec3 nb;

    fcoll = &p->collis->colls[0];
    fsurf = zSurfaceGetSurface(fcoll);
    globals.player.floor_surf = fsurf;

    surfSlickTimer -= dt;
    if (surfSlickTimer < 0.0f)
    {
        surfSlickTimer = 0.0f;
    }

    surfSticky = 0;
    surfSlickRatio = 0.0f;

    if ((globals.player.JumpState == 0 || globals.player.JumpState == 1) && fsurf && !fsurf->state)
    {
        surfSticky = zSurfaceGetSticky(fsurf);
    }

    if (fsurf && !fsurf->state)
    {
        surfFriction = zSurfaceGetFriction(fsurf);
        surfSlickness = zSurfaceGetSlickness(fsurf);
        surfDamping = zSurfaceGetDamping(fsurf, minVelmag);
        surfSlipTimer = 0.00001f;
    }
    else
    {
        surfFriction = 1.0f;
        surfDamping = minVelmag;

        if (globals.player.JumpState != 0)
        {
            surfSlipTimer = 0.0f;
        }

        if (surfSlipTimer)
        {
            surfSlipTimer += dt;
            if (surfSlipTimer >= 0.75f)
            {
                surfSlipTimer = 0.0f;
            }
        }

        if (!surfSlipTimer)
        {
            surfSlickness = 1;
        }
    }

    if (globals.player.ForceSlipperyTimer > 0.0f)
    {
        if ((globals.player.JumpState == 0 || globals.player.JumpState == 1) &&
            globals.player.WallJumpState == k_WALLJUMP_NOT)
        {
            surfFriction = globals.player.ForceSlipperyFriction;
            surfSlickness = (S32)(1.0f / globals.player.ForceSlipperyFriction);
            surfDamping = xpow(minVelmag, globals.player.ForceSlipperyFriction);
            surfSlipTimer = 0.00001f;
        }

        if (globals.player.JumpState != 1)
        {
            globals.player.ForceSlipperyTimer -= dt;
        }
    }

    if (surfSlickness != 1)
    {
        if (surfSlickness > 1)
        {
            surfSlickRatio = 150.0f / surfSlickness;
        }
        else
        {
            surfSlickRatio = 1.0f;
        }

        surfSlickTimer = 0.1f;
    }
    else
    {
        surfSlickRatio = 0.0f;
    }

    vel = &p->frame->vel;

    if (lastSlickness != surfSlickness)
    {
        if (surfSlickness == 1)
        {
            if (lastFloorEnt)
            {
                globals.player.SlipFadeTimer = 0.8f;
            }
            else
            {
                vel->x = 0.0f;
                vel->z = 0.0f;
            }
        }
        else if (globals.player.ForceSlipperyFriction == 0.0f)
        {
            xVec3SMul(&dvel, &lastDeltaPos, 1.0f / dt);
            vel->x = dvel.x;
            vel->z = dvel.z;
        }
    }

    if (surfSlickRatio)
    {
        velmag = vel->x * vel->x + vel->z * vel->z;

        if (surfMaxSpeed)
        {
            if (velmag > surfMaxSpeed * surfMaxSpeed)
            {
                velmag = surfMaxSpeed / xsqrt(velmag);
                vel->x *= velmag;
                vel->z *= velmag;
            }
        }
        else
        {
            if (xStricmp(p->model->Anim->Single->State->Name, "Skid"))
            {
                decelmag = surfDecelIdle * dt;
            }
            else
            {
                decelmag = surfDecelSkid * dt;
            }

            slickLerp = 1.3333334f * surfSlipTimer;
            decelmag = (1.0f - slickLerp) * decelmag + 20.0f * slickLerp * decelmag;

            velmag = xsqrt(velmag);
            if (velmag <= decelmag)
            {
                vel->x = 0.0f;
                vel->z = 0.0f;
            }
            else
            {
                velmag = (velmag - decelmag) / velmag;
                vel->x *= velmag;
                vel->z *= velmag;
            }
        }
    }
    else if (globals.player.SlipFadeTimer > 0.0f)
    {
        globals.player.SlipFadeTimer -= dt;
        if (globals.player.SlipFadeTimer < 0.0f)
        {
            globals.player.SlipFadeTimer = 0.0f;
        }
    }

    fnorm = &fcoll->norm;
    lastSlickness = surfSlickness;

    if (fcoll->flags & 0x1)
    {
        xVec3Copy(&globals.player.floor_norm, fnorm);
    }
    else
    {
        xVec3Copy(&globals.player.floor_norm, &g_Y3);
        xVec3Copy(&ray.origin, xBoundCenter(&p->bound));
        ray.dir.x = 0.0f;
        ray.dir.y = -1.0f;
        ray.dir.z = 0.0f;
        ray.min_t = 0.0f;
        ray.max_t = 10.0f;
        ray.flags = 0x800;
        rcoll.flags = 0;
        rcoll.optr = NULL;
        rcoll.mptr = NULL;
        rcoll.dist = 1e38f;
        xRayHitsScene(globals.sceneCur, &ray, &rcoll);
        fcoll->dist = rcoll.dist;
    }

    ray.dir.x = 0.0f;
    ray.dir.y = -1.0f;
    ray.dir.z = 0.0f;
    ray.min_t = -0.7f;
    ray.max_t = 1.0f;
    ray.flags = 0xc00;

    dave = 0.0f;
    safecount = 0;

    fent = (xEnt*)fcoll->optr;
    if (fent)
    {
        for (i = 0; i < 4; i++)
        {
            xMat4x3Toworld(&ray.origin, &p->frame->mat, &lorigins[i]);
            rcoll.flags = 0;
            rcoll.optr = NULL;
            rcoll.mptr = NULL;
            rcoll.dist = 1e38f;

            if (fent->collLev == 5)
            {
                xRayHitsModel(&ray, fent->model, &rcoll);
            }
            else
            {
                xRayHitsBound(&ray, &fent->bound, &rcoll);
            }

            floor_supp[i] = ray.origin;
            floor_dist[i] = rcoll.dist;

            if (rcoll.flags & 0x1)
            {
                dave += rcoll.dist;
                if (!zGooIs(fent))
                {
                    safecount++;
                }
            }
        }
    }
    else
    {
        for (i = 0; i < 4; i++)
        {
            xMat4x3Toworld(&ray.origin, &p->frame->mat, &lorigins[i]);
            rcoll.flags = 0;
            rcoll.optr = NULL;
            rcoll.mptr = NULL;
            rcoll.dist = 1e38f;

            xRayHitsEnv(&ray, globals.sceneCur->env, &rcoll);

            floor_supp[i] = ray.origin;
            floor_dist[i] = rcoll.dist;

            if (rcoll.flags & 0x1)
            {
                dave += rcoll.dist;
                if (!(rcoll.flags & 0x20000))
                {
                    safecount++;
                }
            }
        }
    }

    if (safecount == 4 && globals.player.JumpState == 0)
    {
        floor_safe_vec = *(xVec3*)&p->model->Mat->pos;
        floor_safe_tmr = 0.0f;
        sGooKnockedToSafety = 0;
        sGooKnockedTimer = 0.0f;
    }
    else
    {
        floor_safe_tmr += dt;
    }

    dave *= 0.25f;
    dmin = dave - 0.17320508f;
    dmax = 0.17320508f + dave;

    for (i = 0; i < 4; i++)
    {
        floor_supp[i].y -= MAX(dmin, MIN(floor_dist[i], dmax));
    }

    xVec3Sub(&sa, &floor_supp[1], &floor_supp[0]);
    xVec3Sub(&sb, &floor_supp[2], &floor_supp[0]);
    xVec3Cross(&na, &sa, &sb);
    xVec3Sub(&sa, &floor_supp[2], &floor_supp[0]);
    xVec3Sub(&sb, &floor_supp[3], &floor_supp[0]);
    xVec3Cross(&nb, &sa, &sb);
    xVec3Add(&globals.player.floor_norm, &na, &nb);
    xVec3Normalize(&globals.player.floor_norm, &globals.player.floor_norm);

    if (globals.player.Slide == 1 || globals.player.Slide == 2)
    {
        xVec3* vel = &p->frame->vel;

        if (((fcoll->flags & 0x1) && (fnorm->y > icos(surfSlideStop) ||
                                      (fsurf && (fsurf->state || !zSurfaceGetSlide(fsurf))))) ||
            vel->y > 0.0f || surfSlickRatio)
        {
            if (surfSlickRatio)
            {
                globals.player.SlideTimer = 0.0f;
            }

            globals.player.Slide = 0;
        }
    }
    else if ((fcoll->flags & 0x1) && fnorm->y < icos(zSurfaceGetSlideStartAngle(fsurf)) && fsurf &&
             !fsurf->state && zSurfaceGetSlide(fsurf))
    {
        xVec3* N = fnorm;
        xVec3 X;
        xVec3 D;
        xMat3x3 N_to_Up;
        xVec3 D_flat;
        F32 ddotm;

        X.x = -N->z;
        X.y = 0.0f;
        X.z = N->x;
        xVec3Normalize(&X, &X);
        xVec3Cross(&D, N, &X);
        xVec3Normalize(&D, &D);
        xMat3x3Rot(&N_to_Up, &X, xacos(N->y));
        xMat3x3RMulVec(&D_flat, &N_to_Up, &D);

        ddotm = D_flat.x * req_motion.x + D_flat.z * req_motion.z;

        if (ddotm < 0.0f &&
            (globals.player.LastJumpState == 0 || globals.player.LastJumpState == 1))
        {
            F32 s;

            globals.player.Slide = 3;

            s = ddotm / xsqrt(D_flat.x * D_flat.x + D_flat.z * D_flat.z);
            // not in the DWARF: a separate output vector, rather than scaling D_flat in place,
            // gives retail's registers.
            xVec3 delta;
            xVec3SMul(&delta, &D_flat, s);
            p->frame->mat.pos.x -= delta.x;
            p->frame->mat.pos.z -= delta.z;

            if (update_motion.y > 0.0f)
            {
                p->frame->mat.pos.y = p->frame->oldmat.pos.y;
            }
        }
        else
        {
            F32 s;

            globals.player.Slide = 1;
            surfSlideStart = zSurfaceGetSlideStartAngle(fsurf);
            surfSlideStop = zSurfaceGetSlideStopAngle(fsurf);
            p->pflags |= 0x80;

            s = 0.5f / update_dt;
            p->frame->vel.x += req_motion.x * s;
            p->frame->vel.z += req_motion.z * s;
            p->frame->vel.y = -4.0f;
        }

        globals.player.SlideTimer = 0.0f;
    }
    else
    {
        globals.player.Slide = 0;
    }

    if (globals.player.Slide == 1)
    {
        globals.player.SlideTimer += dt;
    }
    else if (globals.player.Slide == 0)
    {
        if (globals.player.SlideTimer > 0.0f)
        {
            globals.player.SlideTimer -= dt;
            if (globals.player.SlideTimer < 0.0f)
            {
                globals.player.SlideTimer = 0.0f;
            }
        }
    }
}

static U32 PlayerDepenQuery(xEnt* ent, xEnt* other, xScene* scene, F32 dt, xCollis* coll)
{
    xSurface* surf;
    zSurfaceProps* prop;

    if (globals.player.DamageTimer > 0.0f)
    {
        surf = zSurfaceGetSurface(coll);
        if (surf && !surf->state)
        {
            prop = (zSurfaceProps*)surf->moprops;
            if (prop && prop->asset)
            {
                switch (prop->asset->game_damage_type)
                {
                case 0:
                    return 1;
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                    if (prop->asset->game_damage_flags & 1)
                    {
                        return 0;
                    }
                    break;
                }
            }
        }
    }

    return 1;
}

static void PlayerBoundUpdate(xEnt* ent, xVec3* pos)
{
    xEntDefaultBoundUpdate(ent, pos);
    ent->bound.sph.r = 0.5f;
    ent->bound.sph.center.y -= ent->bound.sph.r;
    xVec3AddScaled(&ent->bound.sph.center, &globals.player.RootUp, ent->bound.sph.r - 0.2f);
}

static void zEntPlayerSurfDamageUpdate(xEnt* p, xScene* sc, F32 dt)
{
    xCollis* coll;
    xCollis* cend;
    xSurface* surf;
    S32 damaged;

    if (globals.player.DamageTimer > 0.0f)
    {
        return;
    }

    coll = p->collis->colls;
    cend = coll + p->collis->idx;

    for (; coll < cend; coll++)
    {
        if ((coll->flags & 0x1) && (surf = zSurfaceGetSurface(coll)) != NULL && !surf->state)
        {
            zSurfaceProps* prop = (zSurfaceProps*)surf->moprops;
            damaged = 0;

            if (prop && prop->asset)
            {
                switch (prop->asset->game_damage_type)
                {
                case 0:
                    break;
                case 4:
                case 6:
                    if (zEntPlayer_Damage(surf, 1, NULL))
                    {
                        if (globals.player.s->Health)
                        {
                            globals.player.DamageTimer = prop->asset->damage_timer ?
                                                             prop->asset->damage_timer :
                                                             globals.player.g.DamageTimeSurface;
                        }

                        if (globals.player.s->Health)
                        {
                            if (tslide_ground || coll == p->collis->colls ||
                                prop->asset->damage_bounce)
                            {
                                zEntPlayer_DamageKnockIntoAir(prop->asset->damage_bounce ?
                                                                  prop->asset->damage_bounce :
                                                                  globals.player.g.DamageSurfKnock);
                            }
                            else
                            {
                                F32 dx = -coll->tohit.x;
                                F32 dz = -coll->tohit.z;
                                F32 mag = xsqrt(dx * dx + dz * dz);

                                if (mag > 0.01f)
                                {
                                    mag = sPlayerNPC_KnockBackVel / mag;
                                    xglobals->___player_ent_dont_use_directly->frame->vel.x =
                                        mag * dx;
                                    xglobals->___player_ent_dont_use_directly->frame->vel.y = 0.0f;
                                    xglobals->___player_ent_dont_use_directly->frame->vel.z =
                                        mag * dz;
                                    globals.player.KnockIntoAirTimer = 0.0f;
                                    globals.player.KnockBackTimer = sPlayerNPC_KnockBackTime;
                                }
                            }
                        }
                    }

                    damaged = 1;
                    break;
                case 1:
                {
                    xEnt* cent = (xEnt*)coll->optr;

                    if (cent && cent->baseType == eBaseTypeEGenerator &&
                        !(((zEGenerator*)cent)->flags & 0x1))
                    {
                        break;
                    }
                }
                    // fall through
                case 2:
                case 3:
                case 5:
                    if (globals.player.s->Health)
                    {
                        globals.player.DamageTimer = 0.0f;
                        zEntPlayer_Damage(surf, globals.player.s->Health);
                        damaged = 1;
                    }
                    break;
                case 7:
                    if (globals.player.s->Health)
                    {
                        globals.player.DamageTimer = 0.0f;
                        zEntPlayer_Damage(surf, 1);
                        damaged = 1;
                    }
                    break;
                }

                if (damaged)
                {
                    return;
                }
            }
        }
    }
}

// No GC symbol (always inlined); BfBB has it as a function of the same name.
static inline void PlayerMountHackUpdate(F32 delta)
{
    sMountTimer += delta;

    if (sMountTimer > 0.1f && sMountObject)
    {
        zEntEvent(sMountObject, sMountType);
        sMountObject = NULL;
        sMountType = 0;
    }
}

// No GC symbol (always inlined); BfBB has it as a function of the same name.
static inline void PlayerMountHackTakeAction(xEnt* ent, U32 type)
{
    if (sMountTimer > 0.1f)
    {
        zEntEvent(ent, type);
    }
    else
    {
        sMountObject = ent;
        sMountType = type;
    }

    sMountTimer = 0.0f;
}

// No GC symbol (always inlined): plays one of the player's SFX groups unless it is still playing.
static inline void PlayerSFXPlay(S32 which, const xVec3* pos)
{
    if (zEntPlayerFX::SFX::Snd[which] != -1 &&
        !xSndMgrIsPlaying(zEntPlayerFX::SFX::SndGroup[which]))
    {
        if (pos == NULL)
        {
            zEntPlayerFX::SFX::Snd[which] =
                xSndMgrPlay(zEntPlayerFX::SFX::SndGroup[which], 0, NULL, NULL, NULL, NULL, NULL);
        }
        else
        {
            zEntPlayerFX::SFX::Snd[which] =
                xSndMgrPlay(zEntPlayerFX::SFX::SndGroup[which], 0, pos, NULL, NULL, NULL, NULL);
        }
    }
}

static void zEntPlayerDriveUpdate(xEnt* p, xScene* sc, F32 dt)
{
    xCollis* coll;
    xEnt* floorEnt;
    zPlatform* plat;
    xEntDrive* drv;
    zJumpParam jump; // not in the DWARF: the parameter block of CalcJumpImpulse/zEntPlayerJumpStart

    if (globals.player.carry.patLauncher)
    {
        plat = (zPlatform*)globals.player.carry.patLauncher;
        globals.player.carry.patLauncher = NULL;

        // Retail (GC and PS2) shares one copy of the bounce code between this path and the
        // springboard path below, with no flag local in the DWARF: a jump into it.
        if (globals.player.s->Health)
        {
            goto do_bounce;
        }
    }

    PlayerMountHackUpdate(dt);

    drv = &globals.player.drv;

    if (drv->old_driver)
    {
        if (drv->old_driver->baseType != eBaseTypePlatform)
        {
            if (!drv->os)
            {
                drv->otm = 0.0f;
                drv->old_driver = NULL;
            }
        }
        else
        {
            zPlatform* oplat = (zPlatform*)drv->old_driver;

            if (!drv->os)
            {
                drv->otm = 0.0f;
                drv->old_driver = NULL;

                if (oplat->subType == ZPLATFORM_SUBTYPE_BREAKAWAY)
                {
                    zPlatform_Dismount(oplat);
                }

                if (oplat->plat_asset->flags & 0x1)
                {
                    zPlatform_Shake(oplat, dt, 0.2f, 12.0f * PI);
                }

                if (oplat->subType == ZPLATFORM_SUBTYPE_SPRINGBOARD)
                {
                    zPlatform_Dismount(oplat);
                }
            }
        }
    }

    coll = &p->collis->colls[0];

    if (coll->flags & 0x1)
    {
        floorEnt = (xEnt*)coll->optr;
    }
    else
    {
        floorEnt = NULL;
    }

    if (!floorEnt)
    {
        if (!drv->driver)
        {
            return;
        }

        PlayerMountHackTakeAction(drv->driver, 0x21);

        // A tiki has no frame of its own: give it a temporary one for the dismount.
        xEntFrame* dummyFrame = NULL;
        xEntFrame** ppDummyFrameUser = NULL;

        if (drv->driver->baseType == eBaseTypeNPC &&
            ((zNMECommon*)drv->driver)->SelfType() >= NME_TYPE_TIKI &&
            ((zNMECommon*)drv->driver)->SelfType() <= NME_TYPE_TIKI_STONE)
        {
            dummyFrame = (xEntFrame*)xMemPushTemp(sizeof(xEntFrame));
            drv->driver->frame = dummyFrame;
            ppDummyFrameUser = &drv->driver->frame;
            xMat4x3Copy(&drv->driver->frame->mat, xEntGetFrame(drv->driver));
        }

        if (globals.player.JumpState != 0 && globals.player.JumpState != 1)
        {
            xEntDriveDismount(drv, 0.0001f);
        }
        else
        {
            xEntDriveDismount(drv, 0.3f);
        }

        if (dummyFrame)
        {
            *ppDummyFrameUser = NULL;
            xMemPopTemp(dummyFrame);
        }

        return;
    }

    xSurface* surf;
    surf = zSurfaceGetSurface(coll);

    if (surf && !surf->state && zSurfaceGetDamageType(surf))
    {
        return;
    }

    if (floorEnt != drv->driver)
    {
        if (drv->driver)
        {
            PlayerMountHackTakeAction(drv->driver, 0x21);
            xEntDriveDismount(drv, 0.3f);
        }

        if (floorEnt->baseType == eBaseTypeBoulder)
        {
            if (boulderVehicle && floorEnt != boulderVehicle)
            {
                xEntDriveMount(drv, floorEnt, 0.15f, coll);
            }
            return;
        }

        if (floorEnt->baseType == eBaseTypeNPC &&
            ((zNMECommon*)floorEnt)->SelfType() >= NME_TYPE_TIKI &&
            ((zNMECommon*)floorEnt)->SelfType() <= NME_TYPE_TIKI_STONE)
        {
            // A tiki has no frame of its own: lend it one for the mount.
            xEntFrame dummyFrame;

            floorEnt->frame = &dummyFrame;
            xMat4x3Copy(&floorEnt->frame->mat, xEntGetFrame(floorEnt));
            xEntDriveMount(drv, floorEnt, 0.15f, coll);
            floorEnt->frame = NULL;
            return;
        }

        if (floorEnt->baseType != eBaseTypePlatform && (floorEnt->moreFlags & 0x2) &&
            (floorEnt->moreFlags & 0x20))
        {
            xEntDriveMount(drv, floorEnt, 0.15f, coll);
            return;
        }

        if (floorEnt->baseType == eBaseTypePendulum)
        {
            xEntDriveMount(drv, floorEnt, 0.3f, coll);
            PlayerMountHackTakeAction(floorEnt, 0x20);
            return;
        }

        if (floorEnt->baseType == eBaseTypeStatic && floorEnt->driver)
        {
            xEntDriveMount(drv, floorEnt, -1.0f, coll);
            return;
        }

        if (floorEnt->baseType != eBaseTypePlatform)
        {
            return;
        }

        plat = (zPlatform*)floorEnt;

        if (!(plat->plat_flags & 0x1))
        {
            return;
        }

        xEntDriveMount(drv, plat, -1.0f, coll);
        PlayerMountHackTakeAction(plat, 0x20);

        if (plat->subType == ZPLATFORM_SUBTYPE_BREAKAWAY)
        {
            zPlatform_Mount(plat);
        }

        if (plat->plat_asset->flags & 0x1)
        {
            zPlatform_Shake(plat, dt, 0.2f, 12.0f * PI);
        }

        if (plat->subType == ZPLATFORM_SUBTYPE_SPRINGBOARD)
        {
            zPlatform_Mount(plat);
            PlayerSFXPlay(3, xEntGetPos(plat));
            zFX_SpawnBubbleSlam(xEntGetPos(xglobals->___player_ent_dont_use_directly), 48, 0.15f,
                                6.0f, 1.0f);
        }
    }
    else
    {
        plat = (zPlatform*)drv->driver;
    }

    if (plat->subType != ZPLATFORM_SUBTYPE_SPRINGBOARD)
    {
        return;
    }

    if (!globals.player.s->Health)
    {
        return;
    }

    if ((plat->plat_asset->flags & 0x2) &&
        (globals.player.JumpState == 0 || globals.player.JumpState == 1))
    {
        return;
    }

    if (plat->plat_flags & 0x2)
    {
        return;
    }

do_bounce:
    if (plat->tmr <= 0.0f)
    {
        plat->ctr = 0;
    }
    else if (plat->ctr < 2)
    {
        plat->ctr++;
    }

    F32 jmph;
    U32 superbounce = 0;

    if (strcmp(p->model->Anim->Single->State->Name, "BbounceAttack01") == 0 &&
        plat->plat_asset->sb.jmpbounce)
    {
        jmph = plat->plat_asset->sb.jmpbounce;
        superbounce = 1;
    }
    else
    {
        F32* jmphs = plat->plat_asset->sb.jmph;
        jmph = MAX(MAX(jmphs[0], jmphs[1]), jmphs[2]);
    }

    xVec3* jmpdir = &plat->plat_asset->sb.jmpdir;

    jump.PeakHeight = jmph;
    jump.TimeHold = 0.0f;
    jump.TimeGravChange = 0.3f;
    CalcJumpImpulse(&jump, NULL);

    startBounce = p->frame->mat.pos.y;
    zEntPlayerJumpStart(p, &jump);

    // Left over from a stripped sound call: the retail build still hashes the model name.
    xStrHash("tree_bounce_bind_top");

    xVec3SMul(&p->frame->vel, jmpdir, p->frame->vel.y);

    globals.player.Jump_CanDouble = 0;
    globals.player.Jump_CanFloat = 1;
    globals.player.Jump_Springboard = plat;
    globals.player.Jump_SpringboardStart = 1;
    zEntPlayerFX::MotionBlur::Blurring = false;
    zEntPlayerFX::MotionBlur::EntToTrack = NULL;

    if (plat->plat_asset->sb.springflags & 0x4)
    {
        zEntPlayerControlOff(CONTROL_OWNER_SPRINGBOARD);
    }
    else
    {
        zEntPlayerControlOn(CONTROL_OWNER_SPRINGBOARD);
    }

    xAnimPlay* aplay = plat->model->Anim;
    xAnimState* spring_state;

    if (aplay && (spring_state = xAnimTableGetState(aplay->Table, "Spring")) != NULL)
    {
        xAnimPlaySetState(aplay->Single, spring_state, 0.0f);
    }

    globals.player.Bounced = 1;
    zCameraSetBbounce(1);

    if ((plat->plat_asset->sb.springflags & 0x1) &&
        (!(plat->plat_asset->sb.springflags & 0x2) || superbounce))
    {
        zCameraSetHighbounce(1);
    }
    else if (jmpdir->x * jmpdir->x + jmpdir->z * jmpdir->z > jmpdir->y * jmpdir->y)
    {
        zCameraSetLongbounce(1);
    }
    else
    {
        zCameraSetHighbounce(0);
    }

    tslide_inair_tmr = 0.0f;
    tslide_dbl_tmr = 0.0f;
    tslide_ground = 0;
    globals.player.SlideTrackDecay = 0.0f;
}

// No GC symbol (always inlined).
static inline void PlayerCollisBuildFromDepen(xCollis* coll)
{
    F32 len = xVec3Length(&coll->depen);

    if (len)
    {
        xVec3SMul(&coll->hdng, &coll->depen, -1.0f / len);
        coll->dist = 0.5f - len;
        xVec3SMul(&coll->tohit, &coll->hdng, coll->dist);
    }
}

static U32 sTrackHash[32] = {
    0x5904a48f, 0x5904a498, 0x5904a512, 0x5904a51b, 0x5904a595, 0x5904a59e, 0x5904a618, 0x5904a621,
    0x5904a69b, 0x5904a6a4, 0x5904a71e, 0x5904a727, 0x5904a7a1, 0x5904a7aa, 0x5904a824, 0x5904a82d,
    0x5904a8a7, 0x5904a8b0, 0x5904a92a, 0x5904a933, 0x8d607866, 0x8d60786f, 0x8d6078e9, 0x8d6078f2,
    0x8d60796c, 0x8d607975, 0x8d6079ef, 0x8d6079f8, 0x8d607a72, 0x8d607a7b, 0x8d607af5, 0x8d607afe,
};

// No GC symbol: inlined into zEntPlayerReset.
// Only the first 10 ranges are searched, so the 0x8d60xxxx ranges never match (as in retail).
S32 zEntPlayer_ObjIDIsTrack(U32 id)
{
    S32 i;

    if (id == 0xcd8dd617)
    {
        return 1;
    }

    if (id >= 0x2f948df5 && id <= 0x2f948dfe)
    {
        return 1;
    }

    if ((id >= 0x5904a48f && id <= 0x5904a933) || (id >= 0x8d607866 && id <= 0x8d607afe))
    {
        for (i = 0; i < 10; i++)
        {
            if (id >= sTrackHash[i * 2] && id <= sTrackHash[i * 2 + 1])
            {
                return 1;
            }
        }
    }

    return 0;
}

void zEntPlayerReset(xEnt* ent)
{
    S32 i;

    gInitShadow = 0;

    xVec3Init(&globals.player.PredictCurrDir, 0.0f, 1.0f, 0.0f);
    globals.player.PredictCurrVel = 0.0f;

    for (i = 0; i <= 1; i++)
    {
        xEntBoulder_Kill(globals.player.bubblebowl[i]);
    }

    if (zEntPlayerGetBBowl())
    {
        zEntPlayerFX::SuperMachoBowlLightning::RemoveAll();
    }

    zEntPlayerFX::HammerThrow::SetDust(false);
    zEntPlayerFX::HammerThrow::SetBlur(false);

    if (!gSpongeBall && !boulderRollShouldStart && boulderVehicle)
    {
        boulderVehicle->flags &= (U8)~0x40;
        boulderVehicle->asset->flags &= (U8)~0x40;
        boulderVehicle->flags &= (U8)~0x8;
        xEntBoulder_Kill(boulderVehicle);

        if (boulderVehicle->model->Anim)
        {
            xAnimPlaySetState(boulderVehicle->model->Anim->Single,
                              boulderVehicle->model->Anim->Table->StateList, 0.0f);
        }
    }

    xglobals->___player_ent_dont_use_directly->update = zPlayerUpdate;
    ent->move = zEntPlayer_Move;
    xglobals->___player_ent_dont_use_directly->render = (xEntRenderCallback)zPlayerRender;

    if (zGameModeGet() == eGameMode_Game &&
        (zGameStateGet() == eGameState_Play || zGameStateGet() == eGameState_GameStats))
    {
    }
    else if (zGameModeGet() == eGameMode_Game && (zGameStateGet() == eGameState_SceneSwitch ||
                                                  zGameStateGet() == eGameState_LoseChance))
    {
        HealthReset();
    }
    else
    {
        InvReset();
    }

    globals.player.ControlOnEvent = 0;
    globals.player.Speed = 0;
    globals.player.SpeedMult = 1.0f;
    globals.player.Sneak = 0;
    globals.player.Teeter = 0;
    globals.player.Slide = 0;
    globals.player.SlideTimer = 0.0f;
    globals.player.SlideNotGroundedSinceSlide = 0;
    globals.player.Stepping = 0;
    globals.player.JumpState = 0;
    globals.player.LastJumpState = 0;
    globals.player.IdleMinorTimer = 0.0f;
    globals.player.IdleMajorTimer = 0.0f;
    globals.player.IdleSitTimer = 0.0f;
    globals.player.cheat_mode = 0;
    globals.player.FireTarget = NULL;
    globals.player.LeanLerp = 1.0f;
    globals.player.ScareTimer = 0.0f;
    globals.player.DamageTimer = 0.0f;
    globals.player.SundaeTimer = -1.0f;
    globals.player.ControlOffTimer = 0.0f;
    globals.player.HelmetTimer = 0.0f;
    globals.player.Bounced = 0;
    globals.player.FallDeathTimer = 0.0f;
    globals.player.Diggable = NULL;
    globals.player.Visible = 1;
    globals.player.AutoMoveSpeed = 0;
    globals.player.Face_ScareTimer = 0.0f;
    globals.player.Face_EventTimer = 0.0f;
    globals.player.Face_PantTimer = 0.0f;
    globals.player.Jump_CanFloat = 1;
    globals.player.Jump_CanDouble = 1;
    globals.player.Jump_Springboard = NULL;
    globals.player.Jump_SpringboardStart = 0;
    globals.player.CanJump = 1;
    globals.player.CanBubbleSpin = 1;
    globals.player.CanBubbleBounce = 1;
    globals.player.CanBubbleBash = 1;
    globals.player.IsJumping = 0;
    globals.player.IsDJumping = 0;
    globals.player.WasDJumping = 0;
    globals.player.IsBubbleSpinning = 0;
    globals.player.IsBubbleBouncing = 0;
    globals.player.IsBubbleBashing = 0;
    globals.player.IsBubbleBowling = 0;
    globals.player.WallJumpState = k_WALLJUMP_NOT;
    globals.player.ForceSlipperyTimer = -1.0f;
    globals.player.carry.grabbed = NULL;
    globals.player.carry.grabTarget = 0;
    globals.player.carry.throwTarget = NULL;
    globals.player.carry.patLauncher = NULL;
    globals.player.lassoInfo.lasso.flags = 0;

    if (globals.player.lassoInfo.swingTarget)
    {
        globals.player.lassoInfo.swingTarget = NULL;
        zCameraDisableLassoCam();
    }

    ent->collis->depenq = PlayerDepenQuery;
    ent->bupdate = PlayerBoundUpdate;
    surfSlickRatio = 0.0f;
    surfSlickTimer = 0.0f;
    globals.player.floor_surf = NULL;
    globals.player.floor_norm = g_Y3;
    ent->collis->chk = 0x2e;

    xEntReset(ent);
    xVec3Copy(&ent->frame->rot.axis, &g_Y3);
    ent->frame->rot.angle = ent->asset->ang.x;

    xEntDriveInit(&globals.player.drv, ent);
    globals.player.drv.flags = 1;

    xCameraSetTargetMatrix(&globals.oldSkoolCamera,
                           xEntGetFrame(xglobals->___player_ent_dont_use_directly));
    memset(&sCameraLastMat, 0, sizeof(xMat4x3));
    xCameraSetTargetOMatrix(&globals.oldSkoolCamera, &sCameraLastMat);
    zCameraReset(&globals.oldSkoolCamera);
    xMat4x3Copy(&gPlayerAbsMat, &globals.oldSkoolCamera.mat);

    globals.player.Jump_CurrGravity = globals.player.g.Gravity;

    xAnimState* curr;
    for (curr = xglobals->___player_ent_dont_use_directly->model->Anim->Table->StateList; curr;
         curr = curr->Next)
    {
        if ((curr->UserFlags & 0x40) &&
            !(curr->Data->NumAnims[0] == 3 && curr->Data->NumAnims[1] == 1))
        {
            curr->UserFlags &= ~0x40;
        }
    }

    xModelInstance* minst;
    U32 index = 0;
    for (minst = xglobals->___player_ent_dont_use_directly->model; minst; minst = minst->Next)
    {
        if (index >= 3)
        {
            minst->Flags &= 0xfffc;
        }
        index++;
    }

    globals.player.RootUp.x = 0.0f;
    globals.player.RootUp.y = 1.0f;
    globals.player.RootUp.z = 0.0f;
    globals.player.RootUpTarget.x = 0.0f;
    globals.player.RootUpTarget.y = 1.0f;
    globals.player.RootUpTarget.z = 0.0f;
    globals.player.HangElapsed = 0.0f;
    globals.player.HangEnt = NULL;
    globals.player.HangEntLast = NULL;
    globals.player.KnockBackTimer = 0.0f;
    globals.player.KnockIntoAirTimer = 0.0f;
    sGooKnockedToSafety = 0;
    sGooKnockedTimer = 0.0f;

    memset(xglobals->___player_ent_dont_use_directly->collis->colls, 0,
           sizeof(xglobals->___player_ent_dont_use_directly->collis->colls));
    xEntBeginCollide(xglobals->___player_ent_dont_use_directly, globals.sceneCur, 0.0f);
    xEntSetNostepNormAngle(surfSlideStart);

    curVelmag = 0.0f;
    curVelangle = 0.0f;
    surfSlickness = 1;
    surfFriction = 1.0f;
    surfDamping = minVelmag;
    lastSlickness = 1;
    surfSticky = 0;
    sPlayerCollAdjust = 0.0f;
    tslide_maxspd_tmr = 0.0f;
    tslide_maxspd = globals.player.g.SlideVelMaxStart;
    globals.player.SlideTrackDecay = 0.0f;
    tslide_lastrealvel.x = 0.0f;
    tslide_lastrealvel.y = 0.0f;
    tslide_lastrealvel.z = 0.0f;
    tslide_inair_tmr = 0.0f;
    tslide_ground = 0;
    sRingDelay = 0.0f;
    player_hit = 0;
    globals.player.SlideTrackSliding = 0;
    globals.player.SlideTrackVel.x = 0.0f;
    globals.player.SlideTrackVel.y = 0.0f;
    globals.player.SlideTrackVel.z = 0.0f;
    sMountType = 0;
    sMountObject = NULL;
    sMountTimer = 0.0f;
    sThrowBlastRadius = 0.0f;
    sThrowBlastPosition.x = 0.0f;
    sThrowBlastPosition.y = 0.0f;
    sThrowBlastPosition.z = 0.0f;
    sGauntletStartHaz = 0;
    sGauntletBoundHit = 0;
    sHackStuckTimer = 0.0f;

    if (globals.player.model_spongebob)
    {
        xAnimPlaySetState(globals.player.model_spongebob->Anim->Single,
                          globals.player.model_spongebob->Anim->Table->StateList, 0.0f);
    }

    if (globals.player.model_patrick)
    {
        xAnimPlaySetState(globals.player.model_patrick->Anim->Single,
                          globals.player.model_patrick->Anim->Table->StateList, 0.0f);
    }

    if (globals.player.model_spongepat)
    {
        xAnimPlaySetState(globals.player.model_spongepat->Anim->Single,
                          globals.player.model_spongepat->Anim->Table->StateList, 0.0f);
    }

    xAnimPlaySetState(ent->model->Anim->Single, ent->model->Anim->Table->StateList, 0.0f);

    if (globals.sceneCur->sceneID == 'MNUS')
    {
        globals.firstStartPressed = TRUE;
        globals.autoSaveFeature = FALSE;
        globals.pad0 = mPad;
        globals.currentActivePad = 0;
        zEntPlayerControlOff(CONTROL_OWNER_EVENT);
        globals.player.ControlOnEvent = 0;
    }

    xColor_tag black = { 0, 0, 0, 255 };
    xColor_tag clear = { 0, 0, 0, 0 };
    xScrFxFade(&black, &clear, 1.0f, 0);

    if (globals.player.model_spongebob)
    {
        PlayerLedgeInit(&globals.player.sb.ledge, globals.player.model_spongebob);
    }

    if (globals.player.model_patrick)
    {
        PlayerLedgeInit(&globals.player.patrick.ledge, globals.player.model_patrick);

        patrickHitMax = 0;
        curr = globals.player.model_patrick->Anim->Table->StateList;
        while (curr && patrickHitMax < 8)
        {
            if (!strncmp(curr->Name, "Hit0", 4))
            {
                patrickHitTran[patrickHitMax].Dest = curr;
                patrickHitTran[patrickHitMax].Callback = NULL;
                patrickHitTran[patrickHitMax].SrcTime = 0.0f;
                patrickHitTran[patrickHitMax].DestTime = 0.0f;
                patrickHitTran[patrickHitMax].BlendRecip = 5.0f;
                patrickHitTran[patrickHitMax].BlendOffset = NULL;
                patrickHitMax++;
            }
            curr = curr->Next;
        }
    }

    globals.player.SlideTrackCount = 0;
    for (S32 i = 0; i < globals.sceneCur->num_base; i++)
    {
        if ((globals.sceneCur->base[i]->baseType == eBaseTypeStatic ||
             globals.sceneCur->base[i]->baseType == eBaseTypePlatform) &&
            zEntPlayer_ObjIDIsTrack(globals.sceneCur->base[i]->id))
        {
            xEnt* trackEnt = (xEnt*)globals.sceneCur->base[i];
            globals.player.SlideTrackEnt[globals.player.SlideTrackCount++] = trackEnt;
            trackEnt->baseFlags &= 0xffe7;
            trackEnt->chkby = 0;
            trackEnt->penby = 0;
        }
    }

    if (globals.player.model_spongebob)
    {
        cruise_bubble::reset();
        bungee_state::reset();
    }

    in_goo_tmr = 0.0f;
    in_goo = 0;
    sOnBouncBack = 0;

    sReticleModel[0] = (RpAtomic*)xSTFindAsset(xStrHash("target_reticle_hand"), NULL);
    sReticleModel[1] = (RpAtomic*)xSTFindAsset(xStrHash("target_reticle_hand"), NULL);
    sReticleModel[2] = (RpAtomic*)xSTFindAsset(xStrHash("joke_target_marker"), NULL);
    sReticleModel[3] = (RpAtomic*)xSTFindAsset(xStrHash("joke_target_marker"), NULL);

    for (S32 i = 0; i < 2; i++)
    {
        xMat4x3Identity(&sReticleMat[i]);
        gReticleTarget[i] = NULL;
        sTimeToRetarget[i] = 0.0f;
    }

    RwTexture* tex = (RwTexture*)xSTFindAsset(xStrHash(cBowlingLaneTexture), NULL);
    sBowlingLaneRast = tex ? tex->raster : NULL;

    tex = (RwTexture*)xSTFindAsset(xStrHash(cThrowArrowTexture), NULL);
    sThrowArrowRast = tex ? tex->raster : NULL;

    sShowThrowArrow = 0.0f;
    sThrowDecalFactor = 0.0f;
    stuck_timer = 0.0f;
    not_stuck_timer = 0.0f;

    sIdleCommentTimer.TimerReset();
    sRunningCommentTimer.TimerReset();
    sPatCarryCommentTimer.TimerReset();
    sRollingCommentTimer.TimerReset();

    if (gCurrentPlayer == eCurrentPlayerCar)
    {
        xAntennaRemove(xglobals->___player_ent_dont_use_directly, 0x22, 0x25);
        zEntPlayerCarGetCar()->Reset(ent, xglobals->___player_ent_dont_use_directly->asset->ang.x,
                                     &xglobals->___player_ent_dont_use_directly->frame->mat.pos);
        xAntennaAddAntenna(xglobals->___player_ent_dont_use_directly, 0.0f, 0.0f, 0x22, 0x25,
                           false);
    }

    globals.player.PlayerIsFrozen = 0;
    zEntPlayerKillGauntlet();
    shrapDeathTimer = -1.0f;
    zEntPlayerFX::MotionBlur::Blurring = false;
    zEntPlayerFX::MotionBlur::UpgradeBlur.deactivate();
    zEntPlayerFX::MotionBlur::NormalBlur.deactivate();
    gBungeeDeath = 0;
    sSpatulaGrabbed = 0;
    sPlayerKillAnimMovement = 0;
}

static xEnt* PlayerCollCheckOneEnt(xEnt* ent, xScene* sc, void* data)
{
    xent_entent = 1;

    xEnt* p = (xEnt*)data;
    xCollis* coll;
    U32 modl_coll = 0;

    if (p->collis->idx >= 15)
    {
        xent_entent = 0;
        return NULL;
    }

    if ((ent->chkby & p->collType) == 0)
    {
        xent_entent = 0;
        return ent;
    }

    if (ent->id == p->id)
    {
        xent_entent = 0;
        return ent;
    }

    coll = &p->collis->colls[p->collis->idx];

    if (ent->miscflags & 0x8)
    {
        if (p->frame->mat.pos.y + p->bound.sph.r > ent->bound.box.box.upper.y)
        {
            xSphere tmpsph;

            tmpsph.center.x = p->frame->mat.pos.x;
            tmpsph.center.y = p->frame->mat.pos.y + p->bound.sph.r;
            tmpsph.center.z = p->frame->mat.pos.z;
            tmpsph.r = p->bound.sph.r;

            CollidePyramidBoxTop(coll, &ent->bound.box.box, 1.0f, &tmpsph);
        }
        else
        {
            coll->flags = 0x1F00;
            xBoundHitsBound(&p->bound, &ent->bound, coll);
        }
    }
    else
    {
        if (ent->collLev == 5 && p->collType & (XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR))
        {
            modl_coll = 1;
        }

        if (modl_coll)
        {
            coll->flags = 0;
        }
        else
        {
            coll->flags = 0x1F00;
        }

        xBoundHitsBound(&p->bound, &ent->bound, coll);
    }

    if (coll->flags & 0x1)
    {
        if (modl_coll)
        {
            coll->flags = 0x1F00;

            xModelInstance* m = ent->collModel ? ent->collModel : ent->model;

            if (m->Flags & 0x800)
            {
                coll->flags |= 0x2000;
            }

            U8 ncolls = xSphereHitsModel3(&p->bound.sph, m, coll, 15 - p->collis->idx, PI / 4);

            for (U8 idx = 0; idx < ncolls; idx++)
            {
                coll[idx].optr = ent;
                coll[idx].mptr = ent->model;

                p->collis->idx++;
            }

            xent_entent = 0;
            return ent;
        }
        else
        {
            coll->oid = 0;
            coll->optr = ent;
            coll->mptr = ent->model;

            p->collis->idx++;

            if (coll->flags & 0x10)
            {
                xVec3Sub(&coll->tohit, xBoundCenter(&ent->bound), xEntGetCenter(p));
                // not in the DWARF: the length is its own statement (PS2 line table)
                F32 len = xVec3Length(&coll->tohit);
                xVec3SMul(&coll->depen, &coll->tohit, -0.25f * (1.0f / len));

                PlayerCollisBuildFromDepen(coll);
            }

            if (ent->pflags & 0x20 && ent->bound.type == XBOUND_TYPE_SPHERE &&
                p->bound.type == XBOUND_TYPE_SPHERE && coll->hdng.y < -0.866025f)
            {
                F32 rsum = p->bound.sph.r + ent->bound.sph.r;
                F32 dx = p->bound.sph.center.x - ent->bound.sph.center.x;
                F32 dy = p->bound.sph.center.y - ent->bound.sph.center.y;
                F32 dz = p->bound.sph.center.z - ent->bound.sph.center.z;

                // not in the TSSM DWARF, but the BfBB PS2 DWARF of this function lists hsqr
                F32 hsqr = SQR(rsum) - (SQR(dx) + SQR(dz));

                if (hsqr >= 0.0f)
                {
                    coll->depen.x = 0.0f;
                    coll->depen.y = xsqrt(hsqr) - dy;
                    coll->depen.z = 0.0f;
                    coll->dist = p->bound.sph.r - coll->depen.y;
                    coll->hdng.x = 0.0f;
                    coll->hdng.y = -1.0f;
                    coll->hdng.z = 0.0f;
                }
            }
        }
    }

    xent_entent = 0;
    return ent;
}

static U32 CollidePyramidBoxTop(xCollis* coll, xBox* box, F32 height, xSphere* sph)
{
    F32 dx;
    F32 dz;
    xVec3 point;
    xVec3 corner[2];
    xSweptSphere sws;
    xVec3 start;
    xVec3 end;
    F32 normX;
    F32 normZ;
    F32 normMag;

    point.x = 0.5f * (box->lower.x + box->upper.x);
    point.y = box->upper.y + height;
    point.z = 0.5f * (box->lower.z + box->upper.z);

    dx = sph->center.x - point.x;
    dz = sph->center.z - point.z;

    if (sph->center.x <= box->lower.x - sph->r || sph->center.x >= box->upper.x + sph->r ||
        sph->center.z <= box->lower.z - sph->r || sph->center.z >= box->upper.z + sph->r ||
        sph->center.y >= point.y + sph->r)
    {
        return 0;
    }

    if (xabs(dx) < 0.001f && xabs(dz) < 0.001f)
    {
        dz = 0.001f;
    }

    corner[0].y = box->upper.y;
    corner[1].y = box->upper.y;

    // From here on dx/dz hold the direction of the pyramid face that was hit.
    if (xabs(dx) > xabs(dz))
    {
        dz = 0.0f;

        if (dx > 0.0f)
        {
            corner[0].x = box->upper.x;
            corner[0].z = box->upper.z;
            corner[1].x = box->upper.x;
            corner[1].z = box->lower.z;
            dx = 1.0f;
        }
        else
        {
            corner[0].x = box->lower.x;
            corner[0].z = box->lower.z;
            corner[1].x = box->lower.x;
            corner[1].z = box->upper.z;
            dx = -1.0f;
        }
    }
    else
    {
        dx = 0.0f;

        if (dz > 0.0f)
        {
            corner[0].x = box->lower.x;
            corner[0].z = box->upper.z;
            corner[1].x = box->upper.x;
            corner[1].z = box->upper.z;
            dz = 1.0f;
        }
        else
        {
            corner[0].x = box->upper.x;
            corner[0].z = box->lower.z;
            corner[1].x = box->lower.x;
            corner[1].z = box->lower.z;
            dz = -1.0f;
        }
    }

    start.x = sph->center.x;
    start.y = sph->r + (sph->center.y + height);
    start.z = sph->center.z;
    end = sph->center;

    xSweptSpherePrepare(&sws, &start, &end, sph->r);

    if (xSweptSphereToTriangle(sws, point, corner[0], corner[1]))
    {
        xSweptSphereGetResults(&sws);

        normX = sws.worldNormal.x;
        normZ = sws.worldNormal.z;

        if (xabs(normX) < 1e-5f && xabs(normZ) < 1e-5f)
        {
            normX = dx;
            normZ = dz;
        }
        else
        {
            normMag = xsqrt(normX * normX + normZ * normZ);

            normX = normX * (1.0f / normMag);
            normZ = normZ * (1.0f / normMag);

            if (normX * dx + normZ * dz < 0.70710678f)
            {
                normX = dx;
                normZ = dz;
            }
        }

        normMag = xsqrt(SQR(0.5f * (box->upper.x - box->lower.x)) +
                        SQR(0.5f * (box->upper.z - box->lower.z)));

        start.x = normX * (normMag + sph->r) + sph->center.x;
        start.y = sph->center.y;
        start.z = normZ * (normMag + sph->r) + sph->center.z;

        xSweptSpherePrepare(&sws, &start, &end, sph->r);

        if (xSweptSphereToTriangle(sws, point, corner[0], corner[1]))
        {
            xSweptSphereGetResults(&sws);

            coll->flags |= k_HIT_IT | k_HIT_0x20000;
            coll->oid = 0;
            coll->optr = NULL;
            coll->mptr = NULL;
            coll->dist = sph->r - (sws.dist - sws.curdist);
            coll->norm = sws.worldNormal;

            coll->tohit.x = -normX * coll->dist;
            coll->tohit.y = 0.0f;
            coll->tohit.z = -normZ * coll->dist;

            coll->hdng.x = -normX;
            coll->hdng.y = 0.0f;
            coll->hdng.z = -normZ;

            coll->depen.x = normX * (sws.dist - sws.curdist);
            coll->depen.y = 0.0f;
            coll->depen.z = normZ * (sws.dist - sws.curdist);

            return 1;
        }
    }

    return 0;
}

static xEnt* PlayerCollCheckOneVillain(xEnt* ent, xScene* sc, void* data)
{
    xEnt* p = (xEnt*)data;

    if (p->collis->idx >= 15)
    {
        return NULL;
    }

    if (!(ent->chkby & p->collType))
    {
        return ent;
    }

    if (ent->id == p->id)
    {
        return ent;
    }

    if (!((zNMECommon*)ent)->IsHealthy())
    {
        return ent;
    }

    xCollis* coll = &p->collis->colls[p->collis->idx];

    if (cchkButtbounce &&
        (p->frame->oldmat.pos.y >= ent->frame->mat.pos.y + ent->bound.sph.r ||
         p->frame->oldmat.pos.y >= ent->frame->oldmat.pos.y + ent->bound.sph.r) &&
        p->frame->mat.pos.y <= ent->frame->mat.pos.y + ent->bound.sph.r)
    {
        F32 playerOldRad = p->bound.sph.r;

        p->bound.sph.r *= 1.5f;
        xBoundHitsBound(&p->bound, &ent->bound, coll);
        p->bound.sph.r = playerOldRad;

        if (coll->flags & k_HIT_IT)
        {
            coll->norm.x = 0.0f;
            coll->norm.y = 1.0f;
            coll->norm.z = 0.0f;

            coll->depen.x = 0.0f;
            coll->depen.y = ent->frame->mat.pos.y + ent->bound.sph.r - p->frame->mat.pos.y;
            coll->depen.z = 0.0f;

            coll->dist = p->bound.sph.r - coll->depen.y;

            coll->hdng.x = 0.0f;
            coll->hdng.y = -1.0f;
            coll->hdng.z = 0.0f;

            coll->tohit.x = coll->hdng.x * coll->dist;
            coll->tohit.y = coll->hdng.y * coll->dist;
            coll->tohit.z = coll->hdng.z * coll->dist;

            coll->oid = ent->id;
            coll->optr = ent;
            coll->mptr = ent->model;

            p->collis->idx++;

            return ent;
        }
    }

    if (ent->bound.type == XBOUND_TYPE_BOX && ent->baseType == eBaseTypeNPC &&
        !(((zNMECommon*)ent)->SelfType() >= NME_TYPE_TIKI &&
          ((zNMECommon*)ent)->SelfType() <= NME_TYPE_TIKI_STONE) &&
        !(((zNMECommon*)ent)->SelfType() >= NME_TYPE_STANDARD &&
          ((zNMECommon*)ent)->SelfType() <= NME_TYPE_MERVYN_V3) &&
        p->frame->mat.pos.y + p->bound.sph.r > ent->bound.box.box.upper.y)
    {
        xSphere tmpsph;

        tmpsph.center.x = p->frame->mat.pos.x;
        tmpsph.center.y = p->frame->mat.pos.y + p->bound.sph.r;
        tmpsph.center.z = p->frame->mat.pos.z;
        tmpsph.r = p->bound.sph.r;

        CollidePyramidBoxTop(coll, &ent->bound.box.box, 1.0f, &tmpsph);
    }
    else
    {
        xBoundHitsBound(&p->bound, &ent->bound, coll);
    }

    if (coll->flags & k_HIT_IT)
    {
        if (!(coll->flags & k_HIT_0x20000) && coll->hdng.y < 0.0f && coll->hdng.y > -0.93969f)
        {
            coll->flags |= k_HIT_0x20000;
        }

        if (xglobals->___player_ent_dont_use_directly->frame->vel.y < 1.0f &&
            !globals.player.IsBubbleBashing && ent->baseType == eBaseTypeNPC &&
            !(((zNMECommon*)ent)->SelfType() >= NME_TYPE_TIKI &&
              ((zNMECommon*)ent)->SelfType() <= NME_TYPE_TIKI_STONE))
        {
            coll->flags |= k_HIT_0x20000;
        }

        coll->oid = 0;
        coll->optr = ent;
        coll->mptr = ent->model;

        p->collis->idx++;

        if (coll->mptr->Flags & 0x800)
        {
            coll->flags |= k_HIT_CALC_TRI;
        }

        if (coll->flags & k_HIT_0x10)
        {
            xVec3Sub(&coll->tohit, xBoundCenter(&ent->bound), xEntGetCenter(p));

            // not in the DWARF: the tohit length is computed once and used by both branches
            F32 len = xVec3Length(&coll->tohit);
            if (xfeq0(len))
            {
                coll->tohit.x = 0.0f;
                coll->tohit.y = 1.0f;
                coll->tohit.z = 0.0f;
                coll->depen.x = 0.0f;
                coll->depen.y = 1.0f;
                coll->depen.z = 0.0f;
            }
            else
            {
                xVec3SMul(&coll->depen, &coll->tohit, -0.25f * (1.0f / len));
                coll->depen.y = 0.0f;
            }

            PlayerCollisBuildFromDepen(coll);
        }
    }

    return ent;
}

// No GC symbol (always inlined).
static inline void PlayerCollisTranslate(xCollis* c, F32 x, F32 y, F32 z)
{
    if (c->depen.x != 0.0f || c->depen.y != 0.0f || c->depen.z != 0.0f)
    {
        F32 dx = c->tohit.x - x;
        F32 dy = c->tohit.y - y;
        F32 dz = c->tohit.z - z;

        // dist2 holds the squared length first, then the length
        F32 dist2 = dx * dx + dy * dy + dz * dz;

        c->tohit.x = dx;
        c->tohit.y = dy;
        c->tohit.z = dz;

        dist2 = xsqrt(dist2);
        c->dist = dist2;

        F32 s = 1.0f / dist2;

        c->hdng.x = dx * s;
        c->hdng.y = dy * s;
        c->hdng.z = dz * s;

        s = MIN(0.0f, s * (dist2 - 0.5f));

        c->depen.x = dx * s;
        c->depen.y = dy * s;
        c->depen.z = dz * s;
    }
}

static void PlayerCollsSidesTranslate(xCollis* colls, F32 x, F32 y, F32 z)
{
    xCollis* left = &colls[k_XCOLLS_IDX_LEFT];
    if (left->dist < 1e38f)
    {
        PlayerCollisTranslate(left, x, y, z);
    }

    xCollis* right = &colls[k_XCOLLS_IDX_RIGHT];
    if (right->dist < 1e38f)
    {
        PlayerCollisTranslate(right, x, y, z);
    }
}

// Combines two depenetration vectors (a, b) in the XZ plane: each one is pushed along the other's
// wall normal, then the result is blended with their average by fudge.
static void CalcCombinedDepen(F32& dx, F32& dz, F32 ax, F32 az, F32 bx, F32 bz, F32 fudge)
{
    F32 normX;
    F32 normZ;
    F32 depenZ;
    F32 nddot;

    F32 la = xsqrt(ax * ax + az * az);
    F32 lb = xsqrt(bx * bx + bz * bz);

    if (la < 1e-5f || lb < 1e-5f)
    {
        dx = 0.5f * (ax + bx);
        dz = 0.5f * (az + bz);
    }
    else
    {
        normX = -az / la;
        normZ = ax / la;
        nddot = normX * (bx / lb) + normZ * (bz / lb);
        depenZ = normZ;

        if (nddot < 0.0f)
        {
            nddot = -nddot;
            normX = -normX;
            depenZ = -normZ;
        }

        if (nddot < 0.0001f)
        {
            dx = 0.5f * (ax + bx);
            dz = 0.5f * (az + bz);
        }
        else
        {
            nddot = MAX(nddot, 0.25f);
            dx = (lb * normX) / nddot;
            dz = (lb * depenZ) / nddot;

            normX = -bz / lb;
            normZ = bx / lb;
            nddot = normX * (ax / la) + normZ * (az / la);

            if (nddot < 0.0f)
            {
                nddot = -nddot;
                normX = -normX;
                normZ = -normZ;
            }

            if (nddot < 0.0001f)
            {
                dx = 0.5f * (ax + bx);
                dz = 0.5f * (az + bz);
            }

            nddot = MAX(nddot, 0.25f);
            dx = dx + (la * normX) / nddot;
            dz = dz + (la * normZ) / nddot;
            // not in the DWARF: avgX/avgZ; retail's operand order in the blend needs the average
            // as a named value.
            F32 avgX = 0.5f * (ax + bx);
            F32 avgZ = 0.5f * (az + bz);
            dx = dx * fudge + avgX * (1.0f - fudge);
            dz = dz * fudge + avgZ * (1.0f - fudge);
        }
    }
}

// No GC symbol (always inlined); name and locals from Scooby's PS2 DWARF.
static inline void PlayerCollsAllTranslate(xCollis* colls, F32 x, F32 y, F32 z)
{
    xCollis* c;
    xCollis* cend = colls + k_XCOLLS_IDX_COUNT;

    for (c = colls; c < cend; c++)
    {
        if (c->dist < 1e38f)
        {
            PlayerCollisTranslate(c, x, y, z);
        }
    }
}

// No GC symbol (always inlined); name and locals from Scooby's PS2 DWARF.
static inline void PlayerCollsWallsTranslate(xCollis* colls, F32 x, F32 y, F32 z)
{
    xCollis* cend = colls + k_XCOLLS_IDX_COUNT;
    xCollis* c = colls + k_XCOLLS_IDX_FRONT;

    for (; c < cend; c++)
    {
        if (c->dist < 1e38f)
        {
            PlayerCollisTranslate(c, x, y, z);
        }
    }
}

// No GC symbol (always inlined); BfBB has it out of line.
static inline void PlayerCollsWallsTranslate(xCollis* colls, const xVec3* v)
{
    PlayerCollsWallsTranslate(colls, v->x, v->y, v->z);
}

// No GC symbol (always inlined); BfBB has it out of line.
static inline F32 ComputeFudge(F32 a, F32 b)
{
    // Ramps from 0 to 1 as the lower tohit.y goes from -0.175 to -0.1.
    F32 fudge = (MIN(a, b) - -0.175f) / (-0.1f - -0.175f);
    return CLAMP(fudge, 0.0f, 1.0f);
}

static void PlayerCollsSelectDepen(xEnt* p, xScene*, F32)
{
    xCollis* colls = p->collis->colls;
    xMat4x3* mat = &p->frame->mat;
    xCollis* c = colls + k_XCOLLS_IDX_COUNT;
    xCollis* cend = colls + p->collis->idx;
    xVec3 motion_delta = mat->pos - p->frame->oldmat.pos;
    // not in the DWARF order (the PS2 DWARF lists cent in the third loop): declared here for
    // retail's registers
    xEnt* cent;

    for (; c < cend; c++)
    {
        U8 idx = xCollideGetCollsIdx(c, &c->tohit, mat);

        if (idx == k_XCOLLS_IDX_FLOOR)
        {
            xSurface* surface = zSurfaceGetSurface(c);

            if (((zSurfaceProps*)surface->moprops)->asset->phys_flags & 0x20)
            {
                xVec3 vec;
                xVec3Init(&vec, c->tohit.x, 0.0001f, c->tohit.z);
                xVec3Normalize(&vec, &vec);
                idx = xCollideGetCollsIdx(c, &vec, mat);
            }
        }

        xCollis* curr = colls + idx;

        if (c->dist < curr->dist)
        {
            *curr = *c;
        }
    }

    if (p->pflags & 0x80)
    {
        xCollis* coll = p->collis->colls;

        if ((coll->flags & k_HIT_IT) && coll->dist < 0.5f)
        {
            F32 h_dot_n = xVec3Dot(&coll->hdng, &coll->norm);

            if (h_dot_n > 0.0f)
            {
                xVec3Inv(&coll->norm, &coll->norm);
                h_dot_n = -h_dot_n;
            }

            if (xabs(xacos(xVec3Dot(&coll->norm, &update_motion))) < 0.7853982f)
            {
                F32 depen_len = h_dot_n * coll->dist + 0.5f;

                if (depen_len < 0.0f || depen_len > 0.5f)
                {
                    depen_len = CLAMP(depen_len, 0.0f, 0.5f);
                }

                xVec3SMul(&coll->depen, &coll->norm, depen_len);
            }
        }
    }

    for (c = colls, cend = colls + k_XCOLLS_IDX_COUNT; c < cend; c++)
    {
        cent = (xEnt*)c->optr;

        if (!cent)
        {
            continue;
        }

        if (!(cent->penby & p->collType) || !(p->collis->pen & cent->collType))
        {
            xVec3Init(&c->depen, 0.0f, 0.0f, 0.0f);
            continue;
        }

        if (globals.player.DamageTimer > 0.0f)
        {
            xSurface* surf = zSurfaceGetSurface(c);

            if (surf && !surf->state && zSurfaceGetDamageType(surf) &&
                zSurfaceGetDamagePassthrough(surf))
            {
                xVec3Init(&c->depen, 0.0f, 0.0f, 0.0f);
                continue;
            }
        }

        if (cent->baseType == eBaseTypeVillain)
        {
            if (c == &colls[k_XCOLLS_IDX_FLOOR])
            {
                c->depen.y = 0.0f;
            }
            else if (c != &colls[k_XCOLLS_IDX_FRONT])
            {
                xVec3Init(&c->depen, 0.0f, 0.0f, 0.0f);
            }
        }
    }

    xCollis* cceil = &colls[k_XCOLLS_IDX_CEIL];
    xSurface* sfloor = zSurfaceGetSurface(&colls[k_XCOLLS_IDX_FLOOR]);

    if ((colls[k_XCOLLS_IDX_FLOOR].flags & k_HIT_IT) && p->frame->vel.y > 0.0f &&
        globals.player.JumpState != 0 && globals.player.JumpState != 1)
    {
        if (xVec3Dot(&motion_delta, &colls[k_XCOLLS_IDX_FLOOR].tohit) < 0.0f)
        {
            colls[k_XCOLLS_IDX_FLOOR].flags &= ~k_HIT_IT;
        }
    }

    if ((colls[k_XCOLLS_IDX_FLOOR].flags & k_HIT_IT) && colls[k_XCOLLS_IDX_FLOOR].dist < 0.5f)
    {
        if (sfloor && (globals.player.Slide == 1 || globals.player.Slide == 3))
        {
            xVec3AddTo(&mat->pos, &colls[k_XCOLLS_IDX_FLOOR].depen);
            PlayerCollsWallsTranslate(colls, &colls[k_XCOLLS_IDX_FLOOR].depen);
        }
        else
        {
            mat->pos.y += colls[k_XCOLLS_IDX_FLOOR].depen.y;
            PlayerCollsWallsTranslate(colls, 0.0f, colls[k_XCOLLS_IDX_FLOOR].depen.y, 0.0f);
        }
    }
    else if ((cceil->flags & k_HIT_IT) && cceil->dist < 0.5f)
    {
        if (cceil->hdng.y < icos(PI / 6.0f))
        {
            xVec3AddTo(&mat->pos, &cceil->depen);
            PlayerCollsWallsTranslate(colls, &cceil->depen);
        }
        else
        {
            mat->pos.y += cceil->depen.y;
            PlayerCollsWallsTranslate(colls, 0.0f, cceil->depen.y, 0.0f);
            p->frame->vel.y = 0.0f;
        }
    }

    S32 num_walls = 0;
    xCollis* first_wall = NULL;
    xCollis* inside_wall = NULL;

    for (c = &colls[k_XCOLLS_IDX_FRONT], cend = &colls[k_XCOLLS_IDX_COUNT]; c < cend; c++)
    {
        if (c->flags & k_HIT_0x10)
        {
            inside_wall = c;
        }

        if (c->flags & k_HIT_IT)
        {
            if (c->optr)
            {
                num_walls++;

                if (!first_wall)
                {
                    first_wall = c;
                }
            }
            else
            {
                num_walls++;

                if (!first_wall)
                {
                    first_wall = c;
                }
            }
        }
    }

    if (inside_wall)
    {
        inside_wall->flags &= ~k_HIT_IT;
    }

    if (!num_walls)
    {
        return;
    }

    if (num_walls == 1)
    {
        mat->pos.x += first_wall->depen.x;
        mat->pos.z += first_wall->depen.z;
        return;
    }

    xCollis* cfront = &colls[k_XCOLLS_IDX_FRONT];
    xCollis* crear = &colls[k_XCOLLS_IDX_REAR];

    if ((cfront->flags & k_HIT_IT) && cfront->dist < 0.5f)
    {
        if ((crear->flags & k_HIT_IT) && crear->dist < 0.5f)
        {
            F32 dx;
            F32 dz;

            CalcCombinedDepen(dx, dz, crear->depen.x, crear->depen.z, cfront->depen.x,
                              cfront->depen.z, ComputeFudge(crear->tohit.y, cfront->tohit.y));

            mat->pos.x += dx;
            mat->pos.z += dz;

            PlayerCollsSidesTranslate(colls, dx, 0.0f, dz);
        }
        else
        {
            mat->pos.x += cfront->depen.x;
            mat->pos.z += cfront->depen.z;

            PlayerCollsSidesTranslate(colls, cfront->depen.x, 0.0f, cfront->depen.z);
        }
    }
    else if ((crear->flags & k_HIT_IT) && crear->dist < 0.5f)
    {
        mat->pos.x += crear->depen.x;
        mat->pos.z += crear->depen.z;

        PlayerCollsSidesTranslate(colls, crear->depen.x, 0.0f, crear->depen.z);
    }

    xCollis* cleft = &colls[k_XCOLLS_IDX_LEFT];
    xCollis* cright = &colls[k_XCOLLS_IDX_RIGHT];

    if ((cleft->flags & k_HIT_IT) && cleft->dist < 0.5f)
    {
        if ((cright->flags & k_HIT_IT) && cright->dist < 0.5f)
        {
            F32 dx;
            F32 dz;

            CalcCombinedDepen(dx, dz, cleft->depen.x, cleft->depen.z, cright->depen.x,
                              cright->depen.z, ComputeFudge(cleft->tohit.y, cright->tohit.y));

            mat->pos.x += dx;
            mat->pos.z += dz;
        }
        else
        {
            mat->pos.x += cleft->depen.x;
            mat->pos.z += cleft->depen.z;
        }
    }
    else if ((cright->flags & k_HIT_IT) && cright->dist < 0.5f)
    {
        mat->pos.x += cright->depen.x;
        mat->pos.z += cright->depen.z;
    }
}

void zEntPlayerCollide(xEnt* ent, xScene* sc, F32 dt)
{
    xCollis* colls;
    xCollis* cend;
    xCollis* c;

    cchkButtbounce = 0;
    cchkSquish = globals.player.s->Health &&
                 (ent->model->Anim->Single->State->UserFlags & 0x1e) == 0xa &&
                 ent->frame->mat.pos.y < ent->frame->oldmat.pos.y;

    xEntBeginCollide(ent, sc, dt);

    // Probably an inlined helper or macro in the original (one PS2 source line); written out here.
    if (ent->collis->chk & XENT_COLLTYPE_ENV)
    {
        ent->collis->env_sidx = ent->collis->idx;

        ent->bound.sph.r = 0.65f;
        xSphereHitsEnv4(&ent->bound.sph, sc->env, &ent->frame->mat, ent->collis->colls);
        ent->bound.sph.r = 0.5f;

        PlayerCollsAllTranslate(ent->collis->colls, 0.0f, 0.0f, 0.0f);

        ent->collis->env_eidx = ent->collis->idx;
    }

    if (ent->collis->chk & XENT_COLLTYPE_NPC)
    {
        xEntCollCheckNPCsByGrid(ent, sc, PlayerCollCheckOneVillain);
    }

    if (ent->collis->chk & (XENT_COLLTYPE_STAT | XENT_COLLTYPE_DYN))
    {
        xEntCollCheckByGrid(ent, sc, PlayerCollCheckOneEnt);
    }

    if (ent->collis->chk &
        (XENT_COLLTYPE_STAT | XENT_COLLTYPE_DYN | XENT_COLLTYPE_NPC | XENT_COLLTYPE_ENV))
    {
        PlayerCollsSelectDepen(ent, sc, dt);

        colls = ent->collis->colls;
        c = &colls[k_XCOLLS_IDX_FRONT];
        cend = &colls[ent->collis->idx];
        for (; c < cend; c++)
        {
            if (c->dist > 0.5f)
            {
                c->flags &= ~k_HIT_IT;
            }
        }
    }

    xEntEndCollide(ent, sc, dt);
}

void zEntPlayerCollTrigger(xEnt* ent, xScene* sc)
{
    ((zPlayer*)ent)->CollideTrigger(sc);
}

static xVec3* GetPosVec(xBase* base)
{
    xVec3* vec = (xVec3*)&g_O3;

    switch (base->baseType)
    {
    case eBaseTypeMovePoint:
        vec = ((xMovePoint*)base)->pos;
        break;
    case eBaseTypeVillain:
    case eBaseTypePlayer:
    case eBaseTypePickup:
    case eBaseTypePlatform:
    case eBaseTypeDoor:
    case eBaseTypeStatic:
    case eBaseTypeDynamic:
    case eBaseTypePendulum:
    case eBaseTypeHangable:
    case eBaseTypeButton:
    case eBaseTypeDestructObj:
        vec = (xVec3*)&((xEnt*)base)->model->Mat->pos;
        break;
    }

    return vec;
}

void zEntPlayerEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                       xBase* toParamWidget, U32 toParamWidgetID)
{
    S32 iParam;
    S32 num;
    S32 s;
    U32 p;
    zEnt* z;
    const char* astatename;
    zShrapnelAsset* shrap;
    xVec3 currVel;
    eSNDCurrentPlayer oldPlayer;

    if (cruise_bubble::event_handler(from, toEvent, toParam, toParamWidget))
    {
        return;
    }

    switch (toEvent)
    {
    case eEventControlOff:
        zEntPlayerControlOff(CONTROL_OWNER_EVENT);
        globals.player.ControlOnEvent = 0;
        globals.player.ControlOffTimer = 1.0f;
        globals.player.AutoMoveSpeed = 0;
        xScrFxLetterbox(1);
        break;
    case eEventControlOn:
        globals.player.ControlOnEvent = 1;
        globals.player.AutoMoveSpeed = 0;
        xScrFxLetterbox(0);
        break;
    case eEventGiveHealth:
        iParam = (S32)toParam[0];

        if (iParam == 0)
        {
            iParam = 1;
        }

        if (iParam < 0)
        {
            if (zEntPlayer_Damage(from, -iParam) && globals.player.s->Health != 0)
            {
                if (0.0f == toParam[1])
                {
                    if (iParam != -666 && globals.player.g.DamageGiveHealthKnock)
                    {
                        zEntPlayer_DamageKnockIntoAir(globals.player.g.DamageGiveHealthKnock);
                    }
                }
                else if (toParam[1] > 0.0f)
                {
                    zEntPlayer_DamageKnockIntoAir(toParam[1]);
                }
            }
        }
        else
        {
            zEntPlayer_GiveHealth(iParam);
        }
        break;
    case eEventGiveShinyObjects:
        num = (S32)(toParam[0] + (toParam[0] > 0.0f ? 0.5f : -0.5f));
        zEntPlayer_GiveShinyObject(num);
        break;
    case eEventGivePowerUp:
        iParam = (S32)toParam[0];

        if (!globals.player.g.PowerUp[iParam])
        {
            globals.player.g.PowerUp[iParam] = 1;
        }

        zMenuSB_UpdatePowerUpMenu();
        break;
    case eEventUpgradePowerUp:
        iParam = (S32)toParam[0];

        if (iParam < 8)
        {
            if (!(globals.player.g.PowerUp[iParam] && globals.player.g.PowerUp[iParam] < 2))
            {
                break;
            }
        }
        else
        {
            if (globals.player.g.PowerUp[iParam] >= 3)
            {
                break;
            }

            if (iParam == 8)
            {
                globals.player.sb.MaxHealth++;
            }
            else if (iParam == 9)
            {
                globals.player.patrick.MaxHealth++;
            }
        }

        globals.player.g.PowerUp[iParam]++;
        globals.player.CurrentUnusedSkillPoints--;
        globals.player.CurrentSpentSkillPoints++;
        zMenuSB_UpdatePowerUpMenu();
        UpgradeSparkleEffect::PlayUpgradeFMV(iParam);
        break;
    case eEventTakeSocks:
        s = (S32)(toParam[0] + (toParam[0] > 0.0f ? 0.5f : -0.5f));

        if (s < 0)
        {
            s = 0;
        }

        if (s > (S32)globals.player.InvTreasureChestTotal)
        {
            globals.player.InvTreasureChestTotal = 0;
        }
        else
        {
            globals.player.InvTreasureChestTotal -= s;
        }
        break;
    case eEventGiveCurrLevelSocks:
        zEntPlayer_GivePatsSocksCurrentLevel((S32)toParam[0]);
        break;
    case eEventGiveCurrLevelPickup:
        zEntPlayer_GiveLevelPickupCurrentLevel((S32)toParam[0]);
        break;
    case eEventSetCurrLevelSocks:
        p = globals.player.InvTreasureChest[zSceneGetLevelIndex()];
        globals.player.InvTreasureChestTotal += (U32)toParam[0] - p;
        globals.player.InvTreasureChest[zSceneGetLevelIndex()] = (U32)toParam[0];
        break;
    case eEventSetCurrLevelPickup:
        globals.player.InvLevelPickups[zSceneGetLevelIndex()] = (U32)toParam[0];
        break;
    case eEventKill:
        if (xglobals->___player_ent_dont_use_directly->update ==
            (xEntUpdateCallback)zEntPlayer_BoulderVehicleUpdate)
        {
            BoulderRollDoneCB();
        }

        if (globals.player.s->Health != 0)
        {
            globals.player.DamageTimer = 0.0f;
            zEntPlayer_Damage(from, globals.player.s->Health);
        }
        break;
    case eEventHit:
        if (xglobals->___player_ent_dont_use_directly->update ==
            (xEntUpdateCallback)zEntPlayer_BoulderVehicleUpdate)
        {
            BoulderRollDoneCB();
        }

        if (globals.player.s->Health != 0)
        {
            zEntPlayer_Damage(from, 1);
        }
        break;
    case eEventOutOfBounds:
        if (to == xglobals->___player_ent_dont_use_directly)
        {
            globals.dontShowPadMessageDuringLoadingOrCutScene = 1;
            oob_state::force_start();
        }
        else
        {
            z = (zEnt*)to;

            if (z->asset)
            {
                xVec3Copy(&z->frame->mat.pos, &z->asset->pos);
                xVec3Copy((xVec3*)&z->model->Mat->pos, &z->asset->pos);
                xVec3Copy(&z->frame->vel, &g_O3);
                xVec3Copy(&z->frame->rot.axis, &g_Y3);
                z->frame->rot.angle = 0.0f;
            }
        }
        break;
    case eEventFallToDeath:
        if (xglobals->___player_ent_dont_use_directly->update ==
            (xEntUpdateCallback)zEntPlayer_BoulderVehicleUpdate)
        {
            BoulderRollDoneCB();
        }

        astatename = ((zEnt*)to)->model->Anim->Single->State->Name;

        if (xStricmp(astatename, "Fall_losechance") && xStricmp(astatename, "Fall_losedrop"))
        {
            globals.player.FallDeathTimer = 1.2f;
            globals.player.s->Health = 0;
            globals.player.DamageTimer = 15.0f;
            xglobals->___player_ent_dont_use_directly->frame->vel.x = 0.0f;
            xglobals->___player_ent_dont_use_directly->frame->vel.z = 0.0f;
        }
        break;
    case eEventVisible:
        globals.player.Visible = 1;
        break;
    case eEventInvisible:
        globals.player.Visible = 0;
        xShadowManager_Remove(xglobals->___player_ent_dont_use_directly);
        break;
    case eEventMoveToTarget:
        if (globals.player.ControlOff && toParamWidget)
        {
            globals.player.AutoMoveDist = toParam[0];
            globals.player.AutoMoveSpeed = (U32)toParam[1] + 1;
            globals.player.AutoMoveTarget = *GetPosVec(toParamWidget);
            globals.player.AutoMoveObject = toParamWidget;

            if (globals.player.AutoMoveSpeed > 3 || globals.player.AutoMoveSpeed < 1)
            {
                globals.player.AutoMoveSpeed = 1;
            }
        }
        break;
    case eEventFaceTarget:
        if (globals.player.ControlOff && toParamWidget)
        {
            globals.player.AutoMoveSpeed = 4;
            globals.player.AutoMoveTarget = *GetPosVec(toParamWidget);
            globals.player.AutoMoveObject = toParamWidget;
        }
        break;
    case eEventNPCPatrolOff:
        globals.player.AutoMoveSpeed = 0;
        break;
    case eEventLaunchShrapnel:
        shrap = (zShrapnelAsset*)toParamWidget;

        if (shrap && shrap->initCB)
        {
            xVec3SMul(&currVel, &globals.player.PredictCurrDir, globals.player.PredictCurrVel);
            shrap->initCB(shrap, xglobals->___player_ent_dont_use_directly->model, &currVel, NULL);
        }
        break;
    case eEventPlrSwitchCharacter:
        oldPlayer = gCurrentPlayer;

        if (1.0f == toParam[0])
        {
            gCurrentPlayer = eCurrentPlayerPatrick;
        }
        else
        {
            gCurrentPlayer = eCurrentPlayerSpongeBob;
        }

        if (gCurrentPlayer != oldPlayer)
        {
            zEntPlayer_SNDPlayStream(ePlayerStreamSnd_StartingOut);
        }
        break;
    case eEventSituationDestroyedTiki:
        if ((xrand_GenRandInt32() & 3) == 3)
        {
            sPlayerOneLiners[gCurrentPlayer].PlaySoundDelayed(ePlayerStreamSnd_SmashTiki, 0.5f);
        }
        break;
    case eEventSituationDestroyedRobot:
        if ((xrand_GenRandInt32() & 3) == 3)
        {
            sPlayerOneLiners[gCurrentPlayer].PlaySoundDelayed(ePlayerStreamSnd_DestroyRobot, 0.5f);
        }
        break;
    case eEventSituationSeeWoodTiki:
    case eEventSituationSeeLoveyTiki:
    case eEventSituationSeeShhhTiki:
    case eEventSituationSeeThunderTiki:
    case eEventSituationSeeStoneTiki:
        break;
    case eEventSituationSeePatrickBoss:
        sPlayerOneLiners[gCurrentPlayer].PlaySoundDelayed(ePlayerStreamSnd_SeePatrickBoss1, 0.5f);
        break;
    case eEventSituationSeeSpongeBobBoss:
        sPlayerOneLiners[gCurrentPlayer].PlaySoundDelayed(ePlayerStreamSnd_SeeSpongeBobBoss, 0.5f);
        break;
    case eEventSituationSeeRobotPlankton:
        sPlayerOneLiners[gCurrentPlayer].PlaySoundDelayed(ePlayerStreamSnd_SeeRobotPlankton, 0.5f);
        break;
    case eEventCelebrationAnimPlay:
        if (gCurrentPlayer == eCurrentPlayerSpongeBob || gCurrentPlayer == eCurrentPlayerPatrick)
        {
            if (gCurrentPlayer == eCurrentPlayerSpongeBob && player_is_sponge_ball())
            {
                boulderRollShouldStart = 0;
                boulderRollShouldEnd = 1;

                if (gCurrentPlayer == eCurrentPlayerSpongeBob)
                {
                    BoulderRollDoneCB();
                    lastgCurrentPlayer = eCurrentPlayerCount;
                    gCurrentPlayer = eCurrentPlayerSpongeBob;
                }
            }

            zEntPlayer_GiveSpatula(1);
        }
        break;
    case eEventPlayerDeath:
    case eEventGiveChance:
        break;
    case eEventFreezePlayer:
        globals.player.PlayerIsFrozen = 1;
        break;
    case eEventUnfreezePlayer:
        globals.player.PlayerIsFrozen = 0;
        break;
    case eEventSetLightKit:
        xModelSetLightKit(((zEnt*)to)->model, (xLightKit*)toParamWidget);
        globals.player.pPlayerLightKit = xglobals->___player_ent_dont_use_directly->model->LightKit;
        break;
    }
}

static void PlayerSwingUpdate(xEnt* ent, F32 mag, F32 angle, F32 dt)
{
    F32 hangDist;
    xVec3 pos;
    xVec3 unitHang;
    xVec3 accel;
    xVec3 unitDefl;
    xVec3 unitVel;
    S32 i;
    xVec3 unitAccel;
    F32 lerpDiff;
    F32 newLerp;
    F32 curFactor;

    pos = *(xVec3*)&ent->model->Mat->pos;
    pos.y += ent->bound.sph.r;

    accel.x = mag * (2.0f * isin(angle));
    accel.y = 0.0f;
    accel.z = mag * (2.0f * icos(angle));

    xVec3Normalize(&unitDefl, &accel);

    for (i = k_XCOLLS_IDX_FRONT; i < k_XCOLLS_IDX_COUNT; i++)
    {
        if (ent->collis->colls[i].flags & k_HIT_IT)
        {
            xVec3AddScaled(&globals.player.HangVel, &ent->collis->colls[i].norm,
                           -xVec3Dot(&globals.player.HangVel, &ent->collis->colls[i].norm));
        }
    }

    xVec3Normalize(&unitVel, &globals.player.HangVel);

    xVec3Sub(&unitHang, &pos, (xVec3*)&sLassoInfo->swingTarget->model->Mat->pos);
    hangDist = xVec3Normalize(&unitHang, &unitHang);

    // The PS2 DWARF lists no other F32 locals, so the dot products below live in lerpDiff and the
    // previous blend lerp in curFactor.
    lerpDiff = xVec3Dot(&unitHang, &accel);
    accel.x -= lerpDiff * unitHang.x;
    accel.y -= lerpDiff * unitHang.y;
    accel.z -= lerpDiff * unitHang.z;

    if (xVec3Normalize(&unitAccel, &accel) > 1e-5f)
    {
        lerpDiff = xVec3Dot(&unitAccel, &globals.player.HangVel);

        if (lerpDiff < 0.0f)
        {
            xVec3Inv(&accel, &accel);
        }

        // not in the DWARF: both are held in registers in retail.
        xVec3 velPerp;
        xVec3 velAlong;

        xVec3SMul(&velAlong, &unitAccel, lerpDiff);
        xVec3Sub(&velPerp, &globals.player.HangVel, &velAlong);
        xVec3AddScaled(&accel, &velPerp, -1.5f);
    }

    accel.y -= 15.0f;

    xVec3AddScaled(&globals.player.HangVel, &accel, dt);

    lerpDiff = xVec3Dot(&unitHang, &globals.player.HangVel);

    if (hangDist > 3.96f && lerpDiff > 0.0f)
    {
        globals.player.HangVel.x -= lerpDiff * unitHang.x;
        globals.player.HangVel.y -= lerpDiff * unitHang.y;
        globals.player.HangVel.z -= lerpDiff * unitHang.z;
    }

    xVec3AddScaled(&pos, &globals.player.HangVel, dt);

    xVec3Sub(&unitHang, &pos, (xVec3*)&sLassoInfo->swingTarget->model->Mat->pos);
    hangDist = xVec3Normalize(&unitHang, &unitHang);

    if (hangDist > 3.96f)
    {
        hangDist = 0.95f * hangDist + 0.2f;
    }

    globals.player.RootUpTarget = unitHang;
    xVec3Inv(&globals.player.RootUpTarget, &globals.player.RootUpTarget);

    globals.player.HangElapsed += dt;

    xVec3Copy(&ent->frame->mat.pos, (xVec3*)&sLassoInfo->swingTarget->model->Mat->pos);
    xVec3AddScaled(&ent->frame->mat.pos, &unitHang, hangDist);

    ent->frame->mat.pos.y -= ent->bound.sph.r;
    ent->frame->mode |= 0x1;

    if (globals.player.HangStartLerp < 1.0f)
    {
        globals.player.HangStartLerp = MIN(1.0f, 5.0f * dt + globals.player.HangStartLerp);
    }

    lerpDiff = 1.0f - mag * xVec3Dot(&unitDefl, &unitVel);
    curFactor = ent->model->Anim->Single->BilinearLerp[0];
    lerpDiff -= curFactor;
    lerpDiff = dt * lerpDiff;
    lerpDiff = 6.0f * lerpDiff;

    ent->model->Anim->Single->BilinearLerp[0] += lerpDiff;
    ent->model->Anim->Single->Blend->BilinearLerp[0] += lerpDiff;

    newLerp = ent->model->Anim->Single->BilinearLerp[0];

    if ((curFactor <= 1.8f && newLerp > 1.8f) || (curFactor >= 0.2f && newLerp < 0.2f))
    {
        zEntPlayer_SNDPlay(ePlayerSnd_LassoYank, true);
    }

    sSwingTimeElapsed += dt;

    curFactor = zCameraGetLassoCamFactor();

    lerpDiff = xVec3Dot(&globals.oldSkoolCamera.mat.at, &unitHang);
    zCameraSetLassoCamFactor(0.8f * curFactor + 0.2f * (0.5f - 0.5f * lerpDiff));
}

static void PlayerTeeterCheck(xEnt* ent, xScene* sc, F32 dt)
{
    S32 i;

    if (globals.player.Teeter)
    {
        globals.player.Teeter--;
    }

    if (globals.player.JumpState)
    {
        return;
    }

    for (i = 0; i < 4; i++)
    {
        if (floor_dist[i] > 0.424264f)
        {
            if (floor_tmr[i] >= 0.2f)
            {
                floor_tmr[i] = 0.2f;
                globals.player.Teeter = 1;
                return;
            }

            floor_tmr[i] += dt;
        }
        else
        {
            floor_tmr[i] = 0.0f;
        }
    }
}

static void PlayerRotMatchUpdateEnt(xEnt* ent, xScene* sc, F32 dt, void* fdata)
{
    xFFXRotMatchState* rms = (xFFXRotMatchState*)fdata;
    xCollis* coll;
    S32 hit_it;
    xSurface* surf;
    S32 grounded;

    if (!ent->collis)
    {
        return;
    }

    if (!ent->frame)
    {
        return;
    }

    coll = ent->collis->colls;
    hit_it = coll->flags & 0x1;
    surf = zSurfaceGetSurface(coll);
    grounded = hit_it && surf && !surf->state && zSurfaceGetMatchOrient(surf);

    if (grounded)
    {
        xVec3* fup;
        xVec3* eup;
        xVec3 nfup;
        xVec3 neup;
        F32 fup_len;
        F32 eup_len;
        F32 fdecl;
        F32 edecl;

        if (!rms->lgrounded)
        {
            rms->tmr = 0.0f;
        }

        eup = &globals.player.RootUpTarget;
        fup = &globals.player.floor_norm;
        fup_len = xVec3Normalize(&nfup, fup);
        eup_len = xVec3Normalize(&neup, eup);
        fdecl = xacos(nfup.y);
        edecl = xacos(neup.y);

        if (edecl < rms->max_decl || fdecl < rms->max_decl)
        {
            xVec3 raxis;
            F32 rang;
            F32 dang;
            xMat4x3 rot;
            F32 s;

            xVec3Cross(&raxis, eup, fup);
            xVec3Normalize(&raxis, &raxis);

            rang = xVec3Dot(&nfup, &neup);
            if (rang > 1.0f)
            {
                rang = 1.0f;
            }

            rang = xacos(rang);
            if (rang)
            {
                s = dt / rms->tmatch;
                s = MIN(1.0f, s);

                if (fdecl >= rms->max_decl)
                {
                    s = MIN(s, (rms->max_decl - edecl) / (fdecl - edecl));
                }

                if (s)
                {
                    s *= rang;
                    s -= 0.001f;

                    xMat4x3Rot(&rot, &raxis, s, xEntGetPos(ent));
                    xMat3x3RMulVec(&globals.player.RootUpTarget, &rot, &neup);

                    globals.player.HangElapsed = 0.0f;
                }
            }
        }
    }
    else if (!globals.player.HangEnt)
    {
        if (rms->lgrounded)
        {
            rms->tmr = -0.1f;
        }

        if (rms->tmr > 0.0f)
        {
            xVec3* eup;
            xVec3 neup;
            F32 eup_len;
            xVec3 raxis;
            F32 rang;
            F32 dang;
            xMat4x3 rot;

            eup = &globals.player.RootUpTarget;
            eup_len = xVec3Normalize(&neup, eup);

            xVec3Init(&raxis, -neup.z, 0.0f, neup.x);

            rang = xacos(neup.y);
            if (rang)
            {
                dang = dt / rms->trelax;
                dang = MIN(1.0f, dang);
                dang *= rang;

                raxis.normalize();
                xMat4x3Rot(&rot, &raxis, dang, xEntGetPos(ent));
                xMat3x3RMulVec(&globals.player.RootUpTarget, &rot, &neup);

                globals.player.HangElapsed = 0.0f;
            }
        }
    }

    if (xabs(globals.player.RootUpTarget.y - 1.0f) < 1e-5f)
    {
        globals.player.RootUpTarget.x = 0.0f;
        globals.player.RootUpTarget.y = 1.0f;
        globals.player.RootUpTarget.z = 0.0f;

        globals.player.HangElapsed = 0.0f;
    }

    rms->tmr += dt;
    rms->lgrounded = grounded;
}

static void zEntPlayer_SNDInit()
{
    for (S32 player = 0; player < eCurrentPlayerCount; player++)
    {
        for (S32 snd = 0; snd < ePlayerSnd_Total; snd++)
        {
            sPlayerSndID[player][snd] = (iSndHandle)-1;
        }
    }

    for (S32 snd = 0; snd < ePlayerSnd_Total; snd++)
    {
        sPlayerRumble[snd] = NULL;
    }

    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_Land] = xStrHash("sb_land_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_Jump] = xStrHash("SB_jump_sngl_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_DoubleJump] = xStrHash("SB_jump_dub_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_BowlWindup] =
        xStrHash("SB_BOWL_WINDUP_LOOP_GRP");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_BowlRelease] = xStrHash("SB_BOWL_RELEASE_GRP");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_BubbleBashStart] = xStrHash("SB_bash_start_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_BubbleBashHit1] = xStrHash("SB_bash_hit1_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_BubbleBashHit2] = xStrHash("SB_bash_hit2_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_BubbleWand] = xStrHash("SB_KSPIN_GRP");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_CruiseStart] = xStrHash("SB_cruise_start_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_CruiseNavigate] =
        xStrHash("SB_cruise_nav_loop_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_CruiseHit] = xStrHash("SB_cruise_hit_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_BounceStrike] = xStrHash("SB_bounce_hit2_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_BoulderStart] = xStrHash("SB_wetball_start_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_BoulderRoll] = 0;
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_BoulderEnd] = xStrHash("SB_wetball_end_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_BellyMelee] = 0;
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_BellySmash] = 0;
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_Lift1] = 0;
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_Throw] = 0;
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_Chop] = 0;
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_Kick] = 0;
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_Heli] = 0;
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_LassoThrow] = 0;
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_LassoYank] = 0;
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_Ouch] = xStrHash("SB_Ouch_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_Death] = xStrHash("SB_Death_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_FruitCrackle] = 0;
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_CheckPoint] = 0;
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_PickupSpatula] = xStrHash("GSG_pickup_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_PickupUnderwear] = 0;
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_Bus] = xStrHash("Bus_all_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_Taxi] = 0;
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_SlideLoop] = xStrHash("SB_tb_loop_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_PickupSpatulaComment] = 0;
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_Sneak] = xStrHash("Xylo_sneaky_loop_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_SlipLoop] = xStrHash("SB_slip_idle_loop_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_BubbleBashMiss] = xStrHash("SB_bash_miss_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_WallJumpLand] = xStrHash("SB_WALLJUMP_LAND_GRP");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_GauntletTick] = xStrHash("SB_BASH2_TICK_GRP");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_MeleeDeflect] = xStrHash("PL_KSPIN_DFLECT_GRP");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_BungeeWind] = xStrHash("wind2loop_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_BungeeAttach] = xStrHash("SB_bungee1_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_BungeeRelease] = xStrHash("SB_bungee2_grp");
    sPlayerSnd[eCurrentPlayerSpongeBob][ePlayerSnd_BungeeDive] = xStrHash("SBG01003");

    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_Land] = xStrHash("pat_land_grp");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_Jump] = xStrHash("Pat_jump_sngl_grp");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_DoubleJump] = xStrHash("Pat_jump_dub_grp");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_BowlWindup] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_BowlRelease] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_BubbleBashStart] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_BubbleBashHit1] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_BubbleBashHit2] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_BubbleWand] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_CruiseStart] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_CruiseNavigate] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_CruiseHit] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_BounceStrike] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_BoulderStart] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_BoulderRoll] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_BoulderEnd] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_BellyMelee] = xStrHash("Pat_belly_grp");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_BellySmash] = xStrHash("Pat_smash_belly_grp");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_Lift1] = xStrHash("Pat_lift3B_grp");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_Throw] = xStrHash("Pat_throw_grp");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_Chop] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_Kick] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_Heli] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_LassoThrow] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_LassoYank] = xStrHash("Pat_swing_forth_grp");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_Ouch] = xStrHash("Pat_Ouch_grp");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_Death] = xStrHash("SB_Death_grp");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_FruitCrackle] = xStrHash("Ffruit_crackle_grp");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_CheckPoint] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_PickupSpatula] = xStrHash("GSG_pickup_grp");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_PickupUnderwear] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_Bus] = xStrHash("bus_all_grp");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_Taxi] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_SlideLoop] = xStrHash("Pat_slide_loop_grp");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_PickupSpatulaComment] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_Sneak] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_CartwheelLoop] = xStrHash("PS_CARTLOOP_GRP");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_HammerThrow] = xStrHash("PS_HAMMERTHROW_GRP");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_HammerThrowLoop] =
        xStrHash("PS_HAMMERTHROW_LOOP_GRP");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_Cartwheel2Loop] = xStrHash("PS_CARTLOOP2_GRP");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_MeleeDeflect] = xStrHash("PL_KSPIN_DFLECT_GRP");
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_BellySmash2] = xStrHash("Pat_smash_belly2_grp");

    sPlayerSnd[eCurrentPlayerSpongePat][ePlayerSnd_Land] = xStrHash("sb_land_grp");
    sPlayerSnd[eCurrentPlayerSpongePat][ePlayerSnd_Jump] = xStrHash("SB_jump_sngl_grp");
    sPlayerSnd[eCurrentPlayerSpongePat][ePlayerSnd_DoubleJump] = xStrHash("SB_jump_dub_grp");
    sPlayerSnd[eCurrentPlayerSpongePat][ePlayerSnd_Ouch] = xStrHash("SB_Ouch_grp");
    sPlayerSnd[eCurrentPlayerSpongePat][ePlayerSnd_Death] = 0;
    sPlayerSnd[eCurrentPlayerSpongePat][ePlayerSnd_FruitCrackle] = 0;
    sPlayerSnd[eCurrentPlayerSpongePat][ePlayerSnd_CheckPoint] = 0;
    sPlayerSnd[eCurrentPlayerSpongePat][ePlayerSnd_PickupSpatula] = xStrHash("GSG_pickup_grp");
    sPlayerSnd[eCurrentPlayerSpongePat][ePlayerSnd_PickupUnderwear] = 0;
    sPlayerSnd[eCurrentPlayerSpongePat][ePlayerSnd_SlideLoop] = xStrHash("SB_tb_loop_grp");
    sPlayerSnd[eCurrentPlayerSpongePat][ePlayerSnd_PickupSpatulaComment] = 0;

    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_BungeeWind] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_BungeeAttach] = 0;
    sPlayerSnd[eCurrentPlayerPatrick][ePlayerSnd_BungeeRelease] = 0;

    sPlayerOneLiners[eCurrentPlayerSpongeBob].InitWithSounds(
        spongeBobOneLinerData, sizeof(spongeBobOneLinerData) / sizeof(zStreamedSound) - 1);
    sPlayerOneLiners[eCurrentPlayerPatrick].InitWithSounds(
        patrickOneLinerData, sizeof(patrickOneLinerData) / sizeof(zStreamedSound) - 1);
    sPlayerOneLiners[eCurrentPlayerCar].InitWithSounds(
        carEmptyOneLinerData, sizeof(carEmptyOneLinerData) / sizeof(zStreamedSound) - 1);
    sPlayerOneLiners[eCurrentPlayerSpongePat].InitWithSounds(
        spongePatOneLinerData, sizeof(spongePatOneLinerData) / sizeof(zStreamedSound) - 1);
    sSlidingOneLiners.InitWithSounds(slidingOneLinerData,
                                     sizeof(slidingOneLinerData) / sizeof(zStreamedSound) - 1);

    sIdleCommentTimer.Init(60.0f, false, ePlayerStreamSnd_Waiting);
    sRunningCommentTimer.Init(10.0f, false, ePlayerStreamSnd_RunningLong);
    sPatCarryCommentTimer.Init(5.0f, true, ePlayerStreamSnd_PatCarry);
    sRollingCommentTimer.Init(7.0f, false, ePlayerStreamSnd_SBSpongeBallMove);

    zEntPlayerFX::SFX::Init();

    sPlayerRumble[ePlayerSnd_Land] = zSceneFindObject(xStrHash("RUMBLE_PLAYER_LANDING"));
    sPlayerRumble[ePlayerSnd_BowlRelease] = zSceneFindObject(xStrHash("RUMBLE_BOWL_RELEASE"));
    sPlayerRumble[ePlayerSnd_BubbleBashHit1] = zSceneFindObject(xStrHash("RUMBLE_BUBBLE_BASH"));
    sPlayerRumble[ePlayerSnd_BubbleBashHit2] = zSceneFindObject(xStrHash("RUMBLE_BUBBLE_BASH"));
    sPlayerRumble[ePlayerSnd_BounceStrike] = zSceneFindObject(xStrHash("RUMBLE_BOUNCE_STRIKE"));
    sPlayerRumble[ePlayerSnd_Lift1] = zSceneFindObject(xStrHash("RUMBLE_PLAYER_LIFT"));
    sPlayerRumble[ePlayerSnd_Throw] = zSceneFindObject(xStrHash("RUMBLE_PLAYER_THROW"));
    sPlayerRumble[ePlayerSnd_LassoThrow] = zSceneFindObject(xStrHash("RUMBLE_LASSO_THROW"));
    sPlayerRumble[ePlayerSnd_LassoYank] = zSceneFindObject(xStrHash("RUMBLE_LASSO_YANK"));
    sPlayerRumble[ePlayerSnd_Ouch] = zSceneFindObject(xStrHash("RUMBLE_PLAYER_OUCH"));
    sPlayerRumble[ePlayerSnd_Death] = zSceneFindObject(xStrHash("RUMBLE_PLAYER_DEATH"));
    sPlayerRumble[ePlayerSnd_CheckPoint] = zSceneFindObject(xStrHash("RUMBLE_CHECKPOINT"));
    sPlayerRumble[ePlayerSnd_Bus] = zSceneFindObject(xStrHash("RUMBLE_BUS"));
    sPlayerRumble[ePlayerSnd_Taxi] = zSceneFindObject(xStrHash("RUMBLE_TAXI"));
}

void zEntPlayer_SNDPlay(_tagePlayerSnd player_snd, bool playRumble)
{
    if (globals.cmgr == NULL && !sPlayerIgnoreSound)
    {
        if (gCurrentPlayer >= eCurrentPlayerSpongeBob && gCurrentPlayer < eCurrentPlayerCount &&
            sPlayerSnd[gCurrentPlayer][player_snd])
        {
            sPlayerSndID[gCurrentPlayer][player_snd] =
                xSndMgrPlay(xSndMgrGetSoundGroup(sPlayerSnd[gCurrentPlayer][player_snd]), 0, NULL,
                            NULL, xglobals->___player_ent_dont_use_directly, NULL, NULL);

            if (playRumble && sPlayerRumble[player_snd])
            {
                zEntEvent(sPlayerRumble[player_snd], eEventStartRumbleEffect,
                          globals.currentActivePad, 0.0f, 0.0f, 0.0f);
            }
        }
    }
}

void zEntPlayer_SNDPlayStream(_tagePlayerStreamSnd player_snd)
{
    switch (gCurrentPlayer)
    {
    case eCurrentPlayerSpongeBob:
    case eCurrentPlayerPatrick:
        sPlayerOneLiners[gCurrentPlayer].PlaySound(player_snd);
        break;
    }
}

void zEntPlayer_SNDPlayUpgradeVO(int powerupIndex)
{
    iSndGroupHandle VOGroup;

    if (powerupIndex < 8)
    {
        eSNDCurrentPlayer player =
            powerupIndex < 4 ? eCurrentPlayerSpongeBob : eCurrentPlayerPatrick;
        VOGroup = sPlayerOneLiners[player].GetSoundGroup(ePlayerStreamSnd_UpgradeCommentsBase +
                                                         powerupIndex % 4);
    }
    else
    {
        eSNDCurrentPlayer player =
            powerupIndex == 8 ? eCurrentPlayerSpongeBob : eCurrentPlayerPatrick;
        VOGroup =
            sPlayerOneLiners[player].GetSoundGroup(ePlayerStreamSnd_UpgradeCommentsHealthBase +
                                                   globals.player.g.PowerUp[powerupIndex] - 1);
    }

    xSndMgrPlay(VOGroup, 0, NULL, NULL, NULL, NULL, NULL);
}

void zEntPlayer_SNDPlayStream(U32 lower, U32 upper, _tagePlayerStreamSnd player_snd)
{
    if (globals.player.InvGoofyGoober >= lower && globals.player.InvGoofyGoober <= upper)
    {
        zEntPlayer_SNDPlayStream(player_snd);
    }
}

void zEntPlayer_SNDSetVol(_tagePlayerSnd player_snd, F32 new_vol)
{
    if (gCurrentPlayer >= eCurrentPlayerSpongeBob && gCurrentPlayer < eCurrentPlayerCount &&
        sPlayerSnd[gCurrentPlayer][player_snd])
    {
        xSndMgrSetVolume(sPlayerSndID[gCurrentPlayer][player_snd], new_vol);
    }
}

void zEntPlayer_SNDSetPitch(_tagePlayerSnd player_snd, F32 new_pitch)
{
    if (gCurrentPlayer >= eCurrentPlayerSpongeBob && gCurrentPlayer < eCurrentPlayerCount &&
        sPlayerSnd[gCurrentPlayer][player_snd])
    {
        xSndMgrSetPitch(sPlayerSndID[gCurrentPlayer][player_snd], new_pitch);
    }
}

void zEntPlayer_SNDStop(_tagePlayerSnd player_snd)
{
    if (gCurrentPlayer >= eCurrentPlayerSpongeBob && gCurrentPlayer < eCurrentPlayerCount &&
        sPlayerSnd[gCurrentPlayer][player_snd] &&
        sPlayerSndID[gCurrentPlayer][player_snd] != (iSndHandle)-1)
    {
        xSndMgrStop(sPlayerSndID[gCurrentPlayer][player_snd]);
        sPlayerSndID[gCurrentPlayer][player_snd] = (iSndHandle)-1;

        if (globals.sceneCur->sceneID != 'MNU3')
        {
            xRumble::Manager::Get().StopRumbling();
        }
    }
}

bool zEntPlayer_SoundsCurrentlyAllowed()
{
    if (globals.cmgr)
    {
        return false;
    }

    if (zNMECommon_IsConversation())
    {
        return false;
    }

    return !sPlayerIgnoreSound;
}

void zEntPlayer_TikiKilledBy(zNMECommon* tiki, const xBase* killedBy)
{
    xEnt* ent = (xEnt*)killedBy;

    if ((ent->baseFlags & 0x20) &&
        (ent->baseType == eBaseTypePlayer || ent == zEntPlayerGetBBowl()))
    {
        en_npctyp typ = (en_npctyp)tiki->itemType;

        switch (gCurrentPlayer)
        {
        case eCurrentPlayerSpongeBob:
        case eCurrentPlayerPatrick:
            sPlayerOneLiners[gCurrentPlayer].PlaySound(typ - NME_TYPE_TIKI +
                                                       ePlayerStreamSnd_SmashTiki);
            break;
        case eCurrentPlayerSpongePat:
            sSlidingOneLiners.PlaySound(1);
            break;
        }
    }
}

// xEntBeginCollide without the bupdate call. Plain static: on GC it is auto-inlined into its only
// caller and dead-stripped (the PS2 demo keeps it out of line).
static void PlayerBeginCollideNoBupdate(xEnt* ent, xScene*, F32)
{
    U8 idx;
    xCollis* coll;

    for (idx = 0; idx < 18; idx++)
    {
        coll = &ent->collis->colls[idx];

        coll->flags = 0x1F00;
        coll->optr = NULL;
        coll->mptr = NULL;
        coll->dist = FLOAT_MAX;
    }

    ent->collis->idx = 6;
    ent->collis->stat_sidx = 6;
    ent->collis->stat_eidx = 6;
    ent->collis->dyn_sidx = 6;
    ent->collis->dyn_eidx = 6;
    ent->collis->npc_sidx = 6;
    ent->collis->npc_eidx = 6;
    ent->collis->env_sidx = 6;
    ent->collis->env_eidx = 6;
}

// No GC symbol (always inlined); BfBB has it as a separate function.
static inline void PlayerCollsDetect(xEnt* ent, xScene* sc, F32 dt)
{
    PlayerBeginCollideNoBupdate(ent, sc, dt);

    if (ent->collis->chk & XENT_COLLTYPE_ENV)
    {
        xEntCollCheckEnv(ent, sc);
    }

    if (ent->collis->chk & XENT_COLLTYPE_NPC)
    {
        xEntCollCheckNPCs(ent, sc, PlayerCollCheckOneVillain);
    }

    if (ent->collis->chk & XENT_COLLTYPE_DYN)
    {
        xEntCollCheckDyns(ent, sc, PlayerCollCheckOneEnt);
    }

    if (ent->collis->chk & XENT_COLLTYPE_STAT)
    {
        xEntCollCheckStats(ent, sc, PlayerCollCheckOneEnt);
    }
}

// Placeholder for dead-stripped code whose function-local checkVec remains, unreferenced, in
// retail .data (BfBB retail has the same orphan).
static void __deadstripped_zEntPlayer3()
{
    static xVec3 checkVec[2] = { { 0.0f, 0.0f, 0.3f }, { 0.0f, 0.0f, -0.3f } };
}

// Unreferenced in this build (BfBB called it from zEntPlayer_Init), so its code is stripped; its
// static tables survive in .data. Body from BfBB.
static void PlayerHackFixBbashMiss(xModelInstance* model)
{
    static char* bbstate[5] = { "BbashStart01", "BbashStart02", "BbashAttack01", "BbashStrike01",
                                "BbashMiss01" };
    static F32 bbadjust[5][2] = { { 0.0f, -0.55f },
                                  { 0.0f, -0.55f },
                                  { -0.55f, -0.55f },
                                  { -0.55f, -0.55f },
                                  { -0.55f, -0.55f } };
    static F32 bbspeed[5] = { 1.0f, 1.0f, 0.96f, 0.96f, 0.92f };

    S32 i;
    xAnimState* astate;
    xVec3 tran[2];
    iAnimSKBHeader* skb;
    xVec3 tranList[128];
    S32 tranCount;

    for (i = 0; i < 5; i++)
    {
        astate = xAnimTableGetState(model->Anim->Table, bbstate[i]);
        if (astate)
        {
            skb = (iAnimSKBHeader*)astate->Data->RawData[0];

            // The top bit of ExtractedData marks an animation that was already adjusted.
            if (!((UPtr)skb->ExtractedData & 0x80000000))
            {
                if (i == 4)
                {
                    tranCount = _iAnimSKBExtractTranslate(skb, 1, tranList, 128);
                    tranList[0].y += bbadjust[i][0];
                    tranList[tranCount - 1].y += bbadjust[i][1];
                    _iAnimSKBAdjustTranslate(skb, 1, &tranList[0].x, &tranList[tranCount - 1].x);
                }
                else
                {
                    tran[0].x = 0.0f;
                    tran[0].y = bbadjust[i][0];
                    tran[0].z = 0.0f;
                    tran[1].x = 0.0f;
                    tran[1].y = bbadjust[i][1];
                    tran[1].z = 0.0f;
                    _iAnimSKBAdjustTranslate(skb, 1, &tran[0].x, &tran[1].x);
                }

                skb->ExtractedData = (void*)((UPtr)skb->ExtractedData | 0x80000000);
            }

            astate->Speed = bbspeed[i];
        }
    }
}

static void PlayerLedgeInit(zLedgeGrabParams* ledge, xModelInstance* model)
{
    S32 i;
    xAnimState* idle = model->Anim->Table->StateList;
    xAnimState* grab = xAnimTableGetState(model->Anim->Table, "LedgeGrab01");

    if (grab && !(grab->UserFlags & 0x40000000))
    {
        if (!((iAnimSKBHeader*)grab->Data->RawData[0])->ExtractedData)
        {
            xVec3 tran[64];
            xQuat quat[64];

            ledge->tranCount = _iAnimSKBExtractTranslate((iAnimSKBHeader*)grab->Data->RawData[0], 1,
                                                         ledge->tranTable, 60);

            iAnimEval(idle->Data->RawData[0], 0.0f, 0, tran, quat);
            _iAnimSKBAdjustTranslate((iAnimSKBHeader*)grab->Data->RawData[0], 1, &tran[1].x,
                                     &tran[1].x);

            for (i = 0; i < ledge->tranCount; i++)
            {
                ledge->tranTable[i].x -= tran[1].x;
                ledge->tranTable[i].y -= tran[1].y;
                ledge->tranTable[i].z -= tran[1].z;
            }

            ledge->zdist = ledge->tranTable[ledge->tranCount - 1].z;
            ((iAnimSKBHeader*)grab->Data->RawData[0])->ExtractedData = ledge;
        }

        grab->Data->FileFlags &= ~0x777;
        xAnimFileSetTime(grab->Data, grab->Data->Duration, grab->Data->TimeOffset);
        ledge->ttime = grab->Data->Duration / grab->Speed;
    }
    else
    {
        ledge->ttime = 0.5f;
        ledge->tranCount = 0;
        ledge->zdist = 0.1f;
    }

    ledge->optr = NULL;
    ledge->y0det = 0.0f;
    ledge->dydet = 1.2f;
    ledge->r0det = 0.5f;
    ledge->drdet = 0.7f;
    ledge->thdet = PI / 2;
    ledge->rtime = 0.2f;
    ledge->tmr = 0.0f;
    ledge->spos.x = 0.0f;
    ledge->spos.y = 0.0f;
    ledge->spos.z = 0.0f;
    ledge->tpos.x = 0.0f;
    ledge->tpos.y = 0.0f;
    ledge->tpos.z = 0.0f;
    ledge->nrays = 3;
    ledge->rrand = 0;
}

static void PlayerLedgeUpdate(xEnt* ent, xScene* sc, F32 dt)
{
    zLedgeGrabParams* ledge = &globals.player.s->ledge;

    if (ledge->tmr > 0.0f)
    {
        F32 tnext = ledge->tmr + dt;
        F32 ttest = ledge->ttime - ledge->tmr;

        if (ttest < dt)
        {
            ledge->tmr = 0.0f;
        }
        else
        {
            ledge->tmr = tnext;
        }

        F32 tcap = MIN(tnext, ledge->ttime);
        F32 s = tcap / ledge->ttime;

        if (ledge->optr)
        {
            xMat4x3 tmpmat;

            RwMatrixInvert((RwMatrix*)&tmpmat, (RwMatrix*)&ledge->omat);
            xMat4x3Mul(&tmpmat, &tmpmat, (xMat4x3*)ledge->optr->model->Mat);
            xMat4x3Toworld(&ledge->spos, &tmpmat, &ledge->spos);
            xMat4x3Toworld(&ledge->epos, &tmpmat, &ledge->epos);
            xMat4x3Toworld(&ledge->tpos, &tmpmat, &ledge->tpos);
            ledge->omat = *(xMat4x3*)ledge->optr->model->Mat;
            ledge->endrot = xatan2(ledge->tpos.x - ledge->spos.x, ledge->tpos.z - ledge->spos.z);

            if (ledge->startrot > PI + ledge->endrot)
            {
                ledge->startrot -= 2.0f * PI;
            }
            else if (ledge->startrot < ledge->endrot - PI)
            {
                ledge->startrot += 2.0f * PI;
            }
        }

        if (ledge->tranCount != 0)
        {
            F32 animTime;
            F32 timelerp;
            F32 animDiffZ;
            S32 sidx;
            F32 sfactor;
            F32 sscale;
            // not in the DWARF (fully scalarized): the direction has to be a struct for retail's
            // registers.
            xVec3 dir;
            xVec3 tran;

            if (xStricmp(ent->model->Anim->Single->State->Name, "LedgeGrab01") != 0)
            {
                animTime = 1000.0f;
            }
            else
            {
                animTime = ent->model->Anim->Single->Time;
            }

            dir.x = ledge->epos.x - ledge->spos.x;
            dir.z = ledge->epos.z - ledge->spos.z;
            sscale = xsqrt(dir.x * dir.x + dir.z * dir.z);
            dir.x /= sscale;
            dir.z /= sscale;
            animDiffZ = sscale + ledge->tranTable[0].z;

            if (animTime < ledge->animGrab)
            {
                timelerp = 1.0f - animTime / ledge->animGrab;
            }
            else
            {
                timelerp = 0.0f;
            }

            sscale = s * (ledge->tranCount - 1);
            sfactor = std::floorf(sscale);
            sidx = sfactor;
            sscale -= sfactor;

            if (sidx >= ledge->tranCount - 1)
            {
                sscale = 1.0f;
                sidx--;
            }

            xVec3Lerp(&tran, &ledge->tranTable[sidx], &ledge->tranTable[sidx + 1], sscale);
            tran.x -= ledge->tranTable[ledge->tranCount - 1].x;
            tran.y -= ledge->tranTable[ledge->tranCount - 1].y;
            tran.z -= ledge->tranTable[ledge->tranCount - 1].z;

            ent->frame->mat.pos.x = tran.x * dir.z + dir.x * (tran.z - timelerp * animDiffZ);
            ent->frame->mat.pos.y = tran.y;
            ent->frame->mat.pos.z = -tran.x * dir.x + dir.z * (tran.z - timelerp * animDiffZ);
            ent->frame->mat.pos.x += ledge->tpos.x;
            ent->frame->mat.pos.y += ledge->tpos.y;
            ent->frame->mat.pos.z += ledge->tpos.z;
        }
        else
        {
            xVec3Lerp(&ent->frame->mat.pos, &ledge->spos, &ledge->tpos, s);
            ent->frame->mat.pos.y += isin(PI * s);
        }

        ent->frame->vel.x = 0.0f;
        ent->frame->vel.y = 0.0f;
        ent->frame->vel.z = 0.0f;
        tslide_inair_tmr = 0.0f;
        tslide_dbl_tmr = 0.0f;
        globals.player.SlideTrackDecay = 0.0f;
        tslide_ground = 0;

        tcap = MIN(tnext / ledge->rtime, 1.0f);

        ent->frame->rot.angle =
            globals.player.s->ledge.startrot +
            tcap * (globals.player.s->ledge.endrot - globals.player.s->ledge.startrot);
        xMat3x3Rot(&ent->frame->mat, &ent->frame->rot.axis, ent->frame->rot.angle);
    }
    else
    {
        ledge->tmr = 0.0f;

        if (ent->frame->vel.y >= 0.0f)
        {
            return;
        }

        if (tslide_ground)
        {
            return;
        }

        xNearFloorPoly nfpoly;
        S32 chknum;

        nfpoly.box.upper.x = 1.0f + ent->frame->mat.pos.x;
        nfpoly.box.upper.y = 1.0f + ent->frame->mat.pos.y;
        nfpoly.box.upper.z = 1.0f + ent->frame->mat.pos.z;
        nfpoly.box.lower.x = ent->frame->mat.pos.x - 1.0f;
        nfpoly.box.lower.y = 0.5f + ent->frame->mat.pos.y;
        nfpoly.box.lower.z = ent->frame->mat.pos.z - 1.0f;
        nfpoly.floorDot = 0.866f;
        nfpoly.facingDot = -1.0f;
        nfpoly.facingVec = ent->frame->mat.at;
        xVec3Lerp(&nfpoly.center, &nfpoly.box.upper, &nfpoly.box.lower, 0.5f);

        for (chknum = 0; chknum <= 2; chknum++)
        {
            if (!xSceneNearestFloorPoly(sc, &nfpoly, 0x10, 0x26))
            {
                return;
            }

            if (nfpoly.neardist < 0.4f)
            {
                return;
            }

            xSurface* surface;

            if (nfpoly.optr)
            {
                surface = nfpoly.mptr->Surf;
                if (surface && !(((zSurfaceProps*)surface->moprops)->asset->phys_flags & 0x40))
                {
                    // 'break', not 'return': retail branches straight to the exit here
                    break;
                }
            }
            else
            {
                // retail ignores this result: only model surfaces are checked
                surface = zSurfaceGetSurface(nfpoly.oid);
            }

            F32 neardx = nfpoly.nearpt.x - ent->frame->mat.pos.x;
            F32 neardz = nfpoly.nearpt.z - ent->frame->mat.pos.z;
            F32 sscale = xsqrt(neardx * neardx + neardz * neardz);

            if (sscale > 1e-7f && sscale < 1.0f &&
                (neardx * nfpoly.facingVec.x + neardz * nfpoly.facingVec.z) / sscale > 0.7071f)
            {
                xRay3 groundray;
                xCollis groundcoll;

                groundray.origin.x = ent->frame->mat.pos.x;
                groundray.origin.y = 1.0f + ent->frame->mat.pos.y;
                groundray.origin.z = ent->frame->mat.pos.z;
                groundray.dir.x = 0.0f;
                groundray.dir.y = -1.0f;
                groundray.dir.z = 0.0f;
                groundray.min_t = 0.0f;
                groundray.max_t = groundray.origin.y - (-1.25f + nfpoly.nearpt.y);
                groundray.flags = 0xc00;
                groundcoll.flags = 0;
                groundcoll.dist = 1e38f;
                groundcoll.optr = NULL;
                groundcoll.mptr = NULL;
                xRayHitsSceneFlags(sc, &groundray, &groundcoll, 0x10, 0x26);

                if (groundcoll.dist < 1e38f)
                {
                    return;
                }

                F32 sfactor = neardx / sscale;
                ledge->tpos.x = sfactor * ledge->zdist + nfpoly.nearpt.x;
                ledge->tpos.y = 0.2f + nfpoly.nearpt.y;
                sfactor = neardz / sscale;
                ledge->tpos.z = sfactor * ledge->zdist + nfpoly.nearpt.z;

                xVec3 bpos = ent->bound.sph.center;
                ent->bound.sph.center = ledge->tpos;
                ent->bound.sph.center.y += 0.51f;
                xEntCollis collis = *ent->collis;

                PlayerCollsDetect(ent, sc, dt);

                if (ent->collis->idx > 6)
                {
                    ent->bound.sph.center = bpos;
                    *ent->collis = collis;
                    return;
                }

                ent->bound.sph.center = bpos;
                *ent->collis = collis;

                ledge->tmr = -1.0f;
                ledge->spos = *xEntGetPos(ent);
                ledge->epos = nfpoly.nearpt;
                ledge->tpos.y -= 0.2f;
                ledge->endrot =
                    xatan2(ledge->tpos.x - ledge->spos.x, ledge->tpos.z - ledge->spos.z);
                ledge->optr = (xEnt*)nfpoly.optr;

                if (ledge->optr)
                {
                    ledge->omat = *(xMat4x3*)ledge->optr->model->Mat;
                }

                return;
            }

            if (chknum == 0)
            {
                nfpoly.box.upper.x += 0.2f * ent->frame->mat.at.x - 0.2f;
                nfpoly.box.upper.z += 0.2f * ent->frame->mat.at.z - 0.2f;
                nfpoly.box.lower.x += 0.2f * ent->frame->mat.at.x + 0.2f;
                nfpoly.box.lower.z += 0.2f * ent->frame->mat.at.z + 0.2f;
                nfpoly.center.x += 0.2f * ent->frame->mat.at.x;
                nfpoly.center.z += 0.2f * ent->frame->mat.at.z;
            }
        }
    }
}

static void zEntPlayer_UpdateVelocityBlur()
{
    static F32 start_vel = 10.0f;
    static F32 peak_vel = 50.0f;
    static F32 max_blur = 0.5f;

    if (start_vel >= peak_vel)
    {
        return;
    }

    F32 start_vel2 = start_vel * start_vel;
    F32 peak_vel2 = peak_vel * peak_vel;
    xVec3& vel = xglobals->___player_ent_dont_use_directly->frame->vel;
    if (vel.length2() < start_vel2)
    {
        iCameraSetBlurriness(0.0f);
    }
    else
    {
        F32 blur = vel.length2() / (peak_vel2 - start_vel2);
        if (blur > 1.0f)
        {
            blur = 1.0f;
        }
        iCameraSetBlurriness(blur * max_blur);
    }
}

void zEntPlayer_SceneExit()
{
    S32 i;

    globals.player.model_car = NULL;
    globals.player.model_patrick = NULL;
    globals.player.model_spongebob = NULL;
    globals.player.model_spongepat = NULL;
    globals.player.model_wand = NULL;
    memset(globals.player.sb_models, 0, sizeof(globals.player.sb_models));

    sNumHitches = 0;

    for (i = 0; i <= 1; i++)
    {
        xEntBoulder_Kill(globals.player.bubblebowl[i]);
    }

    if (zEntPlayerGetBBowl())
    {
        zEntPlayerFX::SuperMachoBowlLightning::RemoveAll();
    }

    memset(&globals.player.bubblebowl[0], 0, sizeof(globals.player.bubblebowl));

    cruise_bubble::reset();

    zEntPlayerFX::SuperMachoBowlExplosion::SceneExit();
    zEntPlayerFX::CartShield::SceneExit();
}

static void dont_move(xEnt* ent, xScene*, F32 dt, xEntFrame*)
{
    PlayerAbsControl(ent, 0.0f, 0.0f, dt);
}

U8 zEntPlayer_MinimalUpdate(xEnt* ent, xScene* sc, F32 dt, xVec3& drive_motion)
{
    U8 stop_the_madness = 0;

    if (oob_state::update(*sc, dt))
    {
        return 1;
    }

    if (sBowlBlastRadius > 0.0f && zEntPlayerGetBBowl())
    {
        xBound bowlBound;
        bowlBound.type = XBOUND_TYPE_SPHERE;
        sBowlBlastRadius += 10.0f * dt;
        xSphere* bowlSph = &bowlBound.sph;
        xVec3Copy(&bowlSph->center, (xVec3*)&zEntPlayerGetBBowl()->model->Mat->pos);
        bowlSph->r = sBowlBlastRadius;
        xQuickCullForBound(&bowlBound.qcd, &bowlBound);
        AttackBoundCollide(ent, (zScene*)sc, &bowlBound, PLYR_ATAK_SB_BOWL);
        if (sBowlBlastRadius >= 4.0f)
        {
            sBowlBlastRadius = 0.0f;
        }
    }

    zEntPlayerBowlTargetUpdate(dt);

    if (sGauntletBlastRadius)
    {
        xBound gauntletBound;
        gauntletBound.type = XBOUND_TYPE_SPHERE;
        xSphere* gauntletSph = &gauntletBound.sph;
        xVec3Copy(&gauntletSph->center, (xVec3*)&sGauntletHaz->mdl_hazard->Mat->pos);
        gauntletSph->r = sGauntletBlastRadius;
        xQuickCullForBound(&gauntletBound.qcd, &gauntletBound);
        AttackBoundCollide(ent, (zScene*)sc, &gauntletBound, PLYR_ATAK_SB_BASH);
        zEntPlayerFX::GauntletExplosion::Explode(sGauntletHaz->pos_hazard);
    }

    zEntPickup_CheckAllPickupsAgainstPlayer(sc, dt);

    xEntBeginUpdate(ent, sc, dt);
    zGooCollsBegin();

    xVec3 suboldpos = ent->frame->mat.pos;

    xEntApplyPhysics(ent, sc, dt);

    void (*old_move)(xEnt*, xScene*, F32, xEntFrame*) = ent->move;
    ent->move = dont_move;
    xEntMove(ent, sc, dt);
    ent->move = old_move;

    xVec3 req_motion = ent->frame->dpos;
    xVec3 predrive_loc = ent->frame->mat.pos;

    // Tikis have no frame of their own: lend the drivers a temporary one for the drive update.
    U8 isDriverTiki = 0;
    xEntFrame* dummyFrame = (xEntFrame*)xMemPushTemp(sizeof(xEntFrame));
    if (globals.player.drv.driver && globals.player.drv.driver->baseType == eBaseTypeNPC &&
        ((zNMECommon*)globals.player.drv.driver)->SelfType() >= NME_TYPE_TIKI &&
        ((zNMECommon*)globals.player.drv.driver)->SelfType() <= NME_TYPE_TIKI_STONE)
    {
        globals.player.drv.driver->frame = dummyFrame;
        isDriverTiki = 1;
        xMat4x3Copy(&globals.player.drv.driver->frame->mat,
                    xEntGetFrame(globals.player.drv.driver));
    }

    U8 isOldDriverTiki = 0;
    xEntFrame* dummyFrameOldDrv = (xEntFrame*)xMemPushTemp(sizeof(xEntFrame));
    zNMETiki* tiki = (zNMETiki*)globals.player.drv.old_driver;
    if (tiki && tiki->baseType == eBaseTypeNPC && tiki->SelfType() >= NME_TYPE_TIKI &&
        tiki->SelfType() <= NME_TYPE_TIKI_STONE)
    {
        tiki->frame = dummyFrameOldDrv;
        isOldDriverTiki = 1;
        xMat4x3Copy(&tiki->frame->mat, xEntGetFrame(tiki));
        tiki->frame->mat.pos -= tiki->deltaPos;
    }

    xEntDriveUpdate(&globals.player.drv, sc, dt, NULL);

    drive_motion = ent->frame->mat.pos - predrive_loc;

    if (isDriverTiki)
    {
        globals.player.drv.driver->frame = NULL;
    }

    if (isOldDriverTiki)
    {
        globals.player.drv.old_driver->frame = NULL;
    }

    xMemPopTemp(dummyFrameOldDrv);
    xMemPopTemp(dummyFrame);

    // not in the DWARF: retail subtracts before the JumpState test, as BfBB does
    xVec3 motion;
    xVec3Sub(&motion, &ent->frame->mat.pos, &suboldpos);

    if ((globals.player.JumpState == 0 || globals.player.JumpState == 1) &&
        !globals.player.FallDeathTimer)
    {
        // not in the DWARF: retail multiplies separately (fmuls + fsubs), as BfBB does
        F32 sink = 0.5f * dt;
        ent->frame->mat.pos.y -= sink;

        xVec3& floor_norm = globals.player.floor_norm;
        F32 ndotm = floor_norm.x * motion.x + floor_norm.z * motion.z;

        if (ndotm > 0.0f && (req_motion.x != 0.0f || req_motion.z != 0.0f || surfSlickRatio))
        {
            globals.player.slope = -1;
            ent->frame->mat.pos.y -= xsqrt(ndotm);
        }
        else if (ndotm < 0.0f)
        {
            globals.player.slope = 1;
        }
        else
        {
            globals.player.slope = 0;
        }
    }

    zEntPlayerCollide(ent, sc, dt);
    zEntPlayer_CheckCritterContact(ent, dt);
    xEntBoulder_ApplyForces(ent->collis);
    zGooCollsEnd();

    xCollis* fcoll = &ent->collis->colls[0];

    if ((fcoll->flags & 1) && fcoll->optr && ((xBase*)fcoll->optr)->baseType == eBaseTypeButton)
    {
        zEntButton_Hold((_zEntButton*)fcoll->optr, 0x400);
    }

    if ((fcoll->flags & 1) && fcoll->optr && zGooIs((xEnt*)fcoll->optr))
    {
        stop_the_madness = 1;
    }

    zEntPlayerFloorUpdate(ent, sc, dt);
    zEntPlayerSurfDamageUpdate(ent, sc, dt);
    zEntPlayerDriveUpdate(ent, sc, dt);
    zEntPlayerJumpUpdate(ent, sc, dt);
    zEntPlayerCollTrigger(ent, sc);
    zEntPlayerVelUpdate(ent, sc, dt);
    xEntEndUpdate(ent, sc, dt);

    if (globals.player.DamageTimer > 0.0f)
    {
        globals.player.DamageTimer -= dt;
        if (globals.player.DamageTimer < 0.0f)
        {
            globals.player.DamageTimer = 0.0f;
        }
    }

    return stop_the_madness;
}

// No symbol on GC: inlined into zEntPlayer_Render through cruise_bubble::render().
void zEntPlayer_MinimalRender(zEnt* ent)
{
    F32 reticle_order[2];

    for (U32 i = 0; i < 2; i++)
    {
        if (gReticleTarget[i])
        {
            xVec3& loc = *(xVec3*)&ent->model->Mat->pos;
            xVec3& reticle_loc = *(xVec3*)&gReticleTarget[i]->model->Mat->pos;
            xVec3 cam_offset = globals.oldSkoolCamera.mat.pos - loc;
            xVec3 reticle_offset = reticle_loc - loc;

            reticle_order[i] = cam_offset.dot(reticle_offset);
            if (reticle_order[i] <= 0.0f)
            {
                zEntPlayer_ReticleRender(ent, (ePlayerReticleType)i);
            }
        }
    }

    xEntRender(xglobals->___player_ent_dont_use_directly);

    for (U32 i = 0; i < 2; i++)
    {
        if (reticle_order[i] > 0.0f)
        {
            zEntPlayer_ReticleRender(ent, (ePlayerReticleType)i);
        }
    }
}

static inline void zLedgeAdjust(zLedgeGrabParams* params)
{
    params->animGrab *= (1.0f / 30);
}

void zEntPlayer_InitPlayerDefaultValues(xIniFile* ini)
{
    {
        static F32 fbuf[] = { 0.6f, 3.0f, 7.0f, 0.1f, 0.5f, 1.0f };
        xStrParseFloatList((F32*)memcpy(&globals.player.sb.MoveSpeed, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "sb.MoveSpeed", NULL), 6);
    }
    {
        static F32 fbuf[] = { 1.5f, 0.5f, 1.5f };
        xStrParseFloatList((F32*)memcpy(&globals.player.sb.AnimSneak, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "sb.AnimSneak", NULL), 3);
    }
    {
        static F32 fbuf[] = { 3.0f, 0.5f, 2.5f };
        xStrParseFloatList((F32*)memcpy(&globals.player.sb.AnimWalk, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "sb.AnimWalk", NULL), 3);
    }
    {
        static F32 fbuf[] = { 3.0f, 0.5f, 2.5f };
        xStrParseFloatList((F32*)memcpy(&globals.player.sb.AnimRun, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "sb.AnimRun", NULL), 3);
    }

    globals.player.sb.JumpGravity = xIniGetFloat(ini, "sb.JumpGravity", 7.0f);
    globals.player.sb.GravSmooth = xIniGetFloat(ini, "sb.GravSmooth", 0.25f);
    globals.player.sb.FloatSpeed = xIniGetFloat(ini, "sb.FloatSpeed", 1.0f);
    globals.player.sb.ButtsmashSpeed = xIniGetFloat(ini, "sb.ButtsmashSpeed", 5.0f);
    globals.player.sb.ledge.animGrab = xIniGetFloat(ini, "sb.ledge.animGrab", 3.0f);

    zLedgeAdjust(&globals.player.sb.ledge);
    bungee_state::load_settings(*ini);
    oob_state::load_settings(*ini);
    ztalkbox::load_settings(*ini);

    {
        static F32 fbuf[] = { 2.0f, 0.7f, 0.35f, 0.0f };
        xStrParseFloatList((F32*)memcpy(&globals.player.sb.Jump, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "sb.Jump", NULL), 4);
    }
    {
        static F32 fbuf[] = { 1.0f, 0.7f, 0.35f, 0.0f };
        xStrParseFloatList((F32*)memcpy(&globals.player.sb.Double, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "sb.Double", NULL), 4);
    }
    {
        static F32 fbuf[] = { 1.5f, 0.2f, 0.2f, 0.0f };
        xStrParseFloatList((F32*)memcpy(&globals.player.sb.Bounce, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "sb.Bounce", NULL), 4);
    }
    {
        static F32 fbuf[] = { 3.0f, 0.2f, 0.2f, 0.0f };
        xStrParseFloatList((F32*)memcpy(&globals.player.sb.Spring, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "sb.Spring", NULL), 4);
    }
    {
        static F32 fbuf[] = { 0.0f, 0.0f, 0.0f, 0.0f };
        xStrParseFloatList((F32*)memcpy(&globals.player.sb.Wall, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "sb.Wall", NULL), 4);
    }

    globals.player.sb.WallJumpVelocity = xIniGetFloat(ini, "sb.WallJumpVelocity", 0.0f);
    globals.player.sb.spin_damp_xz = xIniGetFloat(ini, "sb.spin_damp_xz", 15.0f);
    globals.player.sb.spin_damp_y = xIniGetFloat(ini, "sb.spin_damp_y", 15.0f);

    globals.player.s = &globals.player.sb;

    CalcJumpImpulse(&globals.player.sb.Jump, NULL);
    CalcJumpImpulse(&globals.player.sb.Double, NULL);
    CalcJumpImpulse(&globals.player.sb.Bounce, NULL);
    CalcJumpImpulse(&globals.player.sb.Spring, NULL);
    CalcJumpImpulse(&globals.player.sb.Wall, NULL);

    {
        static F32 fbuf[] = { 0.6f, 3.0f, 7.0f, 0.1f, 0.5f, 1.0f };
        xStrParseFloatList((F32*)memcpy(&globals.player.patrick.MoveSpeed, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "patrick.MoveSpeed", NULL), 6);
    }
    {
        static F32 fbuf[] = { 1.5f, 0.5f, 1.5f };
        xStrParseFloatList((F32*)memcpy(&globals.player.patrick.AnimSneak, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "patrick.AnimSneak", NULL), 3);
    }
    {
        static F32 fbuf[] = { 3.0f, 0.5f, 2.5f };
        xStrParseFloatList((F32*)memcpy(&globals.player.patrick.AnimWalk, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "patrick.AnimWalk", NULL), 3);
    }
    {
        static F32 fbuf[] = { 3.0f, 0.5f, 2.5f };
        xStrParseFloatList((F32*)memcpy(&globals.player.patrick.AnimRun, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "patrick.AnimRun", NULL), 3);
    }

    globals.player.patrick.JumpGravity = xIniGetFloat(ini, "patrick.JumpGravity", 7.0f);
    globals.player.patrick.GravSmooth = xIniGetFloat(ini, "patrick.GravSmooth", 0.25f);
    globals.player.patrick.FloatSpeed = xIniGetFloat(ini, "patrick.FloatSpeed", 1.0f);
    globals.player.patrick.ButtsmashSpeed = xIniGetFloat(ini, "patrick.ButtsmashSpeed", 5.0f);
    globals.player.patrick.ledge.animGrab = xIniGetFloat(ini, "patrick.ledge.animGrab", 3.0f);

    zLedgeAdjust(&globals.player.patrick.ledge);

    {
        static F32 fbuf[] = { 2.0f, 0.7f, 0.35f, 0.0f };
        xStrParseFloatList((F32*)memcpy(&globals.player.patrick.Jump, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "patrick.Jump", NULL), 4);
    }
    {
        static F32 fbuf[] = { 1.0f, 0.7f, 0.35f, 0.0f };
        xStrParseFloatList((F32*)memcpy(&globals.player.patrick.Double, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "patrick.Double", NULL), 4);
    }
    {
        static F32 fbuf[] = { 1.5f, 0.2f, 0.2f, 0.0f };
        xStrParseFloatList((F32*)memcpy(&globals.player.patrick.Bounce, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "patrick.Bounce", NULL), 4);
    }
    {
        static F32 fbuf[] = { 3.0f, 0.2f, 0.2f, 0.0f };
        xStrParseFloatList((F32*)memcpy(&globals.player.patrick.Spring, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "patrick.Spring", NULL), 4);
    }
    {
        static F32 fbuf[] = { 0.0f, 0.0f, 0.0f, 0.0f };
        xStrParseFloatList((F32*)memcpy(&globals.player.patrick.Wall, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "patrick.Wall", NULL), 4);
    }

    globals.player.s = &globals.player.sb;

    CalcJumpImpulse(&globals.player.patrick.Jump, NULL);
    CalcJumpImpulse(&globals.player.patrick.Double, NULL);
    CalcJumpImpulse(&globals.player.patrick.Bounce, NULL);
    CalcJumpImpulse(&globals.player.patrick.Spring, NULL);
    CalcJumpImpulse(&globals.player.patrick.Wall, NULL);

    globals.player.patrick.WallJumpVelocity = xIniGetFloat(ini, "patrick.WallJumpVelocity", 0.0f);

    {
        static F32 fbuf[] = { 0.6f, 3.0f, 7.0f, 0.1f, 0.5f, 1.0f };
        xStrParseFloatList((F32*)memcpy(&globals.player.spongepat.MoveSpeed, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "spongepat.MoveSpeed", NULL), 6);
    }
    {
        static F32 fbuf[] = { 1.5f, 0.5f, 1.5f };
        xStrParseFloatList((F32*)memcpy(&globals.player.spongepat.AnimSneak, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "spongepat.AnimSneak", NULL), 3);
    }
    {
        static F32 fbuf[] = { 3.0f, 0.5f, 2.5f };
        xStrParseFloatList((F32*)memcpy(&globals.player.spongepat.AnimWalk, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "spongepat.AnimWalk", NULL), 3);
    }
    {
        static F32 fbuf[] = { 3.0f, 0.5f, 2.5f };
        xStrParseFloatList((F32*)memcpy(&globals.player.spongepat.AnimRun, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "spongepat.AnimRun", NULL), 3);
    }

    globals.player.spongepat.JumpGravity = xIniGetFloat(ini, "spongepat.JumpGravity", 7.0f);
    globals.player.spongepat.GravSmooth = xIniGetFloat(ini, "spongepat.GravSmooth", 0.25f);
    globals.player.spongepat.FloatSpeed = xIniGetFloat(ini, "spongepat.FloatSpeed", 1.0f);
    globals.player.spongepat.ButtsmashSpeed = xIniGetFloat(ini, "spongepat.ButtsmashSpeed", 5.0f);
    globals.player.spongepat.ledge.animGrab = xIniGetFloat(ini, "spongepat.ledge.animGrab", 3.0f);

    zLedgeAdjust(&globals.player.spongepat.ledge);

    {
        static F32 fbuf[] = { 2.0f, 0.7f, 0.35f, 0.0f };
        xStrParseFloatList((F32*)memcpy(&globals.player.spongepat.Jump, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "spongepat.Jump", NULL), 4);
    }
    {
        static F32 fbuf[] = { 1.0f, 0.7f, 0.35f, 0.0f };
        xStrParseFloatList((F32*)memcpy(&globals.player.spongepat.Double, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "spongepat.Double", NULL), 4);
    }
    {
        static F32 fbuf[] = { 1.5f, 0.2f, 0.2f, 0.0f };
        xStrParseFloatList((F32*)memcpy(&globals.player.spongepat.Bounce, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "spongepat.Bounce", NULL), 4);
    }
    {
        static F32 fbuf[] = { 3.0f, 0.2f, 0.2f, 0.0f };
        xStrParseFloatList((F32*)memcpy(&globals.player.spongepat.Spring, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "spongepat.Spring", NULL), 4);
    }
    {
        static F32 fbuf[] = { 0.0f, 0.0f, 0.0f, 0.0f };
        xStrParseFloatList((F32*)memcpy(&globals.player.spongepat.Wall, fbuf, sizeof(fbuf)),
                           xIniGetString(ini, "spongepat.Wall", NULL), 4);
    }

    globals.player.s = &globals.player.sb;

    CalcJumpImpulse(&globals.player.spongepat.Jump, NULL);
    CalcJumpImpulse(&globals.player.spongepat.Double, NULL);
    CalcJumpImpulse(&globals.player.spongepat.Bounce, NULL);
    CalcJumpImpulse(&globals.player.spongepat.Spring, NULL);
    CalcJumpImpulse(&globals.player.spongepat.Wall, NULL);

    globals.player.spongepat.WallJumpVelocity =
        xIniGetFloat(ini, "spongepat.WallJumpVelocity", 0.0f);
}

// Placeholder for dead-stripped code (probably a player-type name lookup; name and body unknown)
// whose strings remain, unreferenced, in retail's string pool before "sb.MoveSpeed".
static const char* __deadstripped_zEntPlayer()
{
    return "SPONGEBALL\0"
           "SPONGEBOB\0"
           "PATRICK\0"
           "CAR\0"
           "DUO\0"
           "UNKNOWN PLAYER";
}

// The '? 1 : 0' index form makes inlined callers (zEntPlayer_MinimalUpdate, zEntPlayer_SceneExit)
// address the bubblebowl array with lwzx as retail does; the out-of-line code is the same.
xEntBoulder* zEntPlayerGetBBowl()
{
    return globals.player.bubblebowl[globals.player.g.PowerUp[2] >= 2 ? 1 : 0];
}

S32 zEntPlayerIsInSlidingMode()
{
    return (globals.player.SlideTrackSliding & 1) || tslide_ground;
}

F32 zEntPlayerGetBowlBlastRadius()
{
    return sBowlBlastRadius;
}
