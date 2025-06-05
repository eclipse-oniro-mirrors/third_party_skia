// Copyright 2019 Google LLC.
#include "include/core/SkFontMetrics.h"
#include "include/core/SkTextBlob.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkTo.h"
#include "modules/skparagraph/include/DartTypes.h"
#include "modules/skparagraph/include/TextStyle.h"
#include "modules/skparagraph/src/ParagraphImpl.h"
#include "modules/skparagraph/src/Run.h"
#include "modules/skshaper/include/SkShaper.h"
#include "src/utils/SkUTF.h"

#ifdef OHOS_SUPPORT
#include "include/FontCollection.h"
#include "log.h"
#endif

namespace skia {
namespace textlayout {
constexpr SkScalar PARAM_TWO = 2.0;
#ifdef OHOS_SUPPORT
// 1px font size "HarmonyOS Sans" metrics
constexpr SkScalar DEFAULT_TOP = -1.056;
constexpr SkScalar DEFAULT_BOTTOM = 0.271;
constexpr SkScalar DEFAULT_ASCENT = -0.928;
constexpr SkScalar DEFAULT_DESCENT = 0.244;
struct ScaleParam {
    SkScalar fontScale;
    SkScalar baselineShiftScale;
};
// unordered_map<familyName, ScaleParam>: compress <familyName> font height, shift font baseline.
// target font size = font size * ScaleParam.scale.
// target baseline = baseline - height * font size * ScaleParam.baselineShiftScale.
const std::unordered_map<std::string, ScaleParam> FONT_FAMILY_COMPRESSION_CONFIG = {
    {"Noto Serif Tibetan", ScaleParam{ .fontScale = 0.79, .baselineShiftScale = 0.1 }},
    {"Noto Sans Tibetan", ScaleParam{ .fontScale = 0.79, .baselineShiftScale = 0.1 }},
};
const std::unordered_map<std::string, ScaleParam> FONT_FAMILY_COMPRESSION_WITH_HEIGHT_ADAPTER_CONFIG = {
    {"Noto Serif Tibetan", ScaleParam{ .fontScale = 0.85, .baselineShiftScale = 0.11 }},
    {"Noto Sans Tibetan", ScaleParam{ .fontScale = 0.85, .baselineShiftScale = 0.11 }},
};
const ScaleParam DEFAULT_SCALE_PARAM = ScaleParam{ .fontScale = 0, .baselineShiftScale = 0 };
enum FontCompressionStatus {
    UNDEFINED,
    COMPRESSED,
    UNCOMPRESSED,
};
// the font padding does not take effect for these font families.
const std::unordered_set<std::string> FONT_PADDING_NOT_EFFECT_FAMILY = {
    "Harmony Clock_01",
    "Harmony Clock_02",
    "Harmony Clock_03",
    "Harmony Clock_04",
    "Harmony Clock_05",
    "Harmony Clock_06",
    "Harmony Clock_07",
    "Harmony Clock_08",
// symbol: need to ensure "the symbol height = the font size".
// so the height compression is not enabled for symbol.
    "HM Symbol",
};

#ifdef USE_SKIA_TXT
FontCompressionStatus getFontCompressionStatus(const RSFont& font)
{
    auto typeface = font.GetTypeface();
    if (typeface == nullptr) {
        return FontCompressionStatus::UNDEFINED;
    }
    return (typeface->IsCustomTypeface() && !typeface->IsThemeTypeface())
                   ? FontCompressionStatus::UNCOMPRESSED
                   : FontCompressionStatus::COMPRESSED;
}
std::string getFamilyNameFromFont(const RSFont& font)
{
    auto typeface = font.GetTypeface();
    return typeface == nullptr ? "" : typeface->GetFamilyName();
}
#else
FontCompressionStatus getFontCompressionStatus(const SkFont& font)
{
    auto typeface = font.refTypeface();
    if (typeface == nullptr) {
        return FontCompressionStatus::UNDEFINED;
    }
    return (typeface->IsCustomTypeface() && !typeface->IsThemeTypeface())
                   ? FontCompressionStatus::UNCOMPRESSED
                   : FontCompressionStatus::COMPRESSED;
}
std::string getFamilyNameFromFont(const SkFont& font)
{
    auto typeface = font.refTypeface();
    if (typeface == nullptr) {
        return "";
    }
    SkString familyName;
    typeface->getFamilyName(&familyName);
    return std::string(familyName.c_str(), familyName.size());
}
#endif

#ifdef USE_SKIA_TXT
const ScaleParam& findCompressionConfigWithFont(const RSFont& font)
#else
const ScaleParam& findCompressionConfigWithFont(const SkFont& font)
#endif
{
    auto fontCompressionStatus = getFontCompressionStatus(font);
    if (fontCompressionStatus != FontCompressionStatus::COMPRESSED) {
        return DEFAULT_SCALE_PARAM;
    }

    const auto& config = FontCollection::IsAdapterTextHeightEnabled() ?
        FONT_FAMILY_COMPRESSION_WITH_HEIGHT_ADAPTER_CONFIG : FONT_FAMILY_COMPRESSION_CONFIG;
    std::string familyName = getFamilyNameFromFont(font);
    auto iter = config.find(familyName);
    if (iter == config.end()) {
        return DEFAULT_SCALE_PARAM;
    }
    return iter->second;
}

#ifdef USE_SKIA_TXT
void metricsIncludeFontPadding(RSFontMetrics* metrics, const RSFont& font)
#else
void metricsIncludeFontPadding(SkFontMetrics* metrics, const SkFont& font)
#endif
{
    if (metrics == nullptr) {
        return;
    }
    auto fontCompressionStatus = getFontCompressionStatus(font);
    auto typeface = font.GetTypeface();
    if (typeface == nullptr || fontCompressionStatus == FontCompressionStatus::UNDEFINED) {
        return;
    }
#ifdef USE_SKIA_TXT
    SkScalar fontSize = font.GetSize();
#else
    SkScalar fontSize = font.getSize();
#endif
    if (!FontCollection::IsAdapterTextHeightEnabled()) {
        if (fontCompressionStatus == FontCompressionStatus::COMPRESSED &&
            (!SkScalarNearlyZero(findCompressionConfigWithFont(font).fontScale) ||
            typeface->IsThemeTypeface())) {
            metrics->fAscent = DEFAULT_ASCENT * fontSize;
            metrics->fDescent = DEFAULT_DESCENT * fontSize;
        }
        return;
    }

    std::string curFamilyName = getFamilyNameFromFont(font);
    auto setIter = FONT_PADDING_NOT_EFFECT_FAMILY.find(curFamilyName);
    if (setIter == FONT_PADDING_NOT_EFFECT_FAMILY.end()) {
        if (fontCompressionStatus == FontCompressionStatus::COMPRESSED) {
            metrics->fAscent = DEFAULT_TOP * fontSize;
            metrics->fDescent = DEFAULT_BOTTOM * fontSize;
            return;
        }
        // use top and bottom as ascent and descent.
        // calculate height with top and bottom.(includeFontPadding)
        metrics->fAscent = metrics->fTop;
        metrics->fDescent = metrics->fBottom;
    }
}

#ifdef USE_SKIA_TXT
void scaleFontWithCompressionConfig(RSFont& font, ScaleOP op)
{
    SkScalar fontSize = font.GetSize();
#else
void scaleFontWithCompressionConfig(SkFont& font, ScaleOP op)
{
    SkScalar fontSize = font.getSize();
#endif
    auto config = findCompressionConfigWithFont(font);
    if (SkScalarNearlyZero(config.fontScale)) {
        return;
    }
    switch (op) {
    case ScaleOP::COMPRESS:
        fontSize *= config.fontScale;
        break;
    case ScaleOP::DECOMPRESS:
        fontSize /= config.fontScale;
        break;
    default:
        return;
    }
#ifdef USE_SKIA_TXT
    font.SetSize(fontSize);
#else
    font.setSize(fontSize);
#endif
}
#endif

Run::Run(ParagraphImpl* owner,
         const SkShaper::RunHandler::RunInfo& info,
         size_t firstChar,
         SkScalar heightMultiplier,
         bool useHalfLeading,
         SkScalar baselineShift,
         size_t index,
         SkScalar offsetX)
    : fOwner(owner)
    , fTextRange(firstChar + info.utf8Range.begin(), firstChar + info.utf8Range.end())
    , fClusterRange(EMPTY_CLUSTERS)
    , fFont(info.fFont)
    , fClusterStart(firstChar)
    , fGlyphData(std::make_shared<GlyphData>())
    , fGlyphs(fGlyphData->glyphs)
    , fPositions(fGlyphData->positions)
    , fOffsets(fGlyphData->offsets)
    , fClusterIndexes(fGlyphData->clusterIndexes)
    , fHeightMultiplier(heightMultiplier)
    , fUseHalfLeading(useHalfLeading)
    , fBaselineShift(baselineShift)
{
    fBidiLevel = info.fBidiLevel;
    fAdvance = info.fAdvance;
    fIndex = index;
    fUtf8Range = info.utf8Range;
    fOffset = SkVector::Make(offsetX, 0);

    fGlyphs.push_back_n(info.glyphCount);
    fPositions.push_back_n(info.glyphCount + 1);
    fOffsets.push_back_n(info.glyphCount + 1);
    fClusterIndexes.push_back_n(info.glyphCount + 1);
    fHalfLetterspacings.push_back_n(info.glyphCount + 1);
    std::fill(fHalfLetterspacings.begin(), fHalfLetterspacings.end(), 0.0);
#ifndef USE_SKIA_TXT
    info.fFont.getMetrics(&fFontMetrics);
#else
    info.fFont.GetMetrics(&fFontMetrics);
#endif

#ifdef OHOS_SUPPORT
    auto decompressFont = info.fFont;
    scaleFontWithCompressionConfig(decompressFont, ScaleOP::DECOMPRESS);
    metricsIncludeFontPadding(&fFontMetrics, decompressFont);
    auto config = findCompressionConfigWithFont(decompressFont);
    fCompressionBaselineShift = (fFontMetrics.fDescent - fFontMetrics.fAscent) * config.baselineShiftScale;
#endif

    this->calculateMetrics();

    // To make edge cases easier:
    fPositions[info.glyphCount] = fOffset + fAdvance;
    fOffsets[info.glyphCount] = {0, 0};
    fClusterIndexes[info.glyphCount] = this->leftToRight() ? info.utf8Range.end() : info.utf8Range.begin();
    fEllipsis = false;
    fPlaceholderIndex = std::numeric_limits<size_t>::max();
}

void Run::calculateMetrics() {
#ifdef OHOS_SUPPORT
    SkScalar fontMetircsScale = 1.0;
    if (fOwner != nullptr) {
        BlockRange blockRange = fOwner->findAllBlocks(fTextRange);
        const SkSpan<Block>& blocks = fOwner->blocks(blockRange);
        for (const Block& block: blocks) {
            if (block.fStyle.getTextBadgeType() == TextBadgeType::BADGE_NONE) {
                continue;
            }
            fontMetircsScale = TEXT_BADGE_HEIGHT_SCALE;
            break;
        }
    }
    fCorrectAscent = fFontMetrics.fAscent * fontMetircsScale - fFontMetrics.fLeading * 0.5;
    fCorrectDescent = fFontMetrics.fDescent * fontMetircsScale + fFontMetrics.fLeading * 0.5;
#else
    fCorrectAscent = fFontMetrics.fAscent - fFontMetrics.fLeading * 0.5;
    fCorrectDescent = fFontMetrics.fDescent + fFontMetrics.fLeading * 0.5;
#endif

    fCorrectLeading = 0;
    if (SkScalarNearlyZero(fHeightMultiplier)) {
        return;
    }
#ifndef USE_SKIA_TXT
    const auto runHeight = fHeightMultiplier * fFont.getSize();
#else
    const auto runHeight = fHeightMultiplier * fFont.GetSize();
#endif
    const auto fontIntrinsicHeight = fCorrectDescent - fCorrectAscent;
    if (fUseHalfLeading) {
        const auto extraLeading = (runHeight - fontIntrinsicHeight) / 2;
        fCorrectAscent -= extraLeading;
        fCorrectDescent += extraLeading;
    } else {
        const auto multiplier = runHeight / fontIntrinsicHeight;
        fCorrectAscent *= multiplier;
        fCorrectDescent *= multiplier;
    }
    // If we shift the baseline we need to make sure the shifted text fits the line
    fCorrectAscent += fBaselineShift;
    fCorrectDescent += fBaselineShift;
}

SkShaper::RunHandler::Buffer Run::newRunBuffer() {
    return {fGlyphs.data(), fPositions.data(), fOffsets.data(), fClusterIndexes.data(), fOffset};
}

SkScalar Run::usingAutoSpaceWidth(const Cluster& cluster) const
{
    return fOwner->clusterUsingAutoSpaceWidth(cluster);
}
#ifndef USE_SKIA_TXT
void Run::copyTo(SkTextBlobBuilder& builder, size_t pos, size_t size) const {
    SkASSERT(pos + size <= this->size());
    const auto& blobBuffer = builder.allocRunPos(fFont, SkToInt(size));
    sk_careful_memcpy(blobBuffer.glyphs, fGlyphs.data() + pos, size * sizeof(SkGlyphID));

    for (size_t i = 0; i < size; ++i) {
        auto point = fPositions[i + pos];
        if (!fJustificationShifts.empty()) {
            point.fX += fJustificationShifts[i + pos].fX;
        }
        if (!fAutoSpacings.empty()) {
            point.fX += fAutoSpacings[i + pos].fX;
        }
        point += fOffsets[i + pos];
        blobBuffer.points()[i] = point;
    }
}
#else
void Run::copyTo(RSTextBlobBuilder& builder, size_t pos, size_t size) const {
    SkASSERT(pos + size <= this->size());
    const auto& blobBuffer = builder.AllocRunPos(fFont, SkToInt(size));
    #ifdef OHOS_SUPPORT
    if (!blobBuffer.glyphs || !fGlyphs.data()) {
        return;
    }
    #endif
    sk_careful_memcpy(blobBuffer.glyphs, fGlyphs.data() + pos, size * sizeof(SkGlyphID));
    auto points = reinterpret_cast<SkPoint*>(blobBuffer.pos);

    for (size_t i = 0; i < size; ++i) {
        auto point = fPositions[i + pos];
        if (!fJustificationShifts.empty()) {
            point.fX += fJustificationShifts[i + pos].fX;
        }
        if (!fAutoSpacings.empty()) {
            point.fX += fAutoSpacings[i + pos].fX;
        }
        point += fOffsets[i + pos];
        points[i] = point;
    }
}

void Run::copyTo(RSTextBlobBuilder& builder,
                 const RSPath* path,
                 float hOffset,
                 float vOffset,
                 float fTextShift,
                 size_t pos,
                 size_t size) const {
    SkASSERT(pos + size <= this->size());
    auto& blobBuffer = builder.AllocRunRSXform(fFont, SkToInt(size));
    #ifdef OHOS_SUPPORT
    if (!blobBuffer.glyphs || !fGlyphs.data()) {
        return;
    }
    #endif
    sk_careful_memcpy(blobBuffer.glyphs, fGlyphs.data() + pos, size * sizeof(SkGlyphID));
    std::vector<float> widths(size);
    fFont.GetWidths(blobBuffer.glyphs, size, widths.data());
    RSXform* xform = reinterpret_cast<RSXform*>(blobBuffer.pos);
    for (size_t i = 0; i < size; ++i) {
        float halfWidth = widths[i + pos] * 0.5f;
        float x = hOffset + posX(i + pos) + halfWidth + fOffsets[i + pos].x() + fTextShift;
        if (!fJustificationShifts.empty()) {
            x += fJustificationShifts[i + pos].fX;
        }
        if (!fAutoSpacings.empty()) {
            x += fAutoSpacings[i + pos].fX;
        }
        RSPoint rsPos;
        RSPoint rsTan;
        if (!path->GetPositionAndTangent(x, rsPos, rsTan, false)) {
            rsPos.Set(x, vOffset);
            rsTan.Set(1, 0);
        }
        xform[i].cos_ = rsTan.GetX();
        xform[i].sin_ = rsTan.GetY();
        xform[i].tx_ = rsPos.GetX() - rsTan.GetY() * vOffset - halfWidth * rsTan.GetX();
        xform[i].ty_ = rsPos.GetY() + rsTan.GetX() * vOffset - halfWidth * rsTan.GetY();
    }
}
#endif

// Find a cluster range from text range (within one run)
// Cluster range is normalized ([start:end) start < end regardless of TextDirection
// Boolean value in triple indicates whether the cluster range was found or not
std::tuple<bool, ClusterIndex, ClusterIndex> Run::findLimitingClusters(TextRange text) const {
    if (text.width() == 0) {
        // Special Flutter case for "\n" and "...\n"
        if (text.end > this->fTextRange.start) {
            ClusterIndex index = fOwner->clusterIndex(text.end - 1);
            return std::make_tuple(true, index, index);
        } else {
            return std::make_tuple(false, 0, 0);
        }
    }

    ClusterRange clusterRange;
    bool found = true;
    // Deal with the case when either start or end are not align with glyph cluster edge
    // In such case we shift the text range to the right
    // (cutting from the left and adding to the right)
    if (leftToRight()) {
        // LTR: [start:end)
        found = clusterRange.start != fClusterRange.end;
        clusterRange.start = fOwner->clusterIndex(text.start);
        clusterRange.end = fOwner->clusterIndex(text.end - 1);
    } else {
        // RTL: (start:end]
        clusterRange.start = fOwner->clusterIndex(text.end);
        clusterRange.end = fOwner->clusterIndex(text.start + 1);
        found = clusterRange.end != fClusterRange.start;
    }

    return std::make_tuple(
            found,
            clusterRange.start,
            clusterRange.end);
}

std::tuple<bool, TextIndex, TextIndex> Run::findLimitingGlyphClusters(TextRange text) const {
    TextIndex start = fOwner->findPreviousGlyphClusterBoundary(text.start);
    TextIndex end = fOwner->findNextGlyphClusterBoundary(text.end);
    return std::make_tuple(true, start, end);
}

// Adjust the text to grapheme edges so the first grapheme start is in the text and the last grapheme start is in the text
// It actually means that the first grapheme is entirely in the text and the last grapheme does not have to be
// 12345 234 2:2 -> 2,5 4:4
std::tuple<bool, TextIndex, TextIndex> Run::findLimitingGraphemes(TextRange text) const {
    TextIndex start = fOwner->findPreviousGraphemeBoundary(text.start);
    TextIndex end = fOwner->findNextGraphemeBoundary(text.end);
    return std::make_tuple(true, start, end);
}

void Run::iterateThroughClusters(const ClusterVisitor& visitor) {

    for (size_t index = 0; index < fClusterRange.width(); ++index) {
        auto correctIndex = leftToRight() ? fClusterRange.start + index : fClusterRange.end - index - 1;
        auto cluster = &fOwner->cluster(correctIndex);
        visitor(cluster);
    }
}

void Run::addSpacesAtTheEnd(SkScalar space, Cluster* cluster) {
    // Increment the run width
    fAdvance.fX += space;
    // Increment the cluster width
    cluster->space(space);
}

SkScalar Run::addSpacesEvenly(SkScalar space) {
    SkScalar shift = 0;
    if (this->size()) {
        shift += space / PARAM_TWO;
    }
    for (size_t i = 0; i < this->size(); ++i) {
        fPositions[i].fX += shift;
        fHalfLetterspacings[i] = space / PARAM_TWO;
        shift += space;
    }
    if (this->size()) {
        shift -= space / PARAM_TWO;
    }
    fPositions[this->size()].fX += shift;
    fAdvance.fX += shift;
    return shift;
}

#ifdef OHOS_SUPPORT
SkScalar Run::addSpacesEvenly(SkScalar space, Cluster* cluster) {
    // Offset all the glyphs in the cluster
    SkScalar shift = 0;
    for (size_t i = cluster->startPos(); i < cluster->endPos(); ++i) {
        fPositions[i].fX += shift;
        fHalfLetterspacings[i] = space / PARAM_TWO;
        shift += space;
    }
    if (this->size() == cluster->endPos()) {
        // To make calculations easier
        fPositions[cluster->endPos()].fX += shift;
        fHalfLetterspacings[cluster->endPos()] = space / PARAM_TWO;
    }
    // Increment the run width
    fAdvance.fX += shift;
    // Increment the cluster width
    cluster->space(shift);
    cluster->setHalfLetterSpacing(space / PARAM_TWO);

    return shift;
}
#else
SkScalar Run::addSpacesEvenly(SkScalar space, Cluster* cluster) {
    // Offset all the glyphs in the cluster
    SkScalar shift = 0;
    for (size_t i = cluster->startPos(); i < cluster->endPos(); ++i) {
        fPositions[i].fX += shift;
        shift += space;
    }
    if (this->size() == cluster->endPos()) {
        // To make calculations easier
        fPositions[cluster->endPos()].fX += shift;
    }
    // Increment the run width
    fAdvance.fX += shift;
    // Increment the cluster width
    cluster->space(shift);
    cluster->setHalfLetterSpacing(space / 2);

    return shift;
}
#endif

void Run::shift(const Cluster* cluster, SkScalar offset) {
    if (offset == 0) {
        return;
    }

    for (size_t i = cluster->startPos(); i < cluster->endPos(); ++i) {
        fPositions[i].fX += offset;
    }
    if (this->size() == cluster->endPos()) {
        // To make calculations easier
        fPositions[cluster->endPos()].fX += offset;
    }
}

#ifdef OHOS_SUPPORT
void Run::extendClusterWidth(Cluster* cluster, SkScalar space) {
    addSpacesAtTheEnd(space, cluster);
    for (size_t pos = cluster->endPos(); pos < fPositions.size(); pos++) {
        fPositions[pos].fX += space;
    }
}

// Checks if the current line contains trailing spaces and current run is at the end of the line
bool Run::isTrailingSpaceIncluded(const ClusterRange& fTextLineClusterRange,
    const ClusterRange& fTextLineGhostClusterRange) const {
    return fTextLineGhostClusterRange.width() > 0 && this->clusterRange().width() > 0 &&
           fTextLineClusterRange.width() > 0 && fTextLineGhostClusterRange.end != fTextLineClusterRange.end &&
           fTextLineGhostClusterRange.end <= this->clusterRange().end &&
           fTextLineGhostClusterRange.end > this->clusterRange().start;
}
#endif

void Run::updateMetrics(InternalLineMetrics* endlineMetrics) {

    SkASSERT(isPlaceholder());
    auto placeholderStyle = this->placeholderStyle();
    // Difference between the placeholder baseline and the line bottom
    SkScalar baselineAdjustment = 0;
    switch (placeholderStyle->fBaseline) {
        case TextBaseline::kAlphabetic:
            break;

        case TextBaseline::kIdeographic:
            baselineAdjustment = endlineMetrics->deltaBaselines() / 2;
            break;
    }

    auto height = placeholderStyle->fHeight;
    auto offset = placeholderStyle->fBaselineOffset;

    fFontMetrics.fLeading = 0;
    switch (placeholderStyle->fAlignment) {
        case PlaceholderAlignment::kBaseline:
            fFontMetrics.fAscent = baselineAdjustment - height - offset;
            fFontMetrics.fDescent = baselineAdjustment - offset;
            break;

        case PlaceholderAlignment::kAboveBaseline:
            fFontMetrics.fAscent = baselineAdjustment - height;
            fFontMetrics.fDescent = baselineAdjustment;
            break;

        case PlaceholderAlignment::kBelowBaseline:
            fFontMetrics.fAscent = baselineAdjustment;
            fFontMetrics.fDescent = baselineAdjustment + height;
            break;

        case PlaceholderAlignment::kTop:
            fFontMetrics.fAscent = endlineMetrics->ascent();
            fFontMetrics.fDescent = height + fFontMetrics.fAscent;
            break;

        case PlaceholderAlignment::kFollow:
        case PlaceholderAlignment::kBottom:
            fFontMetrics.fDescent = endlineMetrics->descent();
            fFontMetrics.fAscent = fFontMetrics.fDescent - height;
            break;

        case PlaceholderAlignment::kMiddle:
            auto mid = (endlineMetrics->ascent() + endlineMetrics->descent()) / PARAM_TWO;
            fFontMetrics.fDescent = mid + height / PARAM_TWO;
            fFontMetrics.fAscent = mid - height / PARAM_TWO;
            break;
    }

    this->calculateMetrics();

    // Make sure the placeholder can fit the line
    endlineMetrics->add(this);
}

SkScalar Cluster::sizeToChar(TextIndex ch) const {
    if (ch < fTextRange.start || ch >= fTextRange.end) {
        return 0;
    }
    auto shift = ch - fTextRange.start;
    auto ratio = shift * 1.0 / fTextRange.width();

    return SkDoubleToScalar(fWidth * ratio);
}

SkScalar Cluster::sizeFromChar(TextIndex ch) const {
    if (ch < fTextRange.start || ch >= fTextRange.end) {
        return 0;
    }
    auto shift = fTextRange.end - ch - 1;
    auto ratio = shift * 1.0 / fTextRange.width();

    return SkDoubleToScalar(fWidth * ratio);
}

size_t Cluster::roundPos(SkScalar s) const {
    auto ratio = (s * 1.0) / fWidth;
    return sk_double_floor2int(ratio * size());
}

SkScalar Cluster::trimmedWidth(size_t pos) const {
    // Find the width until the pos and return the min between trimmedWidth and the width(pos)
    // We don't have to take in account cluster shift since it's the same for 0 and for pos
    auto& run = fOwner->run(fRunIndex);
    SkScalar delta = getHalfLetterSpacing() - run.halfLetterspacing(pos);
    return std::min(run.positionX(pos) - run.positionX(fStart) + delta, fWidth);
}

SkScalar Run::positionX(size_t pos) const {
    return posX(pos) + (fJustificationShifts.empty() ? 0 : fJustificationShifts[pos].fY) +
        (fAutoSpacings.empty() ? 0 : fAutoSpacings[pos].fY);
}

SkScalar Run::posX(size_t index) const {
    if (index < fPositions.size()) {
        return fPositions[index].fX;
    }
    LOGE("index:%{public}zu,size:%{public}zu", index, fPositions.size());
    if (fPositions.empty()) {
        return 0.0f;
    }
    return fPositions[fPositions.size() - 1].fX;
}

PlaceholderStyle* Run::placeholderStyle() const {
    if (isPlaceholder()) {
        return &fOwner->placeholders()[fPlaceholderIndex].fStyle;
    } else {
        return nullptr;
    }
}

bool Run::isResolved() const {
    for (auto& glyph :fGlyphs) {
        if (glyph == 0) {
            return false;
        }
    }
    return true;
}

Run* Cluster::runOrNull() const {
    if (fRunIndex >= fOwner->runs().size()) {
        return nullptr;
    }
    return &fOwner->run(fRunIndex);
}

Run& Cluster::run() const {
    SkASSERT(fRunIndex < fOwner->runs().size());
    return fOwner->run(fRunIndex);
}

#ifndef USE_SKIA_TXT
SkFont Cluster::font() const {
#else
RSFont Cluster::font() const {
#endif
    SkASSERT(fRunIndex < fOwner->runs().size());
    return fOwner->run(fRunIndex).font();
}

bool Cluster::isSoftBreak() const {
    return fOwner->codeUnitHasProperty(fTextRange.end,
                                       SkUnicode::CodeUnitFlags::kSoftLineBreakBefore);
}

bool Cluster::isGraphemeBreak() const {
    return fOwner->codeUnitHasProperty(fTextRange.end, SkUnicode::CodeUnitFlags::kGraphemeStart);
}

#ifdef OHOS_SUPPORT
bool Cluster::isStartCombineBreak() const {
    return fOwner->codeUnitHasProperty(fTextRange.start, SkUnicode::CodeUnitFlags::kCombine);
}

bool Cluster::isEndCombineBreak() const {
    return fOwner->codeUnitHasProperty(fTextRange.end, SkUnicode::CodeUnitFlags::kCombine);
}
#endif
}  // namespace textlayout
}  // namespace skia
