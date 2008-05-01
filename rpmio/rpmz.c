/**
 * \file rpmio/rpmrepo.c
 */

///////////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2007 Lasse Collin
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2.1 of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
///////////////////////////////////////////////////////////////////////////////

#include "system.h"

#include <rpmio.h>
#include <poptIO.h>
#include <lzma.h>

#include "debug.h"

enum tool_mode {
        MODE_COMPRESS,
        MODE_DECOMPRESS,
        MODE_TEST,
        MODE_LIST,
};
/*@unchecked@*/
static enum tool_mode opt_mode = MODE_COMPRESS;

enum header_type {
        HEADER_AUTO,
        HEADER_NATIVE,
        HEADER_SINGLE,
        HEADER_MULTI,
        HEADER_ALONE,
        // HEADER_GZIP,
};
/*@unchecked@*/
static enum header_type opt_header = HEADER_AUTO;

/*@unchecked@*/
static char *opt_suffix = NULL;

/*@unchecked@*/
static const char *opt_files_name = NULL;
/*@unchecked@*/
static char opt_files_split = '\0';
/*@unchecked@*/
static FILE *opt_files_file = NULL;

/*@unchecked@*/
static int opt_stdout;
/*@unchecked@*/
static int opt_force;
/*@unchecked@*/
static int opt_keep_original;
/*@unchecked@*/
static int opt_preserve_name;
/*@unchecked@*/
static int opt_recursive;

/*@unchecked@*/
static lzma_check_type opt_check = LZMA_CHECK_CRC64;
/*@unchecked@*/
static lzma_options_filter opt_filters[8];

// We don't modify or free() this, but we need to assign it in some
// non-const pointers.
/*@unchecked@*/
static const char *stdin_filename = "(stdin)";

/*@unchecked@*/
static int opt_memory;
/*@unchecked@*/
static int opt_threads;

/*@unchecked@*/
static size_t preset_number = 7 - 1;
/*@unchecked@*/
static int preset_default = 1;
/*@unchecked@*/
static size_t filter_count = 0;

enum {
        OPT_COPY = INT_MIN,
        OPT_SUBBLOCK,
        OPT_X86,
        OPT_POWERPC,
        OPT_IA64,
        OPT_ARM,
        OPT_ARMTHUMB,
        OPT_SPARC,
        OPT_DELTA,
        OPT_LZMA,

        OPT_FILES,
        OPT_FILES0,
};

/*==============================================================*/

#define GZ_SUFFIX	".gz"
#define SUFFIX_LEN	(sizeof(GZ_SUFFIX)-1)
#define	BUFLEN		(16 * 1024)
#define	MAX_NAME_LEN	1024

/*
 * Copy input to output.
 */
static rpmRC rpmz_copy(FD_t in, FD_t out)
{
    char buf[BUFLEN];
    size_t len;

    for (;;) {
        len = Fread(buf, 1, sizeof(buf), in);
        if (Ferror(in))
	    return RPMRC_FAIL;

        if (len == 0) break;

        if (Fwrite(buf, 1, len, out) != len)
	    return RPMRC_FAIL;
    }
    return RPMRC_OK;
}

/*
 * Compress the given file: create a corresponding .gz file and remove the
 * original.
 */
static rpmRC file_compress(const char *file, const char *fmode)
{
    const char * infile = file;
    char outfile[MAX_NAME_LEN];
    FD_t in;
    FD_t out;
    rpmRC rc;
    int xx;

    (void) stpcpy( stpcpy(outfile, file), GZ_SUFFIX);

    in = Fopen(infile, "rb");
    if (in == NULL) {
        fprintf(stderr, "%s: can't Fopen %s\n", __progname, infile);
	return RPMRC_FAIL;
    }
    out = gzdio->_fopen(outfile, fmode);
    if (out == NULL) {
        fprintf(stderr, "%s: can't _fopen %s\n", __progname, outfile);
	xx = Fclose(in);
	return RPMRC_FAIL;
    }
    rc = rpmz_copy(in, out);
    xx = Fclose(in);
    xx = Fclose(out);
    if (rc == RPMRC_OK)
	xx = Unlink(infile);
    return rc;
}

/*
 * Uncompress the given file and remove the original.
 */
