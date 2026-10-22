// Copyright 2019 Google LLC.
#ifndef LineBreaker_DEFINED
#define LineBreaker_DEFINED

#include <functional>  // std::function
#include <queue>
#include "include/core/SkSpan.h"
#include "modules/skparagraph/include/TextStyle.h"
#include "modules/skparagraph/src/ParagraphImpl.h"
#include "modules/skparagraph/src/Run.h"

namespace skia {
namespace textlayout {

class ParagraphImpl;

// Aggregates the shaping/geometry scalars passed to createRunFromFont so the
// fast path run constructor stays within the parameter-count guideline.
struct FastPathRunParams {
    SkScalar heightMultiplier;
    bool useHalfLeading;
    SkScalar baselineShift;
    SkScalar offsetX;
};

// Aggregates the glyph IDs and advances produced by computeFastPathGlyphs so
// fillRunGeometry stays within the parameter-count guideline.
struct FastPathGlyphRun {
    const skia_private::STArray<64, SkGlyphID, true>& glyphs;
    const skia_private::STArray<64, SkScalar, true>& widths;
};

class OneLineShaper : public SkShaper::RunHandler {
public:
    explicit OneLineShaper(ParagraphImpl* paragraph)
        : fParagraph(paragraph)
        , fHeight(0.0f)
        , fUseHalfLeading(false)
        , fBaselineShift(0.0f)
        , fAdvance(SkPoint::Make(0.0f, 0.0f))
        , fUnresolvedGlyphs(0)
        , fUniqueRunId(paragraph->fRuns.size()){ }

    bool shape();

    size_t unresolvedGlyphs() { return fUnresolvedGlyphs; }

    static SkUnichar getEmojiSequenceStart(SkUnicode* unicode, const char** begin, const char* end);

#ifdef ENABLE_TEXT_ENHANCE
    bool shapeFastPath(const FastPathSegment& segment, uint8_t bidiLevel, SkScalar& advanceX);
    std::shared_ptr<RSTypeface> findFastPathTypeface(const std::vector<SkString>& familyNames,
                                                     const TextStyle& style);
    std::shared_ptr<RSTypeface> findFastPathTypeface(const char* familyName, const TextStyle& style);
    RSFont createFastPathFont(std::shared_ptr<RSTypeface> typeface, const TextStyle& style,
                              const ScaleParam& scaleParam);
    Run* createRunFromFont(const RSFont& font,
                           const SkString& fullText,
                           TextRange textRange,
                           uint8_t bidiLevel,
                           const FastPathRunParams& params);
    void resolveFastPathTypeface(const FastPathSegment& segment, const TextStyle& style,
                                 std::shared_ptr<RSTypeface>& typeface, ScaleParam& scaleParam);
    void fillRunGeometry(Run* run, const FastPathGlyphRun& glyphs,
                         const char* textStart, size_t textLen, SkScalar offsetX);
    void calculateClusterIndexes(Run* run, const char* textStart, size_t textLen);
#endif

private:

    struct RunBlock {
        RunBlock() : fRun(nullptr) { }

        // First unresolved block
        explicit RunBlock(TextRange text) : fRun(nullptr), fText(text) { }

        RunBlock(std::shared_ptr<Run> run, TextRange text, GlyphRange glyphs, size_t score)
            : fRun(std::move(run))
            , fText(text)
            , fGlyphs(glyphs) { }

        // Entire run comes as one block fully resolved
        explicit RunBlock(std::shared_ptr<Run> run)
            : fRun(std::move(run))
            , fText(fRun->fTextRange)
            , fGlyphs(GlyphRange(0, fRun->size())) { }

        std::shared_ptr<Run> fRun;
        TextRange fText;
        GlyphRange fGlyphs;
#ifdef ENABLE_TEXT_ENHANCE
        bool fIsZeroWidth{false};  // Mark zero-width characters to skip fallback
#endif
        bool isFullyResolved() { return fRun != nullptr && fGlyphs.width() == fRun->size(); }
    };

    using ShapeVisitor =
            std::function<SkScalar(TextRange textRange, SkSpan<Block>, SkScalar&, TextIndex, uint8_t)>;
    bool iterateThroughShapingRegions(const ShapeVisitor& shape);

    using ShapeSingleFontVisitor =
            std::function<void(Block, skia_private::TArray<SkShaper::Feature>)>;
    void iterateThroughFontStyles(
            TextRange textRange, SkSpan<Block> styleSpan, const ShapeSingleFontVisitor& visitor);

    enum Resolved {
        Nothing,
        Something,
        Everything
    };

#ifdef ENABLE_TEXT_ENHANCE
    using TypefaceVisitor = std::function<Resolved(std::shared_ptr<RSTypeface> typeface)>;
#else
    using TypefaceVisitor = std::function<Resolved(sk_sp<SkTypeface> typeface)>;
#endif
    void matchResolvedFonts(const TextStyle& textStyle, const TypefaceVisitor& visitor);

#ifdef ENABLE_TEXT_ENHANCE
    bool isUnresolvedCombineGlyphRange(std::shared_ptr<Run> run, size_t glyphStart, size_t glyphEnd,
        size_t charStart) const;
    void splitUnresolvedBlockAndStageResolvedSubBlock(
        std::deque<RunBlock>& stagedUnresolvedBlocks, const RunBlock& unresolvedBlock);
    void shapeUnresolvedTextSeparatelyFromUnresolvedBlock(const TextStyle& textStyle, const TypefaceVisitor& visitor);
    void matchResolvedFontsFindTypeface(const TextStyle& textStyle, std::shared_ptr<RSTypeface>& typeface,
        SkUnichar& unicode);
    void matchResolvedFontsByUnicode(const TextStyle& textStyle, const TypefaceVisitor& visitor,
        std::vector<RunBlock>& hopelessBlocks, skia_private::THashSet<SkTypefaceID>& alreadyTriedTypefaces);
    void processUnresolvedBlockWithFallback(const TextStyle& textStyle,
        const TypefaceVisitor& visitor, std::vector<RunBlock>& hopelessBlocks,
        skia_private::THashSet<SkTypefaceID>& alreadyTriedTypefaces);
#endif

#ifdef SK_DEBUG
    void printState();
#endif
    void finish(const Block& block, SkScalar height, SkScalar& advanceX);

