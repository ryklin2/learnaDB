/*
 * parser.c — turns tokens into a Statement (an abstract syntax tree). [M2, M5]
 *
 * TECHNIQUE: recursive descent
 *   Each grammar rule below becomes one C function that consumes the tokens
 *   for that rule. It's the most common technique for hand-written parsers
 *   (SQLite uses a generated LALR parser instead; PostgreSQL uses bison).
 *   Recursive descent is easier to read, debug, and give good errors with.
 *
 *   We keep ONE token of lookahead: `p->current`. Every decision is made by
 *   looking at it ("is the next token WHERE?"). The helpers check(), match()
 *   and expect() below are all you need.
 *
 * GRAMMAR (EBNF. UPPERCASE = keyword, 'x' = punctuation, [] optional, * repeat)
 *
 *   statement  := [EXPLAIN] (create | drop | insert | select | delete) [';'] EOF
 *   create     := CREATE TABLE ident '(' coldef (',' coldef)* ')'
 *   coldef     := ident (INT | TEXT)
 *   drop       := DROP TABLE ident
 *   insert     := INSERT INTO ident VALUES '(' literal (',' literal)* ')'
 *   select     := SELECT ('*' | ident (',' ident)*) FROM ident
 *                 [WHERE expr] [LIMIT INT_LIT]
 *   delete     := DELETE FROM ident [WHERE expr]
 *   literal    := ['-'] INT_LIT | STRING_LIT | NULL
 *
 *   expr       := or                                       (lowest precedence)
 *   or         := and (OR and)*
 *   and        := not (AND not)*
 *   not        := NOT not | comparison
 *   comparison := additive [('=' | '!=' | '<' | '<=' | '>' | '>=') additive]
 *   additive   := term (('+' | '-') term)*
 *   term       := unary (('*' | '/') unary)*
 *   unary      := '-' unary | primary
 *   primary    := INT_LIT | STRING_LIT | NULL | ident | '(' expr ')'   (highest)
 *
 *   One function per precedence level is why `1 + 2 * 3` parses as
 *   `1 + (2 * 3)`: additive() calls term(), so term() grabs `2 * 3` before
 *   additive() ever sees the '+'. Read "Crafting Interpreters", chapter 6, for
 *   a great walk-through of exactly this structure.
 *
 * ERRORS
 *   The first error wins: fail() records it and sets p->failed. Later errors
 *   are usually just noise caused by the first one. Every loop you write must
 *   also stop when p->failed is set, or a bad input can loop forever.
 *
 * MEMORY
 *   The parser allocates Exprs and TEXT Values into the Statement. If parsing
 *   fails halfway, parse_statement() calls statement_free() on the partial
 *   result. So set pointers to NULL until they're valid, and never leave a
 *   half-built Expr outside the Statement. (Free it yourself before
 *   returning.)
 */
#include "minidb.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    Lexer     lexer;
    Token     current;   /* lookahead: the next token to be consumed */
    Token     previous;  /* the token just consumed                  */
    MdbError *err;
    bool      failed;
} Parser;

/* --------------------------------------------------------- helpers [provided] */

static void fail_at(Parser *p, const Token *tok, const char *message)
{
    if (p->failed) return;
    p->failed = true;
    if (tok->type == TOK_ERROR)
        mdb_error(p->err, MDB_ERR_SYNTAX, "%d:%d: %.*s", tok->line, tok->column,
                  (int)tok->length, tok->start);
    else if (tok->type == TOK_EOF)
        mdb_error(p->err, MDB_ERR_SYNTAX, "%d:%d: %s at end of input", tok->line,
                  tok->column, message);
    else
        mdb_error(p->err, MDB_ERR_SYNTAX, "%d:%d: %s near '%.*s'", tok->line,
                  tok->column, message, (int)tok->length, tok->start);
}

/* Report an error at the lookahead token, e.g. fail(p, "expected ')'"). */
static void fail(Parser *p, const char *message) { fail_at(p, &p->current, message); }

/* Mark a not-yet-written part of the parser. */
static bool todo(Parser *p, const char *what)
{
    if (!p->failed) {
        p->failed = true;
        MDB_TODO(p->err, what);
    }
    return false;
}

static void advance(Parser *p)
{
    p->previous = p->current;
    p->current = lexer_next(&p->lexer);
    if (p->current.type == TOK_ERROR) fail_at(p, &p->current, "");
}

static bool check(const Parser *p, TokenType type) { return p->current.type == type; }

/* Consume the lookahead if it has this type. */
static bool match(Parser *p, TokenType type)
{
    if (!check(p, type)) return false;
    advance(p);
    return true;
}

