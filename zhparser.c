/*-------------------------------------------------------------------------
 *
 * zhparser.c
 *	  a text search parser for Chinese
 *
 *-------------------------------------------------------------------------
 */
#include "zhparser.h"

#include "postgres.h"
#include "miscadmin.h"
#include "fmgr.h"
#include "utils/guc.h"
#include "utils/builtins.h"
#include "commands/dbcommands.h"

/* dict file extension */
#define TXT_EXT ".txt"
#define XDB_EXT ".xdb"
/* length of file extension */
#define EXT_LEN 4

PG_MODULE_MAGIC;
/*
 * types
 */

/* self-defined type */
typedef struct
{
	char	   *buffer;			/* text to parse */
	int		len;			/* length of the text in buffer */
	int		pos;			/* position of the parser */
	scws_t scws;
	scws_res_t head;
	scws_res_t curr;
} ParserState;

/* copy-paste from wparser.h of tsearch2 */
typedef struct
{
	int			lexid;
	char	   *alias;
	char	   *descr;
} LexDescr;

static void init();

static void init_type(LexDescr descr[]);

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

static scws_t scws = NULL;
static ParserState parser_state;

/* config */
static bool dict_in_memory = false;
static char * extra_dicts = NULL;

static bool punctuation_ignore = false;
static bool seg_with_duality = false;
static bool multi_short = false;
static bool multi_duality = false;
static bool multi_zmain = false;
static bool multi_zall = false;

static void init(){
	char sharepath[MAXPGPATH];
	char dict_path[MAXPGPATH];
	char rule_path[MAXPGPATH];
	int load_dict_mem_mode = 0x0;

	List *elemlist;
	ListCell *l;

	if (!(scws = scws_new())) {
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("Failed to init Chinese Parser Lib SCWS!\"%s\"",""
				       )));
	}
	
	DefineCustomBoolVariable(
		"zhparser.dict_in_memory",
		"load dicts into memory",
		"load dicts into memory",
		&dict_in_memory,
		false,
		PGC_BACKEND,
		0,
		NULL,
		NULL,
		NULL
		);
	DefineCustomStringVariable(
		"zhparser.extra_dicts",
		"extra dicts files to load",
		"extra dicts files to load",
		&extra_dicts,
		NULL,
		PGC_BACKEND,
		0,
		NULL,
		NULL,
		NULL
		);
	DefineCustomBoolVariable(
		"zhparser.punctuation_ignore",
		"set if zhparser ignores the puncuation",
		"set if zhparser ignores the puncuation,except \\r and \\n",
		&punctuation_ignore,
		false,
		PGC_USERSET,
		0,
		NULL,
		NULL,
		NULL
		);

	DefineCustomBoolVariable(
		"zhparser.seg_with_duality",
		"segment words with duality",
		"segment words with duality",
		&seg_with_duality,
		false,
		PGC_USERSET,
		0,
		NULL,
		NULL,
		NULL
		);
	DefineCustomBoolVariable(
		"zhparser.multi_short",
		"prefer short words",
		"prefer short words",
		&multi_short,
		false,
		PGC_USERSET,
		0,
		NULL,
		NULL,
		NULL
		);
	DefineCustomBoolVariable(
		"zhparser.multi_duality",
		"prefer duality",
		"prefer duality",
		&multi_duality,
		false,
		PGC_USERSET,
		0,
		NULL,
		NULL,
		NULL
		);
	DefineCustomBoolVariable(
		"zhparser.multi_zmain",
		"prefer most important element",
		"prefer most important element",
		&multi_zmain,
		false,
		PGC_USERSET,
		0,
		NULL,
		NULL,
		NULL
		);
	DefineCustomBoolVariable(
		"zhparser.multi_zall",
		"prefer all element",
		"prefer all element",
		&multi_zall,
		false,
		PGC_USERSET,
		0,
		NULL,
		NULL,
		NULL
		);

	get_share_path(my_exec_path, sharepath);
	snprintf(dict_path, MAXPGPATH, "%s/tsearch_data/%s.%s",
			sharepath, "dict.utf8", "xdb");
	scws_set_charset(scws, "utf-8");

	if(dict_in_memory)
	    load_dict_mem_mode = SCWS_XDICT_MEM;

	/* ignore error,default dict is xdb */
	if( scws_set_dict(scws,dict_path,load_dict_mem_mode | SCWS_XDICT_XDB ) != 0){
	    ereport(NOTICE,
		    (errcode(ERRCODE_INTERNAL_ERROR),
		     errmsg("zhparser set dict : \"%s\" failed!",dict_path
			 )));
	}

	snprintf(dict_path, MAXPGPATH, "%s/base/%u/zhprs_dict_%s.txt",
			DataDir, MyDatabaseId, get_database_name(MyDatabaseId));
	if(scws_add_dict(scws, dict_path, load_dict_mem_mode | SCWS_XDICT_TXT) != 0 ){
		ereport(NOTICE,
			    (errcode(ERRCODE_INTERNAL_ERROR),
			     errmsg("zhparser add dict : \"%s\" failed!",dict_path
				 )));
	}

	if(extra_dicts != NULL){
	    if(!SplitIdentifierString(extra_dicts,',',&elemlist)){
		scws_free(scws);
		list_free(elemlist);
		scws = NULL;
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("zhparser.extra_dicts syntax error! extra_dicts is \"%s\"",extra_dicts
				       )));
	    }

	    foreach(l,elemlist){
		int load_dict_mode = load_dict_mem_mode;
		char * ext = strrchr((char*)lfirst(l),'.');
		if(ext != NULL && strlen(ext) == EXT_LEN){
		    if(strncmp(ext,TXT_EXT,EXT_LEN) == 0){
			load_dict_mode |= SCWS_XDICT_TXT;
		    }
		    else if(strncmp(ext,XDB_EXT,EXT_LEN) == 0){
			load_dict_mode |= SCWS_XDICT_XDB;
		    }
		}

		if(((load_dict_mode & SCWS_XDICT_TXT) == 0) &&
			((load_dict_mode & SCWS_XDICT_XDB) == 0)){
			scws_free(scws);
			list_free(elemlist);
			scws = NULL;
			ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("zhparser.extra_dicts setting error,the file name must end with .txt or .xdb! error file name is \"%s\"",(char*)lfirst(l)
				     )));
		
		}

		snprintf(dict_path, MAXPGPATH, "%s/tsearch_data/%s",
			sharepath, (char*)lfirst(l));
		/* ignore error*/
		if( scws_add_dict(scws,dict_path,load_dict_mode) != 0 ){
		    ereport(NOTICE,
			    (errcode(ERRCODE_INTERNAL_ERROR),
			     errmsg("zhparser add dict : \"%s\" failed!",dict_path
				 )));
		}
	    }
	    list_free(elemlist);
	}

	snprintf(rule_path, MAXPGPATH, "%s/tsearch_data/%s.%s",
			sharepath, "rules.utf8", "ini");
	scws_set_rule(scws ,rule_path);
}

