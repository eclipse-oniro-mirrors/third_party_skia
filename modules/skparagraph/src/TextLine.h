// Copyright 2019 Google LLC.
#ifndef TextLine_DEFINED
#define TextLine_DEFINED

#include "include/ParagraphStyle.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkScalar.h"
#include "include/private/SkBitmaskEnum.h" // IWYU pragma: keep
#include "include/private/SkTArray.h"
#include "modules/skparagraph/include/DartTypes.h"
#include "modules/skparagraph/include/Metrics.h"
#include "modules/skparagraph/include/ParagraphPainter.h"
#include "modules/skparagraph/include/RunBase.h"
#ifdef OHOS_SUPPORT
#include "modules/skparagraph/include/TextLineBase.h"
#endif
#include "modules/skparagraph/include/TextStyle.h"
#include "modules/skparagraph/src/Run.h"

#include <stddef.h>
#include <functional>
#include <memory>
#include <vector>

class SkString;

namespace skia {
namespace textlayout {
const size_t BOTTOM_PADDING_FACTOR = 8;

class ParagraphImpl;
struct DecorationContext {
    SkScalar thickness = 0.0f;
    SkScalar underlinePosition = 0.0f;
    SkScalar textBlobTop = 0.0f;
    SkScalar lineHeight = 0.0f;
};

#ifdef OHOS_SUPPORT
struct IterateRunsContext {
    size_t runIndex{0};
    SkScalar width{0};
    SkScalar runOffset{0};
    SkScalar totalWidth{0};
    bool isAlreadyUseEllipsis{false};
    TextRange lineIntersection;
    EllipsisModal ellipsisMode{EllipsisModal::NONE};
};
#endif

class TextLine {
public:
    struct ClipContext {
      const Run* run;
      size_t pos;
      size_t size;
      SkScalar fTextShift; // Shifts the text inside the run so it's placed at the right position
      SkRect clip;
      SkScalar fExcludedTrailingSpaces;
      bool clippingNeeded;
#ifdef OHOS_SUPPORT
      bool fIsTrimTrailingSpaceWidth{false};
      SkScalar fTrailingSpaceWidth{0.0f};
#endif
    };

    struct PathParameters {
        const RSPath* recordPath = nullptr;
        SkScalar hOffset = 0;
        SkScalar vOffset = 0;
    } pathParameters;

    enum TextAdjustment {
        GlyphCluster = 0x01,    // All text producing glyphs pointing to the same ClusterIndex
        GlyphemeCluster = 0x02, // base glyph + all attached diacritics
        Grapheme = 0x04,        // Text adjusted to graphemes
        GraphemeGluster = 0x05, // GlyphCluster & Grapheme
    };

#ifdef OHOS_SUPPORT
    enum EllipsisReadStrategy {
        DEFAULT = 0,            // default
        READ_REPLACED_WORD = 1,  // read replaced word
        READ_ELLIPSIS_WORD = 2, // read ellipsis word
    };

    struct HighLevelInfo {
        ClusterIndex clusterIndex{SIZE_MAX};
        bool isClusterPunct{false};
        SkScalar punctWidths{0.0f};
        SkScalar highLevelOffset{0.0f};
    };

    struct MiddleLevelInfo {
        ClusterIndex clusterIndex{SIZE_MAX};
        bool isPrevClusterSpace{true};
    };

    struct ClusterLevelsIndices {
        std::vector<HighLevelInfo> highLevelIndices;
        std::vector<MiddleLevelInfo> middleLevelIndices;
        std::vector<ClusterIndex> LowLevelIndices;
        SkScalar middleLevelOffset{0.0f};
        SkScalar lowLevelOffset{0.0f};

        bool empty()
        {
            return highLevelIndices.empty() && middleLevelIndices.empty() && LowLevelIndices.empty();
        }
    };

    enum class ShiftLevel {
        Undefined,
        HighLevel, // Level 1 Label: Punctuation
        MiddleLevel, // Level-2 label: WhitespaceBreak, between ideographic and non-ideographic characters
        LowLevel // Level-3 label: Between ideographic characters
    };
#endif

    TextLine() = default;
    TextLine(const TextLine&) = delete;
    TextLine& operator=(const TextLine&) = delete;
    TextLine(TextLine&&) = default;
    TextLine& operator=(TextLine&&) = default;
    ~TextLine() = default;

    TextLine(ParagraphImpl* owner,
             SkVector offset,
             SkVector advance,
             BlockRange blocks,
             TextRange textExcludingSpaces,
             TextRange text,
             TextRange textIncludingNewlines,
             ClusterRange clusters,
             ClusterRange clustersWithGhosts,
             SkScalar widthWithSpaces,
             InternalLineMetrics sizes);

