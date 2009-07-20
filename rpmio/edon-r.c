/* Reference implementation of tweaked Edon-R */
#include <string.h> 
#include "edon-r.h"

// EdonR allows to call Update() function consecutively only if the total length of stored 
// unprocessed data and the new supplied data is less than or equal to the BLOCK_SIZE on which the 
// compression functions operates. Otherwise BAD_CONSECUTIVE_CALL_TO_UPDATE is returned.
typedef enum { SUCCESS = 0, FAIL = 1, BAD_HASHLEN = 2, BAD_CONSECUTIVE_CALL_TO_UPDATE = 3 } HashReturn;

#define rotl32(x,n)   (((x) << n) | ((x) >> (32 - n)))
#define rotr32(x,n)   (((x) >> n) | ((x) << (32 - n)))

#define rotl64(x,n)   (((x) << n) | ((x) >> (64 - n)))
#define rotr64(x,n)   (((x) >> n) | ((x) << (64 - n)))

/* EdonR224 initial double chaining pipe */
static
const uint32_t i224p2[16] =
{   0x00010203ul, 0x04050607ul, 0x08090a0bul, 0x0c0d0e0ful,
    0x10111213ul, 0x14151617ul, 0x18191a1bul, 0x1c1d1e1ful,
    0x20212223ul, 0x24252627ul, 0x28292a2bul, 0x2c2d2e2ful,
    0x30313233ul, 0x34353637ul, 0x38393a3bul, 0x3c3d3e3ful,
};
/* EdonR256 initial double chaining pipe */
static
const uint32_t i256p2[16] =
{   0x40414243ul, 0x44454647ul, 0x48494a4bul, 0x4c4d4e4ful,
    0x50515253ul, 0x54555657ul, 0x58595a5bul, 0x5c5d5e5ful,
    0x60616263ul, 0x64656667ul, 0x68696a6bul, 0x6c6d6e6ful,
    0x70717273ul, 0x74757677ul, 0x78797a7bul, 0x7c7d7e7ful,
};

/* EdonR384 initial double chaining pipe */
static
const uint64_t i384p2[16] =
{
    0x0001020304050607ull, 0x08090a0b0c0d0e0full,
    0x1011121314151617ull, 0x18191a1b1c1d1e1full,
    0x2021222324252627ull, 0x28292a2b2c2d2e2full,
    0x3031323334353637ull, 0x38393a3b3c3d3e3full,
    0x4041424344454647ull, 0x48494a4b4c4d4e4full,
    0x5051525354555657ull, 0x58595a5b5c5d5e5full,
    0x6061626364656667ull, 0x68696a6b6c6d6e6full,
    0x7071727374757677ull, 0x78797a7b7c7d7e7full
};

/* EdonR512 initial double chaining pipe */
static
const uint64_t i512p2[16] =
{
    0x8081828384858687ull, 0x88898a8b8c8d8e8full,
    0x9091929394959697ull, 0x98999a9b9c9d9e9full,
    0xa0a1a2a3a4a5a6a7ull, 0xa8a9aaabacadaeafull,
    0xb0b1b2b3b4b5b6b7ull, 0xb8b9babbbcbdbebfull,
    0xc0c1c2c3c4c5c6c7ull, 0xc8c9cacbcccdcecfull,
    0xd0d1d2d3d4d5d6d7ull, 0xd8d9dadbdcdddedfull,
    0xe0e1e2e3e4e5e6e7ull, 0xe8e9eaebecedeeefull,
    0xf0f1f2f3f4f5f6f7ull, 0xf8f9fafbfcfdfeffull
};

#define hashState224(x)  ((x)->pipe->p256)
#define hashState256(x)  ((x)->pipe->p256)
#define hashState384(x)  ((x)->pipe->p512)
#define hashState512(x)  ((x)->pipe->p512)

