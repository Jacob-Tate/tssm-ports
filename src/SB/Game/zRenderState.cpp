#include "zRenderState.h"

#include "iCamera.h"

#include <rwplcore.h>

_SDRenderState sRS;
static U32 sRSBits = 0xFFFFFFFF;

// No GC symbol: inlined into zSceneInit.
inline void zRenderStateInit()
{
    sRS = SDRS_Unknown;
    sRSBits = 0xFFFFFFFF;
}

_SDRenderState zRenderStateCurrent()
{
    return sRS;
}

void zRenderStateBegin()
{
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSWRAP);
    iCameraSetFogRenderStates();
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*)rwSHADEMODEGOURAUD);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);

    sRS = SDRS_Default;
    sRSBits = 0;

    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEARMIPLINEAR);
}

void zRenderState(_SDRenderState next)
{
    U32 customBits;
    U32 defBits;

    if (next == sRS)
    {
        return;
    }

    zRenderStateBegin();

    customBits = 0;
    sRS = next;

    switch (next)
    {
    case SDRS_UI:
        customBits = 0x30C;
        RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
        break;
    case SDRS_Tide:
    case SDRS_Environment:
        customBits = 0x10;
        RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
        break;
    case SDRS_Particles:
        customBits = 0x184;
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        iCameraSetFog(false);
        RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*)rwSHADEMODEFLAT);
        break;
    case SDRS_Lightning:
        customBits = 0x140;
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
        break;
    case SDRS_SkidMark:
    case SDRS_Streak:
        customBits = 0x100;
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        break;
    case SDRS_Glare:
        customBits = 0x344;
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
        iCameraSetFog(false);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
        break;
    case SDRS_NPCVisual:
        customBits = 0x108;
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
        break;
    case SDRS_NMEHazardAlpha:
        customBits = 0x178;
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
        break;
    case SDRS_NPCBossMeter:
        customBits = 0x36C;
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        iCameraSetFog(false);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
        break;
    case SDRS_NPCMatter:
        customBits = 0x8;
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
        break;
    case SDRS_Font:
        customBits = 0x36C;
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
        iCameraSetFog(false);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
        break;
    case SDRS_HUD:
        customBits = 0x4;
        iCameraSetFog(false);
        break;
    case SDRS_Bubble:
        customBits = 0x14;
        iCameraSetFog(false);
        RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
        break;
    case SDRS_SkyBack:
        customBits = 0x304;
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
        iCameraSetFog(false);
        break;
    case SDRS_Fill:
        customBits = 0x3EC;
        iCameraSetFog(false);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
        RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*)rwSHADEMODEFLAT);
        break;
    case SDRS_OOBFade:
        customBits = 0x284;
        iCameraSetFog(false);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*)rwSHADEMODEFLAT);
        break;
    case SDRS_OOBPlayerZ:
        customBits = 0xE8;
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*)rwSHADEMODEFLAT);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
        break;
    case SDRS_OOBPlayerAlpha:
        customBits = 0x48;
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
        break;
    case SDRS_OOBHand:
        customBits = 0x36C;
        iCameraSetFog(false);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
        break;
    case SDRS_Newsfish:
        customBits = 0x26C;
        iCameraSetFog(false);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
        break;
    case SDRS_CruiseHUD:
        customBits = 0x304;
        iCameraSetFog(false);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
        break;
    case SDRS_DiscoFloorGlow:
        customBits = 0x144;
        iCameraSetFog(false);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
        break;
    case SDRS_FireCards:
        customBits = 0x164;
        iCameraSetFog(false);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
        break;
    case SDRS_SplashCones:
        customBits = 0x14C;
        iCameraSetFog(false);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
        break;
    case SDRS_Water:
        customBits = 0xC;
        iCameraSetFog(false);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
        break;
    case SDRS_Grass:
        customBits = 0x6;
        RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
        iCameraSetFog(false);
        break;
    case SDRS_WaterSkyDome:
        customBits = 0x36C;
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
        iCameraSetFog(false);
        break;
    case SDRS_LaserBeam:
        customBits = 0x164;
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
        iCameraSetFog(false);
        break;
    case SDRS_Spotlight:
        customBits = 0x166;
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
        iCameraSetFog(false);
        break;
    case SDRS_LightVolume:
        customBits = 0x164;
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
        iCameraSetFog(false);
        break;
    case SDRS_Ribbon:
        customBits = 0x104;
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        iCameraSetFog(false);
        break;
    case SDRS_ScanLaser:
        customBits = 0x174;
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
        iCameraSetFog(false);
        break;
    case SDRS_HDRBeginRender:
        customBits = 0x104;
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        iCameraSetFog(false);
        break;
    case SDRS_BlurAlpha:
        customBits = 0x64;
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
        iCameraSetFog(false);
        break;
    case SDRS_ZParPTank:
        customBits = 0x140;
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
        break;
    case SDRS_XPTankPool:
        customBits = 0x104;
        RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        break;
    case SDRS_IncrediBall:
        customBits = 0x6;
        RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
        break;
    case SDRS_Frozone:
        customBits = 0x4;
        RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
        break;
    case SDRS_SmashRing:
        customBits = 0x104;
        RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        break;
    case SDRS_LaserBolt:
        customBits = 0x104;
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        iCameraSetFog(false);
        break;
    case SDRS_Decal:
        customBits = 0x3FE;
        RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
        RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*)rwSHADEMODEGOURAUD);
        break;
    case SDRS_PatStunRings:
        customBits = 0x37C;
        RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
        break;
    case SDRS_Unknown:
    case SDRS_Default:
    case SDRS_OpaqueModels:
    case SDRS_AlphaModels:
    case SDRS_Projectile:
    case SDRS_NMEHazardOpaque:
        break;
    }

    defBits = sRSBits & ~customBits;
    if (defBits)
    {
        if (defBits & 0x1)
        {
            RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
        }
        if (defBits & 0x2)
        {
            RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSWRAP);
        }
        if (defBits & 0x4)
        {
            iCameraSetFog(true);
        }
        if (defBits & 0x8)
        {
            RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
        }
        if (defBits & 0x10)
        {
            RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
        }
        if (defBits & 0x20)
        {
            RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        }
        if (defBits & 0x40)
        {
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
        }
        if (defBits & 0x80)
        {
            RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*)rwSHADEMODEGOURAUD);
        }
        if (defBits & 0x100)
        {
            RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
        }
        if (defBits & 0x200)
        {
            RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
        }
    }

    sRSBits = customBits;
}
