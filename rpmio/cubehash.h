#ifndef cubehash_h
#define cubehash_h

#include <string.h>
#include <stdint.h>

#define	BitSequence	cubehash_BitSequence
#define	DataLength	cubehash_DataLength
#define	hashState	cubehash_hashState
#define	HashReturn	int

#define	Init		cubehash_Init
#define	Update		cubehash_Update
#define	Final		cubehash_Final
#define	Hash		cubehash_Hash

#if defined(__SSE2__)
#define	OPTIMIZE_SSE2
#endif

#if defined(OPTIMIZE_SSE2)
#include <emmintrin.h>
#endif

typedef unsigned char BitSequence;
typedef unsigned long long DataLength;

typedef struct {
    int hashbitlen;
    int rounds;
    int blockbytes;
    int pos;		/* number of bits read into x from current block */
#if defined(OPTIMIZE_SSE2)
    __m128i x[8];
#else
    uint32_t x[32];
#endif
} hashState;

HashReturn Init(hashState *state, int hashbitlen, int rounds, int blockbytes);

HashReturn Update(hashState *state, const BitSequence *data,
                  DataLength databitlen);

HashReturn Final(hashState *state, BitSequence *hashval);

HashReturn Hash(int hashbitlen, const BitSequence *data,
                DataLength databitlen, BitSequence *hashval);

/* Impedance match bytes -> bits length. */
static inline
int _cubehash_Update(void * param, const void * _data, size_t _len)
{
    return Update(param, _data, (DataLength)(8 * _len));
}

#endif
