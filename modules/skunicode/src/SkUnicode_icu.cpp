/*
* Copyright 2020 Google Inc.
*
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include "include/core/SkString.h"
#include "include/core/SkTypes.h"
#include "include/private/SkBitmaskEnum.h"
#include "include/private/SkMutex.h"
#include "include/private/SkOnce.h"
#include "include/private/SkTArray.h"
#include "include/private/SkTemplates.h"
#include "include/private/SkTo.h"
#include "modules/skunicode/include/SkUnicode.h"
#include "modules/skunicode/src/SkUnicode_icu.h"
#include "modules/skunicode/src/SkUnicode_icu_bidi.h"
#include "src/utils/SkUTF.h"
#include "include/private/SkTHash.h"
#include <unicode/umachine.h>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#ifdef OHOS_SUPPORT
#include <unordered_set>
#endif

#if defined(SK_USING_THIRD_PARTY_ICU)
#include "SkLoadICU.h"
#endif

static const SkICULib* ICULib() {
    static const auto gICU = SkLoadICULib();

    return gICU.get();
}

// sk_* wrappers for ICU funcs
#define SKICU_FUNC(funcname)                                                                \
    template <typename... Args>                                                             \
    auto sk_##funcname(Args&&... args) -> decltype(funcname(std::forward<Args>(args)...)) { \
        return ICULib()->f_##funcname(std::forward<Args>(args)...);                         \
    }                                                                                       \

SKICU_EMIT_FUNCS
#undef SKICU_FUNC

const char* SkUnicode_IcuBidi::errorName(UErrorCode status) {
    return sk_u_errorName(status);
}

void SkUnicode_IcuBidi::bidi_close(UBiDi* bidi) {
    sk_ubidi_close(bidi);
}
UBiDiDirection SkUnicode_IcuBidi::bidi_getDirection(const UBiDi* bidi) {
    return sk_ubidi_getDirection(bidi);
}
SkBidiIterator::Position SkUnicode_IcuBidi::bidi_getLength(const UBiDi* bidi) {
    return sk_ubidi_getLength(bidi);
}
SkBidiIterator::Level SkUnicode_IcuBidi::bidi_getLevelAt(const UBiDi* bidi, int pos) {
    return sk_ubidi_getLevelAt(bidi, pos);
}
UBiDi* SkUnicode_IcuBidi::bidi_openSized(int32_t maxLength, int32_t maxRunCount, UErrorCode* pErrorCode) {
    return sk_ubidi_openSized(maxLength, maxRunCount, pErrorCode);
}
void SkUnicode_IcuBidi::bidi_setPara(UBiDi* bidi,
                         const UChar* text,
                         int32_t length,
                         UBiDiLevel paraLevel,
                         UBiDiLevel* embeddingLevels,
                         UErrorCode* status) {
    return sk_ubidi_setPara(bidi, text, length, paraLevel, embeddingLevels, status);
}
void SkUnicode_IcuBidi::bidi_reorderVisual(const SkUnicode::BidiLevel runLevels[],
                               int levelsCount,
                               int32_t logicalFromVisual[]) {
    sk_ubidi_reorderVisual(runLevels, levelsCount, logicalFromVisual);
}

static inline UBreakIterator* sk_ubrk_clone(const UBreakIterator* bi, UErrorCode* status) {
    const auto* icu = ICULib();
    SkASSERT(icu->f_ubrk_clone_ || icu->f_ubrk_safeClone_);
    return icu->f_ubrk_clone_
        ? icu->f_ubrk_clone_(bi, status)
        : icu->f_ubrk_safeClone_(bi, nullptr, nullptr, status);
}

static UText* utext_close_wrapper(UText* ut) {
    return sk_utext_close(ut);
}
static void ubrk_close_wrapper(UBreakIterator* bi) {
    sk_ubrk_close(bi);
}

using ICUUText = std::unique_ptr<UText, SkFunctionWrapper<decltype(utext_close),
                                                         utext_close_wrapper>>;
using ICUBreakIterator = std::unique_ptr<UBreakIterator, SkFunctionWrapper<decltype(ubrk_close),
                                                                           ubrk_close_wrapper>>;
/** Replaces invalid utf-8 sequences with REPLACEMENT CHARACTER U+FFFD. */
static inline SkUnichar utf8_next(const char** ptr, const char* end) {
    SkUnichar val = SkUTF::NextUTF8(ptr, end);
    return val < 0 ? 0xFFFD : val;
}