    TextRange trimmedText() const { return fTextExcludingSpaces; }
    TextRange textWithNewlines() const { return fTextIncludingNewlines; }
    TextRange text() const { return fText; }
    ClusterRange clusters() const { return fClusterRange; }
    ClusterRange clustersWithSpaces() const { return fGhostClusterRange; }
    Run* ellipsis() const { return fEllipsis.get(); }
    InternalLineMetrics sizes() const { return fSizes; }
    bool empty() const { return fTextExcludingSpaces.empty(); }

    SkScalar spacesWidth() const { return fWidthWithSpaces - width(); }
    SkScalar height() const { return fAdvance.fY; }
    SkScalar width() const {
        return fAdvance.fX + (fEllipsis != nullptr ? fEllipsis->fAdvance.fX : 0);
    }
    SkScalar widthWithoutEllipsis() const { return fAdvance.fX; }
    SkScalar widthWithEllipsisSpaces() const {
        return fWidthWithSpaces + (fEllipsis != nullptr ? fEllipsis->fAdvance.fX : 0);
    }
    SkVector offset() const;
    void setLineOffsetX(SkScalar x) {
        fOffset.set(x, fOffset.y());
    }

    SkScalar alphabeticBaseline() const { return fSizes.alphabeticBaseline(); }
    SkScalar ideographicBaseline() const { return fSizes.ideographicBaseline(); }
    SkScalar baseline() const { return fSizes.baseline(); }
#ifdef OHOS_SUPPORT
    void extendCoordinateRange(PositionWithAffinity& positionWithAffinity);
#endif

    using RunVisitor = std::function<bool(
            const Run* run, SkScalar runOffset, TextRange textRange, SkScalar* width)>;

#ifdef OHOS_SUPPORT
    bool processEllipsisRun(IterateRunsContext& context,
                            EllipsisReadStrategy ellipsisReadStrategy,
                            const RunVisitor& visitor,
                            SkScalar& runWidthInLine) const;
    bool processInsertedRun(const Run* run,
                            SkScalar& runOffset,
                            EllipsisReadStrategy ellipsisReadStrategy,
                            const RunVisitor& visitor,
                            SkScalar& runWidthInLine) const;
#endif

#ifdef OHOS_SUPPORT
    void iterateThroughVisualRuns(EllipsisReadStrategy ellipsisReadStrategy,
                                  bool includingGhostSpaces,
                                  const RunVisitor& runVisitor) const;
    bool handleMiddleEllipsisMode(const Run* run, IterateRunsContext& context,
                                  EllipsisReadStrategy& ellipsisReadStrategy, const RunVisitor& runVisitor) const;
#else
    void iterateThroughVisualRuns(bool includingGhostSpaces, const RunVisitor& runVisitor) const;
#endif
    using RunStyleVisitor = std::function<void(
            TextRange textRange, const TextStyle& style, const ClipContext& context)>;
    SkScalar iterateThroughSingleRunByStyles(TextAdjustment textAdjustment,
                                             const Run* run,
                                             SkScalar runOffset,
                                             TextRange textRange,
                                             StyleType styleType,
                                             const RunStyleVisitor& visitor) const;

    using ClustersVisitor = std::function<bool(const Cluster* cluster, ClusterIndex index, bool ghost)>;
    void iterateThroughClustersInGlyphsOrder(bool reverse,
                                             bool includeGhosts,
                                             const ClustersVisitor& visitor) const;

    void format(TextAlign align, SkScalar maxWidth, EllipsisModal ellipsisModal);
    SkScalar autoSpacing();
    void paint(ParagraphPainter* painter, SkScalar x, SkScalar y);
    void paint(ParagraphPainter* painter, const RSPath* path, SkScalar hOffset, SkScalar vOffset);
    void visit(SkScalar x, SkScalar y);
    void ensureTextBlobCachePopulated();
    void setParagraphImpl(ParagraphImpl* newpara) { fOwner = newpara; }
    void setBlockRange(const BlockRange& blockRange) { fBlockRange = blockRange; }
    void countWord(int& wordCount, bool& inWord);
    void ellipsisNotFitProcess(EllipsisModal ellipsisModal);
#ifdef OHOS_SUPPORT
    void createTailEllipsis(SkScalar maxWidth, const SkString& ellipsis, bool ltr, WordBreakType wordBreakType);
    void handleTailEllipsisInEmptyLine(std::unique_ptr<Run>& ellipsisRun, const SkString& ellipsis,
        SkScalar width, WordBreakType wordBreakType);
    void TailEllipsisUpdateLine(Cluster& cluster, float width, size_t clusterIndex, WordBreakType wordBreakType);
    void createHeadEllipsis(SkScalar maxWidth, const SkString& ellipsis, bool ltr);
    void createMiddleEllipsis(SkScalar maxWidth, const SkString& ellipsis);
    void middleEllipsisUpdateLine(ClusterIndex& indexS, ClusterIndex& indexE, SkScalar width);
    bool isLineHeightDominatedByRun(const Run& run);
    void updateBlobShift(const Run& run, SkScalar& verticalShift);
    void resetBlobShift(const Run& run);
    void updateBlobAndRunShift(Run& run);
    void shiftPlaceholderByVerticalAlignMode(Run& run, TextVerticalAlign VerticalAlignment);
    void shiftTextByVerticalAlignment(Run& run, TextVerticalAlign VerticalAlignment);
    void applyPlaceholderVerticalShift();
    void applyVerticalShift();
    void refresh();
    void setLineAllRuns(SkSTArray<1, size_t, true>& runsInVisualOrder) {
        fRunsInVisualOrder = std::move(runsInVisualOrder);
    }
    void setEllipsisRunIndex(size_t runIndex) { fEllipsisIndex = runIndex; }
#endif
    // For testing internal structures
    void scanStyles(StyleType style, const RunStyleVisitor& visitor);

