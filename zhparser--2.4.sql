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
CREATE TABLE zhparser.zhprs_custom_word(
    word text primary key,
    tf   float default '1.0',
    idf  float default '1.0',
    attr char  default '@',
    check (attr = '@' or attr = '!')
);

/*
 * sync_zhprs_custom_word
 *
 * 2.4 hardening: build the COPY statement via format() with %I/%L
 * placeholders and validate the database name. Refuses to run if
 * current_database() contains characters that are unsafe in a
 * filesystem path; this matches the C side, which also refuses to
 * load such paths.
 *
 * Requires superuser/pg_write_server_files privileges to actually
 * perform COPY ... TO 'filename', which is unchanged from prior
 * versions.
 */
CREATE FUNCTION sync_zhprs_custom_word() RETURNS void LANGUAGE plpgsql AS
$$
declare
    data_dir       text;
    db_name        text;
    dict_path      text;
    time_tag_path  text;
    query          text;
begin
    select setting from pg_settings where name = 'data_directory'
        into data_dir;
    if data_dir is null then
        raise exception 'zhparser: cannot resolve data_directory';
    end if;

    db_name := current_database();
    if db_name !~ '^[A-Za-z0-9_]+$' then
        raise exception 'zhparser: refusing to write custom dict for database name "%" (only [A-Za-z0-9_] allowed)', db_name;
    end if;

    dict_path     := data_dir || '/base/zhprs_dict_' || db_name || '.txt';
    time_tag_path := data_dir || '/base/zhprs_dict_' || db_name || '.tag';

    /*
     * %L on the path quotes it as a SQL string literal (handles single
     * quotes). %I is irrelevant here; COPY does not accept identifiers.
     * The encoding is hard-coded utf8.
     */
    query := format(
        'copy (select word, tf, idf, attr from zhparser.zhprs_custom_word) to %L encoding %L',
        dict_path, 'utf8');
    execute query;

    query := format('copy (select now()) to %L', time_tag_path);
    execute query;
end;
$$;

-- do not create custom dict files on fresh install
-- select sync_zhprs_custom_word();
