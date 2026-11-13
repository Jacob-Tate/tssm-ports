#include "xAntenna.h"

#include "xCam.h"
#include "xEnt.h"
#include "xMath3.h"
#include "xModel.h"
#include "xRand.h"
#include "xVec3Inlines.h"
#include "xstransvc.h"
#include "zCutsceneMgr.h"
#include "zGlobals.h"
#include "zNMECommon.h"
#include "zNMEParFX.h"
#include "zUICustom.h"

#include <string.h>
#include "zAutoTweak.h"

struct xAntenna
{
    xEnt* pParent;
    S32 startBone;
    S32 endBone;
    F32 length;
    F32 rigidity;
    xVec3 tipCurrentPosition;
    xVec3 tipCurrentVelocity;
    F32 accumDist;
    F32 distToCorrect;
    xVec3 tipCorrection;
    F32 timer;
    U8 noLight;
    xVec3 lastEndBonePos;
};

namespace
{
    xAntenna sAntennaList[96];
    RwTexture* sAntennaTex;

    SparkyCurve sSparksCurveDataRed[2] = {
        { 0.0f, { 0xFF, 0x00, 0x00, 0x7F }, 1.0f },
        { 1.0f, { 0xFF, 0x00, 0x00, 0x7F }, 0.0f },
    };

    SparkyCurve sSparksCurveDataGreen[2] = {
        { 0.0f, { 0x00, 0xFF, 0x00, 0x7F }, 1.0f },
        { 1.0f, { 0x00, 0xFF, 0x00, 0x7F }, 0.0f },
    };

    // The retail data is green as well.
    SparkyCurve sSparksCurveDataYellow[2] = {
        { 0.0f, { 0x00, 0xFF, 0x00, 0x7F }, 1.0f },
        { 1.0f, { 0x00, 0xFF, 0x00, 0x7F }, 0.0f },
    };

