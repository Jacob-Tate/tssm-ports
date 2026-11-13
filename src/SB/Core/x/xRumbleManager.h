#ifndef XRUMBLEMANAGER_H
#define XRUMBLEMANAGER_H

#include "xRumbleEmitter.h"
#include "xCamera.h"
#include "xPad.h"

#include <string.h>
#include <types.h>

#define kMaxRumblingPads 1

namespace xRumble
{
    struct effectInternal
    {
        emitterBase* pEmitter;
        F32 timeToLive;
        F32 prevMagFraction;
        cameraFX* cameraEffect;
        U8 rumbleForever;

        void updateCameraShake(F32 intensity)
        {
            effectAsset* pAsset = pEmitter->pEffectAsset;

            if (pAsset->type != 1 || !xfeq0(intensity))
            {
                cameraEffect->shake.magnitude = intensity * pAsset->shakeMagnitude;
                cameraEffect->shake.rotate_magnitude = intensity * pAsset->shakeRotationalMagnitude;
            }
            else if (!xfeq0(prevMagFraction))
            {
                cameraEffect->shake.dampenRate = prevMagFraction / 20.0f;
            }
            else
            {
                cameraEffect->shake.magnitude -=
                    cameraEffect->shake.dampenRate * pAsset->shakeMagnitude;
                cameraEffect->shake.rotate_magnitude -=
                    cameraEffect->shake.dampenRate * pAsset->shakeRotationalMagnitude;

                if (cameraEffect->shake.magnitude < 0.0f)
                {
                    cameraEffect->shake.magnitude = 0.0f;
                }

                if (cameraEffect->shake.rotate_magnitude < 0.0f)
                {
                    cameraEffect->shake.rotate_magnitude = 0.0f;
                }
            }
        }

        void EndCameraShake()
        {
            xCameraFXShakeEnd(cameraEffect, 0.3f);
            cameraEffect = NULL;
            prevMagFraction = 0.0f;
        }

        F32 GetIntensity(const xVec3& playerPos);
    };

    struct padInfo
    {
        effectInternal m_effects[16];
        effectInternal* m_currentExclusive;

        void FindNewExclusive()
        {
            m_currentExclusive = NULL;

            U32 maxPriority = 0;
            for (U32 i = 0; i < 16; i++)
            {
                if (m_effects[i].pEmitter &&
                    (m_effects[i].rumbleForever || m_effects[i].timeToLive > 0.0f))
                {
                    U32 currentPriority = m_effects[i].pEmitter->pEffectAsset->priority;
                    if (currentPriority > maxPriority)
                    {
                        m_currentExclusive = &m_effects[i];
                        maxPriority = currentPriority;
                    }
                }
            }
        }
    };

    struct Manager
    {
        padInfo m_padInfo[kMaxRumblingPads];

        Manager()
        {
            memset(this, 0, sizeof(Manager));
        }

        void store(padInfo* pInfo, emitterBase* pEmitter, effectInternal* pEffectInternal,
                   F32 timeToLive, bool rumbleForever)
        {
            pEffectInternal->pEmitter = pEmitter;
            pEffectInternal->timeToLive = timeToLive;
            pEffectInternal->rumbleForever = rumbleForever;

            if (!pInfo->m_currentExclusive && pEffectInternal->pEmitter->pEffectAsset->priority > 0)
            {
                pInfo->m_currentExclusive = pEffectInternal;
            }
        }

        void StopCameraShake(S32 padNum)
        {
            padInfo* pInfo = &m_padInfo[padNum];
            for (U32 j = 0; j < 16; j++)
            {
                if (pInfo->m_effects[j].cameraEffect)
                {
                    pInfo->m_effects[j].EndCameraShake();
                }
            }
        }

        void StopCameraShake()
        {
            for (S32 i = 0; i < kMaxRumblingPads; i++)
            {
                StopCameraShake(i);
            }
        }

        static Manager& Get();
        void StopRumbling();
        void Reset();
        void Update(_tagxPad* pad, const xVec3& playerPos, F32 timeElapsed, bool bPauseState);
        bool Add(_tagxPad* pad, emitterBase* pEmitter);
        U8 Stop(_tagxPad* pad, emitterBase* pEmitter)
        {
            padInfo* pInfo = &m_padInfo[pad->port];

            for (U32 i = 0; i < 16; i++)
            {
                if (pInfo->m_effects[i].pEmitter != NULL &&
                    pEmitter->pEffectAsset == pInfo->m_effects[i].pEmitter->pEffectAsset)
                {
                    pInfo->m_effects[i].timeToLive = 0.0f;
                    pInfo->m_effects[i].rumbleForever = 0;
                    return 1;
                }
            }

            return 0;
        }
    };
} // namespace xRumble

#endif
