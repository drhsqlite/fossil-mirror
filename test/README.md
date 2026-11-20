# Fossil test suite

This is an extensible test suite written in Tcl, where a runner executes all the test files it finds in the test/ directory. Some of the tests derive their
cases and expecations at runtime, including from the Fossil source code (when checking to see if the Fossil binary agrees with the source) and from
testing the behaviour of the filesystem the binary is running on (when checking unicode pathname case folding.) There is also some degree of checking for
platforms (eg is_windows) but that is kept to a minimum.

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

 `make test TESTFLAGS="-quiet update"` passes options and names via `make`. 

A full run normally takes a few minutes. `tester.tcl` exits with status 0 when all tests pass, 1 when one or more tests fail. Failed tests are reported with
what it expected vs what it got. The summary lists skipped files with a reason, and all known bugs reported as `FAILED (knownBug)`. Skips and known bugs do not
change the exit status unless `-strict` is given.

A new test should only require adding another `.test` file.

## Requirements

* Tcl 8.6 or later, we also test with Tcl 9.0 which is now common. `configure` finds `tclsh`, and `make test` fails if it finds none.
* A writable temporary directory, from `TMPDIR`, `TEMP` or `TMP`, falling back to `/tmp`. All writes are made to the temporary directory, except with `-prot`
  which writes a session log to wherever it was started from.
* `src/` which must be next to `test/`, just as it is in the source tree. Otherwise `settings.test` and `settings-repo.test` can't read the settings from the source.

The following tests are skipped when their requirements are missing:

| Needs | Test files |
| --- | --- |
| a TCP connection over the loopback interface | `unversioned` |
| `configure --json` | `json` |
| `configure --with-th1-docs` | `th1-docs` |
| `configure --with-th1-hooks` | `th1-hooks` |
| `configure --with-tcl` | `th1-tcl` |

None of the tests need network access. That doesn't mean all network-related tests will work, because some isolated environments don't even have loopback and
the test suite detects this.

## Coverage

| Area | Test files |
| --- | --- |
| Check-ins and the working tree | `amend`, `clean`, `commit-warning`, `mv-rm`, `revert`, `stash`, `symlinks`, `unicode-pathnames`, `update` |
| Branches and tags | `branch-tag` |
| Merges, cherrypicks, backouts, and annotate and blame after them | `merge1` to `merge6`, `merge-history`, `merge_exe`, `merge_renames`, `merge_renames_2`, `merge_warn` |
| Git export and import, bundles, patches, ZIP and tarball archives | `roundtrip` |
| Diffs and deltas | `delta1`, `diff` |
| Settings | `settings`, `settings-repo`, `set-manifest` |
| Wiki, attachments and unversioned files | `wiki`, `unversioned` |
| TH1 scripting | `th1`, `th1-docs`, `th1-hooks`, `th1-repo`, `th1-taint`, `th1-tcl` |
| JSON API | `json` |
| Parsing and file utilities | `cmdline`, `contains-selector`, `file1`, `glob`, `reserved-names`, `utf` |

## Writing new tests

Each `NAME.test` file is a Tcl script sourced by `tester.tcl`. `test_setup` creates and opens a repository in a temporary directory. `fossil ARGS` runs the
executable being tested and sets `RESULT` to its output and `CODE` to 0 if the command succeeded and did not write anything to standard error, or 1 otherwise. `test_eq NAME ACTUAL EXPECTED` and `test_match NAME ACTUAL PATTERN`
report both values when they fail, and `test NAME EXPRESSION` gives the report. `test_skip REASON` skips the rest of the file, `test_skip_part REASON`
reports a few skipped tests while the file continues, and `test_fail_then_return NAME
REASON` records a failed setup step and stops the file. A last argument of `knownBug` to any of the `test` procedures marks a known bug as described above.

## Maintaining the tests

First step is noticing bugs. One pattern is when Fossil's output changes causing tests that rely on changed output to fail. `tester.tcl` prints the expected and the actual text for each but this won't always be obvious. It's possible that output changes may mask a code failure too.

If you suspect an output-change bug, use `-verbose` to print the output of every command, and `-keep` to leave test repos in place for inspection. `rewrite-test-output.tcl` replaces hashes, process IDs and temporary paths in the output of a run with fixed strings, so you can use `diff` to compare two runs. You should then be able to update the expected output text.

When a test finds a fault that is not yet fixed, add `knownBug` as the test's last argument, so that the suite still exits with status 0. A known bug that passes is reported as `OK (knownBug)?`. Don't forget to remove `knownBug` when you're sure the fault is fixed.

If the known bug is the exit status of a `fossil` command, add `-knownBug` to that command too. Otherwise the command prints an `ERROR` line on every run, even with `-quiet`. With `-knownBug` you only see the error line under `-strict`.

A test marked `knownBug` should fail on every applicable system. If the fault only shows up in some environments, the test must set up the relevant environment. If Fossil really does behave differently on some system depending how the system is configured (for example decomposed pathnames on macOS, which vary depending whether the filesystem is HFS+ or APFS) the test must check the behavior. The test suite avoids operating system names by testing for behaviors instead, wherever possible. If a test needs something the system might not have, that becomes a requirement and you can use `test_skip` for.

`fixed_word_lines NLINE` is a procedure in `tester.tcl` that returns generated text as a string, the same text on every run. `delta1.test` and `merge2.test` use it for their input. If your new test needs text to work on start with this. Look at delta and merge tests for examples of how to use this with character encodings.

Beware of global variables! If you feel you really have to create a new global variable, be sure there isn't a collision with other files. Some future design
of tester.tcl should avoid this problem.

`make test` runs only the `.test` files. The other scripts in `test/` are run by hand:

* `release-checklist.wiki` lists the checks to make before a release, with the `.wiki` and `.md` pages they use.
* `many-www.tcl` requests about 10,000 web pages from an open checkout with `fossil test-http` and times each, to find crashes and slow pages. `valgrind-www.tcl` requests hundreds of pages under valgrind to find memory errors.
* `update-test-1.sh` and `update-test-2.sh` run `fossil update` scenarios in an empty directory.
* `link-tester.html` steps through a list of links in `fossil ui`, for checking each page by eye.

The remaining files are helper scripts and test data.
