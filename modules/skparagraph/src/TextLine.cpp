// Copyright 2019 Google LLC.

#include "include/core/SkBlurTypes.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSpan.h"
#include "include/core/SkString.h"
#include "include/core/SkTextBlob.h"
#include "include/core/SkTypes.h"
#include "include/private/SkTemplates.h"
#include "include/private/SkTo.h"
#include "modules/skparagraph/include/DartTypes.h"
#include "modules/skparagraph/include/Metrics.h"
#include "modules/skparagraph/include/ParagraphPainter.h"
#include "modules/skparagraph/include/ParagraphStyle.h"
#include "modules/skparagraph/include/TextShadow.h"
#include "modules/skparagraph/include/TextStyle.h"
#include "modules/skparagraph/src/Decorations.h"
#include "modules/skparagraph/src/ParagraphImpl.h"
#include "modules/skparagraph/src/ParagraphPainterImpl.h"
#include "modules/skparagraph/src/TextLine.h"
#include "modules/skshaper/include/SkShaper.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#ifdef OHOS_SUPPORT
#include <cstddef>
#include <numeric>
#include "log.h"
#include "modules/skparagraph/src/RunBaseImpl.h"
#include "modules/skparagraph/src/TextLineBaseImpl.h"
#include "TextParameter.h"
#include "TextLineJustify.h"
#endif

