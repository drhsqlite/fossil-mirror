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
  The default is `smoke`, which runs something from every file but
  `powerloss`. `ci` adds `powerloss`, the full hand-written cases of
  `unicode-pathnames` and `utf`, and the slower tests of `content`,
  `cgi`, `login` and `config`. `stress` adds generated cases for
  `unicode-pathnames` and `utf`, and the rest of the coverage tests,
  which aim at every branch and condition of the code they test. At
  `smoke` each coverage test file runs only its first sections, about
  half a second of them. The Coverage table below says which files need
  which level.

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
| a Unix shell | `ssh`, `xfer-client`, and the no-reply and merge-tool tests in `sync` and `regress` |
| `git` | `git-export` |
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
| Clone, push, pull and sync | `sync` | `smoke` for the first sections, and the rest at `stress` |
| HTTP and HTTPS client | `http` | `smoke` |
| Rebuild, reconstruct, and `test-integrity` and `test-missing` on damaged repositories | `integrity` | `smoke` |
| Commands that must not change what a repository holds: `rebuild`, `repack`, `deconstruct`, `reconstruct` and `detach` | `rebuild` | `smoke` for the first sections, and the rest at `stress` |
| The content store: long delta chains, the cache of reconstructed artifacts, making and undoing deltas, content that arrives late, and what `test-integrity --parse`, `test-missing` and `test-orphans` report | `content` | `smoke`, and the cache limits at `ci` |
| What the database shows after an artifact of each kind is stored, and what the artifact parser refuses | `crosslink` | `smoke` for the first sections, and the rest at `stress` |
| Check-ins read by name and their files found: two check-ins compared, `/dir` and `/tree`, delta manifests, file names in another letter case, and the JSON form of each kind of artifact | `manifest` | `smoke` |
| How a name from a user or a URL becomes an artifact | `name` | `smoke` for the first sections, and the rest at `stress` |
| Commands interrupted by a power loss or a killed process | `powerloss` | `ci` |
| Git export and import, bundles, patches, ZIP and tarball archives | `roundtrip`, `git-export` | `smoke` |
| Sync protocol cards, sent to the server and to the client | `xfer-server`, `xfer-client` | `smoke` for the first sections, and the rest at `stress` |
| The login card of the sync protocol, checked by a server without `--localauth` | `xfer-server` | `smoke` for the first sections, and the rest at `stress` |
| A clone made over the sync protocol, with and without unversioned files, with small replies, and refused | `sync` | `smoke` for the first sections, and the rest at `stress` |
| Whole pushes, pulls and syncs: private check-ins, unversioned files in both directions, and a pull from a parent project | `sync` | `smoke` for the first sections, and the rest at `stress` |
| URLs of remote repositories, and well-formed and malformed replies to the sync client | `url` | `smoke` for the first sections, and the rest at `stress` |
| `fossil configuration` export, import, merge and reset, and the configuration that push, pull and sync transfer | `config` | `smoke` for the first sections, and the rest at `ci` |
| Commands that remove content: `purge` with its graveyard, undo and obliterate, `scrub`, and `repack` | `purge` | `smoke` for the first sections, and the rest at `stress` |
| How a server reads a request: query strings, forms, uploads in several parts, dates in headers, SCGI, and requests over ssh | `cgi` | `smoke` for the first request of each kind, and the rest at `ci` |
| How a server decides who the user is | `login` | `smoke`, and the logins that Fossil delays by a second at `ci` |
| `fossil import --svn` of dumps whose contents are svndiff0 deltas | `import` | `smoke` for the first sections, and the rest at `stress` |
| What Fossil puts on an `ssh` command line | `ssh` | `smoke` |
| Diffs and deltas | `delta1`, `diff` | `smoke` |
| Fossil's text buffer, `src/blob.c` | `blob` | `smoke` for the first sections, and the rest at `stress` |
| Malformed input to the delta and manifest parsers, from the inputs in `corpus/corpus.pack` (needs a fossil with `test-fuzz-inert`) | `fuzz` | `smoke` |
| Random integration tests of sync: sequences of commits, merges and syncs among three clones, with lost and cut-short sync messages, from the scenario inputs in `corpus/corpus.pack`. Slow for its 16 tests, because each step is a separate run of `fossil`. | `scenario` | `smoke` for one input, and the rest at `stress` |
| Single faults with no better home | `regress` | `smoke` |
| Settings | `settings`, `settings-repo`, `set-manifest` | `smoke` |
| Wiki, attachments and unversioned files | `wiki`, `unversioned` | `smoke` |
| TH1 scripting | `th1`, `th1-docs`, `th1-hooks`, `th1-repo`, `th1-taint`, `th1-tcl` | `smoke` |
| Fossil's own TH1 commands and the template renderer | `th1main` | `smoke` for the first sections, and the rest at `stress` |
| JSON API | `json` | `smoke` |
| Parsing and file utilities | `cmdline`, `contains-selector`, `file1`, `glob`, `reserved-names`, `utf` | `smoke` |

## Fuzz inputs and run-time checking

`corpus/corpus.pack` has the inputs that `fuzz.test` gives to
`fossil test-fuzz`, and the inputs of `scenario.test`. They are packed in
one file so that the corpus doesn't add hundreds of files to every check-in
manifest, and the tests unpack it themselves. To add an input, or to give a
fuzzing engine a starting corpus, unpack it and pack it again:

```sh
tclsh corpus.tcl unpack corpus/corpus.pack DIR
tclsh corpus.tcl pack DIR corpus/corpus.pack
```

`corpus.tcl` describes the format and each type of input.

A build with ASan and UBSan reports memory errors and undefined behavior that a normal build does not detect. `tools/testbuild.sh asan DIR` makes such a build in `DIR`, and `make test` there runs the whole suite against it. `tools/testbuild.sh cov DIR` makes a build that records coverage, and `tools/testbuild.sh report DIR` then prints the branch coverage of each source file.

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
* If a test needs a program to stand in for a server, `ssh` or a merge tool, look in `standin/` before writing another script inside a Tcl string. `test_standin NAME DESTINATION` copies one into the test's directory.
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

Before you write a helper procedure, look in `tester.tcl` for one. Each has
a comment saying what it does. There are helpers for these jobs:

* asking a repository a question in SQL: `fossil_sql`
* requesting a web page without a server: `request_get` or `request_post`,
  then `fossil_http`, then `reply_status`, `reply_header` and `reply_body`
* answering Fossil when it is the client: `test_listen`
* checking that a command left a repository as it was: `repository_state`
  before and after, then `repository_state_diff`
* checking that a repository isn't damaged: `repository_sound`
* hashing a file or a string: `sha3_file`, `sha3_text` and the `sha1`
  pair
* checking output for several strings at once: `holds_strings`
* starting and stopping `fossil server`: `test_start_server` and
  `test_stop_server`

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

`http.test` runs `http-relay.py`, and `tools/testbuild.sh` reads `coverage-marks.txt`. The remaining files are helper scripts and test data.