static rpmRC file_uncompress(const char *file)
{
    char buf[MAX_NAME_LEN];
    const char *infile, *outfile;
    FD_t out;
    FD_t in;
    size_t len = strlen(file);
    rpmRC rc;
    int xx;

    strcpy(buf, file);

    if (len > SUFFIX_LEN && strcmp(file+len-SUFFIX_LEN, GZ_SUFFIX) == 0) {
        buf[len-SUFFIX_LEN] = '\0';
        infile = file;
        outfile = buf;
    } else {
        strcat(buf, GZ_SUFFIX);
        outfile = file;
        infile = buf;
    }
    in = gzdio->_fopen(infile, "rb");
    if (in == NULL) {
        fprintf(stderr, "%s: can't _fopen %s\n", __progname, infile);
	return RPMRC_FAIL;
    }
    out = Fopen(outfile, "wb");
    if (out == NULL) {
        fprintf(stderr, "%s: can't Fopen %s\n", __progname, outfile);
	xx = Fclose(in);
	return RPMRC_FAIL;
    }
    rc = rpmz_copy(in, out);
    xx = Fclose(in);
    xx = Fclose(out);
    if (rc == RPMRC_OK)
	xx = Unlink(infile);
    return rc;
}

/*==============================================================*/

typedef struct {
	const char *name;
	uint64_t id;
} name_id_map;

typedef struct {
	const char *name;
	const name_id_map *map;
	uint64_t min;
	uint64_t max;
} option_map;


/// Parses option=value pairs that are separated with colons, semicolons,
/// or commas: opt=val:opt=val;opt=val,opt=val
///
/// Each option is a string, that is converted to an integer using the
/// index where the option string is in the array.
///
/// Value can be either a number with minimum and maximum value limit, or
/// a string-id map mapping a list of possible string values to integers.
///
/// When parsing both option and value succeed, a filter-specific function
/// is called, which should update the given value to filter-specific
/// options structure.
///
/// \param      str     String containing the options from the command line
/// \param      opts    Filter-specific option map
/// \param      set     Filter-specific function to update filter_options
/// \param      filter_options  Pointer to filter-specific options structure
///
/// \return     Returns only if no errors occur.
///
static void
parse_options(const char *str, const option_map *opts,
		void (*set)(void *filter_options,
			uint32_t key, uint64_t value),
		void *filter_options)
	/*@*/
{
	char * s;
	char *name;

	if (str == NULL || str[0] == '\0')
		return;

	s = xstrdup(str);
	name = s;

	while (1) {
		char *split = strchr(name, ',');
		char *value;
		int found;
		size_t i;

		if (split != NULL)
			*split = '\0';

		value = strchr(name, '=');
		if (value != NULL)
			*value++ = '\0';

		if (value == NULL || value[0] == '\0') {
			fprintf(stderr, _("%s: %s: Options must be `name=value' "
					"pairs separated with commas"),
					__progname, str);
			/*@-exitarg@*/ exit(2); /*@=exitarg@*/
		}

		// Look for the option name from the option map.
		found = 0;
		for (i = 0; opts[i].name != NULL; ++i) {
			if (strcmp(name, opts[i].name) != 0)
				continue;

			if (opts[i].map == NULL) {
				// value is an integer.
				uint64_t v;
#ifdef NOTYET
				v = str_to_uint64(name, value,
						opts[i].min, opts[i].max);
#else
				v = strtoull(value, NULL, 0);
#endif
				set(filter_options, i, v);
			} else {
				// value is a string which we should map
				// to an integer.
				size_t j;
				for (j = 0; opts[i].map[j].name != NULL; ++j) {
					if (strcmp(opts[i].map[j].name, value)
							== 0)
						break;
				}

				if (opts[i].map[j].name == NULL) {
					fprintf(stderr, _("%s: %s: Invalid option "
							"value"), __progname, value);
					/*@-exitarg@*/ exit(2); /*@=exitarg@*/
				}

				set(filter_options, i, opts[i].map[j].id);
			}

			found = 1;
			break;
		}

		if (!found) {
			fprintf(stderr, _("%s: %s: Invalid option name"), __progname, name);
			/*@-exitarg@*/ exit(2); /*@=exitarg@*/
		}

		if (split == NULL)
			break;

		name = split + 1;
	}

	free(s);
	return;
}

/* ===== Subblock ===== */
enum {
	OPT_SIZE,
	OPT_RLE,
	OPT_ALIGN,
};

