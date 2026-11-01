/* libm_fill.c - libm functions nxdk's pdclib only stubs out.
 *
 * nxdk implements these as `assert(0); // Not implemented`, and its assert
 * is `cli; hlt`: the first call freezes the console with a black screen and
 * nothing in the log. Defining them here keeps the linker from pulling the
 * stub members (fma.obj, lround.obj, scalbn.obj, exp2.obj) out of libpdclib.
 *
 * Callers: aurora's C_VECSquareMag (fmaf, to round like Gekko's fmadds),
 * melee-pc's widescreen (lroundf), its large-argument trig reduction and
 * rank_exp (scalbn); MusyX's adsrConvertTimeCents (exp2f: clang folds its
 * powf(2.f, x) into exp2f), first reached when the Frigate escape timer ends
 * (a tester's console, 2026-11-01: "Assertion failed ... exp2.c:12, exp2f"). */
#include <math.h>

/* x*y is exact in double (24 + 24 bits); the sum is rounded to double, then
 * to float. That differs from a truly fused result by one ulp only when the
 * double sum lands exactly halfway between two floats, which is rare enough
 * for gameplay (netplay, which would need bit-exact Gekko rounding, is off). */
float fmaf(float x, float y, float z) { return (float)((double)x * (double)y + (double)z); }

double fma(double x, double y, double z) { return x * y + z; }

/* round half away from zero; x - trunc(x) is exact */
long lroundf(float x) {
    float t = truncf(x);
    if (fabsf(x - t) >= 0.5f) t += x < 0 ? -1.0f : 1.0f;
    return (long)t;
}

long lround(double x) {
    double t = trunc(x);
    if (fabs(x - t) >= 0.5) t += x < 0 ? -1.0 : 1.0;
    return (long)t;
}

double scalbn(double x, int n) { return ldexp(x, n); }
float scalbnf(float x, int n) { return ldexpf(x, n); }

/* 2^x on the x87, as nxdk's exp() but without its fldl2e: split x into an
 * integer n (frndint) and a fraction f in [-0.5, 0.5], 2^f = f2xm1(f) + 1,
 * then fscale by n. Infinities and NaN return as exp() does. Kept in asm so
 * the compiler can't fold it back into a call to exp2. */
double exp2(double x)
{
    if (isnan(x) || x == INFINITY) return x;
    if (x == -INFINITY) return 0.0;
    __asm__ ("fld %%st(0);"
             "frndint;"
             "fld1;"
             "fscale;"
             "fxch %%st(2);"
             "fsubp %%st(1);"
             "f2xm1;"
             "fld1;"
             "faddp;"
             "fmulp" : "+t"(x));
    return x;
}

float exp2f(float x) { return (float)exp2((double)x); }

long double exp2l(long double x) { return exp2((double)x); }
