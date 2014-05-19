/***********************************************************************
**
** Implementation of the Skein hash function.
**
** Source code author: Doug Whiting, 2008.
**
** This algorithm and source code is released to the public domain.
**
************************************************************************/

#define  SKEIN_PORT_CODE /* instantiate any code in skein_port.h */

#include <linux/string.h>       /* get the memcpy/memset functions */
#include <skein.h> /* get the Skein API definitions   */
#include <skein_iv.h>    /* get precomputed IVs */
#include <skein_block.h>

/*****************************************************************/
/*     256-bit Skein                                             */
/*****************************************************************/

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a straight hashing operation  */
int skein_256_init(struct skein_256_ctx *ctx, size_t hash_bit_len)
{
	union {
		u8 b[SKEIN_256_STATE_BYTES];
		u64 w[SKEIN_256_STATE_WORDS];
	} cfg;                              /* config block */

	Skein_Assert(hash_bit_len > 0, SKEIN_BAD_HASHLEN);
	ctx->h.hash_bit_len = hash_bit_len;         /* output hash bit count */

	switch (hash_bit_len) { /* use pre-computed values, where available */
	case  256:
		memcpy(ctx->X, SKEIN_256_IV_256, sizeof(ctx->X));
		break;
	case  224:
		memcpy(ctx->X, SKEIN_256_IV_224, sizeof(ctx->X));
		break;
	case  160:
		memcpy(ctx->X, SKEIN_256_IV_160, sizeof(ctx->X));
		break;
	case  128:
		memcpy(ctx->X, SKEIN_256_IV_128, sizeof(ctx->X));
		break;
	default:
		/* here if there is no precomputed IV value available */
		/*
		 * build/process the config block, type == CONFIG (could be
		 * precomputed)
		 */
		/* set tweaks: T0=0; T1=CFG | FINAL */
		Skein_Start_New_Type(ctx, CFG_FINAL);

		/* set the schema, version */
		cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
		/* hash result length in bits */
		cfg.w[1] = Skein_Swap64(hash_bit_len);
		cfg.w[2] = Skein_Swap64(SKEIN_CFG_TREE_INFO_SEQUENTIAL);
		/* zero pad config block */
		memset(&cfg.w[3], 0, sizeof(cfg) - 3*sizeof(cfg.w[0]));

		/* compute the initial chaining values from config block */
		/* zero the chaining variables */
		memset(ctx->X, 0, sizeof(ctx->X));
		skein_256_process_block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);
		break;
	}
	/* The chaining vars ctx->X are now initialized for hash_bit_len. */
	/* Set up to process the data message portion of the hash (default) */
	Skein_Start_New_Type(ctx, MSG);              /* T0=0, T1= MSG type */

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a MAC and/or tree hash operation */
/* [identical to skein_256_init() when key_bytes == 0 && \
 *	tree_info == SKEIN_CFG_TREE_INFO_SEQUENTIAL] */
