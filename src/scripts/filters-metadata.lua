-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Provides the default metadata object

Script.async_activation = true

filters_metadata = ImplMetadata ("filters")
filters_metadata:activate (Features.ALL, function (m, e)
  if e then
    Script:finish_activation_with_error (
        "failed to activate the filters metadata: " .. tostring (e))
  else
    Script:finish_activation ()
  end
end)
