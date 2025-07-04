/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkShaper_DEFINED
#define SkShaper_DEFINED

#include "include/core/SkFont.h"
#include "include/core/SkFourByteTag.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkString.h"
#include "include/core/SkTextBlob.h"
#include "include/core/SkTypes.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

class SkFontStyle;

#if defined(SK_DISABLE_LEGACY_SKSHAPER_FUNCTIONS)
class SkFontMgr;
#else
#include "include/core/SkFontMgr.h"
#endif

#ifdef ENABLE_DRAWING_ADAPTER
#include "modules/skparagraph/include/drawing.h"
#endif

#if !defined(SKSHAPER_IMPLEMENTATION)
    #define SKSHAPER_IMPLEMENTATION 0
#endif

#if !defined(SKSHAPER_API)
    #if defined(SKSHAPER_DLL)
        #if defined(_MSC_VER)
            #if SKSHAPER_IMPLEMENTATION
                #define SKSHAPER_API __declspec(dllexport)
            #else
                #define SKSHAPER_API __declspec(dllimport)
            #endif
        #else
            #define SKSHAPER_API __attribute__((visibility("default")))
        #endif
    #else
        #define SKSHAPER_API
    #endif
#endif
#ifdef ENABLE_DRAWING_ADAPTER
namespace SkiaRsText { 
#endif
class SKSHAPER_API SkShaper {
public:
#if !defined(SK_DISABLE_LEGACY_SKSHAPER_FUNCTIONS)
    static std::unique_ptr<SkShaper> MakePrimitive();

#if defined(SK_SHAPER_HARFBUZZ_AVAILABLE)
#ifdef ENABLE_DRAWING_ADAPTER
    static std::unique_ptr<SkShaper> MakeShaperDrivenWrapper(std::shared_ptr<RSFontMgr> = nullptr);
    static std::unique_ptr<SkShaper> MakeShapeThenWrap(std::shared_ptr<RSFontMgr> = nullptr);
#else
    static std::unique_ptr<SkShaper> MakeShaperDrivenWrapper(sk_sp<SkFontMgr> fallback);
    static std::unique_ptr<SkShaper> MakeShapeThenWrap(sk_sp<SkFontMgr> fallback);
#endif
    static void PurgeHarfBuzzCache();
#endif

#if defined(SK_SHAPER_CORETEXT_AVAILABLE)
    static std::unique_ptr<SkShaper> MakeCoreText();
#endif

#ifdef ENABLE_DRAWING_ADAPTER
    static std::unique_ptr<SkShaper> Make(std::shared_ptr<RSFontMgr> = nullptr);
#else
    static std::unique_ptr<SkShaper> Make(sk_sp<SkFontMgr> fallback = nullptr);
#endif
    static void PurgeCaches();
#endif  // !defined(SK_DISABLE_LEGACY_SKSHAPER_FUNCTIONS)

    SkShaper();
    virtual ~SkShaper();

