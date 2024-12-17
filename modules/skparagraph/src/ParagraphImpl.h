// Copyright 2019 Google LLC.
#ifndef ParagraphImpl_DEFINED
#define ParagraphImpl_DEFINED

#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPicture.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSpan.h"
#include "include/core/SkString.h"
#include "include/core/SkTypes.h"
#include "include/private/SkBitmaskEnum.h"
#include "include/private/SkOnce.h"
#include "include/private/SkTArray.h"
#include "include/private/SkTemplates.h"
#include "modules/skparagraph/include/DartTypes.h"
#include "modules/skparagraph/include/FontCollection.h"
#include "modules/skparagraph/include/Paragraph.h"
#include "modules/skparagraph/include/ParagraphCache.h"
#include "modules/skparagraph/include/ParagraphStyle.h"
#include "modules/skparagraph/include/TextLineBase.h"
#include "modules/skparagraph/include/TextShadow.h"
#include "modules/skparagraph/include/TextStyle.h"
#include "modules/skparagraph/src/Run.h"
#include "modules/skparagraph/src/TextLine.h"
#include "modules/skunicode/include/SkUnicode.h"
#include "include/private/SkTHash.h"

#include <memory>
#include <string>
#include <vector>

class SkCanvas;

namespace skia {
namespace textlayout {

class LineMetrics;
class TextLine;

template <typename T> bool operator==(const SkSpan<T>& a, const SkSpan<T>& b) {
    return a.size() == b.size() && a.begin() == b.begin();
}

template <typename T> bool operator<=(const SkSpan<T>& a, const SkSpan<T>& b) {
    return a.begin() >= b.begin() && a.end() <= b.end();
}

template <typename TStyle>
struct StyleBlock {
    StyleBlock() : fRange(EMPTY_RANGE), fStyle() { }
    StyleBlock(size_t start, size_t end, const TStyle& style) : fRange(start, end), fStyle(style) {}
    StyleBlock(TextRange textRange, const TStyle& style) : fRange(textRange), fStyle(style) {}
    void add(TextRange tail) {
        SkASSERT(fRange.end == tail.start);
        fRange = TextRange(fRange.start, fRange.start + fRange.width() + tail.width());
    }
    TextRange fRange;
    TStyle fStyle;
};

struct ResolvedFontDescriptor {

#ifndef USE_SKIA_TXT
    ResolvedFontDescriptor(TextIndex index, SkFont font)
        : fFont(font), fTextStart(index) { }
    SkFont fFont;
#else
    ResolvedFontDescriptor(TextIndex index, RSFont font)
        : fFont(font), fTextStart(index) { }
    RSFont fFont;
#endif
    TextIndex fTextStart;
};

enum InternalState {
  kUnknown = 0,
  kIndexed = 1,     // Text is indexed
  kShaped = 2,      // Text is shaped
  kLineBroken = 5,
  kFormatted = 6,
  kDrawn = 7
};

/*
struct BidiRegion {
    BidiRegion(size_t start, size_t end, uint8_t dir)
        : text(start, end), direction(dir) { }
    TextRange text;
    uint8_t direction;
};
*/
class ParagraphImpl final : public Paragraph {

public:

    ParagraphImpl() = default;

    ParagraphImpl(const SkString& text,
                  ParagraphStyle style,
                  SkTArray<Block, true> blocks,
                  SkTArray<Placeholder, true> placeholders,
                  sk_sp<FontCollection> fonts,
                  std::shared_ptr<SkUnicode> unicode);

    ParagraphImpl(const std::u16string& utf16text,
                  ParagraphStyle style,
                  SkTArray<Block, true> blocks,
                  SkTArray<Placeholder, true> placeholders,
                  sk_sp<FontCollection> fonts,
                  std::shared_ptr<SkUnicode> unicode);

    ~ParagraphImpl() override;

    void layout(SkScalar width) override;
    void paint(SkCanvas* canvas, SkScalar x, SkScalar y) override;
    void paint(ParagraphPainter* canvas, SkScalar x, SkScalar y) override;
    void paint(ParagraphPainter* canvas, RSPath* path, SkScalar hOffset, SkScalar vOffset) override;
    std::vector<TextBox> getRectsForRange(unsigned start,
                                          unsigned end,
                                          RectHeightStyle rectHeightStyle,
                                          RectWidthStyle rectWidthStyle) override;
    std::vector<TextBox> getRectsForPlaceholders() override;
    void getLineMetrics(std::vector<LineMetrics>&) override;
    PositionWithAffinity getGlyphPositionAtCoordinate(SkScalar dx, SkScalar dy) override;
    SkRange<size_t> getWordBoundary(unsigned offset) override;

    bool getApplyRoundingHack() const { return false; }

