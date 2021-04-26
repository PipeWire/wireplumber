default_access = {}
default_access.properties = {}
default_access.rules = {}

function default_access.enable()
  load_access("default", {
    rules = default_access.rules
  })

  if default_access.properties["enable-flatpak-portal"] then
    -- Enables portal permissions via org.freedesktop.impl.portal.PermissionStore
    load_module("portal-permissionstore")
    load_access("portal")
  end
end
