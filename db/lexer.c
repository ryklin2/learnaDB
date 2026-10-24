/*
 * lexer.c — turns SQL text into a stream of tokens.                   [M1]
 *
 * WHY
 *   Every later stage wants to think in terms of "keyword SELECT",
 *   "identifier users", "integer 42" instead of raw characters. The lexer
 *   (a.k.a. scanner or tokenizer) does that conversion once, so the parser
 *   never deals with whitespace, comments, or letter case.
 *
 * DESIGN: on demand, zero copy
 *   - The parser calls lexer_next() each time it wants one more token; we
 *     never build an array of all tokens. Most hand-written compilers work
 *     this way, and so does SQLite's tokenizer.
 *   - Tokens don't copy text. They point into the original SQL string with
 *     (start, length). That's fast, but it means:
 *       * the source string must stay alive while tokens are in use, and
 *       * token text is NOT NUL-terminated. Never pass tok.start to
 *         printf("%s") or strcmp(). Use printf("%.*s", (int)tok.length,
 *         tok.start) and mdb_ieq() instead.
 *
 * WHAT YOU SHOULD END UP SUPPORTING
 *   spaces, tabs, newlines            skipped (line/column still tracked)
 *   -- comment to end of line         skipped
 *   SELECT, select, SeLeCt            keywords are case-insensitive
 *   users, user_id, _tmp, t2          identifiers: [A-Za-z_][A-Za-z0-9_]*
 *   0, 42, 9223372036854775807        integers; bigger than INT64_MAX is an error
 *   'hello', 'it''s', ''              strings; '' inside means one quote
 *   ( ) , ; * + - /                   single-character punctuation
 *   = != <> < <= > >=                 comparison operators
 *   anything else                     TOK_ERROR with a helpful message
 *
 * DONE WHEN `test_minidb M1` passes. The tests are in tests/test_minidb.c.
 */
#include "minidb.h"

#include <ctype.h>
#include <string.h>

/* ------------------------------------------- character helpers [provided] */

static bool at_end(const Lexer *lx) { return *lx->cur == '\0'; }
static char peek(const Lexer *lx) { return *lx->cur; }
static char peek_next(const Lexer *lx) { return at_end(lx) ? '\0' : lx->cur[1]; }

