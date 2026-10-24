/*
 * engine.c — executes statements.                      [M4, M5, M6, M7]
 *
 * engine_execute() is the front door: SQL text in, ResultSet out.
 *
 *     parse_statement()
 *        |-- CREATE / DROP / INSERT / DELETE: run directly against storage
 *        '-- SELECT: optimizer_build_plan() -> run the plan (below)
 *     then, if the database has a file and something changed, db_save()
 *
 * THE ITERATOR ("VOLCANO") MODEL
 *   Every plan node supports two operations:
 *     exec_open(node)        reset its state (and its child's)
 *     exec_next(node, &row)  produce its next row, or NULL when finished
 *   A node gets its input by calling exec_next on its CHILD:
 *     SeqScan  returns table rows one by one
 *     Filter   calls child next() until a row passes the predicate
 *     Project  calls child next() once and reshapes the row
 *     Limit    calls child next() until it has returned N rows, then stops
 *   Rows are PULLED up through the tree one at a time, so
 *   `SELECT * FROM huge LIMIT 1` touches one row, not the whole table, and
 *   no operator needs memory for the full result. PostgreSQL's executor
 *   works this way (look at ExecProcNode). SQLite compiles to bytecode
 *   instead, and analytics engines use batches of rows ("vectorized").
 *   Those are worth reading about once this works.
 */
#include "minidb.h"

#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------- M5: expressions */

/*
 * TODO(M5): compare two non-NULL values of the same type.
 * INT: compare numerically. Don't `return a - b`: it can overflow!
 * TEXT: strcmp. (That's byte order, a "binary collation". Real databases
 * support locale-aware collations too: a deep rabbit hole.)
 */
int value_compare(Value a, Value b)
{
    (void)a;
    (void)b;
    return 0;
}

/*
 * TODO(M5): evaluate an expression. Recursive. Types were already checked by
 * bind_expr(), so mismatches here are bugs, not user errors.
 *
 *   LITERAL   *out = the literal (shallow copy; its text is borrowed)
 *   COLUMN    *out = row->values[index]  (shallow; borrowed from the row)
 *   + - * /   NULL if either side is NULL. Otherwise INT arithmetic.
 *             Division by zero -> MDB_ERR_RUNTIME. (SQLite returns NULL
 *             instead; both are defensible.) Stretch: detect overflow.
 *   unary -   NULL stays NULL
 *   = != ...  NULL if either side is NULL, else value_int(0 or 1) using
 *             value_compare()
 *   NOT       NULL -> NULL, otherwise 1 - truth
 *   AND / OR  THREE-VALUED LOGIC (N = NULL):
 *
 *                AND | 0  1  N         OR | 0  1  N
 *                ----+--------         ---+--------
 *                 0  | 0  0  0          0 | 0  1  N
 *                 1  | 0  1  N          1 | 1  1  1
 *                 N  | 0  N  N          N | N  1  N
 *
 *             Note `0 AND <anything>` is 0 even if <anything> is NULL. You
 *             may short-circuit (skip evaluating the right side), but then
 *             `0 AND 1/0` succeeds. Decide whether that's OK, and test it.
 *
 * A Filter keeps a row only if the predicate is exactly 1 (value_is_true).
 * NULL is "unknown", which is not true. That's why `WHERE x = NULL` matches
 * nothing in SQL, and why `IS NULL` exists (a nice stretch feature).
 */
MdbStatus eval_expr(const Expr *expr, const Row *row, Value *out, MdbError *err)
{
    (void)expr;
    (void)row;
    *out = value_null();
    return MDB_TODO(err, "eval_expr (M5)");
}

/* ------------------------------------------------------------ M4: executor */

/*
 * TODO(M4): reset a node and (recursively) its child before iteration.
 *   SeqScan: next_row = 0
 *   Limit:   emitted = 0             (M6)
 *   others:  nothing of their own, just open the child
 */
MdbStatus exec_open(PlanNode *node, MdbError *err)
{
    (void)node;
    return MDB_TODO(err, "exec_open (M4)");
}

/*
 * TODO(M4): produce the next row, or *out_row = NULL at the end. A switch on
 * node->kind:
 *   SeqScan (M4): if next_row < table->nrows, return &rows[next_row++].
 *   Filter  (M5): loop { child next; if NULL -> NULL; eval predicate;
 *                 if value_is_true -> return the row }
 *   Project (M6): child next; if not NULL, copy the chosen values (shallow,
 *                 borrowed) into node->as.project.out and return that.
 *   Limit   (M6): if emitted >= limit -> NULL (without calling the child!);
 *                 otherwise child next, and count it.
 */