    size_t lineNumber() override { return fLineNumber; }

    TextLine& addLine(SkVector offset, SkVector advance,
                      TextRange textExcludingSpaces, TextRange text, TextRange textIncludingNewlines,
                      ClusterRange clusters, ClusterRange clustersWithGhosts, SkScalar widthWithSpaces,
                      InternalLineMetrics sizes);

    SkSpan<const char> text() const { return SkSpan<const char>(fText.c_str(), fText.size()); }
    std::vector<SkUnichar> convertUtf8ToUnicode(const SkString& utf8);
    const std::vector<SkUnichar>& unicodeText() const { return fUnicodeText; }
    size_t getUnicodeIndex(TextIndex index) const {
        if (index >= fUnicodeIndexForUTF8Index.size()) {
            return fUnicodeIndexForUTF8Index.empty() ? 0 : fUnicodeIndexForUTF8Index.back() + 1;
        }
        return fUnicodeIndexForUTF8Index[index];
    }
    InternalState state() const { return fState; }
    SkSpan<Run> runs() { return SkSpan<Run>(fRuns.data(), fRuns.size()); }
    SkSpan<Block> styles() {
        return SkSpan<Block>(fTextStyles.data(), fTextStyles.size());
    }
    SkSpan<Placeholder> placeholders() {
        return SkSpan<Placeholder>(fPlaceholders.data(), fPlaceholders.size());
    }
    SkSpan<TextLine> lines() { return SkSpan<TextLine>(fLines.data(), fLines.size()); }
    const ParagraphStyle& paragraphStyle() const { return fParagraphStyle; }
    SkSpan<Cluster> clusters() { return SkSpan<Cluster>(fClusters.begin(), fClusters.size()); }
    sk_sp<FontCollection> fontCollection() const { return fFontCollection; }
    void formatLines(SkScalar maxWidth);
    void ensureUTF16Mapping();
    SkTArray<TextIndex> countSurroundingGraphemes(TextRange textRange) const;
    TextIndex findNextGraphemeBoundary(TextIndex utf8) const;
    TextIndex findPreviousGraphemeBoundary(TextIndex utf8) const;
    TextIndex findNextGlyphClusterBoundary(TextIndex utf8) const;
    TextIndex findPreviousGlyphClusterBoundary(TextIndex utf8) const;
    size_t getUTF16Index(TextIndex index) const {
        return fUTF16IndexForUTF8Index[index];
    }

    bool strutEnabled() const { return paragraphStyle().getStrutStyle().getStrutEnabled(); }
    bool strutForceHeight() const {
        return paragraphStyle().getStrutStyle().getForceStrutHeight();
    }
    bool strutHeightOverride() const {
        return paragraphStyle().getStrutStyle().getHeightOverride();
    }
    InternalLineMetrics strutMetrics() const { return fStrutMetrics; }

    SkString getEllipsis() const;
    WordBreakType getWordBreakType() const;
    LineBreakStrategy getLineBreakStrategy() const;

    SkSpan<const char> text(TextRange textRange);
    SkSpan<Cluster> clusters(ClusterRange clusterRange);
    Cluster& cluster(ClusterIndex clusterIndex);
    ClusterIndex clusterIndex(TextIndex textIndex) {
        auto clusterIndex = this->fClustersIndexFromCodeUnit[textIndex];
        SkASSERT(clusterIndex != EMPTY_INDEX);
        return clusterIndex;
    }
    Run& run(RunIndex runIndex) {
        SkASSERT(runIndex < SkToSizeT(fRuns.size()));
        return fRuns[runIndex];
    }

    Run& runByCluster(ClusterIndex clusterIndex);
    SkSpan<Block> blocks(BlockRange blockRange);
    Block& block(BlockIndex blockIndex);
    SkTArray<ResolvedFontDescriptor> resolvedFonts() const { return fFontSwitches; }

    void markDirty() override {
        if (fState > kIndexed) {
            fState = kIndexed;
        }
    }

    int32_t unresolvedGlyphs() override;
    std::unordered_set<SkUnichar> unresolvedCodepoints() override;
    void addUnresolvedCodepoints(TextRange textRange);

    void setState(InternalState state);
    sk_sp<SkPicture> getPicture() { return fPicture; }

    SkScalar widthWithTrailingSpaces() { return fMaxWidthWithTrailingSpaces; }
    SkScalar getMaxWidth() { return fOldMaxWidth; }

    void resetContext();
    void resolveStrut();

    bool computeCodeUnitProperties();
    void applySpacingAndBuildClusterTable();
    void buildClusterTable();
    bool shapeTextIntoEndlessLine();
    void positionShapedTextIntoLine(SkScalar maxWidth);
    void breakShapedTextIntoLines(SkScalar maxWidth);