#define Q256(x0,x1,x2,x3,x4,x5,x6,x7,y0,y1,y2,y3,y4,y5,y6,y7,z0,z1,z2,z3,z4,z5,z6,z7) \
{\
/* First Latin Square\
0   7   1   3   2   4   6   5\
4   1   7   6   3   0   5   2\
7   0   4   2   5   3   1   6\
1   4   0   5   6   2   7   3\
2   3   6   7   1   5   0   4\
5   2   3   1   7   6   4   0\
3   6   5   0   4   7   2   1\
6   5   2   4   0   1   3   7\
*/\
    t0  = 0xaaaaaaaaul +\
	      x0 + x1 + x2 + x4 + x7;\
    t1  = x0 + x1 + x3 + x4 + x7;\
    t2  = x0 + x1 + x4 + x6 + x7;\
    t3  = x2 + x3 + x5 + x6 + x7;\
    t4  = x1 + x2 + x3 + x5 + x6;\
    t5  = x0 + x2 + x3 + x4 + x5;\
    t6  = x0 + x1 + x5 + x6 + x7;\
    t7  = x2 + x3 + x4 + x5 + x6;\
    t1  = rotl32((t1), 4);\
    t2  = rotl32((t2), 8);\
    t3  = rotl32((t3),13);\
    t4  = rotl32((t4),17);\
    t5  = rotl32((t5),22);\
    t6  = rotl32((t6),24);\
    t7  = rotl32((t7),29);\
\
    t8  = t3 ^ t5 ^ t6;\
    t9  = t2 ^ t5 ^ t6;\
    t10 = t2 ^ t3 ^ t5;\
    t11 = t0 ^ t1 ^ t4;\
    t12 = t0 ^ t4 ^ t7;\
    t13 = t1 ^ t6 ^ t7;\
    t14 = t2 ^ t3 ^ t4;\
    t15 = t0 ^ t1 ^ t7;\
\
/* Second Orthogonal Latin Square\
0   4   2   3   1   6   5   7\
7   6   3   2   5   4   1   0\
5   3   1   6   0   2   7   4\
1   0   5   4   3   7   2   6\
2   1   0   7   4   5   6   3\
3   5   7   0   6   1   4   2\
4   7   6   1   2   0   3   5\
6   2   4   5   7   3   0   1\
*/\
    t0  = 0x55555555ul +\
          y0 + y1 + y2 + y5 + y7;\
    t1  = y0 + y1 + y3 + y4 + y6;\
    t2  = y0 + y1 + y2 + y3 + y5;\
    t3  = y2 + y3 + y4 + y6 + y7;\
    t4  = y0 + y1 + y3 + y4 + y5;\
    t5  = y2 + y4 + y5 + y6 + y7;\
    t6  = y1 + y2 + y5 + y6 + y7;\
    t7  = y0 + y3 + y4 + y6 + y7;\
    t1  = rotl32((t1), 5);\
    t2  = rotl32((t2), 9);\
    t3  = rotl32((t3),11);\
    t4  = rotl32((t4),15);\
    t5  = rotl32((t5),20);\
    t6  = rotl32((t6),25);\
    t7  = rotl32((t7),27);\
\
    z5   = t8  + (t3 ^ t4 ^ t6);\
    z6   = t9  + (t2 ^ t5 ^ t7);\
    z7   = t10 + (t4 ^ t6 ^ t7);\
    z0   = t11 + (t0 ^ t1 ^ t5);\
    z1   = t12 + (t2 ^ t6 ^ t7);\
    z2   = t13 + (t0 ^ t1 ^ t3);\
    z3   = t14 + (t0 ^ t3 ^ t4);\
    z4   = t15 + (t1 ^ t2 ^ t5);\
}

