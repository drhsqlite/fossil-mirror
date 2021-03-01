#compdef fossil
# Origin: https://chiselapp.com/user/lifepillar/repository/fossil-zsh-completion
#################################################################################
#                                                                               #
# Copyright 2020 Lifepillar                                                     #
#                                                                               #
# Permission is hereby granted, free of charge, to any person obtaining a copy  #
# of this software and associated documentation files (the "Software"), to deal #
# in the Software without restriction, including without limitation the rights  #
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell     #
# copies of the Software, and to permit persons to whom the Software is         #
# furnished to do so, subject to the following conditions:                      #
#                                                                               #
# The above copyright notice and this permission notice shall be included in    #
# all copies or substantial portions of the Software.                           #
#                                                                               #
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR    #
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,      #
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE   #
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER        #
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, #
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE #
# SOFTWARE.                                                                     #
#                                                                               #
#################################################################################

# To reload the completion function after it has been modified:
#
# $ unfunction _fossil
# $ autoload -U _fossil
#
# See also: http://zsh.sourceforge.net/Doc/Release/Completion-System.html
# See also: https://github.com/zsh-users/zsh-completions/blob/master/zsh-completions-howto.org

################################################################################
# Functions that help build this completion file                               #
################################################################################

# This function can be used to generate scaffolding code for the options of all
# the commands. Copy and paste the result at the suitable spot in this script
# to update it. To parse all commands:
#
#     __fossil_parse_help -a
#
# To parse all test commands:
#
#     __fossil_parse_help -t
#
# NOTE: The code must be adapted manually. Diff with previous version!
function __fossil_parse_help() {
  echo '      case "$words[1]" in'
  for c in `fossil help $1 | xargs -n1 | sort`;
  do
    echo "        ($c)"
    echo '          _arguments \\'
    __fossil_format_options $c;
    echo "          '(- *)'--help'[Show help and exit]' \\"
    echo "          '*:files:_files'"
    echo ''
    echo '          ;;'
  done;
  echo '      esac'
  echo '      ;;'
}

# Extract the options of a command and format it in a way that can be used in
# a ZSH completion script.
# Use `__fossil_format_options -o` to extract the common options.
function __fossil_format_options() {
  fossil help $1 2>&1 \
    | grep '^\s\{1,3\}-' \
    | sed -E 's/^ +//' \
    | awk -F ' +' '{
    v=match($1,/\|/)
    split($1,y,"|")
    printf "          "
    if (v>0)
      printf "\"(--help %s %s)\"{%s,%s}",y[1],y[2],y[1],y[2];
    else
      printf "\"(--help %s)\"%s",y[1],y[1];
    $1=""
    gsub(/^ +| +$/,"",$0);
    gsub(/^ +| +$/,"",$0);
    gsub(/\x27/,"\x27\"\x27\"\x27",$0);
    print "\x27["$0"]\x27 \\";
  }'
}


################################################################################
# Helper functions used for completion.                                        #
################################################################################

function __fossil_commands() {
  fossil help --all
}

function __fossil_test_commands() {
  fossil help --test
}

function __fossil_all_commands() {
  __fossil_commands
  __fossil_test_commands
}

function __fossil_users() {
  fossil user ls 2>/dev/null | awk '{print $1}'
}

function __fossil_branches() {
  fossil branch ls -a 2>/dev/null | sed 's/\* *//'
}

function __fossil_tags() {
  fossil tag ls 2>/dev/null
}

function __fossil_repos() {
  ls | grep .fossil
  fossil all ls 2>/dev/null
}

function __fossil_remotes() {
  fossil remote list 2>/dev/null | awk '{print $1}'
}

function __fossil_wiki_pages() {
  fossil wiki list 2>/dev/null
}

function __fossil_areas() {
  compadd all email project shun skin ticket user alias subscriber
  return 0
}

function __fossil_settings() {
  fossil help --setting
}

function __fossil_urls() {
  local u
  u=($(__fossil_remotes))
  compadd -a u
  compadd -S '' file:// http:// https:// ssh://
  return 0
}

################################################################################
# Main                                                                         #
################################################################################

