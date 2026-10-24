/*
 * optimizer.c — turns a parsed SELECT into an executable plan.  [M4, M5, M6]
 *
 * Real query planners work in three phases. We do the same, in miniature:
 *
 * 1. BIND (semantic analysis / name resolution)
 *    The parser only checked SHAPE: "SELECT x FROM y" is well-formed even if
 *    y doesn't exist. Binding checks MEANING. Does the table exist? Does each
 *    column? Do the types make sense (`age > 'bob'` doesn't)? It also turns
 *    column NAMES into column INDEXES (Expr.as.column.index), so the executor
 *    never compares strings once per row.
 *
 * 2. BUILD a plan: a tree of relational operators.
 *        SELECT name FROM users WHERE age > 30 LIMIT 5
 *    becomes
 *        Limit 5
 *          -> Project name
 *            -> Filter (age > 30)
 *              -> SeqScan users
 *    Rows flow upward, one at a time (engine.c explains how).
 *
 * 3. REWRITE (the actual "optimizing"): rules that produce an equivalent
 *    but cheaper plan. Start with constant folding. Later, with indexes,
 *    the big win is replacing SeqScan + Filter(id = 5) with an IndexScan.
 *    With joins, it's choosing the join order. That's what makes real
 *    optimizers hard, and interesting.
 *
 * OWNERSHIP
 *    build_select_plan() MOVES stmt->as.select.where into the Filter node.
 *    It sets the Statement's pointer to NULL so the expression is freed once,
 *    by plan_free(), not twice.
 */
#include "minidb.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- binding */

/*
 * TODO(M5): bind and type-check an expression. Recursive:
 *
 *   LITERAL   type = literal's type (a NULL literal is TYPE_NULL, which is
 *             compatible with everything below)
 *   COLUMN    index = schema_find_column(); missing -> MDB_ERR_NO_SUCH_COLUMN
 *             type  = that column's type
 *   + - * /   both sides INT (or NULL) -> INT, otherwise MDB_ERR_TYPE
 *   unary -   operand INT (or NULL)    -> INT
 *   = != < <= > >=
 *             both sides the same type, or either NULL -> INT (a boolean)
 *             otherwise MDB_ERR_TYPE (`name > 5`)
 *   AND OR NOT
 *             operands INT (or NULL) -> INT
 *
 * Good messages matter: "cannot compare TEXT with INT in name > 5" beats
 * "type error". data_type_name() helps.
 */
MdbStatus bind_expr(const Schema *schema, Expr *expr, DataType *out_type, MdbError *err)
{
    (void)schema;
    (void)expr;
    (void)out_type;
    return MDB_TODO(err, "bind_expr (M5)");
}

/*
 * Check that the SELECT makes sense against the database.
 *
 * TODO(M4): look up the table. Missing -> MDB_ERR_NO_SUCH_TABLE.
 * TODO(M5): if there's a WHERE, bind_expr() it against the table's schema.
 *           (Should a WHERE whose type is TEXT be an error? `WHERE name`?
 *           Decide and enforce it here.)
 * TODO(M6): if !star, check every selected column exists
 *           (-> MDB_ERR_NO_SUCH_COLUMN).
 */
static MdbStatus bind_select(Database *db, Statement *stmt, Table **out_table, MdbError *err)
{
    (void)db;
    (void)stmt;
    *out_table = NULL;
    return MDB_TODO(err, "bind_select (M4)");
}

/* ------------------------------------------------------------ plan building */

/*
 * Build the operator tree, bottom-up. Each node's output_schema says what it
 * produces, and the root's output_schema becomes the result's column names.
 *
 * TODO(M4): SeqScan of `table`. output_schema = the table's schema.
 * TODO(M5): if there's a WHERE, put a Filter on top. Move the expression in
 *           and set stmt->as.select.where = NULL. Filter doesn't change the
 *           columns, so copy its child's output_schema.
 * TODO(M6): if !star, add a Project. Fill source_index[] with each
 *           selected column's index in the CHILD's output schema, and build
 *           output_schema from just those columns.
 * TODO(M6): if limit >= 0, add a Limit on top.
 *
 * Question: the Filter must go BELOW the Project. Why? (What if the WHERE
 * uses a column that isn't in the SELECT list?)
 */
static MdbStatus build_select_plan(Table *table, Statement *stmt, PlanNode **out,
                                   MdbError *err)
{
    (void)table;
    (void)stmt;
    *out = NULL;
    return MDB_TODO(err, "build_select_plan (M4)");
}

/* ------------------------------------------------------------- rewriting */

