#include "iCutscene.h"
#include "xDebug.h"

U32 read_sizzze = 0;

// The compiled-out asserts keep GC/2.6 from auto-inlining this, as retail calls it. The one before
// the channel count is Ratatouille's; the other conditions are unknown.
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
    S32 uOffsetBytes;

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
    DVDCancelAllAsync(NULL);

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

                    csn->SndHandle[0] = xSndMgrPlay((iSndGroupHandle)(UPtr)pLeftGroup, 0x11, NULL,
                                                    NULL, NULL, NULL, NULL);
                    csn->SndHandle[1] = xSndMgrPlay((iSndGroupHandle)(UPtr)pRightGroup, 0x21, NULL,
                                                    NULL, NULL, NULL, NULL);
                }
                else
                {
                    // Mono
                    xSndGroup* pLeftGroup;

                    pLeftGroup = xSndMgr_AllocGroup(1);
                    pLeftGroup->aSndGroupInfo[0].uSoundNameHash = csn->SndAssetID[0];
                    pLeftGroup->header.eSoundCategory = 4;

                    csn->SndHandle[0] = xSndMgrPlay((iSndGroupHandle)(UPtr)pLeftGroup, 0x1, NULL,
                                                    NULL, NULL, NULL, NULL);
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
