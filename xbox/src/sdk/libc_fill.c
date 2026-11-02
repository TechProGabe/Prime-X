/* libc_fill.c - C library functions nxdk leaves as traps.
 *
 * nxdk's strtod, strtof and strtold are assert(0) (xboxrt stdlib_ext_.c),
 * and its assert halts the CPU with interrupts off: no log line, no
 * watchdog, a frozen picture. Prime parses numbers in text markup
 * (CTextParser's atof), which froze the file select. Here a real strtod
 * (decimal and hex floats, inf/nan, exponents; exact to the last bit or one
 * ulp, which is plenty for text sizes and settings), and an assert that
 * says where it failed. */
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "xhw.h"

void _xbox_assert(char const* const expression, char const* const file_name, char const* const function_name,
                  unsigned long line) {
    char msg[256];
    snprintf(msg, sizeof msg, "%s (%s:%lu, %s)", expression ? expression : "?", file_name ? file_name : "?", line,
             function_name ? function_name : "?");
    xhw_logf("[ASSERT] %s", msg);
    xhw_fatal("Assertion failed", msg);
}

static int match_ci(const char* s, const char* word) {
    while (*word)
        if (tolower((unsigned char)*s++) != *word++) return 0;
    return 1;
}

/* not named strtod: xboxrt's stdlib_ext_.obj (which the link needs for
 * other functions) defines that one. Game code reaches this through atof;
 * a strtod call still gets nxdk's trap, which now logs and stops. */
double px_strtod(const char* nptr, char** endptr) {
    const char* s = nptr;
    int neg = 0, any = 0, exp = 0, esign = 1, e = 0;
    double v = 0.0;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '+' || *s == '-') neg = *s++ == '-';
    if (match_ci(s, "inf")) {
        s += match_ci(s, "infinity") ? 8 : 3;
        if (endptr) *endptr = (char*)s;
        return neg ? -HUGE_VAL : HUGE_VAL;
    }
    if (match_ci(s, "nan")) {
        s += 3;
        if (*s == '(') {
            const char* p = s + 1;
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            if (*p == ')') s = p + 1;
        }
        if (endptr) *endptr = (char*)s;
        return neg ? -NAN : NAN;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && (isxdigit((unsigned char)s[2]) ||
                                                        (s[2] == '.' && isxdigit((unsigned char)s[3])))) {
        s += 2;
        for (; isxdigit((unsigned char)*s); s++, any = 1)
            v = v * 16.0 + (isdigit((unsigned char)*s) ? *s - '0' : tolower((unsigned char)*s) - 'a' + 10);
        if (*s == '.')
            for (s++; isxdigit((unsigned char)*s); s++, any = 1, exp -= 4)
                v = v * 16.0 + (isdigit((unsigned char)*s) ? *s - '0' : tolower((unsigned char)*s) - 'a' + 10);
        if (any && (*s == 'p' || *s == 'P')) {
            const char* p = s + 1;
            if (*p == '+' || *p == '-') esign = *p++ == '-' ? -1 : 1;
            if (isdigit((unsigned char)*p)) {
                for (; isdigit((unsigned char)*p); p++)
                    if (e < 100000) e = e * 10 + (*p - '0');
                s = p;
                exp += esign * e;
            }
        }
        if (endptr) *endptr = (char*)(any ? s : nptr);
        v = ldexp(v, exp);
        return neg ? -v : v;
    }
    {
        /* up to 19 significant digits exactly in an integer, then scaled */
        unsigned long long m = 0;
        int digits = 0, drop = 0;
        for (; isdigit((unsigned char)*s); s++, any = 1) {
            if (digits < 19) {
                m = m * 10 + (unsigned)(*s - '0');
                if (m) digits++;
            } else {
                drop++;
            }
        }
        if (*s == '.') {
            for (s++; isdigit((unsigned char)*s); s++, any = 1) {
                if (digits < 19) {
                    m = m * 10 + (unsigned)(*s - '0');
                    if (m) digits++;
                    exp--;
                }
            }
        }
        if (!any) {
            if (endptr) *endptr = (char*)nptr;
            return 0.0;
        }
        exp += drop;
        if (*s == 'e' || *s == 'E') {
            const char* p = s + 1;
            if (*p == '+' || *p == '-') esign = *p++ == '-' ? -1 : 1;
            if (isdigit((unsigned char)*p)) {
                for (; isdigit((unsigned char)*p); p++)
                    if (e < 100000) e = e * 10 + (*p - '0');
                s = p;
                exp += esign * e;
            }
        }
        if (endptr) *endptr = (char*)s;
        v = (double)m;
        if (m && exp) {
            /* 10^|exp| by squaring in long double (x87's 64-bit mantissa) */
            long double p10 = 1.0L, base = 10.0L;
            unsigned n = (unsigned)(exp < 0 ? -exp : exp);
            if (n > 400) n = 400;
            while (n) {
                if (n & 1) p10 *= base;
                base *= base;
                n >>= 1;
            }
            v = (double)(exp < 0 ? (long double)m / p10 : (long double)m * p10);
            if (v == HUGE_VAL || (v == 0.0 && m)) errno = ERANGE;
        }
        return neg ? -v : v;
    }
}

double atof(const char* s) { return px_strtod(s, NULL); }

/* pdclib's abort raises SIGABRT, whose default ends the program: on the
 * Xbox that relaunches the XBE with nothing in any log. MetroidPrimePort
 * aborts on a resource no loaded pak holds (CResLoader's PortCheckLoad),
 * after logging why. */
void abort(void) {
    xhw_logf("[ABORT] abort() called from %p", __builtin_return_address(0));
    xhw_fatal("Game error", "abort() was called; boot.log has the lines before it.");
}

/* nxdk's _snwprintf is assert(0) (xboxrt wchar_ext_.c, the file's only
 * function, so this one stands in for it). libc++'s std::to_wstring(float,
 * double) formats through it, and tools/xbox/check_stubs.py found it
 * reachable (2026-11-02). The format and its output are ASCII in practice
 * ("%f"): narrowed, formatted by vsnprintf, widened. MSVC's semantics, which
 * libc++ expects of it: -1 and no terminator when the buffer is too small. */
#include <stdarg.h>
#include <wchar.h>

int _snwprintf(wchar_t* buffer, size_t count, const wchar_t* format, ...) {
    char fmt[128], out[512];
    size_t i;
    int n;
    va_list ap;
    if (!format) return -1;
    for (i = 0; i + 1 < sizeof fmt && format[i]; i++) fmt[i] = (char)(format[i] < 128 ? format[i] : '?');
    fmt[i] = '\0';
    va_start(ap, format);
    n = vsnprintf(out, sizeof out, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if ((size_t)n >= sizeof out) n = (int)sizeof out - 1;
    if (!buffer) return n;
    for (i = 0; i < count && i <= (size_t)n; i++) buffer[i] = (wchar_t)(unsigned char)out[i];
    return (size_t)n < count ? n : -1;
}
