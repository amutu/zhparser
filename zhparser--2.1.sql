CREATE FUNCTION zhprs_start(internal, int4)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION zhprs_getlexeme(internal, internal, internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION zhprs_end(internal)
RETURNS void
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION zhprs_lextype(internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE TEXT SEARCH PARSER zhparser (
    START    = zhprs_start,
    GETTOKEN = zhprs_getlexeme,
    END      = zhprs_end,
    HEADLINE = pg_catalog.prsd_headline,
    LEXTYPES = zhprs_lextype
);


CREATE SCHEMA zhparser;
CREATE TABLE zhparser.zhprs_custom_word(word text primary key, tf float default '1.0', idf float default '1.0', attr char default '@', check(attr = '@' or attr = '!'));

CREATE FUNCTION sync_zhprs_custom_word() RETURNS void LANGUAGE plpgsql AS
$$
declare
	database_oid text;
	data_dir text;
	dict_path text;
	time_tag_path text;
	query text;
begin
	select setting from pg_settings where name='data_directory' into data_dir;
	select oid from pg_database where datname=current_database() into database_oid;


	select data_dir || '/base/' || database_oid || '/zhprs_dict_' || current_database() || '.txt' into dict_path;
	select data_dir || '/base/' || database_oid || '/zhprs_dict_' || current_database() || '.tag' into time_tag_path;

	query = 'copy (select word, tf, idf, attr from zhparser.zhprs_custom_word) to ' || chr(39) || dict_path || chr(39) || ' encoding ' || chr(39) || 'utf8' || chr(39) ;
	execute query;
	query = 'copy (select now()) to ' || chr(39) || time_tag_path || chr(39) ;
	execute query;
end;
$$;

select sync_zhprs_custom_word();