int skein_256_init_ext(struct skein_256_ctx *ctx, size_t hash_bit_len,
		       u64 tree_info, const u8 *key, size_t key_bytes)
{
	union {
		u8  b[SKEIN_256_STATE_BYTES];
		u64 w[SKEIN_256_STATE_WORDS];
	} cfg; /* config block */

	Skein_Assert(hash_bit_len > 0, SKEIN_BAD_HASHLEN);
	Skein_Assert(key_bytes == 0 || key != NULL, SKEIN_FAIL);

	/* compute the initial chaining values ctx->X[], based on key */
	if (key_bytes == 0) { /* is there a key? */
		/* no key: use all zeroes as key for config block */
		memset(ctx->X, 0, sizeof(ctx->X));
	} else { /* here to pre-process a key */
		Skein_assert(sizeof(cfg.b) >= sizeof(ctx->X));
		/* do a mini-Init right here */
		/* set output hash bit count = state size */
		ctx->h.hash_bit_len = 8*sizeof(ctx->X);
		/* set tweaks: T0 = 0; T1 = KEY type */
		Skein_Start_New_Type(ctx, KEY);
		/* zero the initial chaining variables */
		memset(ctx->X, 0, sizeof(ctx->X));
		/* hash the key */
		skein_256_update(ctx, key, key_bytes);
		/* put result into cfg.b[] */
		skein_256_final_pad(ctx, cfg.b);
		/* copy over into ctx->X[] */
		memcpy(ctx->X, cfg.b, sizeof(cfg.b));
	}
	/*
	 * build/process the config block, type == CONFIG (could be
	 * precomputed for each key)
	 */
	/* output hash bit count */
	ctx->h.hash_bit_len = hash_bit_len;
	Skein_Start_New_Type(ctx, CFG_FINAL);

	/* pre-pad cfg.w[] with zeroes */
	memset(&cfg.w, 0, sizeof(cfg.w));
	cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
	/* hash result length in bits */
	cfg.w[1] = Skein_Swap64(hash_bit_len);
	/* tree hash config info (or SKEIN_CFG_TREE_INFO_SEQUENTIAL) */
	cfg.w[2] = Skein_Swap64(tree_info);

	Skein_Show_Key(256, &ctx->h, key, key_bytes);

	/* compute the initial chaining values from config block */
	skein_256_process_block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);

	/* The chaining vars ctx->X are now initialized */
	/* Set up to process the data message portion of the hash (default) */
	Skein_Start_New_Type(ctx, MSG);

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* process the input bytes */
int skein_256_update(struct skein_256_ctx *ctx, const u8 *msg,
		     size_t msg_byte_cnt)
{
	size_t n;

	/* catch uninitialized context */
	Skein_Assert(ctx->h.b_cnt <= SKEIN_256_BLOCK_BYTES, SKEIN_FAIL);

	/* process full blocks, if any */
	if (msg_byte_cnt + ctx->h.b_cnt > SKEIN_256_BLOCK_BYTES) {
		/* finish up any buffered message data */
		if (ctx->h.b_cnt) {
			/* # bytes free in buffer b[] */
			n = SKEIN_256_BLOCK_BYTES - ctx->h.b_cnt;
			if (n) {
				/* check on our logic here */
				Skein_assert(n < msg_byte_cnt);
				memcpy(&ctx->b[ctx->h.b_cnt], msg, n);
				msg_byte_cnt  -= n;
				msg         += n;
				ctx->h.b_cnt += n;
			}
			Skein_assert(ctx->h.b_cnt == SKEIN_256_BLOCK_BYTES);
			skein_256_process_block(ctx, ctx->b, 1,
						SKEIN_256_BLOCK_BYTES);
			ctx->h.b_cnt = 0;
		}
		/*
		 * now process any remaining full blocks, directly from input
		 * message data
		 */
		if (msg_byte_cnt > SKEIN_256_BLOCK_BYTES) {
			/* number of full blocks to process */
			n = (msg_byte_cnt-1) / SKEIN_256_BLOCK_BYTES;
			skein_256_process_block(ctx, msg, n,
						SKEIN_256_BLOCK_BYTES);
			msg_byte_cnt -= n * SKEIN_256_BLOCK_BYTES;
			msg        += n * SKEIN_256_BLOCK_BYTES;
		}
		Skein_assert(ctx->h.b_cnt == 0);
	}

	/* copy any remaining source message data bytes into b[] */
	if (msg_byte_cnt) {
		Skein_assert(msg_byte_cnt + ctx->h.b_cnt <=
			     SKEIN_256_BLOCK_BYTES);
		memcpy(&ctx->b[ctx->h.b_cnt], msg, msg_byte_cnt);
		ctx->h.b_cnt += msg_byte_cnt;
	}

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the result */
int skein_256_final(struct skein_256_ctx *ctx, u8 *hash_val)
{
	size_t i, n, byte_cnt;
	u64 X[SKEIN_256_STATE_WORDS];
	/* catch uninitialized context */
	Skein_Assert(ctx->h.b_cnt <= SKEIN_256_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.b_cnt < SKEIN_256_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.b_cnt], 0,
			SKEIN_256_BLOCK_BYTES - ctx->h.b_cnt);

	/* process the final block */
	skein_256_process_block(ctx, ctx->b, 1, ctx->h.b_cnt);

	/* now output the result */
	/* total number of output bytes */
	byte_cnt = (ctx->h.hash_bit_len + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof(ctx->b));
	/* keep a local copy of counter mode "key" */
	memcpy(X, ctx->X, sizeof(X));
	for (i = 0; i*SKEIN_256_BLOCK_BYTES < byte_cnt; i++) {
		/* build the counter block */
		((u64 *)ctx->b)[0] = Skein_Swap64((u64) i);
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		skein_256_process_block(ctx, ctx->b, 1, sizeof(u64));
		/* number of output bytes left to go */
		n = byte_cnt - i*SKEIN_256_BLOCK_BYTES;
		if (n >= SKEIN_256_BLOCK_BYTES)
			n  = SKEIN_256_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		Skein_Put64_LSB_First(hash_val+i*SKEIN_256_BLOCK_BYTES, ctx->X,
				      n);
		Skein_Show_Final(256, &ctx->h, n,
				 hash_val+i*SKEIN_256_BLOCK_BYTES);
		/* restore the counter mode key for next time */
		memcpy(ctx->X, X, sizeof(X));
	}
	return SKEIN_SUCCESS;
}