    void setMaxRunMetrics(const InternalLineMetrics& metrics) { fMaxRunMetrics = metrics; }
    InternalLineMetrics getMaxRunMetrics() const { return fMaxRunMetrics; }

    bool isFirstLine() const;
    bool isLastLine() const;
    void getRectsForRange(TextRange textRange,
                          RectHeightStyle rectHeightStyle,
                          RectWidthStyle rectWidthStyle,
                          std::vector<TextBox>& boxes) const;
    void getRectsForPlaceholders(std::vector<TextBox>& boxes);
    PositionWithAffinity getGlyphPositionAtCoordinate(SkScalar dx);

    ClipContext measureTextInsideOneRun(TextRange textRange,
                                        const Run* run,
                                        SkScalar runOffsetInLine,
                                        SkScalar textOffsetInRunInLine,
                                        bool includeGhostSpaces,
                                        TextAdjustment textAdjustment) const;

    LineMetrics getMetrics() const;

    SkRect extendHeight(const ClipContext& context) const;

    void shiftVertically(SkScalar shift) { fOffset.fY += shift; }

    void setAscentStyle(LineMetricStyle style) { fAscentStyle = style; }
    void setDescentStyle(LineMetricStyle style) { fDescentStyle = style; }

    bool endsWithHardLineBreak() const;
#ifdef OHOS_SUPPORT
    bool endsWithOnlyHardBreak() const;
#endif
    std::unique_ptr<Run> shapeString(const SkString& string, const Cluster* cluster);
    std::unique_ptr<Run> shapeEllipsis(const SkString& ellipsis, const Cluster* cluster);
    SkSTArray<1, size_t, true> getLineAllRuns() const { return fRunsInVisualOrder; };

    size_t getGlyphCount() const;
    std::vector<std::unique_ptr<RunBase>> getGlyphRuns() const;
    TextLine CloneSelf();
    TextRange getTextRangeReplacedByEllipsis() const { return fTextRangeReplacedByEllipsis; }
    void setTextBlobCachePopulated(const bool textBlobCachePopulated) {
        fTextBlobCachePopulated = textBlobCachePopulated;
    }

#ifdef OHOS_SUPPORT
    SkScalar usingAutoSpaceWidth(const Cluster* cluster) const;
    std::unique_ptr<TextLineBase> createTruncatedLine(double width, EllipsisModal ellipsisMode,
        const std::string& ellipsisStr);
    double getTypographicBounds(double* ascent, double* descent, double* leading) const;
    RSRect getImageBounds() const;
    double getTrailingSpaceWidth() const;
    int32_t getStringIndexForPosition(SkPoint point) const;
    double getOffsetForStringIndex(int32_t index) const;
    std::map<int32_t, double> getIndexAndOffsets(bool& isHardBreak) const;
    double getAlignmentOffset(double alignmentFactor, double alignmentWidth) const;
    SkRect generatePaintRegion(SkScalar x, SkScalar y);
    void updateClusterOffsets(const Cluster* cluster, SkScalar shift, SkScalar prevShift);
    void justifyUpdateRtlWidth(const SkScalar maxWidth, const SkScalar textLen);
    void setBreakWithHyphen(bool breakWithHyphen);
    bool getBreakWithHyphen() const;
    void updateTextLinePaintAttributes();
#endif

private:
#ifdef OHOS_SUPPORT
    struct RoundRectAttr {
        int styleId;
        RectStyle roundRectStyle;
        SkRect rect;
        const Run* run;
        RoundRectType fRoundRectType = RoundRectType::NONE;
    };
#endif
    void justify(SkScalar maxWidth);
    void buildTextBlob(TextRange textRange, const TextStyle& style, const ClipContext& context);
    void paintBackground(ParagraphPainter* painter,
                         SkScalar x,
                         SkScalar y,
                         TextRange textRange,
                         const TextStyle& style,
                         const ClipContext& context) const;
#ifdef OHOS_SUPPORT
    void paintRoundRect(ParagraphPainter* painter, SkScalar x, SkScalar y) const;
#endif
    void paintShadow(ParagraphPainter* painter,
                     SkScalar x,
                     SkScalar y,
                     TextRange textRange,
                     const TextStyle& style,
                     const ClipContext& context) const;
    void paintDecorations(ParagraphPainter* painter,
                          SkScalar x,
                          SkScalar y,
                          TextRange textRange,
                          const TextStyle& style,
                          const ClipContext& context) const;

