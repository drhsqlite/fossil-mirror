# Fossil test suite

Test a given fossil binary for correct behaviour, either explicitly or in some
cases computed from the binary in question and the OS context it is running on.

This is an extensible test suite written in Tcl, where a runner executes all
the test files it finds in the test/ directory. Tests do not necessarily have
to pass, because the optional status *knownBug* constrains a test to merely
report a bug that hasn't yet been fixed. knownBug enables a useful pattern,
where tests are written first and the code is improved to reduce the knownBug
count to zero over time.

Some of the tests derive their cases and expecations (even some knownBugs) at
runtime by examining the running system and its configuration; many others have
test cases hardcoded. Other tests check for particular platforms (eg
`is_windows`) but we wish to keep that to a minimum because of the myriad of
different system configurations that exist within any given operating system
platform.

## Running the tests

From a build directory:

```sh
make test
```

or with any `fossil` executable:

```sh
tclsh /path/to/fossil/test/tester.tcl /path/to/fossil
tclsh /path/to/fossil/test/tester.tcl /path/to/fossil merge1 update
```

Where `merge1` and `update` are names of test files, eg `test/update.test`. 

Options are:

* `-quiet` prints only failures, skipped files and the summary.
* `-verbose` prints the output of every `fossil` command.
* `-strict` counts the failure of a known bug as a failure.
* `-halt` stops at the first failure.
* `-keep` leaves the temporary repositories in place.
* `-prot` also writes the output to the file `prot`.
* `-trials N` sets how many random trials fuzz tests (delta1, merge2, …)
  run per input kind. The default is 20 (5 for expensive `large` /
  `repeats` inputs). Use `-trials 100` for the historical full fuzz.
  This flag is only the fuzz loop count; it does not select
  `-assurance` cases.
* `-assurance smoke|ci|stress` selects how thoroughly the tests run.
  The default is `smoke`, which skips `powerloss`. It also sets the
  cases that `unicode-pathnames` and `utf` run:
  - `unicode-pathnames`: `smoke` is one pair per fold-behavior class;
    `ci` is the full hand-written lists; `stress` adds generated pairs.
    At startup the harness classifies the temporary filesystem once
    (ASCII case, NFC/NFD, and sharp-s versus `ss`). Predictions use
    that classification with Fossil's `case-sensitive` setting
    (byte-for-byte when on; Unicode D145 caseless match when off).
  - `utf`: `smoke` is a stratified edge set of `test-looks-like-utf`
    cells; `ci` is the full BOM×payload×extra product; `stress` adds
    generated payloads. A Tcl predictor scores each LOOK_*/BOM line
    for both yes and no.
  Unmet predictions fail the run.

 `make test TESTFLAGS="-quiet update"` passes options and names via `make`.

Assurance recipes:

```sh
make test                                          # smoke (default)
make test TESTFLAGS="-assurance ci"                # full current coverage
make test TESTFLAGS="-assurance stress"            # ci plus generated cases
``` 

`tester.tcl` exits with status 0 when all tests pass, 1 when one or more tests
fail. Failed tests are reported with what it expected vs what it got. The
summary lists skipped files with a reason, and all known bugs reported as
`FAILED (knownBug)`. Skips and known bugs do not change the exit status unless
`-strict` is given.

A new test should only require adding another `.test` file.

## Requirements

* Tcl 8.6 or later, we also test with Tcl 9.0 which is now common. `configure` finds `tclsh`, and `make test` fails if it finds none.
* A writable temporary directory, from `TMPDIR`, `TEMP` or `TMP`, falling back to `/tmp`. All writes are made to the temporary directory, except with `-prot`
  which writes a session log to wherever it was started from.
* `src/` which must be at the same level in the same directory as `test/`, reflecting the source tree layout. The tests (at least) `settings.test` and `settings-repo.test` need the source to do their job.

Tests are skipped when their requirements are missing:

| Needs | Test files |
| --- | --- |
| a TCP connection over the loopback interface | `http`, `unversioned`, the HTTP tests in `sync`, and the HTTP clone in `powerloss` |
| `-assurance ci` or `stress` | `powerloss` |
| `openssl` and a Fossil built with TLS support | the HTTPS tests in `http` |
| `python3` | the tests in `http` that run `http-relay.py` |
| a C compiler (`cc`, or `CC`), `LD_PRELOAD`, `tools/powerloss/powerloss.c` from the source tree, and a Fossil that isn't statically linked | `powerloss` |
| `configure --json` | `json` |
| `configure --with-th1-docs` | `th1-docs` |
| `configure --with-th1-hooks` | `th1-hooks` |
| `configure --with-tcl` | `th1-tcl` |

[`tools/powerloss/README-POWERLOSS.md`](../tools/powerloss/README-POWERLOSS.md) describes the `powerloss` library dependency.

None of the tests need network access. That doesn't mean all network-related tests will work, because some isolated environments don't even have loopback and
the test suite detects this.

## Coverage

The last column is the lowest `-assurance` level that runs the files: `smoke`
means every run, and `ci` means `ci` and `stress`.