static bool is_digit(char c) { return isdigit((unsigned char)c) != 0; }
static bool is_ident_start(char c) { return isalpha((unsigned char)c) || c == '_'; }
static bool is_ident_char(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* Consume one character, keeping line/column up to date for error messages. */
static char advance(Lexer *lx)
{
    char c = *lx->cur++;
    if (c == '\n') {
        lx->line++;
        lx->column = 1;
    } else {
        lx->column++;
    }
    return c;
}

/* A token spanning from tok_start to the current position. */
static Token make_token(const Lexer *lx, TokenType type)
{
    Token t;
    t.type = type;
    t.start = lx->tok_start;
    t.length = (size_t)(lx->cur - lx->tok_start);
    t.int_value = 0;
    t.line = lx->tok_line;
    t.column = lx->tok_column;
    return t;
}

/* An error token carries its MESSAGE in start/length. `message` must be a
 * string literal (it has to outlive the token). */
static Token error_token(const Lexer *lx, const char *message)
{
    Token t = make_token(lx, TOK_ERROR);
    t.start = message;
    t.length = strlen(message);
    return t;
}

static const struct {
    const char *word;
    TokenType   type;
} KEYWORDS[] = {
    {"CREATE", TOK_CREATE}, {"TABLE", TOK_TABLE},   {"DROP", TOK_DROP},
    {"INSERT", TOK_INSERT}, {"INTO", TOK_INTO},     {"VALUES", TOK_VALUES},
    {"SELECT", TOK_SELECT}, {"FROM", TOK_FROM},     {"WHERE", TOK_WHERE},
    {"DELETE", TOK_DELETE}, {"LIMIT", TOK_LIMIT},   {"EXPLAIN", TOK_EXPLAIN},
    {"AND", TOK_AND},       {"OR", TOK_OR},         {"NOT", TOK_NOT},
    {"NULL", TOK_NULL},     {"INT", TOK_INT},       {"TEXT", TOK_TEXT},
};

/* ------------------------------------------------------------ your work */

/*
 * TODO(M1): skip whitespace and "--" comments.
 *
 * Loop: while the next char is a space, tab, '\r' or '\n', advance(). If you
 * see "--" (peek() == '-' && peek_next() == '-'), advance until '\n' or the
 * end of input. Keep looping, since a comment can be followed by more
 * whitespace and then another comment.
 *
 * Why does this run BEFORE lexer_next() looks at a character? Because a lone
 * '-' is the minus operator. Only this function knows that "--" means
 * "comment" instead of "minus minus".
 */
static void skip_whitespace_and_comments(Lexer *lx)
{
    (void)lx;
    (void)peek_next; /* stub only: delete these lines when you implement */
}

/*
 * TODO(M1): return the keyword type for this word, or TOK_IDENT.
 *
 * A linear scan over KEYWORDS using mdb_ieq() is fine for 18 keywords.
 * Stretch: real lexers avoid the scan. SQLite generates a perfect hash at
 * build time (tool/mkkeywordhash.c). Measure before and after if you try it.
 */
static TokenType keyword_type(const char *start, size_t length)
{
    (void)start;
    (void)length;
    (void)KEYWORDS;
    return TOK_IDENT;
}

/*
 * TODO(M1): identifiers and keywords.
 * The first character has already been consumed. Consume the rest with
 * is_ident_char(), then make_token(lx, keyword_type(...)).
 *
 * Question to think about: why is "selected" an identifier and not
 * SELECT followed by "ed"? (Hint: "maximal munch".)
 */
static Token lex_identifier(Lexer *lx)
{
    (void)is_ident_char;
    (void)keyword_type;
    return error_token(lx, "not implemented yet: identifiers (M1)");
}

/*
 * TODO(M1): integer literals.
 * The first digit has already been consumed (it's at lx->tok_start). Consume
 * the remaining digits, build the value, and store it in tok.int_value.
 *
 * Overflow: "9223372036854775808" must be a TOK_ERROR, not wrap around.
 * Check BEFORE you multiply. If value > (INT64_MAX - digit) / 10, the next
 * step would overflow. (Signed overflow is undefined behavior in C, so
 * "check afterwards" doesn't work.)
 *
 * There are no negative literals here: "-5" lexes as TOK_MINUS, TOK_INT_LIT.
 * The parser turns that into a negative number.
 */
static Token lex_number(Lexer *lx)
{
    return error_token(lx, "not implemented yet: integers (M1)");
}

/*
 * TODO(M1): string literals.
 * The opening quote has already been consumed. Scan to the closing quote.
 * A doubled quote ('') is an escaped quote and does NOT end the string.
 * Reaching the end of input first is the error "unterminated string".
 *
 * The token keeps the raw text, quotes included ("'it''s'"). Removing the
 * quotes and un-doubling '' is the parser's job when it builds the Value.
 * Why not do it here? Because tokens are zero-copy: there's nowhere to put
 * the modified text without allocating.
 */
static Token lex_string(Lexer *lx)
{
    return error_token(lx, "not implemented yet: strings (M1)");
}

Token lexer_next(Lexer *lx)
{
    skip_whitespace_and_comments(lx);

    lx->tok_start = lx->cur;
    lx->tok_line = lx->line;
    lx->tok_column = lx->column;
    if (at_end(lx)) return make_token(lx, TOK_EOF);

    char c = advance(lx);
    if (is_ident_start(c)) return lex_identifier(lx);
    if (is_digit(c)) return lex_number(lx);
    if (c == '\'') return lex_string(lx);

    switch (c) {
    case '(':
        return make_token(lx, TOK_LPAREN);

    case '<':
        /* A two-character operator: look at the NEXT character before
         * deciding which token this is. */
        if (peek(lx) == '=') { advance(lx); return make_token(lx, TOK_LTE); }
        if (peek(lx) == '>') { advance(lx); return make_token(lx, TOK_NEQ); }
        return make_token(lx, TOK_LT);

    /*
     * TODO(M1): the rest of the punctuation:  ) , ; * + - / =
     * and the operators  !=  >  >=  using the same pattern as '<'.
     * A '!' NOT followed by '=' is an error ("expected '=' after '!'").
     */

    default:
        break;
    }
    return error_token(lx, "unexpected character");
}

/* --------------------------------------------------------------- [provided] */

void lexer_init(Lexer *lx, const char *src)
{
    lx->src = src;
    lx->cur = src;
    lx->line = 1;
    lx->column = 1;
    lx->tok_start = src;
    lx->tok_line = 1;
    lx->tok_column = 1;
}

const char *token_type_name(TokenType type)
{
    switch (type) {
    case TOK_EOF:        return "end of input";
    case TOK_ERROR:      return "error";
    case TOK_IDENT:      return "identifier";
    case TOK_INT_LIT:    return "integer";
    case TOK_STRING_LIT: return "string";
    case TOK_CREATE:     return "CREATE";
    case TOK_TABLE:      return "TABLE";
    case TOK_DROP:       return "DROP";
    case TOK_INSERT:     return "INSERT";
    case TOK_INTO:       return "INTO";
    case TOK_VALUES:     return "VALUES";
    case TOK_SELECT:     return "SELECT";
    case TOK_FROM:       return "FROM";
    case TOK_WHERE:      return "WHERE";
    case TOK_DELETE:     return "DELETE";
    case TOK_LIMIT:      return "LIMIT";
    case TOK_EXPLAIN:    return "EXPLAIN";
    case TOK_AND:        return "AND";
    case TOK_OR:         return "OR";
    case TOK_NOT:        return "NOT";
    case TOK_NULL:       return "NULL";
    case TOK_INT:        return "INT";
    case TOK_TEXT:       return "TEXT";
    case TOK_LPAREN:     return "'('";
    case TOK_RPAREN:     return "')'";
    case TOK_COMMA:      return "','";
    case TOK_SEMICOLON:  return "';'";
    case TOK_STAR:       return "'*'";
    case TOK_PLUS:       return "'+'";
    case TOK_MINUS:      return "'-'";
    case TOK_SLASH:      return "'/'";
    case TOK_EQ:         return "'='";
    case TOK_NEQ:        return "'!='";
    case TOK_LT:         return "'<'";
    case TOK_LTE:        return "'<='";
    case TOK_GT:         return "'>'";
    case TOK_GTE:        return "'>='";
    }
    return "?";
}
