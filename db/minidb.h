/*
 * minidb.h — the one shared header for MiniDB, a small SQL database in C.
 *
 * ARCHITECTURE (data flows top to bottom)
 *
 *   main.c        REPL: read a line of SQL, print the result        [provided]
 *     |
 *   engine.c      engine_execute(): drives the whole pipeline,       [you]
 *     |           and runs query plans (the "executor")
 *     +-- lexer.c      SQL text   -> Tokens                          [you]
 *     +-- parser.c     Tokens     -> Statement (an AST)              [you]
 *     +-- optimizer.c  Statement  -> PlanNode tree                   [you]
 *     +-- storage.c    tables, rows, and (later) the database file   [you]
 *
 *   util.c        values, errors, strings, result sets               [provided]
 *
 * CONVENTIONS (used in every file — worth reading once, carefully)
 *
 *  - Errors: any function that can fail returns MdbStatus and takes an
 *    `MdbError *err` as its LAST argument. On failure, write the message with
 *    `return mdb_error(err, MDB_ERR_xxx, "fmt", ...);` — it fills err and
 *    returns the status in one line. Never print from deep inside the engine;
 *    the caller (main.c, or a test) decides how to show errors.
 *
 *  - Ownership: every function comment says who owns memory. "Takes
 *    ownership" = callee frees it. "Borrows" = callee must not free it or keep
 *    it. "Caller frees" = you got a fresh allocation. Most bugs in a project
 *    like this are ownership bugs, so treat these comments as part of the API.
 *
 *  - Out of memory: mdb_xmalloc() and friends abort the process instead of
 *    returning NULL. That keeps error handling focused on database errors.
 *    (SQLite does handle OOM gracefully — that's a stretch goal.)
 *
 *  - TODO(Mn) marks work for milestone n. See docs/ROADMAP.md for the order,
 *    and run `make test` to see which milestones pass.
 */
#ifndef MINIDB_H
#define MINIDB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ==========================================================================
 * Limits
 *
 * Fixed limits keep memory management simple while you learn. Real databases
 * avoid most of them; removing one is a good stretch exercise.
 * ========================================================================== */

#define MDB_MAX_IDENT_LEN  64   /* table/column name, INCLUDING the '\0'    */
#define MDB_MAX_COLUMNS    32   /* per table                                */
#define MDB_MAX_TABLES     64   /* per database                             */
#define MDB_MAX_TEXT_LEN   255  /* longest TEXT value in bytes, w/o '\0'    */
#define MDB_MAX_ERROR_LEN  256
#define MDB_MAX_PATH_LEN   260
#define MDB_MAX_INPUT_LEN  4096 /* longest line the REPL accepts            */

#if defined(__GNUC__) || defined(__clang__)
#define MDB_PRINTF(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#else
#define MDB_PRINTF(fmt_idx, args_idx)
#endif

/* ==========================================================================
 * Errors                                                         (util.c)
 * ========================================================================== */

typedef enum {
    MDB_OK = 0,
    MDB_ERR_SYNTAX,          /* lexer/parser rejected the input              */
    MDB_ERR_NO_SUCH_TABLE,
    MDB_ERR_NO_SUCH_COLUMN,
    MDB_ERR_TABLE_EXISTS,
    MDB_ERR_TYPE,            /* e.g. 'abc' into an INT column, name > 5      */
    MDB_ERR_CONSTRAINT,      /* wrong value count, a limit exceeded, ...     */
    MDB_ERR_RUNTIME,         /* failed while executing, e.g. division by 0  */
    MDB_ERR_IO,              /* file could not be read/written               */
    MDB_ERR_CORRUPT,         /* database file isn't in the expected format   */
    MDB_ERR_NOT_IMPLEMENTED  /* returned by every TODO stub                  */
} MdbStatus;

typedef struct {
    MdbStatus status;
    char      message[MDB_MAX_ERROR_LEN];
} MdbError;

/* Fill *err (if not NULL) and return `status`, so callers can write
 * `return mdb_error(err, MDB_ERR_TYPE, "bad type for %s", name);`. */
MdbStatus   mdb_error(MdbError *err, MdbStatus status, const char *fmt, ...) MDB_PRINTF(3, 4);
const char *mdb_status_name(MdbStatus status);

