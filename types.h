#pragma once

using millis_t = unsigned long;
using micros_t = unsigned long;


template < typename T >
inline T clamp(const T& x, const T& lo, const T& hi) {
  if (x < lo) return lo;
  if (hi < x) return hi;
  return x;
}

template < typename T >
inline T map_range(const T& x,
  const T& uLo, const T& uHi,
  const T& vLo, const T& vHi)
{
  return (x - uLo) * ((vHi - vLo) / (uHi - uLo)) + vLo;
}

template < typename T >
inline T map_range_clamped(const T& x,
  const T& uLo, const T& uHi,
  const T& vLo, const T& vHi)
{
  return clamp((x - uLo) * ((vHi - vLo) / (uHi - uLo)) + vLo, vLo, vHi);
}