    const SparkyEmitter sAntennaSparksRed = {
        NPAR_TYP_FOUNTAIN,
        { 0, 0, { 0, 0 } },
        { 16.0f, 0.0f },
        { 0.02f, 0.25f },
        { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
        { 0.15f, 0.2f },
        { 10, 10, 0.1f, -0.1f },
        { 2.0f, -4.0f, 3.0f, 0.5f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
        { sSparksCurveDataRed, 2, 1.0f, 2.0f },
    };

    SparkyEmitter sAntennaSparksGreen;
    SparkyEmitter sAntennaSparksYellow;

    // Blink period of an enemy's antenna light from its health; non-enemies never blink. No symbol,
    // name invented; signature and body from the TSSM Xbox build, which keeps it out of line.
    inline F32 AntennaBlinkTime(xAntenna* pAntenna)
    {
        if (pAntenna->pParent->baseType != eBaseTypeNPC)
        {
            return -1.0f;
        }

        F32 health = ((zNMECommon*)pAntenna->pParent)->HealthRatio();
        if (health < 0.5f)
        {
            return 0.25f;
        }
        if (health < 0.75f)
        {
            return 0.5f;
        }
        return 0.7f;
    }

    // Rotation turning 'from' toward 'to' by 'scale' times the angle between them (at most
    // maxAngle); returns true when the two are (nearly) parallel and no rotation was built.
    // No symbol, name invented; signature and body from the TSSM Xbox build.
    inline bool AntennaBendRotation(xMat3x3* m, const xVec3* from, const xVec3* to, F32 maxAngle,
                                    F32 scale)
    {
        xVec3 axis;
        F32 d = xVec3Dot(from, to);

        if (d >= 0.9999f)
        {
            return true;
        }

        xVec3Cross(&axis, from, to);
        if (xVec3Normalize(&axis, &axis) <= 0.995f)
        {
            return true;
        }

        F32 angle = xacos(d);
        angle *= scale;
        if (angle > maxAngle)
        {
            angle = maxAngle;
        }

        xMat3x3RotC(m, axis.x, axis.y, axis.z, angle);
        return false;
    }

    // Locals are declared at first use, as in the PS2 DWARF. pMat reads Mat directly and the
    // lengths use xVec3Length(&v); xModelGetFrame and v.length() change the register allocation.
    void UpdateOneAntenna(xAntenna* pAntenna, F32 dt)
    {
        xVec3 antennaBaseRelative;
        xVec3 antennaBase;
        xModelInstance* pModel = pAntenna->pParent->model;

        if (pAntenna->startBone > -1)
        {
            RwMatrixTag* pBoneMatStart = &pModel->Mat[pAntenna->startBone];
            antennaBaseRelative = *(xVec3*)&pBoneMatStart->pos;
        }
        else
        {
            antennaBaseRelative.assign(1.0f, 1.0f, 1.0f);
        }

        xMat4x3* pMat = (xMat4x3*)pAntenna->pParent->model->Mat;
        xMat4x3Toworld(&antennaBase, pMat, &antennaBaseRelative);

        RwMatrixTag* pBoneMatEnd = &pModel->Mat[pAntenna->startBone + 1];
        if (pAntenna->lastEndBonePos == *(xVec3*)&pBoneMatEnd->pos)
        {
            return;
        }

        if (pAntenna->accumDist > pAntenna->distToCorrect)
        {
            xVec3 tipIdealPosition;

            if (pAntenna->startBone > -1)
            {
                tipIdealPosition = *(xVec3*)&pBoneMatEnd->pos;
            }
            else
            {
                xVec3 antennaDelta;
                antennaDelta.assign(0.0f, pAntenna->length, 0.0f);
                tipIdealPosition = antennaBase + antennaDelta;
            }

            if (pAntenna->startBone > -1)
            {
                xVec3 delta = tipIdealPosition - antennaBaseRelative;
                xMat4x3Toworld(&tipIdealPosition, pMat, &tipIdealPosition);
                if (pAntenna->length < 0.0f)
                {
                    pAntenna->tipCurrentPosition = tipIdealPosition;
                }
                pAntenna->length = xVec3Length(&delta);
            }
            else
            {
                xMat4x3Toworld(&tipIdealPosition, pMat, &tipIdealPosition);
            }

            pAntenna->tipCorrection = tipIdealPosition - (pAntenna->tipCurrentPosition +
                                                          pAntenna->tipCurrentVelocity * 0.95f);
            pAntenna->accumDist = 0.0f;
            pAntenna->distToCorrect = xVec3Length(&pAntenna->tipCorrection);
        }

        pAntenna->tipCurrentVelocity =
            pAntenna->tipCurrentVelocity * 0.95f + pAntenna->tipCorrection * 0.01f;

        xVec3 oldPosition = pAntenna->tipCurrentPosition;
        pAntenna->tipCurrentPosition = pAntenna->tipCurrentPosition + pAntenna->tipCurrentVelocity;

        xVec3 tipVec = pAntenna->tipCurrentPosition - antennaBase;
        xVec3Normalize(&tipVec, &tipVec);
        tipVec *= pAntenna->length;
        pAntenna->tipCurrentPosition = antennaBase + tipVec;

        xVec3 distMoved = pAntenna->tipCurrentPosition - oldPosition;
        pAntenna->accumDist += xVec3Length(&distMoved);

        xVec3 tipPositionRelative;
        xMat4x3Tolocal(&tipPositionRelative, pMat, &pAntenna->tipCurrentPosition);

        xMat4x3 oldTipMtx = *(xMat4x3*)pBoneMatEnd;
        pBoneMatEnd->pos.x = tipPositionRelative.x;
        pBoneMatEnd->pos.y = tipPositionRelative.y;
        pBoneMatEnd->pos.z = tipPositionRelative.z;

        xVec3 initialPosition;
        initialPosition.assign(0.0f, 1.0f, 0.0f);
        xVec3 tipDirection = tipPositionRelative - antennaBaseRelative;
        xVec3Normalize(&tipDirection, &tipDirection);

        xMat3x3 startRotation;
        if (AntennaBendRotation(&startRotation, &initialPosition, &tipDirection, 6.2831855f, 0.75f))
        {
            xMat3x3Identity(&startRotation);
        }
        xMat4x3* startBone = (xMat4x3*)&pModel->Mat[pAntenna->startBone];
        xMat3x3Mul(startBone, startBone, &startRotation);

        xMat3x3 endRotation;
        if (AntennaBendRotation(&endRotation, &initialPosition, &tipDirection, 6.2831855f, 1.25f))
        {
            xMat3x3Identity(&endRotation);
        }
        xMat4x3* endBone = (xMat4x3*)&pModel->Mat[pAntenna->startBone + 1];
        xMat3x3Mul(endBone, endBone, &endRotation);

        if (pAntenna->endBone > pAntenna->startBone + 1)
        {
            RwMatrixTag* pBoneMat = &pModel->Mat[pAntenna->startBone + 2];
            xVec3 oldBonePos = *(xVec3*)&pBoneMat->pos;
            xVec3 bonePos;
            xMat4x3Tolocal(&bonePos, &oldTipMtx, &oldBonePos);
            xMat4x3Toworld(&bonePos, endBone, &bonePos);
            xVec3 boneDelta = bonePos - oldBonePos;

            for (S32 iBone = pAntenna->startBone + 2; iBone <= pAntenna->endBone; iBone++)
            {
                xVec3* pBonePos = (xVec3*)&pModel->Mat[iBone].pos;
                *pBonePos += boneDelta;
            }
        }

        if (zUICustomExtrasManager::IsCheatEnabled(eExtrasScreenCheats_LongAntenna))
        {
            endBone->pos = (endBone->pos - startBone->pos) * 2.0f + startBone->pos;

            if (pAntenna->endBone > pAntenna->startBone + 1)
            {
                for (S32 iBone = pAntenna->startBone + 2; iBone <= pAntenna->endBone; iBone++)
                {
                    xVec3* pBonePos = (xVec3*)&pModel->Mat[iBone].pos;
                    *pBonePos = (*pBonePos - startBone->pos) * 2.0f + startBone->pos;
                }
            }
        }

        pAntenna->lastEndBonePos = *(xVec3*)&pBoneMatEnd->pos;

        if (pAntenna->pParent->baseType == eBaseTypeNPC)
        {
            pAntenna->timer -= dt;
            if (pAntenna->timer < 0.0f)
            {
                pAntenna->timer = 2.0f * AntennaBlinkTime(pAntenna);
            }
        }
    }

    // h is the camera's right axis * 0.5 and w its up axis * 0.4; the corners are pos -/+ w -/+ h.
    // The vertex code, including its 'vert' pointer, is the same as xScrFXGlareRender's.
    void RenderOneAntenna(xAntenna* pAntenna)
    {
        xVec3 w;
        xVec3 h;
        xVec3 pos;
        RwRGBA color;
        static RxObjSpace3DVertex sStripVert[4];

        if (!pAntenna->noLight)
        {
            if (pAntenna->pParent->baseType != eBaseTypeNPC)
            {
                return;
            }

            xModelGetBoneLocationScaled(pos, *pAntenna->pParent->model, pAntenna->endBone);

            xMat4x3 antennaMat = xglobals->cam->mat;
            h = antennaMat.right;
            w = antennaMat.up;
            h *= 0.5f;
            w *= 0.4f;

            en_nmesimp status = ((zNMECommon*)pAntenna->pParent)->SimpleStatus();
            if (status == NME_SIMP_BIGBULLY)
            {
                color.red = 255;
                color.green = 0;
                color.blue = 0;
            }
            else if (status == NME_SIMP_WARYDETECT || status == NME_SIMP_WARY ||
                     status == NME_SIMP_WORRIED)
            {
                color.red = 255;
                color.green = 255;
                color.blue = 0;
            }
            else
            {
                color.red = 0;
                color.green = 200;
                color.blue = 0;
            }

            xVec3 toCam = xglobals->cam->mat.pos - pos;
            toCam.normalize();
            toCam *= 0.5f;
            pos += toCam;

            RxObjSpace3DVertex* vert = &sStripVert[0];
            RwIm3DVertexSetPos(vert, pos.x - w.x - h.x, pos.y - w.y - h.y, pos.z - w.z - h.z);
            RwIm3DVertexSetUV(vert, 0.0f, 0.0f);
            RwIm3DVertexSetRGBA(vert, color.red, color.green, color.blue, 255);
            vert++;

            RwIm3DVertexSetPos(vert, pos.x - w.x + h.x, pos.y - w.y + h.y, pos.z - w.z + h.z);
            RwIm3DVertexSetUV(vert, 0.0f, 1.0f);
            RwIm3DVertexSetRGBA(vert, color.red, color.green, color.blue, 255);
            vert++;

            RwIm3DVertexSetPos(vert, pos.x + w.x - h.x, pos.y + w.y - h.y, pos.z + w.z - h.z);
            RwIm3DVertexSetUV(vert, 1.0f, 0.0f);
            RwIm3DVertexSetRGBA(vert, color.red, color.green, color.blue, 255);
            vert++;

            RwIm3DVertexSetPos(vert, pos.x + w.x + h.x, pos.y + w.y + h.y, pos.z + w.z + h.z);
            RwIm3DVertexSetUV(vert, 1.0f, 1.0f);
            RwIm3DVertexSetRGBA(vert, color.red, color.green, color.blue, 255);

            if (RwIm3DTransform(sStripVert, 4, NULL,
                                rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
            {
                RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
                RwIm3DEnd();
            }
        }
    }

    // Always inlined into xAntennaRender (no GC symbol).
    inline void RenderOneAntennaSparks(xAntenna* pAntenna)
    {
        xMat4x3 antennaMat;
        xVec3 pos_emit;
        xVec3 dir_emit;
        en_nmesimp status;

        if (pAntenna->noLight)
        {
            return;
        }

        xMat4x3Mul(&antennaMat, (xMat4x3*)&pAntenna->pParent->model->Mat[pAntenna->startBone + 1],
                   (xMat4x3*)pAntenna->pParent->model->Mat);
        pos_emit = antennaMat.pos;
        dir_emit = antennaMat.up;

        if (pAntenna->pParent->baseType == eBaseTypeNPC)
        {
            status = ((zNMECommon*)pAntenna->pParent)->SimpleStatus();
            if (status == NME_SIMP_BIGBULLY)
            {
                sAntennaSparksRed.EmitOnAxis(&pos_emit, &dir_emit, 1.0f);
            }
            else if (status == NME_SIMP_WARYDETECT || status == NME_SIMP_WARY ||
                     status == NME_SIMP_WORRIED)
            {
                sAntennaSparksYellow.EmitOnAxis(&pos_emit, &dir_emit, 1.0f);
            }
            else
            {
                sAntennaSparksGreen.EmitOnAxis(&pos_emit, &dir_emit, 1.0f);
            }
        }
        else
        {
            sAntennaSparksRed.EmitOnAxis(&pos_emit, &dir_emit, 1.0f);
        }
    }
} // namespace

void xAntennaInit()
{
    xAntennaRemoveAll();

    sAntennaSparksGreen = sAntennaSparksRed;
    sAntennaSparksGreen.curve.curveData = sSparksCurveDataGreen;
    sAntennaSparksGreen.curve.curveSize = 2;

    sAntennaSparksYellow = sAntennaSparksRed;
    sAntennaSparksYellow.curve.curveData = sSparksCurveDataYellow;
    sAntennaSparksYellow.curve.curveSize = 2;
}

void xAntennaSceneInit()
{
    sAntennaTex = (RwTexture*)xSTFindAsset(0x446A0B76, NULL);
    zNMEParFX_PartyInvite(NPAR_TYP_FOUNTAIN, NULL, 0);
}

void xAntennaScenePostInit()
{
}

void xAntennaRender()
{
    if (sAntennaTex == NULL)
    {
        return;
    }

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, sAntennaTex->raster);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);

    for (S32 i = 0; i < 96; i++)
    {
        zNMECommon* nme;

        if (sAntennaList[i].pParent != NULL && sAntennaList[i].pParent->baseType == eBaseTypeNPC)
        {
            nme = (zNMECommon*)sAntennaList[i].pParent;
            if (xEntIsVisible(nme) && (nme->baseFlags & 0x1) && !(nme->baseFlags & 0x40) &&
                (nme->model == NULL || !(nme->model->Flags & 0x400)) &&
                nme->runtimeData.flg_activeOn &&
                (globals.cmgr == NULL || globals.cmgr->csn == NULL || (nme->flags & 0x80)))
            {
                if (sAntennaList[i].timer < AntennaBlinkTime(&sAntennaList[i]))
                {
                    RenderOneAntenna(&sAntennaList[i]);
                }
            }
        }
    }

    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);

    if (!zUICustomExtrasManager::IsCheatEnabled(eExtrasScreenCheats_AntennaFountain))
    {
        return;
    }

    for (S32 i = 0; i < 96; i++)
    {
        zNMECommon* nme;

        if (sAntennaList[i].pParent == NULL)
        {
            continue;
        }

        if (sAntennaList[i].pParent->baseType == eBaseTypeNPC)
        {
            nme = (zNMECommon*)sAntennaList[i].pParent;
            if (xEntIsVisible(nme) && (nme->baseFlags & 0x1) && !(nme->baseFlags & 0x40) &&
                (nme->model == NULL || !(nme->model->Flags & 0x400)) &&
                nme->runtimeData.flg_activeOn &&
                (globals.cmgr == NULL || globals.cmgr->csn == NULL || (nme->flags & 0x80)))
            {
                RenderOneAntennaSparks(&sAntennaList[i]);
            }
        }
        else
        {
            RenderOneAntennaSparks(&sAntennaList[i]);
        }
    }
}

void xAntennaAddAntenna(xEnt* pParent, F32 length, F32 rigidity, S32 startBone, S32 endBone,
                        bool noLight)
{
    S32 i;
    xAntenna* pAntenna;
    xVec3 antennaBase;
    xVec3 antennaDelta;
    xMat4x3* pMat;

    // As in retail, the slot is read before the bound check (sAntennaList[96] when the list is full).
    i = 0;
    while (sAntennaList[i].pParent != NULL && i < 96)
    {
        i++;
    }

    if (i >= 96)
    {
        return;
    }

    pAntenna = &sAntennaList[i];
    pAntenna->pParent = pParent;
    pAntenna->startBone = startBone;
    pAntenna->endBone = endBone;
    pAntenna->length = length;
    pAntenna->rigidity = rigidity;
    pAntenna->noLight = noLight;

    memset(&pAntenna->tipCurrentVelocity, 0, sizeof(xVec3));

    if (pAntenna->startBone > -1)
    {
        pAntenna->length = -1.0f;
    }
    else
    {
        pMat = xModelGetFrame(pAntenna->pParent->model);
        antennaBase.assign(1.0f, 1.0f, 1.0f);
        antennaDelta.assign(0.0f, pAntenna->length, 0.0f);
        xVec3 tipIdealPosition = antennaBase + antennaDelta;
        xMat4x3Toworld(&tipIdealPosition, pMat, &tipIdealPosition);
        pAntenna->tipCurrentPosition = tipIdealPosition;
    }

    pAntenna->accumDist = 1.0f;
    pAntenna->distToCorrect = 0.0f;
    pAntenna->timer = xrand_RandomFloatRange(0.0f, 1.4f);
}

void xAntennaUpdate(F32 dt)
{
    S32 i;
    zNMECommon* nme;

    for (i = 0; i < 96; i++)
    {
        if (sAntennaList[i].pParent == NULL)
        {
            continue;
        }

        if (sAntennaList[i].pParent->baseType == eBaseTypeNPC)
        {
            nme = (zNMECommon*)sAntennaList[i].pParent;

            if (!xEntIsVisible(nme))
            {
                memset(&sAntennaList[i].tipCurrentVelocity, 0, sizeof(xVec3));
                sAntennaList[i].accumDist = 1.0f;
                sAntennaList[i].distToCorrect = 0.0f;
                continue;
            }

            if (!(nme->baseFlags & 0x1))
            {
                memset(&sAntennaList[i].tipCurrentVelocity, 0, sizeof(xVec3));
                sAntennaList[i].accumDist = 1.0f;
                sAntennaList[i].distToCorrect = 0.0f;
                continue;
            }

            if (nme->baseFlags & 0x40)
            {
                memset(&sAntennaList[i].tipCurrentVelocity, 0, sizeof(xVec3));
                sAntennaList[i].accumDist = 1.0f;
                sAntennaList[i].distToCorrect = 0.0f;
                continue;
            }

            if (nme->model != NULL && (nme->model->Flags & 0x400))
            {
                memset(&sAntennaList[i].tipCurrentVelocity, 0, sizeof(xVec3));
                sAntennaList[i].accumDist = 1.0f;
                sAntennaList[i].distToCorrect = 0.0f;
                continue;
            }

            if (!nme->runtimeData.flg_activeOn)
            {
                memset(&sAntennaList[i].tipCurrentVelocity, 0, sizeof(xVec3));
                sAntennaList[i].accumDist = 1.0f;
                sAntennaList[i].distToCorrect = 0.0f;
                continue;
            }

            if (globals.cmgr != NULL && globals.cmgr->csn != NULL && !(nme->flags & 0x80))
            {
                memset(&sAntennaList[i].tipCurrentVelocity, 0, sizeof(xVec3));
                sAntennaList[i].accumDist = 1.0f;
                sAntennaList[i].distToCorrect = 0.0f;
                continue;
            }

            UpdateOneAntenna(&sAntennaList[i], dt);
        }
        else
        {
            UpdateOneAntenna(&sAntennaList[i], dt);
        }
    }
}

// A pointer local reproduces retail's store through the element pointer on the early-return path.
void xAntennaRemove(xEnt* pParent, S32 startBone, S32 endBone)
{
    S32 i;

    for (i = 0; i < 96; i++)
    {
        xAntenna* pAntenna = &sAntennaList[i];
        if (pAntenna->pParent == pParent)
        {
            if (startBone < 0 || endBone < 0)
            {
                pAntenna->pParent = NULL;
            }
            else if (startBone == pAntenna->startBone && endBone == pAntenna->endBone)
            {
                pAntenna->pParent = NULL;
                return;
            }
        }
    }
}

void xAntennaRemoveAll()
{
    for (S32 i = 0; i < 96; i++)
    {
        sAntennaList[i].pParent = NULL;
    }
}

// namespace

void xAntennaSceneReset()
{
    S32 i;
    xAntenna* pAntenna;
    xVec3 tipIdealPosition;
    xModelInstance* pModel;
    xVec3 antennaBase;
    xVec3 antennaDelta;
    xMat4x3* pMat;

    for (i = 0; i < 96; i++)
    {
        pAntenna = &sAntennaList[i];
        if (pAntenna->pParent != NULL)
        {
            memset(&pAntenna->tipCurrentVelocity, 0, sizeof(xVec3));

            pModel = pAntenna->pParent->model;
            if (pAntenna->startBone > -1)
            {
                pMat = (xMat4x3*)&pModel->Mat[pAntenna->startBone + 1];
                tipIdealPosition = pMat->pos;
            }
            else
            {
                antennaBase.assign(1.0f, 1.0f, 1.0f);
                antennaDelta.assign(0.0f, pAntenna->length, 0.0f);
                tipIdealPosition = antennaBase + antennaDelta;
            }

            pMat = xModelGetFrame(pAntenna->pParent->model);
            xMat4x3Toworld(&tipIdealPosition, pMat, &tipIdealPosition);
            pAntenna->tipCurrentPosition = tipIdealPosition;

            pAntenna->accumDist = 1.0f;
            pAntenna->distToCorrect = 0.0f;
        }
    }
}
