#ifndef QCCONNECTIONPARAMS_H
#define QCCONNECTIONPARAMS_H

#include <string>

#include "qcdbdriver.h"

// Connection parameters for a native driver, picked at runtime via `driver`
// (must have been compiled into this build -- see QC_DB_DRIVERS in
// CMakeLists.txt). Not every field is meaningful for every driver (e.g.
// SQLite only reads `database`, as a file path) — see QcNativeConnection.
struct QcConnectionParams
{
    // Defaults to PostgreSQL, matching this project's long-standing
    // day-to-day default (see CMakeLists.txt) -- set explicitly for any
    // other driver.
    QcDbDriver driver = QcDbDriver::PostgreSQL;

    std::string host = "localhost";
    std::string port;
    std::string database;
    std::string user;
    std::string password;

    // 0 = unset, use the driver's own default (which can be a very long time
    // or unbounded — set this explicitly for anything that must fail fast).
    int connectTimeoutSeconds = 0;
};

#endif // QCCONNECTIONPARAMS_H