#define Q512(x0,x1,x2,x3,x4,x5,x6,x7,y0,y1,y2,y3,y4,y5,y6,y7,z0,z1,z2,z3,z4,z5,z6,z7) \
{\
/* First Latin Square\
0   7   1   3   2   4   6   5\
4   1   7   6   3   0   5   2\
7   0   4   2   5   3   1   6\
1   4   0   5   6   2   7   3\
2   3   6   7   1   5   0   4\
5   2   3   1   7   6   4   0\
3   6   5   0   4   7   2   1\
6   5   2   4   0   1   3   7\
*/\
    tt0  = 0xaaaaaaaaaaaaaaaaull +\
	       x0 + x1 + x2 + x4 + x7;\
    tt1  = x0 + x1 + x3 + x4 + x7;\
    tt2  = x0 + x1 + x4 + x6 + x7;\
    tt3  = x2 + x3 + x5 + x6 + x7;\
    tt4  = x1 + x2 + x3 + x5 + x6;\
    tt5  = x0 + x2 + x3 + x4 + x5;\
    tt6  = x0 + x1 + x5 + x6 + x7;\
    tt7  = x2 + x3 + x4 + x5 + x6;\
    tt1  = rotl64((tt1), 5);\
    tt2  = rotl64((tt2),15);\
    tt3  = rotl64((tt3),22);\
    tt4  = rotl64((tt4),31);\
    tt5  = rotl64((tt5),40);\
    tt6  = rotl64((tt6),50);\
    tt7  = rotl64((tt7),59);\
\
    tt8  = tt3 ^ tt5 ^ tt6;\
    tt9  = tt2 ^ tt5 ^ tt6;\
    tt10 = tt2 ^ tt3 ^ tt5;\
    tt11 = tt0 ^ tt1 ^ tt4;\
    tt12 = tt0 ^ tt4 ^ tt7;\
    tt13 = tt1 ^ tt6 ^ tt7;\
    tt14 = tt2 ^ tt3 ^ tt4;\
    tt15 = tt0 ^ tt1 ^ tt7;\
\
/* Second Orthogonal Latin Square\
0   4   2   3   1   6   5   7\
7   6   3   2   5   4   1   0\
5   3   1   6   0   2   7   4\
1   0   5   4   3   7   2   6\
2   1   0   7   4   5   6   3\
3   5   7   0   6   1   4   2\
4   7   6   1   2   0   3   5\
6   2   4   5   7   3   0   1\
*/\
    tt0  = 0x5555555555555555ull +\
           y0 + y1 + y2 + y5 + y7;\
    tt1  = y0 + y1 + y3 + y4 + y6;\
    tt2  = y0 + y1 + y2 + y3 + y5;\
    tt3  = y2 + y3 + y4 + y6 + y7;\
    tt4  = y0 + y1 + y3 + y4 + y5;\
    tt5  = y2 + y4 + y5 + y6 + y7;\
    tt6  = y1 + y2 + y5 + y6 + y7;\
    tt7  = y0 + y3 + y4 + y6 + y7;\
    tt1  = rotl64((tt1),10);\
    tt2  = rotl64((tt2),19);\
    tt3  = rotl64((tt3),29);\
    tt4  = rotl64((tt4),36);\
    tt5  = rotl64((tt5),44);\
    tt6  = rotl64((tt6),48);\
    tt7  = rotl64((tt7),55);\
\
    z5   = tt8  + (tt3 ^ tt4 ^ tt6);\
    z6   = tt9  + (tt2 ^ tt5 ^ tt7);\
    z7   = tt10 + (tt4 ^ tt6 ^ tt7);\
    z0   = tt11 + (tt0 ^ tt1 ^ tt5);\
    z1   = tt12 + (tt2 ^ tt6 ^ tt7);\
    z2   = tt13 + (tt0 ^ tt1 ^ tt3);\
    z3   = tt14 + (tt0 ^ tt3 ^ tt4);\
    z4   = tt15 + (tt1 ^ tt2 ^ tt5);\
}


int edonr_Init(edonr_hashState *state, int hashbitlen)
{
	switch(hashbitlen)
	{
		case 224:
		state->hashbitlen = 224;
		state->bits_processed = 0;
		state->unprocessed_bits = 0;
		memcpy(hashState224(state)->DoublePipe, i224p2,  16 * sizeof(uint32_t));
		return(SUCCESS);

		case 256:
		state->hashbitlen = 256;
		state->bits_processed = 0;
		state->unprocessed_bits = 0;
		memcpy(hashState256(state)->DoublePipe, i256p2,  16 * sizeof(uint32_t));
		return(SUCCESS);

		case 384:		
		state->hashbitlen = 384;
		state->bits_processed = 0;
		state->unprocessed_bits = 0;
		memcpy(hashState384(state)->DoublePipe, i384p2,  16 * sizeof(uint64_t));
		return(SUCCESS);

		case 512:
		state->hashbitlen = 512;
		state->bits_processed = 0;
		state->unprocessed_bits = 0;
		memcpy(hashState224(state)->DoublePipe, i512p2,  16 * sizeof(uint64_t));
		return(SUCCESS);

        default:    return(BAD_HASHLEN);
    }
}




