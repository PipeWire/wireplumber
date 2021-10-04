default_access.properties = {
  ["enable-flatpak-portal"] = true,
}

default_access.rules = {
  {
    matches = {
      {
        { "pipewire.access", "=", "flatpak" },
        { "media.category", "=", "Manager" },
      },
    },
    default_permissions = "all",
  },
  {
    matches = {
      {
        { "pipewire.access", "=", "flatpak" },
      },
    },
    default_permissions = "rx",
  },
}
