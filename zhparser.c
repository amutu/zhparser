/*-------------------------------------------------------------------------
 *
 * zhparser.c
 *	  a text search parser for Chinese
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "zhparser.h"
#include "miscadmin.h"
#include "utils/guc.h"
#include "utils/builtins.h"


PG_MODULE_MAGIC;

/*
 * types
 */

/* self-defined type */
typedef struct
{
	char	   *buffer;			/* text to parse */
	int			len;			/* length of the text in buffer */
	int			pos;			/* position of the parser */
	scws_t		scws;
	scws_res_t	res;
	scws_res_t	curr;
} ParserState;

/* copy-paste from wparser.h of tsearch2 */
typedef struct
{
	int			lexid;
	char	   *alias;
	char	   *descr;
} LexDescr;

static void zhprs_init();
static void zhprs_init_type(LexDescr descr[]);
static char *zhprs_get_tsearch_config_filename(const char *basename, const char *extension);

void		_PG_init(void);
void		_PG_fini(void);

/*
 * prototypes
 */
PG_FUNCTION_INFO_V1(zhprs_start);
Datum		zhprs_start(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(zhprs_getlexeme);
Datum		zhprs_getlexeme(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(zhprs_end);
Datum		zhprs_end(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(zhprs_lextype);
Datum		zhprs_lextype(PG_FUNCTION_ARGS);


static scws_t zhprs_scws = NULL;

/* config */
static bool zhprs_dict_in_memory = false;
static int zhprs_load_dict_mem_mode = 0;

static char * zhprs_charset = NULL;
static char * zhprs_rules = NULL;
static char * zhprs_extra_dicts = NULL;

static bool zhprs_punctuation_ignore = false;
static bool zhprs_seg_with_duality = false;
static char * zhprs_multi_mode_string = NULL;
static int zhprs_multi_mode = 0;

static void zhprs_assign_dict_in_memory(bool newval, void *extra);
static bool zhprs_check_charset(char **newval, void **extra, GucSource source);
static void zhprs_assign_charset(const char *newval, void *extra);
static bool zhprs_check_rules(char **newval, void **extra, GucSource source);
static void zhprs_assign_rules(const char *newval, void *extra);
static bool zhprs_check_extra_dicts(char **newval, void **extra, GucSource source);
static void zhprs_assign_extra_dicts(const char *newval, void *extra);
static bool zhprs_check_multi_mode(char **newval, void **extra, GucSource source);
static void zhprs_assign_multi_mode(const char *newval, void *extra);


static void
zhprs_init()
{
	zhprs_scws = scws_new();
	if (!zhprs_scws)
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("Failed to init Chinese Parser Lib SCWS!\"%s\"",
						 "")));
}

/*
 * Module load callback
 */
void
_PG_init(void)
{
	if (zhprs_scws)
		return;

	zhprs_init();

	DefineCustomBoolVariable("zhparser.dict_in_memory",
							 "load dicts into memory",
							 NULL,
							 &zhprs_dict_in_memory,
							 false,
							 PGC_BACKEND,
							 0,
							 NULL,
							 zhprs_assign_dict_in_memory,
							 NULL);
	DefineCustomStringVariable("zhparser.charset",
							   "charset",
							   NULL,
							   &zhprs_charset,
							   "utf8",
							   PGC_USERSET,
							   0,
							   zhprs_check_charset,
							   zhprs_assign_charset,
							   NULL);
	DefineCustomStringVariable("zhparser.rules",
							   "rules to load",
							   NULL,
							   &zhprs_rules,
							   "rules.utf8.ini",
							   PGC_USERSET,
							   0,
							   zhprs_check_rules,
							   zhprs_assign_rules,
							   NULL);
	DefineCustomStringVariable("zhparser.extra_dicts",
							   "extra dicts files to load",
							   NULL,
							   &zhprs_extra_dicts,
							   "dict.utf8.xdb",
							   PGC_USERSET,
							   0,
							   zhprs_check_extra_dicts,
							   zhprs_assign_extra_dicts,
							   NULL);

	DefineCustomBoolVariable("zhparser.punctuation_ignore",
							 "set if zhparser ignores the puncuation",
							 "set if zhparser ignores the puncuation, except \\r and \\n",
							 &zhprs_punctuation_ignore,
							 false,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);
	DefineCustomBoolVariable("zhparser.seg_with_duality",
							 "segment words with duality",
							 NULL,
							 &zhprs_seg_with_duality,
							 false,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);
	DefineCustomStringVariable("zhparser.multi_mode",
							   "set multi mode",
							   "short for prefer short words\n"
							   "duality for prefer duality\n"
							   "zmain prefer most important element"
							   "zall prefer prefer all element",
							   &zhprs_multi_mode_string,
							   NULL,
							   PGC_USERSET,
							   0,
							   zhprs_check_multi_mode,
							   zhprs_assign_multi_mode,
							   NULL);
}

/*
 * Module unload callback
 */
void
_PG_fini(void)
{
	if (zhprs_scws)
		scws_free(zhprs_scws);
	zhprs_scws = NULL;
}

/*
 * functions
 */
Datum
zhprs_start(PG_FUNCTION_ARGS)
{
	ParserState *pst = (ParserState *) palloc0(sizeof(ParserState));
	scws_t scws = scws_fork(zhprs_scws);

	pst->buffer = (char *) PG_GETARG_POINTER(0);
	pst->len = PG_GETARG_INT32(1);
	pst->pos = 0;

	pst->scws = scws;
	pst->res = NULL;
	pst->curr = NULL;

	scws_set_ignore(scws, (int) zhprs_punctuation_ignore);
	scws_set_duality(scws, (int) zhprs_seg_with_duality);
	scws_set_multi(scws, zhprs_multi_mode);

	scws_send_text(scws, pst->buffer, pst->len);

	PG_RETURN_POINTER(pst);
}

Datum
zhprs_getlexeme(PG_FUNCTION_ARGS)
{
	ParserState *pst = (ParserState *) PG_GETARG_POINTER(0);
	char	  **t = (char **) PG_GETARG_POINTER(1);
	int		   *tlen = (int *) PG_GETARG_POINTER(2);
	int			type = -1;

	if (pst->curr == NULL)
		pst->res = pst->curr = scws_get_result(pst->scws);

	/* already done the work, or no sentence */
	if (pst->res == NULL)
	{
		*tlen = 0;
		type = 0;
	}
	/* have results */
	else if (pst->curr != NULL)
	{
		scws_res_t curr = pst->curr;

		/*
		* check the first char to determine the lextype
		* if out of [0,25],then set to 'x',mean unknown type
		* so for Ag,Dg,Ng,Tg,Vg,the type will be unknown
		* for full attr explanation,visit http://www.xunsearch.com/scws/docs.php#attr
		*/
		type = (int)(curr->attr)[0];
		if (type > (int)'x' || type < (int)'a')
			type = (int)'x';
		*tlen = curr->len;
		*t = pst->buffer + curr->off;

		pst->curr = curr->next;

		/* clear for the next calling */
		if (pst->curr == NULL)
		{
			scws_free_result(pst->res);
			pst->res = NULL;
		}
	}

	PG_RETURN_INT32(type);
}

Datum
zhprs_end(PG_FUNCTION_ARGS)
{
	ParserState *pst = (ParserState *) PG_GETARG_POINTER(0);

	if (pst->scws)
		scws_free(pst->scws);
	pfree(pst);
	PG_RETURN_VOID();
}

Datum
zhprs_lextype(PG_FUNCTION_ARGS)
{
	static LexDescr   descr[27];
	static LexDescr   *descr_inst = NULL;

	if (!descr_inst)
	{
		zhprs_init_type(descr);
		descr_inst = descr;
	}

	PG_RETURN_POINTER(descr);
}

static void
zhprs_init_type(LexDescr descr[])
{
	/* 
	* there are 26 types in this parser,alias from a to z
	* for full attr explanation,visit http://www.xunsearch.com/scws/docs.php#attr
	*/
	descr[0].lexid = 97;
	descr[0].alias = pstrdup("a");
	descr[0].descr = pstrdup("adjective");
	descr[1].lexid = 98;
	descr[1].alias = pstrdup("b");
	descr[1].descr = pstrdup("differentiation (qu bie)");
	descr[2].lexid = 99;
	descr[2].alias = pstrdup("c");
	descr[2].descr = pstrdup("conjunction");
	descr[3].lexid = 100;
	descr[3].alias = pstrdup("d");
	descr[3].descr = pstrdup("adverb");
	descr[4].lexid = 101;
	descr[4].alias = pstrdup("e");
	descr[4].descr = pstrdup("exclamation");
	descr[5].lexid = 102;
	descr[5].alias = pstrdup("f");
	descr[5].descr = pstrdup("position (fang wei)");
	descr[6].lexid = 103;
	descr[6].alias = pstrdup("g");
	descr[6].descr = pstrdup("root (ci gen)");
	descr[7].lexid = 104;
	descr[7].alias = pstrdup("h");
	descr[7].descr = pstrdup("head");
	descr[8].lexid = 105;
	descr[8].alias = pstrdup("i");
	descr[8].descr = pstrdup("idiom");
	descr[9].lexid = 106;
	descr[9].alias = pstrdup("j");
	descr[9].descr = pstrdup("abbreviation (jian lue)");
	descr[10].lexid = 107;
	descr[10].alias = pstrdup("k");
	descr[10].descr = pstrdup("head");
	descr[11].lexid = 108;
	descr[11].alias = pstrdup("l");
	descr[11].descr = pstrdup("tmp (lin shi)");
	descr[12].lexid = 109;
	descr[12].alias = pstrdup("m");
	descr[12].descr = pstrdup("numeral");
	descr[13].lexid = 110;
	descr[13].alias = pstrdup("n");
	descr[13].descr = pstrdup("noun");
	descr[14].lexid = 111;
	descr[14].alias = pstrdup("o");
	descr[14].descr = pstrdup("onomatopoeia");
	descr[15].lexid = 112;
	descr[15].alias = pstrdup("p");
	descr[15].descr = pstrdup("prepositional");
	descr[16].lexid = 113;
	descr[16].alias = pstrdup("q");
	descr[16].descr = pstrdup("quantity");
	descr[17].lexid = 114;
	descr[17].alias = pstrdup("r");
	descr[17].descr = pstrdup("pronoun");
	descr[18].lexid = 115;
	descr[18].alias = pstrdup("s");
	descr[18].descr = pstrdup("space");
	descr[19].lexid = 116;
	descr[19].alias = pstrdup("t");
	descr[19].descr = pstrdup("time");
	descr[20].lexid = 117;
	descr[20].alias = pstrdup("u");
	descr[20].descr = pstrdup("auxiliary");
	descr[21].lexid = 118;
	descr[21].alias = pstrdup("v");
	descr[21].descr = pstrdup("verb");
	descr[22].lexid = 119;
	descr[22].alias = pstrdup("w");
	descr[22].descr = pstrdup("punctuation (qi ta biao dian)");
	descr[23].lexid = 120;
	descr[23].alias = pstrdup("x");
	descr[23].descr = pstrdup("unknown");
	descr[24].lexid = 121;
	descr[24].alias = pstrdup("y");
	descr[24].descr = pstrdup("modal (yu qi)");
	descr[25].lexid = 122;
	descr[25].alias = pstrdup("z");
	descr[25].descr = pstrdup("status (zhuang tai)");
	descr[26].lexid = 0;
}
//TODO :headline function

/*
 * check_hook, assign_hook and show_hook subroutines
 */
static bool
zhprs_check_charset(char **newval, void **extra, GucSource source)
{
	char* string = *newval;

	if (pg_strcasecmp(string, "gbk") != 0 &&
		pg_strcasecmp(string, "utf8") != 0)
	{
		GUC_check_errdetail("Charset: \"%s\". Only Support \"gbk\" or \"utf8\"", string);
		return false;
	}

	return true;
}

static void
zhprs_assign_charset(const char *newval, void *extra)
{
	scws_set_charset(zhprs_scws, newval);
}

static bool
zhprs_check_rules(char **newval, void **extra, GucSource source)
{
	char	   *rawstring;
	char	    *myextra;
	char	    *ext;
	char	    *rule_path;

	if (strcmp(*newval, "none") == 0)
		return true;

	/* Need a modifiable copy of string */
	rawstring = pstrdup(*newval);

	ext = strrchr(rawstring, '.');
	if (ext && pg_strcasecmp(ext, ".ini") == 0)
	{
		*ext = '\0';
		ext++;
		rule_path = zhprs_get_tsearch_config_filename(rawstring, ext);
	}
	else
	{
		GUC_check_errdetail("Unrecognized key word: \"%s\". Must end with .ini", rawstring);
		pfree(rawstring);
		return false;
	}

	myextra = strdup(rule_path);
	*extra = (void *) myextra;
	pfree(rule_path);

	return true;
}

static void
zhprs_assign_rules(const char *newval, void *extra)
{
	char *myextra = (char *) extra;

	/* Do nothing for the boot_val default of NULL */
	if (!extra)
		return;

	scws_set_rule(zhprs_scws, myextra);
}

typedef struct
{
	int			mode;
	char		path[MAXPGPATH];
} dict_elem;

/* This is the "extra" state */
typedef struct
{
	int			num;
	dict_elem	dicts[1];
} dict_extra;

static bool
zhprs_check_extra_dicts(char **newval, void **extra, GucSource source)
{
	char	   *rawstring;
	List	   *elemlist;
	ListCell   *l;
	dict_extra *myextra;
	int			num;
	int			i;

	if (strcmp(*newval, "none") == 0)
		return true;

	/* Need a modifiable copy of string */
	rawstring = pstrdup(*newval);

	/* Parse string into list of identifiers */
	if (!SplitIdentifierString(rawstring, ',', &elemlist))
	{
		/* syntax error in list */
		GUC_check_errdetail("List syntax is invalid.");
		pfree(rawstring);
		list_free(elemlist);
		return false;
	}

	num = list_length(elemlist);
	myextra = (dict_extra *) malloc(sizeof(dict_extra) + num * sizeof(dict_elem));
	if (!myextra)
	{
		GUC_check_errdetail("Out of memory. Too many dictionary");
		pfree(rawstring);
		list_free(elemlist);
		return false;
	}

	i = 0;
	foreach(l, elemlist)
	{
		char	   *tok = (char *) lfirst(l);
		char	   *dict_path;

		int load_dict_mode = zhprs_load_dict_mem_mode;
		char * ext = strrchr(tok, '.');
		if (ext && strlen(ext) == 4)
		{
			if (pg_strcasecmp(ext, ".txt") == 0)
				load_dict_mode |= SCWS_XDICT_TXT;
			else if(pg_strcasecmp(ext, ".xdb") == 0)
				load_dict_mode |= SCWS_XDICT_XDB;
			else
			{
				GUC_check_errdetail("Unrecognized key word: \"%s\". Must end with .txt or .xdb", tok);
				pfree(rawstring);
				list_free(elemlist);
				free(myextra);
				return false;
			}

			*ext = '\0';
			ext++;
		}
		else
		{
			GUC_check_errdetail("Unrecognized key word: \"%s\". Must end with .txt or .xdb", tok);
			pfree(rawstring);
			list_free(elemlist);
			free(myextra);
			return false;
		}

		dict_path = zhprs_get_tsearch_config_filename(tok, ext);

		memcpy(myextra->dicts[i].path, dict_path, MAXPGPATH);
		myextra->dicts[i].mode = load_dict_mode;
		i++;
	}
	myextra->num = i;
	*extra = (void *) myextra;

	pfree(rawstring);
	list_free(elemlist);

	return true;
}

static void
zhprs_assign_extra_dicts(const char *newval, void *extra)
{
	dict_extra *myextra = (dict_extra *) extra;
	int i;

	/* Do nothing for the boot_val default of NULL */
	if (!extra)
		return;

	for (i = 0; i < myextra->num; i++)
	{
		int err;
		char	*dict_path = myextra->dicts[i].path;
		int		mode = myextra->dicts[i].mode;

		if (i == 0)
			err = scws_set_dict(zhprs_scws, dict_path, mode);
		else
			err = scws_add_dict(zhprs_scws, dict_path, mode);

		/* ignore error*/
		if (err != 0)
			ereport(NOTICE,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("zhparser add dict : \"%s\" failed!",
						 dict_path)));
	}
}

