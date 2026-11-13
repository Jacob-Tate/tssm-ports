#include "zNPCTypes.h"

#include "xFactory.h"
#include "zNPC.h"

#include "zWadNME.h"
#include "zNMETypeTiki.h"
#include "zNMETypeCritJelly.h"
#include "zNMETypeTurret.h"
#include "zNMETypeTurBarrel.h"
#include "zNMETypeTurPopup.h"
#include "zNMETypeTurTurner.h"
#include "zNMETypeTurSpiral.h"
#include "zNMETypeStandard.h"
#include "zNMETypeStdFogger.h"
#include "zNMETypeStdSlammer.h"
#include "zNMETypeStdFlinger.h"
#include "zNMETypeStdSpinner.h"
#include "zNMETypeStdPopper.h"
#include "zNMETypeStdZapper.h"
#include "zNMETypeStdMervyn.h"
#include "zNMETypeBuckOTron.h"
#include "zNMETypeBossFrogfish.h"
#include "zNMETypeBossDenToo.h"
#include "zNMETypeBossNeptune.h"
#include "zNMETypeTongueSpin.h"
#include "zNMETypeFrdMindy.h"

// Placeholder for dead-stripped data, not recovered code: retail's .rodata keeps two unreferenced
// all-zero 0x10 templates here, probably the locals of an uncalled header inline (the same pair
// recurs in other units). The body is unknown.
static void __deadstripped_zNPCTypes()
{
    F32 a[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    F32 b[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
}

void zNPCTypes_ShutdownTypes()
{
}

// No symbol in the target: always inlined (into zNPCMgr::Startup).
inline void zNPCTypes_RegisterTypes(xFactory* fac)
{
    XGOFTypeInfo typelist[] = {
        { NME_TYPE_COMMON, zNPC::create_npc<zNMECommon>, zNPC::destroy_npc },
        { NME_TYPE_TIKI, zNPC::create_npc<zNMETiki>, zNPC::destroy_npc },
        { NME_TYPE_TIKI_WOOD, zNPC::create_npc<zNMETiki>, zNPC::destroy_npc },
        { NME_TYPE_TIKI_LOVEY, zNPC::create_npc<zNMETiki>, zNPC::destroy_npc },
        { NME_TYPE_TIKI_QUIET, zNPC::create_npc<zNMETiki>, zNPC::destroy_npc },
        { NME_TYPE_TIKI_THUNDER, zNPC::create_npc<zNMETiki>, zNPC::destroy_npc },
        { NME_TYPE_TIKI_STONE, zNPC::create_npc<zNMETiki>, zNPC::destroy_npc },
        { NME_TYPE_CRITTER, zNPC::create_npc<zNMECritter>, zNPC::destroy_npc },
        { NME_TYPE_CRITBASIC, zNPC::create_npc<zNMECritBasic>, zNPC::destroy_npc },
        { NME_TYPE_CRITJELLY, zNPC::create_npc<zNMECritJelly>, zNPC::destroy_npc },
        { NME_TYPE_BUCKETJELLY, zNPC::create_npc<zNMECritJelly>, zNPC::destroy_npc },
        { NME_TYPE_TURRET, zNPC::create_npc<zNMETurret>, zNPC::destroy_npc },
        { NME_TYPE_TURBARREL_V1, zNPC::create_npc<zNMETurBarrel>, zNPC::destroy_npc },
        { NME_TYPE_TURBARREL_V2, zNPC::create_npc<zNMETurBarrel>, zNPC::destroy_npc },
        { NME_TYPE_TURBARREL_V3, zNPC::create_npc<zNMETurBarrel>, zNPC::destroy_npc },
        { NME_TYPE_TURPOPUP, zNPC::create_npc<zNMETurPopup>, zNPC::destroy_npc },
        { NME_TYPE_TURTURNER, zNPC::create_npc<zNMETurTurner>, zNPC::destroy_npc },
        { NME_TYPE_TURTRACE, zNPC::create_npc<zNMETurTurner>, zNPC::destroy_npc },
        { NME_TYPE_TURARTY, zNPC::create_npc<zNMETurTurner>, zNPC::destroy_npc },
        { NME_TYPE_TURSPIRAL, zNPC::create_npc<zNMETurSpiral>, zNPC::destroy_npc },
        { NME_TYPE_STANDARD, zNPC::create_npc<zNMEStandard>, zNPC::destroy_npc },
        { NME_TYPE_FOGGER_V1, zNPC::create_npc<zNMEFogger>, zNPC::destroy_npc },
        { NME_TYPE_SLAMMER_V1, zNPC::create_npc<zNMESlammer>, zNPC::destroy_npc },
        { NME_TYPE_FLINGER_V1, zNPC::create_npc<zNMEFlinger>, zNPC::destroy_npc },
        { NME_TYPE_SPINNER_V1, zNPC::create_npc<zNMESpinner>, zNPC::destroy_npc },
        { NME_TYPE_POPPER_V1, zNPC::create_npc<zNMEPopper>, zNPC::destroy_npc },
        { NME_TYPE_ZAPPER_V1, zNPC::create_npc<zNMEZapper>, zNPC::destroy_npc },
        { NME_TYPE_MERVYN_V1, zNPC::create_npc<zNMEMervyn>, zNPC::destroy_npc },
        { NME_TYPE_FOGGER_V2, zNPC::create_npc<zNMEFogger>, zNPC::destroy_npc },
        { NME_TYPE_SLAMMER_V2, zNPC::create_npc<zNMESlammer>, zNPC::destroy_npc },
        { NME_TYPE_FLINGER_V2, zNPC::create_npc<zNMEFlinger>, zNPC::destroy_npc },
        { NME_TYPE_SPINNER_V2, zNPC::create_npc<zNMESpinner>, zNPC::destroy_npc },
        { NME_TYPE_POPPER_V2, zNPC::create_npc<zNMEPopper>, zNPC::destroy_npc },
        { NME_TYPE_ZAPPER_V2, zNPC::create_npc<zNMEZapper>, zNPC::destroy_npc },
        { NME_TYPE_MERVYN_V2, zNPC::create_npc<zNMEMervyn>, zNPC::destroy_npc },
        { NME_TYPE_FOGGER_V3, zNPC::create_npc<zNMEFogger>, zNPC::destroy_npc },
        { NME_TYPE_SLAMMER_V3, zNPC::create_npc<zNMESlammer>, zNPC::destroy_npc },
        { NME_TYPE_FLINGER_V3, zNPC::create_npc<zNMEFlinger>, zNPC::destroy_npc },
        { NME_TYPE_SPINNER_V3, zNPC::create_npc<zNMESpinner>, zNPC::destroy_npc },
        { NME_TYPE_POPPER_V3, zNPC::create_npc<zNMEPopper>, zNPC::destroy_npc },
        { NME_TYPE_ZAPPER_V3, zNPC::create_npc<zNMEZapper>, zNPC::destroy_npc },
        { NME_TYPE_MERVYN_V3, zNPC::create_npc<zNMEMervyn>, zNPC::destroy_npc },
        { NME_TYPE_BUCKOTRON_V1, zNPC::create_npc<zNMEBuckotron>, zNPC::destroy_npc },
        { NME_TYPE_BUCKOTRON_V2, zNPC::create_npc<zNMEBuckotron>, zNPC::destroy_npc },
        { NME_TYPE_BUCKOTRON_V3, zNPC::create_npc<zNMEBuckotron>, zNPC::destroy_npc },
        { NME_TYPE_BUCKOTRON_V4, zNPC::create_npc<zNMEBuckotron>, zNPC::destroy_npc },
        { NME_TYPE_BUCKOTRON_V5, zNPC::create_npc<zNMEBuckotron>, zNPC::destroy_npc },
        { NME_TYPE_BUCKOTRON_V6, zNPC::create_npc<zNMEBuckotron>, zNPC::destroy_npc },
        { NME_TYPE_BUCKOTRON_V7, zNPC::create_npc<zNMEBuckotron>, zNPC::destroy_npc },
        { NME_TYPE_FROGFISH, zNPC::create_npc<zNMEFrogfish>, zNPC::destroy_npc },
        { NME_TYPE_DENNIS_V1, zNPC::create_npc<zNMEDennis>, zNPC::destroy_npc },
        { NME_TYPE_DENNIS_V2, zNPC::create_npc<zNMEDenToo>, zNPC::destroy_npc },
        { NME_TYPE_NEPTUNE, zNPC::create_npc<zNMENeptune>, zNPC::destroy_npc },
        { NME_TYPE_SBBAT, zNPC::create_npc<zNMESBBat>, zNPC::destroy_npc },
        { NME_TYPE_TONGUESPIN, zNPC::create_npc<zNMETongueSpin>, zNPC::destroy_npc },
        { NME_TYPE_MINDY, zNPC::create_npc<zNMEMindy>, zNPC::destroy_npc },
        { NME_TYPE_SPONGEBOB, zNPC::create_npc<zNMEMindy>, zNPC::destroy_npc },
        { NME_TYPE_PATRICK, zNPC::create_npc<zNMEMindy>, zNPC::destroy_npc },
        { NPC_TYPE_UNKNOWN, NULL, NULL },
    };
    fac->RegItemType(typelist);
}

// No symbol, but its table is a local (non-weak) static, so the helper is static: plain 'inline'
// would make the table a weak object.
static inline void zNPCTypes_ForEachType(void (*zNPCTypeInfo::*func)())
{
    static const zNPCTypeInfo info[] = {
        { NME_TYPE_COMMON },       { NME_TYPE_TIKI },
        { NME_TYPE_TIKI_WOOD },    { NME_TYPE_TIKI_LOVEY },
        { NME_TYPE_TIKI_QUIET },   { NME_TYPE_TIKI_THUNDER },
        { NME_TYPE_TIKI_STONE },   { NME_TYPE_CRITTER },
        { NME_TYPE_CRITBASIC },    { NME_TYPE_CRITJELLY },
        { NME_TYPE_TURRET },       { NME_TYPE_TURBARREL_V1 },
        { NME_TYPE_TURBARREL_V2 }, { NME_TYPE_TURBARREL_V3 },
        { NME_TYPE_TURPOPUP },     { NME_TYPE_TURTURNER },
        { NME_TYPE_TURTRACE },     { NME_TYPE_TURARTY },
        { NME_TYPE_TURSPIRAL },    { NME_TYPE_STANDARD },
        { NME_TYPE_FOGGER_V1 },    { NME_TYPE_SLAMMER_V1 },
        { NME_TYPE_FLINGER_V1 },   { NME_TYPE_SPINNER_V1 },
        { NME_TYPE_POPPER_V1 },    { NME_TYPE_ZAPPER_V1 },
        { NME_TYPE_MERVYN_V1 },    { NME_TYPE_FOGGER_V2 },
        { NME_TYPE_SLAMMER_V2 },   { NME_TYPE_FLINGER_V2 },
        { NME_TYPE_SPINNER_V2 },   { NME_TYPE_POPPER_V2 },
        { NME_TYPE_ZAPPER_V2 },    { NME_TYPE_MERVYN_V2 },
        { NME_TYPE_FOGGER_V3 },    { NME_TYPE_SLAMMER_V3 },
        { NME_TYPE_FLINGER_V3 },   { NME_TYPE_SPINNER_V3 },
        { NME_TYPE_POPPER_V3 },    { NME_TYPE_ZAPPER_V3 },
        { NME_TYPE_MERVYN_V3 },    { NME_TYPE_BUCKOTRON_V1 },
        { NME_TYPE_BUCKOTRON_V2 }, { NME_TYPE_BUCKOTRON_V3 },
        { NME_TYPE_BUCKOTRON_V4 }, { NME_TYPE_BUCKOTRON_V5 },
        { NME_TYPE_BUCKOTRON_V6 }, { NME_TYPE_BUCKOTRON_V7 },
        { NME_TYPE_DENNIS_V1 },    { NME_TYPE_DENNIS_V2 },
        { NME_TYPE_FROGFISH },     { NME_TYPE_NEPTUNE },
        { NME_TYPE_MINDY },        { NME_TYPE_SPONGEBOB },
        { NME_TYPE_PATRICK },      { 0 },
    };

    for (const zNPCTypeInfo* type = info; type->id != 0; type++)
    {
        if (type->*func != NULL)
        {
            (type->*func)();
        }
    }
}
