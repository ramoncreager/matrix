
#include "NANutils.h"

#include<math.h>

#ifdef TESTMAIN

#include<stdio.h>

#ifdef VXWORKS
#include<private/mathP.h>
int
testNan()
#else
#if !defined(_WIN32)
extern "C" int isnand(double);
#endif
int main(int argc, char **argv)
#endif
{
double d;
float f;

    d = NANDvalue();
    f = NANFvalue();

#if 0
#ifdef VXWORKS
    if(isNan(d)) {
#else
    if(isnand(d)) {
#endif
        printf("d is NAN!\n");
    }
#endif
    if(isNAND(d)) {
        printf("isNAND() indicates NAN\n");
    }
    if(isNANF(f)) {
        printf("isNANF() indicates NAN\n");
    }

    printf("%lf %f \n", d, f);
    return(0);
}
#endif

/* This is straight from the IEEE-754 (1985) standard, 
   section 3.2.2 (Double format), page 9.
   It should be noted that the NANXvalue() functions return a NAN, but
   NAN is defined as a family of values, not a single bit value. Comparisons
   operators usually don't work with NAN values.
*/
double
NANDvalue()
{
union t {
    double d;
    unsigned int l[2];
} ut;

/* Intel architectures */
#if defined(LINUX) || defined(_WIN32_)
    ut.l[0] = 1;
    ut.l[1] = 2047 << 20;
#else /* Sparc/Motorola */
    ut.l[1] = 1;
    ut.l[0] = 2047 << 20;
#endif
   

    return(ut.d);
}

int
isNAND(double x)
{
union t {
    double d;
    unsigned int l[2];
} ut;

    ut.d = x;
/* Intel architectures */
#if defined(LINUX) || defined(_WIN32_)
    if(((ut.l[1] & 0x7FF00000) >> 20) == 2047 &&
         ((ut.l[1] & 0x000FFFFF) || ut.l[0]))
#else /* Sparc/Motorola */
    if(((ut.l[0] & 0x7FF00000) >> 20) == 2047 &&
         ((ut.l[0] & 0x000FFFFF) || ut.l[1]))
#endif
        return(1);
    else
        return(0);
}

/* These are straight from the IEEE-754 (1985) standard, 
   section 3.2.1 (Single format), page 9.
*/
int
isNANF(float x)
{
union t {
    float f;
    unsigned int l;
} ut;

    ut.f = x;
    if((ut.l & 0x7f800000L) == 0x7f800000L &&
       (ut.l & 0x007fffffL) != 0x0) 
        return(1);
    else
        return(0);
}

float
NANFvalue()
{
union t {
    float f;
    unsigned int l;
} ut;

    ut.l = (255 << 23) | 1;
    return(ut.f);
}