static UBreakIteratorType convertType(SkUnicode::BreakType type) {
    switch (type) {
        case SkUnicode::BreakType::kLines: return UBRK_LINE;
        case SkUnicode::BreakType::kGraphemes: return UBRK_CHARACTER;
        case SkUnicode::BreakType::kWords: return UBRK_WORD;
        default:
            return UBRK_CHARACTER;
    }
}

class SkBreakIterator_icu : public SkBreakIterator {
    ICUBreakIterator fBreakIterator;
    Position fLastResult;
 public:
    explicit SkBreakIterator_icu(ICUBreakIterator iter)
            : fBreakIterator(std::move(iter))
            , fLastResult(0) {}
    Position first() override { return fLastResult = sk_ubrk_first(fBreakIterator.get()); }
    Position current() override { return fLastResult = sk_ubrk_current(fBreakIterator.get()); }
    Position next() override { return fLastResult = sk_ubrk_next(fBreakIterator.get()); }
    Status status() override { return sk_ubrk_getRuleStatus(fBreakIterator.get()); }
    bool isDone() override { return fLastResult == UBRK_DONE; }

    bool setText(const char utftext8[], int utf8Units) override {
        UErrorCode status = U_ZERO_ERROR;
        ICUUText text(sk_utext_openUTF8(nullptr, &utftext8[0], utf8Units, &status));

        if (U_FAILURE(status)) {
            SkDEBUGF("Break error: %s", sk_u_errorName(status));
            return false;
        }
        SkASSERT(text);
        sk_ubrk_setUText(fBreakIterator.get(), text.get(), &status);
        if (U_FAILURE(status)) {
            SkDEBUGF("Break error: %s", sk_u_errorName(status));
            return false;
        }
        fLastResult = 0;
        return true;
    }
    bool setText(const char16_t utftext16[], int utf16Units) override {
        UErrorCode status = U_ZERO_ERROR;
        ICUUText text(sk_utext_openUChars(nullptr, reinterpret_cast<const UChar*>(&utftext16[0]),
                                          utf16Units, &status));

        if (U_FAILURE(status)) {
            SkDEBUGF("Break error: %s", sk_u_errorName(status));
            return false;
        }
        SkASSERT(text);
        sk_ubrk_setUText(fBreakIterator.get(), text.get(), &status);
        if (U_FAILURE(status)) {
            SkDEBUGF("Break error: %s", sk_u_errorName(status));
            return false;
        }
        fLastResult = 0;
        return true;
    }
};

class SkIcuBreakIteratorCache {
    SkTHashMap<SkUnicode::BreakType, ICUBreakIterator> fBreakCache;
    SkMutex fBreakCacheMutex;

 public:
    static SkIcuBreakIteratorCache& get() {
        static SkIcuBreakIteratorCache instance;
        return instance;
    }

#ifdef OHOS_SUPPORT
    ICUBreakIterator makeBreakIterator(const char locale[], SkUnicode::BreakType type) {
        UErrorCode status = U_ZERO_ERROR;
        ICUBreakIterator* cachedIterator;
        {
            SkAutoMutexExclusive lock(fBreakCacheMutex);
            cachedIterator = fBreakCache.find(type);
            if (!cachedIterator) {
                ICUBreakIterator newIterator(sk_ubrk_open(convertType(type), locale, nullptr, 0, &status));
                if (U_FAILURE(status)) {
                    SkDEBUGF("Break error: %s", sk_u_errorName(status));
                } else {
                    cachedIterator = fBreakCache.set(type, std::move(newIterator));
                }
            }
        }
        ICUBreakIterator iterator;
        if (cachedIterator) {
            iterator.reset(sk_ubrk_clone(cachedIterator->get(), &status));
            if (U_FAILURE(status)) {
                SkDEBUGF("Break error: %s", sk_u_errorName(status));
            }
        }
        return iterator;
    }
#else
    ICUBreakIterator makeBreakIterator(SkUnicode::BreakType type) {
        UErrorCode status = U_ZERO_ERROR;
        ICUBreakIterator* cachedIterator;
        {
            SkAutoMutexExclusive lock(fBreakCacheMutex);
            cachedIterator = fBreakCache.find(type);
            if (!cachedIterator) {
                ICUBreakIterator newIterator(sk_ubrk_open(convertType(type), sk_uloc_getDefault(),
                                                          nullptr, 0, &status));
                if (U_FAILURE(status)) {
                    SkDEBUGF("Break error: %s", sk_u_errorName(status));
                } else {
                    cachedIterator = fBreakCache.set(type, std::move(newIterator));
                }
            }
        }
        ICUBreakIterator iterator;
        if (cachedIterator) {
            iterator.reset(sk_ubrk_clone(cachedIterator->get(), &status));
            if (U_FAILURE(status)) {
                SkDEBUGF("Break error: %s", sk_u_errorName(status));
            }
        }
        return iterator;
    }
#endif
};

