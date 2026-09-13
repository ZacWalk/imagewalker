// Fixed point math helpers by Zac Walker.

#define INT_TO_FIXED(x)         ((x) << 16)
#define DOUBLE_TO_FIXED(x)      ((int)(x * 65536.0 + 0.5))
#define FIXED_TO_INT(x)         ((x) >> 16)
#define FIXED_TO_DOUBLE(x)      (((double)x) / 65536.0)
#define ROUND_FIXED_TO_INT(x)   (((x) + 0x8000) >> 16)


#define ONE             INT_TO_FIXED(1)
#define FIXED_PI        205887L
#define FIXED_2PI       411775L
#define FIXED_E         178144L
#define FIXED_ROOT2      74804L
#define FIXED_ROOT3     113512L
#define FIXED_GOLDEN    106039L

inline int FixedMul(int x, int y)
{
   return (int)(((__int64)x * (__int64)y) >> 16);
}

// edx:eax = x * y, plus a half for rounding, then shifted back down 16.
#define FIXED_MUL(x, y, z) \
   (z) = (int)(((((__int64)(x)) * ((__int64)(y))) + 0x8000) >> 16)


// edx:eax held x shifted up 16 before the divide.
#define FIXED_DIV(x, y, z) \
   (z) = (int)((((__int64)(x)) << 16) / ((__int64)(y)))




/*
Fixed32 FixedMul(Fixed32 num1, Fixed32 num2);
Fixed32 FixedDiv(Fixed32 numer, Fixed32 denom);

#pragma aux FixedMul =      \
    "imul edx"              \
    "add eax, 8000h"        \
    "adc edx, 0"            \
    "shrd eax, edx, 16"     \
    parm caller [eax] [edx] \
    value [eax]             \
    modify [eax edx];

#pragma aux FixedDiv =      \
    "xor eax, eax"          \
    "shrd eax, edx, 16"     \
    "sar edx, 16"           \
    "idiv ebx"              \
    parm caller [edx] [ebx] \
    value [eax]             \
    modify [eax ebx edx];
	*/