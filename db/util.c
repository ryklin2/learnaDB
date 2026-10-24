/*
 * util.c — small shared helpers: errors, memory, values, result sets.
 *
 * [provided] Nothing here is database theory; it exists so the other files
 * can focus on the interesting parts. Read it once so you know what's
 * available, then feel free to treat it as a library.
 */
#include "minidb.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- errors */

MdbStatus mdb_error(MdbError *err, MdbStatus status, const char *fmt, ...)
{
    if (err != NULL) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(err->message, sizeof err->message, fmt, args);
        va_end(args);
        err->status = status;
    }
    return status;
}

const char *mdb_status_name(MdbStatus status)
{
    switch (status) {
    case MDB_OK:                  return "OK";
    case MDB_ERR_SYNTAX:          return "SYNTAX";
    case MDB_ERR_NO_SUCH_TABLE:   return "NO_SUCH_TABLE";
    case MDB_ERR_NO_SUCH_COLUMN:  return "NO_SUCH_COLUMN";
    case MDB_ERR_TABLE_EXISTS:    return "TABLE_EXISTS";
    case MDB_ERR_TYPE:            return "TYPE";
    case MDB_ERR_CONSTRAINT:      return "CONSTRAINT";
    case MDB_ERR_RUNTIME:         return "RUNTIME";
    case MDB_ERR_IO:              return "IO";
    case MDB_ERR_CORRUPT:         return "CORRUPT";
    case MDB_ERR_NOT_IMPLEMENTED: return "NOT_IMPLEMENTED";
    }
    return "UNKNOWN";
}

/* ---------------------------------------------------------------- memory */

static void out_of_memory(void)
{
    fputs("minidb: out of memory\n", stderr);
    abort();
}

void *mdb_xmalloc(size_t size)
{
    void *p = malloc(size ? size : 1);
    if (p == NULL) out_of_memory();
    return p;
}

void *mdb_xcalloc(size_t count, size_t size)
{
    void *p = calloc(count ? count : 1, size ? size : 1);
    if (p == NULL) out_of_memory();
    return p;
}

void *mdb_xrealloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size ? size : 1);
    if (p == NULL) out_of_memory();
    return p;
}

char *mdb_strndup(const char *s, size_t len)
{
    char *copy = mdb_xmalloc(len + 1);
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

bool mdb_ieq(const char *a, size_t alen, const char *b)
{
    for (size_t i = 0; i < alen; i++) {
        if (b[i] == '\0') return false;
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    }
    return b[alen] == '\0';
}

/* ---------------------------------------------------------------- values */

Value value_null(void)
{
    Value v;
    v.type = TYPE_NULL;
    v.as.i = 0;
    return v;
}

Value value_int(int64_t i)
{
    Value v;
    v.type = TYPE_INT;
    v.as.i = i;
    return v;
}

Value value_text(const char *s, size_t len)
{
    Value v;
    v.type = TYPE_TEXT;
    v.as.s = mdb_strndup(s, len);
    return v;
}

Value value_copy(Value v)
{
    if (v.type == TYPE_TEXT) return value_text(v.as.s, strlen(v.as.s));
    return v;
}

void value_free(Value *v)
{
    if (v->type == TYPE_TEXT) free(v->as.s);
    *v = value_null();
}

bool value_is_true(Value v)
{
    return v.type == TYPE_INT && v.as.i != 0;
}

void value_format(Value v, char *buf, size_t cap)
{
    switch (v.type) {
    case TYPE_NULL: snprintf(buf, cap, "NULL"); break;
    case TYPE_INT:  snprintf(buf, cap, "%lld", (long long)v.as.i); break;
    case TYPE_TEXT: snprintf(buf, cap, "%s", v.as.s); break;
    }
}

const char *data_type_name(DataType type)
{
    switch (type) {
    case TYPE_NULL: return "NULL";
    case TYPE_INT:  return "INT";
    case TYPE_TEXT: return "TEXT";
    }
    return "?";
}

/* ----------------------------------------------------------- result sets */

void result_init(ResultSet *rs)
{
    memset(rs, 0, sizeof *rs);
}

void result_add_row(ResultSet *rs, const Value *values, size_t nvalues)
{
    if (rs->nrows == rs->capacity) {
        rs->capacity = rs->capacity ? rs->capacity * 2 : 16;
        rs->rows = mdb_xrealloc(rs->rows, rs->capacity * sizeof *rs->rows);
    }
    Row *row = &rs->rows[rs->nrows++];
    for (size_t i = 0; i < MDB_MAX_COLUMNS; i++)
        row->values[i] = i < nvalues ? value_copy(values[i]) : value_null();
}

void result_free(ResultSet *rs)
{
    for (size_t r = 0; r < rs->nrows; r++)
        for (size_t c = 0; c < MDB_MAX_COLUMNS; c++)
            value_free(&rs->rows[r].values[c]);
    free(rs->rows);
    result_init(rs);
}

static void print_separator(const size_t *widths, size_t ncolumns, FILE *out)
{
    fputc('+', out);
    for (size_t c = 0; c < ncolumns; c++) {
        for (size_t i = 0; i < widths[c] + 2; i++) fputc('-', out);
        fputc('+', out);
    }
    fputc('\n', out);
}

void result_print(const ResultSet *rs, FILE *out)
{
    if (rs->ncolumns == 0) {
        if (rs->message[0] != '\0') fprintf(out, "%s\n", rs->message);
        return;
    }

    char   cell[MDB_MAX_TEXT_LEN + 32];
    size_t widths[MDB_MAX_COLUMNS];
    for (size_t c = 0; c < rs->ncolumns; c++) {
        widths[c] = strlen(rs->column_names[c]);
        for (size_t r = 0; r < rs->nrows; r++) {
            value_format(rs->rows[r].values[c], cell, sizeof cell);
            size_t w = strlen(cell);
            if (w > widths[c]) widths[c] = w;
        }
    }

    print_separator(widths, rs->ncolumns, out);
    fputc('|', out);
    for (size_t c = 0; c < rs->ncolumns; c++)
        fprintf(out, " %-*s |", (int)widths[c], rs->column_names[c]);
    fputc('\n', out);
    print_separator(widths, rs->ncolumns, out);
    for (size_t r = 0; r < rs->nrows; r++) {
        fputc('|', out);
        for (size_t c = 0; c < rs->ncolumns; c++) {
            value_format(rs->rows[r].values[c], cell, sizeof cell);
            fprintf(out, " %-*s |", (int)widths[c], cell);
        }
        fputc('\n', out);
    }
    print_separator(widths, rs->ncolumns, out);
    fprintf(out, "(%lu row%s)\n", (unsigned long)rs->nrows, rs->nrows == 1 ? "" : "s");
}