    void shiftCluster(const Cluster* cluster, SkScalar shift, SkScalar prevShift);
    void spacingCluster(const Cluster* cluster, SkScalar spacing, SkScalar prevSpacing);
    SkScalar calculateThickness(const TextStyle& style, const ClipContext& context);
#ifdef OHOS_SUPPORT
    void computeRoundRect(int& index, int& preIndex, std::vector<Run*>& groupRuns, Run* run);
    void prepareRoundRect();
    bool hasBackgroundRect(const RoundRectAttr& attr);
    void measureTextWithSpacesAtTheEnd(ClipContext& context, bool includeGhostSpaces) const;
    void computeNextPaintGlyphRange(ClipContext& context, const TextRange& lastGlyphRange, StyleType styleType) const;
    SkRect computeShadowRect(SkScalar x, SkScalar y, const TextStyle& style, const ClipContext& context) const;
    SkRect getAllShadowsRect(SkScalar x, SkScalar y) const;
#endif

    ParagraphImpl* fOwner;
    BlockRange fBlockRange;
    TextRange fTextExcludingSpaces;
    TextRange fText;
    TextRange fTextIncludingNewlines;
    ClusterRange fClusterRange;
    ClusterRange fGhostClusterRange;
    // Avoid the malloc/free in the common case of one run per line
    SkSTArray<1, size_t, true> fRunsInVisualOrder;
    SkVector fAdvance;                  // Text size
    SkVector fOffset;                   // Text position
    SkScalar fShift;                    // Let right
    SkScalar fWidthWithSpaces;
    std::unique_ptr<Run> fEllipsis;     // In case the line ends with the ellipsis
    TextRange fTextRangeReplacedByEllipsis;     // text range replaced by ellipsis
    InternalLineMetrics fSizes;                 // Line metrics as a max of all run metrics and struts
    InternalLineMetrics fMaxRunMetrics;         // No struts - need it for GetRectForRange(max height)
    size_t fEllipsisIndex = EMPTY_INDEX;

    bool fHasBackground;
    bool fHasShadows;
    bool fHasDecorations;
    bool fIsArcText;
    bool fArcTextState;
    bool fLastClipRunLtr;

    LineMetricStyle fAscentStyle;
    LineMetricStyle fDescentStyle;

    struct TextBlobRecord {
        void paint(ParagraphPainter* painter, SkScalar x, SkScalar y);
        void paint(ParagraphPainter* painter);

#ifndef USE_SKIA_TXT
        sk_sp<SkTextBlob> fBlob;
#else
        std::shared_ptr<RSTextBlob> fBlob;
#endif
        SkPoint fOffset = SkPoint::Make(0.0f, 0.0f);
        ParagraphPainter::SkPaintOrID fPaint;
        SkRect fBounds = SkRect::MakeEmpty();
        bool fClippingNeeded = false;
        SkRect fClipRect = SkRect::MakeEmpty();

        // Extra fields only used for the (experimental) visitor
        const Run* fVisitor_Run;
        size_t     fVisitor_Pos;
        size_t     fVisitor_Size;
    };
    bool fTextBlobCachePopulated;
    DecorationContext fDecorationContext;
#ifdef OHOS_SUPPORT
    std::vector<RoundRectAttr> fRoundRectAttrs = {};
    bool fIsTextLineEllipsisHeadModal = false;
#endif
public:
    std::vector<TextBlobRecord> fTextBlobCache;
#ifdef OHOS_SUPPORT
    SkString fEllipsisString;
    bool fBreakWithHyphen{false};
    std::unique_ptr<Run> fHyphenRun;
    size_t fHyphenIndex = EMPTY_INDEX;
#endif
};
}  // namespace textlayout
}  // namespace skia

namespace sknonstd {
    template <> struct is_bitmask_enum<skia::textlayout::TextLine::TextAdjustment> : std::true_type {};
}  // namespace sknonstd

#endif  // TextLine_DEFINED
