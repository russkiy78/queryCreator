cmake_minimum_required(VERSION 3.21)
project(queryCreator LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Needed so queryCreatorLib can be built as a shared library (.so/.dll) via
# -DBUILD_SHARED_LIBS=ON -- also covers the vendored static sqlite3 target
# below, which would otherwise fail to link into a .so without -fPIC.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# --- Native database drivers -------------------------------------------------
# queryCreator picks its native database driver at RUNTIME (QcConnectionParams
# ::driver / QcDbDriver, see src/query/qcdbdriver.h) -- QC_DB_DRIVERS below
# only controls which of the five drivers' native client libraries get
# compiled into this particular binary. A build only compiled with a subset
# doesn't need that subset's excluded drivers' dependencies installed at all
# (notably Oracle Instant Client, which is proprietary and not
# vcpkg-redistributable) -- but constructing a QcNativeConnection for a driver
# outside that subset throws at runtime. PostgreSQL is the default single
# driver used for day-to-day development and testing; QC_DB_DRIVERS=All
# compiles every driver into one binary.
#
# PostgreSQL/MySQL dependencies are pulled automatically via vcpkg manifest
# mode (see vcpkg.json and CMakePresets.json) — configure with
# `cmake --preset <postgresql|mysql|mssql|sqlite|oracle|all>`, which requires
# the VCPKG_ROOT environment variable for presets that include PostgreSQL or
# MySQL. SQLite is vendored in-tree (third_party/sqlite) and Oracle Instant
# Client must be installed manually (proprietary, not redistributable via
# vcpkg) — neither needs VCPKG_ROOT. MSSQL needs the system ODBC driver
# manager (unixODBC on Linux/macOS) plus an actual SQL Server ODBC driver
# registered with it (e.g. Microsoft's msodbcsql18, or FreeTDS's tdsodbc).
set(QC_DB_DRIVERS "PostgreSQL" CACHE STRING
    "Semicolon-separated native drivers to compile into this build (PostgreSQL/Oracle/MySQL/SQLite/MSSQL), or 'All' for every supported driver")
set_property(CACHE QC_DB_DRIVERS PROPERTY STRINGS PostgreSQL Oracle MySQL SQLite MSSQL All)

set(QC_DB_ALL_DRIVERS PostgreSQL Oracle MySQL SQLite MSSQL)
if(QC_DB_DRIVERS STREQUAL "All")
    set(QC_DB_DRIVERS_LIST ${QC_DB_ALL_DRIVERS})
else()
    set(QC_DB_DRIVERS_LIST ${QC_DB_DRIVERS})
endif()
list(REMOVE_DUPLICATES QC_DB_DRIVERS_LIST)

set(QC_DB_DRIVER_LIBRARIES "")
set(QC_DB_DRIVER_SOURCES "")
set(QC_DB_DRIVER_DEFINES "")

foreach(driver ${QC_DB_DRIVERS_LIST})
    if(driver STREQUAL "PostgreSQL")
        find_package(PostgreSQL REQUIRED)
        list(APPEND QC_DB_DRIVER_LIBRARIES PostgreSQL::PostgreSQL)
        list(APPEND QC_DB_DRIVER_SOURCES src/query/qcdriverpostgresql.cpp)
        list(APPEND QC_DB_DRIVER_DEFINES QC_DB_HAS_POSTGRESQL)

    elseif(driver STREQUAL "Oracle")
        find_package(OracleOCI REQUIRED)
        list(APPEND QC_DB_DRIVER_LIBRARIES OracleOCI::OCI)
        list(APPEND QC_DB_DRIVER_SOURCES src/query/qcdriveroracle.cpp)
        list(APPEND QC_DB_DRIVER_DEFINES QC_DB_HAS_ORACLE)

    elseif(driver STREQUAL "MySQL")
        if(NOT CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
            message(STATUS "QC_DB_DRIVERS includes MySQL, which is fetched via vcpkg — configure with a preset that sets VCPKG_ROOT rather than a plain cmake invocation")
        endif()
        find_package(unofficial-libmysql CONFIG REQUIRED)
        list(APPEND QC_DB_DRIVER_LIBRARIES unofficial::libmysql::libmysql)
        list(APPEND QC_DB_DRIVER_SOURCES src/query/qcdrivermysql.cpp)
        list(APPEND QC_DB_DRIVER_DEFINES QC_DB_HAS_MYSQL)

    elseif(driver STREQUAL "SQLite")
        add_subdirectory(third_party/sqlite)
        list(APPEND QC_DB_DRIVER_LIBRARIES sqlite3)
        list(APPEND QC_DB_DRIVER_SOURCES src/query/qcdriversqlite.cpp)
        list(APPEND QC_DB_DRIVER_DEFINES QC_DB_HAS_SQLITE)

    elseif(driver STREQUAL "MSSQL")
        # CMake's own FindODBC module covers both sides uniformly: on Windows it
        # picks up the OS/SDK-provided driver manager, on Linux/macOS it locates
        # unixODBC (via pkg-config's odbc.pc, present once unixodbc-dev is
        # installed) — no vcpkg-specific config package needed, the same way
        # PostgreSQL/Oracle above link against plain system dev packages.
        # Connecting still additionally needs an actual SQL Server ODBC driver
        # registered with that driver manager (e.g. Microsoft's msodbcsql18, or
        # FreeTDS's tdsodbc) — that's a runtime/odbcinst.ini concern, not a link-time one.
        find_package(ODBC REQUIRED)
        list(APPEND QC_DB_DRIVER_LIBRARIES ODBC::ODBC)
        list(APPEND QC_DB_DRIVER_SOURCES src/query/qcdrivermssql.cpp)
        list(APPEND QC_DB_DRIVER_DEFINES QC_DB_HAS_MSSQL)

    else()
        message(FATAL_ERROR "Unknown driver '${driver}' in QC_DB_DRIVERS — expected some of PostgreSQL, Oracle, MySQL, SQLite, MSSQL, or 'All'")
    endif()
endforeach()

message(STATUS "queryCreator: compiled-in database drivers = ${QC_DB_DRIVERS_LIST}")

# No STATIC/SHARED keyword here -- respects the standard BUILD_SHARED_LIBS
# variable (OFF by default, so this stays a static .a/.lib unless the caller
# passes -DBUILD_SHARED_LIBS=ON, which produces a .so on Linux or a .dll on
# Windows). On Windows, WINDOWS_EXPORT_ALL_SYMBOLS below auto-generates the
# export table so a .lib import library comes out too, without hand-writing
# __declspec(dllexport) on every public class in src/query.
add_library(queryCreatorLib
    src/query/qcsqlquery.cpp
    src/query/qcsqlqueryelement.cpp
    src/query/qcsqlqueryvalue.cpp
    src/query/qcsqlinsert.cpp
    src/query/qcsqlupdate.cpp
    src/query/qcsqldelete.cpp
    src/query/qcsqldialect.cpp
    src/query/qcconnectionpool.cpp
    src/query/qcnativeconnection.cpp
    src/query/qcdriverconnection.cpp
    src/query/querycreator.cpp
    ${QC_DB_DRIVER_SOURCES}
)
set_target_properties(queryCreatorLib PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_include_directories(queryCreatorLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(queryCreatorLib PRIVATE ${QC_DB_DRIVER_LIBRARIES})
# PUBLIC (not PRIVATE): tests/ constructs QcNativeConnection directly against
# specific drivers and needs to know which ones this build actually compiled
# in (see tests/testdbconfig.cpp's compiledInDrivers()), the same reason this
# was PUBLIC back when it was a single QC_DB_* macro.
target_compile_definitions(queryCreatorLib PUBLIC ${QC_DB_DRIVER_DEFINES})

# QcConnectionPool is thread-safe (std::mutex/condition_variable) — PUBLIC so
# that requirement (e.g. -pthread) propagates to anything linking the lib.
find_package(Threads REQUIRED)
target_link_libraries(queryCreatorLib PUBLIC Threads::Threads)

add_executable(queryCreator src/main.cpp)
target_link_libraries(queryCreator PRIVATE queryCreatorLib)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