static void
set_subblock(void *options, uint32_t key, uint64_t value)
	/*@*/
{
	lzma_options_subblock *opt = options;

	switch (key) {
	case OPT_SIZE:
		opt->subblock_data_size = value;
		break;

	case OPT_RLE:
		opt->rle = value;
		break;

	case OPT_ALIGN:
		opt->alignment = value;
		break;
	}
}

static lzma_options_subblock *
parse_options_subblock(const char *str)
	/*@*/
{
	static const option_map opts[] = {
		{ "size", NULL,   LZMA_SUBBLOCK_DATA_SIZE_MIN,
		                  LZMA_SUBBLOCK_DATA_SIZE_MAX },
		{ "rle",  NULL,   LZMA_SUBBLOCK_RLE_OFF,
		                  LZMA_SUBBLOCK_RLE_MAX },
		{ "align",NULL,   LZMA_SUBBLOCK_ALIGNMENT_MIN,
		                  LZMA_SUBBLOCK_ALIGNMENT_MAX },
		{ NULL,   NULL,   0, 0 }
	};

	lzma_options_subblock *options
			= xmalloc(sizeof(lzma_options_subblock));
	options->allow_subfilters = 0;
	options->alignment = LZMA_SUBBLOCK_ALIGNMENT_DEFAULT;
	options->subblock_data_size = LZMA_SUBBLOCK_DATA_SIZE_DEFAULT;
	options->rle = LZMA_SUBBLOCK_RLE_OFF;

	parse_options(str, opts, &set_subblock, options);

	return options;
}

/* ===== Delta ===== */

enum {
	OPT_DISTANCE,
};


static void
set_delta(void *options, uint32_t key, uint64_t value)
	/*@*/
{
	lzma_options_delta *opt = options;
	switch (key) {
	case OPT_DISTANCE:
		opt->distance = value;
		break;
	}
}

static lzma_options_delta *
parse_options_delta(const char *str)
{
	static const option_map opts[] = {
		{ "distance", NULL,  LZMA_DELTA_DISTANCE_MIN,
		                     LZMA_DELTA_DISTANCE_MAX },
		{ NULL,       NULL,  0, 0 }
	};

	lzma_options_delta *options = xmalloc(sizeof(lzma_options_subblock));
	// It's hard to give a useful default for this.
	options->distance = LZMA_DELTA_DISTANCE_MIN;

	parse_options(str, opts, &set_delta, options);

	return options;
}

/* ===== LZMA ===== */
enum {
	OPT_DICT,
	OPT_LC,
	OPT_LP,
	OPT_PB,
	OPT_MODE,
	OPT_FB,
	OPT_MF,
	OPT_MC
};

static void
set_lzma(void *options, uint32_t key, uint64_t value)
	/*@*/
{
	lzma_options_lzma *opt = options;

	switch (key) {
	case OPT_DICT:
		opt->dictionary_size = value;
		break;

	case OPT_LC:
		opt->literal_context_bits = value;
		break;

	case OPT_LP:
		opt->literal_pos_bits = value;
		break;

	case OPT_PB:
		opt->pos_bits = value;
		break;

	case OPT_MODE:
		opt->mode = value;
		break;

	case OPT_FB:
		opt->fast_bytes = value;
		break;

	case OPT_MF:
		opt->match_finder = value;
		break;

	case OPT_MC:
		opt->match_finder_cycles = value;
		break;
	}
}

