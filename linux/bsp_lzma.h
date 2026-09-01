// The LZMA1 decoder Source uses for compressed BSP lumps, with no dependencies.
//
//   std::vector<uint8_t> out;
//   if (bsplzma::DecodeLump(lumpBytes, lumpLen, out)) { ... }
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace bsplzma {

// Source LZMA-compresses individual BSP lumps and plenty of bhop maps ship that
// way. Standard LzmaDec.
const int kNumStates        = 12;
const int kNumPosBitsMax    = 4;
const int kLenToPosStates   = 4;
const int kNumAlignBits     = 4;
const int kStartPosModel    = 4;
const int kEndPosModel      = 14;
const int kNumFullDistances = 1 << (kEndPosModel >> 1);
const int kMatchMinLen      = 2;
const uint32_t kTopValue    = 1u << 24;

class LzmaDecoder {
public:
    bool Decode(const uint8_t* props, const uint8_t* in, size_t inLen,
                uint8_t* out, size_t outLen)
    {
        if (!props || !in || !out) return false;

        int d = props[0];
        if (d >= 9 * 5 * 5) return false;
        lc_ = d % 9; d /= 9;
        lp_ = d % 5;
        pb_ = d / 5;

        in_ = in; inLen_ = inLen; inPos_ = 0;
        out_ = out; outLen_ = outLen; outPos_ = 0;
        failed_ = false;

        size_t o = 0;
        oIsMatch    = o; o += kNumStates << kNumPosBitsMax;
        oIsRep      = o; o += kNumStates;
        oIsRepG0    = o; o += kNumStates;
        oIsRepG1    = o; o += kNumStates;
        oIsRepG2    = o; o += kNumStates;
        oIsRep0Long = o; o += kNumStates << kNumPosBitsMax;
        oPosSlot    = o; o += kLenToPosStates << 6;
        oSpecPos    = o; o += kNumFullDistances - kEndPosModel;
        oAlign      = o; o += 1 << kNumAlignBits;
        oLenChoice  = o; o += 1;
        oLenChoice2 = o; o += 1;
        oLenLow     = o; o += 1 << (3 + kNumPosBitsMax);
        oLenMid     = o; o += 1 << (3 + kNumPosBitsMax);
        oLenHigh    = o; o += 256;
        oRepChoice  = o; o += 1;
        oRepChoice2 = o; o += 1;
        oRepLow     = o; o += 1 << (3 + kNumPosBitsMax);
        oRepMid     = o; o += 1 << (3 + kNumPosBitsMax);
        oRepHigh    = o; o += 256;
        oLiteral    = o; o += (size_t)0x300 << (lc_ + lp_);

        probs_.assign(o, 1024);

        NextByte();
        code_ = 0;
        for (int i = 0; i < 4; i++) code_ = (code_ << 8) | NextByte();
        range_ = 0xFFFFFFFFu;
        if (failed_) return false;

        return Run();
    }

private:
    const uint8_t* in_ = nullptr; size_t inLen_ = 0, inPos_ = 0;
    uint8_t* out_ = nullptr;      size_t outLen_ = 0, outPos_ = 0;
    uint32_t range_ = 0, code_ = 0;
    std::vector<uint16_t> probs_;
    int lc_ = 0, lp_ = 0, pb_ = 0;
    bool failed_ = false;

    size_t oIsMatch, oIsRep, oIsRepG0, oIsRepG1, oIsRepG2, oIsRep0Long;
    size_t oPosSlot, oSpecPos, oAlign;
    size_t oLenChoice, oLenChoice2, oLenLow, oLenMid, oLenHigh;
    size_t oRepChoice, oRepChoice2, oRepLow, oRepMid, oRepHigh;
    size_t oLiteral;

    uint8_t NextByte()
    {
        if (inPos_ >= inLen_) { failed_ = true; return 0; }
        return in_[inPos_++];
    }

    void Normalize()
    {
        if (range_ < kTopValue) { range_ <<= 8; code_ = (code_ << 8) | NextByte(); }
    }

    unsigned Bit(size_t idx)
    {
        Normalize();
        uint16_t prob = probs_[idx];
        uint32_t bound = (range_ >> 11) * prob;
        if (code_ < bound) {
            range_ = bound;
            probs_[idx] = (uint16_t)(prob + ((2048 - prob) >> 5));
            return 0;
        }
        range_ -= bound;
        code_  -= bound;
        probs_[idx] = (uint16_t)(prob - (prob >> 5));
        return 1;
    }

    unsigned DirectBits(int count)
    {
        unsigned result = 0;
        for (int i = 0; i < count; i++) {
            Normalize();
            range_ >>= 1;
            code_ -= range_;
            uint32_t t = 0u - (code_ >> 31);
            code_ += range_ & t;
            result = (result << 1) + (t + 1);
        }
        return result;
    }

    unsigned Tree(size_t off, int numBits)
    {
        unsigned m = 1;
        for (int i = 0; i < numBits; i++) m = (m << 1) + Bit(off + m);
        return m - (1u << numBits);
    }

    unsigned TreeReverse(size_t off, int numBits)
    {
        unsigned m = 1, symbol = 0;
        for (int i = 0; i < numBits; i++) {
            unsigned b = Bit(off + m);
            m = (m << 1) + b;
            symbol |= b << i;
        }
        return symbol;
    }

    unsigned Length(size_t choice, size_t choice2, size_t low, size_t mid,
                    size_t high, unsigned posState)
    {
        if (Bit(choice) == 0)  return Tree(low + (posState << 3), 3);
        if (Bit(choice2) == 0) return 8 + Tree(mid + (posState << 3), 3);
        return 16 + Tree(high, 8);
    }

    bool Run()
    {
        uint32_t state = 0, rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
        uint32_t pbMask = (1u << pb_) - 1;
        uint32_t lpMask = (1u << lp_) - 1;

        while (outPos_ < outLen_) {
            uint32_t posState = (uint32_t)outPos_ & pbMask;

            if (Bit(oIsMatch + (state << kNumPosBitsMax) + posState) == 0) {
                unsigned prev = outPos_ > 0 ? out_[outPos_ - 1] : 0;
                size_t litIdx = oLiteral + 0x300 *
                    ((((uint32_t)outPos_ & lpMask) << lc_) + (prev >> (8 - lc_)));
                unsigned symbol = 1;

                if (state >= 7) {
                    if (outPos_ < (size_t)rep0 + 1) return false;
                    unsigned matchByte = out_[outPos_ - rep0 - 1];
                    unsigned offs = 0x100;
                    while (symbol < 0x100) {
                        matchByte = (matchByte << 1) & 0x1FF;
                        unsigned matchBit = matchByte & offs;
                        unsigned b = Bit(litIdx + offs + matchBit + symbol);
                        symbol = (symbol << 1) | b;
                        offs = b ? (offs & matchBit) : (offs & ~matchBit);
                    }
                } else {
                    while (symbol < 0x100) symbol = (symbol << 1) | Bit(litIdx + symbol);
                }

                out_[outPos_++] = (uint8_t)(symbol & 0xFF);
                state = state < 4 ? 0 : (state < 10 ? state - 3 : state - 6);
                if (failed_) return false;
                continue;
            }

            unsigned len;

            if (Bit(oIsRep + state) != 0) {
                if (Bit(oIsRepG0 + state) == 0) {
                    if (Bit(oIsRep0Long + (state << kNumPosBitsMax) + posState) == 0) {
                        state = state < 7 ? 9 : 11;
                        if (outPos_ < (size_t)rep0 + 1) return false;
                        out_[outPos_] = out_[outPos_ - rep0 - 1];
                        outPos_++;
                        continue;
                    }
                } else {
                    uint32_t dist;
                    if (Bit(oIsRepG1 + state) == 0) {
                        dist = rep1;
                    } else {
                        if (Bit(oIsRepG2 + state) == 0) dist = rep2;
                        else { dist = rep3; rep3 = rep2; }
                        rep2 = rep1;
                    }
                    rep1 = rep0;
                    rep0 = dist;
                }
                len = Length(oRepChoice, oRepChoice2, oRepLow, oRepMid, oRepHigh, posState);
                state = state < 7 ? 8 : 11;
            } else {
                rep3 = rep2; rep2 = rep1; rep1 = rep0;
                len = Length(oLenChoice, oLenChoice2, oLenLow, oLenMid, oLenHigh, posState);
                state = state < 7 ? 7 : 10;

                unsigned lenToPos = len < kLenToPosStates ? len : kLenToPosStates - 1;
                unsigned slot = Tree(oPosSlot + (lenToPos << 6), 6);

                if (slot < (unsigned)kStartPosModel) {
                    rep0 = slot;
                } else {
                    int nDirect = (int)(slot >> 1) - 1;
                    rep0 = (2 | (slot & 1)) << nDirect;
                    if (slot < (unsigned)kEndPosModel) {
                        rep0 += TreeReverse(oSpecPos + rep0 - slot - 1, nDirect);
                    } else {
                        rep0 += DirectBits(nDirect - kNumAlignBits) << kNumAlignBits;
                        rep0 += TreeReverse(oAlign, kNumAlignBits);
                        if (rep0 == 0xFFFFFFFFu) return true;
                    }
                }
            }

            if (failed_) return false;
            if (outPos_ < (size_t)rep0 + 1) return false;

            size_t n = len + kMatchMinLen;
            if (n > outLen_ - outPos_) n = outLen_ - outPos_;
            for (size_t i = 0; i < n; i++) {
                out_[outPos_] = out_[outPos_ - rep0 - 1];
                outPos_++;
            }
        }

        return !failed_;
    }
};

// Decodes a lump 17-byte lzma_header_t ('LZMA',
// uncompressed size, packed size, five property bytes).
inline bool DecodeLump(const uint8_t* p, size_t len, std::vector<uint8_t>& out)
{
    out.clear();
    if (!p || len < 17 || std::memcmp(p, "LZMA", 4) != 0) return false;

    uint32_t actual = 0, packed = 0;
    std::memcpy(&actual, p + 4, 4);
    std::memcpy(&packed, p + 8, 4);

    if (actual == 0 || (size_t)packed + 17 > len) return false;
    if (actual > (256u << 20)) return false;

    out.resize(actual);
    LzmaDecoder dec;
    if (!dec.Decode(p + 12, p + 17, packed, out.data(), actual)) {
        out.clear();
        return false;
    }
    return true;
}

}
