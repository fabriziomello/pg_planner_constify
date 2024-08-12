\set EXPLAIN 'EXPLAIN (analyze, costs off, timing off, summary off)'
\set NOW '2024-08-10 00:00:00-03'
SET timezone = 'America/Sao_Paulo';

SET pg_planner_constify.function = 'pg_catalog.now()';
:EXPLAIN SELECT now();

CREATE TABLE mock_now(v TIMESTAMPTZ);
INSERT INTO mock_now VALUES (:'NOW'::timestamp);
CREATE FUNCTION sql_now() RETURNS TIMESTAMPTZ AS
$$
BEGIN
    RETURN (SELECT v FROM mock_now LIMIT 1);
END;
$$
STABLE LANGUAGE plpgsql;

CREATE TABLE test_now(t TIMESTAMPTZ);
CREATE INDEX test_now_t_idx ON test_now(t);

INSERT INTO test_now(t)
SELECT * FROM generate_series(:'NOW'::timestamptz - interval '1 year', :'NOW'::timestamptz + '1 year', interval '1 hour');
VACUUM ANALYZE test_now;

:EXPLAIN SELECT * FROM test_now WHERE t = sql_now();
SET pg_planner_constify.function = 'public.sql_now()';
:EXPLAIN SELECT * FROM test_now WHERE t = sql_now();

RESET pg_planner_constify.function;

CREATE TABLE conditions (
    "time" TIMESTAMPTZ NOT NULL,
    device_id INTEGER, 
    temperature NUMERIC
) PARTITION BY RANGE ("time");

CREATE INDEX conditions_time_idx ON conditions("time");

CREATE TABLE conditions_2024_08_10 PARTITION OF conditions
    FOR VALUES FROM ('2024-08-10 00:00:00-03') TO ('2024-08-11 00:00:00-03');

CREATE TABLE conditions_2024_08_11 PARTITION OF conditions
    FOR VALUES FROM ('2024-08-11 00:00:00-03') TO ('2024-08-12 00:00:00-03');

CREATE TABLE conditions_2024_08_12 PARTITION OF conditions
    FOR VALUES FROM ('2024-08-12 00:00:00-03') TO ('2024-08-13 00:00:00-03');

INSERT INTO conditions("time", device_id, temperature)
SELECT "time", 1, 10
FROM generate_series('2024-08-10 00:00:00-03', '2024-08-12 23:59:59-03', interval '1 minute') AS "time";

VACUUM ANALYZE conditions;

-- Without constify
:EXPLAIN SELECT * FROM conditions WHERE "time" = sql_now();

-- With constify
SET pg_planner_constify.function = 'public.sql_now()';
:EXPLAIN SELECT * FROM conditions WHERE "time" = sql_now();

:EXPLAIN SELECT * FROM conditions WHERE "time" = sql_now() AND device_id = 1;