static lzma_options_lzma *
parse_options_lzma(const char *str)
{
	/*@unchecked@*/ /*@observer@*/
	static const name_id_map modes[] = {
		{ "fast", LZMA_MODE_FAST },
		{ "best", LZMA_MODE_BEST },
		{ NULL,   0 }
	};

	/*@unchecked@*/ /*@observer@*/
	static const name_id_map mfs[] = {
		{ "hc3", LZMA_MF_HC3 },
		{ "hc4", LZMA_MF_HC4 },
		{ "bt2", LZMA_MF_BT2 },
		{ "bt3", LZMA_MF_BT3 },
		{ "bt4", LZMA_MF_BT4 },
		{ NULL,  0 }
	};

	/*@unchecked@*/ /*@observer@*/
	static const option_map opts[] = {
		{ "dict", NULL,   LZMA_DICTIONARY_SIZE_MIN,
				LZMA_DICTIONARY_SIZE_MAX },
		{ "lc",   NULL,   LZMA_LITERAL_CONTEXT_BITS_MIN,
				  LZMA_LITERAL_CONTEXT_BITS_MAX },
		{ "lp",   NULL,   LZMA_LITERAL_POS_BITS_MIN,
				  LZMA_LITERAL_POS_BITS_MAX },
		{ "pb",   NULL,   LZMA_POS_BITS_MIN, LZMA_POS_BITS_MAX },
		{ "mode", modes,  0, 0 },
		{ "fb",   NULL,   LZMA_FAST_BYTES_MIN, LZMA_FAST_BYTES_MAX },
		{ "mf",   mfs,    0, 0 },
		{ "mc",   NULL,   0, UINT32_MAX },
		{ NULL,   NULL,   0, 0 }
	};

	lzma_options_lzma *options = xmalloc(sizeof(lzma_options_lzma));

	options->dictionary_size = LZMA_DICTIONARY_SIZE_DEFAULT;
	options->literal_context_bits = LZMA_LITERAL_CONTEXT_BITS_DEFAULT;
	options->literal_pos_bits = LZMA_LITERAL_POS_BITS_DEFAULT;
	options->pos_bits = LZMA_POS_BITS_DEFAULT;
	options->mode = LZMA_MODE_BEST;
	options->fast_bytes = LZMA_FAST_BYTES_DEFAULT;
	options->match_finder = LZMA_MF_BT4;
	options->match_finder_cycles = 0;

	parse_options(str, opts, &set_lzma, options);

	return options;
}

static void
add_filter(lzma_vli id, const char *opt_str)
	/*@globals filter_count, opt_filters, preset_default @*/
	/*@modifies filter_count, opt_filters, preset_default @*/
{
    if (filter_count == 7) {
	fprintf(stderr, _("%s: Maximum number of filters is seven\n"),
		__progname);
	/*@-exitarg@*/ exit(2); /*@=exitarg@*/
    }

    opt_filters[filter_count].id = id;

    switch (id) {
    case LZMA_FILTER_SUBBLOCK:
	opt_filters[filter_count].options = parse_options_subblock(opt_str);
	break;
    case LZMA_FILTER_DELTA:
	opt_filters[filter_count].options = parse_options_delta(opt_str);
	break;
    case LZMA_FILTER_LZMA:
	opt_filters[filter_count].options = parse_options_lzma(opt_str);
	break;
    default:
	assert(opt_str == NULL);
	opt_filters[filter_count].options = NULL;
	break;
    }

    ++filter_count;
    preset_default = 0;
    return;
}

/**
 */