function _fossil() {
  local context state state_descr line
  typeset -A opt_args

  local -a _common_options
  # Scaffolding code for common options can be generated with `__fossil_format_options -o`.
  _common_options=(
    "(--help --args)"--args'[FILENAME Read additional arguments and options from FILENAME]:file:_files'
    "(--help --cgitrace)"--cgitrace'[Active CGI tracing]'
    "(--help --comfmtflags --comment-format)"--comfmtflags'[VALUE Set comment formatting flags to VALUE]:value:'
    "(--help --comment-format --comfmtflags)"--comment-format'[VALUE Alias for --comfmtflags]:value:'
    "(--help --errorlog)"--errorlog'[FILENAME Log errors to FILENAME]:file:_files'
    "(- --help)"--help'[Show help on the command rather than running it]'
    "(--help --httptrace)"--httptrace'[Trace outbound HTTP requests]'
    "(--help --localtime)"--localtime'[Display times using the local timezone]'
    "(--help --no-th-hook)"--no-th-hook'[Do not run TH1 hooks]'
    "(--help --quiet)"--quiet'[Reduce the amount of output]'
    "(--help --sqlstats)"--sqlstats'[Show SQL usage statistics when done]'
    "(--help --sqltrace)"--sqltrace'[Trace all SQL commands]'
    "(--help --sshtrace)"--sshtrace'[Trace SSH activity]'
    "(--help --ssl-identity)"--ssl-identity'[NAME Set the SSL identity to NAME]:name:'
    "(--help --systemtrace)"--systemtrace'[Trace calls to system()]'
    "(--help --user -U)"{--user,-U}'[USER Make the default user be USER]:user:($(__fossil_users))'
    "(--help --utc)"--utc'[Display times using UTC]'
    "(--help --vfs)"--vfs'[NAME Cause SQLite to use the NAME VFS]:name:'
  )

  local -a _fossil_clean_options
  _fossil_clean_options=(
    "(--help --allckouts)"--allckouts'[Check for empty directories within any checkouts]'
    "(--help --case-sensitive)"--case-sensitive'[BOOL Override case-sensitive setting]:bool:(yes no)'
    "(--help --dirsonly)"--dirsonly'[Only remove empty directories]'
    "(--help --disable-undo)"--disable-undo'[Disables use of the undo]'
    "(--help --dotfiles)"--dotfiles'[Include files beginning with a dot (".")]'
    "(--help --emptydirs)"--emptydirs'[Remove empty directories]'
    "(--help -f --force)"{-f,--force}'[Remove files without prompting]'
    "(--help -i --prompt)"{-i,--prompt}'[Prompt before removing each file]'
    "(--help -x --verily)"{-x,--verily}'[Remove everything that is not managed]'
    "(--help --clean)"--clean'[CSG Never prompt to delete files matching CSG glob pattern]:pattern:'
    "(--help --ignore)"--ignore'[CSG Ignore files matching CSG glob pattern]:pattern:'
    "(--help --keep)"--keep'[CSG Keep files matching CSG glob pattern]:pattern:'
    "(--help -n --dry-run)"{-n,--dry-run}'[Delete nothing, but display what would have been deleted]'
    "(--help --no-prompt)"--no-prompt'[Assume NO for every question]'
    "(--help --temp)"--temp'[Remove only Fossil-generated temporary files]'
    "(--help -v --verbose)"{-v,--verbose}'[Show all files as they are removed]'
  )

  local -a _fossil_rebuild_options
  _fossil_rebuild_options=(
    "(--help --analyze)"--analyze'[Run ANALYZE on the database after rebuilding]'
    "(--help --cluster)"--cluster'[Compute clusters for unclustered artifacts]'
    "(--help --compress)"--compress'[Strive to make the database as small as possible]'
    "(--help --compress-only)"--compress-only'[Skip the rebuilding step. Do --compress only]'
    "(--help --deanalyze)"--deanalyze'[Remove ANALYZE tables from the database]'
    "(--help --ifneeded)"--ifneeded'[Only do the rebuild if it would change the schema version]'
    "(--help --index)"--index'[Always add in the full-text search index]'
    "(--help --noverify)"--noverify'[Skip the verification of changes to the BLOB table]'
    "(--help --noindex)"--noindex'[Always omit the full-text search index]'
    "(--help --pagesize)"--pagesize'[N Set the database pagesize to N. (512..65536 and power of 2)]:number:'
    "(--help --quiet)"--quiet'[Only show output if there are errors]'
    "(--help --stats)"--stats'[Show artifact statistics after rebuilding]'
    "(--help --vacuum)"--vacuum'[Run VACUUM on the database after rebuilding]'
    "(--help --wal)"--wal'[Set Write-Ahead-Log journalling mode on the database]'
  )

  local -a _fossil_dbstat_options
  _fossil_dbstat_options=(
    "(--help --brief -b)"{--brief,-b}'[Only show essential elements]'
    "(--help --db-check)"--db-check'[Run "PRAGMA quick_check" on the repository database]'
    "(--help --db-verify)"--db-verify'[Run a full verification of the repository integrity]'
    "(--help --omit-version-info)"--omit-version-info'[Omit the SQLite and Fossil version information]'
  )

  local -a _fossil_diff_options
  _fossil_diff_options=(
    "(--help --binary)"--binary'[PATTERN Treat files that match the glob PATTERN as binary]:pattern:'
    "(--help --branch)"--branch'[BRANCH Show diff of all changes on BRANCH]:branch:($(__fossil_branches))'
    "(--help --brief)"--brief'[Show filenames only]'
    "(--help --checkin)"--checkin'[VERSION Show diff of all changes in VERSION]:version:'
    "(--help --command)"--command'[PROG External diff program - overrides "diff-command"]:program:'
    "(--help --context -c)"{--context,-c}'[N Use N lines of context]:number:'
    "(--help --diff-binary)"--diff-binary'[BOOL Include binary files when using external commands]:bool:(yes no)'
    "(--help --exec-abs-paths)"--exec-abs-paths'[Force absolute path names with external commands]'
    "(--help --exec-rel-paths)"--exec-rel-paths'[Force relative path names with external commands]'
    "(--help --from -r)"{--from,-r}'[VERSION Select VERSION as source for the diff]:version:'
    "(--help --internal -i)"{--internal,-i}'[Use internal diff logic]'
    "(--help --new-file -N)"{--new-file,-N}'[Show complete text of added and deleted files]'
    "(--help --numstat)"--numstat'[Show only the number of lines delete and added]'
    "(--help --side-by-side -y)"{--side-by-side,-y}'[Side-by-side diff]'
    "(--help --strip-trailing-cr)"--strip-trailing-cr'[Strip trailing CR]'
    "(--help --tclsh)"--tclsh'[PATH Tcl/Tk used for --tk (default: "tclsh")]'
    "(--help --tk)"--tk'[Launch a Tcl/Tk GUI for display]'
    "(--help --to)"--to'[VERSION Select VERSION as target for the diff]:version:'
    "(--help --undo)"--undo'[Diff against the "undo" buffer]'
    "(--help --unified)"--unified'[Unified diff]'
    "(--help -v --verbose)"{-v,--verbose}'[Output complete text of added or deleted files]'
    "(--help -w --ignore-all-space)"{-w,--ignore-all-space}'[Ignore white space when comparing lines]'
    "(--help -W --width)"{-W,--width}'[NUM Width of lines in side-by-side diff]:number:'
    "(--help -Z --ignore-trailing-space)"{-Z,--ignore-trailing-space}'[Ignore changes to end-of-line whitespace]'
  )

  local -a _fossil_extras_options
  _fossil_extras_options=(
    "(--help --abs-paths)"--abs-paths'[Display absolute pathnames]'
    "(--help --case-sensitive)"--case-sensitive'[BOOL Override case-sensitive setting]:bool:(yes no)'
    "(--help --dotfiles)"--dotfiles'[Include files beginning with a dot (".")]'
    "(--help --header)"--header'[Identify the repository if there are extras]'
    "(--help --ignore)"--ignore'[CSG Ignore files matching patterns from the argument]:pattern:'
    "(--help --rel-paths)"--rel-paths'[Display pathnames relative to the current working]'
  )

  local -a _fossil_server_options
  _fossil_server_options=(
    "(--help --baseurl)"--baseurl'[URL Use URL as the base]:url:__fossil_urls'
    "(--help --create)"--create'[Create a new REPOSITORY if it does not already exist]'
    "(--help --extroot)"--extroot'[DIR Document root for the /ext extension mechanism]:directory:_files -/'
    "(--help --files)"--files'[GLOBLIST Comma-separated list of glob patterns for static files]:pattern:'
    "(--help --localauth)"--localauth'[Enable automatic login for requests from localhost]'
    "(--help --localhost)"--localhost'[Listen on 126.0.0.1 only]'
    "(--help --https)"--https'[Input passes through a reverse HTTPS->HTTP proxy]'
    "(--help --jsmode)"--jsmode'[MODE Determine how JavaScript is delivered with pages]:mode:(inline separate bundled)'
    "(--help --max-latency)"--max-latency'[N Do not let any single HTTP request run for more than N seconds]:number:'
    "(--help --nocompress)"--nocompress'[Do not compress HTTP replies]'
    "(--help --nojail)"--nojail'[Drop root privileges but do not enter the chroot jail]'
    "(--help --nossl)"--nossl'[Signal that no SSL connections are available]'
    "(--help --notfound)"--notfound'[URL Redirect]:url:__fossil_urls'
    "(--help --page)"--page'[PAGE Start "ui" on PAGE. ex: --page "timeline?y=ci"]:number:'
    "(--help -P --port)"{-P,--port}'[TCPPORT Listen to request on port TCPPORT]:number:'
    "(--help --th-trace)"--th-trace'[Trace TH0 execution (for debugging purposes)]'
    "(--help --repolist)"--repolist'[If REPOSITORY is dir, URL "/" lists repos]'
    "(--help --scgi)"--scgi'[Accept SCGI rather than HTTP]'
    "(--help --skin)"--skin'[LABEL Use override skin LABEL]:label:'
    "(--help --usepidkey)"--usepidkey'[Use saved encryption key from parent process]'
  )

  _arguments -C           \
    ${_common_options[@]} \
    '1:command:->command' \
    '*::args:->args'

  case $state in
    (command)
      if [[ $line =~ '^te' ]]; then
        _arguments '*:test commands:($(__fossil_test_commands))'
      else
        _arguments '*:commands:($(__fossil_commands))'
      fi
      ;;
    (args)
      if [[ $line[1] =~ '^test-' ]]; then
        __fossil_complete_test_commands
        return 0
      fi

      case $line[1] in
        (3-way-merge)
          _arguments                            \
            '(- *)'--help'[Show help and exit]' \
            '*:files:_files'

          ;;
        (add)
          _arguments                                                                                              \
            "(--help --case-sensitive)"--case-sensitive'[BOOL Override the case-sensitive setting]:bool:(yes no)' \
            "(--help --dotfiles)"--dotfiles'[include files beginning with a dot (".")]'                           \
            "(--help -f --force)"{-f,--force}'[Add files without prompting]'                                      \
            "(--help --ignore)"--ignore'[CSG Ignore unmanaged files matching Comma Separated Glob]:pattern:'      \
            "(--help --clean)"--clean'[CSG Also ignore files matching Comma Separated Glob]:pattern:'             \
            "(--help --reset)"--reset'[Reset the ADDED state of a checkout]'                                      \
            "(--help -v --verbose)"{-v,--verbose}'[Outputs information about each --reset file]'                  \
            "(--help -n --dry-run)"{-n,--dry-run}'[Display instead of run actions]'                               \
            '(- *)'--help'[Show help and exit]'                                                                   \
            '*:files:_files'

          ;;
        (addremove)
          _arguments                                                                                              \
            "(--help --case-sensitive)"--case-sensitive'[BOOL Override the case-sensitive setting]:bool:(yes no)' \
            "(--help --dotfiles)"--dotfiles'[Include files beginning with a dot (".")]'                           \
            "(--help --ignore)"--ignore'[CSG Ignore unmanaged files matching Comma Separated Glob]:pattern:'      \
            "(--help --clean)"--clean'[CSG Also ignore files matching Comma Separated Glob]:pattern:'             \
            "(--help -n --dry-run)"{-n,--dry-run}'[If given, display instead of run actions]'                     \
            "(--help --reset)"--reset'[Reset the ADDED/DELETED state of a checkout]'                              \
            "(--help --verbose -v)"{--verbose,-v}'[Outputs information about each --reset file]'                  \
            '(- *)'--help'[Show help and exit]'                                                                   \
            '*:files:_files'

          ;;
        (alerts)
          __fossil_alerts

          ;;
        (all)
          __fossil_all

          ;;
        (amend)
          _arguments                                                                                                             \
            ':hash:'                                                                                                             \
            "(--help --author)"--author'[USER Make USER the author for check-in]:user:($(__fossil_users))'                       \
            "(--help -m --comment)"{-m,--comment}'[COMMENT Make COMMENT the check-in comment]:comment:'                          \
            "(--help -M --message-file)"{-M,--message-file}'[FILE Read the amended comment from FILE]:file:_files'               \
            "(--help -e --edit-comment)"{-e,--edit-comment}'[Launch editor to revise comment]'                                   \
            "(--help --date)"--date'[DATETIME Make DATETIME the check-in time]:datetime:(now)'                                   \
            "(--help --bgcolor)"--bgcolor'[COLOR Apply COLOR to this check-in]:color:'                                           \
            "(--help --branchcolor)"--branchcolor'[COLOR Apply and propagate COLOR to the branch]:color:'                        \
            "(--help --tag)"--tag'[TAG Add new TAG to this check-in]:tag:($(__fossil_tags))'                                     \
            "(--help --cancel)"--cancel'[TAG Cancel TAG from this check-in]:tag:($(__fossil_tags))'                              \
            "(--help --branch)"--branch'[NAME Make this check-in the start of branch NAME]:name:($(__fossil_branches))'          \
            "(--help --hide)"--hide'[Hide branch starting from this check-in]'                                                   \
            "(--help --close)"--close'[Mark this "leaf" as closed]'                                                              \
            "(--help -n --dry-run)"{-n,--dry-run}'[Print control artifact, but make no changes]'                                 \
            "(--help --date-override)"--date-override'[DATETIME Set the change time on the control artifact]:datetime:(now)'     \
            "(--help --user-override)"--user-override'[USER Set the user name on the control artifact]:user:($(__fossil_users))' \
            '(- *)'--help'[Show help and exit]'

          ;;
        (annotate|blame|praise)
          _arguments                                                                                              \
            "(--help --filevers)"--filevers'[Show file version numbers]'                                          \
            "(--help -r --revision)"{-r,--revision}'[VERSION The specific check-in containing the file]:version:' \
            "(--help -l --log)"{-l,--log}'[List all versions analyzed]'                                           \
            "(--help -n --limit)"{-n,--limit}'[LIMIT Limit versions]:limit:(none)'                                \
            "(--help -o --origin)"{-o,--origin}'[VERSION The origin check-in]:version:'                           \
            "(--help -w --ignore-all-space)"{-w,--ignore-all-space}'[Ignore white space when comparing lines]'    \
            "(--help -Z --ignore-trailing-space)"{-Z,--ignore-trailing-space}'[Ignore whitespace at line end]'    \
            '(- *)'--help'[Show help and exit]'                                                                   \
            '1:file:_files'

          ;;
        (artifact)
          _arguments                                                                                                               \
            "(--help -R --repository)"{-R,--repository}'[FILE Extract artifacts from repository FILE]:fossils:($(__fossil_repos))' \
            '(- *)'--help'[Show help and exit]'                                                                                    \
            '1:artifact id:'                                                                                                       \
            '2::output file:_files'

          ;;
        (attachment)
          _arguments                                                                                         \
            "(--help -t --technote)"{-t,--technote}'[DATETIME The timestamp of the technote]:datetime:(now)' \
            "(--help -t --technote)"{-t,--technote}'[TECHNOTE-ID The technote to be updated]:technote-id:'   \
            '(- *)'--help'[Show help and exit]'                                                              \
            '1:what:(add)'                                                                                   \
            '::pagename:($(__fossil_wiki_pages))'                                                           \
            ':file:_files'

          ;;
        (backoffice)
          _arguments                                                                                       \
            "(--help --debug)"--debug'[Show what this command is doing]'                                   \
            "(--help --logfile)"--logfile'[FILE Append a log of backoffice actions onto FILE]:file:_files' \
            "(--help --min)"--min'[N Invoke backoffice at least once every N seconds]:number:'             \
            "(--help --poll)"--poll'[N Polling frequency]:number:'                                         \
            "(--help --trace)"--trace'[Enable debugging output on stderr]'                                 \
            "(--help --nodelay)"--nodelay'[Do not queue up or wait for a backoffice job]'                  \
            "(--help --nolease)"--nolease'[Always run backoffice]'                                         \
            '(- *)'--help'[Show help and exit]'                                                            \
            '*:fossils:($(__fossil_repos))'

          ;;
        (backup)
          _arguments                                                                                 \
            "(--help --overwrite)"--overwrite'[OK to overwrite an existing file]'                    \
            "(--help -R)"-R'[NAME Filename of the repository to backup]:fossils:($(__fossil_repos))' \
            '(- *)'--help'[Show help and exit]'                                                      \
            '1:file:_files'

          ;;
        (bisect)
          __fossil_bisect

          ;;
        (branch)
          __fossil_branch

          ;;
        (bundle)
          __fossil_bundle

          ;;
        (cache)
          __fossil_cache

          ;;
        (cat)
          _arguments                                                                                                               \
            "(--help -R --repository)"{-R,--repository}'[FILE Extract artifacts from repository FILE]:fossils:($(__fossil_repos))' \
            "(--help -r)"-r'[VERSION The specific check-in containing the file]:version:'                                          \
            '(- *)'--help'[Show help and exit]'                                                                                    \
            '*:files:_files'

          ;;
        (cgi)
          _arguments                            \
            '(- *)'--help'[Show help and exit]' \
            '::cgi:'                            \
            ':file:_files'

          ;;
        (changes|status)
          _arguments                                                                                        \
          "(--help --abs-paths)"--abs-paths'[Display absolute pathnames]'                                   \
          "(--help --rel-paths)"--rel-paths'[Display pathnames relative to the current directory]'          \
          "(--help --hash)"--hash'[Verify file status using hashing]'                                       \
          "(--help --case-sensitive)"--case-sensitive'[BOOL Override case-sensitive setting]:bool:(yes no)' \
          "(--help --dotfiles)"--dotfiles'[Include unmanaged files beginning with a dot]'                   \
          "(--help --ignore)"--ignore'[CSG Ignore unmanaged files matching CSG glob patterns]:pattern:'     \
          "(--help --header)"--header'[Identify the repository if report is non-empty.]'                    \
          "(--help -v --verbose)"{-v,--verbose}'[Say "(none)" if the change report is empty]'               \
          "(--help --classify)"--classify'[Start each line with the file'"'"'s change type]'                \
          "(--help --no-classify)"--no-classify'[Do not print file change types]'                           \
          "(--help --edited)"--edited'[Display edited, merged, and conflicted files]'                       \
          "(--help --updated)"--updated'[Display files updated by merge/integrate]'                         \
          "(--help --changed)"--changed'[Combination of --edited and --updated]'                            \
          "(--help --missing)"--missing'[Display missing files]'                                            \
          "(--help --added)"--added'[Display added files]'                                                  \
          "(--help --deleted)"--deleted'[Display deleted files]'                                            \
          "(--help --renamed)"--renamed'[Display renamed files]'                                            \
          "(--help --conflict)"--conflict'[Display files having merge conflicts]'                           \
          "(--help --meta)"--meta'[Display files with metadata changes]'                                    \
          "(--help --unchanged)"--unchanged'[Display unchanged files]'                                      \
          "(--help --all)"--all'[Display all managed files]'                                                \
          "(--help --extra)"--extra'[Display unmanaged files]'                                              \
          "(--help --differ)"--differ'[Display modified and extra files]'                                   \
          "(--help --merge)"--merge'[Display merge contributors]'                                           \
          "(--help --no-merge)"--no-merge'[Do not display merge contributors]'                              \
          '(- *)'--help'[Show help and exit]'                                                               \
          '*:files:_files'

          ;;
        (checkout|co)
          _arguments                                                                             \
          "(--help --force)"--force'[Ignore edited files in the current checkout]'               \
          "(--help --keep)"--keep'[Only update the manifest and manifest.uuid files]'            \
          "(--help --force-missing)"--force-missing'[Force checkout even if content is missing]' \
          "(--help --setmtime)"--setmtime'[Set timestamps of all files to match their SCM-side]' \
          '(- *)'--help'[Show help and exit]'                                                    \
          '*:files:_files'

          ;;
        (ci|commit)
          _arguments                                                                                                         \
          "(--help --allow-conflict)"--allow-conflict'[Allow unresolved merge conflicts]'                                    \
          "(--help --allow-empty)"--allow-empty'[Allow a commit with no changes]'                                            \
          "(--help --allow-fork)"--allow-fork'[Allow the commit to fork]'                                                    \
          "(--help --allow-older)"--allow-older'[Allow a commit older than its ancestor]'                                    \
          "(--help --baseline)"--baseline'[Use a baseline manifest in the commit process]'                                   \
          "(--help --bgcolor)"--bgcolor'[COLOR Apply COLOR to this one check-in only]:color:'                                \
          "(--help --branch)"--branch'[NEW-BRANCH-NAME check in to this new branch]:new branch:'                             \
          "(--help --branchcolor)"--branchcolor'[COLOR Apply given COLOR to the branch]:color:'                              \
          "(--help --close)"--close'[Close the branch being committed]'                                                      \
          "(--help --date-override)"--date-override'[DATETIME DATE to use instead of '"'"'now'"'"']:datetime:'               \
          "(--help --delta)"--delta'[Use a delta manifest in the commit process]'                                            \
          "(--help --hash)"--hash'[Verify file status using hashing]'                                                        \
          "(--help --integrate)"--integrate'[Close all merged-in branches]'                                                  \
          "(--help -m --comment)"{-m,--comment}'[COMMENT Use COMMENT as commit comment]:comment:'                            \
          "(--help -M --message-file)"{-M,--message-file}'[FILE Read the commit comment from given file]:file:_files'        \
          "(--help --mimetype)"--mimetype'[MIMETYPE Mimetype of check-in comment]:mimetype:'                                 \
          "(--help -n --dry-run)"{-n,--dry-run}'[If given, display instead of run actions]'                                  \
          "(--help --no-prompt)"--no-prompt'[Assume NO for every question]'                                                  \
          "(--help --no-warnings)"--no-warnings'[Omit all warnings about file contents]'                                     \
          "(--help --no-verify)"--no-verify'[Do not run before-commit hooks]'                                                \
          "(--help --nosign)"--nosign'[Do not attempt to sign this commit with gpg]'                                         \
          "(--help --override-lock)"--override-lock'[Allow a check-in even though parent is locked]'                         \
          "(--help --private)"--private'[Do not sync changes and their descendants]'                                         \
          "(--help --tag)"--tag'[TAG-NAME Assign given tag TAG-NAME to the check-in]:tag:($(__fossil_tags))'                 \
          "(--help --trace)"--trace'[Debug tracing]'                                                                         \
          "(--help --user-override)"--user-override'[USER Use USER instead of the current default]:user:($(__fossil_users))' \
          '(- *)'--help'[Show help and exit]'                                                                                \
          '*:files:_files'

          ;;
        (clean)
          _arguments                    \
            ${_fossil_clean_options[@]} \
            '*:files:_files'

          ;;
        (clone)
          _arguments                                                                                                         \
            "(--help --admin-user -A)"{--admin-user,-A}'[USERNAME Make USERNAME the administrator]:user:($(__fossil_users))' \
            "(--help --httpauth -B)"{--httpauth,-B}'[USER:PASS Add HTTP Basic Authorization to requests]:user pass:'         \
            "(--help --nocompress)"--nocompress'[Omit extra delta compression]'                                              \
            "(--help --once)"--once'[Don'"'"'t remember the URI]'                                                            \
            "(--help --private)"--private'[Also clone private branches]'                                                     \
            "(--help --save-http-password)"--save-http-password'[Remember the HTTP password without asking]'                 \
            "(--help --ssh-command -c)"{--ssh-command,-c}'[SSH Use SSH as the "ssh" command]:ssh command:'                   \
            "(--help --ssl-identity)"--ssl-identity'[FILENAME Use the SSL identity if requested by the server]:file:_files'  \
            "(--help -u --unversioned)"{-u,--unversioned}'[Also sync unversioned content]'                                   \
            "(--help -v --verbose)"{-v,--verbose}'[Show more statistics in output]'                                          \
            '(- *)'--help'[Show help and exit]'                                                                              \
            '1:uri:__fossil_urls'                                                                                            \
            '2:file:_files'

          ;;
        (close)
          _arguments                                                                      \
          "(--help --force -f)"{--force,-f}'[Close a check out with uncommitted changes]' \
          '(- *)'--help'[Show help and exit]'

          ;;
        (configuration)
          __fossil_configuration

          ;;
        (dbstat)
          _arguments                   \
          ${_fossil_dbstat_options[@]} \
            '(- *)'--help'[Show help and exit]'

          ;;
        (deconstruct)
          _arguments                                                                                                           \
            "(--help -R --repository)"{-R,--repository}'[REPOSITORY Deconstruct given REPOSITORY]:fossils:($(__fossil_repos))' \
            "(--help -K --keep-rid1)"{-K,--keep-rid1}'[Save the filename of the artifact with RID=1]'                          \
            "(--help -L --prefixlength)"{-L,--prefixlength}'[N Set the length of the names of the DESTINATION]:number:'        \
            "(--help --private)"--private'[Include private artifacts]'                                                         \
            "(--help -P --keep-private)"{-P,--keep-private}'[Save the list of private artifacts to .private]'                  \
            '(- *)'--help'[Show help and exit]'                                                                                \
            '1:destination:_files -/'

          ;;
        (delete|forget|rm)
          _arguments                                                                                              \
            "(--help --soft)"--soft'[Skip removing files from the checkout]'                                      \
            "(--help --hard)"--hard'[Remove files from the checkout]'                                             \
            "(--help --case-sensitive)"--case-sensitive'[BOOL Override the case-sensitive setting]:bool:(yes no)' \
            "(--help -n --dry-run)"{-n,--dry-run}'[If given, display instead of run actions]'                     \
            "(--help --reset)"--reset'[Reset the DELETED state of a checkout]'                                    \
            "(--help --verbose -v)"{--verbose,-v}'[Outputs information about each --reset file]'                  \
            '(- *)'--help'[Show help and exit]'                                                                   \
            '*:files:_files'

          ;;
        (descendants)
          _arguments                                                                                                          \
            "(--help -R --repository)"{-R,--repository}'[FILE Extract info from repository FILE]:fossils:($(__fossil_repos))' \
            "(--help -W --width)"{-W,--width}'[NUM Width of lines]:number:'                                                   \
            '(- *)'--help'[Show help and exit]'                                                                               \
            '1::check-in:'

          ;;
        (diff|gdiff)
          _arguments                            \
            ${_fossil_diff_options[@]}          \
            '(- *)'--help'[Show help and exit]' \
            '*:files:_files'

          ;;
        (export)
          _arguments \
          '(- *)'--help'[Show help and exit]'

          ;;
        (extras)
          _arguments                            \
            ${_fossil_extras_options[@]}        \
            '(- *)'--help'[Show help and exit]' \
            '*:files:_files -/'

          ;;
        (finfo)
          _arguments                                                                                                   \
          "(--help -b --brief)"{-b,--brief}'[Display a brief (one line / revision) summary]'                           \
          "(--help --case-sensitive)"--case-sensitive'[BOOL Enable or disable case-sensitive filenames]:bool:(yes no)' \
          "(--help -l --log)"{-l,--log}'[Select log mode (the default)]'                                               \
          "(--help -n --limit)"{-n,--limit}'[N Display only the first N changes]:number:'                              \
          "(--help --offset)"--offset'[P Skip P changes]:number:'                                                      \
          "(--help -p --print)"{-p,--print}'[Select print mode]'                                                       \
          "(--help -r --revision)"{-r,--revision}'[R Print the given revision]:version:'                               \
          "(--help -s --status)"{-s,--status}'[Select status mode (print a status indicator for FILE)]'                \
          "(--help -W --width)"{-W,--width}'[NUM Width of lines]:number:'                                              \
          '(- *)'--help'[Show help and exit]'                                                                          \
          '1:file:_files'

          ;;
        (fts-config)
          __fossil_fts_config

          ;;
        (git)
          __fossil_git

          ;;
        (grep)
          _arguments                                                                                   \
          "(--help -c --count)"{-c,--count}'[Suppress normal output; print count of files]'            \
          "(--help -i --ignore-case)"{-i,--ignore-case}'[Ignore case]'                                 \
          "(--help -l --files-with-matches)"{-l,--files-with-matches}'[List only hash for each match]' \
          "(--help --once)"--once'[Stop searching after the first match]'                              \
          "(--help -s --no-messages)"{-s,--no-messages}'[Suppress error messages]'                     \
          "(--help -v --invert-match)"{-v,--invert-match}'[Invert the sense of matching]'              \
          "(--help --verbose)"--verbose'[Show each file as it is analyzed]'                            \
          '(- *)'--help'[Show help and exit]'                                                          \
          '1:pattern:'                                                                                 \
          '*:files:_files'

          ;;
        (hash-policy)
          _arguments                            \
            '(- *)'--help'[Show help and exit]' \
            '1::policy:(sha1 auto sha3 sha3-only shun-sha1)'

          ;;
        (help)
          _arguments                                                                \
          "(- :)"{-a,--all}'[List both common and auxiliary commands]'              \
          "(- :)"{-o,--options}'[List command-line options common to all commands]' \
          "(- :)"{-s,--setting}'[List setting names]'                               \
          "(- :)"{-t,--test}'[List unsupported "test" commands]'                    \
          "(- :)"{-x,--aux}'[List only auxiliary commands]'                         \
          "(- :)"{-w,--www}'[List all web pages]'                                   \
          "(- :):all commands:($(__fossil_all_commands))"                           \
          "(- *)"--html'[Format output as HTML rather than plain text]'

          ;;
        (hook)
          __fossil_hook

          ;;
        (http)
          _arguments                                                                                                          \
          "(--help --baseurl)"--baseurl'[URL Base URL (useful with reverse proxies)]:url:__fossil_urls'                       \
          "(--help --extroot)"--extroot'[DIR Document root for the /ext extension mechanism]:file:_files -/'                  \
          "(--help --files)"--files'[GLOB Comma-separate glob patterns for static file to serve]:pattern:'                    \
          "(--help --host)"--host'[NAME Specify hostname of the server]:name:'                                                \
          "(--help --https)"--https'[Signal a request coming in via https]'                                                   \
          "(--help --in)"--in'[FILE Take input from FILE instead of standard input]:file:_files'                              \
          "(--help --ipaddr)"--ipaddr'[ADDR Assume the request comes from the given IP address]:address:'                     \
          "(--help --jsmode)"--jsmode'[MODE Determine how JavaScript is delivered with pages]:mode:(inline separate bundled)' \
          "(--help --localauth)"--localauth'[Enable automatic login for local connections]'                                   \
          "(--help --nocompress)"--nocompress'[Do not compress HTTP replies]'                                                 \
          "(--help --nodelay)"--nodelay'[Omit backoffice processing if it would delay process exit]'                          \
          "(--help --nojail)"--nojail'[Drop root privilege but do not enter the chroot jail]'                                 \
          "(--help --nossl)"--nossl'[Signal that no SSL connections are available]'                                           \
          "(--help --notfound)"--notfound'[URL Use URL as "HTTP 404, object not found" page]:url:__fossil_urls'               \
          "(--help --out)"--out'[FILE Write results to FILE instead of to standard output]:file:_files'                       \
          "(--help --repolist)"--repolist'[If REPOSITORY is directory, URL "/" lists all repos]'                              \
          "(--help --scgi)"--scgi'[Interpret input as SCGI rather than HTTP]'                                                 \
          "(--help --skin)"--skin'[LABEL Use override skin LABEL]:label:'                                                     \
          "(--help --th-trace)"--th-trace'[Trace TH1 execution (for debugging purposes)]'                                     \
          "(--help --usepidkey)"--usepidkey'[Use saved encryption key from parent process]'                                   \
          '(- *)'--help'[Show help and exit]'                                                                                 \
          '1::fossils:_files'

          ;;
        (import)
          _arguments                                                                                               \
          "(--help --git)"--git'[Import from the git-fast-export file format (default)]'                           \
          "(--help --import-marks)"--import-marks'[FILE Restore marks table from FILE]:file:_files'                \
          "(--help --export-marks)"--export-marks'[FILE Save marks table to FILE]:files:_files'                    \
          "(--help --rename-master)"--rename-master'[NAME Renames the master branch to NAME]:name:'                \
          "(--help --use-author)"--use-author'[Uses author as the committer]:user:($(__fossil_users))'             \
          "(--help --svn)"--svn'[Import from the svnadmin-dump file format]'                                       \
          "(--help --trunk)"--trunk'[FOLDER Name of trunk folder]:file:_files -/'                                  \
          "(--help --branches)"--branches'[FOLDER Name of branches folder]:file:_files -/'                         \
          "(--help --tags)"--tags'[FOLDER Name of tags folder]:file:_files -/'                                     \
          "(--help --base)"--base'[PATH Path to project root in repository]:file:_files'                           \
          "(--help --flat)"--flat'[The whole dump is a single branch]'                                             \
          "(--help --rev-tags)"--rev-tags'[Tag each revision, implied by -i]'                                      \
          "(--help --no-rev-tags)"--no-rev-tags'[Disables tagging effect of -i]'                                   \
          "(--help --rename-rev)"--rename-rev'[PAT Rev tag names, default "svn-rev-%"]:pattern:'                   \
          "(--help --ignore-tree)"--ignore-tree'[DIR Ignores subtree rooted at DIR]:file:_files -/'                \
          "(--help -i --incremental)"{-i,--incremental}'[Allow importing into an existing repository]'             \
          "(--help -f --force)"{-f,--force}'[Overwrite repository if already exists]'                              \
          "(--help -q --quiet)"{-q,--quiet}'[Omit progress output]'                                                \
          "(--help --no-rebuild)"--no-rebuild'[Skip the "rebuilding metadata" step]'                               \
          "(--help --no-vacuum)"--no-vacuum'[Skip the final VACUUM of the database file]'                          \
          "(--help --rename-trunk)"--rename-trunk'[NAME use NAME as name of imported trunk branch]:name:'          \
          "(--help --rename-branch)"--rename-branch'[PAT Rename all branch names using PAT pattern]:pattern:'      \
          "(--help --rename-tag)"--rename-tag'[PAT Rename all tag names using PAT pattern]:pattern:'               \
          "(--help --admin-user -A)"{--admin-user,-A}'[NAME Use NAME for the admin user]:user:($(__fossil_users))' \
          '(- *)'--help'[Show help and exit]'                                                                      \
          '*:files:_files'

          ;;
        (info)
          _arguments                                                                                                          \
            "(--help -R --repository)"{-R,--repository}'[FILE Extract info from repository FILE]:fossils:($(__fossil_repos))' \
            "(--help -v --verbose)"{-v,--verbose}'[Show extra information about repositories]'                                \
            '(- *)'--help'[Show help and exit]'                                                                               \
            '1:fossils:($(__fossil_repos))'

          ;;
        (init|new)
          _arguments                                                                                                           \
          "(--help --template)"--template'[FILE Copy settings from repository file]:file:_files'                               \
          "(--help --admin-user -A)"{--admin-user,-A}'[USERNAME Select given USERNAME as admin user]:user:($(__fossil_users))' \
          "(--help --date-override)"--date-override'[DATETIME Use DATETIME as time of the initial check-in]:datetime:(now)'    \
          "(--help --sha1)"--sha1'[Use an initial hash policy of "sha1"]'                                                      \
          '(- *)'--help'[Show help and exit]'                                                                                  \
          '1:file:_files'

          ;;
        (json)
          __fossil_json

          ;;
        (leaves)
          _arguments                                                                                           \
            "(--help -a --all)"{-a,--all}'[Show ALL leaves]'                                                   \
            "(--help --bybranch)"--bybranch'[Order output by branch name]'                                     \
            "(--help -c --closed)"{-c,--closed}'[Show only closed leaves]'                                     \
            "(--help -m --multiple)"{-m,--multiple}'[Show only cases with multiple leaves on a single branch]' \
            "(--help --recompute)"--recompute'[Recompute the "leaf" table in the repository DB]'               \
            "(--help -W --width)"{-W,--width}'[NUM Width of lines (default is to auto-detect)]:number:'        \
            '(- *)'--help'[Show help and exit]'

          ;;
        (login-group)
          __fossil_login_group

          ;;
        (ls)
          _arguments                                                                                                          \
            "(--help --age)"--age'[Show when each file was committed]'                                                        \
            "(--help -v --verbose)"{-v,--verbose}'[Provide extra information about each file]'                                \
            "(--help -t)"-t'[Sort output in time order]'                                                                      \
            "(--help -r)"-r'[VERSION The specific check-in to list]:version:'                                                 \
            "(--help -R --repository)"{-R,--repository}'[FILE Extract info from repository FILE]:fossils:($(__fossil_repos))' \
            '(- *)'--help'[Show help and exit]'                                                                               \
            '*:files:_files'

          ;;
        (md5sum)
          _arguments                            \
            '(- *)'--help'[Show help and exit]' \
            '*:files:_files'

          ;;
        (merge)
          _arguments                                                                                                 \
            "(--help --backout)"--backout'[Do a reverse cherrypick merge against VERSION]'                           \
            "(--help --baseline)"--baseline'[BASELINE Use BASELINE as the "pivot" of the merge instead]:version:'    \
            "(--help --binary)"--binary'[GLOBPATTERN Treat files that match GLOBPATTERN as binary]:pattern:'         \
            "(--help --case-sensitive)"--case-sensitive'[BOOL Override the case-sensitive setting]:bool:(yes no)'    \
            "(--help --cherrypick)"--cherrypick'[Do a cherrypick merge VERSION into the current checkout]'           \
            "(--help -f --force)"{-f,--force}'[Force the merge even if it would be a no-op]'                         \
            "(--help --force-missing)"--force-missing'[Force the merge even if there is missing content]'            \
            "(--help --integrate)"--integrate'[Merged branch will be closed when committing]'                        \
            "(--help -K --keep-merge-files)"{-K,--keep-merge-files}'[On merge conflict, retain the temporary files]' \
            "(--help -n --dry-run)"{-n,--dry-run}'[If given, display instead of run actions]'                        \
            "(--help -v --verbose)"{-v,--verbose}'[Show additional details of the merge]'                            \
            '(- *)'--help'[Show help and exit]'                                                                      \
            '1:version:'

          ;;
        (mv|rename)
          _arguments                                                                                              \
            "(--help --soft)"--soft'[Skip moving files within the checkout]'                                      \
            "(--help --hard)"--hard'[Move files within the checkout]'                                             \
            "(--help --case-sensitive)"--case-sensitive'[BOOL Override the case-sensitive setting]:bool:(yes no)' \
            "(--help -n --dry-run)"{-n,--dry-run}'[If given, display instead of run actions]'                     \
            '(- *)'--help'[Show help and exit]'                                                                   \
            '*:files:_files'

          ;;
        (open)
          _arguments                                                                                     \
            "(--help --empty)"--empty'[Initialize checkout as being empty, but still connected]'         \
            "(--help --force)"--force'[Continue even if the directory is not empty]'                     \
            "(--help --force-missing)"--force-missing'[Force opening a repository with missing content]' \
            "(--help --keep)"--keep'[Only modify the manifest and manifest.uuid files]'                  \
            "(--help --nested)"--nested'[Allow opening a repository inside an opened checkout]'          \
            "(--help --repodir)"--repodir'[DIR Store the clone in DIR]:directory:_files -/'              \
            "(--help --setmtime)"--setmtime'[Set timestamps of all files to match their SCM-side times]' \
            "(--help --workdir)"--workdir'[DIR Use DIR as the working directory]:directory:_files -/'    \
            '(- *)'--help'[Show help and exit]'                                                          \
            '1:fossils:($(__fossil_repos))'                                                              \
            '::version:'

          ;;
        (pop3d)
          _arguments                                                                           \
            "(--help --logdir)"--logdir'[DIR Create log files inside DIR]:directory:_files -/' \
            '(- *)'--help'[Show help and exit]'                                                \
            '1:fossils:($(__fossil_repos))'

          ;;
        (publish)
          _arguments                                                                              \
            '(--help --only)'--only'[Publish only the specific artifacts identified by the tags]' \
            '(- *)'--help'[Show help and exit]'                                                   \
            '*:tags:($(__fossil_tags))'

          ;;
        (pull)
          _arguments                                                                                                      \
            "(--help -B --httpauth)"{-B,--httpauth}'[USER:PASS Credentials for the simple HTTP auth protocol]:user pass:' \
            "(--help --from-parent-project)"--from-parent-project'[Pull content from the parent project]'                 \
            "(--help --ipv4)"--ipv4'[Use only IPv4, not IPv6]'                                                            \
            "(--help --once)"--once'[Do not remember URL for subsequent syncs]'                                           \
            "(--help --private)"--private'[Pull private branches too]'                                                    \
            "(--help --project-code)"--project-code'[CODE Use CODE as the project code]:project code:'                    \
            "(--help --proxy)"--proxy'[PROXY Use the specified HTTP proxy]:proxy:'                                        \
            "(--help -R --repository)"{-R,--repository}'[REPO Local repository to pull into]:fossils:($(__fossil_repos))' \
            "(--help --ssl-identity)"--ssl-identity'[FILE Local SSL credentials]:ssl credentials:'                        \
            "(--help --ssh-command)"--ssh-command'[SSH Use SSH as the "ssh" command]:ssh command:'                        \
            "(--help -v --verbose)"{-v,--verbose}'[Additional (debugging) output]'                                        \
            "(--help --verily)"--verily'[Exchange extra information with the remote]'                                     \
            '(- *)'--help'[Show help and exit]'                                                                           \
            '1:url:__fossil_urls'

          ;;
        (purge)
          __fossil_purge

          ;;
        (push)
          _arguments                                                                                                      \
            "(--help -B --httpauth)"{-B,--httpauth}'[USER:PASS Credentials for the simple HTTP auth protocol]:user pass:' \
            "(--help --ipv4)"--ipv4'[Use only IPv4, not IPv6]'                                                            \
            "(--help --once)"--once'[Do not remember URL for subsequent syncs]'                                           \
            "(--help --proxy)"--proxy'[PROXY Use the specified HTTP proxy]'                                               \
            "(--help --private)"--private'[Push private branches too]'                                                    \
            "(--help -R --repository)"{-R,--repository}'[REPO Local repository to push from]:fossils:($(__fossil_repos))' \
            "(--help --ssl-identity)"--ssl-identity'[FILE Local SSL credentials]:ssl credentials:'                        \
            "(--help --ssh-command)"--ssh-command'[SSH Use SSH as the "ssh" command]:ssh command:'                        \
            "(--help -v --verbose)"{-v,--verbose}'[Additional (debugging) output]'                                        \
            "(--help --verily)"--verily'[Exchange extra information with the remote]'                                     \
            '(- *)'--help'[Show help and exit]'                                                                           \
            '1::url:__fossil_urls'

          ;;
        (rebuild)
          _arguments                                                                           \
            ${_fossil_rebuild_options[@]}                                                      \
            "(--help --force)"--force'[Force the rebuild to complete even if errors are seen]' \
            "(--help --randomize)"--randomize'[Scan artifacts in a random order]'              \
            '(- *)'--help'[Show help and exit]'                                                \
            '1:fossils:($(__fossil_repos))'

          ;;
        (reconstruct)
          _arguments                                                                                            \
            "(--help -K --keep-rid1)"{-K,--keep-rid1}'[Read the filename of the artifact with RID=1 from .rid]' \
            "(--help -P --keep-private)"{-P,--keep-private}'[Mark the artifacts listed in .private as private]' \
            '(- *)'--help'[Show help and exit]'                                                                 \
            '1:file:_files'                                                                                     \
            '2:directory:_files -/'

          ;;
        (redo|undo)
          _arguments                                                                                 \
            "(--help -n --dry-run)"{-n,--dry-run}'[Do not make changes but show what would be done]' \
            '(- *)'--help'[Show help and exit]'                                                      \
            '*:files:_files'

          ;;
        (remote|remote-url)
          __fossil_remote

          ;;
        (reparent)
          _arguments                                                                                                             \
            "(--help --test)"--test'[Make database entries but do not add the tag artifact]'                                     \
            "(--help -n --dryrun)"{-n,--dryrun}'[Do not actually change the database]'                                           \
            "(--help --date-override)"--date-override'[DATETIME Set the change time on the control artifact]:datetime:'          \
            "(--help --user-override)"--user-override'[USER Set the user name on the control artifact]:user:($(__fossil_users))' \
            '(- *)'--help'[Show help and exit]'                                                                                  \
            '1:check-in:'                                                                                                        \
            '*:parents:'

          ;;
        (revert)
          _arguments                                                                            \
            "(--help -r --revision)"{-r,--revision}'[VERSION Revert to given VERSION]:version:' \
            '(- *)'--help'[Show help and exit]'                                                 \
            '*:files:_files'

          ;;
        (rss)
          _arguments                                                                                             \
            "(--help -type -y)"{-type,-y}'[FLAG]:type:(all ci t w)'                                              \
            "(--help -limit -n)"{-limit,-n}'[LIMIT The maximum number of items to show]:number:'                 \
            "(--help -tkt)"-tkt'[HASH Filters for only those events for the specified ticket]:hash:'             \
            "(--help -tag)"-tag'[TAG Filters for a tag]:tag:($(__fossil_tags))'                                  \
            "(--help -wiki)"-wiki'[NAME Filters on a specific wiki page]:wiki page:($(__fossil_wiki_pages))'     \
            "(--help -name)"-name'[FILENAME filters for a specific file]:file:_files'                            \
            "(--help -url)"-url'[STRING Sets the RSS feed'"'"'s root URL to the given string]:url:__fossil_urls' \
            '(- *)'--help'[Show help and exit]'

          ;;
        (scrub)
          _arguments                                                                               \
            "(--help --force)"--force'[Do not prompt for confirmation]'                            \
            "(--help --private)"--private'[Only private branches are removed from the repository]' \
            "(--help --verily)"--verily'[Scrub real thoroughly (see above)]'                       \
            '(- *)'--help'[Show help and exit]'                                                    \
            '1::fossils:($(__fossil_repos))'

          ;;
        (search)
          _arguments                                                                              \
            "(--help -a --all)"{-a,--all}'[Output all matches, not just best matches]'            \
            "(--help -n --limit)"{-n,--limit}'[N Limit output to N matches]:number:'              \
            "(--help -W --width)"{-W,--width}'[WIDTH Set display width to WIDTH columns]:number:' \
            '(- *)'--help'[Show help and exit]'                                                   \
            '*:patterns:'

          ;;
        (server|ui)
          _arguments                     \
            ${_fossil_server_options[@]} \
            '1::fossils:($(__fossil_repos))'

          ;;
        (set|settings)
          _arguments                                                         \
            "(--global)"--global'[Set or unset the given property globally]' \
            "(--exact)"--exact'[Only consider exact name matches]'           \
            "1:setting:($(__fossil_settings))"                               \
            '2:value:'

          ;;
        (sha1sum)
          _arguments                                                                \
            "(--help -h --dereference)"{-h,--dereference}'[Resolve symbolic links]' \
            '(- *)'--help'[Show help and exit]'                                     \
            '*:files:_files'

          ;;
        (sha3sum)
          _arguments                                                                                \
            "(--help --224 --256 --384 --512 --size)"--224'[Compute a SHA3-224 hash]'               \
            "(--help --256 --224 --384 --512 --size)"--256'[Compute a SHA3-256 hash (the default)]' \
            "(--help --384 --224 --256 --512 --size)"--384'[Compute a SHA3-384 hash]'               \
            "(--help --512 --224 --256 --384 --size)"--512'[Compute a SHA3-512 hash]'               \
            "(--help --size --224 --256 --384 --512 --size)"--size'[N An N-bit hash]:number:'       \
            "(--help -h --dereference)"{-h,--dereference}'[Resolve symbolic links]'                 \
            '(- *)'--help'[Show help and exit]'                                                     \
            '*:files:_files'

          ;;
        (shell)
          _arguments \
          '(- *)'--help'[Show help and exit]'

          ;;
        (smtpd)
          _arguments                                                                            \
            "(--help --dryrun)"--dryrun'[Do not record any emails in the database]'             \
            "(--help --trace)"--trace'[Print a transcript of the conversation on stderr]'       \
            "(--help --ipaddr)"--ipaddr'[ADDR The SMTP connection originates at ADDR]:address:' \
            '(- *)'--help'[Show help and exit]'                                                 \
            '1:fossils:($(__fossil_repos))'

          ;;
        (sql|sqlite3)
          _arguments                                                                                            \
            "(--help --no-repository)"--no-repository'[Skip opening the repository database]'                   \
            "(--help --readonly)"--readonly'[Open the repository read-only]'                                    \
            "(--help -R)"-R'[REPOSITORY Use REPOSITORY as the repository database]:fossils:($(__fossil_repos))' \
            '(- *)'--help'[Show help and exit]'

          ;;
        (sqlar)
          _arguments                                                                                                     \
            "(--help -X --exclude)"{-X,--exclude}'[GLOBLIST Comma-separated list of GLOBs of files to exclude]:pattern:' \
            "(--help --include)"--include'[GLOBLIST Comma-separated list of GLOBs of files to include]:pattern:'         \
            "(--help --name)"--name'[DIR The name of the top-level directory in the archive]:directory:_files -/'        \
            "(--help -R)"-R'[REPOSITORY Specify a Fossil repository]:fossils:($(__fossil_repos))'                        \
            '(- *)'--help'[Show help and exit]'                                                                          \
            '1:version:'                                                                                                 \
            '2:output file:_files'

          ;;
        (stash)
          __fossil_stash

          ;;
        (sync)
          _arguments                                                                                                      \
            "(--help -B --httpauth)"{-B,--httpauth}'[USER:PASS Credentials for the simple HTTP auth protocol]:user pass:' \
            "(--help --ipv3)"--ipv4'[Use only IPv4, not IPv6]'                                                            \
            "(--help --once)"--once'[Do not remember URL for subsequent syncs]'                                           \
            "(--help --proxy)"--proxy'[PROXY Use the specified HTTP proxy]:proxy:'                                        \
            "(--help --private)"--private'[Sync private branches too]'                                                    \
            "(--help -R --repository)"{-R,--repository}'[REPO Local repository to sync with]:fossils:($(__fossil_repos))' \
            "(--help --ssl-identity)"--ssl-identity'[FILE Local SSL credentials]:ssl credentials:'                        \
            "(--help --ssh-command)"--ssh-command'[SSH Use SSH as the "ssh" command]:ssh command:'                        \
            "(--help -u --unversioned)"{-u,--unversioned}'[Also sync unversioned content]'                                \
            "(--help -v --verbose)"{-v,--verbose}'[Additional (debugging) output]'                                        \
            "(--help --verily)"--verily'[Exchange extra information with the remote]'                                     \
            '(- *)'--help'[Show help and exit]'                                                                           \
            '1::url:__fossil_urls'

          ;;
       (tag)
          __fossil_tag

          ;;
       (tarball)
          _arguments                                                                                                      \
             "(--help -X --exclude)"{-X,--exclude}'[GLOBLIST Comma-separated list of GLOBs of files to exclude]:pattern:' \
             "(--help --include)"--include'[GLOBLIST Comma-separated list of GLOBs of files to include]:pattern:'         \
             "(--help --name)"--name'[DIR The name of the top-level directory in the archive]:directory:_files -/'        \
             "(--help -R)"-R'[REPOSITORY Specify a Fossil repository]:fossils:($(__fossil_repos))'                        \
             '(- *)'--help'[Show help and exit]'                                                                          \
             '1:version:'                                                                                                 \
             '2:ouput file:_files'

          ;;
        (ticket)
          __fossil_ticket

          ;;
       (timeline)
          _arguments                                                                                        \
             "(--help -n --limit)"{-n,--limit}'[N Output the first N entries]:number:'                      \
             "(--help -p --path)"{-p,--path}'[PATH Output items affecting PATH only]:path:_files'           \
             "(--help --offset)"--offset'[P skip P changes]:number:'                                        \
             "(--help --sql)"--sql'[Show the SQL used to generate the timeline]'                            \
             "(--help -t --type)"{-t,--type}'[TYPE Output items from the given types only]:type:(ci e t w)' \
             "(--help -v --verbose)"{-v,--verbose}'[Output the list of files changed by each commit]'       \
             "(--help -W --width)"{-W,--width}'[NUM Width of lines (default is to auto-detect)]:number:'    \
             "(--help -R)"-R'[REPO Specifies the repository db to use]:fossils:($(__fossil_repos))'         \
             '(- *)'--help'[Show help and exit]'                                                            \
             '::when:(before after descendants children ancestors parents)'                                 \
             '::check-in:'                                                                                  \
             '::datetime:'

          ;;
        (tls-config)
          __fossil_tls_config

          ;;
        (touch)
          _arguments                                                                                                  \
            "(--help --now -c --checkin -C --checkout)"--now'[Stamp each affected file with the current time]'        \
            "(--help -c --checkin --now -C --checkout)"{-c,--checkin}'[Stamp with the last modification time]'        \
            "(--help -C --checkout -c --checkin --now)"{-C,--checkout}'[Stamp with the time of the current checkout]' \
            "(--help -g)"-g'[GLOBLIST Comma-separated list of glob patterns]:pattern:'                                \
            "(--help -G)"-G'[GLOBFILE Like -g but reads globs from glob default file]:pattern:'                       \
            "(--help -v -verbose)"{-v,-verbose}'[Outputs extra information about its globs]'                          \
            "(--help -n --dry-run)"{-n,--dry-run}'[Do not touch anything]'                                            \
            "(--help -q --quiet)"{-q,--quiet}'[Suppress warnings]'                                                    \
            '(- *)'--help'[Show help and exit]'                                                                       \
            '*:files:_files'

          ;;
        (unpublished)
          _arguments                                                      \
          "(--help --all)"--all'[Show all artifacts, not just check-ins]' \
          '(- *)'--help'[Show help and exit]'

          ;;
        (unset)
          _arguments                                                         \
            "(--global)"--global'[Set or unset the given property globally]' \
            "(--exact)"--exact'[Only consider exact name matches]'           \
            "1:setting:($(__fossil_settings))"

          ;;
        (unversioned|uv)
          __fossil_unversioned

          ;;
        (update)
          _arguments                                                                                                 \
            "(--help --case-sensitive)"--case-sensitive'[BOOL Override case-sensitive setting]:bool:(yes no)'        \
            "(--help --debug)"--debug'[Print debug information on stdout]'                                           \
            "(--help --latest)"--latest'[Update to latest version]'                                                  \
            "(--help --force-missing)"--force-missing'[Force update if missing content after sync]'                  \
            "(--help -n --dry-run)"{-n,--dry-run}'[If given, display instead of run actions]'                        \
            "(--help -v --verbose)"{-v,--verbose}'[Print status information about all files]'                        \
            "(--help -W --width)"{-W,--width}'[WIDTH Width of lines (default is to auto-detect)]:number:'            \
            "(--help --setmtime)"--setmtime'[Set timestamps of all files to match their SCM-side times]'             \
            "(--help -K --keep-merge-files)"{-K,--keep-merge-files}'[On merge conflict, retain the temporary files]' \
            '(- *)'--help'[Show help and exit]'                                                                      \
            '1::version:'                                                                                            \
            '*::files:_files'

          ;;
        (user)
          __fossil_user

          ;;
        (version)
          _arguments                                                                            \
            "(--help -v --verbose)"{-v,--verbose}'[Additional details about optional features]' \
            '(- *)'--help'[Show help and exit]'

          ;;
        (whatis)
          _arguments                                                                             \
            "(--help --type)"--type'[TYPE Only find artifacts of TYPE]:type:(ci t w g e)'        \
            "(--help -v --verbose)"{-v,--verbose}'[Provide extra information (such as the RID)]' \
            '(- *)'--help'[Show help and exit]'                                                  \
            '1:name:'

          ;;
        (wiki)
          __fossil_wiki

          ;;
        (zip)
          _arguments                                                                                                   \
          "(--help -X --exclude)"{-X,--exclude}'[GLOBLIST Comma-separated list of GLOBs of files to exclude]:pattern:' \
          "(--help --include)"--include'[GLOBLIST Comma-separated list of GLOBs of files to include]:pattern:'         \
          "(--help --name)"--name'[DIR The name of the top-level directory in the archive]:directory:_files -/'        \
          "(--help -R)"-R'[REPOSITORY Specify a Fossil repository]:fossils:($(__fossil_repos))'                        \
          '(- *)'--help'[Show help and exit]'                                                                          \
          '1:version:'                                                                                                 \
          '2:output file:_files'

          ;;
      esac
      ;;
    *)
  esac
  return 0
}

