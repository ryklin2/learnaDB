/*
 * storage.c — where the data actually lives.                  [M3, M7, M8]
 *
 * VERSION 1 (M3): everything in memory.
 *   A Database is an array of Table pointers. A Table is a schema plus a
 *   growable array of Rows. It's simple, so you can get end-to-end queries
 *   working quickly.
 *
 * VERSION 2 (M8): persistence.
 *   db_save() writes the whole database to a file and db_load() reads it
 *   back. Rewriting everything on every change is slow, but it's correct and
 *   it teaches the key ideas: serialization, file formats, and crash safety.
 *
 * VERSION 3 (stretch, see docs/ROADMAP.md): pages.
 *   Real databases split the file into fixed-size pages (often 4 KiB),
 *   cache them in a "buffer pool", and store tables as B+trees of pages. Then
 *   a change rewrites a few pages instead of the whole file. If you keep this
 *   file's PUBLIC functions stable, you can swap the internals later without
 *   touching the parser, optimizer or executor. That separation is the point
 *   of having a storage layer.
 */
#include "minidb.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- [provided] */

MdbStatus db_open(Database *db, const char *path, MdbError *err)
{
    memset(db, 0, sizeof *db);
    if (path == NULL || path[0] == '\0') return MDB_OK; /* in-memory database */

    size_t len = strlen(path);
    if (len >= sizeof db->path) return mdb_error(err, MDB_ERR_IO, "path too long: %s", path);
    memcpy(db->path, path, len + 1);
    return db_load(db, err);
}

/* -------------------------------------------------------------- M3: tables */

/*
 * TODO(M3): find a table by name, case-insensitively (SQL identifiers are
 * case-insensitive: `Users` and `users` are the same table).
 * Return NULL if there isn't one.
 */
Table *db_find_table(Database *db, const char *name)
{
    (void)db;
    (void)name;
    return NULL;
}

/*
 * TODO(M3): create an empty table.
 *   - MDB_ERR_TABLE_EXISTS if the name is taken (use db_find_table)
 *   - MDB_ERR_CONSTRAINT if there are already MDB_MAX_TABLES tables
 *   - otherwise mdb_xcalloc a Table, copy the name and schema, and append it
 *     to db->tables.
 */
MdbStatus db_create_table(Database *db, const char *name, const Schema *schema,
                          MdbError *err)
{
    (void)db;
    (void)name;
    (void)schema;
    return MDB_TODO(err, "db_create_table (M3)");
}

/*
 * TODO(M3): index of the column called `name` (case-insensitive), or -1.
 */
int schema_find_column(const Schema *schema, const char *name)
{
    (void)schema;
    (void)name;
    return -1;
}

/*
 * TODO(M3): validate and append one row.
 *
 * Validation, in this order, so error messages make sense:
 *   1. nvalues must equal table->schema.ncolumns       -> MDB_ERR_CONSTRAINT
 *   2. each value's type must match its column's type,
 *      except that NULL fits any column                -> MDB_ERR_TYPE
 *      (Name the column and both types in the message.)
 *   3. TEXT longer than MDB_MAX_TEXT_LEN               -> MDB_ERR_CONSTRAINT
 * Validate EVERYTHING before changing anything. A failed insert must leave
 * the table exactly as it was. That's atomicity, the "A" in ACID, in
 * miniature.
 *
 * Growing the array: when nrows == capacity, double the capacity (start at,
 * say, 16) and mdb_xrealloc. Doubling makes appends O(1) amortized. Growing
 * by +1 each time would make N inserts cost O(N^2) copying.
 *
 * Copying: the caller BORROWS `values` to us, so store value_copy() of each
 * one. Set unused slots (index >= ncolumns) to value_null().
 */
MdbStatus table_insert(Table *table, const Value *values, size_t nvalues, MdbError *err)
{
    (void)table;
    (void)values;
    (void)nvalues;
    return MDB_TODO(err, "table_insert (M3)");
}

/*
 * TODO(M3): free every table (every row's values, the rows array, the Table
 * itself) and reset *db. Does NOT save. The engine saves after each change.
 */