/*****************************************************************/
/*     512-bit Skein                                             */
/*****************************************************************/

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a straight hashing operation  */
int skein_512_init(struct skein_512_ctx *ctx, size_t hash_bit_len)
{
	union {
		u8 b[SKEIN_512_STATE_BYTES];
		u64 w[SKEIN_512_STATE_WORDS];
	} cfg;                              /* config block */

	Skein_Assert(hash_bit_len > 0, SKEIN_BAD_HASHLEN);
	ctx->h.hash_bit_len = hash_bit_len;         /* output hash bit count */

	switch (hash_bit_len) { /* use pre-computed values, where available */
	case  512:
		memcpy(ctx->X, SKEIN_512_IV_512, sizeof(ctx->X));
		break;
	case  384:
		memcpy(ctx->X, SKEIN_512_IV_384, sizeof(ctx->X));
		break;
	case  256:
		memcpy(ctx->X, SKEIN_512_IV_256, sizeof(ctx->X));
		break;
	case  224:
		memcpy(ctx->X, SKEIN_512_IV_224, sizeof(ctx->X));
		break;
	default:
		/* here if there is no precomputed IV value available */
		/*
		 * build/process the config block, type == CONFIG (could be
		 * precomputed)
		 */
		/* set tweaks: T0=0; T1=CFG | FINAL */
		Skein_Start_New_Type(ctx, CFG_FINAL);

		/* set the schema, version */
		cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
		/* hash result length in bits */
		cfg.w[1] = Skein_Swap64(hash_bit_len);
		cfg.w[2] = Skein_Swap64(SKEIN_CFG_TREE_INFO_SEQUENTIAL);
		/* zero pad config block */
		memset(&cfg.w[3], 0, sizeof(cfg) - 3*sizeof(cfg.w[0]));

		/* compute the initial chaining values from config block */
		/* zero the chaining variables */
		memset(ctx->X, 0, sizeof(ctx->X));
		skein_512_process_block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);
		break;
	}

	/*
	 * The chaining vars ctx->X are now initialized for the given
	 * hash_bit_len.
	 */
	/* Set up to process the data message portion of the hash (default) */
	Skein_Start_New_Type(ctx, MSG);              /* T0=0, T1= MSG type */

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a MAC and/or tree hash operation */
/* [identical to skein_512_init() when key_bytes == 0 && \
 *	tree_info == SKEIN_CFG_TREE_INFO_SEQUENTIAL] */
