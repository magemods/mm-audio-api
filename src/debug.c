#include "global.h"
#include "modding.h"
#include "recomputils.h"
#include "util.h"

// Sequence 128 Testing

typedef void SoundFontData;

SoundFontData* AudioLoad_SyncLoadFont(u32 fontId);
u8* AudioLoad_SyncLoadSeq(s32 seqId);
u32 AudioLoad_GetRealTableIndex(s32 tableType, u32 id);
void* AudioLoad_SyncLoad(s32 tableType, u32 id, s32* didAllocate);


RECOMP_HOOK("Player_Update") void onPlayer_Update(Player* this, PlayState* play) {
    if (CHECK_BTN_ALL(CONTROLLER1(&play->state)->press.button, BTN_L)) {
        AudioSeq_StartSequence(SEQ_PLAYER_BGM_MAIN, 128, 0, 0);
    }
}

RECOMP_PATCH s32 AudioLoad_SyncInitSeqPlayerInternal(s32 playerIndex, s32 seqId, s32 arg2) {
    SequencePlayer* seqPlayer = &gAudioCtx.seqPlayers[playerIndex];
    u8* seqData;
    s32 index;
    s32 numFonts;
    s32 fontId;

    if (seqId >= gAudioCtx.numSequences) {
        return 0;
    }

    AudioScript_SequencePlayerDisable(seqPlayer);

    // @mod We need to resize and add entries to the sequenceFontTable,
    // but for now just load fontId 3 for our new sequence
    if (seqId == 128) {
        fontId = 3;
        AudioLoad_SyncLoadFont(fontId);
    } else {
        fontId = 0xFF;
        index = ((u16*)gAudioCtx.sequenceFontTable)[seqId];
        numFonts = gAudioCtx.sequenceFontTable[index++];

        while (numFonts > 0) {
            fontId = gAudioCtx.sequenceFontTable[index++];
            AudioLoad_SyncLoadFont(fontId);
            numFonts--;
        }
    }

    seqData = AudioLoad_SyncLoadSeq(seqId);
    if (seqData == NULL) {
        return 0;
    }

    AudioScript_ResetSequencePlayer(seqPlayer);
    seqPlayer->seqId = seqId;

    if (fontId != 0xFF) {
        seqPlayer->defaultFont = AudioLoad_GetRealTableIndex(FONT_TABLE, fontId);
    } else {
        seqPlayer->defaultFont = 0xFF;
    }

    seqPlayer->seqData = seqData;
    seqPlayer->enabled = true;
    seqPlayer->scriptState.pc = seqData;
    seqPlayer->scriptState.depth = 0;
    seqPlayer->delay = 0;
    seqPlayer->finished = false;
    seqPlayer->playerIndex = playerIndex;
    //! @bug missing return (but the return value is not used so it's not UB)
}

// gAudioCtx.seqLoadStatus has 128 max entries, so we can't read/write anything higher
// The sequence should load immediately since it's already in RAM, but we can also
// make a new array to track the load status of custom sequences.

RECOMP_PATCH u8* AudioLoad_SyncLoadSeq(s32 seqId) {
    s32 pad;
    s32 didAllocate;

    if (seqId <= 0x7F) {
        if (gAudioCtx.seqLoadStatus[AudioLoad_GetRealTableIndex(SEQUENCE_TABLE, seqId)] == LOAD_STATUS_IN_PROGRESS) {
            return NULL;
        }
    }

    return AudioLoad_SyncLoad(SEQUENCE_TABLE, seqId, &didAllocate);
}

RECOMP_PATCH void AudioLoad_SetSeqLoadStatus(s32 seqId, s32 loadStatus) {
    if (seqId > 0x7F) {
        return;
    }
    if ((seqId != 0xFF) && (gAudioCtx.seqLoadStatus[seqId] != LOAD_STATUS_PERMANENT)) {
        gAudioCtx.seqLoadStatus[seqId] = loadStatus;
    }
}

RECOMP_PATCH s32 AudioLoad_IsSeqLoadComplete(s32 seqId) {
    if (seqId > 0x7F) {
        return true;
    } else if (gAudioCtx.seqLoadStatus[seqId] >= LOAD_STATUS_COMPLETE) {
        return true;
    } else if (gAudioCtx.seqLoadStatus[AudioLoad_GetRealTableIndex(SEQUENCE_TABLE, seqId)] >= LOAD_STATUS_COMPLETE) {
        return true;
    } else {
        return false;
    }
}
