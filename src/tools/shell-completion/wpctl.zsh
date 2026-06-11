#compdef wpctl

(( $+functions[_wpctl_pw_nodes] )) ||
_wpctl_pw_nodes() {
  local -a pw_objects
  if (( $+commands[pw-dump] )) && (( $+commands[jq] )); then
    local -a pw_objects=(${(@f)"$(2>/dev/null {
    command pw-dump |
      command jq -r '.[] | select(
        .type == "PipeWire:Interface:Node"
      ) |
      {id, type, name: (
        .info.name //
        (.info.props | (
          ."application.name" //
          ."node.name")
        ) //
        .type)
      } |
        "\(.id):\(.name | gsub(":"; "\\:"))"'
      })"})
  fi
  _wpctl_describe_nodes() {_describe "node id" pw_objects "$@"}
  _alternative \
    'pw-defaults:defaults:(@DEFAULT_SINK@ @DEFAULT_AUDIO_SINK@ @DEFAULT_SOURCE@ @DEFAULT_AUDIO_SOURCE@ @DEFAULT_VIDEO_SOURCE@)' \
    'pw-node-id:node id:_wpctl_describe_nodes'
}

local -a node_id=(/$'[^\0]#\0'/ ':pw-node-id:node id:_wpctl_pw_nodes')
local -a volume=(/$'[0-9]##(%|)([+-]|)\0'/ ':volume:volume:( )')
local -a toggle=(/$'[^\0]#\0'/ ':(0 1 toggle)')
local -a index=(/$'[^\0]#\0'/ ':index:index:( )')
local -a set_volume=( "$node_id[@]" "$volume[@]" )
local -a set_mute=( "$node_id[@]" "$toggle[@]" )
local -a set_profile=( "$node_id[@]" "$index[@]" )
local -a set_route=( "$node_id[@]" "$index[@]" )
local -a clear_default_id=(/$'[^\0]#\0'/ ':(0 1 2)')
local -a list_media=(/$'[^\0]#\0'/ ':media-type:media type:(audio video)')
local -a list_object=(/$'[^\0]#\0'/ ':object-type:object type:(devices sinks sources)')
local -a list_args=( "$list_media[@]" "$list_object[@]" )

_regex_words options 'wpctl options' \
  {-h,--help}':show help message and exit'
local -a options=( "$reply[@]" )

_regex_words wpctl-commands 'wpctl commands' \
  'status:show wireplumber status' \
  'list:list PipeWire objects:$list_args' \
  'get-volume:get object volume:$node_id' \
  'inspect:inspect an object:$node_id' \
  'set-default:set a default node:$node_id' \
  'set-volume:set object volume:$set_volume' \
  'set-mute:set object mute:$set_mute' \
  'set-profile:set object profile:$set_profile' \
  'set-route:set object route:$set_route' \
  'clear-default:unset a configured default node:$clear_default_id' \
  'settings:show or change settings' \
  'set-log-level:set the log level of a client' \
  'reset:reset wireplumber/pipewire to defaults'
local -a wpctlcmd=( /$'[^\0]#\0'/ "$options[@]" "#" "$reply[@]")
_regex_arguments _wpctl "$wpctlcmd[@]"
_wpctl "$@"
