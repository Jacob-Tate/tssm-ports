#include "iCutscene.h"
#include "xDebug.h"

// PORT: iCSSoundPull, at the end of the file.
#include "iSndXbox.h"

U32 read_sizzze = 0;

// 4 compiled-out asserts keep GC/2.6 from auto-inlining this (>= 30 statements), as retail calls it
// (measured minimum 4). The one before the channel count is Ratatouille's (iCutscene.cpp:47, same
// position: before the loop that counts SndNumChannel); the other conditions are unknown.
void iCSSoundSetup(xCutscene* csn)
{
    xCutsceneData* data;
    U32 numData;
    xRegion eMyRegion;
    S32 i;
    U32 dataIndex;
    U32 firstSoundID;
    bool foundTrack;

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    data = (xCutsceneData*)(csn->Play + 1);
    numData = csn->Play->NumData;

    eMyRegion = xSTGetLocalizationEnum();

    csn->SndAssetID[0] = csn->Info->AudioTracks[eMyRegion].uLeftSoundId;
    csn->SndAssetID[1] = csn->Info->AudioTracks[eMyRegion].uRightSoundId;

    xASSERT(csn->SndNumChannel == 0);

    for (dataIndex = 0, foundTrack = false, firstSoundID = 0; dataIndex < numData; dataIndex++)
    {
        if (data->DataType == XCUTSCENEDATA_TYPE_SOUND)
        {
            if (firstSoundID == 0)
            {
                firstSoundID = data->AssetID;
            }

            if (data->AssetID == csn->SndAssetID[0])
            {
                foundTrack = true;
            }

            csn->SndNumChannel++;
        }

        data = (xCutsceneData*)((U8*)data + ALIGN(data->ChunkSize, 16) + sizeof(xCutsceneData));
    }

    if (!foundTrack)
    {
        csn->SndAssetID[0] = firstSoundID;
        csn->SndAssetID[1] = 0;

        for (i = 0; i < eRegionCount; i++)
        {
            if (firstSoundID == csn->Info->AudioTracks[i].uLeftSoundId)
            {
                csn->SndAssetID[0] = csn->Info->AudioTracks[i].uLeftSoundId;
                csn->SndAssetID[1] = csn->Info->AudioTracks[i].uRightSoundId;
            }
        }
    }
}

void* iCSSoundGetData(U32 assetID, U32* len)
{
    S32 channelIndex;
    U32 dataIndex;
    xCutscene* csn;
    void* retdata;
    xCutsceneData* data;
    U32 numData;
    S32 sndChannelIndex;
    U32 sndChannelReq;

    csn = xCutscene_CurrentCutscene();

    retdata = NULL;
    sndChannelIndex = -1;

    // Retail bug: SndAssetID[channelIndex] is read before the bounds check, so the loop reads
    // one element past the end (SndHandle[0]) when both channels are in use.
    for (channelIndex = 0; csn->SndAssetID[channelIndex] != 0xFFFFFFFF && channelIndex < 2;
         channelIndex++)
    {
        if (csn->SndAssetID[channelIndex] == assetID)
        {
            sndChannelIndex = channelIndex;
            sndChannelReq = csn->SndChannelReq[channelIndex];
        }
    }

    if (sndChannelIndex == -1)
    {
        return NULL;
    }

    if (!csn->Waiting && csn->Stream->ChunkIndex == sndChannelReq)
    {
        numData = csn->Stream->NumData;
        data = (xCutsceneData*)(csn->Stream + 1);
    }
    else
    {
        numData = csn->Play->NumData;
        data = (xCutsceneData*)(csn->Play + 1);

        if (csn->SndChannelReq[sndChannelIndex] != csn->Play->ChunkIndex)
        {
            csn->SndChannelReq[sndChannelIndex] = csn->Play->ChunkIndex;
        }
    }

    for (dataIndex = 0; dataIndex < numData; dataIndex++)
    {
        if (data->DataType == XCUTSCENEDATA_TYPE_SOUND &&
            data->AssetID == csn->SndAssetID[sndChannelIndex])
        {
            retdata = (void*)(data + 1);
            *len = data->ChunkSize;
            break;
        }

        data = (xCutsceneData*)((U8*)data + ALIGN(data->ChunkSize, 16) + sizeof(xCutsceneData));
    }

    if (!retdata)
    {
        return NULL;
    }

    while ((UPtr)retdata & 0x1F)
    {
        retdata = (void*)((U8*)retdata + 16);
        *len -= 16;
    }

    csn->SndChannelReq[sndChannelIndex]++;

    return retdata;
}

