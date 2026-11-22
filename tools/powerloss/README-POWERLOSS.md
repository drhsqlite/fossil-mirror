# Power loss and killed-process tests for SQLite programs

Dan Shearer, November 2026

`powerloss.so` is an `LD_PRELOAD` library for Linux and BSD that stops a
program wherever its SQLite database files could reach the disk, and leaves the
files as a power loss or a killed process would. You can use it with any
program that links SQLite. In the example the driver is `powerloss.sh`, and
we're asking the question *what happens to Fossil repo and checkout databases
when bad things happen during `fossil commit`?* There are different cases for
WAL and DELETE mode repo dbs. `test/powerloss.test` in Fossil's test suite uses
the same library to interrupt other Fossil commands, such as merge, update,
push and clone.

## The point in power loss testing

One transaction committed across two SQLite database files can leave the
checkout database and the repo database out of step if the transaction is
interrupted, although both files pass an integrity check. The cause is that
SQLite can only do an atomic commit across both files via a *super-journal*,
and SQLite only makes a super-journal for databases in a rollback journal mode.
If one of the databases is in WAL, MEMORY or OFF journal mode, or at
`synchronous=OFF`, SQLite commits each file in turn (that is, not atomically).
The [SQLite ATTACH documentation](https://sqlite.org/lang_attach.html) says:

> If the main database is ":memory:" or if the journal_mode is WAL, then
> transactions continue to be atomic within each individual database file. But
> if the host computer crashes in the middle of a COMMIT where two or more
> database files are updated, some of those files might get the changes where
> others might not.

The Fossil data model commits across multiple databases, and there are good
reasons why a user might switch the repository database to [WAL
mode](https://sqlite.org/wal.html). This test tool was written to verify that
multi-database commits are safe.

A single database in WAL mode can lose a committed transaction too. The [PRAGMA
documentation](https://sqlite.org/pragma.html#pragma_synchronous) says:

> A transaction committed in WAL mode with synchronous=NORMAL might roll back
> following a power loss or system crash.

Whether a program survives an interruption depends on its journal modes, its
`synchronous` settings and the order in which it commits its databases. This
tool stops the program at every point where the outcome could differ, and
records the state of the databases after each stop.

These problems are less likely to occur when using LMDB as the SQLite backing
store via [LumoSQL](https://lumosql.org/src/lumosql), because LMDB syncs new
pages before writing the meta page that refers to them, so a power loss does
not lose a committed transaction from one database. A commit across two
databases still needs this test to check that failures are graceful.

## How it works

There are two ways to interrupt a program. When the process is killed, the
operating system retains everything the process wrote. When the operating
system crashes or the power fails, the disk has only what the process synced.

* **Power loss** (the default): the stop points are at every `fsync()`,
  `fdatasync()` and `unlink()` of a database or journal file. The library saves
  a copy of each database file when the first process of the run starts and
  whenever a process syncs it. At the stop point it restores the saved copies,
  deletes the files that were never synced and every `-shm` file, and exits
  with status 77.
* **Killed process** (`STOP_MODE=kill`): each `pwrite()` or `write()` to those
  files is also a stop point, and so is each `fwrite()` to one that was opened
  with `fopen()`. The C library's own write from inside `fwrite()` cannot be
  intercepted, so without this a file written through stdio, such as the
  repository that `fossil clone` copies from a file, would have no stop points.
  At the stop point the library exits with status 77 and leaves every file as
  it is. The other processes continue, and see the stopped process disappear,
  as a client sees a server that is killed during a request.

A transfer between two repositories writes them in two processes. A `fossil
push` over a `file:` URL runs `fossil http` to write the remote repository,
while `fossil server` writes its repository in the answering process. A power
loss stops every process on the machine at the same moment, and a killed
process stops only itself.

Every process that loads the library during a run counts its stop points in the
same sequence, including the programs that a program runs with `system()`. A
process locks the library's state file around each write, sync and unlink of a
tracked file, so a restore does not overlap another process's write, and a
process that locks the state file after a power loss exits with status 77.

The library reads its settings from the environment:

* `STOP_DIR`: a working directory. The library tracks files under
  `STOP_DIR/run`, saves copies in `STOP_DIR/shadow` and appends one line per
  stop point to `STOP_DIR/log`, with the number, the call, the file and the
  process ID. A file's copy has its path under `STOP_DIR/run`, with each `/`
  replaced by `%`, so the `.fslckout` files of two checkouts have separate
  copies. `STOP_DIR/shadow` also has the stop-point count, so a driver deletes
  it before each run.
* `STOP_AT`: the stop point to stop at, counting from 1. `0` runs the program
  to the end and only records the stop points.
* `STOP_MODE`: `kill` for the killed-process mode.

A driver runs the program once with `STOP_AT=0` to count the stop points, then
once for each one from a fresh copy of the files, and examines the files after
each run. It should also check that each run stopped where it was told to,
because a run that does not stop leaves the files of a finished command, which
pass every other check. `powerloss.sh` requires that the run logged its stop
point, that in power mode it logged nothing after it, and that the exit status
isn't 0. `pl_stop_seen` in `test/powerloss.test` is stricter: for a command of
one process it also requires the same call on the same file as in the counting
run, and exit status 77.

## Running the Fossil check-in test

`powerloss.sh` uses `fossil` to create a repository and a check-out with an
edit, an add, a remove and a rename pending. It then runs the check-in once for
each stop point, and stops it there, using the command in `PL_COMMIT`, which
defaults to `fossil commit -m two`. After each run the script reads the
check-in that `.fslckout` says was just made and the repo database's latest
check-in, and runs `PRAGMA integrity_check` on both files. The third argument
sets the repo to WAL or DELETE mode.

From the root of the Fossil source tree, with the `fossil` you want to test:

```sh
cc -shared -fPIC -o tools/powerloss/powerloss.so tools/powerloss/powerloss.c
sh tools/powerloss/powerloss.sh "$(command -v fossil)" /tmp/pl wal
```

Give the `fossil` binary as an absolute path, because the script runs it from
other directories. A statically linked `fossil` doesn't load the library, so
the script finds no stop points.

Set `STOP_MODE=kill` in the environment of `powerloss.sh` for the
killed-process test.

Output from Fossil 2.29 with a WAL repository:

```text
repository journal mode: wal
check-out journal mode: delete
commit without a stop: exit 0, 13 stop points
1 unlink /r.fossil-wal checkout=5 repo-latest=5 integrity=ok/ok consistent
2 fsync /ck/.fslckout-journal checkout=5 repo-latest=5 integrity=ok/ok consistent
3 fsync /ck/.fslckout-journal checkout=5 repo-latest=5 integrity=ok/ok consistent
4 fsync /ck/.fslckout checkout=5 repo-latest=5 integrity=ok/ok consistent
5 unlink /ck/.fslckout-journal checkout=5 repo-latest=5 integrity=ok/ok consistent
6 fsync /ck/.fslckout-journal checkout=5 repo-latest=5 integrity=ok/ok consistent
7 fsync /ck/.fslckout-journal checkout=5 repo-latest=5 integrity=ok/ok consistent
8 fsync /ck/.fslckout checkout=5 repo-latest=5 integrity=ok/ok consistent
9 fsync /r.fossil-wal checkout=5 repo-latest=5 integrity=ok/ok consistent
10 unlink /ck/.fslckout-journal checkout=8 repo-latest=5 integrity=ok/ok AHEAD
11 fsync /r.fossil-wal checkout=8 repo-latest=8 integrity=ok/ok consistent
12 fsync /r.fossil checkout=8 repo-latest=8 integrity=ok/ok consistent
13 unlink /r.fossil-wal checkout=8 repo-latest=8 integrity=ok/ok consistent
```

Each line gives the verdict:

* **consistent:** the checkout db records the repository's latest check-in.
* **BEHIND:** the repository has a later check-in than the checkout db records.
  `fossil status` works and still lists the changes as pending, and `fossil
  commit` stops with "Would fork". `fossil update` moves the checkout to the
  new check-in, warning of merge conflicts on the way, and after it nothing is
  pending.
* **AHEAD:** the checkout db records a check-in that the repository does not
  have. `fossil status`, `changes`, `commit`, `update` and `ls` all fail with
  "Local check-out database is out of sync with repository file".

At point 10 above, the checkout db has committed, and the repository's commit
is in the WAL but SQLite has not synced it. Fossil builds SQLite with
`-DSQLITE_DEFAULT_WAL_SYNCHRONOUS=1` (see `tools/makemake.tcl`), so a WAL
repository runs at `synchronous=NORMAL`, where SQLite syncs the WAL at a
checkpoint and not at each commit. The checkout db is in DELETE mode at
`synchronous=FULL`, so its commit is synced. A power loss there loses the
repository's commit and leaves the checkout db ahead of the repository. `fossil
close` and `fossil open --force` fail there with the same message, so the way
out is to delete `.fslckout` and run `fossil open --force --keep`. That keeps
your edits, and you redo the pending adds, removes and renames. With a DELETE
repository the same run has 14 stop points and is consistent at every one. In
kill mode a WAL repository has 122 stop points, with one BEHIND and no AHEAD.

## Testing SQLite programs other than Fossil

Only `is_db()` in `powerloss.c` and all of `powerloss.sh` are specific to
Fossil. The rest of the C program is generic:

* `is_db()` decides which files to track, matching names that contain `.fossil`
  or `.fslckout`. Change it to match the program's database files.
* The library intercepts `open()`, `open64()`, `close()`, `fsync()`,
  `fdatasync()`, `unlink()`, `pwrite()`, `pwrite64()`, `write()`, `fopen()`,
  `fopen64()`, `fclose()` and `fwrite()`. Other programs and other operating
  systems may write through other calls. Check your binary or library that
  contains SQLite with `nm -D FILE | grep -E ' U
  (f?open|f?close|f(data)?sync|unlink|[fp]?write|lseek|ftruncate|rename)'`.
* Write a driver for the program to replace `powerloss.sh`.

## Stuff it doesn't do

* At a power loss, every write since a file's last sync is lost. A real disk
  may retain some unsynced writes, in any order, or a partly written page; the
  library does not test those cases.
* The library treats an `unlink()` as reaching the disk at once and ignores
  directory syncs.
* The library tracks file descriptors below 4096 and at most 64 stdio streams.
  `MAXFD` and `MAXFILE` in `powerloss.c` set those limits.
* A process that does not load the library, such as one started with
  `LD_PRELOAD` removed from its environment, is neither counted nor stopped.
* At a power loss, the other processes of the run aren't killed. Each one exits
  with status 77 at its next write, sync or unlink of a tracked file, and can
  write other files until then.
* The library tracks only the database files. The other files under
  `STOP_DIR/run`, such as the files of a check-out, stay as the program wrote
  them. A real power loss can lose what was written to a file that was never
  synced, and Fossil doesn't sync the files it writes into a check-out.
* The library doesn't intercept `ftruncate()` or `rename()`. A truncate isn't a
  stop point, and a tracked file that the program renames has no saved copy
  under its new name, so a power loss deletes it.
* The library is for a single-threaded program such as Fossil. It locks its
  state file with `fcntl()`, which doesn't stop two threads of one process from
  writing at the same time.
