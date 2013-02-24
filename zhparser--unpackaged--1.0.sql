-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION zhparser" to load this file. \quit

ALTER EXTENSION zhparser ADD function zhprs_start(internal,integer);
ALTER EXTENSION zhparser ADD function zhprs_getlexeme(internal,internal,internal);
ALTER EXTENSION zhparser ADD function zhprs_end(internal);
ALTER EXTENSION zhparser ADD function zhprs_lextype(internal);
ALTER EXTENSION zhparser ADD text search parser zhparser;
