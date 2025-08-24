#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace Decoder {

class Metadata {
public:
    void setTrackCount(uint32_t count);
    void setSampleRate(uint32_t rate);
    void setSampleCount(uint32_t count);
    void setLoopInfo(uint32_t start, uint32_t end = 0, int32_t count = 0);

    void setCuePointLabel(uint32_t cueId, const char* data, size_t size);
    void setCuePointOffset(uint32_t cueId, uint32_t offset);
    void setCuePointLength(uint32_t cueId, uint32_t length);

    void parseRiffCue(const uint8_t* data, size_t size);
    void parseRiffList(const uint8_t* data, size_t size);
    void parseRiffSmpl(const uint8_t* data, size_t size);
    void parseId3v1(const uint8_t* data, size_t size);
    void parseId3v2(const uint8_t* data, size_t size);
    void parseComment(const char* data, size_t size);
    void parseComment(const std::string& comment);

    void findLoopPoints();

    uint32_t trackCount = 0;
    uint32_t sampleRate = 0;
    uint32_t sampleCount = 0;
    uint32_t loopStart = 0;
    uint32_t loopEnd = 0;
    int32_t loopCount = 0;

private:
    enum class LabelType {
        NONE,
        LOOP,
        LOOP_START,
        LOOP_END,
        LOOP_LENGTH,
        LOOP_POINTS,
    };

    struct CuePoint {
        LabelType type = LabelType::NONE;
        uint32_t sampleOffset = 0;
        uint32_t sampleLength = 0;
    };

    LabelType parseLabelType(std::string text);

    std::map<int, CuePoint> cuePoints;
    std::map<LabelType, unsigned long> comments;
    bool hasLoopPoints = false;
};

} // namespace Decoder
