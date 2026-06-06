-- ===========================================================================
-- zhparser hardening regression tests
--
-- Tests are independent of dictionary tokenization output; they verify
-- the structural / behavioural fixes shipped in 2.4.
--
-- NOTE on GUC tests: zhparser.{extra_dicts,dict_in_memory} are PGC_BACKEND,
-- which means PostgreSQL itself rejects SET inside a session ("cannot be
-- set after connection start"). The path-traversal validation at the C
-- level is exercised at startup time; pg_regress cannot easily test it
-- without restarting backends. We instead verify the GUCs are registered
-- with the correct context.
-- ===========================================================================

CREATE EXTENSION IF NOT EXISTS zhparser;

-- ----- 1. lex types: y (modal) and z (status) must be present -----
-- Regression for the [a,x] truncation bug.
SELECT count(*) AS lex_type_count FROM ts_token_type('zhparser');
SELECT alias FROM ts_token_type('zhparser') WHERE alias IN ('y','z') ORDER BY alias;

-- ----- 2. GUC registration: 8 zhparser.* GUCs exist with expected contexts -
SELECT name, context, vartype
FROM pg_settings
WHERE name LIKE 'zhparser.%'
ORDER BY name;

-- ----- 3. Per-call state isolation -----
-- Two parser invocations side-by-side must not corrupt each other's
-- token streams. If the global-state bug from <2.4 were back, one of
-- these subqueries would observe the other's input.
WITH
    a AS (SELECT string_agg(token, ',') AS s FROM ts_parse('zhparser', 'hello')),
    b AS (SELECT string_agg(token, ',') AS s FROM ts_parse('zhparser', 'world'))
SELECT
    (a.s LIKE '%hello%') AS a_has_hello,
    (a.s LIKE '%world%') AS a_has_world,
    (b.s LIKE '%hello%') AS b_has_hello,
    (b.s LIKE '%world%') AS b_has_world
FROM a, b;

-- ----- 4. sync_zhprs_custom_word: regex guard must be active -----
SELECT
    (pg_get_functiondef(p.oid) LIKE '%format(%') AS uses_format_func,
    (pg_get_functiondef(p.oid) LIKE '%[A-Za-z0-9_]%') AS has_dbname_regex
FROM pg_proc p
JOIN pg_namespace n ON n.oid = p.pronamespace
WHERE n.nspname = 'public' AND p.proname = 'sync_zhprs_custom_word';

-- ----- 5. Session-scoped GUCs are still mutable -----
SET zhparser.punctuation_ignore = on;
SET zhparser.multi_short = on;
SET zhparser.multi_zall = on;
RESET ALL;
