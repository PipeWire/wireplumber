_wpctl_pw_defaults() {
  local defaults="@DEFAULT_SINK@ @DEFAULT_AUDIO_SINK@ @DEFAULT_SOURCE@
                  @DEFAULT_AUDIO_SOURCE@ @DEFAULT_VIDEO_SOURCE@"
  COMPREPLY+=($(compgen -W "$defaults" -- "$cur"))
}

_wpctl() {
  local cur prev words cword
  local commands="status get-volume inspect set-default set-volume set-mute
                 set-profile set-route clear-default settings set-log-level"

  _init_completion -n = || return

  if [[ ${#COMP_WORDS[@]} -eq 2 ]]; then
    COMPREPLY=($(compgen -W "$commands" -- "$cur"))
    return
  fi

  case $prev in
  get-volume | inspect | set-volume | set-mute | set-profile | set-route)
    _wpctl_pw_defaults
    ;;

  clear-default)
    COMPREPLY+=($(compgen -W "0 1 2" -- "$cur"))
    ;;
  esac
}

complete -F _wpctl wpctl
