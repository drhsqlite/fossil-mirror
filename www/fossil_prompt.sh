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
  fossil info 2> /dev/null |tr '\042\047\140' _|grep "^[^ ]*:" |
  while read LINE ; do
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
    # Color the path part of the prompt blue if this is a clean checkout
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
    PS1="\[\033[01;32m\]\u@\h\[\033[00m\]:$c1\w\$$c2 "
  else
    PS1="\[\033[01;32m\]\u@\h\[\033[00m\]:\w\$ "
  fi
}

PROMPT_COMMAND=set_prompt
