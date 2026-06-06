/*
 * 2.3 -> 2.4
 *
 * - Replace string concatenation in sync_zhprs_custom_word() with
 *   format() / %L to mitigate path injection through current_database()
 *   and embedded quotes.
 * - Validate database name characters before writing the dict file.
 *
 * Existing dict files on disk are unaffected.
 */

CREATE OR REPLACE FUNCTION sync_zhprs_custom_word() RETURNS void LANGUAGE plpgsql AS
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

    query := format(
        'copy (select word, tf, idf, attr from zhparser.zhprs_custom_word) to %L encoding %L',
        dict_path, 'utf8');
    execute query;

    query := format('copy (select now()) to %L', time_tag_path);
    execute query;
end;
$$;
