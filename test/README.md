# Fossil test suite

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

The names after the executable select test files, eg `update` runs `test/update.test`.

Options are:

* `-quiet` prints only failures, skipped files and the summary.
* `-verbose` prints the output of every `fossil` command.
* `-strict` counts the failure of a known bug as a failure.
* `-halt` stops at the first failure.
* `-keep` leaves the temporary repositories in place.
* `-prot` also writes the output to the file `prot`.

 `make test TESTFLAGS="-quiet update"` passes options and names via `make`. 

A full run normally takes a few minutes. `tester.tcl` exits with status 0 when every test passes, and 1 when one or more tests fail. Failed tests are reported with what it expected vs what it got. The summary lists each skipped file with a reason, and known bugs. A known bug is a test for a fault that isn't fixed yet, marked `knownBug` in its test file; its failure is reported as `FAILED (knownBug)` and does not change the exit status unless `-strict` is given.

## Requirements

* Tcl 8.6 or later. `configure` finds `tclsh`, and `make test` fails if it finds none.
* A writable temporary directory, from `TMPDIR`, `TEMP` or `TMP`, falling back to `/tmp`. All writes are made to the temporary directory, except with `-prot`
  which writes a session log to wherever it was started from.
* `src/` which must be next to `test/`, just as it is in the source tree. Otherwise `settings.test` and `settings-repo.test` can't read the settings from the source.

The following tests are skipped when their requirements are missing:

| Needs | Test files |
| --- | --- |
| a TCP connection over the loopback interface | `unversioned` |
| the `sha1` package from tcllib | `set-manifest`, `unversioned` |
| `configure --json` | `json` |
| `configure --with-th1-docs` | `th1-docs` |
| `configure --with-th1-hooks` | `th1-hooks` |
| `configure --with-tcl` | `th1-tcl` |

None of the tests need network access (but that doesn't mean all network-related tests will work, some isolated environments don't even have loopback and the test
suite accounts for that.)

## Coverage

| Area | Test files |
| --- | --- |
| Check-ins and the working tree | `amend`, `clean`, `commit-warning`, `mv-rm`, `revert`, `stash`, `symlinks`, `update` |
| Merges | `merge1` to `merge6`, `merge_exe`, `merge_renames`, `merge_renames_2`, `merge_warn` |
| Diffs and deltas | `delta1`, `diff` |
| Settings | `settings`, `settings-repo`, `set-manifest` |
| Wiki, attachments and unversioned files | `wiki`, `unversioned` |
| TH1 scripting | `th1`, `th1-docs`, `th1-hooks`, `th1-repo`, `th1-taint`, `th1-tcl` |
| JSON API | `json` |
| Parsing and file utilities | `cmdline`, `contains-selector`, `file1`, `glob`, `reserved-names`, `utf` |

## Writing new tests

Each `NAME.test` file is a Tcl script sourced by `tester.tcl`. `test_setup` creates and opens a repository in a temporary directory. `fossil ARGS` runs the
executable being tested and sets `RESULT` to its output and `CODE` to its exit status. `test_eq NAME ACTUAL EXPECTED` and `test_match NAME ACTUAL PATTERN`
report both values when they fail, and `test NAME EXPRESSION` gives the report. `test_skip REASON` skips the rest of the file, and `test_fail_then_return NAME
REASON` records a failed setup step and stops the file. A last argument of `knownBug` to any of the `test` procedures marks a known bug as described above.

## Maintaining the tests

First step is noticing bugs. One pattern is when Fossil's output changes causing tests that rely on changed output to fail. `tester.tcl` prints the expected and the actual text for each but this won't always be obvious. It's possible that output changes may mask a code failure too.

If you suspect an output-change bug, use `-verbose` to print the output of every command, and `-keep` to leave test repos in place for inspection. `rewrite-test-output.tcl` replaces hashes, process IDs and temporary paths in the output of a run with fixed strings, so you can use `diff` to compare two runs. You should then be able to update the expected output text.

When a test finds a fault that is not yet fixed, add `knownBug` as the test's last argument, so that the suite still exits with status 0. A known bug that passes is reported as `OK (knownBug)?`. Don't forget to remove `knownBug` when you're sure the fault is fixed.

Stray files in `test/` can be confusing because `delta1.test` and `merge2.test` run their tests on every file and the counts may differ from what is expected. A different total is not a failure.

Beware of global variables! If you must create a new global variable, be sure there isn't a collision with other files. Some future design should avoid this
problem.

`make test` runs only the `.test` files. The other scripts in `test/` are run by hand:

* `release-checklist.wiki` lists the checks to make before a release, with the `.wiki` and `.md` pages they use.
* `many-www.tcl` requests about 10,000 web pages from an open checkout with `fossil test-http` and times each, to find crashes and slow pages. `valgrind-www.tcl` requests hundreds of pages under valgrind to find memory errors.
* `update-test-1.sh` and `update-test-2.sh` run `fossil update` scenarios in an empty directory.
* `link-tester.html` steps through a list of links in `fossil ui`, for checking each page by eye.

The remaining files are test data.
