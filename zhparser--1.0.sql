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