int edonr_Update(edonr_hashState *state, const void *_data, size_t _len)
{
	const unsigned char *data = _data;
	unsigned long long databitlen = 8 * _len;
	uint32_t *data32, *p256;
	uint32_t t0,  t1,  t2,  t3,  t4,  t5,  t6,  t7;
	uint32_t t8,  t9, t10, t11, t12, t13, t14, t15;

	uint64_t *data64, *p512;
	uint64_t tt0,  tt1,  tt2,  tt3,  tt4,  tt5,  tt6,  tt7; 
	uint64_t tt8,  tt9, tt10, tt11, tt12, tt13, tt14, tt15; 

	int LastBytes;

	switch(state->hashbitlen)
	{
		case 224:
		case 256:
			if (state->unprocessed_bits > 0)
			{
				if ( state->unprocessed_bits + databitlen > EdonR256_BLOCK_SIZE * 8)
				{
					return BAD_CONSECUTIVE_CALL_TO_UPDATE;
				}
				else
				{
					LastBytes = (int)databitlen >> 3; // LastBytes = databitlen / 8
					memcpy(hashState256(state)->LastPart + (state->unprocessed_bits >> 3), data, LastBytes );
					state->unprocessed_bits += (int)databitlen;
					databitlen = state->unprocessed_bits;
					data32 = (uint32_t *)hashState256(state)->LastPart;
				}
			}
			else 
				data32 = (uint32_t *)data;

			p256   = hashState256(state)->DoublePipe;
			while (databitlen >= EdonR256_BLOCK_SIZE * 8)
			{
				databitlen -= EdonR256_BLOCK_SIZE * 8;

				state->bits_processed += EdonR256_BLOCK_SIZE * 8;

				/* First row of quasigroup e-transformations */
				Q256( data32[15],  data32[14],  data32[13],  data32[12],  data32[11],  data32[10],  data32[ 9],  data32[ 8],
				      data32[ 0],  data32[ 1],  data32[ 2],  data32[ 3],  data32[ 4],  data32[ 5],  data32[ 6],  data32[ 7],
				        p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23]);
				Q256(   p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23],
				      data32[ 8],  data32[ 9],  data32[10],  data32[11],  data32[12],  data32[13],  data32[14],  data32[15],
				        p256[24],    p256[25],    p256[26],    p256[27],    p256[28],    p256[29],    p256[30],    p256[31]);

				/* Second row of quasigroup e-transformations */
				Q256(   p256[ 8],    p256[ 9],    p256[10],    p256[11],    p256[12],    p256[13],    p256[14],    p256[15],
				        p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23],
				        p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23]);
				Q256(   p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23],
					    p256[24],    p256[25],    p256[26],    p256[27],    p256[28],    p256[29],    p256[30],    p256[31],
				        p256[24],    p256[25],    p256[26],    p256[27],    p256[28],    p256[29],    p256[30],    p256[31]);

				/* Third row of quasigroup e-transformations */
				Q256(   p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23],
					    p256[ 0],    p256[ 1],    p256[ 2],    p256[ 3],    p256[ 4],    p256[ 5],    p256[ 6],    p256[ 7],
				        p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23]);
				Q256(   p256[24],    p256[25],    p256[26],    p256[27],    p256[28],    p256[29],    p256[30],    p256[31],
				        p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23],
				        p256[24],    p256[25],    p256[26],    p256[27],    p256[28],    p256[29],    p256[30],    p256[31]);

				/* Fourth row of quasigroup e-transformations */
				Q256( data32[ 7],  data32[ 6],  data32[ 5],  data32[ 4],  data32[ 3],  data32[ 2],  data32[ 1],  data32[ 0],
				        p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23],
				        p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23]);
				Q256(   p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23],
				        p256[24],    p256[25],    p256[26],    p256[27],    p256[28],    p256[29],    p256[30],    p256[31],
				        p256[24],    p256[25],    p256[26],    p256[27],    p256[28],    p256[29],    p256[30],    p256[31]);

				/* This is the proposed tweak on original SHA-3 Edon-R submission.  */
				/* Instead of the old compression function R(oldPipe, M), now the   */
				/* compression function is: R(oldPipe, M) xor oldPipe xor M'        */
				/* where M is represented in two parts i.e. M = (M0, M1), and       */
				/* M' = (M1, M0).                                                   */
				p256[ 0]^=(data32[ 8] ^ p256[16]);
				p256[ 1]^=(data32[ 9] ^ p256[17]);
				p256[ 2]^=(data32[10] ^ p256[18]);
				p256[ 3]^=(data32[11] ^ p256[19]);
				p256[ 4]^=(data32[12] ^ p256[20]);
				p256[ 5]^=(data32[13] ^ p256[21]);
				p256[ 6]^=(data32[14] ^ p256[22]);
				p256[ 7]^=(data32[15] ^ p256[23]);
				p256[ 8]^=(data32[ 0] ^ p256[24]);
				p256[ 9]^=(data32[ 1] ^ p256[25]);
				p256[10]^=(data32[ 2] ^ p256[26]);
				p256[11]^=(data32[ 3] ^ p256[27]);
				p256[12]^=(data32[ 4] ^ p256[28]);
				p256[13]^=(data32[ 5] ^ p256[29]);
				p256[14]^=(data32[ 6] ^ p256[30]);
				p256[15]^=(data32[ 7] ^ p256[31]);

				data32 += 16;
			}
			state->unprocessed_bits = (int)databitlen;
			if (databitlen > 0)
			{
				LastBytes = ((~(((- (int)databitlen)>>3) & 0x01ff)) + 1) & 0x01ff;  // LastBytes = Ceil(databitlen / 8)
				memcpy(hashState256(state)->LastPart, data32, LastBytes );
			}
			return(SUCCESS);


		case 384:
		case 512:
			if (state->unprocessed_bits > 0)
			{
				if ( state->unprocessed_bits + databitlen > EdonR512_BLOCK_SIZE * 8)
				{
					return BAD_CONSECUTIVE_CALL_TO_UPDATE;
				}
				else
				{
					LastBytes = (int)databitlen >> 3; // LastBytes = databitlen / 8
					memcpy(hashState512(state)->LastPart + (state->unprocessed_bits >> 3), data, LastBytes );
					state->unprocessed_bits += (int)databitlen;
					databitlen = state->unprocessed_bits;
					data64 = (uint64_t *)hashState512(state)->LastPart;
				}
			}
			else 
				data64 = (uint64_t *)data;

			p512   = hashState512(state)->DoublePipe;
			while (databitlen >= EdonR512_BLOCK_SIZE * 8)
			{
				databitlen -= EdonR512_BLOCK_SIZE * 8;

				state->bits_processed += EdonR512_BLOCK_SIZE * 8;

				/* First row of quasigroup e-transformations */
				Q512( data64[15],  data64[14],  data64[13],  data64[12],  data64[11],  data64[10],  data64[ 9],  data64[ 8],
				      data64[ 0],  data64[ 1],  data64[ 2],  data64[ 3],  data64[ 4],  data64[ 5],  data64[ 6],  data64[ 7],
				        p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23]);
				Q512(   p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23],
				      data64[ 8],  data64[ 9],  data64[10],  data64[11],  data64[12],  data64[13],  data64[14],  data64[15],
				        p512[24],    p512[25],    p512[26],    p512[27],    p512[28],    p512[29],    p512[30],    p512[31]);

				/* Second row of quasigroup e-transformations */
				Q512(   p512[ 8],    p512[ 9],    p512[10],    p512[11],    p512[12],    p512[13],    p512[14],    p512[15],
				        p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23],
				        p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23]);
				Q512(   p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23],
					    p512[24],    p512[25],    p512[26],    p512[27],    p512[28],    p512[29],    p512[30],    p512[31],
				        p512[24],    p512[25],    p512[26],    p512[27],    p512[28],    p512[29],    p512[30],    p512[31]);

				/* Third row of quasigroup e-transformations */
				Q512(   p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23],
					    p512[ 0],    p512[ 1],    p512[ 2],    p512[ 3],    p512[ 4],    p512[ 5],    p512[ 6],    p512[ 7],
				        p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23]);
				Q512(   p512[24],    p512[25],    p512[26],    p512[27],    p512[28],    p512[29],    p512[30],    p512[31],
				        p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23],
				        p512[24],    p512[25],    p512[26],    p512[27],    p512[28],    p512[29],    p512[30],    p512[31]);

				/* Fourth row of quasigroup e-transformations */
				Q512( data64[ 7],  data64[ 6],  data64[ 5],  data64[ 4],  data64[ 3],  data64[ 2],  data64[ 1],  data64[ 0],
				        p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23],
				        p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23]);
				Q512(   p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23],
				        p512[24],    p512[25],    p512[26],    p512[27],    p512[28],    p512[29],    p512[30],    p512[31],
				        p512[24],    p512[25],    p512[26],    p512[27],    p512[28],    p512[29],    p512[30],    p512[31]);

				/* This is the proposed tweak on original SHA-3 Edon-R submission.  */
				/* Instead of the old compression function R(oldPipe, M), now the   */
				/* compression function is: R(oldPipe, M) xor oldPipe xor M'        */
				/* where M is represented in two parts i.e. M = (M0, M1), and       */
				/* M' = (M1, M0).                                                   */
				p512[ 0]^=(data64[ 8] ^ p512[16]);
				p512[ 1]^=(data64[ 9] ^ p512[17]);
				p512[ 2]^=(data64[10] ^ p512[18]);
				p512[ 3]^=(data64[11] ^ p512[19]);
				p512[ 4]^=(data64[12] ^ p512[20]);
				p512[ 5]^=(data64[13] ^ p512[21]);
				p512[ 6]^=(data64[14] ^ p512[22]);
				p512[ 7]^=(data64[15] ^ p512[23]);
				p512[ 8]^=(data64[ 0] ^ p512[24]);
				p512[ 9]^=(data64[ 1] ^ p512[25]);
				p512[10]^=(data64[ 2] ^ p512[26]);
				p512[11]^=(data64[ 3] ^ p512[27]);
				p512[12]^=(data64[ 4] ^ p512[28]);
				p512[13]^=(data64[ 5] ^ p512[29]);
				p512[14]^=(data64[ 6] ^ p512[30]);
				p512[15]^=(data64[ 7] ^ p512[31]);

				data64 += 16;
			}
			state->unprocessed_bits = (int)databitlen;
			if (databitlen > 0)
			{
				LastBytes = ((~(((- (int)databitlen)>>3) & 0x03ff)) + 1) & 0x03ff; // LastBytes = Ceil(databitlen / 8)
				memcpy(hashState512(state)->LastPart, data64, LastBytes );
			}
			return(SUCCESS);
		
		default:    return(BAD_HASHLEN); //This should never happen
	}
}


