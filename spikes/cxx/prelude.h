/* spike prelude: what the Xbox twin of compat.h will need ahead of it */
#include <stdarg.h>
#ifdef __cplusplus
#include <new>
[[noreturn]] void px_throw(const char* file, int line);
#define throw for (;;) px_throw(__FILE__, __LINE__), (void)
#define try if (1)
#define catch(...) else if (0)
#endif
