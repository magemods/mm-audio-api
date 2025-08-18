#include <extlib/decoder/metadata.hpp>

#include <climits>
#include <cstring>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <extlib/utils.hpp>

namespace Decoder {

void Metadata::setTrackCount(uint32_t count) {
    trackCount = std::min(count, UINT32_MAX);
}

void Metadata::setSampleRate(uint32_t rate) {
    sampleRate = std::min(rate, UINT32_MAX);
}

void Metadata::setSampleCount(uint32_t count) {
    sampleCount = std::min(count, UINT32_MAX);
    if (loopEnd == 0) {
        loopEnd = sampleCount;
    }
}

void Metadata::setLoopInfo(uint32_t start, uint32_t end, int32_t count) {
    loopStart = std::min(start, UINT32_MAX);
    if (end > loopStart) {
        loopEnd = std::min(end, UINT32_MAX);
    }
    loopCount = count == 0 ? -1 : count;
    hasLoopPoints = true;
}

void Metadata::setCuePointLabel(uint32_t cueId, const char* data, size_t size) {
    std::string text(data, size);
    uppercase(text);
    trim(text);

    if (text == "LOOP" || text == "CYCLE") {
        cuePoints[cueId].type = CuePointType::LOOP;
    } else if (text == "LOOPSTART" || text == "LOOP_START") {
        cuePoints[cueId].type = CuePointType::LOOP_START;
    } else if (text == "LOOPEND" || text == "LOOP_END") {
        cuePoints[cueId].type = CuePointType::LOOP_END;
    }
}

void Metadata::setCuePointOffset(uint32_t cueId, uint32_t offset) {
    cuePoints[cueId].sampleOffset = offset;
}

void Metadata::setCuePointLength(uint32_t cueId, uint32_t length) {
    cuePoints[cueId].sampleLength = length;
}

void Metadata::parseRiffCue(const uint8_t* data, size_t size) {
    if (size < 12 || std::memcmp(data, "cue ", 4) != 0) {
        return;
    }

    uint32_t numPoints = read_u32_le(data + 8);
    const uint8_t* p = data + 12;

    for (uint32_t i = 0; i < numPoints; i++) {
        if (p + 24 > data + size) {
            break;
        }

        uint32_t cueId = read_u32_le(p + 0);
        uint32_t offset = read_u32_le(p + 20);
        setCuePointOffset(cueId, offset);

        p += 24;
    }
}

void Metadata::parseRiffList(const uint8_t* data, size_t size) {
    if (size < 12 || std::memcmp(data, "LIST", 4) != 0) {
        return;
    }
    if (std::memcmp(data + 8, "adtl", 4) != 0) {
        return;
    }

    const uint8_t* p = data + 12;
    const uint8_t* end = data + size;

    while (p + 8 <= end) {
        char id[5] = {0};
        std::memcpy(id, p, 4);

        uint32_t subSize = read_u32_le(p + 4);
        const uint8_t* subData = p + 8;

        if (std::memcmp(id, "labl", 4) == 0) {
            if (subSize >= 4) {
                uint32_t cueId = read_u32_le(subData);
                setCuePointLabel(cueId, reinterpret_cast<const char*>(subData + 4), subSize - 4);
            }
        } else if (std::memcmp(id, "ltxt", 4) == 0) {
            if (subSize >= 20) {
                uint32_t cueId = read_u32_le(subData);
                uint32_t length = read_u32_le(subData + 4);
                setCuePointLength(cueId, length);
            }
        }

        p += ((8 + subSize) + 1) & ~1;
    }
}

void Metadata::parseRiffSmpl(const uint8_t* data, size_t size) {
    if (size < 36 || std::memcmp(data, "smpl", 4) != 0) {
        return;
    }

    uint32_t loopCount = read_u32_le(data + 28);
    const uint8_t* p = data + 36;

    if (loopCount > 0) {
        uint32_t start = read_u32_le(p + 8);
        uint32_t end   = read_u32_le(p + 12);
        uint32_t count = read_u32_le(p + 20);
        setLoopInfo(start, end, count);
    }
}

void Metadata::parseVorbisComment(const char* data, size_t size) {
    std::string comment(data, size);
    auto pos = comment.find('=');
    if (pos == std::string::npos) {
        return;
    }

    auto key = comment.substr(0, pos);
    uppercase(key);

    auto value = comment.substr(pos + 1);

    if (key == "LOOPSTART" || key == "LOOP_START") {
        comments[CuePointType::LOOP_START] = std::stoul(value);
    } else if (key == "LOOPEND" || key == "LOOP_END") {
        comments[CuePointType::LOOP_END] = std::stoul(value);
    } else if (key == "LOOPLENGTH" || key == "LOOP_LENGTH") {
        comments[CuePointType::LOOP_LENGTH] = std::stoul(value);
    }
}

void Metadata::findLoopPoints() {
    uint32_t start = 0, end = 0;
    int32_t count = 0;

    if (hasLoopPoints) {
        return;
    }

    if (comments.contains(CuePointType::LOOP_START)) {
        if (comments.contains(CuePointType::LOOP_END)) {
            start = comments[CuePointType::LOOP_START];
            end = comments[CuePointType::LOOP_END];
            return setLoopInfo(start, end, count);
        }
        if (comments.contains(CuePointType::LOOP_LENGTH)) {
            start = comments[CuePointType::LOOP_START];
            end = start + comments[CuePointType::LOOP_LENGTH];
            return setLoopInfo(start, end, count);
        }
    }

    for (const auto& [ cuePointId, cuePoint ] : cuePoints) {
        switch (cuePoint.type) {
        case CuePointType::LOOP:
            start = cuePoint.sampleOffset;
            end = cuePoint.sampleOffset + cuePoint.sampleLength;
            return setLoopInfo(start, end, count);
        case CuePointType::LOOP_START:
            start = cuePoint.sampleOffset;
            break;
        case CuePointType::LOOP_END:
            end = cuePoint.sampleOffset;
            break;
        default:
            break;
        }
    }

    setLoopInfo(start, end, count);
}

} // namespace Decoder
