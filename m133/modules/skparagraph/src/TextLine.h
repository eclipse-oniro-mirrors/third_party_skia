// Copyright 2019 Google LLC.
#ifndef TextLine_DEFINED
#define TextLine_DEFINED

#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkScalar.h"
#include "include/private/base/SkTArray.h"
#include "modules/skparagraph/include/DartTypes.h"
#include "modules/skparagraph/include/Metrics.h"
#include "modules/skparagraph/include/ParagraphPainter.h"
#ifdef ENABLE_TEXT_ENHANCE
#include "modules/skparagraph/include/RunBase.h"
#include "modules/skparagraph/include/TextLineBase.h"
#include "modules/skparagraph/include/ParagraphStyle.h"
#endif
#include "modules/skparagraph/include/TextStyle.h"
#include "modules/skparagraph/src/Run.h"
#include "src/base/SkBitmaskEnum.h"

#include <stddef.h>
#include <functional>
#include <memory>
#include <vector>

class SkString;

namespace skia {
namespace textlayout {

class ParagraphImpl;
#ifdef ENABLE_TEXT_ENHANCE
struct DecorationContext {
    SkScalar thickness{0.0f};
    SkScalar underlinePosition{0.0f};
    SkScalar textBlobTop{0.0f};
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
#ifdef ENABLE_TEXT_ENHANCE
      bool fIsTrimTrailingSpaceWidth{false};
      SkScalar fTrailingSpaceWidth{0.0f};
#endif
    };

#ifdef ENABLE_TEXT_ENHANCE
    struct PathParameters {
        const RSPath* recordPath{nullptr};
        SkScalar hOffset{0};
        SkScalar vOffset{0};
    } pathParameters;
#endif

    enum TextAdjustment {
        GlyphCluster = 0x01,    // All text producing glyphs pointing to the same ClusterIndex
        GlyphemeCluster = 0x02, // base glyph + all attached diacritics
        Grapheme = 0x04,        // Text adjusted to graphemes
        GraphemeGluster = 0x05, // GlyphCluster & Grapheme
    };

#ifdef ENABLE_TEXT_ENHANCE
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
#ifdef ENABLE_TEXT_ENHANCE
    SkScalar widthWithEllipsisSpaces() const {
        return fWidthWithSpaces + (fEllipsis != nullptr ? fEllipsis->fAdvance.fX : 0);
    }
#endif
    SkVector offset() const;
#ifdef ENABLE_TEXT_ENHANCE
    void setLineOffsetX(SkScalar x) {
        fOffset.set(x, fOffset.y());
    }
#endif

    SkScalar alphabeticBaseline() const { return fSizes.alphabeticBaseline(); }
    SkScalar ideographicBaseline() const { return fSizes.ideographicBaseline(); }
    SkScalar baseline() const { return fSizes.baseline(); }
#ifdef ENABLE_TEXT_ENHANCE
    void extendCoordinateRange(PositionWithAffinity& positionWithAffinity);
#endif

    using RunVisitor = std::function<bool(
            const Run* run, SkScalar runOffset, TextRange textRange, SkScalar* width)>;

#ifdef ENABLE_TEXT_ENHANCE
    bool processEllipsisRun(bool& isAlreadyUseEllipsis,
                            SkScalar& runOffset,
                            EllipsisReadStrategy ellipsisReadStrategy,
                            const RunVisitor& visitor,
                            SkScalar& runWidthInLine) const;
    bool processInsertedRun(const Run* run,
                            SkScalar& runOffset,
                            EllipsisReadStrategy ellipsisReadStrategy,
                            const RunVisitor& visitor,
                            SkScalar& runWidthInLine) const;
    void iterateThroughVisualRuns(EllipsisReadStrategy ellipsisReadStrategy,
                                  bool includingGhostSpaces,
                                  const RunVisitor& runVisitor) const;
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

#ifdef ENABLE_TEXT_ENHANCE
    void format(TextAlign align, SkScalar maxWidth, EllipsisModal ellipsisModal);
    SkScalar autoSpacing();
#else
	void format(TextAlign align, SkScalar maxWidth);
#endif
    void paint(ParagraphPainter* painter, SkScalar x, SkScalar y);
    void visit(SkScalar x, SkScalar y);
    void ensureTextBlobCachePopulated();

    void createEllipsis(SkScalar maxWidth, const SkString& ellipsis, bool ltr);

#ifdef ENABLE_TEXT_ENHANCE
    void setParagraphImpl(ParagraphImpl* newpara) { fOwner = newpara; }
    void setBlockRange(const BlockRange& blockRange) { fBlockRange = blockRange; }
    void countWord(int& wordCount, bool& inWord);
    void ellipsisNotFitProcess(EllipsisModal ellipsisModal);

    void createTailEllipsis(SkScalar maxWidth, const SkString& ellipsis, bool ltr, WordBreakType wordBreakType);
    void handleTailEllipsisInEmptyLine(std::unique_ptr<Run>& ellipsisRun, const SkString& ellipsis,
        SkScalar width, WordBreakType wordBreakType);
    void TailEllipsisUpdateLine(Cluster& cluster, float width, size_t clusterIndex, WordBreakType wordBreakType);
    void createHeadEllipsis(SkScalar maxWidth, const SkString& ellipsis, bool ltr);
    void paint(ParagraphPainter* painter, const RSPath* path, SkScalar hOffset, SkScalar vOffset);
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