/* Like match(), but its absence is a syntax error. */
static bool expect(Parser *p, TokenType type, const char *message)
{
    if (match(p, type)) return true;
    fail(p, message);
    return false;
}

static void parser_init(Parser *p, const char *src, MdbError *err)
{
    memset(p, 0, sizeof *p);
    p->err = err;
    lexer_init(&p->lexer, src);
    advance(p); /* load the first token into p->current */
}

/* Consume an identifier into `out`, checking that it fits. */
static bool parse_identifier(Parser *p, char out[MDB_MAX_IDENT_LEN], const char *message)
{
    if (!expect(p, TOK_IDENT, message)) return false;
    if (p->previous.length >= MDB_MAX_IDENT_LEN) {
        fail_at(p, &p->previous, "identifier too long");
        return false;
    }
    memcpy(out, p->previous.start, p->previous.length);
    out[p->previous.length] = '\0';
    return true;
}

/* ------------------------------------------------------------ statements */

/*
 * WORKED EXAMPLE [provided]: DROP TABLE ident
 * DROP has already been consumed by parse_statement(). Every statement
 * parser follows this shape: expect tokens in order, fill in the Statement,
 * and return false on the first problem.
 */
static bool parse_drop_table(Parser *p, Statement *stmt)
{
    stmt->kind = STMT_DROP_TABLE;
    if (!expect(p, TOK_TABLE, "expected TABLE after DROP")) return false;
    return parse_identifier(p, stmt->table, "expected table name");
}

/*
 * TODO(M2): CREATE TABLE ident '(' coldef (',' coldef)* ')'
 * Fill stmt->table and stmt->as.create.schema. Reject:
 *   - more than MDB_MAX_COLUMNS columns
 *   - an empty column list "()"
 *   - a type that isn't INT or TEXT
 * Stretch: reject duplicate column names here, or in the engine. Which
 * layer is the right place, and why?
 */
static bool parse_create_table(Parser *p, Statement *stmt)
{
    stmt->kind = STMT_CREATE_TABLE;
    return todo(p, "parse CREATE TABLE (M2)");
}

/*
 * TODO(M2): INSERT INTO ident VALUES '(' literal (',' literal)* ')'
 *
 * You'll want a helper, parse_literal(Parser *, Value *out), that handles:
 *   INT_LIT             -> value_int(tok.int_value)
 *   '-' INT_LIT         -> value_int(-tok.int_value)
 *   NULL                -> value_null()
 *   STRING_LIT          -> value_text(...) with the quotes stripped and
 *                          '' turned into ' (a small "unescape" loop)
 * parse_primary() can reuse the same helper in M5.
 *
 * Remember: once a TEXT value is stored in stmt->as.insert.values and nvalues
 * is incremented, statement_free() will clean it up if a later token fails.
 */
static bool parse_insert(Parser *p, Statement *stmt)
{
    stmt->kind = STMT_INSERT;
    return todo(p, "parse INSERT (M2)");
}

/*
 * TODO(M2): SELECT ('*' | ident (',' ident)*) FROM ident [LIMIT INT_LIT]
 * TODO(M5): ... [WHERE expr]   (between FROM and LIMIT)
 *
 * Set stmt->as.select.limit = -1 when there is no LIMIT.
 */
static bool parse_select(Parser *p, Statement *stmt)
{
    stmt->kind = STMT_SELECT;
    return todo(p, "parse SELECT (M2)");
}

/*
 * TODO(M7): DELETE FROM ident [WHERE expr]
 * (DELETE has already been consumed.) Short once SELECT ... WHERE works.
 */
static bool parse_delete(Parser *p, Statement *stmt)
{
    stmt->kind = STMT_DELETE;
    return todo(p, "parse DELETE (M7)");
}

/* ----------------------------------------------------------- expressions */

/*
 * TODO(M5): expressions.
 *
 * Write one static function per grammar level (parse_or, parse_and,
 * parse_not, parse_comparison, parse_additive, parse_term, parse_unary,
 * parse_primary), each returning an Expr* (NULL on failure). For example:
 *
 *     static Expr *parse_additive(Parser *p)
 *     {
 *         Expr *left = parse_term(p);
 *         while (left && (check(p, TOK_PLUS) || check(p, TOK_MINUS))) {
 *             OpKind op = check(p, TOK_PLUS) ? OP_ADD : OP_SUB;
 *             advance(p);
 *             Expr *right = parse_term(p);
 *             if (!right) { expr_free(left); return NULL; }
 *             left = expr_new_binary(op, left, right);
 *         }
 *         return left;
 *     }
 *
 * The `while` loop makes `1 - 2 - 3` LEFT-associative: ((1 - 2) - 3).
 * Recursion in parse_not and parse_unary (`NOT NOT x`, `- -5`) is right-
 * associative, which is what you want for prefix operators.
 *
 * Comparisons use `if` rather than `while`: `a < b < c` is a syntax error,
 * not a chain. TOK_NEQ covers both != and <>.
 */
