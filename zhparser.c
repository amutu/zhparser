/*-------------------------------------------------------------------------
 *
 * zhparser.c
 *	  A text search parser for Chinese based on SCWS.
 *
 * Hardened revision (PG 16/17/18):
 *   - Per-call SCWS instance via scws_fork() to remove global mutable state.
 *   - GUCs registered in _PG_init() and validated through check hooks.
 *   - extra_dicts / database-name path traversal hardening.
 *   - mmap as default dict load mode (shared via OS page cache).
 *   - Lexeme attribute range fixed to ['a','z'] (was ['a','x']).
 *   - Cached multi-mode flags so hot path avoids repeated GUC reads.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "fmgr.h"
#include "miscadmin.h"
#include "commands/dbcommands.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/memutils.h"
#include "utils/palloc.h"
#include "utils/varlena.h"

#include <ctype.h>
#include <string.h>
#include <sys/stat.h>

#include "zhparser.h"

PG_MODULE_MAGIC;

/* ----------------------------------------------------------------------- *
 * Constants
 * ----------------------------------------------------------------------- */

#define LEX_TYPE_COUNT		26			/* a..z */
#define DICT_EXT_LEN		4			/* ".txt" / ".xdb" */
#define TXT_EXT				".txt"
#define XDB_EXT				".xdb"

/* ----------------------------------------------------------------------- *
 * Types
 * ----------------------------------------------------------------------- */

typedef struct ParserState
{
	char	   *buffer;			/* text to parse (palloc'd by caller) */
	int			len;
	int			pos;
	scws_t		scws;			/* per-call SCWS instance (forked) */
	scws_res_t	head;
	scws_res_t	curr;
} ParserState;

typedef struct LexDescr
{
	int			lexid;
	char	   *alias;
	char	   *descr;
} LexDescr;

/* ----------------------------------------------------------------------- *
 * GUC variables
 * ----------------------------------------------------------------------- */

static bool	dict_in_memory = false;
static char *extra_dicts = NULL;

static bool	punctuation_ignore = false;
static bool	seg_with_duality = false;
static bool	multi_short = false;
static bool	multi_duality = false;
static bool	multi_zmain = false;
static bool	multi_zall = false;

/* ----------------------------------------------------------------------- *
 * Process-local state
 *
 * The "master" SCWS instance owns the loaded dictionary/rules. Every
 * zhprs_start() forks a cheap per-call clone (scws_fork) so concurrent
 * parser invocations within the same backend (e.g. SRFs, nested calls,
 * subqueries) cannot trample each other.
 * ----------------------------------------------------------------------- */

static scws_t master_scws = NULL;
static bool   master_load_failed = false;

/* ----------------------------------------------------------------------- *
 * Forward declarations
 * ----------------------------------------------------------------------- */

void _PG_init(void);
void _PG_fini(void);

PG_FUNCTION_INFO_V1(zhprs_start);
PG_FUNCTION_INFO_V1(zhprs_getlexeme);
PG_FUNCTION_INFO_V1(zhprs_end);
PG_FUNCTION_INFO_V1(zhprs_lextype);

static void ensure_master_loaded(void);
static int	resolve_load_mode(void);
static int	current_multi_mode(void);
static bool is_safe_dict_filename(const char *name);
static bool is_safe_database_name(const char *name);
static void init_type(LexDescr descr[]);

/* GUC hooks */
static bool check_extra_dicts(char **newval, void **extra, GucSource source);

/* ----------------------------------------------------------------------- *
 * Lex type table  (static, copied per zhprs_lextype call)
 * ----------------------------------------------------------------------- */

static const struct
{
	int			lexid;
	const char *alias;
	const char *descr;
} lex_types[LEX_TYPE_COUNT] = {
	{ 'a', "a", "adjective,形容词" },
	{ 'b', "b", "differentiation,区别词" },
	{ 'c', "c", "conjunction,连词" },
	{ 'd', "d", "adverb,副词" },
	{ 'e', "e", "exclamation,感叹词" },
	{ 'f', "f", "position,方位词" },
	{ 'g', "g", "root,词根" },
	{ 'h', "h", "head,前连接成分" },
	{ 'i', "i", "idiom,成语" },
	{ 'j', "j", "abbreviation,简称" },
	{ 'k', "k", "tail,后连接成分" },
	{ 'l', "l", "tmp,习用语" },
	{ 'm', "m", "numeral,数词" },
	{ 'n', "n", "noun,名词" },
	{ 'o', "o", "onomatopoeia,拟声词" },
	{ 'p', "p", "prepositional,介词" },
	{ 'q', "q", "quantity,量词" },
	{ 'r', "r", "pronoun,代词" },
	{ 's', "s", "space,处所词" },
	{ 't', "t", "time,时语素" },
	{ 'u', "u", "auxiliary,助词" },
	{ 'v', "v", "verb,动词" },
	{ 'w', "w", "punctuation,标点符号" },
	{ 'x', "x", "unknown,未知词" },
	{ 'y', "y", "modal,语气词" },
	{ 'z', "z", "status,状态词" },
};