    std::unique_ptr<Run> shapeEllipsis(const SkString& ellipsis, const Cluster* cluster);

#ifdef ENABLE_TEXT_ENHANCE
    std::vector<std::unique_ptr<RunBase>> getGlyphRuns() const;
    double getTypographicBounds(double* ascent, double* descent, double* leading) const;
    RSRect getImageBounds() const;
    int32_t getStringIndexForPosition(SkPoint point) const;
    size_t getGlyphCount() const;
    bool endsWithOnlyHardBreak() const;
    std::unique_ptr<Run> shapeString(const SkString& string, const Cluster* cluster);
    skia_private::STArray<1, size_t, true> getLineAllRuns() const { return fRunsInVisualOrder; };
    TextLine CloneSelf();
    TextRange getTextRangeReplacedByEllipsis() const { return fTextRangeReplacedByEllipsis; }
    void setTextBlobCachePopulated(const bool textBlobCachePopulated) {
        fTextBlobCachePopulated = textBlobCachePopulated;
    }
    std::unique_ptr<TextLineBase> createTruncatedLine(double width, EllipsisModal ellipsisMode,
        const std::string& ellipsisStr);

    double getTrailingSpaceWidth() const;
    double getOffsetForStringIndex(int32_t index) const;
    std::map<int32_t, double> getIndexAndOffsets(bool& isHardBreak) const;
    double getAlignmentOffset(double alignmentFactor, double alignmentWidth) const;
    SkRect generatePaintRegion(SkScalar x, SkScalar y);
    void updateClusterOffsets(const Cluster* cluster, SkScalar shift, SkScalar prevShift);
    void justifyUpdateRtlWidth(const SkScalar maxWidth, const SkScalar textLen);
    void setBreakWithHyphen(bool breakWithHyphen);
    bool getBreakWithHyphen() const;
#endif
private:
#ifdef ENABLE_TEXT_ENHANCE
    struct RoundRectAttr {
        int styleId;
        RectStyle roundRectStyle;
        SkRect rect;
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
#ifdef ENABLE_TEXT_ENHANCE
    void paintRoundRect(ParagraphPainter* painter, SkScalar x, SkScalar y, const Run* run) const;
    void spacingCluster(const Cluster* cluster, SkScalar spacing, SkScalar prevSpacing);
    bool hasBackgroundRect(const RoundRectAttr& attr);
    void computeRoundRect(int& index, int& preIndex, std::vector<Run*>& groupRuns, Run* run);
    void prepareRoundRect();
    SkScalar calculateThickness(const TextStyle& style, const ClipContext& context);
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
    skia_private::STArray<1, size_t, true> fRunsInVisualOrder;
    SkVector fAdvance;                  // Text size
    SkVector fOffset;                   // Text position
    SkScalar fShift;                    // Let right
    SkScalar fWidthWithSpaces;
    std::unique_ptr<Run> fEllipsis;     // In case the line ends with the ellipsis
    InternalLineMetrics fSizes;                 // Line metrics as a max of all run metrics and struts
    InternalLineMetrics fMaxRunMetrics;         // No struts - need it for GetRectForRange(max height)
    bool fHasBackground;
    bool fHasShadows;
    bool fHasDecorations;
#ifdef ENABLE_TEXT_ENHANCE
    size_t fEllipsisIndex = EMPTY_INDEX;
    TextRange fTextRangeReplacedByEllipsis;     // text range replaced by ellipsis
    bool fIsArcText;
    bool fArcTextState;
    bool fLastClipRunLtr;
#endif

    LineMetricStyle fAscentStyle;
    LineMetricStyle fDescentStyle;

    struct TextBlobRecord {
        void paint(ParagraphPainter* painter, SkScalar x, SkScalar y);

#ifdef ENABLE_TEXT_ENHANCE
        void paint(ParagraphPainter* painter);
        std::shared_ptr<RSTextBlob> fBlob;
        size_t fVisitor_Size;
#else
        sk_sp<SkTextBlob> fBlob;
#endif
        SkPoint fOffset = SkPoint::Make(0.0f, 0.0f);
        ParagraphPainter::SkPaintOrID fPaint;
        SkRect fBounds = SkRect::MakeEmpty();
        bool fClippingNeeded = false;
        SkRect fClipRect = SkRect::MakeEmpty();

        // Extra fields only used for the (experimental) visitor
        const Run* fVisitor_Run;
        size_t     fVisitor_Pos;
    };
    bool fTextBlobCachePopulated;
#ifdef ENABLE_TEXT_ENHANCE
    DecorationContext fDecorationContext;
    std::vector<RoundRectAttr> roundRectAttrs = {};
    bool fIsTextLineEllipsisHeadModal = false;
#endif
public:
    std::vector<TextBlobRecord> fTextBlobCache;
#ifdef ENABLE_TEXT_ENHANCE
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