################################################################################
# Subcommands                                                                  #
################################################################################

########################################
# fossil alerts                        #
########################################
function __fossil_alerts_settings() {
  fossil alerts settings 2>/dev/null
}

function __fossil_alerts_subscribers() {
  fossil alerts subscribers 2>/dev/null
}

function __fossil_alerts() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(--help -R --repository)"{-R,--repository}'[FILE Run command on repository FILE]:fossils:($(__fossil_repos))'
    "(-)"--help'[Show help and exit]'
  )

  _arguments -C                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _values 'subcommand'                             \
        pending reset send settings status subscribers \
        test-message unsubscribe

      ;;
    (options)
      case $line[1] in
        (pending|reset)
          _arguments \
           ${_common_options[@]}

          ;;
        (send)
          _arguments                                    \
            ${_common_options[@]}                       \
            "(--help --digest)"--digest'[Send digests]' \
            "(--help --test)"--test'[Write to standard output]'

          ;;
        (set|settings)
          _arguments                                \
           ${_common_options[@]}                    \
            '1::name:($(__fossil_alerts_settings))' \
            '2::value:'

          ;;
        (subscribers)
          _arguments             \
           ${_common_options[@]} \
           '1::pattern:'

          ;;
        (test-message)
          _arguments                                                  \
           ${_common_options[@]}                                      \
            "(--help --body)"--body'[FILENAME]:file:_files'           \
            "(--help --smtp-trace)"--smtp-trace'[]'                   \
            "(--help --stdout)"--stdout'[]'                           \
            "(--help --subject -S)"{--subject,-S}'[SUBJECT]:subject:' \
            '1:recipient:'

          ;;
        unsubscribe)
          _arguments             \
           ${_common_options[@]} \
            '1:email:($(__fossil_alerts_subscribers))'

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}

