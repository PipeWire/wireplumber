-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Provides the default metadata object

Script.async_activation = true

default_metadata = ImplMetadata ("default")
default_metadata:activate (Features.ALL, function (m, e)
  if e then
    Script:finish_activation_with_error (
        "failed to activate the default metadata: " .. tostring (e))
  else
    Script:finish_activation ()
  end
end)