int skein_512_init_ext(struct skein_512_ctx *ctx, size_t hash_bit_len,
		       u64 tree_info, const u8 *key, size_t key_bytes)
{
	union {
		u8 b[SKEIN_512_STATE_BYTES];
		u64 w[SKEIN_512_STATE_WORDS];
	} cfg;                              /* config block */

	Skein_Assert(hash_bit_len > 0, SKEIN_BAD_HASHLEN);
	Skein_Assert(key_bytes == 0 || key != NULL, SKEIN_FAIL);

	/* compute the initial chaining values ctx->X[], based on key */
	if (key_bytes == 0) { /* is there a key? */
		/* no key: use all zeroes as key for config block */
		memset(ctx->X, 0, sizeof(ctx->X));
	} else { /* here to pre-process a key */
		Skein_assert(sizeof(cfg.b) >= sizeof(ctx->X));
		/* do a mini-Init right here */
		/* set output hash bit count = state size */
		ctx->h.hash_bit_len = 8*sizeof(ctx->X);
		/* set tweaks: T0 = 0; T1 = KEY type */
		Skein_Start_New_Type(ctx, KEY);
		/* zero the initial chaining variables */
		memset(ctx->X, 0, sizeof(ctx->X));
		/* hash the key */
		skein_512_update(ctx, key, key_bytes);
		/* put result into cfg.b[] */
		skein_512_final_pad(ctx, cfg.b);
		/* copy over into ctx->X[] */
		memcpy(ctx->X, cfg.b, sizeof(cfg.b));
	}
	/*
	 * build/process the config block, type == CONFIG (could be
	 * precomputed for each key)
	 */
	ctx->h.hash_bit_len = hash_bit_len;          /* output hash bit count */
	Skein_Start_New_Type(ctx, CFG_FINAL);

	/* pre-pad cfg.w[] with zeroes */
	memset(&cfg.w, 0, sizeof(cfg.w));
	cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
	/* hash result length in bits */
	cfg.w[1] = Skein_Swap64(hash_bit_len);
	/* tree hash config info (or SKEIN_CFG_TREE_INFO_SEQUENTIAL) */
	cfg.w[2] = Skein_Swap64(tree_info);

	Skein_Show_Key(512, &ctx->h, key, key_bytes);

	/* compute the initial chaining values from config block */
	skein_512_process_block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);

	/* The chaining vars ctx->X are now initialized */
	/* Set up to process the data message portion of the hash (default) */
	Skein_Start_New_Type(ctx, MSG);

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* process the input bytes */
int skein_512_update(struct skein_512_ctx *ctx, const u8 *msg,
		     size_t msg_byte_cnt)
{
	size_t n;

	/* catch uninitialized context */
	Skein_Assert(ctx->h.b_cnt <= SKEIN_512_BLOCK_BYTES, SKEIN_FAIL);

	/* process full blocks, if any */
	if (msg_byte_cnt + ctx->h.b_cnt > SKEIN_512_BLOCK_BYTES) {
		/* finish up any buffered message data */
		if (ctx->h.b_cnt) {
			/* # bytes free in buffer b[] */
			n = SKEIN_512_BLOCK_BYTES - ctx->h.b_cnt;
			if (n) {
				/* check on our logic here */
				Skein_assert(n < msg_byte_cnt);
				memcpy(&ctx->b[ctx->h.b_cnt], msg, n);
				msg_byte_cnt  -= n;
				msg         += n;
				ctx->h.b_cnt += n;
			}
			Skein_assert(ctx->h.b_cnt == SKEIN_512_BLOCK_BYTES);
			skein_512_process_block(ctx, ctx->b, 1,
						SKEIN_512_BLOCK_BYTES);
			ctx->h.b_cnt = 0;
		}
		/*
		 * now process any remaining full blocks, directly from input
		 * message data
		 */
		if (msg_byte_cnt > SKEIN_512_BLOCK_BYTES) {
			/* number of full blocks to process */
			n = (msg_byte_cnt-1) / SKEIN_512_BLOCK_BYTES;
			skein_512_process_block(ctx, msg, n,
						SKEIN_512_BLOCK_BYTES);
			msg_byte_cnt -= n * SKEIN_512_BLOCK_BYTES;
			msg        += n * SKEIN_512_BLOCK_BYTES;
		}
		Skein_assert(ctx->h.b_cnt == 0);
	}

	/* copy any remaining source message data bytes into b[] */
	if (msg_byte_cnt) {
		Skein_assert(msg_byte_cnt + ctx->h.b_cnt <=
			     SKEIN_512_BLOCK_BYTES);
		memcpy(&ctx->b[ctx->h.b_cnt], msg, msg_byte_cnt);
		ctx->h.b_cnt += msg_byte_cnt;
	}

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the result */
int skein_512_final(struct skein_512_ctx *ctx, u8 *hash_val)
{
	size_t i, n, byte_cnt;
	u64 X[SKEIN_512_STATE_WORDS];
	/* catch uninitialized context */
	Skein_Assert(ctx->h.b_cnt <= SKEIN_512_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.b_cnt < SKEIN_512_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.b_cnt], 0,
			SKEIN_512_BLOCK_BYTES - ctx->h.b_cnt);

	/* process the final block */
	skein_512_process_block(ctx, ctx->b, 1, ctx->h.b_cnt);

	/* now output the result */
	/* total number of output bytes */
	byte_cnt = (ctx->h.hash_bit_len + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof(ctx->b));
	/* keep a local copy of counter mode "key" */
	memcpy(X, ctx->X, sizeof(X));
	for (i = 0; i*SKEIN_512_BLOCK_BYTES < byte_cnt; i++) {
		/* build the counter block */
		((u64 *)ctx->b)[0] = Skein_Swap64((u64) i);
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		skein_512_process_block(ctx, ctx->b, 1, sizeof(u64));
		/* number of output bytes left to go */
		n = byte_cnt - i*SKEIN_512_BLOCK_BYTES;
		if (n >= SKEIN_512_BLOCK_BYTES)
			n  = SKEIN_512_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		Skein_Put64_LSB_First(hash_val+i*SKEIN_512_BLOCK_BYTES, ctx->X,
				      n);
		Skein_Show_Final(512, &ctx->h, n,
				 hash_val+i*SKEIN_512_BLOCK_BYTES);
		/* restore the counter mode key for next time */
		memcpy(ctx->X, X, sizeof(X));
	}
	return SKEIN_SUCCESS;
}