static void rpmzArgCallback(poptContext con,
                /*@unused@*/ enum poptCallbackReason reason,
                const struct poptOption * opt, const char * arg,
                /*@unused@*/ void * data)
	/*@*/
{
    /* XXX avoid accidental collisions with POPT_BIT_SET for flags */
    if (opt->arg == NULL)
    switch (opt->val) {
    case 'C':
	if (!strcmp(arg, "none"))
	    opt_check = LZMA_CHECK_NONE;
	else if (!strcmp(arg, "crc32"))
	    opt_check = LZMA_CHECK_CRC32;
	else if (!strcmp(arg, "crc64"))
	    opt_check = LZMA_CHECK_CRC64;
	else if (!strcmp(arg, "sha256"))
	    opt_check = LZMA_CHECK_SHA256;
	else {
	    fprintf(stderr, _("%s: Unknown integrity check method \"%s\"\n"),
		__progname, arg);
	    /*@-exitarg@*/ exit(2); /*@=exitarg@*/
	    /*@notreached@*/
	}
	break;
    case 'F':
	if (!strcmp(arg, "auto"))
	    opt_header = 0;
	else if (!strcmp(arg, "native"))
	    opt_header = 1;
	else if (!strcmp(arg, "single"))
	    opt_header = 2;
	else if (!strcmp(arg, "multi"))
	    opt_header = 3;
	else if (!strcmp(arg, "alone"))
	    opt_header = 4;
	else if (!strcmp(arg, "gzip"))
	    opt_header = 5;
	else {
	    fprintf(stderr, _("%s: Unknown file format type \"%s\"\n"),
		__progname, arg);
	    /*@-exitarg@*/ exit(2); /*@=exitarg@*/
	    /*@notreached@*/
	}
	break;
    case OPT_COPY:
	add_filter(LZMA_FILTER_COPY, NULL);
	break;
    case OPT_X86:
	add_filter(LZMA_FILTER_X86, NULL);
	break;
    case OPT_POWERPC:
	add_filter(LZMA_FILTER_POWERPC, NULL);
	break;
    case OPT_IA64:
	add_filter(LZMA_FILTER_IA64, NULL);
	break;
    case OPT_ARM:
	add_filter(LZMA_FILTER_ARM, NULL);
	break;
    case OPT_ARMTHUMB:
	add_filter(LZMA_FILTER_ARMTHUMB, NULL);
	break;
    case OPT_SPARC:
	add_filter(LZMA_FILTER_SPARC, NULL);
	break;
    case OPT_SUBBLOCK:
	add_filter(LZMA_FILTER_SUBBLOCK, optarg);
	break;
    case OPT_DELTA:
	add_filter(LZMA_FILTER_DELTA, optarg);
	break;
    case OPT_LZMA:
	add_filter(LZMA_FILTER_LZMA, optarg);
	break;

    case OPT_FILES:
	opt_files_split = '\n';
	/*@fallthrough@*/
    case OPT_FILES0:
	if (arg == NULL || !strcmp(arg, "-")) {
	    opt_files_name = xstrdup(stdin_filename);
	    opt_files_file = stdin;
	} else {
	    opt_files_name = xstrdup(arg);
	    opt_files_file = fopen(opt_files_name,
			opt->val == OPT_FILES ? "r" : "rb");
	    if (opt_files_file == NULL || ferror(opt_files_file)) {
		fprintf(stderr, _("%s: %s: %s\n"),
			__progname, opt_files_name, strerror(errno));
		/*@-exitarg@*/ exit(2); /*@=exitarg@*/
		/*@notreached@*/
	    }
	}
	break;

    default:
	fprintf(stderr, _("%s: Unknown option -%c\n"), __progname, opt->val);
	poptPrintUsage(con, stderr, 0);
	/*@-exitarg@*/ exit(2); /*@=exitarg@*/
	/*@notreached@*/ break;
    }
}

/*==============================================================*/

/*@unchecked@*/ /*@observer@*/
static struct poptOption optionsTable[] = {
/*@-type@*/ /* FIX: cast? */
 { NULL, '\0', POPT_ARG_CALLBACK | POPT_CBFLAG_INC_DATA | POPT_CBFLAG_CONTINUE,
        rpmzArgCallback, 0, NULL, NULL },
/*@=type@*/

  /* ===== Operation modes */
  { "compress", 'z', POPT_ARG_VAL,		&opt_mode, MODE_COMPRESS,
	N_("force compression"), NULL },
  { "decompress", 'd', POPT_ARG_VAL,		&opt_mode, MODE_DECOMPRESS,
	N_("force decompression"), NULL },
  { "uncompress", '\0', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &opt_mode, MODE_DECOMPRESS,

	N_("force decompression"), NULL },
  { "test", 't', POPT_ARG_VAL,			&opt_mode,  MODE_TEST,
	N_("test compressed file integrity"), NULL },
  { "list", 'l', POPT_ARG_VAL,			&opt_mode,  MODE_LIST,
	N_("list block sizes, total sizes, and possible metadata"), NULL },
  { "info", '\0', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN,	&opt_mode,  MODE_LIST,
	N_("list block sizes, total sizes, and possible metadata"), NULL },

  /* ===== Operation modifiers */
  { "keep", 'k', POPT_ARG_VAL,			&opt_keep_original, 1,
	N_("keep (don't delete) input files"), NULL },
  { "force", 'f', POPT_ARG_VAL,			&opt_force,  1,
	N_("force overwrite of output file and (de)compress links"), NULL },
  { "stdout", 'c', POPT_ARG_VAL,		&opt_stdout,  1,
	N_("write to standard output and don't delete input files"), NULL },
  { "to-stdout", 'c', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &opt_stdout,  1,
	N_("write to standard output and don't delete input files"), NULL },
  { "suffix", 'S', POPT_ARG_STRING,		&opt_suffix, 0,
	N_("use suffix `.SUF' on compressed files instead"), N_(".SUF") },
  { "format", 'F', POPT_ARG_STRING,		NULL, 'F',
	N_("file FORMAT {auto|native|single|multi|alone|gzip}"), N_("FORMAT") },
  { "files", '\0', POPT_ARG_STRING|POPT_ARGFLAG_OPTIONAL, NULL, OPT_FILES,
	N_("read filenames to process from FILE"), N_("FILE") },
  { "files0", '\0', POPT_ARG_STRING|POPT_ARGFLAG_OPTIONAL, NULL, OPT_FILES0,
	N_("like --files but use the nul byte as terminator"), N_("FILE") },

