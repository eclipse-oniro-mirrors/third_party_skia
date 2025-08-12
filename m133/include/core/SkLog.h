/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SKLOG_DEFINED
#define SKLOG_DEFINED

#ifdef SKIA_OHOS
#include <hilog/log.h>

#undef LOG_DOMAIN
#define LOG_DOMAIN 0xD001406

#undef LOG_TAG
#define LOG_TAG "skia"

#define SK_LOGD(fmt, ...) HILOG_DEBUG(LOG_CORE, fmt, ##__VA_ARGS__)
#define SK_LOGI(fmt, ...) HILOG_INFO(LOG_CORE, fmt, ##__VA_ARGS__)
#define SK_LOGW(fmt, ...) HILOG_WARN(LOG_CORE, fmt, ##__VA_ARGS__)
#define SK_LOGE(fmt, ...) HILOG_ERROR(LOG_CORE, fmt, ##__VA_ARGS__)
#else
#define SK_LOGD(fmt, ...)
#define SK_LOGI(fmt, ...)
#define SK_LOGW(fmt, ...)
#define SK_LOGE(fmt, ...)
#endif

#endif  // SKLOG_DEFINED