/*****************************************************************/
/*    1024-bit Skein                                             */
/*****************************************************************/

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a straight hashing operation  */
int skein_1024_init(struct skein_1024_ctx *ctx, size_t hash_bit_len)
{
	union {
		u8 b[SKEIN1024_STATE_BYTES];
		u64 w[SKEIN1024_STATE_WORDS];
	} cfg;                              /* config block */

	Skein_Assert(hash_bit_len > 0, SKEIN_BAD_HASHLEN);
	ctx->h.hash_bit_len = hash_bit_len;         /* output hash bit count */

	switch (hash_bit_len) { /* use pre-computed values, where available */
	case  512:
		memcpy(ctx->X, SKEIN1024_IV_512, sizeof(ctx->X));
		break;
	case  384:
		memcpy(ctx->X, SKEIN1024_IV_384, sizeof(ctx->X));
		break;
	case 1024:
		memcpy(ctx->X, SKEIN1024_IV_1024, sizeof(ctx->X));
		break;
	default:
		/* here if there is no precomputed IV value available */
		/*
		 * build/process the config block, type == CONFIG
		 * (could be precomputed)
		 */
		/* set tweaks: T0=0; T1=CFG | FINAL */
		Skein_Start_New_Type(ctx, CFG_FINAL);

		/* set the schema, version */
		cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
		/* hash result length in bits */
		cfg.w[1] = Skein_Swap64(hash_bit_len);
		cfg.w[2] = Skein_Swap64(SKEIN_CFG_TREE_INFO_SEQUENTIAL);
		/* zero pad config block */
		memset(&cfg.w[3], 0, sizeof(cfg) - 3*sizeof(cfg.w[0]));

		/* compute the initial chaining values from config block */
		/* zero the chaining variables */
		memset(ctx->X, 0, sizeof(ctx->X));
		skein_1024_process_block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);
		break;
	}

	/* The chaining vars ctx->X are now initialized for the hash_bit_len. */
	/* Set up to process the data message portion of the hash (default) */
	Skein_Start_New_Type(ctx, MSG);              /* T0=0, T1= MSG type */

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a MAC and/or tree hash operation */
/* [identical to skein_1024_init() when key_bytes == 0 && \
 *	tree_info == SKEIN_CFG_TREE_INFO_SEQUENTIAL] */
