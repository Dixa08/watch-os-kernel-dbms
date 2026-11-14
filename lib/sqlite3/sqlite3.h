#ifndef SQLITE3_H
#define SQLITE3_H
#include <stdint.h>
#include <stddef.h>

#define SQLITE_OK 0
#define SQLITE_ROW 100
#define SQLITE_DONE 101
#define SQLITE_NULL 5
#define SQLITE_STATIC ((void(*)(void*))0)
#define SQLITE_TRANSIENT ((void(*)(void*))-1)

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

inline int sqlite3_open(const char *filename, sqlite3 **ppDb) { *ppDb = (sqlite3*)1; return SQLITE_OK; }
inline int sqlite3_close(sqlite3 *db) { return SQLITE_OK; }
inline int sqlite3_exec(sqlite3 *db, const char *sql, int (*callback)(void*,int,char**,char**), void *arg, char **errmsg) { return SQLITE_OK; }
inline void sqlite3_free(void *p) {}
inline const char *sqlite3_errmsg(sqlite3 *db) { return "Mock Error"; }
inline const char *sqlite3_errstr(int rc) { return "Mock Error String"; }
inline int sqlite3_prepare_v2(sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail) { *ppStmt = (sqlite3_stmt*)1; return SQLITE_OK; }
inline int sqlite3_finalize(sqlite3_stmt *pStmt) { return SQLITE_OK; }
inline int sqlite3_step(sqlite3_stmt *pStmt) { return SQLITE_DONE; }
inline int sqlite3_reset(sqlite3_stmt *pStmt) { return SQLITE_OK; }
inline int sqlite3_clear_bindings(sqlite3_stmt *pStmt) { return SQLITE_OK; }
inline int sqlite3_bind_int(sqlite3_stmt *pStmt, int idx, int val) { return SQLITE_OK; }
inline int sqlite3_bind_int64(sqlite3_stmt *pStmt, int idx, int64_t val) { return SQLITE_OK; }
inline int sqlite3_bind_double(sqlite3_stmt *pStmt, int idx, double val) { return SQLITE_OK; }
inline int sqlite3_bind_text(sqlite3_stmt *pStmt, int idx, const char *val, int len, void(*destructor)(void*)) { return SQLITE_OK; }
inline int sqlite3_bind_null(sqlite3_stmt *pStmt, int idx) { return SQLITE_OK; }
inline int sqlite3_column_int(sqlite3_stmt *pStmt, int iCol) { return 0; }
inline int64_t sqlite3_column_int64(sqlite3_stmt *pStmt, int iCol) { return 0; }
inline double sqlite3_column_double(sqlite3_stmt *pStmt, int iCol) { return 0.0; }
inline const unsigned char *sqlite3_column_text(sqlite3_stmt *pStmt, int iCol) { return (const unsigned char*)""; }
inline int sqlite3_column_type(sqlite3_stmt *pStmt, int iCol) { return SQLITE_NULL; }
inline int64_t sqlite3_last_insert_rowid(sqlite3 *) { return 0; }
#endif