static Expr *parse_expression(Parser *p)
{
    todo(p, "parse expressions (M5)");
    return NULL;
}

/* -------------------------------------------------------------- [provided] */

MdbStatus parse_statement(const char *sql, Statement *out, MdbError *err)
{
    Parser p;
    parser_init(&p, sql, err);
    memset(out, 0, sizeof *out);

    if (match(&p, TOK_EXPLAIN)) out->explain = true;

    if (match(&p, TOK_CREATE))      parse_create_table(&p, out);
    else if (match(&p, TOK_DROP))   parse_drop_table(&p, out);
    else if (match(&p, TOK_INSERT)) parse_insert(&p, out);
    else if (match(&p, TOK_SELECT)) parse_select(&p, out);
    else if (match(&p, TOK_DELETE)) parse_delete(&p, out);
    else fail(&p, "expected CREATE, DROP, INSERT, SELECT or DELETE");

    if (!p.failed) {
        match(&p, TOK_SEMICOLON);
        if (!check(&p, TOK_EOF)) fail(&p, "unexpected input after end of statement");
    }
    if (!p.failed && out->explain && out->kind != STMT_SELECT)
        fail(&p, "EXPLAIN only supports SELECT");

    if (p.failed) {
        statement_free(out);
        return err->status;
    }
    return MDB_OK;
}

MdbStatus parse_expression_string(const char *src, Expr **out, MdbError *err)
{
    Parser p;
    parser_init(&p, src, err);
    *out = NULL;

    Expr *expr = parse_expression(&p);
    if (!p.failed && !check(&p, TOK_EOF)) fail(&p, "unexpected input after expression");
    if (p.failed) {
        expr_free(expr);
        return err->status;
    }
    *out = expr;
    return MDB_OK;
}

Expr *expr_new_literal(Value v)
{
    Expr *e = mdb_xcalloc(1, sizeof *e);
    e->kind = EXPR_LITERAL;
    e->as.literal = v;
    return e;
}

Expr *expr_new_column(const char *name, size_t len)
{
    Expr *e = mdb_xcalloc(1, sizeof *e);
    e->kind = EXPR_COLUMN;
    if (len >= MDB_MAX_IDENT_LEN) len = MDB_MAX_IDENT_LEN - 1;
    memcpy(e->as.column.name, name, len);
    e->as.column.name[len] = '\0';
    e->as.column.index = -1;
    return e;
}

Expr *expr_new_binary(OpKind op, Expr *left, Expr *right)
{
    Expr *e = mdb_xcalloc(1, sizeof *e);
    e->kind = EXPR_BINARY;
    e->as.binary.op = op;
    e->as.binary.left = left;
    e->as.binary.right = right;
    return e;
}

Expr *expr_new_unary(OpKind op, Expr *operand)
{
    Expr *e = mdb_xcalloc(1, sizeof *e);
    e->kind = EXPR_UNARY;
    e->as.unary.op = op;
    e->as.unary.operand = operand;
    return e;
}

const char *op_name(OpKind op)
{
    switch (op) {
    case OP_EQ:  return "=";
    case OP_NEQ: return "!=";
    case OP_LT:  return "<";
    case OP_LTE: return "<=";
    case OP_GT:  return ">";
    case OP_GTE: return ">=";
    case OP_AND: return "AND";
    case OP_OR:  return "OR";
    case OP_NOT: return "NOT";
    case OP_ADD: return "+";
    case OP_SUB: return "-";
    case OP_MUL: return "*";
    case OP_DIV: return "/";
    case OP_NEG: return "-";
    }
    return "?";
}

/* ---------------------------------------------------------- cleanup: yours */

/*
 * TODO(M5): free an expression tree. NULL is allowed (do nothing).
 * Free the children first, then the node itself. A LITERAL may own a TEXT
 * string, so use value_free() on it.
 * Until this is written, expressions leak. Nothing crashes, but run
 * `make sanitize` (or valgrind) after implementing it to prove the leak is gone.
 */
void expr_free(Expr *expr)
{
    (void)expr;
}

/*
 * TODO(M2): free what a Statement owns: the INSERT values (value_free each),
 * and the SELECT/DELETE `where` expression (expr_free). It must be safe on a
 * partially built Statement and on one that was memset to zero.
 * Tip: after freeing a pointer, set it to NULL. Then calling this twice is
 * harmless, and use-after-free bugs show up as NULL crashes, not corruption.
 */
void statement_free(Statement *stmt)
{
    (void)stmt;
}