    void beginLine() override {}
    void runInfo(const RunInfo&) override {}
    void commitRunInfo() override {}
    void commitLine() override {}

    Buffer runBuffer(const RunInfo& info) override {
        fCurrentRun = std::make_shared<Run>(fParagraph,
                                           info,
                                           fCurrentText.start,
                                           fHeight,
                                           fUseHalfLeading,
                                           fBaselineShift,
                                           ++fUniqueRunId,
                                           fAdvance.fX);
        return fCurrentRun->newRunBuffer();
    }

    void commitRunBuffer(const RunInfo&) override;

#ifdef ENABLE_TEXT_ENHANCE
    void adjustRange(GlyphRange& glyphs, TextRange& textRange);
#endif
    TextRange clusteredText(GlyphRange& glyphs);
    ClusterIndex clusterIndex(GlyphIndex glyph) {
        return fCurrentText.start + fCurrentRun->fClusterIndexes[glyph];
    }
    void addFullyResolved();
    void addUnresolvedWithRun(GlyphRange glyphRange);
    void sortOutGlyphs(std::function<void(GlyphRange)>&& sortOutUnresolvedBLock);
    ClusterRange normalizeTextRange(GlyphRange glyphRange);
    void fillGaps(size_t);
#ifdef ENABLE_TEXT_ENHANCE
	BlockRange generateBlockRange(const Block& block, const TextRange& textRange);
    std::shared_ptr<Run> createZeroWidthRun(const Block& block, TextRange textRange, SkScalar currentAdvanceX);
    // Helper methods for zero-width run creation
    std::shared_ptr<RSTypeface> obtainTypefaceForPlaceholder(const TextStyle& style);
    RSFont createConfiguredFont(std::shared_ptr<RSTypeface> typeface, const TextStyle& style);
    SkGlyphID obtainSpaceGlyphId(const RSFont& font);
    void fillRunWithZeroWidthData(Run* run, SkGlyphID spaceGlyph, size_t charCount);
#endif

    ParagraphImpl* fParagraph;
    TextRange fCurrentText;
    SkScalar fHeight;
    bool fUseHalfLeading;
    SkScalar fBaselineShift;
    SkVector fAdvance;
    size_t fUnresolvedGlyphs;
    size_t fUniqueRunId;

    // TODO: Something that is not thead-safe since we don't need it
    std::shared_ptr<Run> fCurrentRun;
    std::deque<RunBlock> fUnresolvedBlocks;
    std::vector<RunBlock> fResolvedBlocks;

#ifdef ENABLE_TEXT_ENHANCE
    // Per-block fast path typefaces. A block owns a single TextStyle (hence a
    // single fontStyle + fontArguments), so its variable-font typefaces are
    // resolved once instead of once per fast path segment. This avoids the
    // per-segment FamilyKey construction (vector<SkString> copy + hash) and
    // shared_lock inside FontCollection::findTypefaces.
    struct FastPathTypefaces {
        std::shared_ptr<RSTypeface> fSimplifiedChinese;
        std::shared_ptr<RSTypeface> fEmoji;
        std::shared_ptr<RSTypeface> fCustom;
        ScaleParam fSimplifiedChineseScale;
        ScaleParam fEmojiScale;
        ScaleParam fCustomScale;
    };
    std::unordered_map<const Block*, FastPathTypefaces> fFastPathTypefaces;
#endif

    // Keeping all resolved typefaces
    struct FontKey {

        FontKey() {}

#ifdef ENABLE_TEXT_ENHANCE
        FontKey(SkUnichar unicode, RSFontStyle fontStyle, SkString locale)
            : fUnicode(unicode), fFontStyle(fontStyle), fLocale(locale) { }
#else
        FontKey(SkUnichar unicode, SkFontStyle fontStyle, SkString locale)
            : fUnicode(unicode), fFontStyle(fontStyle), fLocale(std::move(locale)) { }
#endif
        SkUnichar fUnicode;
#ifdef ENABLE_TEXT_ENHANCE
        RSFontStyle fFontStyle;
#else
        SkFontStyle fFontStyle;
#endif
        SkString fLocale;

        bool operator==(const FontKey& other) const;

        struct Hasher {
            uint32_t operator()(const FontKey& key) const;
        };
    };

#ifdef ENABLE_TEXT_ENHANCE
    std::unordered_map<FontKey, std::shared_ptr<RSTypeface>, FontKey::Hasher> fFallbackFonts;
#else
    skia_private::THashMap<FontKey, sk_sp<SkTypeface>, FontKey::Hasher> fFallbackFonts;
#endif
};

}  // namespace textlayout
}  // namespace skia
#endif
