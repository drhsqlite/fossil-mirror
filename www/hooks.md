# Hooks

Hooks are short scripts that Fossil runs at defined points of processing.
Administrators can use hooks to help enforce policy or connect Fossil to
a continuous integration (CI) system.

## Interim Documentation.

  *  This is a work-in-progress.  The interface is in flux.
     For the time being, the documentation is a list of
     bullet points.  We hope to transform this into a proper document
     later, after things settle down.

  *  Contributions and suggestions to the hook system and/or the
     documentation are welcomed.

## General Notes.

  *  Each hooks has a "type", a "sequence", and a "command".  The command
     is a shell command that runs at the appropriate time.  The type
     is currently one of "after-receive", "before-commit", "commit-msg",
     or "disabled". The sequence is an arbitrary integer.

  *  There can be multiple hooks of the same type.  When that is the
     case, the hooks are run in order of ascending sequence.

  *  Use the "fossil hook" command to create, edit, and delete hooks.

  *  Use the "fossil hook test" command to test new hooks.

## Hook Scripts

  *  All scripts are expected to run relatively quickly.  If a long-running
     process is started by a hook, it should be run in the background so
     that the original script can return.

  *  The "%F" sequence inside the script is translated into the
     name of the fossil executable.

  *  The "%R" sequence in the script is translated into the name of
     the repository.

  *  The "%A" sequence becomes the name of an auxiliary input file,
     the meaning of which depends on the hook type.  The auxiliary filename
     might be an empty string.  Take care to use appropriate quoting!

## Disabled Hooks

  *  Hooks with type "disabled" never run.  They are a place-holder for
     scripts that might be converted to some other hook-type later.
     For example, the command "fossil hook edit --type disabled ID"
     can be used to temporarily disable the hook named ID, and then
     "fossil hook edit --type after-receive ID" can be used to reenable
     it later.

## After-Receive Hooks

  *  The "after-receive" hook is run by [the backoffice](./backoffice.md)
     whenever new artifacts are received into the repository.  The artifacts
     have already been committed and so there is nothing that the
     after-receive hook can do to block them.

  *  The after-receive hooks are intended to be run on a server to start
     up a background testing or CI process.  But they can also be run
     on the client side.  The key point is that after-receive hooks are
     invoked by backoffice, so backoffice must be running in order to
     fire after-receive hooks.

  *  The exit code from the after-receive script is ignored.

  *  The standard input to the after-receive hook is a list of
     new artifacts, one per line.  The first token on each line is the
     hash of the new artifact.  After the hash is a human-readable text
     description of what the artifact represents.

  *  Sometimes the same artifact can represent two or more things.
     For example, the same artifact might represent two or more files
     in the check-out (assuming the files hold identical content).  In
     that case, the text description that is input to the after-receive
     hook only shows one of the possible uses for the artifact.

  *  If two or more pushes occur against a repository at about the same
     time, then the set of artifacts added by both pushes might be
     combined into a single after-receive callback.

  *  Fossil holds a write transaction on the repository while the
     after-receive hook is running.  If the script needs to access the
     database, then the database will need to be in WAL mode so that
     readers can co-exist with the writer.  Or the script might just
     launch a background process that waits until the hook script finishes
     and the transaction commits before it tries to access the repository
     database.

  *  A push might not deliver all of the artifacts for a checkin.  If
     Fossil knows that a /xfer HTTP request is incomplete, it will defer
     running the after-receive push for 60 seconds, or until a complete
     /xfer request is received.  This helps to prevent after-receive hooks
     from running when incomplete checkins exist in the repository, but
     it does not provide hard guarantees, as there is no way to do that
     in a distributed system.

  *  The list of artifacts delivered to standard input of the
     after-receive hook will not contain more than 24-hours worth
     of artifacts.  If the backoffice has been shut down for a while
     such that after-receive hooks have not been running, and more
     than 24-hours of changes have accumulated since the last run
     of an after-receive hook, then only the most recent 24-hours
     is included in the input.

## Before-Commit Hooks

  *  Before-commit hooks run during the "fossil commit" command before
     the user is prompted for the check-in comment.  Fossil holds
     a write-transaction on the repository when the before-commit
     hook is running, so the repository needs to be in WAL mode if the
     script needs to access the repository.

  *  The %A substitution is the name of a "commit description file" that
     shows the details of the commit in progress.  To see what a
     "commit description file" looks like, set a before-commit hook
     with a command of "cat %A" and then run a sample commit with
     the --dry-run option.

  *  If any before-commit hook returns a non-zero exit code, then
     the commit is abandoned.  All
     before-commit hooks must exit(0) in order for the commit to
     proceed.

  *  The --no-validate flag to the "fossil commit" command prevents any
     before-commit hooks from running.

  *  The --trace flag to the "fossil commit" command shows each
     before-commit hook as it is run.

  *  If a before-commit hook fails, it should print an error message
     on standard output or standard error.  Otherwise, the user won't
     know what went wrong, because Fossil won't tell them.

  *  Nothing is written to standard input of the before-commit hook.
     The information transmitted to the before-commit hook is contained
     in the "%A" auxiliary file.  The before-commit hook must open and
     read that file if it wants access to the commit information.

## Commit-Msg Hooks

  *  Commit-msg hooks are not yet implemented.

  *  The commit-msg hooks run during "fossil commit" after the check-in
     message has been entered by the user.  The "%A" argument to the
     commit-msg hook is the text of the commit message.  The intent
     of the commit-msg hook is to validate the text of the commit
     message to (for example) check for typos or ensure that it
     conforms to standards.

  *  If any commit-msg hook returns a non-zero exit code, then
     the commit is abandoned.  All
     commit-msg hooks must exit(0) in order for the commit to
     proceed.

  *  Commit-msg hooks are advisory only.  Each developer is in total
     control of the local repository and can easily bypass the hooks
     to cause a non-conforming checkin to be committed.
