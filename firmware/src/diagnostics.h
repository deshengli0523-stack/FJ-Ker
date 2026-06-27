#pragma once

#include <cstddef>

#ifndef FJKER_ENABLE_DIAGNOSTICS
#define FJKER_ENABLE_DIAGNOSTICS 1
#endif

namespace diagnostics {
void formatEvent(char* dst, size_t dstLen, const char* area, const char* message);
void formatText(char* dst, size_t dstLen, const char* area, const char* key,
                const char* value);
void formatValue(char* dst, size_t dstLen, const char* area, const char* key,
                 long long value);
void formatSize(char* dst, size_t dstLen, const char* area, const char* key,
                size_t value);
void formatToken(char* dst, size_t dstLen, const char* area, const char* key,
                 const char* value);

void event(const char* area, const char* message);
void text(const char* area, const char* key, const char* value);
void token(const char* area, const char* key, const char* value);
void value(const char* area, const char* key, long long value);
void size(const char* area, const char* key, size_t value);
void result(const char* area, const char* action, bool ok);
}  // namespace diagnostics
