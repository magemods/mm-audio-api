#ifndef __AUDIO_API_AUDIO_FUNCTIONS__
#define __AUDIO_API_AUDIO_FUNCTIONS__

#include <global.h>
#include <utils/queue.h>

#define NA_BGM_UNKNOWN 0xFE

typedef struct {
    s32 seqId;
    u8 priority;
} SeqRequestExtended;

typedef struct {
    RecompQueueCmd setupCmd[8]; // setup commands
    RecompQueueCmd startAsyncSeqCmd; // temporarily stores the seqCmd used in SEQCMD_PLAY_SEQUENCE, to be called again once the font is reloaded in
    s32 seqId;
    u16 seqArgs;
    s32 prevSeqId; // last seqId played on a player
    u16 prevSeqArgs;
    u8 setupCmdTimer;
    u8 setupCmdNum; // number of setup commands
} ActiveSequenceExtended;

extern u8 sSeqFlags[];
extern u8* sExtSeqFlags;
extern u8 sStartSeqDisabled;
extern SeqRequestExtended sExtSeqRequests[SEQ_PLAYER_MAX][5];
extern ActiveSequenceExtended gExtActiveSeqs[SEQ_PLAYER_MAX];

void AudioApi_StartSequence(u8 seqPlayerIndex, s32 seqId, u16 seqArgs, u16 fadeInDuration);
u8 AudioApi_GetSequenceFlagsInternal(s32 seqId);
void AudioApi_SetSequenceFlagsInternal(s32 seqId, u8 flags);

#endif