/*
 * TODO(M6): optimize the plan in place. Doing nothing is CORRECT, just not
 * optimal, which is why this stub returns MDB_OK. Rules to add:
 *
 *   Constant folding: an expression with no column references always has
 *   the same value, so compute it once at plan time with
 *   eval_expr(expr, NULL, ...) and replace it with a LITERAL.
 *     WHERE 1 + 1 = 2     ->  WHERE 1      ->  the Filter can be removed
 *     WHERE age > 10 * 3  ->  WHERE age > 30
 *   Fold bottom-up: fold the children first, then check whether this node's
 *   children are all literals now.
 *   Careful: if evaluation FAILS (1 / 0), don't fail the query here. Leave
 *   the expression unfolded and let it fail at runtime only if a row is
 *   actually evaluated. (Real databases differ on this. Think about why.)
 *
 *   If the predicate folds to false or NULL, no row can pass. Stretch: add
 *   a PLAN_EMPTY node that returns nothing without scanning at all.
 *
 *   LIMIT 0: same idea.
 */
static MdbStatus rewrite_plan(PlanNode **plan, MdbError *err)
{
    (void)plan;
    (void)err;
    return MDB_OK;
}

/* ---------------------------------------------------------------- [provided] */

MdbStatus optimizer_build_plan(Database *db, Statement *stmt, PlanNode **out, MdbError *err)
{
    *out = NULL;
    if (stmt->kind != STMT_SELECT)
        return mdb_error(err, MDB_ERR_NOT_IMPLEMENTED, "the planner only handles SELECT");

    Table *table;
    MdbStatus st = bind_select(db, stmt, &table, err);
    if (st != MDB_OK) return st;

    PlanNode *plan;
    st = build_select_plan(table, stmt, &plan, err);
    if (st != MDB_OK) return st;

    st = rewrite_plan(&plan, err);
    if (st != MDB_OK) {
        plan_free(plan);
        return st;
    }
    *out = plan;
    return MDB_OK;
}

PlanNode *plan_new(PlanKind kind, PlanNode *child)
{
    PlanNode *node = mdb_xcalloc(1, sizeof *node);
    node->kind = kind;
    node->child = child;
    return node;
}

/*
 * TODO(M4): free a plan tree: the child (recursively), a Filter's
 * predicate (expr_free), then the node. NULL is allowed. Never free a scan's
 * Table: the plan only BORROWS it from the Database.
 */
void plan_free(PlanNode *plan)
{
    (void)plan;
}

/*
 * TODO(M6): write `expr` as SQL-like text, fully parenthesized, e.g.
 * "((age > 30) AND (name = 'bob'))". Recursive. snprintf into a local buffer
 * for each child, then combine them. op_name() and value_format() help.
 * Wrap TEXT literals in quotes so they don't look like column names.
 */
static void expr_to_string(const Expr *expr, char *buf, size_t cap)
{
    (void)expr;
    snprintf(buf, cap, "<expr>");
}

static void explain_node(const PlanNode *node, int depth, ResultSet *out)
{
    char line[512];
    int  indent = depth * 2;
    const char *arrow = depth > 0 ? "-> " : "";

    switch (node->kind) {
    case PLAN_SEQ_SCAN:
        snprintf(line, sizeof line, "%*s%sSeqScan %s", indent, "", arrow,
                 node->as.scan.table->name);
        break;
    case PLAN_FILTER: {
        char expr[256];
        expr_to_string(node->as.filter.predicate, expr, sizeof expr);
        snprintf(line, sizeof line, "%*s%sFilter %s", indent, "", arrow, expr);
        break;
    }
    case PLAN_PROJECT: {
        int w = snprintf(line, sizeof line, "%*s%sProject", indent, "", arrow);
        size_t used = w > 0 ? (size_t)w : 0;
        for (size_t i = 0; i < node->output_schema.ncolumns && used < sizeof line; i++) {
            w = snprintf(line + used, sizeof line - used, "%s%s", i ? ", " : " ",
                         node->output_schema.columns[i].name);
            if (w < 0) break;
            used += (size_t)w;
        }
        break;
    }
    case PLAN_LIMIT:
        snprintf(line, sizeof line, "%*s%sLimit %lld", indent, "", arrow,
                 (long long)node->as.limit.limit);
        break;
    }

    Value v = value_text(line, strlen(line));
    result_add_row(out, &v, 1);
    value_free(&v);
    if (node->child != NULL) explain_node(node->child, depth + 1, out);
}

void plan_explain(const PlanNode *plan, ResultSet *out)
{
    out->ncolumns = 1;
    snprintf(out->column_names[0], sizeof out->column_names[0], "plan");
    explain_node(plan, 0, out);
}