int skein_1024_init_ext(struct skein_1024_ctx *ctx, size_t hash_bit_len,
			u64 tree_info, const u8 *key, size_t key_bytes)
{
	union {
		u8 b[SKEIN1024_STATE_BYTES];
		u64 w[SKEIN1024_STATE_WORDS];
	} cfg;                              /* config block */

	Skein_Assert(hash_bit_len > 0, SKEIN_BAD_HASHLEN);
	Skein_Assert(key_bytes == 0 || key != NULL, SKEIN_FAIL);

	/* compute the initial chaining values ctx->X[], based on key */
	if (key_bytes == 0) { /* is there a key? */
		/* no key: use all zeroes as key for config block */
		memset(ctx->X, 0, sizeof(ctx->X));
	} else { /* here to pre-process a key */
		Skein_assert(sizeof(cfg.b) >= sizeof(ctx->X));
		/* do a mini-Init right here */
		/* set output hash bit count = state size */
		ctx->h.hash_bit_len = 8*sizeof(ctx->X);
		/* set tweaks: T0 = 0; T1 = KEY type */
		Skein_Start_New_Type(ctx, KEY);
		/* zero the initial chaining variables */
		memset(ctx->X, 0, sizeof(ctx->X));
		/* hash the key */
		skein_1024_update(ctx, key, key_bytes);
		/* put result into cfg.b[] */
		skein_1024_final_pad(ctx, cfg.b);
		/* copy over into ctx->X[] */
		memcpy(ctx->X, cfg.b, sizeof(cfg.b));
	}
	/*
	 * build/process the config block, type == CONFIG (could be
	 * precomputed for each key)
	 */
	/* output hash bit count */
	ctx->h.hash_bit_len = hash_bit_len;
	Skein_Start_New_Type(ctx, CFG_FINAL);

	/* pre-pad cfg.w[] with zeroes */
	memset(&cfg.w, 0, sizeof(cfg.w));
	cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
	/* hash result length in bits */
	cfg.w[1] = Skein_Swap64(hash_bit_len);
	/* tree hash config info (or SKEIN_CFG_TREE_INFO_SEQUENTIAL) */
	cfg.w[2] = Skein_Swap64(tree_info);

	Skein_Show_Key(1024, &ctx->h, key, key_bytes);

	/* compute the initial chaining values from config block */
	skein_1024_process_block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);

	/* The chaining vars ctx->X are now initialized */
	/* Set up to process the data message portion of the hash (default) */
	Skein_Start_New_Type(ctx, MSG);

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* process the input bytes */
int skein_1024_update(struct skein_1024_ctx *ctx, const u8 *msg,
		      size_t msg_byte_cnt)
{
	size_t n;

	/* catch uninitialized context */
	Skein_Assert(ctx->h.b_cnt <= SKEIN1024_BLOCK_BYTES, SKEIN_FAIL);

	/* process full blocks, if any */
	if (msg_byte_cnt + ctx->h.b_cnt > SKEIN1024_BLOCK_BYTES) {
		/* finish up any buffered message data */
		if (ctx->h.b_cnt) {
			/* # bytes free in buffer b[] */
			n = SKEIN1024_BLOCK_BYTES - ctx->h.b_cnt;
			if (n) {
				/* check on our logic here */
				Skein_assert(n < msg_byte_cnt);
				memcpy(&ctx->b[ctx->h.b_cnt], msg, n);
				msg_byte_cnt  -= n;
				msg         += n;
				ctx->h.b_cnt += n;
			}
			Skein_assert(ctx->h.b_cnt == SKEIN1024_BLOCK_BYTES);
			skein_1024_process_block(ctx, ctx->b, 1,
						 SKEIN1024_BLOCK_BYTES);
			ctx->h.b_cnt = 0;
		}
		/*
		 * now process any remaining full blocks, directly from input
		 * message data
		 */
		if (msg_byte_cnt > SKEIN1024_BLOCK_BYTES) {
			/* number of full blocks to process */
			n = (msg_byte_cnt-1) / SKEIN1024_BLOCK_BYTES;
			skein_1024_process_block(ctx, msg, n,
						 SKEIN1024_BLOCK_BYTES);
			msg_byte_cnt -= n * SKEIN1024_BLOCK_BYTES;
			msg        += n * SKEIN1024_BLOCK_BYTES;
		}
		Skein_assert(ctx->h.b_cnt == 0);
	}

	/* copy any remaining source message data bytes into b[] */
	if (msg_byte_cnt) {
		Skein_assert(msg_byte_cnt + ctx->h.b_cnt <=
			     SKEIN1024_BLOCK_BYTES);
		memcpy(&ctx->b[ctx->h.b_cnt], msg, msg_byte_cnt);
		ctx->h.b_cnt += msg_byte_cnt;
	}

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the result */
int skein_1024_final(struct skein_1024_ctx *ctx, u8 *hash_val)
{
	size_t i, n, byte_cnt;
	u64 X[SKEIN1024_STATE_WORDS];
	/* catch uninitialized context */
	Skein_Assert(ctx->h.b_cnt <= SKEIN1024_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.b_cnt < SKEIN1024_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.b_cnt], 0,
			SKEIN1024_BLOCK_BYTES - ctx->h.b_cnt);

	/* process the final block */
	skein_1024_process_block(ctx, ctx->b, 1, ctx->h.b_cnt);

	/* now output the result */
	/* total number of output bytes */
	byte_cnt = (ctx->h.hash_bit_len + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof(ctx->b));
	/* keep a local copy of counter mode "key" */
	memcpy(X, ctx->X, sizeof(X));
	for (i = 0; i*SKEIN1024_BLOCK_BYTES < byte_cnt; i++) {
		/* build the counter block */
		((u64 *)ctx->b)[0] = Skein_Swap64((u64) i);
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		skein_1024_process_block(ctx, ctx->b, 1, sizeof(u64));
		/* number of output bytes left to go */
		n = byte_cnt - i*SKEIN1024_BLOCK_BYTES;
		if (n >= SKEIN1024_BLOCK_BYTES)
			n  = SKEIN1024_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		Skein_Put64_LSB_First(hash_val+i*SKEIN1024_BLOCK_BYTES, ctx->X,
				      n);
		Skein_Show_Final(1024, &ctx->h, n,
				 hash_val+i*SKEIN1024_BLOCK_BYTES);
		/* restore the counter mode key for next time */
		memcpy(ctx->X, X, sizeof(X));
	}
	return SKEIN_SUCCESS;
}