    void updateTextAlign(TextAlign textAlign) override;
    void updateFontSize(size_t from, size_t to, SkScalar fontSize) override;
    void updateForegroundPaint(size_t from, size_t to, SkPaint paint) override;
    void updateBackgroundPaint(size_t from, size_t to, SkPaint paint) override;
#ifdef OHOS_SUPPORT
    std::vector<ParagraphPainter::PaintID> updateColor(size_t from, size_t to, SkColor color) override;
    SkIRect generatePaintRegion(SkScalar x, SkScalar y) override;
#endif

    void visit(const Visitor&) override;

    void setIndents(const std::vector<SkScalar>& indents) override;
    int getLineNumberAt(TextIndex codeUnitIndex) const override;
    bool getLineMetricsAt(int lineNumber, LineMetrics* lineMetrics) const override;
    TextRange getActualTextRange(int lineNumber, bool includeSpaces) const override;
    bool getGlyphClusterAt(TextIndex codeUnitIndex, GlyphClusterInfo* glyphInfo) override;
    bool getClosestGlyphClusterAt(SkScalar dx,
                                  SkScalar dy,
                                  GlyphClusterInfo* glyphInfo) override;
#ifndef USE_SKIA_TXT
    SkFont getFontAt(TextIndex codeUnitIndex) const override;
#else
    RSFont getFontAt(TextIndex codeUnitIndex) const override;
#endif
    std::vector<FontInfo> getFonts() const override;

    InternalLineMetrics getEmptyMetrics() const { return fEmptyMetrics; }
    InternalLineMetrics getStrutMetrics() const { return fStrutMetrics; }

    BlockRange findAllBlocks(TextRange textRange);

    void resetShifts() {
        for (auto& run : fRuns) {
            run.resetJustificationShifts();
        }
    }

    void resetAutoSpacing() {
        for (auto& run : fRuns) {
            run.resetAutoSpacing();
        }
    }

    void scanTextCutPoint(const std::vector<TextCutRecord>& rawTextSize, size_t& start, size_t& end);
    bool middleEllipsisDeal();
    bool codeUnitHasProperty(size_t index, SkUnicode::CodeUnitFlags property) const {
        return (fCodeUnitProperties[index] & property) == property;
    }

    SkUnicode* getUnicode() { return fUnicode.get(); }

    SkScalar detectIndents(size_t index) override;

    SkScalar getTextSplitRatio() const override { return fParagraphStyle.getTextSplitRatio(); }

#ifndef USE_SKIA_TXT
    SkFontMetrics measureText() override;
#else
    RSFontMetrics measureText() override;
#endif

    bool &getEllipsisState() { return isMiddleEllipsis; }

#ifndef USE_SKIA_TXT
    bool GetLineFontMetrics(const size_t lineNumber, size_t& charNumber,
        std::vector<SkFontMetrics>& fontMetrics) override;
#else
    bool GetLineFontMetrics(const size_t lineNumber, size_t& charNumber,
        std::vector<RSFontMetrics>& fontMetrics) override;
#endif

    std::vector<std::unique_ptr<TextLineBase>> GetTextLines() override;
    std::unique_ptr<Paragraph> CloneSelf() override;

    uint32_t& hash() {
        return hash_;
    }

#ifdef OHOS_SUPPORT
    size_t GetMaxLines() const override { return fParagraphStyle.getMaxLines(); }
#endif

private:
    friend class ParagraphBuilder;
    friend class ParagraphCacheKey;
    friend class ParagraphCacheValue;
    friend class ParagraphCache;

    friend class TextWrapper;
    friend class OneLineShaper;

