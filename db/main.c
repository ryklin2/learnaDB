/*
 * main.c — the REPL (read-eval-print loop).                       [provided]
 *
 *   minidb              in-memory database (gone when you exit)
 *   minidb shop.db      persistent database file (needs M8)
 *   minidb < x.sql      run a script, one statement per line
 *
 * Lines starting with '.' are "meta-commands" handled here (.help, .tables,
 * .schema, .exit), the same convention the sqlite3 shell uses. Everything
 * else is SQL and goes to engine_execute().
 *
 * Stretch ideas: statements spanning several lines (keep reading until ';'),
 * .timer on/off to measure queries, .read FILE to run a script.
 */
#include "minidb.h"

#include <string.h>

static void print_help(void)
{
    puts("Meta-commands:\n"
         "  .help            this message\n"
         "  .tables          list tables\n"
         "  .schema [TABLE]  show table definitions\n"
         "  .exit            quit (also .quit or Ctrl-Z/Ctrl-D)\n"
         "\n"
         "SQL (one statement per line):\n"
         "  CREATE TABLE t (id INT, name TEXT);\n"
         "  INSERT INTO t VALUES (1, 'alice');\n"
         "  SELECT * FROM t WHERE id > 0 LIMIT 10;\n"
         "  SELECT name FROM t;   EXPLAIN SELECT ...;\n"
         "  DELETE FROM t WHERE id = 1;   DROP TABLE t;");
}

static void print_schema(const Table *t)
{
    printf("CREATE TABLE %s (", t->name);
    for (size_t c = 0; c < t->schema.ncolumns; c++)
        printf("%s%s %s", c ? ", " : "", t->schema.columns[c].name,
               data_type_name(t->schema.columns[c].type));
    printf(");\n");
}

/* Returns false when the REPL should exit. */
static bool run_meta_command(Database *db, char *line)
{
    char *arg = strchr(line, ' ');
    if (arg != NULL) {
        *arg++ = '\0';
        while (*arg == ' ') arg++;
    }

    if (strcmp(line, ".exit") == 0 || strcmp(line, ".quit") == 0) return false;

    if (strcmp(line, ".help") == 0) {
        print_help();
    } else if (strcmp(line, ".tables") == 0) {
        for (size_t i = 0; i < db->ntables; i++) printf("%s\n", db->tables[i]->name);
    } else if (strcmp(line, ".schema") == 0) {
        if (arg != NULL && *arg != '\0') {
            Table *t = db_find_table(db, arg);
            if (t != NULL) print_schema(t);
            else printf("error: no such table: %s\n", arg);
        } else {
            for (size_t i = 0; i < db->ntables; i++) print_schema(db->tables[i]);
        }
    } else {
        printf("unknown command: %s (try .help)\n", line);
    }
    return true;
}

/* Strip trailing whitespace/newline in place; return pointer past leading. */
static char *trim(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ' ||
                       s[len - 1] == '\t'))
        s[--len] = '\0';
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : NULL;

    Database db;
    MdbError err = {MDB_OK, ""};
    if (db_open(&db, path, &err) != MDB_OK) {
        fprintf(stderr, "error (%s): %s\n", mdb_status_name(err.status), err.message);
        return 1;
    }
    printf("MiniDB (%s). Type .help for help.\n", path ? path : "in-memory");

    char buf[MDB_MAX_INPUT_LEN];
    for (;;) {
        printf("minidb> ");
        fflush(stdout);
        if (fgets(buf, sizeof buf, stdin) == NULL) break;

        /* No newline and not at EOF means the line didn't fit: discard it. */
        if (strchr(buf, '\n') == NULL && !feof(stdin)) {
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF) {}
            printf("error: line longer than %d characters\n", MDB_MAX_INPUT_LEN - 1);
            continue;
        }

        char *line = trim(buf);
        if (*line == '\0') continue;
        if (*line == '.') {
            if (!run_meta_command(&db, line)) break;
            continue;
        }

        ResultSet rs;
        if (engine_execute(&db, line, &rs, &err) == MDB_OK)
            result_print(&rs, stdout);
        else
            printf("error (%s): %s\n", mdb_status_name(err.status), err.message);
        result_free(&rs);
    }

    db_close(&db);
    return 0;
}