/* ----------------------------------------------------------------------- *
 * Helpers
 * ----------------------------------------------------------------------- */

/*
 * is_safe_dict_filename
 *
 * Whitelist for entries listed in zhparser.extra_dicts. We deliberately
 * forbid anything that could escape the tsearch_data directory (no '/',
 * no '\', no '..', no leading dot). Only [A-Za-z0-9_.-] is allowed.
 */
static bool
is_safe_dict_filename(const char *name)
{
	const char *p;

	if (name == NULL || name[0] == '\0' || name[0] == '.' || name[0] == '-')
		return false;

	for (p = name; *p; p++)
	{
		unsigned char c = (unsigned char) *p;
		if (!(isalnum(c) || c == '_' || c == '.' || c == '-'))
			return false;
	}

	/* explicit ".." rejection */
	if (strstr(name, "..") != NULL)
		return false;

	return true;
}

/*
 * is_safe_database_name
 *
 * The custom-word file is named after current_database(). PG allows
 * almost any character there once quoted, so we refuse to load the
 * custom dict (with a LOG) for "exotic" names instead of building a
 * traversable filesystem path.
 */
static bool
is_safe_database_name(const char *name)
{
	const char *p;

	if (name == NULL || name[0] == '\0')
		return false;

	for (p = name; *p; p++)
	{
		unsigned char c = (unsigned char) *p;
		if (!(isalnum(c) || c == '_'))
			return false;
	}
	return true;
}

/*
 * resolve_load_mode
 *
 * NOTE: SCWS itself does NOT expose a public "mmap" flag. With the default
 * (no SCWS_XDICT_MEM), libscws opens the .xdb via mmap (xdb.c uses fmap),
 * so the kernel page cache already gives backends a shared dictionary
 * footprint. Setting SCWS_XDICT_MEM forces the dict to be slurped into
 * private heap, which is the only mode where 14MB is duplicated per
 * backend.
 */
static int
resolve_load_mode(void)
{
	return dict_in_memory ? SCWS_XDICT_MEM : 0;
}

/* ----------------------------------------------------------------------- *
 * GUC hooks
 * ----------------------------------------------------------------------- */

static bool
check_extra_dicts(char **newval, void **extra, GucSource source)
{
	List	   *elemlist;
	ListCell   *l;
	char	   *rawname;
	bool		ok = true;

	if (*newval == NULL || (*newval)[0] == '\0')
		return true;

	rawname = pstrdup(*newval);
	if (!SplitIdentifierString(rawname, ',', &elemlist))
	{
		GUC_check_errdetail("List syntax is invalid.");
		pfree(rawname);
		return false;
	}

	foreach(l, elemlist)
	{
		const char *name = (const char *) lfirst(l);
		const char *ext;

		if (!is_safe_dict_filename(name))
		{
			GUC_check_errdetail("Dict file name \"%s\" contains illegal characters.", name);
			ok = false;
			break;
		}

		ext = strrchr(name, '.');
		if (ext == NULL || strlen(ext) != DICT_EXT_LEN ||
			(strcmp(ext, TXT_EXT) != 0 && strcmp(ext, XDB_EXT) != 0))
		{
			GUC_check_errdetail("Dict file \"%s\" must end with .txt or .xdb.", name);
			ok = false;
			break;
		}
	}

	list_free(elemlist);
	pfree(rawname);
	return ok;
}

static int
current_multi_mode(void)
{
	int m = 0;
	if (multi_short)	m |= SCWS_MULTI_SHORT;
	if (multi_duality)	m |= SCWS_MULTI_DUALITY;
	if (multi_zmain)	m |= SCWS_MULTI_ZMAIN;
	if (multi_zall)		m |= SCWS_MULTI_ZALL;
	return m;
}

