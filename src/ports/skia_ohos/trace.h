/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

 #ifndef TEXT_TRACE_H
 #define TEXT_TRACE_H
 
 #ifdef ENABLE_OHOS_ENHANCE
 #include "hitrace_meter.h"
 #include "parameters.h"
namespace OHOS::Rosen {
    #define LIKELY(exp) (__builtin_expect((exp) != 0, true))
    #define UNLIKELY(exp) (__builtin_expect((exp) != 0, false))
}
 #endif
 
 namespace OHOS::Rosen {
 #ifdef ENABLE_OHOS_ENHANCE
 enum class TextTraceLevel {
     TEXT_TRACE_LEVEL_DEFAULT,
     TEXT_TRACE_LEVEL_LOW,
     TEXT_TRACE_LEVEL_MIDDLE,
     TEXT_TRACE_LEVEL_HIGH
 };
 
 #define TEXT_TRACE(name) OHOS::Rosen::TextOptionalTrace optionalTrace(name)
 #define TEXT_TRACE_FUNC() OHOS::Rosen::TextOptionalTrace optionalTrace(__PRETTY_FUNCTION__)
 #define TEXT_TRACE_LEVEL(level, name) OHOS::Rosen::TextOptionalTrace::TraceWithLevel(level, name, __PRETTY_FUNCTION__)
 
 class TextOptionalTrace {
 public:
     TextOptionalTrace(std::string traceStr)
     {
         static bool debugTraceEnable = (OHOS::system::GetIntParameter("persist.sys.graphic.openDebugTrace", 0) != 0);
         if (UNLIKELY(debugTraceEnable)) {
             std::string name { "Text#" };
             CutPrettyFunction(traceStr);
             name.append(traceStr);
             StartTrace(HITRACE_TAG_GRAPHIC_AGP | HITRACE_TAG_COMMERCIAL, name);
         }
     }
 
     ~TextOptionalTrace()
     {
         static bool debugTraceEnable = (OHOS::system::GetIntParameter("persist.sys.graphic.openDebugTrace", 0) != 0);
         if (UNLIKELY(debugTraceEnable)) {
             FinishTrace(HITRACE_TAG_GRAPHIC_AGP | HITRACE_TAG_COMMERCIAL);
         }
     }
 
     // Simplify __PRETTY_FUNCTION__ to only return class name and function name
     // case: std::unique_str<XXX::XXX::Xxx> XXX::XXX::ClassName::FunctionName()
     // retrun: ClassName::FunctionName
     static void CutPrettyFunction(std::string& str)
     {
         // find last '('
         size_t endIndex = str.rfind('(');
         if (endIndex == std::string::npos) {
             return;
         }
 
         // find the third ':' before '('
         auto rIter = std::make_reverse_iterator(str.begin() + endIndex);
         size_t count = 0;
         size_t startIndex = 0;
         for (; rIter != str.rend(); ++rIter) {
             if (*rIter == ':') {
                 count += 1;
                 if (count == 3) { // 3 means to stop iterating when reaching the third ':'
                     startIndex = str.rend() - rIter;
                     break;
                 }
             }
         }
         str = str.substr(startIndex, endIndex - startIndex);
     }
 
     static void TraceWithLevel(TextTraceLevel level, const std::string& traceStr, std::string caller)
     {
         static int32_t systemLevel =
             std::atoi(OHOS::system::GetParameter("persist.sys.graphic.openDebugTrace", "0").c_str());
         if ((systemLevel != 0) && (systemLevel <= static_cast<int32_t>(level))) {
             CutPrettyFunction(caller);
             HITRACE_METER_FMT(HITRACE_TAG_GRAPHIC_AGP, "Text#%s %s", traceStr.c_str(), caller.c_str());
         }
     }
 };
 
 #else
 #define TEXT_TRACE(name)
 #define TEXT_TRACE_FUNC()
 #define TEXT_TRACE_LEVEL(level, name)
 #endif
 } // namespace OHOS::Rosen
 #endif