/*
 * functions
 */

Datum
zhprs_start(PG_FUNCTION_ARGS)
{
	ParserState *pst = &parser_state;
	int multi_mode = 0x0;

	if(scws == NULL)
		init();
	pst -> scws = scws;
	pst -> buffer = (char *) PG_GETARG_POINTER(0);
	pst -> len = PG_GETARG_INT32(1);
	pst -> pos = 0;

	scws_set_ignore(scws, (int)punctuation_ignore);
	scws_set_duality(scws,(int)seg_with_duality);

	if(multi_short){
	    multi_mode |= SCWS_MULTI_SHORT;
	}

	if(multi_duality){
	    multi_mode |= SCWS_MULTI_DUALITY;
	}

	if(multi_zmain){
	    multi_mode |= SCWS_MULTI_ZMAIN;
	}

	if(multi_zall){
	    multi_mode |= SCWS_MULTI_ZALL;
	}

	scws_set_multi(scws,multi_mode);

	scws_send_text(pst -> scws, pst -> buffer, pst -> len);

	(pst -> head) = (pst -> curr) = scws_get_result(pst -> scws);

	PG_RETURN_POINTER(pst);
}

Datum
zhprs_getlexeme(PG_FUNCTION_ARGS)
{
	ParserState *pst = (ParserState *) PG_GETARG_POINTER(0);
	char	  **t = (char **) PG_GETARG_POINTER(1);
	int		   *tlen = (int *) PG_GETARG_POINTER(2);
	int			type = -1;

	if((pst -> head) == NULL ) /* already done the work,or no sentence */
	{
		*tlen = 0;
		type = 0;
	}
	/* have results */
	else if(pst -> curr != NULL)
	{
		scws_res_t  curr = pst -> curr;

		/*
 		* check the first char to determine the lextype
 		* if out of [0,25],then set to 'x',mean unknown type 
 		* so for Ag,Dg,Ng,Tg,Vg,the type will be unknown
 		* for full attr explanation,visit http://www.xunsearch.com/scws/docs.php#attr
		*/
		type = (int)(curr -> attr)[0];
		if(type > (int)'x' || type < (int)'a')
		    type = (int)'x';
		*tlen = curr -> len;
		*t = pst -> buffer + curr -> off;

		pst -> curr = curr -> next;

		/* fetch the next sentence */
		if(pst -> curr == NULL ){
			scws_free_result(pst -> head);
			(pst -> head) =	(pst -> curr) = scws_get_result(pst -> scws);
		}
	}

	PG_RETURN_INT32(type);
}