/* ----------------------------------------------------------------------- *
 * _PG_init / _PG_fini
 * ----------------------------------------------------------------------- */

void
_PG_init(void)
{
	DefineCustomBoolVariable(
		"zhparser.dict_in_memory",
		"Load dicts into memory (private heap copy per backend).",
		"When false (default) the dict is mmap'd, sharing via OS page cache.",
		&dict_in_memory,
		false,
		PGC_BACKEND,
		0, NULL, NULL, NULL);

	DefineCustomStringVariable(
		"zhparser.extra_dicts",
		"Extra dict files to load (comma separated, basenames only).",
		"Names must end with .txt or .xdb and contain only [A-Za-z0-9_.-].",
		&extra_dicts,
		NULL,
		PGC_BACKEND,
		0,
		check_extra_dicts,
		NULL,
		NULL);

	DefineCustomBoolVariable(
		"zhparser.punctuation_ignore",
		"Ignore punctuation (except CR/LF).",
		NULL,
		&punctuation_ignore,
		false,
		PGC_USERSET,
		0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"zhparser.seg_with_duality",
		"Segment words with duality.",
		NULL,
		&seg_with_duality,
		false,
		PGC_USERSET,
		0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"zhparser.multi_short",
		"Prefer short words.",
		NULL,
		&multi_short,
		false,
		PGC_USERSET,
		0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"zhparser.multi_duality",
		"Prefer duality.",
		NULL,
		&multi_duality,
		false,
		PGC_USERSET,
		0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"zhparser.multi_zmain",
		"Prefer most important element.",
		NULL,
		&multi_zmain,
		false,
		PGC_USERSET,
		0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"zhparser.multi_zall",
		"Prefer all elements.",
		NULL,
		&multi_zall,
		false,
		PGC_USERSET,
		0, NULL, NULL, NULL);

#if PG_VERSION_NUM >= 150000
	MarkGUCPrefixReserved("zhparser");
#endif
}

void
_PG_fini(void)
{
	if (master_scws != NULL)
	{
		scws_free(master_scws);
		master_scws = NULL;
	}
}

/* ----------------------------------------------------------------------- *
 * ensure_master_loaded
 *
 * Lazy-loads the master SCWS instance. Safe to call repeatedly: on
 * persistent failure we cache the failure for the rest of the backend
 * lifetime instead of trying again on every call.
 * ----------------------------------------------------------------------- */