########################################
# fossil all                           #
########################################
function __fossil_all() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(-)"--help'[Show help and exit]'
    "(--help --showfile)"--showfile'[Show the repository or checkout being operated upon]'
    "(--help --dontstop)"--dontstop'[Continue with other repositories even after an error]'
    "(--help --dry-run)"--dry-run'[If given, display instead of run actions]'
  )

  _arguments -C                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _values 'subcommand'                                              \
        backup cache changes clean config dbstat extras fts-config info \
        pull push rebuild sync set unset server ui add ignore list ls

      ;;
    (options)
      case $line[1] in
        (backup)
          _arguments              \
            ${_common_options[@]} \
            '1:output directory:_files -/'

          ;;
        (cache)
          __fossil_cache

          ;;
        (clean)
          _arguments                    \
            ${_common_options[@]}       \
            ${_fossil_clean_options[@]} \
            "(--help --whatif)"--whatif'[Review the files to be deleted]'

          ;;
        (config)
          _arguments              \
            ${_common_options[@]} \
            '1:what:(pull)'       \
            '2:area:'

          ;;
        (dbstat)
          _arguments \
            ${_fossil_dbstat_options[@]}

          ;;
        (extras)
          _arguments \
            ${_fossil_extras_options[@]}

          ;;
        (fts-config)
          __fossil_fts_config

          ;;
        (pull|push)
          _arguments              \
            ${_common_options[@]} \
            "(--help -v --verbose)"{-v,--verbose}'[Additional (debugging) output]'

          ;;
        (rebuild)
          _arguments              \
            ${_common_options[@]} \
            ${_fossil_rebuild_options[@]}

          ;;
        (sync)
          _arguments                                                                       \
            ${_common_options[@]}                                                          \
            "(--help -u --unversioned)"{-u,--unversioned}'[Also sync unversioned content]' \
            "(--help -v --verbose)"{-v,--verbose}'[Additional (debugging) output]'

          ;;
        (set|unset)
          _arguments                                                                \
            ${_common_options[@]}                                                   \
            "1:setting:($(__fossil_settings))"                                      \
            '2:value:'                                                              \
            "(--help --global)"--global'[Set or unset the given property globally]' \
            "(--help --exact)"--exact'[Only consider exact name matches]'

          ;;
        (server|ui)
          _arguments              \
            ${_common_options[@]} \
            ${_fossil_server_options[@]}

          ;;
        (add)
          _arguments              \
            ${_common_options[@]} \
            '*:fossils:($(__fossil_repos))'

          ;;
        (ignore)
          _arguments                                                                            \
            ${_common_options[@]}                                                               \
            "(--help -c --ckout)"{-c,--ckout}'[Ignore local checkouts instead of repositories]' \
            '*:fossils:($(__fossil_repos))'

          ;;
        (list|ls)
          _arguments                          \
            "(-)"--help'[Show help and exit]' \
            "(--help -c --ckout)"{-c,--ckout}'[List local checkouts instead of repositories]'

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}


