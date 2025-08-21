#ifndef __AUDIO_API_TYPES_H__
#define __AUDIO_API_TYPES_H__

/*! \file audio_api/types.h
    \version 0.5.0
    \brief Audio API type definitions
 */

#ifdef __cplusplus
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
#endif

typedef s32 (*AudioApiDmaCallback)(void* ramAddr, size_t size, size_t offset, u32 arg0, u32 arg1, u32 arg2);

typedef enum {
    SEQCMD_EXTENDED_OP_PLAY_SEQUENCE = 0x10,
    SEQCMD_EXTENDED_OP_QUEUE_SEQUENCE = 0x12,
    SEQCMD_EXTENDED_OP_UNQUEUE_SEQUENCE = 0x13,
    SEQCMD_EXTENDED_OP_SETUP_CMD = 0x1C,
} SeqCmdExtendedOp;

typedef enum : u32 {                        // Note: specific implementation may vary by resource type
    AUDIOAPI_CACHE_DEFAULT,                 // Resource will assign a default value
    AUDIOAPI_CACHE_NONE,                    // Do not cache
    AUDIOAPI_CACHE_PRELOAD,                 // Preload entire file
    AUDIOAPI_CACHE_PRELOAD_ON_USE,          // Preload upon DMA request
    AUDIOAPI_CACHE_PRELOAD_ON_USE_NO_EVICT, // Preload upon DMA request and never evict from cache
} AudioApiCacheStrategy;

typedef enum : u32 {
    AUDIOAPI_CODEC_AUTO,
    AUDIOAPI_CODEC_WAV,
    AUDIOAPI_CODEC_FLAC,
    AUDIOAPI_CODEC_VORBIS,
    AUDIOAPI_CODEC_OPUS,
} AudioApiCodec;

typedef enum : u32 {
    AUDIOAPI_CHANNEL_TYPE_DEFAULT,
    AUDIOAPI_CHANNEL_TYPE_MONO,
    AUDIOAPI_CHANNEL_TYPE_STEREO,
} AudioApiChannelType;

typedef struct AudioApiFileInfo {
    u32 resourceId;
    u32 trackCount;
    u32 sampleRate;
    u32 sampleCount;
    u32 loopStart;
    u32 loopEnd;
    s32 loopCount;
    AudioApiCodec codec;
    AudioApiChannelType channelType;
    AudioApiCacheStrategy cacheStrategy;
} AudioApiFileInfo;

typedef struct AudioApiResourceInfo {
    u32 resourceId;
    u32 filesize;
    AudioApiCacheStrategy cacheStrategy;
} AudioApiResourceInfo;

typedef AudioApiResourceInfo AudioApiSequenceInfo;
typedef AudioApiResourceInfo AudioApiSoundFontInfo;
typedef AudioApiResourceInfo AudioApiSampleBankInfo;

#endif
