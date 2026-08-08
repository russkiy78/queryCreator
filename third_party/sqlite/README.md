# Vendored SQLite amalgamation

`sqlite3.c`, `sqlite3.h`, `sqlite3ext.h` — unmodified SQLite 3.53.4 amalgamation,
downloaded from https://sqlite.org/2026/sqlite-amalgamation-3530400.zip
(SHA3-256: `628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e`).

SQLite is public domain — see https://sqlite.org/copyright.html. Vendored
directly (per the project's driver policy) instead of depending on a system
package or vcpkg port, since the amalgamation is a single self-contained
translation unit that builds identically everywhere.

To upgrade: replace these three files with a newer amalgamation release and
update the version/checksum above.