########################################
# fossil bisect                        #
########################################
function __fossil_bisect() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(-)"--help'[Show help and exit]'
  )

  _arguments -C                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _values 'subcommand' \
        bad good log chart next options reset skip vlist ls status ui undo

      ;;
    (options)
      case $line[1] in
        (bad|good|skip)
          _arguments \
            '1:version:'

          ;;
        (options)
          _arguments \
            "1::name:($(fossil bisect options 2>/dev/null | awk '{print $1}'))" \
            '2::value:'

          ;;
        (vlist|ls|status)
          _arguments \
            "(-a --all)"{-a,--all}'[List all]'

          ;;
        (ui)
          _arguments \
            ${_fossil_server_options[@]}

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}

########################################
# fossil branch                        #
########################################
function __fossil_branch() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(-)"--help'[Show help and exit]'
    "(--help -R --repository)"{-R,--repository}'[FILE Run commands on repository FILE]:fossils:($(__fossil_repos))'
  )

  _arguments -C                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _values 'subcommand' \
        current info list ls new

      ;;
    (options)
      case $line[1] in
        (current)
          _arguments \
            ${_common_options[@]}

          ;;
        (info)
          _arguments              \
            ${_common_options[@]} \
          ':branch:($(__fossil_branches))'

          ;;
        (list|ls)
          _arguments                                                    \
            ${_common_options[@]}                                       \
            "(--help -a --all)"{-a,--all}'[List all branches]'          \
            "(--help -c --closed)"{-c,--closed}'[List closed branches]' \
            "(--help -r)"-r'[Reverse the sort order]'                   \
            "(--help -t)"-t'[Show recently changed branches first]'

          ;;
        (new)
          _arguments                                                                                                           \
            ${_common_options[@]}                                                                                              \
            "(--help --private)"--private'[Branch is private]'                                                                 \
            "(--help --bgcolor)"--bgcolor'[COLOR Use COLOR instead of automatic background]:color:'                            \
            "(--help --nosign)"--nosign'[Do not sign contents on this branch]'                                                 \
            "(--help --date-override)"--date-override'[DATE Use DATE instead of '"'"'now'"'"']:date:(now)'                     \
            "(--help --user-override)"--user-override'[USER Use USER instead of the default]:user:($(__fossil_users))'         \
            '1:branch name:'                                                                                                   \
            '2:base check-in:'

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}

########################################
# fossil bundle                        #
########################################
function __fossil_bundle() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(-)"--help'[Show help and exit]'
  )

  _arguments -C                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _values 'subcommand' \
        append cat export extend import ls purge

      ;;
    (options)
      case $line[1] in
        (append)
          _arguments          \
            '1:bundle:_files' \
            '*:files:_files'

          ;;
        (bundle)
          _arguments          \
            '1:what:(cat)'    \
            '2:bundle:_files' \
            '*:hashes:'

          ;;
        (export)
          _arguments                                                                                       \
            "(--branch)"--branch'[BRANCH Package all check-ins on BRANCH]:branch:($(__fossil_branches))'   \
            "(--from)"--from'[TAG1 --to TAG2 Package check-ins starting with TAG1]:tag:($(__fossil_tags))' \
            "(--to)"--to'[TAG2 Package check-ins up to TAG2]:tag:($(__fossil_tags))'                       \
            "(--checkin)"--checkin'[TAG Package the single check-in TAG]:tag:($(__fossil_tags))'           \
            "(--standalone)"--standalone'[Do no use delta-encoding against]'                               \
            '1:bundle:_files'

          ;;
        (extend|ls|purge)
          _arguments \
            '1:bundle:_files'

          ;;
        (import)
          _arguments                                                        \
            "(--publish)"--publish'[Make the import public]'                \
            "(--force)"--force'[Import even if project codes do not match]' \
            '1:bundle:_files'

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}

########################################
# fossil cache                         #
########################################
function __fossil_cache_subcommand {
}

