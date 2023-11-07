-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Provides the default metadata object

Script.async_activation = true

-- note that args is a WpSpaJson
local args = ...
args = args:parse(1)

local metadata_name = args["metadata.name"]

log = Log.open_topic ("s-metadata")
log:info ("creating metadata object: " .. metadata_name)

impl_metadata = ImplMetadata (metadata_name)
impl_metadata:activate (Features.ALL, function (m, e)
  if e then
    Script:finish_activation_with_error (
        "failed to activate the ".. metadata_name .." metadata: " .. tostring (e))
  else
    Script:finish_activation ()
  end
end)