/* Every TODO stub returns this, so an unfinished feature fails loudly and
 * says which milestone it belongs to instead of silently doing nothing. */
#define MDB_TODO(err, what) \
    mdb_error((err), MDB_ERR_NOT_IMPLEMENTED, "not implemented yet: %s", (what))

/* ==========================================================================
 * Memory and string helpers                                      (util.c)
 * ========================================================================== */

void *mdb_xmalloc(size_t size);                 /* never returns NULL */
void *mdb_xcalloc(size_t count, size_t size);
void *mdb_xrealloc(void *ptr, size_t size);
char *mdb_strndup(const char *s, size_t len);   /* caller frees */

/* Case-insensitive compare of a length-delimited string (like token text)
 * with a NUL-terminated one. mdb_ieq("SeLeCt", 6, "select") == true. */
bool mdb_ieq(const char *a, size_t alen, const char *b);

/* ==========================================================================
 * Values                                                         (util.c)
 *
 * A Value is one cell: an INT, a TEXT, or NULL. Booleans are INTs 0/1 (as in
 * C and SQLite). TEXT values own a heap string: copy with value_copy(), free
 * with value_free(). A plain struct assignment (`Value b = a;`) is a SHALLOW
 * copy — both now point at the same string, and only one may free it.
 * ========================================================================== */

typedef enum { TYPE_NULL, TYPE_INT, TYPE_TEXT } DataType;

typedef struct {
    DataType type;
    union {
        int64_t i;   /* TYPE_INT                                       */
        char   *s;   /* TYPE_TEXT: heap-allocated, NUL-terminated       */
    } as;
} Value;

Value       value_null(void);
Value       value_int(int64_t i);
Value       value_text(const char *s, size_t len); /* copies s; free with value_free */
Value       value_copy(Value v);                   /* deep copy */
void        value_free(Value *v);                  /* frees text, leaves *v NULL */
bool        value_is_true(Value v);                /* INT and nonzero */
void        value_format(Value v, char *buf, size_t cap); /* for printing */
const char *data_type_name(DataType type);

/* ==========================================================================
 * Schemas and rows
 * ========================================================================== */

typedef struct {
    char     name[MDB_MAX_IDENT_LEN];
    DataType type;                     /* TYPE_INT or TYPE_TEXT */
} ColumnDef;

typedef struct {
    size_t    ncolumns;
    ColumnDef columns[MDB_MAX_COLUMNS];
} Schema;

/* One row. Only the first schema.ncolumns slots are meaningful; the rest are
 * TYPE_NULL so they can always be freed safely. Fixed-size rows waste some
 * memory but make the storage code much simpler to start with. */
typedef struct {
    Value values[MDB_MAX_COLUMNS];
} Row;

/* ==========================================================================
 * Lexer                                                          (lexer.c, M1)
 * ========================================================================== */

typedef enum {
    TOK_EOF,
    TOK_ERROR,      /* start/length hold an error MESSAGE, not source text */

    /* literals and names */
    TOK_IDENT,      /* users, user_id                        */
    TOK_INT_LIT,    /* 42  (value in Token.int_value)        */
    TOK_STRING_LIT, /* 'it''s'  (text still includes quotes) */

    /* keywords (matched case-insensitively) */
    TOK_CREATE, TOK_TABLE, TOK_DROP, TOK_INSERT, TOK_INTO, TOK_VALUES,
    TOK_SELECT, TOK_FROM, TOK_WHERE, TOK_DELETE, TOK_LIMIT, TOK_EXPLAIN,
    TOK_AND, TOK_OR, TOK_NOT, TOK_NULL,
    TOK_INT, TOK_TEXT, /* the type names in CREATE TABLE */

    /* punctuation and operators */
    TOK_LPAREN, TOK_RPAREN, TOK_COMMA, TOK_SEMICOLON,
    TOK_STAR, TOK_PLUS, TOK_MINUS, TOK_SLASH,
    TOK_EQ, TOK_NEQ, TOK_LT, TOK_LTE, TOK_GT, TOK_GTE
} TokenType;

typedef struct {
    TokenType   type;
    const char *start;     /* points INTO the source; NOT NUL-terminated  */
    size_t      length;
    int64_t     int_value; /* only for TOK_INT_LIT                        */
    int         line;      /* 1-based position, for error messages        */
    int         column;
} Token;

