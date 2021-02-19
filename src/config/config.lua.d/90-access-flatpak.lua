-- Enables portal permissions via org.freedesktop.impl.portal.PermissionStore
load_module("portal-permissionstore")

-- Flatpak access
load_access("flatpak")

-- Portal access
load_access("portal")
