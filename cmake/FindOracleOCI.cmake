#[[
FindOracleOCI.cmake

Locates the Oracle Call Interface (OCI — not OCCI) headers and client library
from an Oracle Instant Client "Basic" + "SDK" installation.

Oracle does not distribute Instant Client through vcpkg or any system package
manager (license restrictions), so it must be installed manually and cannot be
"auto-pulled" the way the other drivers are. This module only locates an
existing installation.

Hints (checked in this order):
  OCI_ROOT / ENV OCI_ROOT   — Instant Client install directory
  ENV ORACLE_HOME           — full Oracle client/DB install directory

Result variables:
  OracleOCI_FOUND
  OracleOCI_INCLUDE_DIRS
  OracleOCI_LIBRARIES

Imported target:
  OracleOCI::OCI
#]]

file(GLOB _qc_oci_glob_dirs
    # Linux/macOS Instant Client zip/rpm layouts
    /opt/oracle/instantclient*
    /usr/lib/oracle/*/client64
    /usr/lib/oracle/*/client
    # Windows Instant Client zip layout
    "$ENV{ProgramFiles}/Oracle/instantclient*"
    "$ENV{ProgramFiles\(x86\)}/Oracle/instantclient*"
    "C:/oracle/instantclient*"
)

find_path(OracleOCI_INCLUDE_DIR
    NAMES oci.h
    HINTS
        ${OCI_ROOT} ENV OCI_ROOT
        ENV ORACLE_HOME
        ${_qc_oci_glob_dirs}
    PATH_SUFFIXES
        include
        sdk/include
)

find_library(OracleOCI_LIBRARY
    NAMES clntsh oci
    HINTS
        ${OCI_ROOT} ENV OCI_ROOT
        ENV ORACLE_HOME
        ${_qc_oci_glob_dirs}
    PATH_SUFFIXES
        lib
        lib64
        sdk/lib/msvc
        sdk/lib
)

unset(_qc_oci_glob_dirs)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OracleOCI
    REQUIRED_VARS OracleOCI_LIBRARY OracleOCI_INCLUDE_DIR
    REASON_FAILURE_MESSAGE
        "Oracle Instant Client (Basic + SDK) not found. Download it from https://www.oracle.com/database/technologies/instant-client.html, then point OCI_ROOT (or ORACLE_HOME) at the install directory."
)

if(OracleOCI_FOUND AND NOT TARGET OracleOCI::OCI)
    add_library(OracleOCI::OCI UNKNOWN IMPORTED)
    set_target_properties(OracleOCI::OCI PROPERTIES
        IMPORTED_LOCATION "${OracleOCI_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OracleOCI_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(OracleOCI_INCLUDE_DIR OracleOCI_LIBRARY)
set(OracleOCI_INCLUDE_DIRS ${OracleOCI_INCLUDE_DIR})
set(OracleOCI_LIBRARIES ${OracleOCI_LIBRARY})
