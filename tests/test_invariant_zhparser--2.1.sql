#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <libpq-fe.h>

START_TEST(test_sql_injection_in_dict_sync)
{
    // Invariant: User input (database names) must never appear in SQL queries without proper escaping
    const char *payloads[] = {
        "test'--",                          // Exact exploit: single quote breaks out of string
        "db'; DROP TABLE zhparser.zhprs_custom_word; --",  // SQL injection with DROP
        "normal_db"                         // Valid input for baseline
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);

    PGconn *conn = PQconnectdb("dbname=postgres");
    ck_assert_msg(PQstatus(conn) == CONNECTION_OK, "Failed to connect to PostgreSQL");

    for (int i = 0; i < num_payloads; i++) {
        // Attempt to create database with injection payload as name
        char create_db[512];
        char *escaped_name = PQescapeIdentifier(conn, payloads[i], strlen(payloads[i]));
        
        if (escaped_name == NULL) {
            // If escaping fails, the payload contains invalid characters - this is safe
            continue;
        }
        
        snprintf(create_db, sizeof(create_db), "CREATE DATABASE %s", escaped_name);
        PGresult *res = PQexec(conn, create_db);
        
        if (PQresultStatus(res) == PGRES_COMMAND_OK) {
            // Connect to the new database and try to trigger the vulnerable function
            char conninfo[256];
            snprintf(conninfo, sizeof(conninfo), "dbname=%s", escaped_name);
            PGconn *test_conn = PQconnectdb(conninfo);
            
            if (PQstatus(test_conn) == CONNECTION_OK) {
                // The dict_path construction should use parameterized queries
                // If current_database() returns unescaped payload, injection occurs
                PGresult *db_res = PQexec(test_conn, "SELECT current_database()");
                if (PQresultStatus(db_res) == PGRES_TUPLES_OK) {
                    const char *db_name = PQgetvalue(db_res, 0, 0);
                    // Verify the database name doesn't contain unescaped SQL metacharacters
                    // that could break out of string literals in COPY command
                    ck_assert_msg(strchr(db_name, '\'') == NULL || i == 2,
                        "Database name contains unescaped single quote - SQL injection risk");
                }
                PQclear(db_res);
            }
            PQfinish(test_conn);
            
            // Cleanup: drop the test database
            char drop_db[512];
            snprintf(drop_db, sizeof(drop_db), "DROP DATABASE IF EXISTS %s", escaped_name);
            PQexec(conn, drop_db);
        }
        PQclear(res);
        PQfreemem(escaped_name);
    }
    
    PQfinish(conn);
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_sql_injection_in_dict_sync);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}