#include <extlib/decoder/metadata.hpp>

#include <climits>
#include <cstring>
#include <algorithm>
#include <array>
#include <map>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include <utf8.h>

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
    LabelType type = parseLabelType(text);

    if (type != LabelType::NONE) {
        cuePoints[cueId].type = type;
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

void Metadata::parseId3v1(const uint8_t* data, size_t size) {
    if (size != 128 || std::memcmp(data, "TAG", 3) != 0) {
        return;
    }

    // Check for ID3v1.1 trackNo
    if (data[125] == 0 && data[126] > 0) {
        parseComment(reinterpret_cast<const char*>(data + 97), 28);
    } else {
        parseComment(reinterpret_cast<const char*>(data + 97), 30);
    }
}

void Metadata::parseId3v2(const uint8_t* data, size_t size) {
    if (size < 10 || std::memcmp(data, "ID3", 3) != 0) {
        return;
    }

    int headerSize, frameIdSize;
    uint32_t (*frameSizeFn)(const uint8_t*);

    switch (data[3]) {
    case 2: // ID3v2.2
        headerSize = 6;
        frameIdSize = 3;
        frameSizeFn = read_u24_be;
        break;
    case 3: // ID3v2.3
        headerSize = 10;
        frameIdSize = 4;
        frameSizeFn = read_u32_be;
        break;
    case 4: // ID3v2.4
        headerSize = 10;
        frameIdSize = 4;
        frameSizeFn = read_u32_syncsafe;
        break;
    default:
        return;
    }

    const uint8_t* p = data + 10;
    const uint8_t* end = data + size;

    while (p + headerSize <= end) {
        const char* frameId = reinterpret_cast<const char*>(p);

        if (std::memcmp(frameId, "\0\0\0\0", frameIdSize) == 0) {
            break;
        }

        size_t frameSize = frameSizeFn(p + frameIdSize);
        if (frameSize <= 2 || p + headerSize + frameSize > end) {
            break;
        }

        p += headerSize;

        if (std::memcmp(frameId, "TXXX", frameIdSize) == 0) {
            uint8_t encoding = *p;
            std::string data;

            switch (encoding) {
            case 0: // Latin-1
            case 3: // UTF-8
                data = std::string(reinterpret_cast<const char*>(p + 1), frameSize - 1);
                break;
            case 1: // UTF-16 with BOM
            case 2: // UTF-16BE
                data = utf8::utf16to8(std::u16string(reinterpret_cast<const char16_t*>(p + 1), (frameSize - 1) / 2));
                break;
            default:
                break;
            }

            auto pos = data.find('\0');
            if (pos != std::string::npos) {
                data.replace(pos, 1, "=");
                parseComment(data);
            }
        }

        p += frameSize;
    }
}

void Metadata::parseComment(const char* data, size_t size) {
    std::string comment(data, size);
    parseComment(comment);
}

void Metadata::parseComment(const std::string& comment) {
    auto pos = comment.find('=');

    if (pos == std::string::npos) {
        return;
    }

    auto key = comment.substr(0, pos);
    auto value = comment.substr(pos + 1);
    auto type = parseLabelType(key);

    if (type == LabelType::NONE || value.empty()) {
        return;
    }

    try {
        if (type == LabelType::LOOP_POINTS) {
            // Replace this if we ever bring in a full JSON library
            std::regex reStart(R"("start"\s*:\s*(\d+))");
            std::regex reEnd(R"("end"\s*:\s*(\d+))");
            std::smatch match;

            if (std::regex_search(value, match, reStart)) {
                comments[LabelType::LOOP_START] = std::stoll(match[1].str());
            }
            if (std::regex_search(value, match, reEnd)) {
                comments[LabelType::LOOP_END] = std::stoll(match[1].str());
            }
        } else {
            comments[type] = utf8::starts_with_bom(value)
                ? std::stoul(value.substr(3))
                : std::stoul(value);
        }
    } catch (...) {
    }
}

Metadata::LabelType Metadata::parseLabelType(std::string text) {
    const static std::array<std::pair<std::string, LabelType>, 9> types = {
        {
            {"LOOP",       LabelType::LOOP},
            {"CYCLE",      LabelType::LOOP},
            {"CYCLES",     LabelType::LOOP},
            {"LOOPSTART",  LabelType::LOOP_START},
            {"LOOPBEGIN",  LabelType::LOOP_START},
            {"LOOPPOINT",  LabelType::LOOP_START},
            {"LOOPEND",    LabelType::LOOP_END},
            {"LOOPLENGTH", LabelType::LOOP_LENGTH},
            {"LOOPPOINTS", LabelType::LOOP_POINTS},
        }
    };

    alphanum(text);
    uppercase(text);

    for (const auto& [ label, type ] : types) {
        if (std::strcmp(label.c_str(), text.c_str()) == 0) {
            return type;
        }
    }

    return LabelType::NONE;
}

void Metadata::findLoopPoints() {
    uint32_t start = 0, end = 0;
    int32_t count = 0;

    if (hasLoopPoints) {
        return;
    }

    if (comments.contains(LabelType::LOOP_START)) {
        if (comments.contains(LabelType::LOOP_END)) {
            start = comments[LabelType::LOOP_START];
            end = comments[LabelType::LOOP_END];
            return setLoopInfo(start, end, count);
        }
        if (comments.contains(LabelType::LOOP_LENGTH)) {
            start = comments[LabelType::LOOP_START];
            end = start + comments[LabelType::LOOP_LENGTH];
            return setLoopInfo(start, end, count);
        }
    }

    for (const auto& [ cuePointId, cuePoint ] : cuePoints) {
        switch (cuePoint.type) {
        case LabelType::LOOP:
            start = cuePoint.sampleOffset;
            end = cuePoint.sampleOffset + cuePoint.sampleLength;
            return setLoopInfo(start, end, count);
        case LabelType::LOOP_START:
            start = cuePoint.sampleOffset;
            break;
        case LabelType::LOOP_END:
            end = cuePoint.sampleOffset;
            break;
        default:
            break;
        }
    }

    setLoopInfo(start, end, count);
}

} // namespace Decoder