    class RunIterator {
    public:
        virtual ~RunIterator() = default;
        /** Set state to that of current run and move iterator to end of that run. */
        virtual void consume() = 0;
        /** Offset to one past the last (utf8) element in the current run. */
        virtual size_t endOfCurrentRun() const = 0;
        /** Return true if consume should no longer be called. */
        virtual bool atEnd() const = 0;
    };
    class FontRunIterator : public RunIterator {
    public:
#ifdef ENABLE_DRAWING_ADAPTER
        virtual const RSFont& currentFont() const = 0;
#else
        virtual const SkFont& currentFont() const = 0;
#endif
    };
    class BiDiRunIterator : public RunIterator {
    public:
        /** The unicode bidi embedding level (even ltr, odd rtl) */
        virtual uint8_t currentLevel() const = 0;
    };
    class ScriptRunIterator : public RunIterator {
    public:
        /** Should be iso15924 codes. */
        virtual SkFourByteTag currentScript() const = 0;
    };
    class LanguageRunIterator : public RunIterator {
    public:
        /** Should be BCP-47, c locale names may also work. */
        virtual const char* currentLanguage() const = 0;
    };
    struct Feature {
        SkFourByteTag tag;
        uint32_t value;
        size_t start; // Offset to the start (utf8) element of the run.
        size_t end;   // Offset to one past the last (utf8) element of the run.
    };

private:
    template <typename RunIteratorSubclass>
    class TrivialRunIterator : public RunIteratorSubclass {
    public:
        static_assert(std::is_base_of<RunIterator, RunIteratorSubclass>::value, "");
        TrivialRunIterator(size_t utf8Bytes) : fEnd(utf8Bytes), fAtEnd(fEnd == 0) {}
        void consume() override { SkASSERT(!fAtEnd); fAtEnd = true; }
        size_t endOfCurrentRun() const override { return fAtEnd ? fEnd : 0; }
        bool atEnd() const override { return fAtEnd; }
    private:
        size_t fEnd;
        bool fAtEnd;
    };

public:
#ifdef ENABLE_DRAWING_ADAPTER
    static std::unique_ptr<FontRunIterator>
    MakeFontMgrRunIterator(const char* utf8, size_t utf8Bytes,
                        const RSFont& font, std::shared_ptr<RSFontMgr> fallback);
    static std::unique_ptr<SkShaper::FontRunIterator>
    MakeFontMgrRunIterator(const char* utf8, size_t utf8Bytes,
                        const RSFont& font, std::shared_ptr<RSFontMgr> fallback,
                        const char* requestName, RSFontStyle requestStyle,
                        const SkShaper::LanguageRunIterator*);
    class TrivialFontRunIterator : public TrivialRunIterator<FontRunIterator> {
    public:
        TrivialFontRunIterator(const RSFont& font, size_t utf8Bytes)
            : TrivialRunIterator(utf8Bytes), fFont(font) {}
        const RSFont& currentFont() const override { return fFont; }
    private:
        RSFont fFont;
    };
#else
    static std::unique_ptr<FontRunIterator>
    MakeFontMgrRunIterator(const char* utf8, size_t utf8Bytes,
                           const SkFont& font, sk_sp<SkFontMgr> fallback);
    static std::unique_ptr<SkShaper::FontRunIterator>
    MakeFontMgrRunIterator(const char* utf8, size_t utf8Bytes,
                           const SkFont& font, sk_sp<SkFontMgr> fallback,
                           const char* requestName, SkFontStyle requestStyle,
                           const SkShaper::LanguageRunIterator*);
    class TrivialFontRunIterator : public TrivialRunIterator<FontRunIterator> {
    public:
        TrivialFontRunIterator(const SkFont& font, size_t utf8Bytes)
            : TrivialRunIterator(utf8Bytes), fFont(font) {}
        const SkFont& currentFont() const override { return fFont; }
    private:
        SkFont fFont;
    };
#endif

#if !defined(SK_DISABLE_LEGACY_SKSHAPER_FUNCTIONS)
    static std::unique_ptr<BiDiRunIterator>
    MakeBiDiRunIterator(const char* utf8, size_t utf8Bytes, uint8_t bidiLevel);
#if defined(SK_SHAPER_UNICODE_AVAILABLE)
    static std::unique_ptr<BiDiRunIterator>
    MakeIcuBiDiRunIterator(const char* utf8, size_t utf8Bytes, uint8_t bidiLevel);
#endif  // defined(SK_SHAPER_UNICODE_AVAILABLE)
#endif  // !defined(SK_DISABLE_LEGACY_SKSHAPER_FUNCTIONS)

    class TrivialBiDiRunIterator : public TrivialRunIterator<BiDiRunIterator> {
    public:
        TrivialBiDiRunIterator(uint8_t bidiLevel, size_t utf8Bytes)
            : TrivialRunIterator(utf8Bytes), fBidiLevel(bidiLevel) {}
        uint8_t currentLevel() const override { return fBidiLevel; }
    private:
        uint8_t fBidiLevel;
    };

#if !defined(SK_DISABLE_LEGACY_SKSHAPER_FUNCTIONS)
    static std::unique_ptr<ScriptRunIterator>
    MakeScriptRunIterator(const char* utf8, size_t utf8Bytes, SkFourByteTag script);
#if defined(SK_SHAPER_HARFBUZZ_AVAILABLE)
    static std::unique_ptr<ScriptRunIterator>
    MakeSkUnicodeHbScriptRunIterator(const char* utf8, size_t utf8Bytes);
    static std::unique_ptr<ScriptRunIterator>
    MakeSkUnicodeHbScriptRunIterator(const char* utf8, size_t utf8Bytes, SkFourByteTag script);
    // Still used in some cases
    static std::unique_ptr<ScriptRunIterator>
    MakeHbIcuScriptRunIterator(const char* utf8, size_t utf8Bytes);
#endif  // defined(SK_SHAPER_HARFBUZZ_AVAILABLE)
#endif  // !defined(SK_DISABLE_LEGACY_SKSHAPER_FUNCTIONS)

    class TrivialScriptRunIterator : public TrivialRunIterator<ScriptRunIterator> {
    public:
        TrivialScriptRunIterator(SkFourByteTag script, size_t utf8Bytes)
            : TrivialRunIterator(utf8Bytes), fScript(script) {}
        SkFourByteTag currentScript() const override { return fScript; }
    private:
        SkFourByteTag fScript;
    };

    static std::unique_ptr<LanguageRunIterator>
    MakeStdLanguageRunIterator(const char* utf8, size_t utf8Bytes);
    class TrivialLanguageRunIterator : public TrivialRunIterator<LanguageRunIterator> {
    public:
        TrivialLanguageRunIterator(const char* language, size_t utf8Bytes)
            : TrivialRunIterator(utf8Bytes), fLanguage(language) {}
        const char* currentLanguage() const override { return fLanguage.c_str(); }
    private:
        SkString fLanguage;
    };

    class RunHandler {
    public:
        virtual ~RunHandler() = default;