#ifdef PLATFORM_PC
// PORT: the console's disc seek before a cutscene's first read.
//
// A cutscene loaded frame by frame (zCutsceneMgrUpdate) is Ready only once
// iCSLoadStep has read its models and its first time chunk, and the rest of
// the scene keeps updating until then. On the console the drive first has to
// seek to the cutscene: retail in Dolphin (accurate disc timing) took 38
// frames, 0.63 s, to deliver the first read of Neptune's death cutscene, while
// the reads after it came back within a frame. A host read completes on the
// next service call, so the port was Ready three frames after the load began.
//
// Game code relies on those frames. Enemies stop updating once a cutscene is
// Ready (zNMECentral_IsCinematic). Neptune's last hit starts a 0.1 s blend
// out of his Stun animation, and only the end of that blend stops Stun's
// looped dizzy sound (an animation sound effect, xAnim.cpp EffectSingleStop).
// With the cutscene Ready one frame into the blend, the loop was never
// stopped and played on under the dialogue and the end credits.
//
// So the load waits that long before its first read, counted in game time:
// each step stands for one frame (update_dt), and a step in a frame without
// game time counts nothing off. zCutsceneMgrFinishLoad's loop, where nothing
// else updates, steps many times within one frame with the same update_dt and
// runs through the wait without delaying anything, as before. update_dt is
// set only by the game loop, so it is still 0 when the first scene after boot
// loads its opening cutscene that way: there a second step in the same frame
// with no time to count ends the wait, or the loop would never finish.
static const F32 CSN_DISC_SEEK = 0.63f;
static F32 sDiscSeekLeft;
static U32 sDiscSeekFrame;
#endif

static void iCSAsyncReadCB(tag_xFile* file)
{
    S32 bytes;
    IFILE_READSECTOR_STATUS cdstat;
    xCutscene* csn;

    if (file)
    {
        cdstat = iFileReadAsyncStatus(file->ps.asynckey, &bytes);
        if (cdstat == IFILE_RDSTAT_DONE)
        {
            iFileSeek(file, bytes, IFILE_SEEK_CUR);
        }

        csn = xCutscene_CurrentCutscene();
        csn->Waiting = 0;
    }
}

U32 iCSFileOpen(xCutscene* csn)
{
    U32 headerskip;
    PKRAssetTOCInfo ainfo;
    const char* filename;
    S32 uOffsetBytes; // Ratatouille GC DWARF (ratsgc_d/ratsgc_md iCSFileOpen)

    headerskip = ALIGN(csn->Info->HeaderSize, 2048);

    if (!xSTGetAssetInfo(csn->Info->AssetID, &ainfo))
    {
        return 0;
    }

    filename = xST_xAssetID_HIPFullPath(csn->Info->AssetID);

    if (iFileOpen(filename, IFILE_OPEN_READ, &csn->File) == 0)
    {
        uOffsetBytes = ainfo.sector * iFileGetSectorSize(XFILE_DRIVE_CDVD);
        iFileSeek(&csn->File, uOffsetBytes + headerskip, IFILE_SEEK_SET);

        csn->File.ps.asynckey = -1;

#ifdef PLATFORM_PC
        sDiscSeekLeft = CSN_DISC_SEEK;
        sDiscSeekFrame = gFrameCount - 1;
#endif
    }
    else
    {
        return 0;
    }

    return 1;
}

void iCSFileAsyncRead(xCutscene* csn, void* dest, U32 size)
{
    read_sizzze = size;
    csn->Waiting = 1;

    iFileReadAsync(&csn->File, dest, size, iCSAsyncReadCB, 0);
}

inline void iCSFileAsyncSkip(xCutscene* csn, U32 amount)
{
    csn->Waiting = 1;

    iFileSeek(&csn->File, amount, IFILE_SEEK_CUR);
}

void iCSFileClose(xCutscene* csn)
{
#ifndef PLATFORM_PC
    DVDCancelAllAsync(NULL);
#else
    // PORT: the console cancels every queued DVD read; the host's reads are
    // iFile's queue, which iFileReadStop empties.
    iFileReadStop();
#endif

    csn->Opened = 0;
}

