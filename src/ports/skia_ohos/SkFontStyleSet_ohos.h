// Copyright (c) 2023 Huawei Device Co., Ltd. All rights reserved
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SKFONTSTYLESET_OHOS_H
#define SKFONTSTYLESET_OHOS_H

#include "SkFontMgr.h"

#include "FontConfig_ohos.h"
#include "SkTypeface_ohos.h"

/*!
 * \brief To implement SkFontStyleSet for ohos platform
 */
class SK_API SkFontStyleSet_OHOS : public SkFontStyleSet {
public:
    SkFontStyleSet_OHOS(const std::shared_ptr<FontConfig_OHOS>& fontConfig,
        int index, bool isFallback = false);
    ~SkFontStyleSet_OHOS() override = default;
    int count() override;
    void getStyle(int index, SkFontStyle* style, SkString* styleName) override;
    SkTypeface* createTypeface(int index) override;
    SkTypeface* matchStyle(const SkFontStyle& pattern) override;
private:
    std::shared_ptr<FontConfig_OHOS> fontConfig_ = nullptr; // the object of FontConfig_OHOS
    int styleIndex = 0; // the index of the font style set
    bool isFallback = false; // the flag of font style set. False for fallback family, true for generic family.
    int tpCount = -1; // the typeface count in the font style set
};

#endif /* SKFONTSTYLESET_OHOS_H */
