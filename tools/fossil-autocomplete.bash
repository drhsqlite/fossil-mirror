# Command name completion for Fossil.
# Mailing-list contribution by Stuart Rackham.
function _fossil() {
    local cur commands
    cur=${COMP_WORDS[COMP_CWORD]}
    commands=$(fossil help --all)
    if [ $COMP_CWORD -eq 1 ] || [ ${COMP_WORDS[1]} = help ]; then
            # Command name completion for 1st argument or 2nd if help command.
        COMPREPLY=( $(compgen -W "$commands" $cur) )
    else
            # File name completion for other arguments.
        COMPREPLY=( $(compgen -f $cur) )
    fi
}
complete -o default -F _fossil fossil f