typedef struct {
    const char *src;
    const char *cur;        /* next character to look at                  */
    int         line, column;
    const char *tok_start;  /* where the token being scanned began        */
    int         tok_line, tok_column;
} Lexer;

/* The lexer borrows `src`; it must outlive every token produced from it. */
void        lexer_init(Lexer *lx, const char *src);
Token       lexer_next(Lexer *lx);   /* after the end, keeps returning TOK_EOF */
const char *token_type_name(TokenType type);

/* ==========================================================================
 * AST (abstract syntax tree)                                     (parser.c)
 * ========================================================================== */

typedef enum { EXPR_LITERAL, EXPR_COLUMN, EXPR_BINARY, EXPR_UNARY } ExprKind;

typedef enum {
    OP_EQ, OP_NEQ, OP_LT, OP_LTE, OP_GT, OP_GTE, /* comparison -> 0/1/NULL */
    OP_AND, OP_OR, OP_NOT,                       /* logic (3-valued)        */
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_NEG       /* INT arithmetic          */
} OpKind;

typedef struct Expr {
    ExprKind kind;
    union {
        Value literal;                                         /* owned */
        struct {
            char name[MDB_MAX_IDENT_LEN];
            int  index;  /* -1 until the binder resolves it (M5) */
        } column;
        struct { OpKind op; struct Expr *left, *right; } binary;  /* owned */
        struct { OpKind op; struct Expr *operand; } unary;        /* owned */
    } as;
} Expr;

typedef enum {
    STMT_CREATE_TABLE, STMT_DROP_TABLE, STMT_INSERT, STMT_SELECT, STMT_DELETE
} StmtKind;

typedef struct {
    StmtKind kind;
    bool     explain;                   /* EXPLAIN SELECT ...  (M6) */
    char     table[MDB_MAX_IDENT_LEN];
    union {
        struct { Schema schema; } create;
        struct {
            size_t nvalues;
            Value  values[MDB_MAX_COLUMNS];               /* owned */
        } insert;
        struct {
            bool    star;                                 /* SELECT *        */
            size_t  ncolumns;                             /* if !star        */
            char    columns[MDB_MAX_COLUMNS][MDB_MAX_IDENT_LEN];
            Expr   *where;                                /* owned, or NULL  */
            int64_t limit;                                /* -1 = no LIMIT   */
        } select;
        struct { Expr *where; } del;                      /* owned, or NULL  */
    } as;
} Statement;

/* Parse one SQL statement. On success the caller must statement_free(out).
 * On failure *out is already cleaned up and err says what went wrong. */
MdbStatus parse_statement(const char *sql, Statement *out, MdbError *err);
void      statement_free(Statement *stmt);

/* Parse just an expression, e.g. "1 + 2 * 3". Handy for tests. */
MdbStatus parse_expression_string(const char *src, Expr **out, MdbError *err);

/* Expression constructors (take ownership of child expressions / values). */
Expr       *expr_new_literal(Value v);
Expr       *expr_new_column(const char *name, size_t len);
Expr       *expr_new_binary(OpKind op, Expr *left, Expr *right);
Expr       *expr_new_unary(OpKind op, Expr *operand);
void        expr_free(Expr *expr);         /* frees the whole tree; NULL ok */
const char *op_name(OpKind op);            /* "+", "AND", ...               */

/* ==========================================================================
 * Storage                                                        (storage.c)
 * ========================================================================== */

typedef struct {
    char   name[MDB_MAX_IDENT_LEN];
    Schema schema;
    Row   *rows;       /* dynamic array, owned */
    size_t nrows;
    size_t capacity;
} Table;

typedef struct {
    /* Pointers, so a Table never moves in memory when other tables are
     * created or dropped — plans can safely hold a Table*. */
    Table *tables[MDB_MAX_TABLES];
    size_t ntables;
    char   path[MDB_MAX_PATH_LEN];  /* "" = in-memory only */
} Database;

/* path == NULL: in-memory database. Otherwise load the file if it exists. */
MdbStatus db_open(Database *db, const char *path, MdbError *err);
void      db_close(Database *db);   /* frees everything; does NOT save */
MdbStatus db_save(const Database *db, MdbError *err);           /* M8 */
MdbStatus db_load(Database *db, MdbError *err);                 /* M8 */