class SkUnicode_icu : public SkUnicode {

    std::unique_ptr<SkUnicode> copy() override {
        return std::make_unique<SkUnicode_icu>();
    }

    static bool extractWords(uint16_t utf16[], int utf16Units, const char* locale,  std::vector<Position>* words) {

        UErrorCode status = U_ZERO_ERROR;

#ifdef OHOS_SUPPORT
        ICUBreakIterator iterator = SkIcuBreakIteratorCache::get().makeBreakIterator(locale, BreakType::kWords);
#else
        ICUBreakIterator iterator = SkIcuBreakIteratorCache::get().makeBreakIterator(BreakType::kWords);
#endif
        if (!iterator) {
            SkDEBUGF("Break error: %s", sk_u_errorName(status));
            return false;
        }
        SkASSERT(iterator);

        ICUUText utf16UText(sk_utext_openUChars(nullptr, (UChar*)utf16, utf16Units, &status));
        if (U_FAILURE(status)) {
            SkDEBUGF("Break error: %s", sk_u_errorName(status));
            return false;
        }

        sk_ubrk_setUText(iterator.get(), utf16UText.get(), &status);
        if (U_FAILURE(status)) {
            SkDEBUGF("Break error: %s", sk_u_errorName(status));
            return false;
        }

        // Get the words
        int32_t pos = sk_ubrk_first(iterator.get());
        while (pos != UBRK_DONE) {
            words->emplace_back(pos);
            pos = sk_ubrk_next(iterator.get());
        }

        return true;
    }