| Area | Test files | Runs at |
| --- | --- | --- |
| Check-ins and the working tree | `amend`, `clean`, `commit-warning`, `mv-rm`, `revert`, `stash`, `symlinks`, `unicode-pathnames`, `update` | `smoke` |
| Branches and tags | `branch-tag` | `smoke` |
| Merges, cherrypicks, backouts, and annotate and blame after them | `merge1` to `merge6`, `merge-history`, `merge_exe`, `merge_renames`, `merge_renames_2`, `merge_warn` | `smoke` |
| HTTP and HTTPS client | `http` | `smoke` |
| Commands interrupted by a power loss or a killed process | `powerloss` | `ci` |
| Diffs and deltas | `delta1`, `diff` | `smoke` |
| Settings | `settings`, `settings-repo`, `set-manifest` | `smoke` |
| Wiki, attachments and unversioned files | `wiki`, `unversioned` | `smoke` |
| TH1 scripting | `th1`, `th1-docs`, `th1-hooks`, `th1-repo`, `th1-taint`, `th1-tcl` | `smoke` |
| JSON API | `json` | `smoke` |
| Parsing and file utilities | `cmdline`, `contains-selector`, `file1`, `glob`, `reserved-names`, `utf` | `smoke` |
| Git export and import, bundles, patches, ZIP and tarball archives | `roundtrip` |

## Writing new tests

Each `NAME.test` file is a Tcl script sourced by `tester.tcl`. `test_setup` creates and opens a repository in a temporary directory. `fossil ARGS` runs the
executable being tested and sets `RESULT` to its output and `CODE` to 0 if the command succeeded and did not write anything to standard error, or 1 otherwise. `test_eq NAME ACTUAL EXPECTED` and `test_match NAME ACTUAL PATTERN`
report both values when they fail, and `test NAME EXPRESSION` gives the report. `test_skip REASON` skips the rest of the file, `test_skip_part REASON`
reports a few skipped tests while the file continues, and `test_fail_then_return NAME
REASON` records a failed setup step and stops the file.

A test file looks like this:

```tcl
#
# What this file tests, in a sentence or two.
#

require_no_open_checkout

test_setup

write_file a.txt "one\n"
fossil add a.txt
fossil commit -m one
test example-commit {$CODE == 0}

test_cleanup
```

* Start each test name and each procedure name with the file's name or a short form of it, such as `sync-clone-artifacts` and `sync_commit`. A failure report then says which file to open.
* Comment a procedure only if its name and arguments don't say what it does. Don't repeat the code in a comment.
* Call your global variables what you like. `tester.tcl` removes them when your file ends, so two files can't collide. Don't set a variable that `tester.tcl` owns, such as `RESULT`, `CODE`, `testdir` or `fossilexe`.
* If the file needs something the system might not have, call `test_skip` near the top so the summary shows it.
* `fossil ARGS` counts output on standard error as a failure and can't tell a crash from an error. If your test expects either, reads the output as bytes, gives `fossil` input or needs a time limit, use `fossil_raw ARGS`, which sets `CODE` to the exit status, `crash` or `timeout`. Don't call `exec $::fossilexe` yourself.
* Read a file with `read_file` and write one with `write_file`.
* Keep lines within 80 columns.

A command that fails under `fossil_raw` isn't reported as an error, so
check `CODE` yourself. Use `fossil_raw` when you need:

* the output byte for byte, such as CRLF line endings or binary content
* the exit status, or to tell a crash from a failure
* text on standard error from a command that succeeds
* standard input, with `-input TEXT`
* a time limit, with `-seconds N`. The default is 600.
* a server in the test's own Tcl. The Tcl event loop runs while the
  command does, so a `socket -server` in your test file can answer it.

`holds_strings TEXT WANTED` returns 1 if `TEXT` has every string in the
list `WANTED`, where a string starting with `!` must be absent.
`test_start_server` takes options for `fossil server` after its second
argument.

## Maintaining the tests

First step is noticing bugs. One pattern is when Fossil's output changes causing tests that rely on changed output to fail. `tester.tcl` prints the expected and the actual text for each but this won't always be obvious. It's possible that output changes may mask a code failure too.

If you suspect an output-change bug, use `-verbose` to print the output of every command, and `-keep` to leave test repos in place for inspection. `rewrite-test-output.tcl` replaces hashes, process IDs and temporary paths in the output of a run with fixed strings, so you can use `diff` to compare two runs. You should then be able to update the expected output text.

When a test finds a fault that is not yet fixed, add `knownBug` as the test's last argument, so that the suite still exits with status 0. A known bug that passes is reported as `OK (knownBug)?`. Don't forget to remove `knownBug` when you're sure the fault is fixed.

If the known bug is the exit status of a `fossil` command, add `-knownBug` to that command too. Otherwise the command prints an `ERROR` line on every run, even with `-quiet`. With `-knownBug` you only see the error line under `-strict`.

A test marked `knownBug` should fail on every applicable system. If the fault only shows up in some environments, the test must set up the relevant environment. If Fossil really does behave differently on some system depending how the system is configured (for example decomposed pathnames on macOS, which vary depending whether the filesystem is HFS+ or APFS) the test must check the behavior. The test suite avoids operating system names by testing for behaviors instead, wherever possible. If a test needs something the system might not have, that becomes a requirement and you can use `test_skip` for.

`fixed_word_lines NLINE` is a procedure in `tester.tcl` that returns generated text as a string, the same text on every run. `delta1.test` and `merge2.test` use it for their input. If your new test needs text to work on start with this. Look at delta and merge tests for examples of how to use this with character encodings.

`make test` runs only the `.test` files. The other scripts in `test/` are run by hand:

* `release-checklist.wiki` lists the checks to make before a release, with the `.wiki` and `.md` pages they use.
* `many-www.tcl` requests about 10,000 web pages from an open checkout with `fossil test-http` and times each, to find crashes and slow pages. `valgrind-www.tcl` requests hundreds of pages under valgrind to find memory errors.
* `update-test-1.sh` and `update-test-2.sh` run `fossil update` scenarios in an empty directory.
* `link-tester.html` steps through a list of links in `fossil ui`, for checking each page by eye.

The remaining files are helper scripts and test data.