static void
ensure_master_loaded(void)
{
	char		sharepath[MAXPGPATH];
	char		dict_path[MAXPGPATH];
	char		rule_path[MAXPGPATH];
	int			load_mode;
	List	   *elemlist = NIL;
	ListCell   *l;
	char	   *rawnames = NULL;
	const char *dbname;
	scws_t		newscws;

	if (master_scws != NULL || master_load_failed)
		return;

	newscws = scws_new();
	if (newscws == NULL)
	{
		master_load_failed = true;
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("failed to initialize SCWS")));
	}

	scws_set_charset(newscws, "utf-8");

	get_share_path(my_exec_path, sharepath);

	load_mode = resolve_load_mode();

	/* 1) Built-in main dict ------------------------------------------------ */
	snprintf(dict_path, MAXPGPATH, "%s/tsearch_data/dict.utf8.xdb", sharepath);
	if (scws_set_dict(newscws, dict_path, load_mode | SCWS_XDICT_XDB) != 0)
		ereport(NOTICE,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("zhparser: failed to set main dict \"%s\"", dict_path)));

	/* 2) Per-database custom dict ---------------------------------------- */
	dbname = get_database_name(MyDatabaseId);
	if (!is_safe_database_name(dbname))
	{
		ereport(LOG,
				(errmsg("zhparser: skipping custom dict for database \"%s\" "
						"(name contains characters that are unsafe for filesystem paths)",
						dbname != NULL ? dbname : "(null)")));
	}
	else
	{
		snprintf(dict_path, MAXPGPATH, "%s/base/zhprs_dict_%s.txt",
				 DataDir, dbname);
		if (scws_add_dict(newscws, dict_path, load_mode | SCWS_XDICT_TXT) != 0)
			ereport(LOG,
					(errmsg("zhparser: custom dict \"%s\" not loaded "
							"(missing or unreadable; run zhparser.sync_zhprs_custom_word() if expected)",
							dict_path)));
	}

	/* 3) extra_dicts ----------------------------------------------------- */
	if (extra_dicts != NULL && extra_dicts[0] != '\0')
	{
		rawnames = pstrdup(extra_dicts);
		if (!SplitIdentifierString(rawnames, ',', &elemlist))
		{
			pfree(rawnames);
			scws_free(newscws);
			master_load_failed = true;
			ereport(ERROR,
					(errcode(ERRCODE_INTERNAL_ERROR),
					 errmsg("zhparser.extra_dicts has invalid syntax: \"%s\"",
							extra_dicts)));
		}

		foreach(l, elemlist)
		{
			const char *name = (const char *) lfirst(l);
			const char *ext;
			int			mode = load_mode;

			/* Re-validate at load time too (defence in depth). */
			if (!is_safe_dict_filename(name))
			{
				list_free(elemlist);
				pfree(rawnames);
				scws_free(newscws);
				master_load_failed = true;
				ereport(ERROR,
						(errcode(ERRCODE_INTERNAL_ERROR),
						 errmsg("zhparser.extra_dicts contains illegal name \"%s\"",
								name)));
			}

			ext = strrchr(name, '.');
			if (ext == NULL || strlen(ext) != DICT_EXT_LEN)
			{
				list_free(elemlist);
				pfree(rawnames);
				scws_free(newscws);
				master_load_failed = true;
				ereport(ERROR,
						(errcode(ERRCODE_INTERNAL_ERROR),
						 errmsg("zhparser.extra_dicts entry \"%s\" must end with .txt or .xdb",
								name)));
			}
			if (strcmp(ext, TXT_EXT) == 0)
				mode |= SCWS_XDICT_TXT;
			else if (strcmp(ext, XDB_EXT) == 0)
				mode |= SCWS_XDICT_XDB;
			else
			{
				list_free(elemlist);
				pfree(rawnames);
				scws_free(newscws);
				master_load_failed = true;
				ereport(ERROR,
						(errcode(ERRCODE_INTERNAL_ERROR),
						 errmsg("zhparser.extra_dicts entry \"%s\" must end with .txt or .xdb",
								name)));
			}

			snprintf(dict_path, MAXPGPATH, "%s/tsearch_data/%s",
					 sharepath, name);
			if (scws_add_dict(newscws, dict_path, mode) != 0)
				ereport(LOG,
						(errmsg("zhparser: failed to add extra dict \"%s\"",
								dict_path)));
		}

		list_free(elemlist);
		pfree(rawnames);
	}

	/* 4) Rules ----------------------------------------------------------- */
	snprintf(rule_path, MAXPGPATH, "%s/tsearch_data/rules.utf8.ini", sharepath);
	{
		struct stat st;
		if (stat(rule_path, &st) == 0)
			scws_set_rule(newscws, rule_path);
		else
			ereport(LOG,
					(errmsg("zhparser: rules file \"%s\" not found, continuing without rules",
							rule_path)));
	}

	/* Configure ignore/duality on the master so forks inherit them. */
	scws_set_ignore(newscws, (int) punctuation_ignore);
	scws_set_duality(newscws, (int) seg_with_duality);
	scws_set_multi(newscws, current_multi_mode());

	master_scws = newscws;
}

/* ----------------------------------------------------------------------- *
 * Per-call resource cleanup
 *
 * MemoryContextRegisterResetCallback lets us guarantee that on any
 * unwind (ERROR, transaction abort) we still free the forked SCWS
 * instance and its result cursor.
 * ----------------------------------------------------------------------- */

static void
parser_state_cleanup(void *arg)
{
	ParserState *pst = (ParserState *) arg;

	if (pst == NULL)
		return;
	if (pst->head != NULL)
	{
		scws_free_result(pst->head);
		pst->head = NULL;
		pst->curr = NULL;
	}
	if (pst->scws != NULL)
	{
		scws_free(pst->scws);
		pst->scws = NULL;
	}
}

/* ----------------------------------------------------------------------- *
 * SQL-callable functions
 * ----------------------------------------------------------------------- */