function __fossil_cache() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(-)"--help'[Show help and exit]'
  )

  _arguments -C                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _values 'subcommand' \
        clear init list ls status

      ;;
    *)
      _message 'no more arguments'

      ;;
  esac
  return 0
}

########################################
# fossil configuration                 #
########################################
function __fossil_configuration() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(--help -R --repository)"{-R,--repository}'[FILE Extract info from repository FILE]:fossils:($(__fossil_repos))'
    "(-)"--help'[Show help and exit]'
  )

  _arguments -C           \
    ${_common_options[@]} \
    '1:method:->method'   \
    '*::options:->options'

  case $state in
    (method)
      _values 'method' \
        export import merge pull push reset sync

      ;;
    (options)
      case $line[1] in
        (export)
          _arguments                \
            ${_common_options[@]}   \
            '1:area:__fossil_areas' \
            '2:file:_files'

          ;;
        (import|merge)
          _arguments              \
            ${_common_options[@]} \
            '1:file:_files'

          ;;
        (pull)
          _arguments                                                    \
            ${_common_options[@]}                                       \
            "(--help --overwrite)"--overwrite'[Replace local settings]' \
            '1:area:__fossil_areas'                                     \
            '2::url:__fossil_urls'

          ;;
        (push|sync)
          _arguments                \
            ${_common_options[@]}   \
            '1:area:__fossil_areas' \
            '2::url:__fossil_urls'

          ;;
        (reset)
          _arguments              \
            ${_common_options[@]} \
            '1:area:__fossil_areas'

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}

########################################
# fossil fts-config                    #
########################################
function __fossil_fts_config() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(-)"--help'[Show help and exit]'
  )

  _arguments -C                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _values 'subcommand' \
        reindex index enable disable stemmer

      ;;
    (options)
      case $line[1] in
        index|stemmer)
          _arguments \
            "(- *):setting:(on off)"

          ;;
        enable|disable)
          _arguments \
            "(- *):setting:(cdtwe)"

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}

########################################
# fossil git                           #
########################################
function __fossil_git() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(-)"--help'[Show help and exit]'
  )

  _arguments -C                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    subcommand)
      _values 'subcommand' \
        export import status

      ;;
    options)
      case $line[1] in
        (export)
          _arguments                                                                                              \
            ${_common_options[@]}                                                                                 \
            "(--help --autopush)"--autopush'[URL Automatically do a '"'"'git push'"'"' to URL]:url:__fossil_urls' \
            "(--help --debug)"--debug'[FILE Write fast-export text to FILE]:file:_files'                          \
            "(--help --force -f)"{--force,-f}'[Do the export even if nothing has changed]'                        \
            "(--help --limit)"--limit'[N Add no more than N new check-ins to MIRROR]:number:'                     \
            "()*"{--quiet,-q}'[Reduce output. Repeat for even less output]'                                       \
            "(--verbose -v)"{--verbose,-v}'[More output]'

          ;;
        (import)
          _arguments              \
            ${_common_options[@]} \
            ':url:__fossil_urls'

          ;;
        (status)
          _arguments              \
            ${_common_options[@]}

            ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}

########################################
# fossil hook                          #
########################################
function __fossil_hook() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(-)"--help'[Show help and exit]'
  )

  _arguments -C                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _values 'subcommand' \
        add delete edit list status test

      ;;
    (options)
      case $line[1] in
        (add)
          _arguments                                          \
            ${_common_options[@]}                             \
            "(--help --command)"--command'[COMMAND]:command:' \
            "(--help --type)"--type'[TYPE]:type:'             \
            "(--help --sequence)"--sequence'[NUM]:number:'

          ;;
        (delete)
          _arguments              \
            ${_common_options[@]} \
            '*:IDs:'

          ;;
        (edit)
          _arguments                                          \
            ${_common_options[@]}                             \
            "(--help --command)"--command'[COMMAND]:command:' \
            "(--help --type)"--type'[TYPE]:type:'             \
            "(--help --sequence)"--sequence'[NUM]:number:'    \
            '*:IDs:'

          ;;
        (list|status)
          _arguments              \
            ${_common_options[@]} \

            ;;
        (test)
          _arguments                                                                                     \
            ${_common_options[@]}                                                                        \
            "(--help --dry-run)"--dry-run'[Print the script on stdout rather than run it]'               \
            "(--help --base-rcvid)"--base-rcvid'[N Pretend that the hook-last-rcvid value is N]:number:' \
            "(--help --new-rcvid)"--new-rcvid'[M Pretend that the last rcvid valud is M]:number:'        \
            "(--help --aux-file)"--aux-file'[NAME NAME is substituted for %A in the script]:name:'       \
            '1:ID:'

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}

########################################
# fossil json                          #
########################################
function __fossil_json_subcommands() {
  _values 'subcommand' \
    anonymousPassword artifact branch cap config diff dir g login logout \
    query rebuild report resultCodes stat tag timeline user version \
    whoami wiki
}

function __fossil_json() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(-)"--help'[Show help and exit]'
    "(--help -R --repository)"{-R,--repository}'[FILE Run commands on repository FILE]:fossils:($(__fossil_repos))'
  )

  _arguments -C                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _arguments                                                        \
        '(- *)'-json-input'[FILE Read JSON data from FILE]:file:_files' \
        '1:subcommand:__fossil_json_subcommands'

      ;;
    (options)
      case $line[1] in
        -json-input)
          _arguments              \
            ${_common_options[@]} \
            '1:file:_files'

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
    *)
  esac
  return 0
}

########################################
# fossil login-group                   #
########################################
function __fossil_login_group() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(-)"--help'[Show help and exit]'
  )

  _arguments -C                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _values 'subcommand' \
        join leave

      ;;
    (options)
      case $line[1] in
        (join)
          _arguments                                                      \
            "(-name)"-name'[Specified the name of the login group]:name:' \
            ':fossils:($(__fossil_repos))'

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}

########################################
# fossil purge                         #
########################################
function __fossil_purge() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(--help --explain --dry-run)"--explain'[Make no changes, but show what would happen]'
    "(--help --dry-run --explain)"--dry-run'[An alias for --explain]'
    "(-)"--help'[Show help and exit]'
  )

  _arguments -C                 \
    "(-)"--help'[Show help and exit]' \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _values 'subcommand' \
        artifacts cat checkins files list ls obliterate tickets undo wiki

      ;;
    (options)
      case $line[1] in
        (artifacts|cat)
          _arguments              \
            ${_common_options[@]} \
            '*:hashes:'

          ;;
        (checkins)
          _arguments              \
            ${_common_options[@]} \
            '*:tags:($(__fossil_tags))'

          ;;
        (files)
          _arguments              \
            ${_common_options[@]} \
            '*:files:_files'

          ;;
        (list|ls)
          _arguments              \
            ${_common_options[@]} \
            "(--help -l)"-l'[Provide more details]'

          ;;
        (obliterate)
          _arguments                                           \
            ${_common_options[@]}                              \
            "(--help --force)"--force'[Suppress confirmation prompt]' \
            '*:IDs:'

          ;;
        (tickets)
          _arguments              \
            ${_common_options[@]} \
            '*:ticket names:'

          ;;
        (undo)
          _arguments              \
            ${_common_options[@]} \
            '1:ID:'

          ;;
        (wiki)
          _arguments              \
            ${_common_options[@]} \
            '*:wiki pages:($(__fossil_wiki_pages))'

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}

########################################
# fossil remote                        #
########################################
function __fossil_remote() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(-)"--help'[Show help and exit]'
  )

  _arguments -C                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _values 'subcommand' \
        add delete list off

      ;;
    (options)
      case $line[1] in
        (add)
          _arguments              \
            '1:name:'             \
            '2:url:__fossil_urls'

          ;;

        (delete)
          _arguments              \
            '1:name:($(__fossil_remotes))'

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}

########################################
# fossil stash                         #
########################################
function __fossil_stash_ids() {
  fossil stash ls | grep '^\s*\d\+:' | awk -F ':' '{print $1}' | sed 's/^ *//'
}

function __fossil_stash() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(-)"--help'[Show help and exit]'
  )

  _arguments -C                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _values 'subcommand' \
        save snapshot list ls show gshow pop apply goto drop diff gdiff

      ;;
    (options)
      case $line[1] in
        (save|snapshot)
          _arguments                                                   \
            "(-m --comment)"{-m,--comment}'[MSG Add comment]:comment:' \
            '*:files:_files'

          ;;
        (list|ls)
          _arguments \
            "(-v --verbose)"{-v,--verbose}'[Show info about individual files]' \
            "(-W --width)"{-W,--width}'[N Set width]:number:'

          ;;
        (show|cat|diff|gdiff)
          _arguments \
            ${_fossil_diff_options[@]} \
            '1::stash ID:($(__fossil_stash_ids))'

          ;;
        (apply|goto)
          _arguments \
            '1::stash ID:($(__fossil_stash_ids))'

          ;;
        (drop|rm)
          _arguments                                                        \
            "(-a --all)"{-a,--all}'[Forget the whole stash (CANNOT UNDO!)]' \
            '1::stash ID:($(__fossil_stash_ids))'

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}

########################################
# fossil tag                           #
########################################
function __fossil_tag() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(-)"--help'[Show help and exit]'
  )

  _arguments -C                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _values 'subcommand' \
         add cancel find list ls

      ;;
    (options)
      case $line[1] in
         (add)
            _arguments                                                                                              \
              ${_common_options[@]}                                                                                     \
               "(--help --raw)"--raw'[Raw tag name]'                                                                    \
               "(--help --propagate)"--propagate'[Propagating tag]'                                                     \
               "(--help --date-override)"--date-override'[DATETIME Set date and time added]:datetime:'                  \
               "(--help --user-override)"--user-override'[USER Name USER when adding the tag]:user:($(__fossil_users))' \
               "(--help --dryrun -n)"{--dryrun,-n}'[Display the tag text, but do not]'                                  \
               '1:tag name:($(__fossil_tags))'                                                                               \
               '2:check-in:'                                                                                            \
               '3::value:'

            ;;
         (cancel)
            _arguments                                                                                                    \
              ${_common_options[@]}                                                                                       \
               "(--help --raw)"--raw'[Raw tag name]'                                                                      \
               "(--help --date-override)"--date-override'[DATETIME Set date and time deleted]:datetime:'                  \
               "(--help --user-override)"--user-override'[USER Name USER when deleting the tag]:user:($(__fossil_users))' \
               "(--help --dryrun -n)"{--dryrun,-n}'[Display the control artifact, but do]'                                \
               '1:tag name:($(__fossil_tags))'                                                                                 \
               '2:check-in:'

            ;;
        (find)
           _arguments                                                          \
             ${_common_options[@]}                                             \
            "(--help --raw)"--raw'[Raw tag name]'                              \
            "(--help -t --type)"{-t,--type}'[TYPE One of "ci", or "e"]:(ci e)' \
            "(--help -n --limit)"{-n,--limit}'[N Limit to N results]:number:'  \
            '1:tag name:($(__fossil_tags))'

          ;;
       (list|ls)
          _arguments                                                                          \
            ${_common_options[@]}                                                             \
            "(--help --raw)"--raw'[List tags raw names of tags]'                              \
            "(--help --tagtype)"--tagtype'[TYPE List only tags of type TYPE]:tag type:(ci e)' \
            "(--help -v --inverse)"{-v,--inverse}'[Inverse the meaning of --tagtype TYPE]'    \
            '1::check-in:'

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}

########################################
# fossil ticket                        #
########################################
function __fossil_ticket() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(-)"--help'[Show help and exit]'
  )

  _arguments -C                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _values 'subcommand' \
        show list ls set change add history

      ;;
    (options)
      case $line[1] in
        (show)
          _arguments                                                                        \
            ${_common_options[@]}                                                           \
            "(--help -l --limit)"{-l,--limit}'[LIMITCHAR]:limit:'                           \
            "(--help -q --quote)"{-q,--quote}'[]'                                           \
            "(--help -R --repository)"{-R,--repository}'[FILE]:fossils:($(__fossil_repos))' \
            '1:report title/nr:'                                                            \
            '2::ticket filter:'

          ;;
       (list|ls)
          _arguments \
             ':what:(fields reports)'

          ;;
       (set|change)
          _arguments                              \
            ${_common_options[@]}                 \
            "(--help -q --quote)"{-q,--quote}'[]' \
            '1:ticket UUID:'                      \
            '*:field/value:'

          ;;
       (add)
          _arguments                              \
            ${_common_options[@]}                 \
            "(--help -q --quote)"{-q,--quote}'[]' \
            '*:field/value:'

          ;;
       (history)
          _arguments \
            ':ticket UUID:'

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}

########################################
# fossil tls-config                    #
########################################
function __fossil_tls_config() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(-)"--help'[Show help and exit]'
  )

  _arguments -C                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _values 'subcommand' \
        show remove-exception

      ;;
    (options)
      case $line[1] in
        (remove-exception)
          _arguments \
            '*:domains:'

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}