    void computeEmptyMetrics();
    void middleEllipsisAddText(size_t charStart,
                               size_t charEnd,
                               SkScalar& allTextWidth,
                               SkScalar width,
                               bool isLeftToRight);
    SkScalar resetEllipsisWidth(SkScalar ellipsisWidth, size_t& lastRunIndex, const size_t textIndex);
    void scanRTLTextCutPoint(const std::vector<TextCutRecord>& rawTextSize, size_t& start, size_t& end);
    void scanLTRTextCutPoint(const std::vector<TextCutRecord>& rawTextSize, size_t& start, size_t& end);
    void prepareForMiddleEllipsis(SkScalar rawWidth);
    bool shapeForMiddleEllipsis(SkScalar rawWidth);
    TextRange resetRangeWithDeletedRange(const TextRange& sourceRange,
        const TextRange& deletedRange, const size_t& ellSize);
    void resetTextStyleRange(const TextRange& deletedRange);
    void resetPlaceholderRange(const TextRange& deletedRange);
    void setSize(SkScalar height, SkScalar width, SkScalar longestLine) {
        fHeight = height;
        fWidth = width;
        fLongestLine = longestLine;
    }
    void getSize(SkScalar& height, SkScalar& width, SkScalar& longestLine) {
        height = fHeight;
        width = fWidth;
        longestLine = fLongestLine;
    }
    void setIntrinsicSize(SkScalar maxIntrinsicWidth, SkScalar minIntrinsicWidth, SkScalar alphabeticBaseline,
                          SkScalar ideographicBaseline, bool exceededMaxLines) {
        fMaxIntrinsicWidth = maxIntrinsicWidth;
        fMinIntrinsicWidth = minIntrinsicWidth;
        fAlphabeticBaseline = alphabeticBaseline;
        fIdeographicBaseline = ideographicBaseline;
        fExceededMaxLines = exceededMaxLines;
    }
    void getIntrinsicSize(SkScalar& maxIntrinsicWidth, SkScalar& minIntrinsicWidth, SkScalar& alphabeticBaseline,
                          SkScalar& ideographicBaseline, bool& exceededMaxLines) {
        maxIntrinsicWidth = fMaxIntrinsicWidth;
        minIntrinsicWidth = fMinIntrinsicWidth;
        alphabeticBaseline = fAlphabeticBaseline ;
        ideographicBaseline = fIdeographicBaseline;
        exceededMaxLines = fExceededMaxLines;
    }

#ifdef OHOS_SUPPORT
    ParagraphPainter::PaintID updateTextStyleColorAndForeground(TextStyle& TextStyle, SkColor color);
    TextBox getEmptyTextRect(RectHeightStyle rectHeightStyle) const;
#endif

    // Input
    SkTArray<StyleBlock<SkScalar>> fLetterSpaceStyles;
    SkTArray<StyleBlock<SkScalar>> fWordSpaceStyles;
    SkTArray<StyleBlock<SkPaint>> fBackgroundStyles;
    SkTArray<StyleBlock<SkPaint>> fForegroundStyles;
    SkTArray<StyleBlock<std::vector<TextShadow>>> fShadowStyles;
    SkTArray<StyleBlock<Decoration>> fDecorationStyles;
    SkTArray<Block, true> fTextStyles; // TODO: take out only the font stuff
    SkTArray<Placeholder, true> fPlaceholders;
    SkString fText;
    std::vector<SkUnichar> fUnicodeText;

    // Internal structures
    InternalState fState;
    SkTArray<Run, false> fRuns;         // kShaped
    SkTArray<Cluster, true> fClusters;  // kClusterized (cached: text, word spacing, letter spacing, resolved fonts)
    SkTArray<SkUnicode::CodeUnitFlags, true> fCodeUnitProperties;
    SkTArray<size_t, true> fClustersIndexFromCodeUnit;
    std::vector<size_t> fWords;
    std::vector<SkScalar> fIndents;
    std::vector<TextCutRecord> rtlTextSize;
    std::vector<TextCutRecord> ltrTextSize;
    std::vector<SkUnicode::BidiRegion> fBidiRegions;
    // These two arrays are used in measuring methods (getRectsForRange, getGlyphPositionAtCoordinate)
    // They are filled lazily whenever they need and cached
    SkTArray<TextIndex, true> fUTF8IndexForUTF16Index;
    SkTArray<size_t, true> fUTF16IndexForUTF8Index;
    SkTArray<size_t, true> fUnicodeIndexForUTF8Index;
    SkOnce fillUTF16MappingOnce;
    size_t fUnresolvedGlyphs;
    bool isMiddleEllipsis;
    std::unordered_set<SkUnichar> fUnresolvedCodepoints;

    SkTArray<TextLine, false> fLines;   // kFormatted   (cached: width, max lines, ellipsis, text align)
    sk_sp<SkPicture> fPicture;          // kRecorded    (cached: text styles)

    SkTArray<ResolvedFontDescriptor> fFontSwitches;

    InternalLineMetrics fEmptyMetrics;
    InternalLineMetrics fStrutMetrics;

    SkScalar fOldWidth;
    SkScalar fOldHeight;
    SkScalar fMaxWidthWithTrailingSpaces;
    SkScalar fOldMaxWidth;
    SkScalar allTextWidth;
    std::shared_ptr<SkUnicode> fUnicode;
    bool fHasLineBreaks;
    bool fHasWhitespacesInside;
    TextIndex fTrailingSpaces;
    SkScalar fLayoutRawWidth {0};

    size_t fLineNumber;
    uint32_t hash_{0u};

#ifdef OHOS_SUPPORT
    std::optional<SkRect> fPaintRegion;
#endif
};
}  // namespace textlayout
}  // namespace skia


#endif  // ParagraphImpl_DEFINED
