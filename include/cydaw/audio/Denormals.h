#pragma once
#if defined(_M_IX86) || defined(_M_X64) || defined(__SSE__)
#include <xmmintrin.h>
#endif
namespace cydaw::audio {
class ScopedNoDenormals {
#if defined(_M_IX86) || defined(_M_X64) || defined(__SSE__)
  unsigned previous_ = _mm_getcsr();

public:
  ScopedNoDenormals() { _mm_setcsr(previous_ | 0x8040u); }
  ~ScopedNoDenormals() { _mm_setcsr(previous_); }
#else
public:
  ScopedNoDenormals() = default;
#endif
};
} // namespace cydaw::audio
