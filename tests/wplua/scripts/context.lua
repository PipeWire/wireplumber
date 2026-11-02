Script.async_activation = true

local main_context = Core.get_main_context ()
assert (main_context ~= nil)

assert (main_context:get_properties () ["application.name"] ==
    Core.get_properties () ["application.name"])
assert (main_context:get_vm_type () == Core.get_vm_type ())

assert (main_context:get_core () ~= nil)
local spa_libs = main_context:get_spa_libs ()
assert (spa_libs == nil or spa_libs:is_object ())
local modules = main_context:get_modules ()
assert (modules == nil or modules:is_array ())

local connection = main_context:get_connection ()
assert (connection:get_core () ~= nil)
assert (Core.get_connection ():get_own_bound_id () ==
    connection:get_own_bound_id ())

local info = Core.get_info ()
assert (connection:get_own_bound_id () == Core.get_own_bound_id ())
assert (connection:get_remote_cookie () == info.cookie)
assert (connection:get_remote_name () == info.name)
assert (connection:get_remote_user_name () == info.user_name)
assert (connection:get_remote_host_name () == info.host_name)
assert (connection:get_remote_version () == info.version)
assert (connection:get_remote_properties () ["core.name"] ==
    info.properties ["core.name"])

connection:update_properties { ["test.context-lua"] = "1" }
assert (connection:get_properties () ["test.context-lua"] == "1")

local idle_source = main_context:idle_add (function ()
  error ("destroyed source was called")
end)
idle_source:destroy ()

main_context:idle_add (function ()
  main_context:timeout_add (1, function ()
    local expected_seq
    expected_seq = connection:sync (function (err, seq)
      assert (err == nil)
      assert (seq == expected_seq)
      Script:finish_activation ()
    end)
    assert (expected_seq >= 0)
    return false
  end)
  return false
end)
