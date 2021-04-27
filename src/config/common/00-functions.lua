components = {}

function load_module(m, a)
  if not components[m] then
    components[m] = { "libwireplumber-module-" .. m, type = "module", args = a }
  end
end

function load_pw_module(m)
  if not components[m] then
    components[m] = { "libpipewire-module-" .. m, type = "pw_module" }
  end
end

function load_script(s, a)
  if not components[s] then
    components[s] = { s, type = "script/lua", args = a }
  end
end

function load_monitor(s, a)
  load_script("monitors/" .. s .. ".lua", a)
end

function load_access(s, a)
  load_script("access/access-" .. s .. ".lua", a)
end