        struct Range {
            constexpr Range() : fBegin(0), fSize(0) {}
            constexpr Range(size_t begin, size_t size) : fBegin(begin), fSize(size) {}
            size_t fBegin;
            size_t fSize;
            constexpr size_t begin() const { return fBegin; }
            constexpr size_t end() const { return begin() + size(); }
            constexpr size_t size() const { return fSize; }
        };

        struct RunInfo {
#ifdef ENABLE_DRAWING_ADAPTER
            const RSFont& fFont;
#else
            const SkFont& fFont;
#endif
            uint8_t fBidiLevel;
            SkVector fAdvance;
            size_t glyphCount;
            Range utf8Range;
        };

        struct Buffer {
            SkGlyphID* glyphs;  // required
            SkPoint* positions; // required, if (!offsets) put glyphs[i] at positions[i]
                                //           if ( offsets) positions[i+1]-positions[i] are advances
            SkPoint* offsets;   // optional, if ( offsets) put glyphs[i] at positions[i]+offsets[i]
            uint32_t* clusters; // optional, utf8+clusters[i] starts run which produced glyphs[i]
            SkPoint point;      // offset to add to all positions
#ifdef ENABLE_TEXT_ENHANCE
            SkPoint* advances;
#endif
        };

        /** Called when beginning a line. */
        virtual void beginLine() = 0;

        /** Called once for each run in a line. Can compute baselines and offsets. */
        virtual void runInfo(const RunInfo&) = 0;

        /** Called after all runInfo calls for a line. */
        virtual void commitRunInfo() = 0;

        /** Called for each run in a line after commitRunInfo. The buffer will be filled out. */
        virtual Buffer runBuffer(const RunInfo&) = 0;

        /** Called after each runBuffer is filled out. */
        virtual void commitRunBuffer(const RunInfo&) = 0;

        /** Called when ending a line. */
        virtual void commitLine() = 0;
    };

#if !defined(SK_DISABLE_LEGACY_SKSHAPER_FUNCTIONS)
    virtual void shape(const char* utf8, size_t utf8Bytes,
#ifdef ENABLE_DRAWING_ADAPTER
                       const RSFont& srcFont,
#else
                        const SkFont& srcFont,
#endif
                       bool leftToRight,
                       SkScalar width,
                       RunHandler*) const = 0;

    virtual void shape(const char* utf8, size_t utf8Bytes,
                       FontRunIterator&,
                       BiDiRunIterator&,
                       ScriptRunIterator&,
                       LanguageRunIterator&,
                       SkScalar width,
                       RunHandler*) const = 0;
#endif
    virtual void shape(const char* utf8,
                       size_t utf8Bytes,
                       FontRunIterator&,
                       BiDiRunIterator&,
                       ScriptRunIterator&,
                       LanguageRunIterator&,
                       const Feature* features,
                       size_t featuresSize,
                       SkScalar width,
                       RunHandler*) const = 0;

private:
    SkShaper(const SkShaper&) = delete;
    SkShaper& operator=(const SkShaper&) = delete;
};

#ifndef ENABLE_DRAWING_ADAPTER
/**
 * Helper for shaping text directly into a SkTextBlob.
 */
class SKSHAPER_API SkTextBlobBuilderRunHandler final : public SkShaper::RunHandler {
public:
    SkTextBlobBuilderRunHandler(const char* utf8Text, SkPoint offset)
        : fUtf8Text(utf8Text)
        , fOffset(offset) {}
    sk_sp<SkTextBlob> makeBlob();
    SkPoint endPoint() { return fOffset; }

    void beginLine() override;
    void runInfo(const RunInfo&) override;
    void commitRunInfo() override;
    Buffer runBuffer(const RunInfo&) override;
    void commitRunBuffer(const RunInfo&) override;
    void commitLine() override;

private:
    SkTextBlobBuilder fBuilder;
    char const * const fUtf8Text;
    uint32_t* fClusters;
    int fClusterOffset;
    int fGlyphCount;
    SkScalar fMaxRunAscent;
    SkScalar fMaxRunDescent;
    SkScalar fMaxRunLeading;
    SkPoint fCurrentPosition;
    SkPoint fOffset;
};
#endif

namespace SkShapers::Primitive {
SKSHAPER_API std::unique_ptr<SkShaper> PrimitiveText();

SKSHAPER_API std::unique_ptr<SkShaper::BiDiRunIterator> TrivialBiDiRunIterator
                                              (size_t utf8Bytes,  uint8_t bidiLevel);
SKSHAPER_API std::unique_ptr<SkShaper::ScriptRunIterator> TrivialScriptRunIterator
                                              (size_t utf8Bytes, SkFourByteTag scriptTag);
}  // namespace SkShapers
#ifdef ENABLE_DRAWING_ADAPTER
}
using SkShaper = SkiaRsText::SkShaper;
// namespace SkShapers::Primitive = SkiaRsText::SkShapers::Primitive;
namespace SkShapers::Primitive{
    using namespace SkiaRsText::SkShapers::Primitive;;
}
#endif
#endif  // SkShaper_DEFINED