    static bool extractPositions
#ifdef OHOS_SUPPORT
        (const char utf8[], int utf8Units, BreakType type, const char locale[],
            std::function<void(int, int)> setBreak) {
#else
        (const char utf8[], int utf8Units, BreakType type, std::function<void(int, int)> setBreak) {
#endif

        UErrorCode status = U_ZERO_ERROR;
        ICUUText text(sk_utext_openUTF8(nullptr, &utf8[0], utf8Units, &status));

        if (U_FAILURE(status)) {
            SkDEBUGF("Break error: %s", sk_u_errorName(status));
            return false;
        }
        SkASSERT(text);

#ifdef OHOS_SUPPORT
        ICUBreakIterator iterator = SkIcuBreakIteratorCache::get().makeBreakIterator(locale, type);
#else
        ICUBreakIterator iterator = SkIcuBreakIteratorCache::get().makeBreakIterator(type);
#endif
        if (!iterator) {
            return false;
        }

        sk_ubrk_setUText(iterator.get(), text.get(), &status);
        if (U_FAILURE(status)) {
            SkDEBUGF("Break error: %s", sk_u_errorName(status));
            return false;
        }

        auto iter = iterator.get();
        int32_t pos = sk_ubrk_first(iter);
        while (pos != UBRK_DONE) {
            int s = type == SkUnicode::BreakType::kLines
                        ? UBRK_LINE_SOFT
                        : sk_ubrk_getRuleStatus(iter);
            setBreak(pos, s);
            pos = sk_ubrk_next(iter);
        }

        if (type == SkUnicode::BreakType::kLines) {
            // This is a workaround for https://bugs.chromium.org/p/skia/issues/detail?id=10715
            // (ICU line break iterator does not work correctly on Thai text with new lines)
            // So, we only use the iterator to collect soft line breaks and
            // scan the text for all hard line breaks ourselves
            const char* end = utf8 + utf8Units;
            const char* ch = utf8;
            while (ch < end) {
                auto unichar = utf8_next(&ch, end);
                if (isHardLineBreak(unichar)) {
                    setBreak(ch - utf8, UBRK_LINE_HARD);
                }
            }
        }
        return true;
    }

    static bool isControl(SkUnichar utf8) {
        return sk_u_iscntrl(utf8);
    }

    static bool isWhitespace(SkUnichar utf8) {
        return sk_u_isWhitespace(utf8);
    }

    static bool isSpace(SkUnichar utf8) {
        return sk_u_isspace(utf8);
    }

    static bool isTabulation(SkUnichar utf8) {
        return utf8 == '\t';
    }

    static bool isHardBreak(SkUnichar utf8) {
        auto property = sk_u_getIntPropertyValue(utf8, UCHAR_LINE_BREAK);
        return property == U_LB_LINE_FEED || property == U_LB_MANDATORY_BREAK;
    }

    static bool isIdeographic(SkUnichar unichar) {
        return sk_u_hasBinaryProperty(unichar, UCHAR_IDEOGRAPHIC);
    }
#ifdef OHOS_SUPPORT
    static bool isPunctuation(SkUnichar unichar)
    {
        if (sk_u_ispunct(unichar)) {
            return true;
        }
        static constexpr std::array<std::pair<SkUnichar, SkUnichar>, 13> ranges{{
                {0x0021, 0x002F},  // ASCII punctuation (e.g., ! " # $ % & ' ( ) * + , - . /)
                {0x003A, 0x0040},  // ASCII punctuation (e.g., : ; < = > ? @)
                {0x005B, 0x0060},  // ASCII punctuation (e.g., [ \ ] ^ _ `)
                {0x007B, 0x007E},  // ASCII punctuation (e.g., { | } ~)
                {0x2000, 0x206F},  // Common punctuation (Chinese & English)
                {0xFF00, 0xFFEF},  // Full-width characters and symbols
                {0x2E00, 0x2E7F},  // Supplemental punctuation (e.g., ancient)
                {0x3001, 0x3003},  // CJK punctuation (e.g., Chinese comma)
                {0xFF01, 0xFF0F},  // Full-width ASCII punctuation (0x21-0x2F)
                {0xFF1A, 0xFF20},  // Full-width ASCII punctuation (0x3A-0x40)
                {0xFF3B, 0xFF40},  // Full-width ASCII punctuation (0x5B-0x60)
                {0xFF5B, 0xFF65},  // Other full-width punctuation (e.g., quotes)
        }};
        for (auto range : ranges) {
            if (range.first <= unichar && unichar <= range.second) {
                return true;
            }
        }
        return false;
    }
    static bool isEllipsis(SkUnichar unichar) { return (unichar == 0x2026 || unichar == 0x002E); }
    static bool isGraphemeExtend(SkUnichar unichar) {
        return sk_u_hasBinaryProperty(unichar, UCHAR_GRAPHEME_EXTEND);
    }
    static bool isCustomSoftBreak(SkUnichar unichar) {
        // ‘ “ ( [ { < « — – • – – $ £ € + = × \ % ° # * @ _ § © ®
        static const std::unordered_set<SkUnichar> kBreakTriggerCodePoints {
            0x2018, 0x201C, 0x0028, 0x005B, 0x007B, 0x003C, 0x00AB, 0x2014, 0x2013,
            0x2022, 0x0024, 0x00A3, 0x20AC, 0x002B, 0x003D, 0x00D7, 0x005C, 0x0025,
            0x00B0, 0x0023, 0x002A, 0x0040, 0x005F, 0x00A7, 0x00A9, 0x00AE
        };

        return kBreakTriggerCodePoints.count(unichar) > 0;
    }
#endif

public:
    ~SkUnicode_icu() override { }
    std::unique_ptr<SkBidiIterator> makeBidiIterator(const uint16_t text[], int count,
                                                     SkBidiIterator::Direction dir) override {
        return SkUnicode::makeBidiIterator(text, count, dir);
    }
    std::unique_ptr<SkBidiIterator> makeBidiIterator(const char text[],
                                                     int count,
                                                     SkBidiIterator::Direction dir) override {
        return SkUnicode::makeBidiIterator(text, count, dir);
    }
    std::unique_ptr<SkBreakIterator> makeBreakIterator(const char locale[],
                                                       BreakType breakType) override {
        UErrorCode status = U_ZERO_ERROR;
        ICUBreakIterator iterator(sk_ubrk_open(convertType(breakType), locale, nullptr, 0,
                                               &status));
        if (U_FAILURE(status)) {
            SkDEBUGF("Break error: %s", sk_u_errorName(status));
            return nullptr;
        }
        return std::unique_ptr<SkBreakIterator>(new SkBreakIterator_icu(std::move(iterator)));
    }
    std::unique_ptr<SkBreakIterator> makeBreakIterator(BreakType breakType) override {
        return makeBreakIterator(sk_uloc_getDefault(), breakType);
    }

    static bool isHardLineBreak(SkUnichar utf8) {
        auto property = sk_u_getIntPropertyValue(utf8, UCHAR_LINE_BREAK);
        return property == U_LB_LINE_FEED || property == U_LB_MANDATORY_BREAK;
    }

    SkString toUpper(const SkString& str) override {
        // Convert to UTF16 since that's what ICU wants.
        auto str16 = SkUnicode::convertUtf8ToUtf16(str.c_str(), str.size());

        UErrorCode icu_err = U_ZERO_ERROR;
        const auto upper16len = sk_u_strToUpper(nullptr, 0, (UChar*)(str16.c_str()), str16.size(),
                                                nullptr, &icu_err);
        if (icu_err != U_BUFFER_OVERFLOW_ERROR || upper16len <= 0) {
            return SkString();
        }

        SkAutoSTArray<128, uint16_t> upper16(upper16len);
        icu_err = U_ZERO_ERROR;
        sk_u_strToUpper((UChar*)(upper16.get()), SkToS32(upper16.size()),
                        (UChar*)(str16.c_str()), str16.size(),
                        nullptr, &icu_err);
        SkASSERT(!U_FAILURE(icu_err));

        // ... and back to utf8 'cause that's what we want.
        return convertUtf16ToUtf8((char16_t*)upper16.get(), upper16.size());
    }

    bool getBidiRegions(const char utf8[],
                        int utf8Units,
                        TextDirection dir,
                        std::vector<BidiRegion>* results) override {
        return SkUnicode::extractBidi(utf8, utf8Units, dir, results);
    }

    bool getWords(const char utf8[], int utf8Units, const char* locale, std::vector<Position>* results) override {

        // Convert to UTF16 since we want the results in utf16
        auto utf16 = convertUtf8ToUtf16(utf8, utf8Units);
        return SkUnicode_icu::extractWords((uint16_t*)utf16.c_str(), utf16.size(), locale, results);
    }

#ifdef OHOS_SUPPORT
    void processPunctuationAndEllipsis(SkTArray<SkUnicode::CodeUnitFlags, true>* results, int i, SkUnichar unichar)
    {
        if (SkUnicode_icu::isPunctuation(unichar)) {
            results->at(i) |= SkUnicode::kPunctuation;
        }
        if (SkUnicode_icu::isEllipsis(unichar)) {
            results->at(i) |= SkUnicode::kEllipsis;
        }
        if (SkUnicode_icu::isCustomSoftBreak(unichar)) {
            results->at(i) |= SkUnicode::kSoftLineBreakBefore;
        }
    }
#endif

#ifdef OHOS_SUPPORT
    bool computeCodeUnitFlags(char utf8[], int utf8Units, bool replaceTabs, const char locale[],
#else
    bool computeCodeUnitFlags(char utf8[], int utf8Units, bool replaceTabs,
#endif
                          SkTArray<SkUnicode::CodeUnitFlags, true>* results) override {
        results->reset();
        results->push_back_n(utf8Units + 1, CodeUnitFlags::kNoCodeUnitFlag);

#ifdef OHOS_SUPPORT
        SkUnicode_icu::extractPositions(utf8, utf8Units, BreakType::kLines, locale, [&](int pos, int status) {
#else
        SkUnicode_icu::extractPositions(utf8, utf8Units, BreakType::kLines, [&](int pos, int status) {
#endif
            (*results)[pos] |= status == UBRK_LINE_HARD
                                    ? CodeUnitFlags::kHardLineBreakBefore
                                    : CodeUnitFlags::kSoftLineBreakBefore;
        });

#ifdef OHOS_SUPPORT
        SkUnicode_icu::extractPositions(utf8, utf8Units, BreakType::kGraphemes, locale, [&](int pos, int status) {
#else
        SkUnicode_icu::extractPositions(utf8, utf8Units, BreakType::kGraphemes, [&](int pos, int status) {
#endif
            (*results)[pos] |= CodeUnitFlags::kGraphemeStart;
        });

        const char* current = utf8;
        const char* end = utf8 + utf8Units;
        while (current < end) {
            auto before = current - utf8;
            SkUnichar unichar = SkUTF::NextUTF8(&current, end);
            if (unichar < 0) unichar = 0xFFFD;
            auto after = current - utf8;
            if (replaceTabs && SkUnicode_icu::isTabulation(unichar)) {
                results->at(before) |= SkUnicode::kTabulation;
                if (replaceTabs) {
                    unichar = ' ';
                    utf8[before] = ' ';
                }
            }
            for (auto i = before; i < after; ++i) {
                if (SkUnicode_icu::isSpace(unichar)) {
                    results->at(i) |= SkUnicode::kPartOfIntraWordBreak;
                }
                if (SkUnicode_icu::isWhitespace(unichar)) {
                    results->at(i) |= SkUnicode::kPartOfWhiteSpaceBreak;
                }
                if (SkUnicode_icu::isControl(unichar)) {
                    results->at(i) |= SkUnicode::kControl;
                }
                if (SkUnicode_icu::isIdeographic(unichar)) {
                    results->at(i) |= SkUnicode::kIdeographic;
                }
#ifdef OHOS_SUPPORT
                processPunctuationAndEllipsis(results, i, unichar);
#endif
            }

#ifdef OHOS_SUPPORT
            if (SkUnicode_icu::isGraphemeExtend(unichar)) {
                // Current unichar is a combining one.
                results->at(before) |= SkUnicode::kCombine;
            }
#endif
        }

        return true;
    }

#ifdef OHOS_SUPPORT
    bool computeCodeUnitFlags(char16_t utf16[], int utf16Units, bool replaceTabs, const char locale[],
#else
    bool computeCodeUnitFlags(char16_t utf16[], int utf16Units, bool replaceTabs,
#endif
                          SkTArray<SkUnicode::CodeUnitFlags, true>* results) override {
        results->reset();
        results->push_back_n(utf16Units + 1, CodeUnitFlags::kNoCodeUnitFlag);

        // Get white spaces
        this->forEachCodepoint((char16_t*)&utf16[0], utf16Units,
           [results, replaceTabs, &utf16](SkUnichar unichar, int32_t start, int32_t end) {
                for (auto i = start; i < end; ++i) {
                    if (replaceTabs && SkUnicode_icu::isTabulation(unichar)) {
                        results->at(i) |= SkUnicode::kTabulation;
                    if (replaceTabs) {
                            unichar = ' ';
                            utf16[start] = ' ';
                        }
                    }
                    if (SkUnicode_icu::isSpace(unichar)) {
                        results->at(i) |= SkUnicode::kPartOfIntraWordBreak;
                    }
                    if (SkUnicode_icu::isWhitespace(unichar)) {
                        results->at(i) |= SkUnicode::kPartOfWhiteSpaceBreak;
                    }
                    if (SkUnicode_icu::isControl(unichar)) {
                        results->at(i) |= SkUnicode::kControl;
                    }
                }
           });
        // Get graphemes
        this->forEachBreak((char16_t*)&utf16[0],
                           utf16Units,
                           SkUnicode::BreakType::kGraphemes,
#ifdef OHOS_SUPPORT
                           locale,
#endif
                           [results](SkBreakIterator::Position pos, SkBreakIterator::Status) {
                               (*results)[pos] |= CodeUnitFlags::kGraphemeStart;
                           });
        // Get line breaks
        this->forEachBreak(
                (char16_t*)&utf16[0],
                utf16Units,
                SkUnicode::BreakType::kLines,
#ifdef OHOS_SUPPORT
                locale,
#endif
                [results](SkBreakIterator::Position pos, SkBreakIterator::Status status) {
                    if (status ==
                        (SkBreakIterator::Status)SkUnicode::LineBreakType::kHardLineBreak) {
                        // Hard line breaks clears off all the other flags
                        // TODO: Treat \n as a formatting mark and do not pass it to SkShaper
                        (*results)[pos-1] = CodeUnitFlags::kHardLineBreakBefore;
                    } else {
                        (*results)[pos] |= CodeUnitFlags::kSoftLineBreakBefore;
                    }
                });

        return true;
    }

    void reorderVisual(const BidiLevel runLevels[],
                       int levelsCount,
                       int32_t logicalFromVisual[]) override {
        SkUnicode_IcuBidi::bidi_reorderVisual(runLevels, levelsCount, logicalFromVisual);
    }
};

std::unique_ptr<SkUnicode> SkUnicode::MakeIcuBasedUnicode() {
    #if defined(SK_USING_THIRD_PARTY_ICU)
    if (!SkLoadICU()) {
        static SkOnce once;
        once([] { SkDEBUGF("SkLoadICU() failed!\n"); });
        return nullptr;
    }
    #endif

    return ICULib()
        ? std::make_unique<SkUnicode_icu>()
        : nullptr;
}
