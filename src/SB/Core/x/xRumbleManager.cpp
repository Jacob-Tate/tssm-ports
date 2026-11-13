#include "xRumbleManager.h"
#include "zGlobals.h"
#include "iPad.h"

#include <string.h>

namespace xRumble
{

    F32 effectInternal::GetIntensity(const xVec3& playerPos)
    {
        F32 intensity = 0.0f;
        effectAsset* pAsset = pEmitter->pEffectAsset;

        if (pEmitter->GetIntensity(playerPos, intensity))
        {
            intensity *= pAsset->GetIntensity(pEmitter->pEffectAsset->time - timeToLive,
                                              pEmitter->pEffectAsset->time);

            F32 magFraction = intensity / pAsset->intensity;
            if (cameraEffect)
            {
                updateCameraShake(magFraction);
            }
            else if ((!xfeq0(pAsset->shakeMagnitude) || !xfeq0(pAsset->shakeRotationalMagnitude)) &&
                     !xfeq0(pAsset->shakeCycleMax))
            {
                cameraEffect = xCameraFXShakeForever(magFraction * pAsset->shakeMagnitude,
                                                     pAsset->shakeCycleMax,
                                                     magFraction * pAsset->shakeRotationalMagnitude,
                                                     0.0f, NULL, NULL, pAsset->shakeY);
            }

            prevMagFraction = magFraction;
        }
        else if (cameraEffect)
        {
            EndCameraShake();
        }

        return intensity;
    }

    bool Manager::Add(_tagxPad* pad, emitterBase* pEmitter)
    {
        if (!pEmitter)
        {
            return false;
        }

        padInfo* pInfo = &m_padInfo[pad->port];
        // Not in the PS2 DWARF, but retail loads the effect time ahead of the rumbleForever
        // compare, as it does when it is kept in a local.
        F32 timeToLive = pEmitter->pEffectAsset->time;
        bool rumbleForever = (pEmitter->pEffectAsset->time == 0.0f);

        emitterBase* pCurrentEmitter = NULL;
        if (pInfo->m_currentExclusive)
        {
            pCurrentEmitter = pInfo->m_currentExclusive->pEmitter;
        }

        if (pCurrentEmitter && pEmitter->pEffectAsset->id != 0 &&
            pEmitter->pEffectAsset->id == pCurrentEmitter->pEffectAsset->id)
        {
            store(pInfo, pEmitter, pInfo->m_currentExclusive, timeToLive, rumbleForever);
            pInfo->m_currentExclusive = NULL;
            if (pEmitter->pEffectAsset->priority == 0)
            {
                pInfo->FindNewExclusive();
            }
            return true;
        }

        U32 effectPriority = pEmitter->pEffectAsset->priority;
        if (pInfo->m_currentExclusive &&
            ((effectPriority != 0 &&
              pInfo->m_currentExclusive->pEmitter->pEffectAsset->priority >= effectPriority) ||
             effectPriority == 0) &&
            (pInfo->m_currentExclusive->rumbleForever ||
             pInfo->m_currentExclusive->timeToLive >= timeToLive))
        {
            return false;
        }

        effectInternal* freeEffect = NULL;
        for (U32 i = 0; i < 16; i++)
        {
            if (pEmitter->pEffectAsset->id != 0 && pInfo->m_effects[i].pEmitter &&
                pEmitter->pEffectAsset->id == pInfo->m_effects[i].pEmitter->pEffectAsset->id)
            {
                store(pInfo, pEmitter, &pInfo->m_effects[i], timeToLive, rumbleForever);
                return true;
            }

            if (!freeEffect && !pInfo->m_effects[i].rumbleForever &&
                pInfo->m_effects[i].timeToLive <= 0.0f)
            {
                freeEffect = &pInfo->m_effects[i];
            }
        }

        if (freeEffect)
        {
            store(pInfo, pEmitter, freeEffect, timeToLive, rumbleForever);
        }

        return true;
    }

    void Manager::Update(_tagxPad* pad, const xVec3& playerPos, F32 timeElapsed, bool bPauseState)
    {
        if (!pad || pad->port >= kMaxRumblingPads)
        {
            return;
        }

        padInfo* pInfo = &m_padInfo[pad->port];

        for (U32 i = 0; i < 16; i++)
        {
            if (pInfo->m_effects[i].pEmitter &&
                (!bPauseState || pInfo->m_effects[i].pEmitter->pEffectAsset->rumbleInPause) &&
                pInfo->m_effects[i].timeToLive >= 0.0f)
            {
                pInfo->m_effects[i].timeToLive -= timeElapsed;
            }
        }

        if (pInfo->m_currentExclusive)
        {
            if (pInfo->m_currentExclusive->rumbleForever)
            {
                iPadStartRumble(pad, pInfo->m_currentExclusive->GetIntensity(playerPos));
                return;
            }

            pInfo->FindNewExclusive();
            if (pInfo->m_currentExclusive)
            {
                iPadStartRumble(pad, pInfo->m_currentExclusive->GetIntensity(playerPos));
                return;
            }
        }

        F32 intensity = 0.0f;
        for (U32 i = 0; i < 16; i++)
        {
            if (pInfo->m_effects[i].rumbleForever || pInfo->m_effects[i].timeToLive > 0.0f)
            {
                if (!bPauseState || pInfo->m_effects[i].pEmitter->pEffectAsset->rumbleInPause)
                {
                    intensity += pInfo->m_effects[i].GetIntensity(playerPos);
                }
            }
            else if (pInfo->m_effects[i].cameraEffect)
            {
                pInfo->m_effects[i].EndCameraShake();
            }
        }

        if (intensity > 1.0f)
        {
            intensity = 1.0f;
        }

        if (xfeq0(intensity))
        {
            iPadStopRumble(pad);
        }
        else
        {
            iPadStartRumble(pad, intensity);
        }
    }

    Manager& Manager::Get()
    {
        static Manager rumbleManager;
        return rumbleManager;
    }

    void Manager::Reset()
    {
        StopRumbling();
        StopCameraShake();
        memset(this, 0, sizeof(Manager));
    }

    void Manager::StopRumbling()
    {
        iPadStopRumble(xglobals->pad0);
    }
} // namespace xRumble