########################################
# fossil unversioned/uv                #
########################################
function __fossil_unversioned() {
  local curcontext="$curcontext" state line
  typeset -A opt_args
  local -a _common_options

  _common_options=(
    "(--help -R --repository)"{-R,--repository}'[FILE Use FILE as the repository]:fossils:($(__fossil_repos))'
    "(--help --mtime)"--mtime'[TIMESTAMP Use TIMESTAMP instead of "now"]:timestamp:'
    "(-)"--help'[Show help and exit]'
  )

  _arguments -C                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _values 'subcommand' \
        add cat edit export list ls revert remove rm delete sync touch

      ;;
    (options)
      case $line[1] in
        (add)
          _arguments                                  \
            ${_common_options[@]}                     \
            "(--help --as)"--as'[UVFILE]:file:_files' \
            '*:files:_files'

          ;;
        (cat)
          _arguments              \
            ${_common_options[@]} \
            '*:files:_files'

          ;;
        (edit)
          _arguments              \
            ${_common_options[@]} \
            '1:file:_files'

          ;;
        (export)
          _arguments              \
            ${_common_options[@]} \
            '1:file:_files' \
            '2:output file:_files'

          ;;
        (list|ls)
          _arguments                                                               \
            ${_common_options[@]}                                                  \
            "(--help --glob)"--glob'[PATTERN Show only files that match]:pattern:' \
            "(--help --like)"--like'[PATTERN Show only files that match]:pattern:'

          ;;
        (revert)
          _arguments                                                             \
            ${_common_options[@]}                                                \
            "(--help -v --verbose)"{-v,--verbose}'[Extra diagnostic output]'     \
            "(--help -n --dryrun)"{-n,--dryrun}'[Show what would have happened]' \
            '1::url:__fossil_urls'

          ;;
        (remove|rm|delete)
          _arguments                                                            \
            ${_common_options[@]}                                               \
            "(--help --glob)"--glob'[PATTERN Remove files that match]:pattern:' \
            "(--help --like)"--like'[PATTERN Remove files that match]:pattern:' \
            '*:files:_files'

          ;;
        (sync)
          _arguments                                                             \
            ${_common_options[@]}                                                \
            "(--help -v --verbose)"{-v,--verbose}'[Extra diagnostic output]'     \
            "(--help -n --dryrun)"{-n,--dryrun}'[Show what would have happened]' \
            '1::url:__fossil_urls'

          ;;
        (touch)
          _arguments              \
            ${_common_options[@]} \
            '*:files:_files'

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}

########################################
# fossil user                          #
########################################
function __fossil_user() {
  local curcontext="$curcontext" state line
  typeset -a opt_args
  local -a _common_options

  _common_options=(
    "(--help -R --repository)"{-R,--repository}'[FILE Apply command to repository FILE]:fossils:($(__fossil_repos))'
    "(-)"--help'[show help and exit]'
  )

  _arguments -c                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _values 'subcommand' \
        capabilities default list ls new password

      ;;
    (options)
      case $line[1] in
        (capabilities)
          _arguments                     \
            ${_common_options[@]}        \
            '1:user:($(__fossil_users))' \
            '2::string:'

          ;;
        (default)
          _arguments              \
            ${_common_options[@]} \
            '1::user:($(__fossil_users))'

          ;;
        (list|ls)
          _arguments \
            ${_common_options[@]}

          ;;
        (new)
          _arguments                      \
            ${_common_options[@]}         \
            '1::username:'                \
            '2::contact info:'            \
            '3::password:'

          ;;
        (password)
          _arguments                     \
            ${_common_options[@]}        \
            '1:user:($(__fossil_users))' \
            '2::password:'

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}

########################################
# fossil wiki                          #
########################################
function __fossil_wiki() {
  local curcontext="$curcontext" state line
  typeset -a opt_args
  local -a _common_options

  _common_options=(
    "(-)"--help'[show help and exit]'
  )

  _arguments -c                 \
    ${_common_options[@]}       \
    '1:subcommand:->subcommand' \
    '*::options:->options'

  case $state in
    (subcommand)
      _values 'subcommand' \
        export create commit list

      ;;
    (options)
      case $line[1] in
        (export)
          _arguments                                                                                                     \
            ${_common_options[@]}                                                                                        \
            "(--help --technote -t)"{--technote,-t}'[DATETIME|TECHNOTE-ID Export a technote]:datetime/technote-id:(now)' \
            "(--help -h --html -H --HTML)"--html'[Render only HTML body]'                                                \
            "(--help -H --HTML -h --html)"--HTML'[Like -h|-html but wraps the output in <html>/</html>]'                 \
            "(--help -p --pre)"{-p,--pre}'[Wrap into <pre>...</pre>]'                                                    \
            '::pagename:($(__fossil_wiki_pages))'                                                                        \
            '::file:_files'

          ;;
        (create|commit)
          _arguments                                                                                        \
            ${_common_options[@]}                                                                           \
            "(--help -M --mimetype)"{-M,--mimetype}'[TEXT-FORMAT The mime type of the update]:mimetype:'    \
            "(--help --technote -t)"{--technote,-t}'[DATETIME|TECHNOTE-ID]:datetime/technote-id:(now)'      \
            "(--help --technote-tags)"--technote-tags'[TAGS The set of tags for a technote]:tags:'          \
            "(--help --technote-bgcolor)"--technote-bgcolor'[COLOR The color used for the technote]:color:' \
            '1:pagename:($(__fossil_wiki_pages))'                                                                                    \
            '2::file:_files'

          ;;
        (list|ls)
          _arguments                                                  \
            ${_common_options[@]}                                     \
            "(--help -t --technote)"{-t,--technote}'[List technotes]' \
            "(--help -s --show-technote-ids)"{-s,--show-technote-ids}'[The id of the tech note will be listed]'

          ;;
        *)
          _message 'no more arguments'

          ;;
      esac
      ;;
  esac
  return 0
}


########################################
# fossil test commands                 #
########################################