/**************** Functions to support MAC/tree hashing ***************/
/*   (this code is identical for Optimized and Reference versions)    */

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the block, no OUTPUT stage */
int skein_256_final_pad(struct skein_256_ctx *ctx, u8 *hash_val)
{
	/* catch uninitialized context */
	Skein_Assert(ctx->h.b_cnt <= SKEIN_256_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.b_cnt < SKEIN_256_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.b_cnt], 0,
			SKEIN_256_BLOCK_BYTES - ctx->h.b_cnt);
	/* process the final block */
	skein_256_process_block(ctx, ctx->b, 1, ctx->h.b_cnt);

	/* "output" the state bytes */
	Skein_Put64_LSB_First(hash_val, ctx->X, SKEIN_256_BLOCK_BYTES);

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the block, no OUTPUT stage */
int skein_512_final_pad(struct skein_512_ctx *ctx, u8 *hash_val)
{
	/* catch uninitialized context */
	Skein_Assert(ctx->h.b_cnt <= SKEIN_512_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.b_cnt < SKEIN_512_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.b_cnt], 0,
			SKEIN_512_BLOCK_BYTES - ctx->h.b_cnt);
	/* process the final block */
	skein_512_process_block(ctx, ctx->b, 1, ctx->h.b_cnt);

	/* "output" the state bytes */
	Skein_Put64_LSB_First(hash_val, ctx->X, SKEIN_512_BLOCK_BYTES);

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the block, no OUTPUT stage */
int skein_1024_final_pad(struct skein_1024_ctx *ctx, u8 *hash_val)
{
	/* catch uninitialized context */
	Skein_Assert(ctx->h.b_cnt <= SKEIN1024_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.b_cnt < SKEIN1024_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.b_cnt], 0,
			SKEIN1024_BLOCK_BYTES - ctx->h.b_cnt);
	/* process the final block */
	skein_1024_process_block(ctx, ctx->b, 1, ctx->h.b_cnt);

	/* "output" the state bytes */
	Skein_Put64_LSB_First(hash_val, ctx->X, SKEIN1024_BLOCK_BYTES);

	return SKEIN_SUCCESS;
}

