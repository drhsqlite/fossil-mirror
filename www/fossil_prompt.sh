function set_prompt() {
  case `fossil status -b` in
    clean)
       PS1="\[\e[1;32m\]\u@\h\[\e[0m\]:\[\e[1;36m\]\w\$\[\e[0m\] "
       ;;
    dirty)
       PS1="\[\e[1;32m\]\u@\h\[\e[0m\]:\[\e[38;5;202m\]\w\$\[\e[0m\] "
       ;;
    *)
       PS1="\[\e[1;32m\]\u@\h\[\e[0m\]:\w\$ "
       ;;
  esac
}
PROMPT_COMMAND=set_prompt
