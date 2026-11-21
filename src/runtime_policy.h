#pragma once
#include <stddef.h>
#include <stdint.h>

inline bool deadlineReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

inline bool weatherFetchDue(uint32_t now, uint32_t lastFetch, uint32_t lastAttempt,
                            uint32_t interval, uint32_t retryDelay, bool failed) {
  // Failed scheduled refreshes respect backoff even with an old good sample.
  if (failed) return now - lastAttempt >= retryDelay;
  return lastFetch == 0 ? lastAttempt == 0 : now - lastFetch >= interval;
}

// Index of the next page after `current` that has content, searching every
// entry once and wrapping around. Returns `count` when none has content.
template <class HasContent>
inline size_t nextPageWithContent(size_t current, size_t count, HasContent hasContent) {
  for (size_t step = 1; step <= count; step++) {
    const size_t candidate = (current + step) % count;
    if (hasContent(candidate)) return candidate;
  }
  return count;
}