static bool
zhprs_check_multi_mode(char **newval, void **extra, GucSource source)
{
	char	   *rawstring;
	List	   *elemlist;
	ListCell   *l;
	int			new_multi_mode = 0;
	int		   *myextra;

	if (*newval)
	{
		/* Need a modifiable copy of string */
		rawstring = pstrdup(*newval);

		/* Parse string into list of identifiers */
		if (!SplitIdentifierString(rawstring, ',', &elemlist))
		{
			/* syntax error in list */
			GUC_check_errdetail("List syntax is invalid.");
			pfree(rawstring);
			list_free(elemlist);
			return false;
		}

		foreach(l, elemlist)
		{
			char	   *tok = (char *) lfirst(l);

			if (pg_strcasecmp(tok, "short") == 0)
				new_multi_mode |= SCWS_MULTI_SHORT;
			else if (pg_strcasecmp(tok, "duality") == 0)
				new_multi_mode |= SCWS_MULTI_DUALITY;
			else if (pg_strcasecmp(tok, "zmain") == 0)
				new_multi_mode |= SCWS_MULTI_ZMAIN;
			else if (pg_strcasecmp(tok, "zall") == 0)
				new_multi_mode |= SCWS_MULTI_ZALL;
			else
			{
				GUC_check_errdetail("Unrecognized key word: \"%s\".", tok);
				pfree(rawstring);
				list_free(elemlist);
				return false;
			}
		}

		pfree(rawstring);
		list_free(elemlist);
	}

	myextra = (int *) malloc(sizeof(int));
	if (!myextra)
	{
		GUC_check_errdetail("Out of memory");
		return false;
	}

	*myextra = new_multi_mode;
	*extra = (void *) myextra;

	return true;
}

