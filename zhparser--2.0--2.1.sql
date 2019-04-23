drop function zhprs_getsharepath();

CREATE or REPLACE FUNCTION sync_zhprs_custom_word() RETURNS void LANGUAGE plpgsql AS
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