  /* ===== Compression options */
  { "fast", '\0', POPT_ARG_VAL,			&preset_number,  1,
	N_("fast compression"), NULL },
  { NULL, '1', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN,	&preset_number,  1,
	NULL, NULL },
  { NULL, '2', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN,	&preset_number,  2,
	NULL, NULL },
  { NULL, '3', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN,	&preset_number,  3,
	NULL, NULL },
  { NULL, '4', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN,	&preset_number,  4,
	NULL, NULL },
  { NULL, '5', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN,	&preset_number,  5,
	NULL, NULL },
  { NULL, '6', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN,	&preset_number,  6,
	NULL, NULL },
  { NULL, '7', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN,	&preset_number,  7,
	NULL, NULL },
  { NULL, '8', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN,	&preset_number,  8,
	NULL, NULL },
  { NULL, '9', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN,	&preset_number,  9,
	NULL, NULL },
  { "best", '\0', POPT_ARG_VAL,			&preset_number,  9,
	N_("best compression"), NULL },
  { "check", 'C', POPT_ARG_STRING,		NULL, 'C',
	N_("integrity check METHOD {none|crc32|crc64|sha256}"), N_("METHOD") },

  /* ===== Custom filters */
#ifdef	REFERENCE
"  --lzma=[OPTS]       LZMA filter; OPTS is a comma-separated list of zero or\n"
"                      more of the following options (valid values; default):\n"
"                        dict=NUM   dictionary size in bytes (1 - 1Gi; 8Mi)\n"
"                        lc=NUM     number of literal context bits (0-8; 3)\n"
"                        lp=NUM     number of literal position bits (0-4; 0)\n"
"                        pb=NUM     number of position bits (0-4; 2)\n"
"                        mode=MODE  compression mode (`fast' or `best'; `best')\n"
"                        fb=NUM     number of fast bytes (5-273; 128)\n"
"                        mf=NAME    match finder (hc3, hc4, bt2, bt3, bt4; bt4)\n"
"                        mfc=NUM    match finder cycles; 0=automatic (default)\n"
#endif
  { "lzma", '\0', POPT_ARG_STRING|POPT_ARGFLAG_OPTIONAL,	NULL, OPT_LZMA,
	N_("set lzma filter"), N_("OPTS") },

  { "x86", '\0', 0,				NULL, OPT_X86,
	N_("ix86 filter (sometimes called BCJ filter)"), NULL },
  { "bcj", '\0', POPT_ARGFLAG_DOC_HIDDEN,	NULL, OPT_X86,
	N_("x86 filter (sometimes called BCJ filter)"), NULL },
  { "powerpc", '\0', 0,				NULL, OPT_POWERPC,
	N_("PowerPC (big endian) filter"), NULL },
  { "ppc", '\0', POPT_ARGFLAG_DOC_HIDDEN,	NULL, OPT_POWERPC,
	N_("PowerPC (big endian) filter"), NULL },
  { "ia64", '\0', 0,				NULL, OPT_IA64,
	N_("IA64 (Itanium) filter"), NULL },
  { "itanium", '\0', POPT_ARGFLAG_DOC_HIDDEN,	NULL, OPT_IA64,
	N_("IA64 (Itanium) filter"), NULL },
  { "arm", '\0', 0,				NULL, OPT_ARM,
	N_("ARM filter"), NULL },
  { "armthumb", '\0', 0,			NULL, OPT_ARMTHUMB,
	N_("ARM-Thumb filter"), NULL },
  { "sparc", '\0', 0,				NULL, OPT_SPARC,
	N_("SPARC filter"), NULL },

#ifdef	REFERENCE
"  --delta=[OPTS]      Delta filter; valid OPTS (valid values; default):\n"
"                        distance=NUM  Distance between bytes being\n"
"                                      subtracted from each other (1-256; 1)\n"
#endif
  { "delta", '\0', POPT_ARG_STRING|POPT_ARGFLAG_OPTIONAL,	NULL, OPT_DELTA,
	N_("set delta filter"), N_("OPTS") },