#if SKEIN_TREE_HASH
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* just do the OUTPUT stage                                       */
int skein_256_output(struct skein_256_ctx *ctx, u8 *hash_val)
{
	size_t i, n, byte_cnt;
	u64 X[SKEIN_256_STATE_WORDS];
	/* catch uninitialized context */
	Skein_Assert(ctx->h.b_cnt <= SKEIN_256_BLOCK_BYTES, SKEIN_FAIL);

	/* now output the result */
	/* total number of output bytes */
	byte_cnt = (ctx->h.hash_bit_len + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof(ctx->b));
	/* keep a local copy of counter mode "key" */
	memcpy(X, ctx->X, sizeof(X));
	for (i = 0; i*SKEIN_256_BLOCK_BYTES < byte_cnt; i++) {
		/* build the counter block */
		((u64 *)ctx->b)[0] = Skein_Swap64((u64) i);
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		skein_256_process_block(ctx, ctx->b, 1, sizeof(u64));
		/* number of output bytes left to go */
		n = byte_cnt - i*SKEIN_256_BLOCK_BYTES;
		if (n >= SKEIN_256_BLOCK_BYTES)
			n  = SKEIN_256_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		Skein_Put64_LSB_First(hash_val+i*SKEIN_256_BLOCK_BYTES, ctx->X,
				      n);
		Skein_Show_Final(256, &ctx->h, n,
				 hash_val+i*SKEIN_256_BLOCK_BYTES);
		/* restore the counter mode key for next time */
		memcpy(ctx->X, X, sizeof(X));
	}
	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* just do the OUTPUT stage                                       */
int skein_512_output(struct skein_512_ctx *ctx, u8 *hash_val)
{
	size_t i, n, byte_cnt;
	u64 X[SKEIN_512_STATE_WORDS];
	/* catch uninitialized context */
	Skein_Assert(ctx->h.b_cnt <= SKEIN_512_BLOCK_BYTES, SKEIN_FAIL);

	/* now output the result */
	/* total number of output bytes */
	byte_cnt = (ctx->h.hash_bit_len + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof(ctx->b));
	/* keep a local copy of counter mode "key" */
	memcpy(X, ctx->X, sizeof(X));
	for (i = 0; i*SKEIN_512_BLOCK_BYTES < byte_cnt; i++) {
		/* build the counter block */
		((u64 *)ctx->b)[0] = Skein_Swap64((u64) i);
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		skein_512_process_block(ctx, ctx->b, 1, sizeof(u64));
		/* number of output bytes left to go */
		n = byte_cnt - i*SKEIN_512_BLOCK_BYTES;
		if (n >= SKEIN_512_BLOCK_BYTES)
			n  = SKEIN_512_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		Skein_Put64_LSB_First(hash_val+i*SKEIN_512_BLOCK_BYTES, ctx->X,
				      n);
		Skein_Show_Final(256, &ctx->h, n,
				 hash_val+i*SKEIN_512_BLOCK_BYTES);
		/* restore the counter mode key for next time */
		memcpy(ctx->X, X, sizeof(X));
	}
	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* just do the OUTPUT stage                                       */
int skein_1024_output(struct skein_1024_ctx *ctx, u8 *hash_val)
{
	size_t i, n, byte_cnt;
	u64 X[SKEIN1024_STATE_WORDS];
	/* catch uninitialized context */
	Skein_Assert(ctx->h.b_cnt <= SKEIN1024_BLOCK_BYTES, SKEIN_FAIL);

	/* now output the result */
	/* total number of output bytes */
	byte_cnt = (ctx->h.hash_bit_len + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof(ctx->b));
	/* keep a local copy of counter mode "key" */
	memcpy(X, ctx->X, sizeof(X));
	for (i = 0; i*SKEIN1024_BLOCK_BYTES < byte_cnt; i++) {
		/* build the counter block */
		((u64 *)ctx->b)[0] = Skein_Swap64((u64) i);
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		skein_1024_process_block(ctx, ctx->b, 1, sizeof(u64));
		/* number of output bytes left to go */
		n = byte_cnt - i*SKEIN1024_BLOCK_BYTES;
		if (n >= SKEIN1024_BLOCK_BYTES)
			n  = SKEIN1024_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		Skein_Put64_LSB_First(hash_val+i*SKEIN1024_BLOCK_BYTES, ctx->X,
				      n);
		Skein_Show_Final(256, &ctx->h, n,
				 hash_val+i*SKEIN1024_BLOCK_BYTES);
		/* restore the counter mode key for next time */
		memcpy(ctx->X, X, sizeof(X));
	}
	return SKEIN_SUCCESS;
}
#endif
