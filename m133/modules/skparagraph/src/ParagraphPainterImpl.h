// Copyright 2019 Google LLC.
#ifndef ParagraphPainterImpl_DEFINED
#define ParagraphPainterImpl_DEFINED

#include "include/core/SkCanvas.h"
#include "modules/skparagraph/include/ParagraphPainter.h"

namespace skia {
namespace textlayout {

class CanvasParagraphPainter : public ParagraphPainter {
public:
    CanvasParagraphPainter(SkCanvas* canvas);

#ifndef ENABLE_DRAWING_ADAPTER
    void drawTextBlob(const sk_sp<SkTextBlob>& blob, SkScalar x, SkScalar y, const SkPaintOrID& paint) override;
    void drawTextShadow(const sk_sp<SkTextBlob>& blob, SkScalar x, SkScalar y, SkColor color, SkScalar blurSigma) override;
#else
    void drawTextBlob(
        const std::shared_ptr<RSTextBlob>& blob, SkScalar x, SkScalar y, const SkPaintOrID& paint) override {}
    void drawTextShadow(
        const std::shared_ptr<RSTextBlob>& blob, SkScalar x, SkScalar y, SkColor color, SkScalar blurSigma) override {}
#endif
    void drawRect(const SkRect& rect, const SkPaintOrID& paint) override;
#ifdef ENABLE_TEXT_ENHANCE
    void drawRRect(const SkRRect& rrect, const SkColor color) override;
#endif
    void drawFilledRect(const SkRect& rect, const DecorationStyle& decorStyle) override;
#ifndef ENABLE_DRAWING_ADAPTER
    void drawPath(const SkPath& path, const DecorationStyle& decorStyle) override;
#else
    void drawPath(const RSPath& path, const DecorationStyle& decorStyle) override {}
#endif
    void drawLine(SkScalar x0, SkScalar y0, SkScalar x1, SkScalar y1, const DecorationStyle& decorStyle) override;

    void clipRect(const SkRect& rect) override;
    void translate(SkScalar dx, SkScalar dy) override;

    void save() override;
    void restore() override;

private:
    SkCanvas* fCanvas;
};

class ParagraphPainterAutoRestore {
public:
    ParagraphPainterAutoRestore(ParagraphPainter* painter)
        : fPainter(painter) {
        fPainter->save();
    }

    ~ParagraphPainterAutoRestore() {
        fPainter->restore();
    }

private:
    ParagraphPainter*   fPainter;
};

}  // namespace textlayout
}  // namespace skia

#endif  // ParagraphPainterImpl_DEFINED