  { "copy", '\0', 0,				NULL, OPT_COPY,
	N_("No filtering (useful only when specified alone)"), NULL },

#ifdef	REFERENCE
"  --subblock=[OPTS]   Subblock filter; valid OPTS (valid values; default):\n"
"                        size=NUM    number of bytes of data per subblock\n"
"                                    (1 - 256Mi; 4Ki)\n"
"                        rle=NUM     run-length encoder chunk size (0-256; 0)\n"
#endif
  { "subblock", '\0', POPT_ARG_STRING|POPT_ARGFLAG_OPTIONAL,	NULL, OPT_SUBBLOCK,
	N_("set subblock filter"), N_("OPTS") },

  /* ===== Metadata options */
  { "name", 'N', POPT_ARG_VAL,			&opt_preserve_name, 1,
	N_("save or restore the original filename and time stamp"), NULL },
  { "no-name", 'n', POPT_ARG_VAL,		&opt_preserve_name, 0,
	N_("do not save or restore filename and time stamp (default)"), NULL },
#ifdef	NOTYET
  { "sign", 'S', POPT_ARG_STRING,		NULL, 0,
	N_("sign the data with GnuPG when compressing, or verify the signature when decompressing"), N_("PUBKEY"} },
#endif

  /* ===== Resource usage options */
  { "memory", 'M', POPT_ARG_INT,		&opt_memory,  0,
	N_("use roughly NUM Mbytes of memory at maximum"), N_("NUM") },
  { "threads", 'T', POPT_ARG_INT,		&opt_threads, 0,
	N_("use maximum of NUM (de)compression threads"), N_("NUM") },

  /* ===== Other options */
#ifdef	NOTYET
  { "quiet", 'q',              POPT_ARG_VAL,       NULL,  'q',
	N_("suppress warnings; specify twice to suppress errors too"), NULL },
  { "verbose", 'v',            POPT_ARG_VAL,       NULL,  'v',
	N_("be verbose; specify twice for even more verbose"), NULL },
  { "help", 'h',               POPT_ARG_VAL,       NULL,  'h',
	N_("display this help and exit"), NULL },
  { "version", 'V',            POPT_ARG_VAL,       NULL,  'V',
	N_("display version and license information and exit"), NULL },
#endif

  /* XXX unimplemented */
  { "recursive", 'r', POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &opt_recursive, 1,
	N_(""), NULL },

  POPT_AUTOALIAS
  POPT_AUTOHELP

  { NULL, (char)-1, POPT_ARG_INCLUDE_TABLE, NULL, 0,
        "\
Usage: rpmz [OPTION]... [FILE]...\n\
Compress or decompress FILEs.\n\
\n\
", NULL },

  POPT_TABLEEND

};

int
main(int argc, char *argv[])
	/*@globals __assert_program_name,
		rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies __assert_program_name, _rpmrepo,
		rpmGlobalMacroContext, fileSystem, internalState @*/
{
    poptContext optCon = rpmioInit(argc, argv, optionsTable);
    const char ** av = NULL;
    int ac;
    int rc = 1;		/* assume failure. */
    int i;

/*@-observertrans -readonlytrans @*/
    __progname = "rpmgenpkglist";
/*@=observertrans =readonlytrans @*/

    av = poptGetArgs(optCon);
    if (av == NULL || av[0] == NULL) {
	poptPrintUsage(optCon, stderr, 0);
	goto exit;
    }
    ac = argvCount(av);

    if (av != NULL)
    for (i = 0; i < ac; i++) {
	rpmRC frc;
	frc = RPMRC_OK;
	switch (opt_mode) {
	case MODE_COMPRESS:
	    frc = file_compress(av[i], "wb6");
	    break;
	case MODE_DECOMPRESS:
	    frc = file_uncompress(av[i]);
	    break;
	case MODE_TEST:
	    break;
	case MODE_LIST:
	    break;
	}
	if (frc != RPMRC_OK)
	    goto exit;
    }

    rc = 0;

exit:
    optCon = rpmioFini(optCon);

    return rc;
}
