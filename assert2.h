#if defined(__GNUC__) && defined(NDEBUG)
#define assert(x) do { if (!(x)) __builtin_unreachable(); } while (0)
#else
#include <assert.h>
#endif