Datum
zhprs_end(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}

Datum
zhprs_lextype(PG_FUNCTION_ARGS)
{
	LexDescr *descr = (LexDescr *) palloc(sizeof(LexDescr) * (26 + 1));
	init_type(descr);

	PG_RETURN_POINTER(descr);
}

static void init_type(LexDescr descr[]){
	/* 
	* there are 26 types in this parser,alias from a to z
	* for full attr explanation,visit http://www.xunsearch.com/scws/docs.php#attr
	*/
	descr[0].lexid = 97;
	descr[0].alias = pstrdup("a");
	descr[0].descr = pstrdup("adjective,形容词");
	descr[1].lexid = 98;
	descr[1].alias = pstrdup("b");
	descr[1].descr = pstrdup("differentiation,区别词");
	descr[2].lexid = 99;
	descr[2].alias = pstrdup("c");
	descr[2].descr = pstrdup("conjunction,连词");
	descr[3].lexid = 100;
	descr[3].alias = pstrdup("d");
	descr[3].descr = pstrdup("adverb,副词");
	descr[4].lexid = 101;
	descr[4].alias = pstrdup("e");
	descr[4].descr = pstrdup("exclamation,感叹词");
	descr[5].lexid = 102;
	descr[5].alias = pstrdup("f");
	descr[5].descr = pstrdup("position,方位词");
	descr[6].lexid = 103;
	descr[6].alias = pstrdup("g");
	descr[6].descr = pstrdup("root,词根");
	descr[7].lexid = 104;
	descr[7].alias = pstrdup("h");
	descr[7].descr = pstrdup("head,前连接成分");
	descr[8].lexid = 105;
	descr[8].alias = pstrdup("i");
	descr[8].descr = pstrdup("idiom,成语");
	descr[9].lexid = 106;
	descr[9].alias = pstrdup("j");
	descr[9].descr = pstrdup("abbreviation,简称");
	descr[10].lexid = 107;
	descr[10].alias = pstrdup("k");
	descr[10].descr = pstrdup("tail,后连接成分");
	descr[11].lexid = 108;
	descr[11].alias = pstrdup("l");
	descr[11].descr = pstrdup("tmp,习用语");
	descr[12].lexid = 109;
	descr[12].alias = pstrdup("m");
	descr[12].descr = pstrdup("numeral,数词");
	descr[13].lexid = 110;
	descr[13].alias = pstrdup("n");
	descr[13].descr = pstrdup("noun,名词");
	descr[14].lexid = 111;
	descr[14].alias = pstrdup("o");
	descr[14].descr = pstrdup("onomatopoeia,拟声词");
	descr[15].lexid = 112;
	descr[15].alias = pstrdup("p");
	descr[15].descr = pstrdup("prepositional,介词");
	descr[16].lexid = 113;
	descr[16].alias = pstrdup("q");
	descr[16].descr = pstrdup("quantity,量词");
	descr[17].lexid = 114;
	descr[17].alias = pstrdup("r");
	descr[17].descr = pstrdup("pronoun,代词");
	descr[18].lexid = 115;
	descr[18].alias = pstrdup("s");
	descr[18].descr = pstrdup("space,处所词");
	descr[19].lexid = 116;
	descr[19].alias = pstrdup("t");
	descr[19].descr = pstrdup("time,时语素");
	descr[20].lexid = 117;
	descr[20].alias = pstrdup("u");
	descr[20].descr = pstrdup("auxiliary,助词");
	descr[21].lexid = 118;
	descr[21].alias = pstrdup("v");
	descr[21].descr = pstrdup("verb,动词");
	descr[22].lexid = 119;
	descr[22].alias = pstrdup("w");
	descr[22].descr = pstrdup("punctuation,标点符号");
	descr[23].lexid = 120;
	descr[23].alias = pstrdup("x");
	descr[23].descr = pstrdup("unknown,未知词");
	descr[24].lexid = 121;
	descr[24].alias = pstrdup("y");
	descr[24].descr = pstrdup("modal,语气词");
	descr[25].lexid = 122;
	descr[25].alias = pstrdup("z");
	descr[25].descr = pstrdup("status,状态词");
	descr[26].lexid = 0;
}
//TODO :headline function