MdbStatus exec_next(PlanNode *node, const Row **out_row, MdbError *err)
{
    (void)node;
    *out_row = NULL;
    return MDB_TODO(err, "exec_next (M4)");
}

/* -------------------------------------------------------------- statements */

/*
 * TODO(M4): CREATE TABLE -> db_create_table(), then set a message like
 * "Table created." in out->message.
 */
static MdbStatus execute_create(Database *db, Statement *stmt, ResultSet *out, MdbError *err)
{
    (void)db;
    (void)stmt;
    (void)out;
    return MDB_TODO(err, "CREATE TABLE (M4)");
}

/*
 * TODO(M4): INSERT -> find the table (MDB_ERR_NO_SUCH_TABLE if missing),
 * table_insert() the values, and set rows_affected = 1 and a message.
 */
static MdbStatus execute_insert(Database *db, Statement *stmt, ResultSet *out, MdbError *err)
{
    (void)db;
    (void)stmt;
    (void)out;
    return MDB_TODO(err, "INSERT (M4)");
}

/*
 * TODO(M4): SELECT.
 *   1. optimizer_build_plan()
 *   2. If stmt->explain: plan_explain(plan, out) and stop.        (M6)
 *   3. Copy the root's output_schema column names into out->column_names
 *      and set out->ncolumns.
 *   4. exec_open(), then loop exec_next() until NULL, calling
 *      result_add_row(). It COPIES the row, which is required: the row is
 *      only borrowed from the plan.
 *   5. plan_free(), on the error paths too!
 */
static MdbStatus execute_select(Database *db, Statement *stmt, ResultSet *out, MdbError *err)
{
    (void)db;
    (void)stmt;
    (void)out;
    return MDB_TODO(err, "SELECT (M4)");
}

/*
 * TODO(M7): DELETE FROM t [WHERE expr].
 *   1. Find the table, and bind_expr() the WHERE against its schema.
 *   2. Scan the rows and collect the indices where the WHERE is true (all
 *      rows if there's no WHERE) into a malloc'd array.
 *   3. table_delete_rows(), set rows_affected, free the array.
 * Two phases, "find then delete", for the reason explained at
 * table_delete_rows(). Stretch: route DELETE through the planner so it can
 * use an index (M9).
 */
static MdbStatus execute_delete(Database *db, Statement *stmt, ResultSet *out, MdbError *err)
{
    (void)db;
    (void)stmt;
    (void)out;
    return MDB_TODO(err, "DELETE (M7)");
}

/* TODO(M7): DROP TABLE -> db_drop_table(), plus a message. */
static MdbStatus execute_drop(Database *db, Statement *stmt, ResultSet *out, MdbError *err)
{
    (void)db;
    (void)stmt;
    (void)out;
    return MDB_TODO(err, "DROP TABLE (M7)");
}

/* ---------------------------------------------------------------- [provided] */

MdbStatus engine_execute(Database *db, const char *sql, ResultSet *out, MdbError *err)
{
    result_init(out);

    Statement stmt;
    MdbStatus st = parse_statement(sql, &stmt, err);
    if (st != MDB_OK) return st;

    switch (stmt.kind) {
    case STMT_CREATE_TABLE: st = execute_create(db, &stmt, out, err); break;
    case STMT_DROP_TABLE:   st = execute_drop(db, &stmt, out, err);   break;
    case STMT_INSERT:       st = execute_insert(db, &stmt, out, err); break;
    case STMT_SELECT:       st = execute_select(db, &stmt, out, err); break;
    case STMT_DELETE:       st = execute_delete(db, &stmt, out, err); break;
    }

    /* Durability (M8): once a change succeeds, write it to disk before
     * reporting success. Saving the whole file every time is slow but simple;
     * a write-ahead log is the professional answer (see docs/ROADMAP.md).
     * Question: if the save fails, the in-memory database has the change but
     * the file doesn't. What should happen then? */
    if (st == MDB_OK && stmt.kind != STMT_SELECT && db->path[0] != '\0')
        st = db_save(db, err);

    statement_free(&stmt);
    return st;
}