static void
zhprs_assign_multi_mode(const char *newval, void *extra)
{
	zhprs_multi_mode = *((int *) extra);
}

static void
zhprs_assign_dict_in_memory(bool newval, void *extra)
{
	if (newval)
		zhprs_load_dict_mem_mode = SCWS_XDICT_MEM;
	else
		zhprs_load_dict_mem_mode = 0;
}


/*
 * Given the base name and extension of a tsearch config file, return
 * its full path name.	The base name is assumed to be user-supplied,
 * and is checked to prevent pathname attacks.	The extension is assumed
 * to be safe.
 *
 * The result is a palloc'd string.
 */
static char *
zhprs_get_tsearch_config_filename(const char *basename,
							const char *extension)
{
	char		sharepath[MAXPGPATH];
	char	   *result;

	/*
	 * We limit the basename to contain a-z, 0-9, and underscores.	This may
	 * be overly restrictive, but we don't want to allow access to anything
	 * outside the tsearch_data directory, so for instance '/' *must* be
	 * rejected, and on some platforms '\' and ':' are risky as well. Allowing
	 * uppercase might result in incompatible behavior between case-sensitive
	 * and case-insensitive filesystems, and non-ASCII characters create other
	 * interesting risks, so on the whole a tight policy seems best.
	 */
	if (strspn(basename, "abcdefghijklmnopqrstuvwxyz0123456789_.") != strlen(basename))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("invalid text search configuration file name \"%s\"",
						basename)));

	get_share_path(my_exec_path, sharepath);
	result = palloc(MAXPGPATH);
	snprintf(result, MAXPGPATH, "%s/tsearch_data/%s.%s",
			 sharepath, basename, extension);

	return result;
}