S32 iCSLoadStep(xCutscene* csn)
{
    U32 skipAccum;
    U32 tmpSize;
    void* foundModel;

    if (csn->Waiting)
    {
        return 0;
    }

#ifdef PLATFORM_PC
    // A step without game time counts nothing off the wait; a second one in the
    // same frame is zCutsceneMgrFinishLoad's loop, which does not wait.
    if (sDiscSeekLeft > 0.0f)
    {
        if (xglobals->update_dt > 0.0f)
        {
            sDiscSeekLeft -= xglobals->update_dt;
        }
        else if (sDiscSeekFrame == gFrameCount)
        {
            sDiscSeekLeft = 0.0f;
        }

        sDiscSeekFrame = gFrameCount;

        if (sDiscSeekLeft > 0.0f)
        {
            return 0;
        }
    }
#endif

    if (csn->DataLoading >= 0)
    {
        skipAccum = 0;

        while (csn->DataLoading < (S32)csn->Info->NumData)
        {
            if (csn->Data[csn->DataLoading].DataType == XCUTSCENEDATA_TYPE_6)
            {
                foundModel = NULL;
            }
            else
            {
                foundModel = xSTFindAsset(csn->Data[csn->DataLoading].AssetID, &tmpSize);
            }

            if (foundModel || csn->Data[csn->DataLoading].ChunkSize == 0)
            {
                csn->Data[csn->DataLoading].DataPtr = foundModel;

                skipAccum += ALIGN(csn->Data[csn->DataLoading].ChunkSize, 2048);

                csn->DataLoading++;
            }
            else
            {
                if (skipAccum)
                {
                    iCSFileAsyncSkip(csn, skipAccum);

                    skipAccum = 0;
                }

                if (csn->GotData)
                {
                    if (csn->Data[csn->DataLoading].DataType == XCUTSCENEDATA_TYPE_6)
                    {
                        csn->Data[csn->DataLoading].DataPtr =
                            RwMalloc(csn->Data[csn->DataLoading].ChunkSize, 0);

                        memcpy(csn->Data[csn->DataLoading].DataPtr, csn->AlignBuf,
                               csn->Data[csn->DataLoading].ChunkSize);
                    }
                    else
                    {
                        csn->Data[csn->DataLoading].DataPtr =
                            iModelFileNew(csn->AlignBuf, csn->Data[csn->DataLoading].ChunkSize);
                    }

                    csn->Data[csn->DataLoading].DataType |= 0x80000000;
                    csn->DataLoading++;
                    csn->GotData = 0;
                    csn->Stream->ChunkIndex = -1;
                }
                else
                {
                    iCSFileAsyncRead(csn, csn->AlignBuf,
                                     ALIGN(csn->Data[csn->DataLoading].ChunkSize, 2048));

                    csn->GotData = 1;
                    return 0;
                }
            }
        }

        if (skipAccum)
        {
            iCSFileAsyncSkip(csn, skipAccum);
        }

        csn->DataLoading = -1;
    }

    if (csn->DataLoading == -1)
    {
        if (csn->GotData)
        {
            iCSSoundSetup(csn);

            if (csn->SndNumChannel != 0)
            {
                xSndMgrUpdate();

                if (csn->SndAssetID[0] != 0 && csn->SndAssetID[1] != 0)
                {
                    // Stereo
                    xSndGroup* pLeftGroup;
                    xSndGroup* pRightGroup;

                    pLeftGroup = xSndMgr_AllocGroup(1);
                    pLeftGroup->aSndGroupInfo[0].uSoundNameHash = csn->SndAssetID[0];
                    pLeftGroup->header.eSoundCategory = 4;

                    pRightGroup = xSndMgr_AllocGroup(1);
                    pRightGroup->aSndGroupInfo[0].uSoundNameHash = csn->SndAssetID[1];
                    pRightGroup->header.eSoundCategory = 4;

                    csn->SndHandle[0] = xSndMgrPlay((iSndGroupHandle)(UPtr)pLeftGroup, 0x11, NULL, NULL,
                                                    NULL, NULL, NULL);
                    csn->SndHandle[1] = xSndMgrPlay((iSndGroupHandle)(UPtr)pRightGroup, 0x21, NULL, NULL,
                                                    NULL, NULL, NULL);
                }
                else
                {
                    // Mono
                    xSndGroup* pLeftGroup;

                    pLeftGroup = xSndMgr_AllocGroup(1);
                    pLeftGroup->aSndGroupInfo[0].uSoundNameHash = csn->SndAssetID[0];
                    pLeftGroup->header.eSoundCategory = 4;

                    csn->SndHandle[0] =
                        xSndMgrPlay((iSndGroupHandle)(UPtr)pLeftGroup, 0x1, NULL, NULL, NULL, NULL, NULL);
                    csn->SndHandle[1] = (iSndHandle)-1;
                }
            }
            else
            {
                csn->SndHandle[0] = (iSndHandle)-1;
                csn->SndHandle[1] = (iSndHandle)-1;
            }

            csn->GotData = 0;
            csn->DataLoading = -2;
        }
        else
        {
            iCSFileAsyncRead(csn, csn->Play, csn->TimeChunkOffs[1] - csn->TimeChunkOffs[0]);

            csn->GotData = 1;
            return 0;
        }
    }

    if (csn->DataLoading == -2)
    {
        if (csn->Info->NumTime > 1)
        {
            iCSFileAsyncRead(csn, csn->Stream, csn->TimeChunkOffs[2] - csn->TimeChunkOffs[1]);
        }

        csn->DataLoading = -3;
    }

    // Retail bug: SndHandle[i] is read before the bounds check, so the loop reads one element
    // past the end (SubTitlesID) when both channels are playing.
    U32 i;
    for (i = 0; csn->SndHandle[i] != -1 && i < 2; i++)
    {
        if (!xSndMgrIsReady(csn->SndHandle[i]))
        {
            return 0;
        }
    }

    csn->Ready = 1;
    return 1;
}

