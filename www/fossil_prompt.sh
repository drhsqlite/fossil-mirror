
#-------------------------------------------------------------------------
#   get_fossil_data()
#
# If the current directory is part of a fossil checkout, then populate
# a series of global variables based on the current state of that
# checkout. Variables are populated based on the output of the [fossil info]
# command.
#
# If the current directory is not part of a fossil checkout, set global
# variable $fossil_info_project_name to an empty string and return.
#
function get_fossil_data() {
  fossil_info_project_name=""
  eval `get_fossil_data2`
}
function get_fossil_data2() {
  fossil info 2> /dev/null | sed 's/"//g'|grep "^[^ ]*:" | while read LINE ; do
    local field=`echo $LINE | sed 's/:.*$//' | sed 's/-/_/'`
    local value=`echo $LINE | sed 's/^[^ ]*: *//'`
    echo fossil_info_${field}=\"${value}\"
  done
}

#-------------------------------------------------------------------------
#   set_prompt()
#
# Set the PS1 variable. If the current directory is part of a fossil
# checkout then the prompt contains information relating to the state
# of the checkout.
#
# Otherwise, if the current directory is not part of a fossil checkout, it
# is set to a fairly standard bash prompt containing the host name, user
# name and current directory.
#
function set_prompt() {
  get_fossil_data
  if [ -n "$fossil_info_project_name" ] ; then
    project=$fossil_info_project_name
    checkout=`echo $fossil_info_checkout | sed 's/^\(........\).*/\1/'`
    date=`echo $fossil_info_checkout | sed 's/^[^ ]* *..//' | sed 's/:[^:]*$//'`
    tags=$fossil_info_tags
    local_root=`echo $fossil_info_local_root | sed 's/\/$//'`
    local=`pwd | sed "s*${local_root}**" | sed "s/^$/\//"`

    # Color the first part of the prompt blue if this is a clean checkout.
    # Or red if it has been edited in any way at all. Set $c1 to the escape
    # sequence required to change the type to the required color. And $c2
    # to the sequence that changes it back.
    #
    if [ -n "`fossil chang`" ] ; then
      c1="\[\033[1;31m\]"           # red
    else
      c1="\[\033[1;34m\]"           # blue
    fi
    c2="\[\033[0m\]"

    PS1="$c1${project}.${tags}$c2 ${checkout} (${date}):${local}$c1\$$c2 "
  else
    PS1="\u@\h:\w\$ "
  fi
}

PROMPT_COMMAND=set_prompt