Table    *db_find_table(Database *db, const char *name);  /* NULL if missing */
MdbStatus db_create_table(Database *db, const char *name, const Schema *schema,
                          MdbError *err);
MdbStatus db_drop_table(Database *db, const char *name, MdbError *err);

int       schema_find_column(const Schema *schema, const char *name); /* -1 if missing */

/* Validates and appends one row. BORROWS `values`: the table stores copies. */
MdbStatus table_insert(Table *table, const Value *values, size_t nvalues, MdbError *err);

/* Deletes the rows at `indices` (sorted ascending, no duplicates). */
MdbStatus table_delete_rows(Table *table, const size_t *indices, size_t count,
                            MdbError *err);

/* ==========================================================================
 * Result sets                                                    (util.c)
 * ========================================================================== */

typedef struct {
    size_t ncolumns;                   /* 0 for statements that return no rows */
    char   column_names[MDB_MAX_COLUMNS][MDB_MAX_IDENT_LEN];
    Row   *rows;                       /* owned; text values are deep copies */
    size_t nrows;
    size_t capacity;
    size_t rows_affected;              /* INSERT / DELETE                     */
    char   message[MDB_MAX_ERROR_LEN]; /* e.g. "Table created."               */
} ResultSet;

void result_init(ResultSet *rs);
void result_add_row(ResultSet *rs, const Value *values, size_t nvalues); /* copies */
void result_free(ResultSet *rs);
void result_print(const ResultSet *rs, FILE *out);

/* ==========================================================================
 * Query plans                                                    (optimizer.c)
 *
 * A plan is a tree of operators. Rows flow from the leaves (scans) up to the
 * root, one at a time (see engine.c for how they are pulled).
 * ========================================================================== */

typedef enum { PLAN_SEQ_SCAN, PLAN_FILTER, PLAN_PROJECT, PLAN_LIMIT } PlanKind;

typedef struct PlanNode {
    PlanKind         kind;
    struct PlanNode *child;          /* input node; NULL for scans (owned)   */
    Schema           output_schema;  /* the columns this node produces       */
    union {
        struct { Table *table; size_t next_row; } scan;     /* table borrowed */
        struct { Expr *predicate; } filter;                 /* owned          */
        struct {
            size_t ncolumns;
            int    source_index[MDB_MAX_COLUMNS]; /* which child column -> out */
            Row    out;                           /* output buffer (borrows)   */
        } project;
        struct { int64_t limit; int64_t emitted; } limit;
    } as;
} PlanNode;

PlanNode *plan_new(PlanKind kind, PlanNode *child);  /* takes ownership of child */
void      plan_free(PlanNode *plan);

/* Bind + build + optimize a SELECT. May MOVE expressions out of *stmt (it
 * sets the moved pointers to NULL). Caller plan_free()s *out. */
MdbStatus optimizer_build_plan(Database *db, Statement *stmt, PlanNode **out,
                               MdbError *err);

/* Resolve column names in `expr` against `schema` and type-check it.
 * *out_type receives the expression's result type. */
MdbStatus bind_expr(const Schema *schema, Expr *expr, DataType *out_type, MdbError *err);

/* Describe a plan as rows of text (one row per node), for EXPLAIN. */
void      plan_explain(const PlanNode *plan, ResultSet *out);

/* ==========================================================================
 * Engine / executor                                              (engine.c)
 * ========================================================================== */

/* The front door: run one SQL statement. Always result_free(out) afterwards,
 * whether it succeeded or not. */
MdbStatus engine_execute(Database *db, const char *sql, ResultSet *out, MdbError *err);

/* The iterator ("Volcano") interface every plan node implements.
 * exec_next sets *out_row to the next row, or to NULL when there are no more.
 * The row is BORROWED and only valid until the next call. */
MdbStatus exec_open(PlanNode *node, MdbError *err);
MdbStatus exec_next(PlanNode *node, const Row **out_row, MdbError *err);

/* Evaluate `expr` against `row` (row may be NULL for constant expressions).
 * If *out is TEXT, its string is BORROWED from the row or the expression:
 * don't free it; value_copy() it if you need to keep it. */
MdbStatus eval_expr(const Expr *expr, const Row *row, Value *out, MdbError *err);

/* <0, 0, >0 like strcmp. Both values must be non-NULL and of the same type. */
int       value_compare(Value a, Value b);

#endif /* MINIDB_H */