// ---------------------------------------------------------------------------
// PORT: the PC's feed of cutscene audio (declared in pc/iSnd.h).
//
// On the console FMOD pulls the track through iCSSoundGetData from its stream
// callback, a buffer at a time as it runs dry. The PC mixer runs on its own
// thread and must not touch the cutscene's buffers, so iSnd.cpp pulls instead,
// from the game thread, each block as soon as the chunk holding it has been
// read, and copies it into the voice's ring (iSndHost.h).
//
// The track is laid out one chunk ahead of the picture: chunk 0 carries two
// chunks' worth of audio and each later chunk the next one's (measured on all
// 28 Xbox cutscenes). So pulling eagerly keeps the voice a chunk ahead of the
// playhead, and a block is always taken from the chunk that holds it --
// iCSSoundGetData's fallback of re-reading the Play chunk when the Stream
// chunk is late would play a chunk twice here, so this waits instead.
//
// The PCM itself is found by iSndXboxCsnAudio, bounded by the chunk's size.
void* iCSSoundPull(U32 assetID, U32* size)
{
    xCutscene* csn = xCutscene_CurrentCutscene();
    S32 ch;

    *size = 0;

    if (csn == NULL || csn->Info == NULL || csn->Play == NULL || csn->Stream == NULL ||
        csn->TimeChunkOffs == NULL)
    {
        return NULL;
    }

    // Chunk 0 is in the Play buffer from the step that starts the sound
    // (DataLoading -1 with its read done) on; before that the buffer holds
    // models.
    if (csn->DataLoading >= 0 || (csn->DataLoading == -1 && (!csn->GotData || csn->Waiting)))
    {
        return NULL;
    }

    for (ch = 0; ch < 2; ch++)
    {
        if (csn->SndAssetID[ch] == assetID)
        {
            break;
        }
    }

    if (ch == 2)
    {
        return NULL;
    }

    // A chunk without this track is passed over; the loop is bounded by the
    // chunk count.
    for (U32 guard = 0; guard <= csn->Info->NumTime; guard++)
    {
        U32 req = csn->SndChannelReq[ch];
        xCutsceneTime* chunk = NULL;

        if (req >= csn->Info->NumTime)
        {
            return NULL;
        }

        // The Stream buffer holds chunk `req` once its read is issued (from
        // DataLoading -3 on) and finished.
        if (csn->DataLoading <= -3 && !csn->Waiting && csn->Stream->ChunkIndex == req)
        {
            chunk = csn->Stream;
        }
        else if (csn->Play->ChunkIndex == req)
        {
            chunk = csn->Play;
        }
        else if (csn->Play->ChunkIndex > req && csn->Play->ChunkIndex < csn->Info->NumTime)
        {
            // Behind the picture (a stall long enough to swap the buffers
            // twice): what was missed is gone, so carry on from the Play chunk,
            // as iCSSoundGetData does.
            req = csn->Play->ChunkIndex;
            chunk = csn->Play;
        }
        else
        {
            // The next chunk is not in yet.
            return NULL;
        }

        U32 bytes = csn->TimeChunkOffs[req + 1] - csn->TimeChunkOffs[req];
        const void* pcm = iSndXboxCsnAudio(chunk, bytes, assetID, size);

        csn->SndChannelReq[ch] = req + 1;

        if (pcm != NULL)
        {
            return (void*)pcm;
        }
    }

    return NULL;
}