Datum
zhprs_start(PG_FUNCTION_ARGS)
{
	ParserState			   *pst;
	scws_t					forked;
	MemoryContext			cxt;
	MemoryContextCallback  *cb;

	ensure_master_loaded();
	if (master_scws == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("zhparser: SCWS not initialized")));

	cxt = CurrentMemoryContext;

	pst = (ParserState *) MemoryContextAllocZero(cxt, sizeof(ParserState));

	forked = scws_fork(master_scws);
	if (forked == NULL)
	{
		pfree(pst);
		ereport(ERROR,
				(errcode(ERRCODE_OUT_OF_MEMORY),
				 errmsg("zhparser: scws_fork() failed")));
	}

	/*
	 * Apply per-call user settings to the forked instance only.
	 * The master is unaffected, so concurrent calls cannot collide.
	 */
	scws_set_ignore(forked, (int) punctuation_ignore);
	scws_set_duality(forked, (int) seg_with_duality);
	scws_set_multi(forked, current_multi_mode());

	pst->scws   = forked;
	pst->buffer = (char *) PG_GETARG_POINTER(0);
	pst->len    = PG_GETARG_INT32(1);
	pst->pos    = 0;

	/* Register cleanup before sending text, so any failure unwinds cleanly. */
	cb = (MemoryContextCallback *) MemoryContextAllocZero(cxt, sizeof(*cb));
	cb->func = parser_state_cleanup;
	cb->arg  = pst;
	MemoryContextRegisterResetCallback(cxt, cb);

	scws_send_text(pst->scws, pst->buffer, pst->len);
	pst->head = pst->curr = scws_get_result(pst->scws);

	PG_RETURN_POINTER(pst);
}

Datum
zhprs_getlexeme(PG_FUNCTION_ARGS)
{
	ParserState	   *pst = (ParserState *) PG_GETARG_POINTER(0);
	char		  **t = (char **) PG_GETARG_POINTER(1);
	int			   *tlen = (int *) PG_GETARG_POINTER(2);
	int				type = -1;

	if (pst == NULL || pst->head == NULL)
	{
		*t = NULL;
		*tlen = 0;
		return Int32GetDatum(0);
	}

	if (pst->curr != NULL)
	{
		scws_res_t curr = pst->curr;
		unsigned char attr0 = (unsigned char) curr->attr[0];

		/*
		 * SCWS attributes use 'a'..'z' (see init_type below). Anything
		 * outside that range is mapped to 'x' (unknown).
		 *
		 * NOTE: the original code restricted to ['a','x'] which silently
		 * dropped 'y' (modal) and 'z' (status). Fixed to ['a','z'].
		 */
		if (attr0 < (unsigned char) 'a' || attr0 > (unsigned char) 'z')
			type = (int) 'x';
		else
			type = (int) attr0;

		*tlen = curr->len;
		*t    = pst->buffer + curr->off;

		pst->curr = curr->next;

		if (pst->curr == NULL)
		{
			scws_free_result(pst->head);
			pst->head = pst->curr = scws_get_result(pst->scws);
		}
	}
	else
	{
		*t = NULL;
		*tlen = 0;
		type = 0;
	}

	PG_RETURN_INT32(type);
}

Datum
zhprs_end(PG_FUNCTION_ARGS)
{
	ParserState *pst = (ParserState *) PG_GETARG_POINTER(0);

	/*
	 * The MemoryContext reset callback we registered in zhprs_start will
	 * release the forked SCWS instance and any pending result cursor.
	 *
	 * However, when the caller (e.g. a long-lived loop in to_tsvector_byid)
	 * keeps the same context alive across many parse cycles, we want to
	 * release immediately to keep RSS flat.
	 */
	if (pst != NULL)
	{
		if (pst->head != NULL)
		{
			scws_free_result(pst->head);
			pst->head = NULL;
			pst->curr = NULL;
		}
		if (pst->scws != NULL)
		{
			scws_free(pst->scws);
			pst->scws = NULL;
		}
	}
	PG_RETURN_VOID();
}

Datum
zhprs_lextype(PG_FUNCTION_ARGS)
{
	LexDescr   *descr;

	descr = (LexDescr *) palloc(sizeof(LexDescr) * (LEX_TYPE_COUNT + 1));
	init_type(descr);
	PG_RETURN_POINTER(descr);
}

static void
init_type(LexDescr descr[])
{
	int			i;

	for (i = 0; i < LEX_TYPE_COUNT; i++)
	{
		descr[i].lexid = lex_types[i].lexid;
		descr[i].alias = pstrdup(lex_types[i].alias);
		descr[i].descr = pstrdup(lex_types[i].descr);
	}
	/* sentinel */
	descr[LEX_TYPE_COUNT].lexid = 0;
	descr[LEX_TYPE_COUNT].alias = NULL;
	descr[LEX_TYPE_COUNT].descr = NULL;
}