int edonr_Final(edonr_hashState *state, unsigned char *hashval)
{
	uint32_t *data32, *p256 = NULL;
	uint32_t t0,  t1,  t2,  t3,  t4,  t5,  t6,  t7;
	uint32_t t8,  t9, t10, t11, t12, t13, t14, t15;

	uint64_t *data64, *p512 = NULL;
	uint64_t tt0,  tt1,  tt2,  tt3,  tt4,  tt5,  tt6,  tt7; 
	uint64_t tt8,  tt9, tt10, tt11, tt12, tt13, tt14, tt15; 

	unsigned long long databitlen;

	int LastByte, PadOnePosition;

	switch(state->hashbitlen)
	{
		case 224:
		case 256:
			LastByte = (int)state->unprocessed_bits >> 3;
			PadOnePosition = 7 - (state->unprocessed_bits & 0x07);
			hashState256(state)->LastPart[LastByte] = hashState256(state)->LastPart[LastByte] & (0xff << (PadOnePosition + 1) )\
				                                    ^ (0x01 << PadOnePosition);
			data64 = (uint64_t *)hashState256(state)->LastPart;

			if (state->unprocessed_bits < 448)
			{
				memset( (hashState256(state)->LastPart) + LastByte + 1, 0x00, EdonR256_BLOCK_SIZE - LastByte - 9 );
				databitlen = EdonR256_BLOCK_SIZE * 8;
				data64[7] = state->bits_processed + state->unprocessed_bits;
			}
			else
			{
				memset( (hashState256(state)->LastPart) + LastByte + 1, 0x00, EdonR256_BLOCK_SIZE * 2 - LastByte - 9 );
				databitlen = EdonR256_BLOCK_SIZE * 16;
				data64[15] = state->bits_processed + state->unprocessed_bits;
			}

			data32   = (uint32_t *)hashState256(state)->LastPart;
			p256     = hashState256(state)->DoublePipe;
			while (databitlen >= EdonR256_BLOCK_SIZE * 8)
			{
				databitlen -= EdonR256_BLOCK_SIZE * 8;

				/* First row of quasigroup e-transformations */
				Q256( data32[15],  data32[14],  data32[13],  data32[12],  data32[11],  data32[10],  data32[ 9],  data32[ 8],
				      data32[ 0],  data32[ 1],  data32[ 2],  data32[ 3],  data32[ 4],  data32[ 5],  data32[ 6],  data32[ 7],
				        p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23]);
				Q256(   p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23],
				      data32[ 8],  data32[ 9],  data32[10],  data32[11],  data32[12],  data32[13],  data32[14],  data32[15],
				        p256[24],    p256[25],    p256[26],    p256[27],    p256[28],    p256[29],    p256[30],    p256[31]);

				/* Second row of quasigroup e-transformations */
				Q256(   p256[ 8],    p256[ 9],    p256[10],    p256[11],    p256[12],    p256[13],    p256[14],    p256[15],
				        p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23],
				        p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23]);
				Q256(   p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23],
					    p256[24],    p256[25],    p256[26],    p256[27],    p256[28],    p256[29],    p256[30],    p256[31],
				        p256[24],    p256[25],    p256[26],    p256[27],    p256[28],    p256[29],    p256[30],    p256[31]);

				/* Third row of quasigroup e-transformations */
				Q256(   p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23],
					    p256[ 0],    p256[ 1],    p256[ 2],    p256[ 3],    p256[ 4],    p256[ 5],    p256[ 6],    p256[ 7],
				        p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23]);
				Q256(   p256[24],    p256[25],    p256[26],    p256[27],    p256[28],    p256[29],    p256[30],    p256[31],
				        p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23],
				        p256[24],    p256[25],    p256[26],    p256[27],    p256[28],    p256[29],    p256[30],    p256[31]);

				/* Fourth row of quasigroup e-transformations */
				Q256( data32[ 7],  data32[ 6],  data32[ 5],  data32[ 4],  data32[ 3],  data32[ 2],  data32[ 1],  data32[ 0],
				        p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23],
				        p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23]);
				Q256(   p256[16],    p256[17],    p256[18],    p256[19],    p256[20],    p256[21],    p256[22],    p256[23],
				        p256[24],    p256[25],    p256[26],    p256[27],    p256[28],    p256[29],    p256[30],    p256[31],
				        p256[24],    p256[25],    p256[26],    p256[27],    p256[28],    p256[29],    p256[30],    p256[31]);

				/* This is the proposed tweak on original SHA-3 Edon-R submission.  */
				/* Instead of the old compression function R(oldPipe, M), now the   */
				/* compression function is: R(oldPipe, M) xor oldPipe xor M'        */
				/* where M is represented in two parts i.e. M = (M0, M1), and       */
				/* M' = (M1, M0).                                                   */
				p256[ 0]^=(data32[ 8] ^ p256[16]);
				p256[ 1]^=(data32[ 9] ^ p256[17]);
				p256[ 2]^=(data32[10] ^ p256[18]);
				p256[ 3]^=(data32[11] ^ p256[19]);
				p256[ 4]^=(data32[12] ^ p256[20]);
				p256[ 5]^=(data32[13] ^ p256[21]);
				p256[ 6]^=(data32[14] ^ p256[22]);
				p256[ 7]^=(data32[15] ^ p256[23]);
				p256[ 8]^=(data32[ 0] ^ p256[24]);
				p256[ 9]^=(data32[ 1] ^ p256[25]);
				p256[10]^=(data32[ 2] ^ p256[26]);
				p256[11]^=(data32[ 3] ^ p256[27]);
				p256[12]^=(data32[ 4] ^ p256[28]);
				p256[13]^=(data32[ 5] ^ p256[29]);
				p256[14]^=(data32[ 6] ^ p256[30]);
				p256[15]^=(data32[ 7] ^ p256[31]);


				data32 += 16;
			}
			break;

		case 384:
		case 512:
			LastByte = (int)state->unprocessed_bits >> 3;
			PadOnePosition = 7 - (state->unprocessed_bits & 0x07);
			hashState512(state)->LastPart[LastByte] = hashState512(state)->LastPart[LastByte] & (0xff << (PadOnePosition + 1) )\
				                                    ^ (0x01 << PadOnePosition);
			data64 = (uint64_t *)hashState512(state)->LastPart;

			if (state->unprocessed_bits < 960)
			{
				memset( (hashState512(state)->LastPart) + LastByte + 1, 0x00, EdonR512_BLOCK_SIZE - LastByte - 9 );
				databitlen = EdonR512_BLOCK_SIZE * 8;
				data64[15] = state->bits_processed + state->unprocessed_bits;
			}
			else
			{
				memset( (hashState512(state)->LastPart) + LastByte + 1, 0x00, EdonR512_BLOCK_SIZE * 2 - LastByte - 9 );
				databitlen = EdonR512_BLOCK_SIZE * 16;
				data64[31] = state->bits_processed + state->unprocessed_bits;
			}

			p512   = hashState512(state)->DoublePipe;
			while (databitlen >= EdonR512_BLOCK_SIZE * 8)
			{
				databitlen -= EdonR512_BLOCK_SIZE * 8;

				/* First row of quasigroup e-transformations */
				Q512( data64[15],  data64[14],  data64[13],  data64[12],  data64[11],  data64[10],  data64[ 9],  data64[ 8],
				      data64[ 0],  data64[ 1],  data64[ 2],  data64[ 3],  data64[ 4],  data64[ 5],  data64[ 6],  data64[ 7],
				        p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23]);
				Q512(   p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23],
				      data64[ 8],  data64[ 9],  data64[10],  data64[11],  data64[12],  data64[13],  data64[14],  data64[15],
				        p512[24],    p512[25],    p512[26],    p512[27],    p512[28],    p512[29],    p512[30],    p512[31]);

				/* Second row of quasigroup e-transformations */
				Q512(   p512[ 8],    p512[ 9],    p512[10],    p512[11],    p512[12],    p512[13],    p512[14],    p512[15],
				        p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23],
				        p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23]);
				Q512(   p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23],
					    p512[24],    p512[25],    p512[26],    p512[27],    p512[28],    p512[29],    p512[30],    p512[31],
				        p512[24],    p512[25],    p512[26],    p512[27],    p512[28],    p512[29],    p512[30],    p512[31]);

				/* Third row of quasigroup e-transformations */
				Q512(   p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23],
					    p512[ 0],    p512[ 1],    p512[ 2],    p512[ 3],    p512[ 4],    p512[ 5],    p512[ 6],    p512[ 7],
				        p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23]);
				Q512(   p512[24],    p512[25],    p512[26],    p512[27],    p512[28],    p512[29],    p512[30],    p512[31],
				        p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23],
				        p512[24],    p512[25],    p512[26],    p512[27],    p512[28],    p512[29],    p512[30],    p512[31]);

				/* Fourth row of quasigroup e-transformations */
				Q512( data64[ 7],  data64[ 6],  data64[ 5],  data64[ 4],  data64[ 3],  data64[ 2],  data64[ 1],  data64[ 0],
				        p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23],
				        p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23]);
				Q512(   p512[16],    p512[17],    p512[18],    p512[19],    p512[20],    p512[21],    p512[22],    p512[23],
				        p512[24],    p512[25],    p512[26],    p512[27],    p512[28],    p512[29],    p512[30],    p512[31],
				        p512[24],    p512[25],    p512[26],    p512[27],    p512[28],    p512[29],    p512[30],    p512[31]);

				/* This is the proposed tweak on original SHA-3 Edon-R submission.  */
				/* Instead of the old compression function R(oldPipe, M), now the   */
				/* compression function is: R(oldPipe, M) xor oldPipe xor M'        */
				/* where M is represented in two parts i.e. M = (M0, M1), and       */
				/* M' = (M1, M0).                                                   */
				p512[ 0]^=(data64[ 8] ^ p512[16]);
				p512[ 1]^=(data64[ 9] ^ p512[17]);
				p512[ 2]^=(data64[10] ^ p512[18]);
				p512[ 3]^=(data64[11] ^ p512[19]);
				p512[ 4]^=(data64[12] ^ p512[20]);
				p512[ 5]^=(data64[13] ^ p512[21]);
				p512[ 6]^=(data64[14] ^ p512[22]);
				p512[ 7]^=(data64[15] ^ p512[23]);
				p512[ 8]^=(data64[ 0] ^ p512[24]);
				p512[ 9]^=(data64[ 1] ^ p512[25]);
				p512[10]^=(data64[ 2] ^ p512[26]);
				p512[11]^=(data64[ 3] ^ p512[27]);
				p512[12]^=(data64[ 4] ^ p512[28]);
				p512[13]^=(data64[ 5] ^ p512[29]);
				p512[14]^=(data64[ 6] ^ p512[30]);
				p512[15]^=(data64[ 7] ^ p512[31]);

				data64 += 16;
			}
			break;
		
		default:    return(BAD_HASHLEN); //This should never happen
	}


	switch(state->hashbitlen)
	{
		case 224:
			memcpy(hashval, p256 + 9, EdonR224_DIGEST_SIZE );
			return(SUCCESS);
		case 256:
			memcpy(hashval, p256 + 8, EdonR256_DIGEST_SIZE );
			return(SUCCESS);
		case 384:
			memcpy(hashval, p512 + 10, EdonR384_DIGEST_SIZE );
			return(SUCCESS);
		case 512:
			memcpy(hashval, p512 + 8,  EdonR512_DIGEST_SIZE );
			return(SUCCESS);
		default:    return(BAD_HASHLEN); //This should never happen
	}
}

int edonr_Hash(int hashbitlen, const void *_data, size_t _len, unsigned char *hashval)
{
	int qq;
	edonr_hashState state;

	qq = edonr_Init(&state, hashbitlen);
	if (qq != SUCCESS) return(qq);
	qq = edonr_Update(&state, _data, _len);
	if (qq != SUCCESS) return(qq);
	qq = edonr_Final(&state, hashval);
	return(qq);
}