namespace skia {
namespace textlayout {
#define MAX_INT_VALUE 0x7FFFFFFF

#ifdef OHOS_SUPPORT
#define EMOJI_UNICODE_START 0x1F300
#define EMOJI_UNICODE_END 0x1F9EF
#define EMOJI_WIDTH 4
#endif

namespace {

// TODO: deal with all the intersection functionality
TextRange intersected(const TextRange& a, const TextRange& b) {
    if (a.start == b.start && a.end == b.end) return a;
    auto begin = std::max(a.start, b.start);
    auto end = std::min(a.end, b.end);
    return end >= begin ? TextRange(begin, end) : EMPTY_TEXT;
}

SkScalar littleRound(SkScalar a) {
    // This rounding is done to match Flutter tests. Must be removed..
  return SkScalarRoundToScalar(a * 100.0)/100.0;
}

TextRange operator*(const TextRange& a, const TextRange& b) {
    if (a.start == b.start && a.end == b.end) return a;
    auto begin = std::max(a.start, b.start);
    auto end = std::min(a.end, b.end);
    return end > begin ? TextRange(begin, end) : EMPTY_TEXT;
}

int compareRound(SkScalar a, SkScalar b, bool applyRoundingHack) {
    // There is a rounding error that gets bigger when maxWidth gets bigger
    // VERY long zalgo text (> 100000) on a VERY long line (> 10000)
    // Canvas scaling affects it
    // Letter spacing affects it
    // It has to be relative to be useful
    auto base = std::max(SkScalarAbs(a), SkScalarAbs(b));
    auto diff = SkScalarAbs(a - b);
    if (nearlyZero(base) || diff / base < 0.001f) {
        return 0;
    }

    auto ra = a;
    auto rb = b;

    if (applyRoundingHack) {
        ra = littleRound(a);
        rb = littleRound(b);
    }
    if (ra < rb) {
        return -1;
    } else {
        return 1;
    }
}

#ifdef USE_SKIA_TXT
bool IsRSFontEquals(const RSFont& font0, const RSFont& font1) {
    auto f0 = const_cast<RSFont&>(font0);
    auto f1 = const_cast<RSFont&>(font1);
    return f0.GetTypeface().get() == f1.GetTypeface().get() &&
        f0.GetSize() == f1.GetSize() &&
        f0.GetScaleX() == f1.GetScaleX() &&
        f0.GetSkewX() == f1.GetSkewX() &&
        f0.GetEdging() == f1.GetEdging() &&
        f0.GetHinting() == f1.GetHinting();
}
#endif

#ifdef OHOS_SUPPORT
#ifdef USE_SKIA_TXT
SkRect GetTextBlobSkTightBound(std::shared_ptr<RSTextBlob> blob, float offsetX, float offsetY, const SkRect& clipRect)
{
    if (blob == nullptr || blob->Bounds() == nullptr) {
        return SkRect::MakeEmpty();
    }

    RSRect bound = *blob->Bounds();
    bound.Offset(offsetX, offsetY);
    if (!clipRect.isEmpty()) {
        bound.left_ = std::max(bound.left_, clipRect.fLeft);
        bound.right_ = std::min(bound.right_, clipRect.fRight);
    }
    return SkRect::MakeLTRB(bound.left_, bound.top_, bound.right_, bound.bottom_);
}
#else
SkRect GetTextBlobSkTightBound(sk_sp<SkTextBlob> blob, float offsetX, float offsetY, const SkRect& clipRect)
{
    if (blob == nullptr) {
        return SkRect::MakeEmpty();
    }

    SkRect bound = blob->bounds();
    if (!clipRect.isEmpty()) {
        bound.fLeft = std::max(bound.fLeft, clipRect.fLeft);
        bound.fRight = std::min(bound.fRight, clipRect.fRight);
    }
    bound.offset(offsetX, offsetY);
    return bound;
}
#endif
#endif
}  // namespace

TextLine::TextLine(ParagraphImpl* owner,
                   SkVector offset,
                   SkVector advance,
                   BlockRange blocks,
                   TextRange textExcludingSpaces,
                   TextRange text,
                   TextRange textIncludingNewlines,
                   ClusterRange clusters,
                   ClusterRange clustersWithGhosts,
                   SkScalar widthWithSpaces,
                   InternalLineMetrics sizes)
        : fOwner(owner)
        , fBlockRange(blocks)
        , fTextExcludingSpaces(textExcludingSpaces)
        , fText(text)
        , fTextIncludingNewlines(textIncludingNewlines)
        , fClusterRange(clusters)
        , fGhostClusterRange(clustersWithGhosts)
        , fRunsInVisualOrder()
        , fAdvance(advance)
        , fOffset(offset)
        , fShift(0.0)
        , fWidthWithSpaces(widthWithSpaces)
        , fEllipsis(nullptr)
        , fSizes(sizes)
        , fHasBackground(false)
        , fHasShadows(false)
        , fHasDecorations(false)
        , fIsArcText(false)
        , fArcTextState(false)
        , fAscentStyle(LineMetricStyle::CSS)
        , fDescentStyle(LineMetricStyle::CSS)
        , fTextBlobCachePopulated(false) {
    // Reorder visual runs
    auto& start = owner->cluster(fGhostClusterRange.start);
    auto& end = owner->cluster(fGhostClusterRange.end - 1);
    size_t numRuns = end.runIndex() - start.runIndex() + 1;

    for (BlockIndex index = fBlockRange.start; index < fBlockRange.end; ++index) {
        auto b = fOwner->styles().begin() + index;
        if (b->fStyle.hasBackground()) {
            fHasBackground = true;
        }

#ifdef OHOS_SUPPORT
        if (b->fStyle.getDecorationType() != TextDecoration::kNoDecoration &&
            b->fStyle.getDecorationThicknessMultiplier() > 0) {
#else
        if (b->fStyle.getDecorationType() != TextDecoration::kNoDecoration) {
#endif
            fHasDecorations = true;
        }
        if (b->fStyle.getShadowNumber() > 0) {
            fHasShadows = true;
        }
    }

    // Get the logical order

    // This is just chosen to catch the common/fast cases. Feel free to tweak.
    constexpr int kPreallocCount = 4;
    SkAutoSTArray<kPreallocCount, SkUnicode::BidiLevel> runLevels(numRuns);
    std::vector<RunIndex> placeholdersInOriginalOrder;
    size_t runLevelsIndex = 0;
    // Placeholders must be laid out using the original order in which they were added
    // in the input. The API does not provide a way to indicate that a placeholder
    // position was moved due to bidi reordering.
    for (auto runIndex = start.runIndex(); runIndex <= end.runIndex(); ++runIndex) {
        auto& run = fOwner->run(runIndex);
        runLevels[runLevelsIndex++] = run.fBidiLevel;
        fMaxRunMetrics.add(
            InternalLineMetrics(run.correctAscent(), run.correctDescent(), run.fFontMetrics.fLeading));
        if (run.isPlaceholder()) {
            placeholdersInOriginalOrder.push_back(runIndex);
        }
    }
    SkASSERT(runLevelsIndex == numRuns);

    SkAutoSTArray<kPreallocCount, int32_t> logicalOrder(numRuns);

    // TODO: hide all these logic in SkUnicode?
    fOwner->getUnicode()->reorderVisual(runLevels.data(), numRuns, logicalOrder.data());
    auto firstRunIndex = start.runIndex();
    auto placeholderIter = placeholdersInOriginalOrder.begin();
    for (auto index : logicalOrder) {
        auto runIndex = firstRunIndex + index;
        if (fOwner->run(runIndex).isPlaceholder()) {
            fRunsInVisualOrder.push_back(*placeholderIter++);
        } else {
            fRunsInVisualOrder.push_back(runIndex);
        }
    }

    fTextRangeReplacedByEllipsis = EMPTY_RANGE;
    fEllipsisIndex = EMPTY_INDEX;
    fLastClipRunLtr = false;
}

void TextLine::paint(ParagraphPainter* painter, const RSPath* path, SkScalar hOffset, SkScalar vOffset) {
    prepareRoundRect();
    fIsArcText = true;
    if (pathParameters.hOffset != hOffset || pathParameters.vOffset != vOffset) {
        fTextBlobCachePopulated = false;
    }
    pathParameters.recordPath = path;
    pathParameters.hOffset = hOffset;
    pathParameters.vOffset = vOffset;
    this->ensureTextBlobCachePopulated();
    for (auto& record : fTextBlobCache) {
        record.paint(painter);
    }
}

void TextLine::paint(ParagraphPainter* painter, SkScalar x, SkScalar y) {
    prepareRoundRect();
    fIsArcText = false;
#ifdef OHOS_SUPPORT
    this->iterateThroughVisualRuns(EllipsisReadStrategy::READ_REPLACED_WORD, true,
#else
    this->iterateThroughVisualRuns(false,
#endif
        [painter, x, y, this]
        (const Run* run, SkScalar runOffsetInLine, TextRange textRange, SkScalar* runWidthInLine) {
            *runWidthInLine = this->iterateThroughSingleRunByStyles(
            TextAdjustment::GlyphCluster, run, runOffsetInLine, textRange, StyleType::kBackground,
            [painter, x, y, run, this](TextRange textRange, const TextStyle& style, const ClipContext& context) {
                if (fHasBackground) {
                    this->paintBackground(painter, x, y, textRange, style, context);
                }
                paintRoundRect(painter, x, y, run);
            });
        return true;
        });

    if (fHasShadows) {
#ifdef OHOS_SUPPORT
        this->iterateThroughVisualRuns(EllipsisReadStrategy::READ_REPLACED_WORD, false,
#else
        this->iterateThroughVisualRuns(false,
#endif
            [painter, x, y, this]
            (const Run* run, SkScalar runOffsetInLine, TextRange textRange, SkScalar* runWidthInLine) {
            *runWidthInLine = this->iterateThroughSingleRunByStyles(
                TextAdjustment::GlyphCluster, run, runOffsetInLine, textRange, StyleType::kShadow,
                [painter, x, y, this]
                (TextRange textRange, const TextStyle& style, const ClipContext& context) {
                    this->paintShadow(painter, x, y, textRange, style, context);
                });
            return true;
            });
    }

    this->ensureTextBlobCachePopulated();

    for (auto& record : fTextBlobCache) {
        record.paint(painter, x, y);
    }

    if (fHasDecorations) {
#ifdef OHOS_SUPPORT
        this->fDecorationContext = {0.0f, 0.0f, 0.0f};
        // 16 is default value in placeholder-only scenario, calculated by the fontsize 14.
        SkScalar maxLineHeightWithoutPlaceholder = 16;
        this->iterateThroughVisualRuns(EllipsisReadStrategy::DEFAULT, true,
            [painter, x, y, this, &maxLineHeightWithoutPlaceholder]
            (const Run* run, SkScalar runOffsetInLine, TextRange textRange, SkScalar* runWidthInLine) {
                *runWidthInLine = this->iterateThroughSingleRunByStyles(
                    TextAdjustment::GlyphCluster, run, runOffsetInLine, textRange,
                    StyleType::kDecorations, [painter, x, y, this, &maxLineHeightWithoutPlaceholder]
                    (TextRange textRange, const TextStyle& style, const ClipContext& context) {
                    if (style.getDecoration().fType == TextDecoration::kUnderline) {
                        SkScalar tmpThick = this->calculateThickness(style, context);
                        fDecorationContext.thickness = fDecorationContext.thickness > tmpThick ?
                            fDecorationContext.thickness : tmpThick;
                    }
                    auto cur = context.run;
                    if (cur != nullptr && !cur->isPlaceholder()) {
                        SkScalar height = round(cur->correctDescent() - cur->correctAscent() + cur->correctLeading());
                        maxLineHeightWithoutPlaceholder = maxLineHeightWithoutPlaceholder < height ?
                            height : maxLineHeightWithoutPlaceholder;
                    }
                });
                return true;
        });
        // 16% of row height wihtout placeholder.
        fDecorationContext.underlinePosition = maxLineHeightWithoutPlaceholder * 0.16 + baseline();
        fDecorationContext.textBlobTop = maxLineHeightWithoutPlaceholder * 0.16;
#endif

#ifdef OHOS_SUPPORT
        this->iterateThroughVisualRuns(EllipsisReadStrategy::DEFAULT, true,
#else
        this->iterateThroughVisualRuns(false,
#endif
            [painter, x, y, this]
            (const Run* run, SkScalar runOffsetInLine, TextRange textRange, SkScalar* runWidthInLine) {
                *runWidthInLine = this->iterateThroughSingleRunByStyles(
                TextAdjustment::GlyphCluster, run, runOffsetInLine, textRange, StyleType::kDecorations,
                [painter, x, y, this]
                (TextRange textRange, const TextStyle& style, const ClipContext& context) {
                    this->paintDecorations(painter, x, y, textRange, style, context);
                });
                return true;
        });
    }
}

bool TextLine::hasBackgroundRect(const RoundRectAttr& attr) {
    return attr.roundRectStyle.color != 0 && attr.rect.width() > 0;
}

void TextLine::computeRoundRect(int& index, int& preIndex, std::vector<Run*>& groupRuns, Run* run) {
    int runCount = roundRectAttrs.size();
    if (index >= runCount) {
        return;
    }

    bool leftRound = false;
    bool rightRound = false;
    if (hasBackgroundRect(roundRectAttrs[index])) {
        int styleId = roundRectAttrs[index].styleId;
        // index - 1 is previous index, -1 is the invalid styleId
        int preStyleId = index == 0 ? -1 : roundRectAttrs[index - 1].styleId;
        // runCount - 1 is the last run index, index + 1 is next run index, -1 is the invalid styleId
        int nextStyleId = index == runCount - 1 ? -1 : roundRectAttrs[index + 1].styleId;
        // index - preIndex > 1 means the left run has no background rect
        leftRound = (preIndex < 0 || index - preIndex > 1 || preStyleId != styleId);
        // runCount - 1 is the last run index
        rightRound = (index == runCount - 1 || !hasBackgroundRect(roundRectAttrs[index + 1]) ||
            nextStyleId != styleId);
        preIndex = index;
        groupRuns.push_back(run);
    } else if (!groupRuns.empty()) {
        groupRuns.erase(groupRuns.begin(), groupRuns.end());
    }
    if (leftRound && rightRound) {
        run->setRoundRectType(RoundRectType::ALL);
    } else if (leftRound) {
        run->setRoundRectType(RoundRectType::LEFT_ONLY);
    } else if (rightRound) {
        run->setRoundRectType(RoundRectType::RIGHT_ONLY);
    } else {
        run->setRoundRectType(RoundRectType::NONE);
    }

    if (rightRound && !groupRuns.empty()) {
        double maxRoundRectRadius = MAX_INT_VALUE;
        double minTop = MAX_INT_VALUE;
        double maxBottom = 0;
        for (auto &gRun : groupRuns) {
            RoundRectAttr& attr = roundRectAttrs[gRun->getIndexInLine()];
            maxRoundRectRadius = std::fmin(std::fmin(attr.rect.width(), attr.rect.height()), maxRoundRectRadius);
            minTop = std::fmin(minTop, attr.rect.top());
            maxBottom = std::fmax(maxBottom, attr.rect.bottom());
        }
        for (auto &gRun : groupRuns) {
            gRun->setMaxRoundRectRadius(maxRoundRectRadius);
            gRun->setTopInGroup(minTop - gRun->offset().y());
            gRun->setBottomInGroup(maxBottom - gRun->offset().y());
        }
        groupRuns.erase(groupRuns.begin(), groupRuns.end());
    }
    index++;
}

void TextLine::prepareRoundRect() {
    roundRectAttrs.clear();
#ifdef OHOS_SUPPORT
        this->iterateThroughVisualRuns(EllipsisReadStrategy::DEFAULT, true,
#else
        this->iterateThroughVisualRuns(true,
#endif
        [this](const Run* run, SkScalar runOffsetInLine, TextRange textRange, SkScalar* runWidthInLine) {
            *runWidthInLine = this->iterateThroughSingleRunByStyles(
            TextAdjustment::GlyphCluster, run, runOffsetInLine, textRange, StyleType::kBackground,
            [run, this](TextRange textRange, const TextStyle& style, const ClipContext& context) {
                roundRectAttrs.push_back({style.getStyleId(), style.getBackgroundRect(), context.clip});
            });
            return true;
        });

    std::vector<Run*> groupRuns;
    int index = 0;
    int preIndex = -1;
    for (auto& runIndex : fRunsInVisualOrder) {
        auto run = &this->fOwner->run(runIndex);
        run->setIndexInLine(static_cast<size_t>(index));
        computeRoundRect(index, preIndex, groupRuns, run);
    }
}

void TextLine::ensureTextBlobCachePopulated() {
    if (fTextBlobCachePopulated && fArcTextState == fIsArcText) {
        return;
    }
    fTextBlobCache.clear();
    if (fBlockRange.width() == 1 &&
        fRunsInVisualOrder.size() == 1 &&
        fEllipsis == nullptr &&
        fOwner->run(fRunsInVisualOrder[0]).placeholderStyle() == nullptr) {
        if (fClusterRange.width() == 0) {
            return;
        }
        // Most common and most simple case
        const auto& style = fOwner->block(fBlockRange.start).fStyle;
        const auto& run = fOwner->run(fRunsInVisualOrder[0]);
        auto clip = SkRect::MakeXYWH(0.0f, this->sizes().runTop(&run, this->fAscentStyle),
                                     fAdvance.fX,
                                     run.calculateHeight(this->fAscentStyle, this->fDescentStyle));

        auto& start = fOwner->cluster(fClusterRange.start);
        auto& end = fOwner->cluster(fClusterRange.end - 1);
        SkASSERT(start.runIndex() == end.runIndex());
        GlyphRange glyphs;
        if (run.leftToRight()) {
            glyphs = GlyphRange(start.startPos(),
                                end.isHardBreak() ? end.startPos() : end.endPos());
        } else {
            glyphs = GlyphRange(end.startPos(),
                                start.isHardBreak() ? start.startPos() : start.endPos());
        }
        ClipContext context = {/*run=*/&run,
                               /*pos=*/glyphs.start,
                               /*size=*/glyphs.width(),
                               /*fTextShift=*/-run.positionX(glyphs.start), // starting position
                               /*clip=*/clip,                               // entire line
                               /*fExcludedTrailingSpaces=*/0.0f,            // no need for that
                               /*clippingNeeded=*/false};                   // no need for that
        this->buildTextBlob(fTextExcludingSpaces, style, context);
    } else {
#ifdef OHOS_SUPPORT
        this->iterateThroughVisualRuns(EllipsisReadStrategy::READ_ELLIPSIS_WORD, false,
#else
        this->iterateThroughVisualRuns(false,
#endif
           [this](const Run* run,
                  SkScalar runOffsetInLine,
                  TextRange textRange,
                  SkScalar* runWidthInLine) {
               if (run->placeholderStyle() != nullptr) {
                   *runWidthInLine = run->advance().fX;
                   return true;
               }
               *runWidthInLine = this->iterateThroughSingleRunByStyles(
                   TextAdjustment::GlyphCluster,
                   run,
                   runOffsetInLine,
                   textRange,
                   StyleType::kForeground,
                   [this](TextRange textRange, const TextStyle& style, const ClipContext& context) {
                       this->buildTextBlob(textRange, style, context);
                   });
               return true;
           });
    }
    fTextBlobCachePopulated = true;
    fArcTextState = fIsArcText;
    pathParameters.recordPath = nullptr;
}

void TextLine::format(TextAlign align, SkScalar maxWidth, EllipsisModal ellipsisModal) {
    SkScalar delta = maxWidth - this->widthWithEllipsisSpaces();
#ifdef OHOS_SUPPORT
    delta = (delta < 0) ? 0 : delta;
#else
    if (delta <= 0) {
        return;
    }
#endif
    // We do nothing for left align
    if (align == TextAlign::kJustify) {
        if (!this->endsWithHardLineBreak()) {
            this->justify(maxWidth);
#ifdef OHOS_SUPPORT
            this->fOwner->setLongestLine(maxWidth);
#endif
        } else if (fOwner->paragraphStyle().getTextDirection() == TextDirection::kRtl) {
            // Justify -> Right align
            fShift = delta;
        }
    } else if (align == TextAlign::kRight) {
        fShift = delta;
    } else if (align == TextAlign::kCenter) {
        fShift = delta / 2;
    }
}

#ifdef OHOS_SUPPORT
SkScalar TextLine::autoSpacing() {
    if (!TextParameter::GetAutoSpacingEnable()) {
        return 0;
    }
    SkScalar spacing = 0.0;
    auto prevCluster = fOwner->cluster(fClusterRange.start);
    for (auto clusterIndex = fClusterRange.start + 1; clusterIndex < fClusterRange.end; ++clusterIndex) {
        auto prevSpacing = spacing;
        auto& cluster = fOwner->cluster(clusterIndex);
        spacing += cluster.needAutoSpacing() ? prevCluster.getFontSize() / AUTO_SPACING_WIDTH_RATIO : 0;
        spacingCluster(&cluster, spacing, prevSpacing);
        prevCluster = cluster;
    }
    this->fWidthWithSpaces += spacing;
    this->fAdvance.fX += spacing;
    return spacing;
}
#endif

void TextLine::scanStyles(StyleType styleType, const RunStyleVisitor& visitor) {
    if (this->empty()) {
        return;
    }

#ifdef OHOS_SUPPORT
    this->iterateThroughVisualRuns(EllipsisReadStrategy::READ_REPLACED_WORD, false,
#else
    this->iterateThroughVisualRuns(false,
#endif
            [this, visitor, styleType](
                    const Run* run, SkScalar runOffset, TextRange textRange, SkScalar* width) {
                *width = this->iterateThroughSingleRunByStyles(
                        TextAdjustment::GlyphCluster,
                        run,
                        runOffset,
                        textRange,
                        styleType,
                        [visitor](TextRange textRange,
                                  const TextStyle& style,
                                  const ClipContext& context) {
                            visitor(textRange, style, context);
                        });
                return true;
            });
}

SkRect TextLine::extendHeight(const ClipContext& context) const {
    SkRect result = context.clip;
    result.fBottom += std::max(this->fMaxRunMetrics.height() - this->height(), 0.0f);
    return result;
}

void TextLine::buildTextBlob(TextRange textRange, const TextStyle& style, const ClipContext& context) {
    if (context.run->placeholderStyle() != nullptr) {
        return;
    }

    fTextBlobCache.emplace_back();
    TextBlobRecord& record = fTextBlobCache.back();

    if (style.hasForeground()) {
        record.fPaint = style.getForegroundPaintOrID();
    } else {
        std::get<SkPaint>(record.fPaint).setColor(style.getColor());
    }
    record.fVisitor_Run = context.run;
    record.fVisitor_Pos = context.pos;
    record.fVisitor_Size = context.size;

    // TODO: This is the change for flutter, must be removed later
#ifndef USE_SKIA_TXT
    SkTextBlobBuilder builder;
#else
    RSTextBlobBuilder builder;
#endif
    if (pathParameters.recordPath) {
        context.run->copyTo(builder,
                            pathParameters.recordPath,
                            pathParameters.hOffset,
                            pathParameters.vOffset,
                            context.fTextShift,
                            SkToU32(context.pos),
                            context.size);
    } else {
        context.run->copyTo(builder, SkToU32(context.pos), context.size);
    }
#ifdef OHOS_SUPPORT
    // when letterspacing < 0, it causes the font is cliped. so the record fClippingNeeded is set false
#else
    record.fClippingNeeded = context.clippingNeeded;
#endif
    if (context.clippingNeeded) {
        record.fClipRect = extendHeight(context).makeOffset(this->offset());
    } else {
        record.fClipRect = context.clip.makeOffset(this->offset());
    }

    SkASSERT(nearlyEqual(context.run->baselineShift(), style.getBaselineShift()));
    SkScalar correctedBaseline = SkScalarFloorToScalar(this->baseline() + style.getBaselineShift() +  0.5);
#ifndef USE_SKIA_TXT
    record.fBlob = builder.make();
    if (record.fBlob != nullptr) {
        record.fBounds.joinPossiblyEmptyRect(record.fBlob->bounds());
    }
#else
    record.fBlob = builder.Make();
    if (record.fBlob != nullptr) {
        auto bounds = record.fBlob->Bounds();
        if (bounds) {
            record.fBounds.joinPossiblyEmptyRect(SkRect::MakeLTRB(
                bounds->left_, bounds->top_, bounds->right_, bounds->bottom_
            ));
        }
    }
#endif

    record.fOffset = SkPoint::Make(this->offset().fX + context.fTextShift,
#ifdef OHOS_SUPPORT
        this->offset().fY + correctedBaseline - (context.run ? context.run->fCompressionBaselineShift : 0));
#else
                                   this->offset().fY + correctedBaseline);
#endif
#ifdef OHOS_SUPPORT
#ifndef USE_SKIA_TXT
    SkFont font;
#else
    RSFont font;
#endif
    if (record.fBlob != nullptr && record.fVisitor_Run != nullptr) {
        font = record.fVisitor_Run->font();
        if (font.GetTypeface() != nullptr &&
            (font.GetTypeface()->GetFamilyName().find("Emoji") != std::string::npos ||
            font.GetTypeface()->GetFamilyName().find("emoji") != std::string::npos)) {
                record.fBlob->SetEmoji(true);
        }
    }
#endif
}

void TextLine::TextBlobRecord::paint(ParagraphPainter* painter, SkScalar x, SkScalar y) {
    if (fClippingNeeded) {
        painter->save();
        painter->clipRect(fClipRect.makeOffset(x, y));
    }
    painter->drawTextBlob(fBlob, x + fOffset.x(), y + fOffset.y(), fPaint);
    if (fClippingNeeded) {
        painter->restore();
    }
}

void TextLine::TextBlobRecord::paint(ParagraphPainter* painter) {
    if (fClippingNeeded) {
        painter->save();
    }
    painter->drawTextBlob(fBlob, 0, 0, fPaint);
    if (fClippingNeeded) {
        painter->restore();
    }
}

void TextLine::paintBackground(ParagraphPainter* painter,
                               SkScalar x,
                               SkScalar y,
                               TextRange textRange,
                               const TextStyle& style,
                               const ClipContext& context) const {
    if (style.hasBackground()) {
        painter->drawRect(context.clip.makeOffset(this->offset() + SkPoint::Make(x, y)),
                          style.getBackgroundPaintOrID());
    }
}

void TextLine::paintRoundRect(ParagraphPainter* painter, SkScalar x, SkScalar y, const Run* run) const {
    size_t index = run->getIndexInLine();
    if (index >= roundRectAttrs.size()) {
        return;
    }

    const RoundRectAttr& attr = roundRectAttrs[index];
    if (attr.roundRectStyle.color == 0) {
        return;
    }

    SkScalar ltRadius = 0.0f;
    SkScalar rtRadius = 0.0f;
    SkScalar rbRadius = 0.0f;
    SkScalar lbRadius = 0.0f;
    RoundRectType rType = run->getRoundRectType();
    if (rType == RoundRectType::ALL || rType == RoundRectType::LEFT_ONLY) {
        ltRadius = std::fmin(attr.roundRectStyle.leftTopRadius, run->getMaxRoundRectRadius());
        lbRadius = std::fmin(attr.roundRectStyle.leftBottomRadius, run->getMaxRoundRectRadius());
    }
    if (rType == RoundRectType::ALL || rType == RoundRectType::RIGHT_ONLY) {
        rtRadius = std::fmin(attr.roundRectStyle.rightTopRadius, run->getMaxRoundRectRadius());
        rbRadius = std::fmin(attr.roundRectStyle.rightBottomRadius, run->getMaxRoundRectRadius());
    }
    const SkVector radii[4] = {{ltRadius, ltRadius}, {rtRadius, rtRadius}, {rbRadius, rbRadius}, {lbRadius, lbRadius}};
    SkRect skRect(SkRect::MakeLTRB(attr.rect.left(), run->getTopInGroup(), attr.rect.right(),
        run->getBottomInGroup()));
    SkRRect skRRect;
    skRRect.setRectRadii(skRect, radii);
    skRRect.offset(x + this->offset().x(), y + this->offset().y());
    painter->drawRRect(skRRect, attr.roundRectStyle.color);
}

void TextLine::paintShadow(ParagraphPainter* painter,
                           SkScalar x,
                           SkScalar y,
                           TextRange textRange,
                           const TextStyle& style,
                           const ClipContext& context) const {
    SkScalar correctedBaseline = SkScalarFloorToScalar(this->baseline() + style.getBaselineShift() + 0.5);

    for (TextShadow shadow : style.getShadows()) {
        if (!shadow.hasShadow()) continue;

#ifndef USE_SKIA_TXT
        SkTextBlobBuilder builder;
#else
        RSTextBlobBuilder builder;
#endif
        context.run->copyTo(builder, context.pos, context.size);

        if (context.clippingNeeded) {
            painter->save();
            SkRect clip = extendHeight(context);
            clip.offset(x, y);
            clip.offset(this->offset());
            painter->clipRect(clip);
        }
#ifndef USE_SKIA_TXT
        auto blob = builder.make();
#else
        auto blob = builder.Make();
#endif
        painter->drawTextShadow(blob,
            x + this->offset().fX + shadow.fOffset.x() + context.fTextShift,
#ifdef OHOS_SUPPORT
            y + this->offset().fY + shadow.fOffset.y() + correctedBaseline -
                (context.run ? context.run->fCompressionBaselineShift : 0),
#else
            y + this->offset().fY + shadow.fOffset.y() + correctedBaseline,
#endif
            shadow.fColor,
            SkDoubleToScalar(shadow.fBlurSigma));
        if (context.clippingNeeded) {
            painter->restore();
        }
    }
}

SkScalar TextLine::calculateThickness(const TextStyle& style, const ClipContext& content)
{
    Decorations decoration;
    return decoration.calculateThickness(style, content);
}

void TextLine::paintDecorations(ParagraphPainter* painter, SkScalar x, SkScalar y, TextRange textRange, const TextStyle& style, const ClipContext& context) const {
    ParagraphPainterAutoRestore ppar(painter);
    painter->translate(x + this->offset().fX, y + this->offset().fY + style.getBaselineShift());
    Decorations decorations;
    decorations.setDecorationContext(fDecorationContext);
    SkScalar correctedBaseline = SkScalarFloorToScalar(-this->sizes().rawAscent() + style.getBaselineShift() + 0.5);
    decorations.paint(painter, style, context, correctedBaseline);
}

#ifdef OHOS_SUPPORT
void TextLine::justify(SkScalar maxWidth)
{
    TextLineJustify textLineJustify(*this);
    if (textLineJustify.justify(maxWidth)) {
        this->fWidthWithSpaces += (maxWidth - this->widthWithoutEllipsis());
        this->fAdvance.fX = maxWidth;
    }
}

void TextLine::updateClusterOffsets(const Cluster* cluster, SkScalar shift, SkScalar prevShift)
{
    this->shiftCluster(cluster, shift, prevShift);
}

void TextLine::justifyUpdateRtlWidth(const SkScalar maxWidth, const SkScalar textLen)
{
    if (this->fOwner->paragraphStyle().getTextDirection() == TextDirection::kRtl) {
        // Justify -> Right align
        this->fShift = maxWidth - textLen;
    }
}
#else
void TextLine::justify(SkScalar maxWidth) {
    int whitespacePatches = 0;
    SkScalar textLen = 0;
    bool whitespacePatch = false;
    // Take leading whitespaces width but do not increment a whitespace patch number
    bool leadingWhitespaces = false;
    this->iterateThroughClustersInGlyphsOrder(false, false,
        [&](const Cluster* cluster, ClusterIndex index, bool ghost) {
            if (cluster->isWhitespaceBreak()) {
                if (index == 0) {
                    leadingWhitespaces = true;
                } else if (!whitespacePatch && !leadingWhitespaces) {
                    // We only count patches BETWEEN words, not before
                    ++whitespacePatches;
                }
                whitespacePatch = !leadingWhitespaces;
            } else if (cluster->isIdeographic()) {
                // Whitespace break before and after
                if (!whitespacePatch && index != 0) {
                    // We only count patches BETWEEN words, not before
                    ++whitespacePatches; // before
                }
                whitespacePatch = true;
                leadingWhitespaces = false;
                ++whitespacePatches;    // after
            } else {
                whitespacePatch = false;
                leadingWhitespaces = false;
            }
            textLen += cluster->width();
            return true;
        });

    if (whitespacePatch) {
        // We only count patches BETWEEN words, not after
        --whitespacePatches;
    }
    if (whitespacePatches == 0) {
        if (fOwner->paragraphStyle().getTextDirection() == TextDirection::kRtl) {
            // Justify -> Right align
            fShift = maxWidth - textLen;
        }
        return;
    }
    SkScalar step = (maxWidth - textLen) / whitespacePatches;
    SkScalar shift = 0.0f;
    SkScalar prevShift = 0.0f;

    // Deal with the ghost spaces
    auto ghostShift = maxWidth - this->fAdvance.fX;
    // Spread the extra whitespaces
    whitespacePatch = false;
    // Do not break on leading whitespaces
    leadingWhitespaces = false;
    this->iterateThroughClustersInGlyphsOrder(false, true, [&](const Cluster* cluster, ClusterIndex index, bool ghost) {

        if (ghost) {
            if (cluster->run().leftToRight()) {
                this->shiftCluster(cluster, ghostShift, ghostShift);
            }
            return true;
        }

        if (cluster->isWhitespaceBreak()) {
            if (index == 0) {
                leadingWhitespaces = true;
            } else if (!whitespacePatch && !leadingWhitespaces) {
                shift += step;
                whitespacePatch = true;
                --whitespacePatches;
            }
        } else if (cluster->isIdeographic()) {
            if (!whitespacePatch && index != 0) {
                shift += step;
               --whitespacePatches;
            }
            whitespacePatch = false;
            leadingWhitespaces = false;
        } else {
            whitespacePatch = false;
            leadingWhitespaces = false;
        }
        this->shiftCluster(cluster, shift, prevShift);
        prevShift = shift;
        // We skip ideographic whitespaces
        if (!cluster->isWhitespaceBreak() && cluster->isIdeographic()) {
            shift += step;
            whitespacePatch = true;
            --whitespacePatches;
        }
        return true;
    });

    if (whitespacePatch && whitespacePatches < 0) {
        whitespacePatches++;
        shift -= step;
    }

    SkAssertResult(nearlyEqual(shift, maxWidth - textLen));
    SkASSERT(whitespacePatches == 0);

    this->fWidthWithSpaces += ghostShift;
    this->fAdvance.fX = maxWidth;
}
#endif

void TextLine::shiftCluster(const Cluster* cluster, SkScalar shift, SkScalar prevShift) {

    auto& run = cluster->run();
    auto start = cluster->startPos();
    auto end = cluster->endPos();

    if (end == run.size()) {
        // Set the same shift for the fake last glyph (to avoid all extra checks)
        ++end;
    }

    if (run.fJustificationShifts.empty()) {
        // Do not fill this array until needed
        run.fJustificationShifts.push_back_n(run.size() + 1, { 0, 0 });
    }

    for (size_t pos = start; pos < end; ++pos) {
        run.fJustificationShifts[pos] = { shift, prevShift };
    }
}

void TextLine::spacingCluster(const Cluster* cluster, SkScalar spacing, SkScalar prevSpacing) {
    auto& run = cluster->run();
    auto start = cluster->startPos();
    auto end = cluster->endPos();
    if (end == run.size()) {
        // Set the same shift for the fake last glyph (to avoid all extra checks)
        ++end;
    }

    if (run.fAutoSpacings.empty()) {
        // Do not fill this array until needed
        run.fAutoSpacings.push_back_n(run.size() + 1, { 0, 0 });
    }

    for (size_t pos = start; pos < end; ++pos) {
        run.fAutoSpacings[pos] = { spacing, prevSpacing};
    }
}

void TextLine::countWord(int& wordCount, bool& inWord) {
    for (auto clusterIndex = fGhostClusterRange.start; clusterIndex < fGhostClusterRange.end; ++clusterIndex) {
        auto& cluster = fOwner->cluster(clusterIndex);
        if (cluster.isWordBreak()) {
            inWord = false;
        } else if (!inWord) {
            ++wordCount;
            inWord = true;
        }
    }
}

void TextLine::ellipsisNotFitProcess(EllipsisModal ellipsisModal) {
    if (fEllipsis) {
        return;
    }

    // Weird situation: ellipsis does not fit; no ellipsis then
    switch (ellipsisModal) {
        case EllipsisModal::TAIL:
            fClusterRange.end = fClusterRange.start;
            fGhostClusterRange.end = fClusterRange.start;
            fText.end = fText.start;
            fTextIncludingNewlines.end = fTextIncludingNewlines.start;
            fTextExcludingSpaces.end = fTextExcludingSpaces.start;
            fAdvance.fX = 0;
            break;
        case EllipsisModal::HEAD:
            fClusterRange.start = fClusterRange.end;
            fGhostClusterRange.start = fClusterRange.end;
            fText.start = fText.end;
            fTextIncludingNewlines.start = fTextIncludingNewlines.end;
            fTextExcludingSpaces.start = fTextExcludingSpaces.end;
            fAdvance.fX = 0;
            break;
        default:
            return;
    }
}

#ifdef OHOS_SUPPORT
void TextLine::createTailEllipsis(SkScalar maxWidth, const SkString& ellipsis, bool ltr, WordBreakType wordBreakType) {
    // Replace some clusters with the ellipsis
    // Go through the clusters in the reverse logical order
    // taking off cluster by cluster until the ellipsis fits
    SkScalar width = fAdvance.fX;
    RunIndex lastRun = EMPTY_RUN;
    std::unique_ptr<Run> ellipsisRun;
    int wordCount = 0;
    bool inWord = false;

    countWord(wordCount, inWord);

    bool iterForWord = false;

    for (auto clusterIndex = fClusterRange.end; clusterIndex > fClusterRange.start; --clusterIndex) {
        auto& cluster = fOwner->cluster(clusterIndex - 1);
        // Shape the ellipsis if the run has changed
        if (lastRun != cluster.runIndex()) {
            ellipsisRun = this->shapeEllipsis(ellipsis, &cluster);
            // We may need to continue
            lastRun = cluster.runIndex();
        }

        if (!cluster.isWordBreak()) {
            inWord = true;
        } else if (inWord) {
            --wordCount;
            inWord = false;
        }
        // See if it fits
        if (fOwner->getEllipsis() == fEllipsisString && ellipsisRun && width + ellipsisRun->advance().fX > maxWidth) {
            if (!cluster.isHardBreak()) {
                width -= cluster.width();
            }
            // Continue if the ellipsis does not fit
            iterForWord = (wordCount != 1 && wordBreakType != WordBreakType::BREAK_ALL && !cluster.isWordBreak());
            if (std::floor(width) > 0) {
                continue;
            }
        }

        if (iterForWord && !cluster.isWordBreak()) {
            width -= cluster.width();
            if (std::floor(width) > 0) {
                continue;
            }
        }

        fEllipsis = std::move(ellipsisRun);
        fEllipsis->fTextRange = TextRange(cluster.textRange().end, cluster.textRange().end + ellipsis.size());
        TailEllipsisUpdateLine(cluster, width, clusterIndex, wordBreakType);

        break;
    }

    fWidthWithSpaces = width;

    ellipsisNotFitProcess(EllipsisModal::TAIL);
}

void TextLine::TailEllipsisUpdateLine(Cluster& cluster, float width, size_t clusterIndex, WordBreakType wordBreakType)
{
    // We found enough room for the ellipsis
    fAdvance.fX = width;
    fEllipsis->setOwner(fOwner);
    fEllipsis->fClusterStart = cluster.textRange().end;

    // Let's update the line
    if (wordBreakType != WordBreakType::BREAK_HYPHEN) {
        fTextRangeReplacedByEllipsis = TextRange(cluster.textRange().end, fOwner->text().size());
    }
    fClusterRange.end = clusterIndex;
    fGhostClusterRange.end = fClusterRange.end;
    // Get the last run directions after clipping
    fEllipsisIndex = cluster.runIndex();
    fLastClipRunLtr = fOwner->run(fEllipsisIndex).leftToRight();
    fText.end = cluster.textRange().end;
    fTextIncludingNewlines.end = cluster.textRange().end;
    fTextExcludingSpaces.end = cluster.textRange().end;

    if (SkScalarNearlyZero(width)) {
        fRunsInVisualOrder.reset();
    }
}
#endif

#ifdef OHOS_SUPPORT
void TextLine::createHeadEllipsis(SkScalar maxWidth, const SkString& ellipsis, bool) {
    if (fAdvance.fX <= maxWidth) {
        return;
    }
    SkScalar width = fAdvance.fX;
    std::unique_ptr<Run> ellipsisRun;
    RunIndex lastRun = EMPTY_RUN;
    for (auto clusterIndex = fGhostClusterRange.start; clusterIndex < fGhostClusterRange.end; ++clusterIndex) {
        auto& cluster = fOwner->cluster(clusterIndex);
        // Shape the ellipsis if the run has changed
        if (lastRun != cluster.runIndex()) {
            ellipsisRun = this->shapeEllipsis(ellipsis, &cluster);
            // We may need to continue
            lastRun = cluster.runIndex();
        }
        // See if it fits
        if (ellipsisRun && width + ellipsisRun->advance().fX > maxWidth) {
            width -= cluster.width();
            // Continue if the ellipsis does not fit
            if (std::floor(width) > 0) {
                continue;
            }
        }

        // Get the last run directions after clipping
        fEllipsisIndex = cluster.runIndex();
        fLastClipRunLtr = fOwner->run(fEllipsisIndex).leftToRight();

        // We found enough room for the ellipsis
        fAdvance.fX = width + ellipsisRun->advance().fX;
        fEllipsis = std::move(ellipsisRun);
        fEllipsis->setOwner(fOwner);
        fTextRangeReplacedByEllipsis = TextRange(0, cluster.textRange().start);
        fClusterRange.start = clusterIndex;
        fGhostClusterRange.start = fClusterRange.start;
        fEllipsis->fClusterStart = 0;
        fText.start = cluster.textRange().start;
        fTextIncludingNewlines.start = cluster.textRange().start;
        fTextExcludingSpaces.start = cluster.textRange().start;
        break;
    }

    fWidthWithSpaces = width;

    ellipsisNotFitProcess(EllipsisModal::HEAD);
}
#endif

static inline SkUnichar nextUtf8Unit(const char** ptr, const char* end) {
    SkUnichar val = SkUTF::NextUTF8(ptr, end);
    return val < 0 ? 0xFFFD : val;
}

std::unique_ptr<Run> TextLine::shapeEllipsis(const SkString& ellipsis, const Cluster* cluster) {
#ifdef OHOS_SUPPORT
    fEllipsisString = ellipsis;
#endif
    class ShapeHandler final : public SkShaper::RunHandler {
    public:
        ShapeHandler(SkScalar lineHeight, bool useHalfLeading, SkScalar baselineShift, const SkString& ellipsis)
            : fRun(nullptr), fLineHeight(lineHeight), fUseHalfLeading(useHalfLeading), fBaselineShift(baselineShift), fEllipsis(ellipsis) {}
        std::unique_ptr<Run> run() & { return std::move(fRun); }

    private:
        void beginLine() override {}

        void runInfo(const RunInfo&) override {}

        void commitRunInfo() override {}

        Buffer runBuffer(const RunInfo& info) override {
            SkASSERT(!fRun);
            fRun = std::make_unique<Run>(nullptr, info, 0, fLineHeight, fUseHalfLeading, fBaselineShift, 0, 0);
            return fRun->newRunBuffer();
        }

        void commitRunBuffer(const RunInfo& info) override {
            fRun->fAdvance.fX = info.fAdvance.fX;
            fRun->fAdvance.fY = fRun->advance().fY;
            fRun->fPlaceholderIndex = std::numeric_limits<size_t>::max();
            fRun->fEllipsis = true;
        }

        void commitLine() override {}

        std::unique_ptr<Run> fRun;
        SkScalar fLineHeight;
        bool fUseHalfLeading;
        SkScalar fBaselineShift;
        SkString fEllipsis;
    };

    const Run& run = cluster->run();
    TextStyle textStyle = fOwner->paragraphStyle().getTextStyle();
    for (auto i = fBlockRange.start; i < fBlockRange.end; ++i) {
        auto& block = fOwner->block(i);
        if (run.leftToRight() && cluster->textRange().end <= block.fRange.end) {
            textStyle = block.fStyle;
            break;
        } else if (!run.leftToRight() && cluster->textRange().start <= block.fRange.end) {
            textStyle = block.fStyle;
            break;
        }
    }

#ifndef USE_SKIA_TXT
    auto shaped = [&](sk_sp<SkTypeface> typeface, bool fallback) -> std::unique_ptr<Run> {
#else
    auto shaped = [&](std::shared_ptr<RSTypeface> typeface, bool fallback) -> std::unique_ptr<Run> {
#endif
        ShapeHandler handler(run.heightMultiplier(), run.useHalfLeading(), run.baselineShift(), ellipsis);
#ifndef USE_SKIA_TXT
        SkFont font(typeface, textStyle.getFontSize());
        font.setEdging(SkFont::Edging::kAntiAlias);
        font.setHinting(SkFontHinting::kSlight);
        font.setSubpixel(true);
#else
        RSFont font(typeface, textStyle.getFontSize(), 1, 0);
        font.SetEdging(RSDrawing::FontEdging::ANTI_ALIAS);
        font.SetHinting(RSDrawing::FontHinting::SLIGHT);
        font.SetSubpixel(true);
#endif

#ifndef USE_SKIA_TXT
        std::unique_ptr<SkShaper> shaper = SkShaper::MakeShapeDontWrapOrReorder(
                            fOwner->getUnicode()->copy(),
                            fallback ? SkFontMgr::RefDefault() : SkFontMgr::RefEmpty());
#else
        std::unique_ptr<SkShaper> shaper = SkShaper::MakeShapeDontWrapOrReorder(
                            fOwner->getUnicode()->copy(),
                            fallback ? RSFontMgr::CreateDefaultFontMgr() : RSFontMgr::CreateDefaultFontMgr());
#endif
        shaper->shape(ellipsis.c_str(),
                      ellipsis.size(),
                      font,
                      true,
                      std::numeric_limits<SkScalar>::max(),
                      &handler);
        auto ellipsisRun = handler.run();
        ellipsisRun->fTextRange = TextRange(0, ellipsis.size());
        ellipsisRun->fOwner = fOwner;
        return ellipsisRun;
    };

    // Check all allowed fonts
    auto typefaces = fOwner->fontCollection()->findTypefaces(
            textStyle.getFontFamilies(), textStyle.getFontStyle(), textStyle.getFontArguments());
    for (const auto& typeface : typefaces) {
        auto ellipsisRun = shaped(typeface, false);
        if (ellipsisRun->isResolved()) {
            return ellipsisRun;
        }
    }

    // Try the fallback
    if (fOwner->fontCollection()->fontFallbackEnabled()) {
        const char* ch = ellipsis.c_str();
        SkUnichar unicode = nextUtf8Unit(&ch, ellipsis.c_str() + ellipsis.size());

        auto typeface = fOwner->fontCollection()->defaultFallback(
            unicode, textStyle.getFontStyle(), textStyle.getLocale());
        if (typeface) {
            if (textStyle.getFontArguments()) {
                typeface = fOwner->fontCollection()->CloneTypeface(typeface, textStyle.getFontArguments());
            }
            auto ellipsisRun = shaped(typeface, true);
            if (ellipsisRun->isResolved()) {
                return ellipsisRun;
            }
        }
    }

    // Check the current font
#ifndef USE_SKIA_TXT
    auto ellipsisRun = shaped(run.fFont.refTypeface(), false);
#else
    auto ellipsisRun = shaped(const_cast<RSFont&>(run.fFont).GetTypeface(), false);
#endif
    if (ellipsisRun->isResolved()) {
        return ellipsisRun;
    }
    return ellipsisRun;
}

#ifdef OHOS_SUPPORT
void TextLine::measureTextWithSpacesAtTheEnd(ClipContext& context, bool includeGhostSpaces) const
{
    if (compareRound(context.clip.fRight, fAdvance.fX, fOwner->getApplyRoundingHack()) > 0 && !includeGhostSpaces &&
        fAdvance.fX > 0) {
        // There are few cases when we need it.
        // The most important one: we measure the text with spaces at the end (or at the beginning in RTL)
        // and we should ignore these spaces
        if (fOwner->paragraphStyle().getTextDirection() == TextDirection::kLtr) {
            // We only use this member for LTR
            context.fExcludedTrailingSpaces = std::max(context.clip.fRight - fAdvance.fX, 0.0f);
            context.clippingNeeded = true;
            context.clip.fRight = fAdvance.fX;
        }
    }
}
#endif

TextLine::ClipContext TextLine::measureTextInsideOneRun(TextRange textRange,
                                                        const Run* run,
                                                        SkScalar runOffsetInLine,
                                                        SkScalar textOffsetInRunInLine,
                                                        bool includeGhostSpaces,
                                                        TextAdjustment textAdjustment) const {
    ClipContext result = { run, 0, run->size(), 0, SkRect::MakeEmpty(), 0, false };

    if (run->fEllipsis) {
        // Both ellipsis and placeholders can only be measured as one glyph
        result.fTextShift = runOffsetInLine;
        result.clip = SkRect::MakeXYWH(runOffsetInLine,
                                       sizes().runTop(run, this->fAscentStyle),
                                       run->advance().fX,
                                       run->calculateHeight(this->fAscentStyle,this->fDescentStyle));
        return result;
    } else if (run->isPlaceholder()) {
        result.fTextShift = runOffsetInLine;
        if (SkScalarIsFinite(run->fFontMetrics.fAscent)) {
          result.clip = SkRect::MakeXYWH(runOffsetInLine,
                                         sizes().runTop(run, this->fAscentStyle),
                                         run->advance().fX,
                                         run->calculateHeight(this->fAscentStyle,this->fDescentStyle));
        } else {
            result.clip = SkRect::MakeXYWH(runOffsetInLine, run->fFontMetrics.fAscent, run->advance().fX, 0);
        }
        return result;
    } else if (textRange.empty()) {
        return result;
    }

    TextRange originalTextRange(textRange); // We need it for proportional measurement
    // Find [start:end] clusters for the text
    while (true) {
        // Update textRange by cluster edges (shift start up to the edge of the cluster)
        // TODO: remove this limitation?
        TextRange updatedTextRange;
        bool found;
        std::tie(found, updatedTextRange.start, updatedTextRange.end) =
                                        run->findLimitingGlyphClusters(textRange);
        if (!found) {
            return result;
        }

        if ((textAdjustment & TextAdjustment::Grapheme) == 0) {
            textRange = updatedTextRange;
            break;
        }

        // Update text range by grapheme edges (shift start up to the edge of the grapheme)
        std::tie(found, updatedTextRange.start, updatedTextRange.end) =
                                    run->findLimitingGraphemes(updatedTextRange);
        if (updatedTextRange == textRange) {
            break;
        }

        // Some clusters are inside graphemes and we need to adjust them
        //SkDebugf("Correct range: [%d:%d) -> [%d:%d)\n", textRange.start, textRange.end, startIndex, endIndex);
        textRange = updatedTextRange;

        // Move the start until it's on the grapheme edge (and glypheme, too)
    }
    Cluster* start = &fOwner->cluster(fOwner->clusterIndex(textRange.start));
    Cluster* end = &fOwner->cluster(fOwner->clusterIndex(textRange.end - (textRange.width() == 0 ? 0 : 1)));

    if (!run->leftToRight()) {
        std::swap(start, end);
    }
    result.pos = start->startPos();
    result.size = (end->isHardBreak() ? end->startPos() : end->endPos()) - start->startPos();
    auto textStartInRun = run->positionX(start->startPos());
    auto textStartInLine = runOffsetInLine + textOffsetInRunInLine;
    if (!run->leftToRight()) {
        std::swap(start, end);
    }
/*
    if (!run->fJustificationShifts.empty()) {
        SkDebugf("Justification for [%d:%d)\n", textRange.start, textRange.end);
        for (auto i = result.pos; i < result.pos + result.size; ++i) {
            auto j = run->fJustificationShifts[i];
            SkDebugf("[%d] = %f %f\n", i, j.fX, j.fY);
        }
    }
*/
    // Calculate the clipping rectangle for the text with cluster edges
    // There are 2 cases:
    // EOL (when we expect the last cluster clipped without any spaces)
    // Anything else (when we want the cluster width contain all the spaces -
    // coming from letter spacing or word spacing or justification)
    result.clip =
            SkRect::MakeXYWH(0,
                             sizes().runTop(run, this->fAscentStyle),
                             run->calculateWidth(result.pos, result.pos + result.size, false),
                             run->calculateHeight(this->fAscentStyle,this->fDescentStyle));

    // Correct the width in case the text edges don't match clusters
    // TODO: This is where we get smart about selecting a part of a cluster
    //  by shaping each grapheme separately and then use the result sizes
    //  to calculate the proportions
    auto leftCorrection = start->sizeToChar(originalTextRange.start);
    auto rightCorrection = end->sizeFromChar(originalTextRange.end - 1);
    /*
    SkDebugf("[%d: %d) => [%d: %d), @%d, %d: [%f:%f) + [%f:%f) = ", // جَآَهُ
             originalTextRange.start, originalTextRange.end, textRange.start, textRange.end,
             result.pos, result.size,
             result.clip.fLeft, result.clip.fRight, leftCorrection, rightCorrection);
     */
    result.clippingNeeded = leftCorrection != 0 || rightCorrection != 0;
    if (run->leftToRight()) {
        result.clip.fLeft += leftCorrection;
        result.clip.fRight -= rightCorrection;
        textStartInLine -= leftCorrection;
    } else {
        result.clip.fRight -= leftCorrection;
        result.clip.fLeft += rightCorrection;
        textStartInLine -= rightCorrection;
    }

    result.clip.offset(textStartInLine, 0);
    //SkDebugf("@%f[%f:%f)\n", textStartInLine, result.clip.fLeft, result.clip.fRight);

#ifdef OHOS_SUPPORT
    measureTextWithSpacesAtTheEnd(result, includeGhostSpaces);
#else
    if (compareRound(result.clip.fRight, fAdvance.fX, fOwner->getApplyRoundingHack()) > 0 && !includeGhostSpaces) {
        // There are few cases when we need it.
        // The most important one: we measure the text with spaces at the end (or at the beginning in RTL)
        // and we should ignore these spaces
        if (fOwner->paragraphStyle().getTextDirection() == TextDirection::kLtr) {
            // We only use this member for LTR
            result.fExcludedTrailingSpaces = std::max(result.clip.fRight - fAdvance.fX, 0.0f);
            result.clippingNeeded = true;
            result.clip.fRight = fAdvance.fX;
        }
    }

    if (result.clip.width() < 0) {
        // Weird situation when glyph offsets move the glyph to the left
        // (happens with zalgo texts, for instance)
        result.clip.fRight = result.clip.fLeft;
    }
#endif

    // The text must be aligned with the lineOffset
    result.fTextShift = textStartInLine - textStartInRun;

    return result;
}

void TextLine::iterateThroughClustersInGlyphsOrder(bool reversed,
                                                   bool includeGhosts,
                                                   const ClustersVisitor& visitor) const {
    // Walk through the clusters in the logical order (or reverse)
    SkSpan<const size_t> runs(fRunsInVisualOrder.data(), fRunsInVisualOrder.size());
    bool ignore = false;
    ClusterIndex index = 0;
    directional_for_each(runs, !reversed, [&](decltype(runs[0]) r) {
        if (ignore) return;
        auto run = this->fOwner->run(r);
        auto trimmedRange = fClusterRange.intersection(run.clusterRange());
        auto trailedRange = fGhostClusterRange.intersection(run.clusterRange());
        SkASSERT(trimmedRange.start == trailedRange.start);

        auto trailed = fOwner->clusters(trailedRange);
        auto trimmed = fOwner->clusters(trimmedRange);
        directional_for_each(trailed, reversed != run.leftToRight(), [&](Cluster& cluster) {
            if (ignore) return;
            bool ghost =  &cluster >= trimmed.end();
            if (!includeGhosts && ghost) {
                return;
            }
            if (!visitor(&cluster, index++, ghost)) {

                ignore = true;
                return;
            }
        });
    });
}

#ifdef OHOS_SUPPORT
void TextLine::computeNextPaintGlyphRange(ClipContext& context,
    const TextRange& lastGlyphRange, StyleType styleType) const
{
    if (styleType != StyleType::kForeground) {
        return;
    }
    TextRange curGlyphRange = TextRange(context.pos, context.pos + context.size);
    auto intersect = intersected(lastGlyphRange, curGlyphRange);
    if (intersect == EMPTY_TEXT || (intersect.start != curGlyphRange.start && intersect.end != curGlyphRange.end)) {
        return;
    }
    if (intersect.start == curGlyphRange.start) {
        curGlyphRange = TextRange(intersect.end, curGlyphRange.end);
    } else if (intersect.end == curGlyphRange.end) {
        curGlyphRange = TextRange(curGlyphRange.start, intersect.start);
    }

    context.pos = curGlyphRange.start;
    context.size = curGlyphRange.width();
}
#endif

SkScalar TextLine::iterateThroughSingleRunByStyles(TextAdjustment textAdjustment,
                                                   const Run* run,
                                                   SkScalar runOffset,
                                                   TextRange textRange,
                                                   StyleType styleType,
                                                   const RunStyleVisitor& visitor) const {
    auto includeGhostSpaces = (styleType == StyleType::kDecorations || styleType == StyleType::kBackground ||
        styleType == StyleType::kNone);
    auto correctContext = [&](TextRange textRange, SkScalar textOffsetInRun) -> ClipContext {
        auto result = this->measureTextInsideOneRun(
            textRange, run, runOffset, textOffsetInRun, includeGhostSpaces, textAdjustment);
        if (styleType == StyleType::kDecorations) {
            // Decorations are drawn based on the real font metrics (regardless of styles and strut)
            result.clip.fTop = this->sizes().runTop(run, LineMetricStyle::CSS) - run->baselineShift();
            result.clip.fBottom = result.clip.fTop +
                                  run->calculateHeight(LineMetricStyle::CSS, LineMetricStyle::CSS);
        }
        return result;
    };

    if (run->fEllipsis) {
        // Extra efforts to get the ellipsis text style
        ClipContext clipContext = correctContext(run->textRange(), 0.0f);
        for (BlockIndex index = fBlockRange.start; index < fBlockRange.end; ++index) {
            auto block = fOwner->styles().begin() + index;
#ifdef OHOS_SUPPORT
            TextRange intersect = intersected(block->fRange,
                TextRange(fEllipsis->textRange().start - 1, fEllipsis->textRange().end));
            if (intersect.width() > 0) {
                visitor(fTextRangeReplacedByEllipsis, block->fStyle, clipContext);
                return run->advance().fX;
            }
#else
           if (block->fRange.start >= run->fClusterStart && block->fRange.end < run->fClusterStart) {
               visitor(fTextRangeReplacedByEllipsis, block->fStyle, clipContext);
               return run->advance().fX;
           }
#endif
        }
        SkASSERT(false);
    }

    if (styleType == StyleType::kNone) {
        ClipContext clipContext = correctContext(textRange, 0.0f);
#ifdef OHOS_SUPPORT
        if (clipContext.clip.height() > 0 ||
            (run->isPlaceholder() && SkScalarNearlyZero(clipContext.clip.height()))) {
#else
        if (clipContext.clip.height() > 0) {
#endif
            visitor(textRange, TextStyle(), clipContext);
            return clipContext.clip.width();
        } else {
            return 0;
        }
    }

    TextIndex start = EMPTY_INDEX;
    size_t size = 0;
    const TextStyle* prevStyle = nullptr;
    SkScalar textOffsetInRun = 0;
#ifdef OHOS_SUPPORT
    TextRange lastGlyphRange = EMPTY_TEXT;
#endif
    const BlockIndex blockRangeSize = fBlockRange.end - fBlockRange.start;
    for (BlockIndex index = 0; index <= blockRangeSize; ++index) {

        TextRange intersect;
        TextStyle* style = nullptr;
        if (index < blockRangeSize) {
            auto block = fOwner->styles().begin() +
                 (run->leftToRight() ? fBlockRange.start + index : fBlockRange.end - index - 1);

            // Get the text
            intersect = intersected(block->fRange, textRange);
            if (intersect.width() == 0) {
                if (start == EMPTY_INDEX) {
                    // This style is not applicable to the text yet
                    continue;
                } else {
                    // We have found all the good styles already
                    // but we need to process the last one of them
                    intersect = TextRange(start, start + size);
                    index = fBlockRange.end;
                }
            } else {
                // Get the style
                style = &block->fStyle;
                if (start != EMPTY_INDEX && style->matchOneAttribute(styleType, *prevStyle)) {
                    size += intersect.width();
                    // RTL text intervals move backward
                    start = std::min(intersect.start, start);
                    continue;
                } else if (start == EMPTY_INDEX ) {
                    // First time only
                    prevStyle = style;
                    size = intersect.width();
                    start = intersect.start;
                    continue;
                }
            }
        } else if (prevStyle != nullptr) {
            // This is the last style
        } else {
            break;
        }

        // We have the style and the text
        auto runStyleTextRange = TextRange(start, start + size);
        ClipContext clipContext = correctContext(runStyleTextRange, textOffsetInRun);
        textOffsetInRun += clipContext.clip.width();
        if (clipContext.clip.height() == 0) {
            continue;
        }

        RectStyle temp;
        if (styleType == StyleType::kBackground &&
            prevStyle->getBackgroundRect() != temp &&
            prevStyle->getHeight() != 0) {
#ifdef OHOS_SUPPORT
                clipContext.clip.fTop = run->fFontMetrics.fAscent + this->baseline() + run->fBaselineShift;
#else
                clipContext.clip.fTop = run->fFontMetrics.fAscent - run->fCorrectAscent;
#endif
                clipContext.clip.fBottom = clipContext.clip.fTop + run->fFontMetrics.fDescent -
                    run->fFontMetrics.fAscent;
        }
#ifdef OHOS_SUPPORT
        computeNextPaintGlyphRange(clipContext, lastGlyphRange, styleType);
        if (clipContext.size != 0) {
            lastGlyphRange = TextRange(clipContext.pos, clipContext.pos + clipContext.size);
        }
#endif
        visitor(runStyleTextRange, *prevStyle, clipContext);

        // Start all over again
        prevStyle = style;
        start = intersect.start;
        size = intersect.width();
    }
    return textOffsetInRun;
}

#ifdef OHOS_SUPPORT
bool TextLine::processEllipsisRun(bool& isAlreadyUseEllipsis,
                                  SkScalar& runOffset,
                                  EllipsisReadStrategy ellipsisReadStrategy,
                                  const RunVisitor& visitor,
                                  SkScalar& runWidthInLine) const {
    isAlreadyUseEllipsis = true;
    runOffset += this->ellipsis()->offset().fX;
    if (ellipsisReadStrategy == EllipsisReadStrategy::READ_REPLACED_WORD) {
        if (!visitor(ellipsis(), runOffset, fTextRangeReplacedByEllipsis, &runWidthInLine)) {
            LOGE("Visitor process ellipsis replace word error!");
            return false;
        }
    } else if (ellipsisReadStrategy == EllipsisReadStrategy::READ_ELLIPSIS_WORD) {
        if (!visitor(ellipsis(), runOffset, ellipsis()->textRange(), &runWidthInLine)) {
            LOGE("Visitor process ellipsis word error!");
            return false;
        }
    } else {
        runWidthInLine = this->ellipsis()->advance().fX;
    }
    return true;
}
#endif

#ifdef OHOS_SUPPORT
void TextLine::iterateThroughVisualRuns(EllipsisReadStrategy ellipsisReadStrategy,
                                        bool includingGhostSpaces,
                                        const RunVisitor& visitor) const {
    // Walk through all the runs that intersect with the line in visual order
    SkScalar width = 0;
    SkScalar runOffset = 0;
    SkScalar totalWidth = 0;
#ifdef OHOS_SUPPORT
    bool ellipsisModeIsHead = fIsTextLineEllipsisHeadModal ? true :
            fOwner->paragraphStyle().getEllipsisMod() == EllipsisModal::HEAD;
#else
    bool ellipsisModeIsHead = fOwner->paragraphStyle().getEllipsisMod() == EllipsisModal::HEAD;
#endif
    bool isAlreadyUseEllipsis = false;
    auto textRange = includingGhostSpaces ? this->textWithNewlines() : this->trimmedText();

    if (fRunsInVisualOrder.size() == 0 && fEllipsis != nullptr) {
        if (!processEllipsisRun(isAlreadyUseEllipsis, runOffset, ellipsisReadStrategy, visitor, width)) {
            return;
        }
        totalWidth += width;
    }

    for (auto& runIndex : fRunsInVisualOrder) {
        // add the lastClipRun's left ellipsis if necessary
        if (!isAlreadyUseEllipsis && fEllipsisIndex == runIndex &&
            ((!fLastClipRunLtr && !ellipsisModeIsHead) || (ellipsisModeIsHead && fLastClipRunLtr))) {
            if (!processEllipsisRun(isAlreadyUseEllipsis, runOffset, ellipsisReadStrategy, visitor, width)) {
                return;
            }
            runOffset += width;
            totalWidth += width;
        }

        const auto run = &this->fOwner->run(runIndex);
        auto lineIntersection = intersected(run->textRange(), textRange);
        if (lineIntersection.width() == 0 && this->width() != 0) {
            // TODO: deal with empty runs in a better way
            continue;
        }
        if (!run->leftToRight() && runOffset == 0 && includingGhostSpaces) {
            // runOffset does not take in account a possibility
            // that RTL run could start before the line (trailing spaces)
            // so we need to do runOffset -= "trailing whitespaces length"
            TextRange whitespaces = intersected(
                    TextRange(fTextExcludingSpaces.end, fTextIncludingNewlines.end), run->fTextRange);
            if (whitespaces.width() > 0) {
                auto whitespacesLen = measureTextInsideOneRun(whitespaces, run, runOffset, 0, true,
                                                              TextAdjustment::GlyphCluster).clip.width();
                runOffset -= whitespacesLen;
            }
        }

        if (!visitor(run, runOffset, lineIntersection, &width)) {
            return;
        }

        runOffset += width;
        totalWidth += width;

        // add the lastClipRun's right ellipsis if necessary
        if (!isAlreadyUseEllipsis && fEllipsisIndex == runIndex) {
            if (!processEllipsisRun(isAlreadyUseEllipsis, runOffset, ellipsisReadStrategy, visitor, width)) {
                return;
            }
            runOffset += width;
            totalWidth += width;
        }
    }

    if (!includingGhostSpaces && compareRound(totalWidth, this->width(), fOwner->getApplyRoundingHack()) != 0) {
    // This is a very important assert!
    // It asserts that 2 different ways of calculation come with the same results
        SkDebugf("ASSERT: %f != %f\n", totalWidth, this->width());
        SkASSERT(false);
    }
}
#else
void TextLine::iterateThroughVisualRuns(bool includingGhostSpaces, const RunVisitor& visitor) const {

    // Walk through all the runs that intersect with the line in visual order
    SkScalar width = 0;
    SkScalar runOffset = 0;
    SkScalar totalWidth = 0;
    auto textRange = includingGhostSpaces ? this->textWithNewlines() : this->trimmedText();
    for (auto& runIndex : fRunsInVisualOrder) {

        const auto run = &this->fOwner->run(runIndex);
        auto lineIntersection = intersected(run->textRange(), textRange);
        if (lineIntersection.width() == 0 && this->width() != 0) {
            // TODO: deal with empty runs in a better way
            continue;
        }
        if (!run->leftToRight() && runOffset == 0 && includingGhostSpaces) {
            // runOffset does not take in account a possibility
            // that RTL run could start before the line (trailing spaces)
            // so we need to do runOffset -= "trailing whitespaces length"
            TextRange whitespaces = intersected(
                    TextRange(fTextExcludingSpaces.end, fTextIncludingNewlines.end), run->fTextRange);
            if (whitespaces.width() > 0) {
                auto whitespacesLen = measureTextInsideOneRun(whitespaces, run, runOffset, 0, true, false).clip.width();
                runOffset -= whitespacesLen;
            }
        }
        runOffset += width;
        totalWidth += width;
        if (!visitor(run, runOffset, lineIntersection, &width)) {
            return;
        }
    }

    runOffset += width;
    totalWidth += width;

    if (this->ellipsis() != nullptr) {
        if (visitor(ellipsis(), runOffset, ellipsis()->textRange(), &width)) {
            totalWidth += width;
        }
    }

    // This is a very important assert!
    // It asserts that 2 different ways of calculation come with the same results
    if (!includingGhostSpaces && compareRound(totalWidth, this->width()) != 0) {
        SkDebugf("ASSERT: %f != %f\n", totalWidth, this->width());
        SkASSERT(false);
    }
}
#endif

SkVector TextLine::offset() const {
    return fOffset + SkVector::Make(fShift, 0);
}

LineMetrics TextLine::getMetrics() const {
    LineMetrics result;

    // Fill out the metrics
    fOwner->ensureUTF16Mapping();
    result.fStartIndex = fOwner->getUTF16Index(fTextExcludingSpaces.start);
    result.fEndExcludingWhitespaces = fOwner->getUTF16Index(fTextExcludingSpaces.end);
    result.fEndIndex = fOwner->getUTF16Index(fText.end);
    result.fEndIncludingNewline = fOwner->getUTF16Index(fTextIncludingNewlines.end);
    result.fHardBreak = endsWithHardLineBreak();
    result.fAscent = - fMaxRunMetrics.ascent();
    result.fDescent = fMaxRunMetrics.descent();
    result.fUnscaledAscent = - fMaxRunMetrics.ascent(); // TODO: implement
    result.fHeight = fAdvance.fY;
    result.fWidth = fAdvance.fX;
    if (fOwner->getApplyRoundingHack()) {
        result.fHeight = littleRound(result.fHeight);
        result.fWidth = littleRound(result.fWidth);
    }
    result.fLeft = this->offset().fX;
    // This is Flutter definition of a baseline
    result.fBaseline = this->offset().fY + this->height() - this->sizes().descent();
    result.fLineNumber = this - fOwner->lines().begin();
    result.fWidthWithSpaces = fWidthWithSpaces;
    result.fTopHeight = this->offset().fY;

    // Fill out the style parts
#ifdef OHOS_SUPPORT
    this->iterateThroughVisualRuns(EllipsisReadStrategy::READ_REPLACED_WORD, false,
#else
    this->iterateThroughVisualRuns(false,
#endif
        [this, &result]
        (const Run* run, SkScalar runOffsetInLine, TextRange textRange, SkScalar* runWidthInLine) {
        if (run->placeholderStyle() != nullptr) {
            *runWidthInLine = run->advance().fX;
            return true;
        }
        *runWidthInLine = this->iterateThroughSingleRunByStyles(
        TextAdjustment::GlyphCluster, run, runOffsetInLine, textRange, StyleType::kForeground,
        [&result, &run](TextRange textRange, const TextStyle& style, const ClipContext& context) {
#ifndef USE_SKIA_TXT
            SkFontMetrics fontMetrics;
            run->fFont.getMetrics(&fontMetrics);
#else
            RSFontMetrics fontMetrics;
            run->fFont.GetMetrics(&fontMetrics);
#endif
#ifdef OHOS_SUPPORT
            auto decompressFont = run->fFont;
            scaleFontWithCompressionConfig(decompressFont, ScaleOP::DECOMPRESS);
            metricsIncludeFontPadding(&fontMetrics, decompressFont);
#endif
            StyleMetrics styleMetrics(&style, fontMetrics);
            result.fLineMetrics.emplace(textRange.start, styleMetrics);
        });
        return true;
    });

    return result;
}

bool TextLine::isFirstLine() const {
    return this == &fOwner->lines().front();
}

bool TextLine::isLastLine() const {
    return this == &fOwner->lines().back();
}

bool TextLine::endsWithHardLineBreak() const {
    // TODO: For some reason Flutter imagines a hard line break at the end of the last line.
    //  To be removed...
#ifdef OHOS_SUPPORT
    return (fGhostClusterRange.width() > 0 && fOwner->cluster(fGhostClusterRange.end - 1).isHardBreak()) ||
           (fEllipsis != nullptr && fOwner->getEllipsis() == fEllipsisString) ||
           fGhostClusterRange.end == fOwner->clusters().size() - 1;
#else
    return (fGhostClusterRange.width() > 0 && fOwner->cluster(fGhostClusterRange.end - 1).isHardBreak()) ||
           fEllipsis != nullptr ||
           fGhostClusterRange.end == fOwner->clusters().size() - 1;
#endif
}

void TextLine::getRectsForRange(TextRange textRange0,
                                RectHeightStyle rectHeightStyle,
                                RectWidthStyle rectWidthStyle,
                                std::vector<TextBox>& boxes) const
{
    const Run* lastRun = nullptr;
    auto startBox = boxes.size();
#ifdef OHOS_SUPPORT
    this->iterateThroughVisualRuns(EllipsisReadStrategy::READ_REPLACED_WORD, true,
#else
    this->iterateThroughVisualRuns(true,
#endif
        [textRange0, rectHeightStyle, rectWidthStyle, &boxes, &lastRun, startBox, this]
        (const Run* run, SkScalar runOffsetInLine, TextRange textRange, SkScalar* runWidthInLine) {
        *runWidthInLine = this->iterateThroughSingleRunByStyles(
        TextAdjustment::GraphemeGluster, run, runOffsetInLine, textRange, StyleType::kNone,
        [run, runOffsetInLine, textRange0, rectHeightStyle, rectWidthStyle, &boxes, &lastRun, startBox, this]
        (TextRange textRange, const TextStyle& style, const TextLine::ClipContext& lineContext) {

            auto intersect = textRange * textRange0;
#ifdef OHOS_SUPPORT
            if (intersect.empty() && !this->fBreakWithHyphen) {
#else
            if (intersect.empty()) {
#endif
                return true;
            }

            auto paragraphStyle = fOwner->paragraphStyle();

            // Found a run that intersects with the text
            auto context = this->measureTextInsideOneRun(
                    intersect, run, runOffsetInLine, 0, true, TextAdjustment::GraphemeGluster);
            SkRect clip = context.clip;
            clip.offset(lineContext.fTextShift - context.fTextShift, 0);

            switch (rectHeightStyle) {
                case RectHeightStyle::kMax:
                    // TODO: Change it once flutter rolls into google3
                    //  (probably will break things if changed before)
                    clip.fBottom = this->height();
                    clip.fTop = this->sizes().delta();
                    break;
                case RectHeightStyle::kIncludeLineSpacingTop: {
                    clip.fBottom = this->height();
                    clip.fTop = this->sizes().delta();
                    auto verticalShift = this->sizes().rawAscent() - this->sizes().ascent();
                    if (isFirstLine()) {
                        clip.fTop += verticalShift;
                    }
                    break;
                }
                case RectHeightStyle::kIncludeLineSpacingMiddle: {
                    clip.fBottom = this->height();
                    clip.fTop = this->sizes().delta();
                    auto verticalShift = this->sizes().rawAscent() - this->sizes().ascent();
                    clip.offset(0, verticalShift / 2.0);
                    if (isFirstLine()) {
                        clip.fTop += verticalShift / 2.0;
                    }
                    if (isLastLine()) {
                        clip.fBottom -= verticalShift / 2.0;
                    }
                    break;
                 }
                case RectHeightStyle::kIncludeLineSpacingBottom: {
                    clip.fBottom = this->height();
                    clip.fTop = this->sizes().delta();
                    auto verticalShift = this->sizes().rawAscent() - this->sizes().ascent();
                    clip.offset(0, verticalShift);
                    if (isLastLine()) {
                        clip.fBottom -= verticalShift;
                    }
                    break;
                }
                case RectHeightStyle::kStrut: {
                    const auto& strutStyle = paragraphStyle.getStrutStyle();
                    if (strutStyle.getStrutEnabled()
                        && strutStyle.getFontSize() > 0) {
                        auto strutMetrics = fOwner->strutMetrics();
                        auto top = this->baseline();
                        clip.fTop = top + strutMetrics.ascent();
                        clip.fBottom = top + strutMetrics.descent();
                    }
                }
                break;
                case RectHeightStyle::kTight: {
                    if (run->fHeightMultiplier <= 0) {
                        break;
                    }
                    const auto effectiveBaseline = this->baseline() + this->sizes().delta();
                    clip.fTop = effectiveBaseline + run->ascent();
                    clip.fBottom = effectiveBaseline + run->descent();
                }
                break;
                default:
                    SkASSERT(false);
                break;
            }

            // Separate trailing spaces and move them in the default order of the paragraph
            // in case the run order and the paragraph order don't match
            SkRect trailingSpaces = SkRect::MakeEmpty();
            if (this->trimmedText().end <this->textWithNewlines().end && // Line has trailing space
                this->textWithNewlines().end == intersect.end &&         // Range is at the end of the line
                this->trimmedText().end > intersect.start)               // Range has more than just spaces
            {
                auto delta = this->spacesWidth();
                trailingSpaces = SkRect::MakeXYWH(0, 0, 0, 0);
                // There are trailing spaces in this run
                if (paragraphStyle.getTextAlign() == TextAlign::kJustify && isLastLine())
                {
                    // TODO: this is just a patch. Make it right later (when it's clear what and how)
                    trailingSpaces = clip;
                    if(run->leftToRight()) {
                        trailingSpaces.fLeft = this->width();
                        clip.fRight = this->width();
                    } else {
                        trailingSpaces.fRight = 0;
                        clip.fLeft = 0;
                    }
                } else if (paragraphStyle.getTextDirection() == TextDirection::kRtl &&
                    !run->leftToRight())
                {
                    // Split
                    trailingSpaces = clip;
                    trailingSpaces.fLeft = - delta;
                    trailingSpaces.fRight = 0;
                    clip.fLeft += delta;
                } else if (paragraphStyle.getTextDirection() == TextDirection::kLtr &&
                    run->leftToRight())
                {
                    // Split
                    trailingSpaces = clip;
                    trailingSpaces.fLeft = this->width();
                    trailingSpaces.fRight = trailingSpaces.fLeft + delta;
                    clip.fRight -= delta;
                }
            }

            clip.offset(this->offset());
            if (trailingSpaces.width() > 0) {
                trailingSpaces.offset(this->offset());
            }

            // Check if we can merge two boxes instead of adding a new one
            auto merge = [&lastRun, &context, &boxes](SkRect clip) {
                bool mergedBoxes = false;
                if (!boxes.empty() &&
                    lastRun != nullptr &&
                    context.run->leftToRight() == lastRun->leftToRight() &&
                    lastRun->placeholderStyle() == nullptr &&
                    context.run->placeholderStyle() == nullptr &&
                    nearlyEqual(lastRun->heightMultiplier(),
                                context.run->heightMultiplier()) &&
#ifndef USE_SKIA_TXT
                    lastRun->font() == context.run->font())
#else
                    IsRSFontEquals(lastRun->font(), context.run->font()))
#endif
                {
                    auto& lastBox = boxes.back();
                    if (nearlyEqual(lastBox.rect.fTop, clip.fTop) &&
                        nearlyEqual(lastBox.rect.fBottom, clip.fBottom) &&
                            (nearlyEqual(lastBox.rect.fLeft, clip.fRight) ||
                             nearlyEqual(lastBox.rect.fRight, clip.fLeft)))
                    {
                        lastBox.rect.fLeft = std::min(lastBox.rect.fLeft, clip.fLeft);
                        lastBox.rect.fRight = std::max(lastBox.rect.fRight, clip.fRight);
                        mergedBoxes = true;
                    }
                }
                lastRun = context.run;
                return mergedBoxes;
            };

            if (!merge(clip)) {
                boxes.emplace_back(clip, context.run->getTextDirection());
            }
            if (!nearlyZero(trailingSpaces.width()) && !merge(trailingSpaces)) {
                boxes.emplace_back(trailingSpaces, paragraphStyle.getTextDirection());
            }

            if (rectWidthStyle == RectWidthStyle::kMax && !isLastLine()) {
                // Align the very left/right box horizontally
                auto lineStart = this->offset().fX;
                auto lineEnd = this->offset().fX + this->width();
                auto left = boxes[startBox];
                auto right = boxes.back();
                if (left.rect.fLeft > lineStart && left.direction == TextDirection::kRtl) {
                    left.rect.fRight = left.rect.fLeft;
                    left.rect.fLeft = 0;
                    boxes.insert(boxes.begin() + startBox + 1, left);
                }
                if (right.direction == TextDirection::kLtr &&
                    right.rect.fRight >= lineEnd &&
                    right.rect.fRight < fOwner->widthWithTrailingSpaces()) {
                    right.rect.fLeft = right.rect.fRight;
                    right.rect.fRight = fOwner->widthWithTrailingSpaces();
                    boxes.emplace_back(right);
                }
            }

            return true;
        });
        return true;
    });
    if (fOwner->getApplyRoundingHack()) {
        for (auto& r : boxes) {
            r.rect.fLeft = littleRound(r.rect.fLeft);
            r.rect.fRight = littleRound(r.rect.fRight);
            r.rect.fTop = littleRound(r.rect.fTop);
            r.rect.fBottom = littleRound(r.rect.fBottom);
        }
    }
}

#ifdef OHOS_SUPPORT
void TextLine::extendCoordinateRange(PositionWithAffinity& positionWithAffinity) {
    if (fEllipsis == nullptr) {
        return;
    }
    // Extending coordinate index if the ellipsis's run is selected.
    EllipsisModal ellipsisModal = fOwner->paragraphStyle().getEllipsisMod();
    if (ellipsisModal == EllipsisModal::TAIL) {
        if (static_cast<size_t>(positionWithAffinity.position) > fOwner->getEllipsisTextRange().start &&
            static_cast<size_t>(positionWithAffinity.position) <= fOwner->getEllipsisTextRange().end) {
            positionWithAffinity.position = fOwner->getEllipsisTextRange().end;
        }
    } else if (ellipsisModal == EllipsisModal::HEAD) {
        if (static_cast<size_t>(positionWithAffinity.position) >= fOwner->getEllipsisTextRange().start &&
            static_cast<size_t>(positionWithAffinity.position) < fOwner->getEllipsisTextRange().end) {
            positionWithAffinity.position = fOwner->getEllipsisTextRange().start;
        }
    }
}
#endif

PositionWithAffinity TextLine::getGlyphPositionAtCoordinate(SkScalar dx) {

    if (SkScalarNearlyZero(this->width()) && SkScalarNearlyZero(this->spacesWidth())) {
        // TODO: this is one of the flutter changes that have to go away eventually
        //  Empty line is a special case in txtlib (but only when there are no spaces, too)
        auto utf16Index = fOwner->getUTF16Index(this->fTextExcludingSpaces.end);
        return { SkToS32(utf16Index) , kDownstream };
    }

    PositionWithAffinity result(0, Affinity::kDownstream);
#ifdef OHOS_SUPPORT
    this->iterateThroughVisualRuns(EllipsisReadStrategy::READ_REPLACED_WORD, true,
#else
    this->iterateThroughVisualRuns(true,
#endif
        [this, dx, &result]
        (const Run* run, SkScalar runOffsetInLine, TextRange textRange, SkScalar* runWidthInLine) {
            bool keepLooking = true;
            *runWidthInLine = this->iterateThroughSingleRunByStyles(
            TextAdjustment::GraphemeGluster, run, runOffsetInLine, textRange, StyleType::kNone,
            [this, run, dx, &result, &keepLooking]
            (TextRange textRange, const TextStyle& style, const TextLine::ClipContext& context0) {

                SkScalar offsetX = this->offset().fX;
                ClipContext context = context0;

                // Correct the clip size because libtxt counts trailing spaces
                if (run->leftToRight()) {
                    context.clip.fRight += context.fExcludedTrailingSpaces; // extending clip to the right
                } else {
                    // Clip starts from 0; we cannot extend it to the left from that
                }
                // However, we need to offset the clip
                context.clip.offset(offsetX, 0.0f);

                // This patch will help us to avoid a floating point error
                if (SkScalarNearlyEqual(context.clip.fRight, dx, 0.01f)) {
                    context.clip.fRight = dx;
                }

                if (dx <= context.clip.fLeft) {
                    // All the other runs are placed right of this one
                    auto utf16Index = fOwner->getUTF16Index(context.run->globalClusterIndex(context.pos));
                    if (run->leftToRight()) {
                        result = { SkToS32(utf16Index), kDownstream };
                        keepLooking = false;
                    } else {
#ifdef OHOS_SUPPORT
                        result = { SkToS32(utf16Index + 1), kUpstream };
                        size_t glyphCnt = context.run->glyphs().size();
                        if ((glyphCnt != 0) && ((context.run->fUtf8Range.size() / glyphCnt) == EMOJI_WIDTH)) {
                            result = { SkToS32(utf16Index + 2), kUpstream };
                        }
#else
                        result = { SkToS32(utf16Index + 1), kUpstream};
#endif
                        // If we haven't reached the end of the run we need to keep looking
                        keepLooking = context.pos != 0;
                    }
                    // For RTL we go another way
                    return !run->leftToRight();
                }

                if (dx >= context.clip.fRight) {
                    // We have to keep looking ; just in case keep the last one as the closest
                    auto utf16Index = fOwner->getUTF16Index(context.run->globalClusterIndex(context.pos + context.size));
                    if (run->leftToRight()) {
                        result = {SkToS32(utf16Index), kUpstream};
                    } else {
                        result = {SkToS32(utf16Index), kDownstream};
                    }
                    // For RTL we go another way
                    return run->leftToRight();
                }

                // So we found the run that contains our coordinates
                // Find the glyph position in the run that is the closest left of our point
                // TODO: binary search
                size_t found = context.pos;
                for (size_t index = context.pos; index < context.pos + context.size; ++index) {
                    // TODO: this rounding is done to match Flutter tests. Must be removed..
                    auto end = context.run->positionX(index) + context.fTextShift + offsetX;
                    if (fOwner->getApplyRoundingHack()) {
                        end = littleRound(end);
                    }
                    if (end > dx) {
                        break;
                    } else if (end == dx && !context.run->leftToRight()) {
                        // When we move RTL variable end points to the beginning of the code point which is included
                        found = index;
                        break;
                    }
                    found = index;
                }

                SkScalar glyphemePosLeft = context.run->positionX(found) + context.fTextShift + offsetX;
                SkScalar glyphemesWidth = context.run->positionX(found + 1) - context.run->positionX(found);

                // Find the grapheme range that contains the point
                auto clusterIndex8 = context.run->globalClusterIndex(found);
                auto clusterEnd8 = context.run->globalClusterIndex(found + 1);
                auto graphemes = fOwner->countSurroundingGraphemes({clusterIndex8, clusterEnd8});

                SkScalar center = (context.clip.right() + context.clip.left()) / 2;
                if (graphemes.size() > 1) {
                    // Calculate the position proportionally based on grapheme count
                    SkScalar averageGraphemeWidth = glyphemesWidth / graphemes.size();
                    SkScalar delta = dx - glyphemePosLeft;
                    int graphemeIndex = SkScalarNearlyZero(averageGraphemeWidth)
                                         ? 0
                                         : SkScalarFloorToInt(delta / averageGraphemeWidth);
                    auto graphemeCenter = glyphemePosLeft + graphemeIndex * averageGraphemeWidth +
                                          averageGraphemeWidth * fOwner->getTextSplitRatio();
                    auto graphemeUtf8Index = graphemes[graphemeIndex];
                    if ((dx < graphemeCenter) == context.run->leftToRight()) {
                        size_t utf16Index = fOwner->getUTF16Index(graphemeUtf8Index);
                        result = { SkToS32(utf16Index), kDownstream };
                    } else {
                        size_t utf16Index = fOwner->getUTF16Index(graphemeUtf8Index + 1);
                        result = { SkToS32(utf16Index), kUpstream };
                    }
                    // Keep UTF16 index as is
                } else if ((dx < center) == context.run->leftToRight()) {
                    size_t utf16Index = fOwner->getUTF16Index(clusterIndex8);
                    result = { SkToS32(utf16Index), kDownstream };
                } else {
#ifdef OHOS_SUPPORT
                    size_t utf16Index = 0;
                    size_t glyphCnt = context.run->glyphs().size();
                    if ((glyphCnt != 0) && !context.run->leftToRight() && ((
                        context.run->fUtf8Range.size() / glyphCnt) == EMOJI_WIDTH)) {
                        utf16Index = fOwner->getUTF16Index(clusterIndex8) + 2;
                    } else if (!context.run->leftToRight()) {
                        utf16Index = fOwner->getUTF16Index(clusterIndex8) + 1;
                    } else {
                        utf16Index = fOwner->getUTF16Index(clusterEnd8);
                    }
#else
                    size_t utf16Index = context.run->leftToRight()
                                                ? fOwner->getUTF16Index(clusterEnd8)
                                                : fOwner->getUTF16Index(clusterIndex8) + 1;
#endif
                    result = { SkToS32(utf16Index), kUpstream };
                }

                return keepLooking = false;

            });
            return keepLooking;
        }
    );

#ifdef OHOS_SUPPORT
    extendCoordinateRange(result);
#endif

    return result;
}

void TextLine::getRectsForPlaceholders(std::vector<TextBox>& boxes) {
#ifdef OHOS_SUPPORT
    this->iterateThroughVisualRuns(EllipsisReadStrategy::READ_REPLACED_WORD, true,
#else
    this->iterateThroughVisualRuns( true,
#endif
        [&boxes, this](const Run* run, SkScalar runOffset, TextRange textRange,
                        SkScalar* width) {
                auto context = this->measureTextInsideOneRun(
                        textRange, run, runOffset, 0, true, TextAdjustment::GraphemeGluster);
                *width = context.clip.width();

            if (textRange.width() == 0) {
                return true;
            }
            if (!run->isPlaceholder()) {
                return true;
            }

            SkRect clip = context.clip;
            clip.offset(this->offset());

            if (fOwner->getApplyRoundingHack()) {
                clip.fLeft = littleRound(clip.fLeft);
                clip.fRight = littleRound(clip.fRight);
                clip.fTop = littleRound(clip.fTop);
                clip.fBottom = littleRound(clip.fBottom);
            }
            boxes.emplace_back(clip, run->getTextDirection());
            return true;
        });
}

size_t TextLine::getGlyphCount() const
{
    size_t glyphCount = 0;
    for (auto& blob: fTextBlobCache) {
        glyphCount += blob.fVisitor_Size;
    }
    return glyphCount;
}

#ifdef OHOS_SUPPORT
std::vector<std::unique_ptr<RunBase>> TextLine::getGlyphRuns() const
{
    std::vector<std::unique_ptr<RunBase>> runBases;
    size_t num = 0;
    // Gets the offset position of the current line across the paragraph
    size_t pos = fClusterRange.start;
    size_t trailSpaces = 0;
    for (auto& blob: fTextBlobCache) {
        ++num;
        if (blob.fVisitor_Size == 0) {
            continue;
        }
        if (num == fTextBlobCache.size()) {
            // Counts how many tabs have been removed from the end of the current line
            trailSpaces = fGhostClusterRange.width() - fClusterRange.width();
        }
        std::unique_ptr<RunBaseImpl> runBaseImplPtr = std::make_unique<RunBaseImpl>(
            blob.fBlob, blob.fOffset, blob.fPaint, blob.fClippingNeeded, blob.fClipRect,
            blob.fVisitor_Run, blob.fVisitor_Pos, pos, trailSpaces, blob.fVisitor_Size);

        // Calculate the position of each blob, relative to the entire paragraph
        pos += blob.fVisitor_Size;
        runBases.emplace_back(std::move(runBaseImplPtr));
    }
    return runBases;
}
#else
std::vector<std::unique_ptr<RunBase>> TextLine::getGlyphRuns() const
{
    std::vector<std::unique_ptr<RunBase>> runBases;
    for (auto& blob: fTextBlobCache) {
        std::unique_ptr<RunBaseImpl> runBaseImplPtr = std::make_unique<RunBaseImpl>(
            blob.fBlob, blob.fOffset, blob.fPaint, blob.fClippingNeeded, blob.fClipRect,
            blob.fVisitor_Run, blob.fVisitor_Pos, blob.fVisitor_Size);
        runBases.emplace_back(std::move(runBaseImplPtr));
    }
    return runBases;
}
#endif

#ifdef OHOS_SUPPORT
int getEndWhitespaceCount(const ClusterRange& range, ParagraphImpl* owner)
{
    if (owner == nullptr) {
        return 0;
    }

    int endWhitespaceCount = 0;
    for (auto clusterIndex = range.end - 1; clusterIndex >= range.start; clusterIndex--) {
        if (!owner->cluster(clusterIndex).isWhitespaceBreak()) {
            break;
        }

        endWhitespaceCount++;
        if (clusterIndex == range.start) {
            break;
        }
    }

    return endWhitespaceCount;
}

std::unique_ptr<TextLineBase> TextLine::createTruncatedLine(double width, EllipsisModal ellipsisMode,
    const std::string& ellipsisStr)
{
    if (width > 0 && (ellipsisMode == EllipsisModal::HEAD || ellipsisMode == EllipsisModal::TAIL)) {
        TextLine textLine = CloneSelf();
        SkScalar widthVal = static_cast<SkScalar>(width);
        if (widthVal < widthWithEllipsisSpaces() && !ellipsisStr.empty()) {
            if (ellipsisMode == EllipsisModal::HEAD) {
                textLine.fIsTextLineEllipsisHeadModal = true;
                textLine.setTextBlobCachePopulated(false);
                textLine.createHeadEllipsis(widthVal, SkString(ellipsisStr), true);
            } else if (ellipsisMode == EllipsisModal::TAIL) {
                textLine.fIsTextLineEllipsisHeadModal = false;
                textLine.setTextBlobCachePopulated(false);
                int endWhitespaceCount = getEndWhitespaceCount(fGhostClusterRange, fOwner);
                textLine.fGhostClusterRange.end -= static_cast<size_t>(endWhitespaceCount);
                textLine.createTailEllipsis(widthVal, SkString(ellipsisStr), true, fOwner->getWordBreakType());
            }
        }
        return std::make_unique<TextLineBaseImpl>(std::make_unique<TextLine>(std::move(textLine)));
    }

    return nullptr;
}

double TextLine::getTypographicBounds(double* ascent, double* descent, double* leading) const
{
    if (ascent == nullptr || descent == nullptr || leading == nullptr) {
        return 0.0;
    }

    *ascent = std::abs(fMaxRunMetrics.ascent());
    *descent = std::abs(fMaxRunMetrics.descent());
    *leading = fMaxRunMetrics.leading();
    return widthWithEllipsisSpaces();
}

size_t getPrevGlyphsIndex(const ClusterRange& range, ParagraphImpl* owner, RunIndex& prevRunIndex)
{
    if (owner == nullptr) {
        return 0;
    }

    size_t glyphsIndex = 0;
    auto clusterIndex = range.start - 1;
    prevRunIndex = owner->cluster(clusterIndex).runIndex();
    if (prevRunIndex != owner->cluster(range.start).runIndex()) {
        // Belongs to a different run.
        return 0;
    }

    for (; clusterIndex >= 0; clusterIndex--) {
        RunIndex runIndex = owner->cluster(clusterIndex).runIndex();
        if (prevRunIndex != runIndex) {
            // Found a different run.
            break;
        }

        glyphsIndex++;

        if (clusterIndex == 0) {
            // All belong to the first run.
            break;
        }
    }

    return glyphsIndex;
}

#ifndef USE_SKIA_TXT
std::vector<SkRect> getAllRectInfo(const ClusterRange& range, ParagraphImpl* owner)
{
    std::vector<SkRect> rectVec;
#else
std::vector<RSRect> getAllRectInfo(const ClusterRange& range, ParagraphImpl* owner)
{
    std::vector<RSRect> rectVec;
#endif
    if (owner == nullptr) {
        return rectVec;
    }

    // If it is not the first line, you need to get the GlyphsIndex of the first character.
    size_t glyphsIndex  = 0;
    RunIndex prevRunIndex = 0;
    if (range.start > 0) {
        glyphsIndex = getPrevGlyphsIndex(range, owner, prevRunIndex);
    }

    for (auto clusterIndex = range.start; clusterIndex < range.end; clusterIndex++) {
        RunIndex runIndex = owner->cluster(clusterIndex).runIndex();
        if (prevRunIndex != runIndex) {
            glyphsIndex = 0;
        }

        auto run = owner->cluster(clusterIndex).runOrNull();
        if (run == nullptr) {
            break;
        }

        SkGlyphID glyphId = run->glyphs()[glyphsIndex];
#ifndef USE_SKIA_TXT
        SkRect glyphBounds;
        run->font().getBounds(&glyphId, 1, &glyphBounds, nullptr);
#else
        RSRect glyphBounds;
        run->font().GetWidths(&glyphId, 1, nullptr, &glyphBounds);
#endif
        rectVec.push_back(glyphBounds);
        glyphsIndex++;
        prevRunIndex = runIndex;
    }

    return rectVec;
}

RSRect TextLine::getImageBounds() const
{
    // Look for the first non-space character from the end and get its advance and index
    // to calculate the final image bounds.
    SkRect rect = {0.0, 0.0, 0.0, 0.0};
    int endWhitespaceCount = getEndWhitespaceCount(fGhostClusterRange, fOwner);
    size_t endWhitespaceCountVal = static_cast<size_t>(endWhitespaceCount);
    if (endWhitespaceCountVal == (fGhostClusterRange.end - fGhostClusterRange.start)) {
        // Full of Spaces.
        return {};
    }
    SkScalar endAdvance = fOwner->cluster(fGhostClusterRange.end - endWhitespaceCountVal - 1).width();

    // The first space width of the line needs to be added to the x value.
    SkScalar startWhitespaceAdvance = 0.0;
    int startWhitespaceCount = 0;
    for (auto clusterIndex = fGhostClusterRange.start; clusterIndex < fGhostClusterRange.end; clusterIndex++) {
        if (fOwner->cluster(clusterIndex).isWhitespaceBreak()) {
            startWhitespaceAdvance += fOwner->cluster(clusterIndex).width();
            startWhitespaceCount++;
        } else {
            break;
        }
    }

    // Gets rect information for all characters in line.
    auto rectVec = getAllRectInfo(fGhostClusterRange, fOwner);
    // Calculate the final y and height.
    auto joinRect = rectVec[startWhitespaceCount];
    for (size_t i = startWhitespaceCount + 1; i < rectVec.size() - endWhitespaceCountVal; ++i) {
        joinRect.Join(rectVec[i]);
    }

    SkScalar lineWidth = width();
    auto endRect = rectVec[rectVec.size() - endWhitespaceCountVal - 1];
#ifndef USE_SKIA_TXT
    SkScalar x = rectVec[startWhitespaceCount].x() + startWhitespaceAdvance;
    SkScalar y = joinRect.bottom();
    SkScalar width = lineWidth - (endAdvance - endRect.x() - endRect.width()) - x;
    SkScalar height = joinRect.height();
#else
    SkScalar x = rectVec[startWhitespaceCount].GetLeft() + startWhitespaceAdvance;
    SkScalar y = joinRect.GetBottom();
    SkScalar width = lineWidth - (endAdvance - endRect.GetLeft() - endRect.GetWidth()) - x;
    SkScalar height = joinRect.GetHeight();
#endif

    rect.setXYWH(x, y, width, height);
    return {rect.fLeft, rect.fTop, rect.fRight, rect.fBottom};
}

double TextLine::getTrailingSpaceWidth() const
{
    return spacesWidth();
}

int32_t TextLine::getStringIndexForPosition(SkPoint point) const
{
    int32_t index = fGhostClusterRange.start;
    double offset = point.x();
    if (offset >= widthWithEllipsisSpaces()) {
        index = fGhostClusterRange.end;
    } else if (offset > 0) {
        double curOffset = 0.0;
        for (auto clusterIndex = fGhostClusterRange.start; clusterIndex < fGhostClusterRange.end; ++clusterIndex) {
            double characterWidth = fOwner->cluster(clusterIndex).width();
            if (offset <= curOffset + characterWidth / 2) {
                return index;
            }
            index++;
            curOffset += characterWidth;
        }
    }

    return index;
}

double TextLine::getOffsetForStringIndex(int32_t index) const
{
    double offset = 0.0;
    if (index <= 0) {
        return offset;
    }

    size_t indexVal = static_cast<size_t>(index);
    if (indexVal >= fGhostClusterRange.end) {
        offset = widthWithEllipsisSpaces();
    } else if (indexVal > fGhostClusterRange.start) {
        size_t clusterIndex = fGhostClusterRange.start;
        while (clusterIndex < fGhostClusterRange.end) {
            offset += fOwner->cluster(clusterIndex).width();
            if (++clusterIndex == indexVal) {
                break;
            }
        }
    }

    return offset;
}

std::map<int32_t, double> TextLine::getIndexAndOffsets(bool& isHardBreak) const
{
    std::map<int32_t, double> offsetMap;
    double offset = 0.0;
    for (auto clusterIndex = fGhostClusterRange.start; clusterIndex < fGhostClusterRange.end; ++clusterIndex) {
        auto& cluster = fOwner->cluster(clusterIndex);
        offset += cluster.width();
        isHardBreak = cluster.isHardBreak();
        if (!isHardBreak) {
            offsetMap[clusterIndex] = offset;
        }
    }
    return offsetMap;
}

double TextLine::getAlignmentOffset(double alignmentFactor, double alignmentWidth) const
{
    double lineWidth = width();
    if (alignmentWidth <= lineWidth) {
        return 0.0;
    }

    double offset = 0.0;
    TextDirection textDirection = fOwner->paragraphStyle().getTextDirection();
    if (alignmentFactor <= 0) {
        // Flush left.
        if (textDirection == TextDirection::kRtl) {
            offset =  lineWidth - alignmentWidth;
        }
    } else if (alignmentFactor < 1) {
        // Align according to the alignmentFactor.
        if (textDirection == TextDirection::kLtr) {
            offset = (alignmentWidth - lineWidth) * alignmentFactor;
        } else {
            offset = (lineWidth - alignmentWidth) * (1 - alignmentFactor);
        }
    } else {
        // Flush right.
        if (textDirection == TextDirection::kLtr) {
            offset = alignmentWidth - lineWidth;
        }
    }

    return offset;
}

SkRect TextLine::computeShadowRect(SkScalar x, SkScalar y, const TextStyle& style, const ClipContext& context) const
{
    SkScalar offsetX = x + this->fOffset.fX;
    SkScalar offsetY = y + this->fOffset.fY -
        (context.run ? context.run->fCompressionBaselineShift : 0);
    SkRect shadowRect = SkRect::MakeEmpty();

    for (const TextShadow& shadow : style.getShadows()) {
        if (!shadow.hasShadow()) {
            continue;
        }

        SkScalar blurSigma = SkDoubleToScalar(shadow.fBlurSigma);
        SkRect rect = context.clip
            .makeOffset(offsetX + shadow.fOffset.fX, offsetY + shadow.fOffset.fY)
            .makeOutset(blurSigma, blurSigma);
        shadowRect.join(rect);
    }
    return shadowRect;
}

SkRect TextLine::getAllShadowsRect(SkScalar x, SkScalar y) const
{
    if (!fHasShadows) {
        return SkRect::MakeEmpty();
    }
    SkRect paintRegion = SkRect::MakeEmpty();
    this->iterateThroughVisualRuns(EllipsisReadStrategy::READ_REPLACED_WORD, false,
        [&paintRegion, x, y, this]
        (const Run* run, SkScalar runOffsetInLine, TextRange textRange, SkScalar* runWidthInLine) {
            if (runWidthInLine == nullptr) {
                return true;
            }
            *runWidthInLine = this->iterateThroughSingleRunByStyles(
                TextAdjustment::GlyphCluster, run, runOffsetInLine, textRange, StyleType::kShadow,
                [&paintRegion, x, y, this]
                (TextRange textRange, const TextStyle& style, const ClipContext& context) {
                    SkRect rect = computeShadowRect(x, y, style, context);
                    paintRegion.join(rect);
                });
            return true;
        });
    return paintRegion;
}

SkRect TextLine::generatePaintRegion(SkScalar x, SkScalar y)
{
    SkRect paintRegion = SkRect::MakeXYWH(x, y, 0, 0);
    fIsArcText = false;

    SkRect rect = getAllShadowsRect(x, y);
    paintRegion.join(rect);

    // textblob
    this->ensureTextBlobCachePopulated();
    for (auto& record : fTextBlobCache) {
        rect = GetTextBlobSkTightBound(record.fBlob, x + record.fOffset.fX, y + record.fOffset.fY, record.fClipRect);
        paintRegion.join(rect);
    }

    return paintRegion;
}
#endif

TextLine TextLine::CloneSelf()
{
    TextLine textLine;
    textLine.fBlockRange = this->fBlockRange;
    textLine.fTextExcludingSpaces = this->fTextExcludingSpaces;
    textLine.fText = this->fText;
    textLine.fTextIncludingNewlines = this->fTextIncludingNewlines;
    textLine.fClusterRange = this->fClusterRange;

    textLine.fGhostClusterRange = this->fGhostClusterRange;
    textLine.fRunsInVisualOrder = this->fRunsInVisualOrder;
    textLine.fAdvance = this->fAdvance;
    textLine.fOffset = this->fOffset;
    textLine.fShift = this->fShift;

    textLine.fWidthWithSpaces = this->fWidthWithSpaces;
    if (this->fEllipsis) {
        textLine.fEllipsis = std::make_unique<Run>(*this->fEllipsis);
    }

    textLine.fSizes = this->fSizes;
    textLine.fMaxRunMetrics = this->fMaxRunMetrics;
    textLine.fHasBackground = this->fHasBackground;
    textLine.fHasShadows = this->fHasShadows;
    textLine.fHasDecorations = this->fHasDecorations;
    textLine.fAscentStyle = this->fAscentStyle;
    textLine.fDescentStyle = this->fDescentStyle;
    textLine.fTextBlobCachePopulated = this->fTextBlobCachePopulated;
#ifdef OHOS_SUPPORT
    textLine.fOwner = this->fOwner;
    textLine.fIsTextLineEllipsisHeadModal = this->fIsTextLineEllipsisHeadModal;
    textLine.fEllipsisString = this->fEllipsisString;
    textLine.fBreakWithHyphen = this->fBreakWithHyphen;
#endif

    textLine.roundRectAttrs = this->roundRectAttrs;
    textLine.fTextBlobCache = this->fTextBlobCache;
    textLine.fTextRangeReplacedByEllipsis = this->fTextRangeReplacedByEllipsis;
    textLine.fEllipsisIndex = this->fEllipsisIndex;
    textLine.fLastClipRunLtr = this->fLastClipRunLtr;
    return textLine;
}
}  // namespace textlayout
}  // namespace skia