function __fossil_complete_test_commands() {
  case $line[1] in
    (test-add-alerts)
      _arguments                                                                                       \
        "(--help --backoffice)"--backoffice'[Run alert_backoffice() after all alerts have been added]' \
        "(--help --debug)"--debug'[Like --backoffice, but print to stdout]'                            \
        "(--help --digest)"--digest'[Process emails using SENDALERT_DIGEST]'                           \
        '(- *)'--help'[Show help and exit]'                                                            \
        '*:event IDs:'

      ;;
    (test-agg-cksum)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-alert)
      _arguments                                                                          \
        "(--help --digest)"--digest'[Generate digest alert text]'                         \
        "(--help --needmod)"--needmod'[Assume all events are pending moderator approval]' \
        '(- *)'--help'[Show help and exit]'                                               \
        '*:event IDs:'

      ;;
    (test-all-help)
      _arguments                                                                   \
        "(--help -e --everything)"{-e,--everything}'[Show all commands and pages]' \
        "(--help -t --test)"{-t,--test}'[Include test- commands]'                  \
        "(--help -w --www)"{-w,--www}'[Show WWW pages]'                            \
        "(--help -s --settings)"{-s,--settings}'[Show settings]'                   \
        "(--help -h --html)"{-h,--html}'[Transform output to HTML]'                \
        "(--help -r --raw)"{-r,--raw}'[No output formatting]'                      \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-ambiguous)
      _arguments                                                                        \
        "(--help --minsize)"--minsize'[N Show hases with N characters or more]:number:' \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-ancestor-path)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:version 1:'                      \
        '2:version 2:'

      ;;
    (test-approx-match)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-backlinks)
      _arguments                                                                      \
        "(--help --mtime)"--mtime'[DATETIME Use an alternative date/time]:datetime:'  \
        "(--help --mimetype)"--mimetype'[TYPE Use an alternative mimetype]:mimetype:' \
        '(- *)'--help'[Show help and exit]'                                           \
        '1:srctype:'                                                                  \
        '2:srcid:'                                                                    \
        '3:input file:_files'

      ;;
    (test-backoffice-lease)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-builtin-get)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:name:'                           \
        '2::output file:_files'

      ;;
    (test-builtin-list)
      _arguments                                                          \
        "(--help --verbose)"--verbose'[Output total item count and size]' \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-canonical-name)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '*:files:_files'

      ;;
    (test-captcha)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '*:numbers:'

      ;;
    (test-ci-mini)
      _arguments                                                                                                         \
        "(--help --repository -R)"{--repository,-R}'[REPO The repository file to commit to]:fossils:($(__fossil_repos))' \
        "(--help --as)"--as'[FILENAME The repository-side name of the input file]:file:_files'                           \
        "(--help --comment -m)"{--comment,-m}'[COMMENT Required checkin comment]:comment:'                               \
        "(--help --comment-file -M)"{--comment-file,-M}'[FILE Reads checkin comment from the given file]:file:_files'    \
        "(--help --revision -r)"{--revision,-r}'[VERSION Commit from this version]:version:'                             \
        "(--help --allow-fork)"--allow-fork'[Allow commit against a non-leaf parent]'                                    \
        "(--help --allow-merge-conflict)"--allow-merge-conflict'[Allow checkin of a file with conflict markers]'         \
        "(--help --user-override)"--user-override'[USER User to use instead of the default]:user:($(__fossil_users))'    \
        "(--help --date-override)"--date-override'[DATETIME Date to use instead of '"'"'now'"'"']:datetime:(now)'        \
        "(--help --allow-older)"--allow-older'[Allow a commit to be older than its ancestor]'                            \
        "(--help --convert-eol-inherit)"--convert-eol-inherit'[Inherit EOL style from previous content]'                 \
        "(--help --convert-eol-unix)"--convert-eol-unix'[Convert the EOL style to Unix]'                                 \
        "(--help --convert-eol-windows)"--convert-eol-windows'[Convert the EOL style to Windows]'                        \
        "(--help --delta)"--delta'[Prefer to generate a delta manifest]'                                                 \
        "(--help --allow-new-file)"--allow-new-file'[Allow addition of a new file this way]'                             \
        "(--help --dump-manifest -d)"{--dump-manifest,-d}'[Dumps the generated manifest to stdout]'                      \
        "(--help --save-manifest)"--save-manifest'[FILE Saves the generated manifest to a file]:file:_files'             \
        "(--help --wet-run)"--wet-run'[Disables the default dry-run mode]'                                               \
        '(- *)'--help'[Show help and exit]'                                                                              \
        '1:file:_files'

      ;;
    (test-clusters)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-command-stats)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-comment-format)
      _arguments                                                                                               \
        "(--help --file)"--file'[The comment text is really just a file name to read it from]'                 \
        "(--help --decode)"--decode'[Decode the text using the same method used for a C-card from a manifest]' \
        "(--help --legacy)"--legacy'[Use the legacy comment printing algorithm]'                               \
        "(--help --trimcrlf)"--trimcrlf'[Enable trimming of leading/trailing CR/LF]'                           \
        "(--help --trimspace)"--trimspace'[Enable trimming of leading/trailing spaces]'                        \
        "(--help --wordbreak)"--wordbreak'[Attempt to break lines on word boundaries]'                         \
        "(--help --origbreak)"--origbreak'[Attempt to break when the original comment text is detected]'       \
        "(--help --indent)"--indent'[NUM Number of spaces to indent]:number:'                                  \
        "(--help -W --width)"{-W,--width}'[NUM Width of lines]:number:'                                        \
        '(- *)'--help'[Show help and exit]'                                                                    \
        '1:prefix:'                                                                                            \
        '2:text:'                                                                                              \
        '3::origtext:'

      ;;
    (test-commit-warning)
      _arguments                                                                                 \
        "(--help --no-settings)"--no-settings'[Do not consider any glob settings]'               \
        "(--help -v --verbose)"{-v,--verbose}'[Show per-file results for all pre-commit checks]' \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-compress)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:input file:_files'               \
        '2:output file:_files'

      ;;
    (test-compress-2)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:input file 1:_files'             \
        '2:input file 2:_files'             \
        '3:output file:_files'

      ;;
    (test-contains-selector)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:file:_files'                     \
        '2:css selector:'

      ;;
    (test-content-deltify)
      _arguments                            \
        "(--help --force)"--force'[]'       \
        '(- *)'--help'[Show help and exit]' \
        '1:rid:'                            \
        '*:src id:'

      ;;
    (test-content-erase)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '*:rid:'

      ;;
    (test-content-put)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:file:_files'

      ;;
    (test-content-rawget)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-content-undelta)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:record id:'

      ;;
    (test-convert-stext)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:file:_files'                     \
        '2:mimetype:'

      ;;
    (test-create-clusters)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-crosslink)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:record id:'

      ;;
    (test-cycle-compress)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-database-names)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-date-format)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '*:date string:'

      ;;
    (test-db-exec-error)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-decode-email)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:file:_files'

      ;;
    (test-decode64)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:string:'

      ;;
    (test-delta)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:file:_files'                     \
        '2:file:_files'

      ;;
    (test-delta-analyze)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:file:_files'                     \
        '2:file:_files'

      ;;
    (test-delta-apply)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:file:_files'                     \
        '2:delta:_files'

      ;;
    (test-delta-create)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:file:_files'                     \
        '2:file:_files'                     \
        '3:delta:_files'

      ;;
    (test-describe-artifacts)
      _arguments                                                   \
        "(--help --from)"--from'[S An artifact]:artifact:'         \
        "(--help --count)"--count'[N Number of artifacts]:number:' \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-detach)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1::fossils:($(__fossil_repos))'

      ;;
    (test-diff)
      _arguments                   \
        ${_fossil_diff_options[@]} \
        '1:file 1:_files'          \
        '2:file 2:_files'

      ;;
    (test-dir-size)
      _arguments                                                              \
        "(--help --nodots)"--nodots'[Omit files that begin with '"'"'.'"'"']' \
        '(- *)'--help'[Show help and exit]'                                   \
        '1:directory:_files -/'                                               \
        '2::glob pattern:'

      ;;
    (test-echo)
      _arguments                                                \
        "(--help --hex)"--hex'[Show the output as hexadecimal]' \
        '(- *)'--help'[Show help and exit]'                     \
        '*:args:'

      ;;
    (test-emailblob-refcheck)
      _arguments                                                                       \
        "(--help --repair)"--repair'[Fix up the enref field]'                          \
        "(--help --full)"--full'[Give a full report]'                                  \
        "(--help --clean)"--clean'[Used with --repair, removes entries with enref==0]' \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-encode64)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:string:'

      ;;
    (test-escaped-arg)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '*:args:'

      ;;
    (test-etag)
      _arguments                                    \
        "(--help --key)"--key'[KEYNUM]:key number:' \
        "(--help --hash)"--hash'[HASH]:hash:'       \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-file-copy)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:source:_files'                   \
        '2:destination:_files'

      ;;
    (test-file-environment)
      _arguments                                                                                                 \
        "(--help --allow-symlinks)"--allow-symlinks'[BOOL Temporarily turn allow-symlinks on/off]:bool:(yes no)' \
        "(--help --open-config)"--open-config'[Open the configuration database first]'                           \
        "(--help --slash)"--slash'[Trailing slashes, if any, are retaine]'                                       \
        "(--help --reset)"--reset'[Reset cached stat() info for each file]'                                      \
        '(- *)'--help'[Show help and exit]'                                                                      \
        '*:files:_files'

      ;;
    (test-fileage)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:checkin:'

      ;;
    (test-filezip)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-find-mx)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '*:domain:'

      ;;
    (test-find-pivot)
      _arguments                                                                               \
        "(--help --ignore-merges)"--ignore-merges'[Ignore merges for discovering name pivots]' \
        '(- *)'--help'[Show help and exit]'                                                    \
        '*:args:'

      ;;
    (test-fingerprint)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1::rcvid:'

      ;;
    (test-forumthread)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:thread id:'

      ;;
    (test-fossil-system)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-fuzz)
      _arguments                                                      \
        "(--help --type)"--type'[TYPE]:type:(wiki markdown artifact)' \
        '(- *)'--help'[Show help and exit]'                           \
        '*:files:_files'

      ;;
    (test-glob)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:pattern:'                        \
        '2:string:'

      ;;
    (test-grep)
      _arguments                                                     \
        "(--help -i --ignore-case)"{-i,--ignore-case}'[Ignore case]' \
        '(- *)'--help'[Show help and exit]'                          \
        '1:regexp:'                                                  \
        '*:files:_files'

      ;;
    (test-gzip)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:file:_files'

      ;;
    (test-hash-color)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '*:tags:($(__fossil_tags))'

      ;;
    (test-hash-passwords)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:fossils:($(__fossil_repos))'

      ;;
    (test-html-tidy)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-html-to-text)
      _arguments \
        '(- *)'--help'[Show help and exit]' \
        '*:files:_files'

      ;;
    (test-html-tokenize)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-http)
      _arguments                                                                        \
        "(--help --th-trace)"--th-trace'[Trace TH1 execution (for debugging purposes)]' \
        "(--help --usercap)"--usercap'[CAP user capability string]:capability string:'  \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-httpmsg)
      _arguments                                                                  \
        "(--help --compress)"--compress'[Use ZLIB compression on the payload]'    \
        "(--help --mimetype)"--mimetype'[TYPE Mimetype of the payload]:mimetype:' \
        "(--help --out)"--out'[FILE Store the reply in FILE]:file:_files'         \
        "(--help -v)"-v'[Verbose output]'                                         \
        '(- *)'--help'[Show help and exit]'                                       \
        '1:url:__fossil_urls' \
        '2::payload:'

      ;;
    (test-integrity)
      _arguments                                                                                   \
        "(--help -d --db-only)"{-d,--db-only}'[Run "PRAGMA integrity_check" on the database only]' \
        "(--help --parse)"--parse'[Parse all manifests, wikis, tickets, events, etc]'              \
        "(--help -q --quick)"{-q,--quick}'[Run "PRAGMA quick_check" on the database only]'         \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-is-reserved-name|test-is-ckout-db)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '*:files:_files'

      ;;
    (test-ishuman)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-isspace)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-leaf-ambiguity)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '*:names:'

      ;;
    (test-list-page)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:directory:_files -/'

      ;;
    (test-list-webpage)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-loadavg)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-looks-like-utf)
      _arguments                                                                              \
        "(--help -n --limit)"{-n,--limit}'[NUM Repeat looks-like function NUM times]:number:' \
        "(--help --utf8)"--utf8'[Ignoring BOM and file size, force UTF-8 checking]'           \
        "(--help --utf16)"--utf16'[Ignoring BOM and file size, force UTF-16 checking]'        \
        '(- *)'--help'[Show help and exit]'                                                   \
        '1:file:_files'

      ;;
    (test-mailbox-hashname)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '*:human name:'

      ;;
    (test-markdown-render)
      _arguments                                                               \
        "(--help --safe)"--safe'[Restrict the output to use only "safe" HTML]' \
        '(- *)'--help'[Show help and exit]'                                    \
        '*:files:_files'

      ;;
    (test-match)
      _arguments                                                                 \
        "(--help --begin)"--begin'[TEXT Text to insert before each match]:text:' \
        "(--help --end)"--end'[TEXT Text to insert after each match]:text:'      \
        "(--help --gap)"--gap'[TEXT Text to indicate elided content]:text:'      \
        "(--help --html)"--html'[Input is HTML]'                                 \
        "(--help --static)"--static'[Use the static Search object]'              \
        '(- *)'--help'[Show help and exit]'                                      \
        '1:search string:'                                                       \
        '*:files:_files'

      ;;
    (test-mimetype)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '*:files:_files'

      ;;
    (test-missing)
      _arguments                                                               \
        "(--help --notshunned)"--notshunned'[Do not report shunned artifacts]' \
        "(--help --quiet)"--quiet'[Only show output if there are errors]'      \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-move-repository)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:pathname:_files'

      ;;
    (test-name-changes)
      _arguments                                      \
        "(--help --debug)"--debug'[Enable debugging]' \
        '(- *)'--help'[Show help and exit]'           \
        '1:version 1:'                                \
        '2:version 2:'

      ;;
    (test-name-to-id)
      _arguments                                                             \
        "(--help --count)"--count'[N Repeat the conversion N times]:number:' \
        '(- *)'--help'[Show help and exit]'                                  \
        '*:name:'

      ;;
    (test-obscure)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '*:args:'

      ;;
    (test-orphans)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-parse-all-blobs)
      _arguments \
        "(--help --limit)"--limit'[N Parse no more than N blobs]:number:' \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-parse-manifest)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:file:_files'                     \
        '2::n:'

      ;;
    (test-phantoms)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-process-id)
      _arguments                                                                  \
        "(--help --sleep)"--sleep'[N Sleep for N seconds before exiting]:number:' \
        '(- *)'--help'[Show help and exit]'                                       \
        '*:process id:'

      ;;
    (test-prompt-password)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:prompt:'                         \
        '2:verify:(0 1 2)'

      ;;
    (test-prompt-user)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:prompt:'

      ;;
    (test-random-password)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1::length:'

      ;;
    (test-rawdiff)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:file 1:_files'                   \
        '2:file 2:_files'

      ;;
    (test-relative-name)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-reserved-names)
      _arguments                          \
        "(--help -omitrepo)"-omitrepo'[]' \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-safe-html)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '*:files:_files'

      ;;
    (test-sanitize-name)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '*:args:'

      ;;
    (test-search-stext)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:type:(c d e t w)'                \
        '2:rid:'                            \
        '3:name:'

      ;;
    (test-set-mtime)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:file:_files'                     \
        '2:date/time:'

      ;;
    (test-shortest-path)
      _arguments                                                                 \
        "(--help --no-merge)"--no-merge'[Follow only direct parent-child paths]' \
        '(- *)'--help'[Show help and exit]'                                      \
        '1:version 1:'                                                           \
        '2:version 2:'

      ;;
    (test-simplify-name)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '*:files:_files'

      ;;
    (test-smtp-probe)
      _arguments                                                                    \
        "(--help --direct)"--direct'[Use DOMAIN directly without going through MX]' \
        "(--help --port)"--port'[N Talk on TCP port N]:number:'                     \
        '(- *)'--help'[Show help and exit]'                                         \
        '1:domain:'                                                                 \
        '2::me:'

      ;;
    (test-smtp-send)
      _arguments                                                                       \
        "(--help --direct)"--direct'[Bypass MX lookup]'                                \
        "(--help --relayhost)"--relayhost'[HOST Use HOST as relay for delivery]:host:' \
        "(--help --port)"--port'[Use TCP port N instead of 25]:number:'                \
        "(--help --trace)"--trace'[Show the SMTP conversation on the console]'         \
        '(- *)'--help'[Show help and exit]'                                            \
        '1:email:_files'                                                               \
        '2:from:'                                                                      \
        '*:to:'

      ;;
    (test-smtp-senddata)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:file:_files'

      ;;
    (test-subtree)
      _arguments                                                                                             \
        "(--help --branch)"--branch'[BRANCH Include only check-ins on BRANCH]:branch:($(__fossil_branches))' \
        "(--help --from)"--from'[TAG Start the subtree at TAG]:tag:($(__fossil_tags))'                       \
        "(--help --to)"--to'[TAG End the subtree at TAG]:tag:($(__fossil_tags))'                             \
        "(--help --checkin)"--checkin'[TAG The subtree is the single check-in TAG]:tag:($(__fossil_tags))'   \
        "(--help --all)"--all'[Include FILE and TAG artifacts]'                                              \
        "(--help --exclusive)"--exclusive'[Include FILES exclusively on check-ins]'                          \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-tag)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:tag:($(__fossil_tags))'          \
        '2:artifact id:'                    \
        '3::value:'

      ;;
    (test-tarball)
      _arguments                                                                                          \
        "(--help -h --dereference)"{-h,--dereference}'[Follow symlinks; archive the files they point to]' \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-tempname)
      _arguments                                                                              \
        "(--help --time)"--time'[SUFFIX Generate names based on the time of the day]:suffix:' \
        "(--help --tag)"--tag'[NAME Try to use NAME as the differentiator]:name:'             \
        '(- *)'--help'[Show help and exit]'                                                   \
        '*:basename:'

      ;;
    (test-terminal-size)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-th-eval)
      _arguments                                                                      \
        "(--help --cgi)"--cgi'[Include a CGI response header in the output]'          \
        "(--help --http)"--http'[Include an HTTP response header in the output]'      \
        "(--help --open-config)"--open-config'[Open the configuration database]'      \
        "(--help --set-anon-caps)"--set-anon-caps'[Set anonymous login capabilities]' \
        "(--help --set-user-caps)"--set-user-caps'[Set user login capabilities]'      \
        "(--help --th-trace)"--th-trace'[Trace TH1 execution]'                        \
        '(- *)'--help'[Show help and exit]'                                           \
        '1:script:_files'

      ;;
    (test-th-render)
      _arguments                                                                      \
        "(--help --cgi)"--cgi'[Include a CGI response header in the output]'          \
        "(--help --http)"--http'[Include an HTTP response header in the output]'      \
        "(--help --open-config)"--open-config'[Open the configuration database]'      \
        "(--help --set-anon-caps)"--set-anon-caps'[Set anonymous login capabilities]' \
        "(--help --set-user-caps)"--set-user-caps'[Set user login capabilities]'      \
        "(--help --th-trace)"--th-trace'[Trace TH1 execution]'                        \
        '(- *)'--help'[Show help and exit]'                                           \
        '1:file:_files'

      ;;
    (test-th-source)
      _arguments                                                                      \
        "(--help --cgi)"--cgi'[Include a CGI response header in the output]'          \
        "(--help --http)"--http'[Include an HTTP response header in the output]'      \
        "(--help --open-config)"--open-config'[Open the configuration database]'      \
        "(--help --set-anon-caps)"--set-anon-caps'[Set anonymous login capabilities]' \
        "(--help --set-user-caps)"--set-user-caps'[Set user login capabilities]'      \
        "(--help --th-trace)"--th-trace'[Trace TH1 execution]'                        \
        '(- *)'--help'[Show help and exit]'                                           \
        '1:file:_files'

      ;;
    (test-ticket-rebuild)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:ticket id:(all)'

      ;;
    (test-timespan)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:timestamp:'

      ;;
    (test-timewarp-list)
      _arguments \
        '(- *)'--help'[Show help and exit]' \
        '*:files:_files'

      ;;
    (test-topological-sort)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-tree-name)
      _arguments                                                                                                     \
        "(--help --absolute)"--absolute'[Return an absolute path instead of a relative one]'                         \
        "(--help --case-sensitive)"--case-sensitive'[BOOL Enable or disable case-sensitive filenames]:bool:(yes no)' \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-unclustered)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-uncompress)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:in:_files'                       \
        '2:out:_files'

      ;;
    (test-unsent)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-urlparser)
      _arguments                                                            \
        "(--help --remember)"--remember'[Store results in last-sync-url]'   \
        "(--help --prompt-pw)"--prompt-pw'[Prompt for password if missing]' \
        '(- *)'--help'[Show help and exit]'                                 \
        '1:url:__fossil_urls'

      ;;
    (test-usernames)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-valid-for-windows)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '*:files:_files'

      ;;
    (test-var-get)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:var:'                            \
        '2::file:_files'

      ;;
    (test-var-list)
      _arguments                                                               \
        "(--help --unset)"--unset'[Delete entries instead of displaying them]' \
        "(--help --mtime)"--mtime'[Show last modification time]'               \
        '(- *)'--help'[Show help and exit]'                                    \
        '1::pattern:'

      ;;
    (test-var-set)
      _arguments                                                                    \
        "(--help --blob --file)"--blob'[FILE Binary file to read from]:file:_files' \
        "(--help --file --blob)"--file'[FILE File to read from]:file:_files'        \
        '(- *)'--help'[Show help and exit]'                                         \
        '1:var:'                                                                    \
        '2::value:'

      ;;
    (test-verify-all)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-whatis-all)
      _arguments \
        '(- *)'--help'[Show help and exit]'

      ;;
    (test-which)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '*:executable name:'

      ;;
    (test-wiki-relink)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:wiki page:($(__fossil_wiki_pages))'

      ;;
    (test-wiki-render)
      _arguments                                                            \
        "(--help --buttons)"--buttons'[Set the WIKI_BUTTONS flag]'          \
        "(--help --htmlonly)"--htmlonly'[Set the WIKI_HTMLONLY flag]'       \
        "(--help --linksonly)"--linksonly'[Set the WIKI_LINKSONLY flag]'    \
        "(--help --nobadlinks)"--nobadlinks'[Set the WIKI_NOBADLINKS flag]' \
        "(--help --inline)"--inline'[Set the WIKI_INLINE flag]'             \
        "(--help --noblock)"--noblock'[Set the WIKI_NOBLOCK flag]'          \
        '(- *)'--help'[Show help and exit]'                                 \
        '1:file:_files'

      ;;
    (test-without-rowid)
      _arguments                                                            \
        "(--help -n --dryrun)"{-n,--dryrun}'[Just print what would happen]' \
        '(- *)'--help'[Show help and exit]'                                 \
        '*:files:_files'

      ;;
    (test-xfer)
      _arguments                            \
        '(- *)'--help'[Show help and exit]' \
        '1:xfer message:'

      ;;
  esac
}

################################################################################
_fossil