void db_close(Database *db)
{
    (void)db;
}

/* ------------------------------------------------------- M7: delete & drop */

/*
 * TODO(M7): delete the rows at `indices` (ascending, no duplicates).
 *
 * The naive way is to memmove the tail down once per deleted row, which is
 * O(n * k). Instead, do one pass with two cursors, "read" and "write":
 * value_free() the rows being deleted, copy each kept row down to `write`,
 * and set nrows = write at the end. That's O(n). (Rows are plain structs, so
 * moving them with `rows[w] = rows[r]` moves ownership of their text too.)
 *
 * Why does the engine collect indices first, instead of deleting while
 * scanning? Deleting shifts later rows down, so a scan that's in progress
 * would skip one. Databases have to handle the same issue when an UPDATE
 * moves a row ahead of the scan (look up "the Halloween problem").
 */
MdbStatus table_delete_rows(Table *table, const size_t *indices, size_t count,
                            MdbError *err)
{
    (void)table;
    (void)indices;
    (void)count;
    return MDB_TODO(err, "table_delete_rows (M7)");
}

/*
 * TODO(M7): drop a table: MDB_ERR_NO_SUCH_TABLE if it doesn't exist.
 * Otherwise free it and remove it from db->tables, keeping the others in
 * order. (Or swap in the last one: order doesn't matter much here.)
 */
MdbStatus db_drop_table(Database *db, const char *name, MdbError *err)
{
    (void)db;
    (void)name;
    return MDB_TODO(err, "db_drop_table (M7)");
}

/* -------------------------------------------------------- M8: persistence */

/*
 * TODO(M8): write the whole database to db->path.
 *
 * SUGGESTED FILE FORMAT (integers little-endian, written byte by byte so the
 * file is the same on every machine. Don't fwrite() a struct: padding and
 * pointers make that unportable and unsafe.)
 *
 *   header      8 bytes   magic "MINIDB01"   (the version is in the magic)
 *               u32       table count
 *   per table   u8 + bytes  name length, name
 *               u8          column count
 *               per column: u8 + bytes name, u8 type
 *               u64         row count
 *               per row, per column:
 *                   u8 type tag, then INT: i64 | TEXT: u16 length + bytes | NULL: nothing
 *
 * Write small helpers first: put_u8/put_u16/put_u32/put_u64(FILE *, value).
 * Check every fwrite. A full disk is an MDB_ERR_IO, not a crash.
 *
 * CRASH SAFETY: never overwrite the real file in place. If the process dies
 * halfway through, you'd be left with half a database. Instead:
 *   1. write everything to "<path>.tmp"
 *   2. fflush() and fclose() it (check both!)
 *   3. rename() it over <path>. A rename on the same filesystem is atomic,
 *      so readers see either the old file or the new one, never a mix.
 *      (On Windows, rename() fails if the target exists. remove() the old
 *      file first, or use MoveFileExA with MOVEFILE_REPLACE_EXISTING.)
 * Stretch: also fsync, and learn why databases use a write-ahead log (WAL)
 * so they don't have to rewrite the whole file on every change.
 */
MdbStatus db_save(const Database *db, MdbError *err)
{
    (void)db;
    return MDB_TODO(err, "db_save (M8)");
}

/*
 * TODO(M8): load db->path into the (already empty) *db.
 *   - File doesn't exist (fopen returns NULL with errno == ENOENT): that's a
 *     brand-new database. Return MDB_OK.
 *   - Wrong magic, truncated file, absurd counts (more tables than
 *     MDB_MAX_TABLES, a type tag that isn't valid, ...): MDB_ERR_CORRUPT.
 *     Never trust bytes from disk. Validate every length before using it,
 *     or a corrupt file becomes a buffer overflow.
 *   - On any failure, free whatever was partially loaded (db_close + keep
 *     db->path) so the caller isn't left with half a database.
 * Reuse db_create_table() and table_insert() to rebuild the tables. That
 * re-validates everything for free.
 */
MdbStatus db_load(Database *db, MdbError *err)
{
    (void)db;
    return MDB_TODO(err, "db_load (M8). Run without a file argument for an in-memory database");
}
