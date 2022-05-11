#!/usr/bin/wpexec
--
-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT
--
-- This is an example of how to load a pipewire module that takes a JSON
-- string as arguments.
--
-- For illustration purposes, this loads module-filter-chain with a 6-band
-- equalizer. This is the same configuration found in the sink-eq6.conf
-- file that can be found in pipewire's source tree.
-----------------------------------------------------------------------------

-- For illustration purposes, we declare 'args' here as a native lua table,
-- we populate it in multiple steps and we transform it later to a json object
local args = {
  ["node.description"] = "Equalizer Sink",
  ["media.name"] = "Equalizer Sink",
  ["audio.channels"] = 2,
  ["audio.position"] = Json.Array { "FL", "FR" },
}

args["filter.graph"] = Json.Object {
  nodes = Json.Array {
    Json.Object {
        type  = "builtin",
        name  = "eq_band_1",
        label = "bq_lowshelf",
        control = Json.Object { Freq = 100.0, Q = 1.0, Gain = 0.0 },
    },
    Json.Object {
        type  = "builtin",
        name  = "eq_band_2",
        label = "bq_peaking",
        control = Json.Object { Freq = 100.0, Q = 1.0, Gain = 0.0 },
    },
    Json.Object {
        type  = "builtin",
        name  = "eq_band_3",
        label = "bq_peaking",
        control = Json.Object { Freq = 500.0, Q = 1.0, Gain = 0.0 },
    },
    Json.Object {
        type  = "builtin",
        name  = "eq_band_4",
        label = "bq_peaking",
        control = Json.Object { Freq = 2000.0, Q = 1.0, Gain = 0.0 },
    },
    Json.Object {
        type  = "builtin",
        name  = "eq_band_5",
        label = "bq_peaking",
        control = Json.Object { Freq = 5000.0, Q = 1.0, Gain = 0.0 },
    },
    Json.Object {
        type  = "builtin",
        name  = "eq_band_6",
        label = "bq_highshelf",
        control = Json.Object { Freq = 5000.0, Q = 1.0, Gain = 0.0 },
    },
  },
  links = Json.Array {
    Json.Object { output = "eq_band_1:Out", input = "eq_band_2:In" },
    Json.Object { output = "eq_band_2:Out", input = "eq_band_3:In" },
    Json.Object { output = "eq_band_3:Out", input = "eq_band_4:In" },
    Json.Object { output = "eq_band_4:Out", input = "eq_band_5:In" },
    Json.Object { output = "eq_band_5:Out", input = "eq_band_6:In" },
  },
}

args["capture.props"] = Json.Object {
  ["node.name"]   = "effect_input.eq6",
  ["media.class"] = "Audio/Sink",
}

args["playback.props"] = Json.Object {
  ["node.name"]   = "effect_output.eq6",
  ["node.passive"] = true,
}

-- Transform 'args' to a json object here
local args_json = Json.Object(args)

-- and get the final JSON as a string from the json object
local args_string = args_json:get_data()

local properties = {}

print("Loading module-filter-chain with arguments = ")
print(args_string)

filter_chain = LocalModule("libpipewire-module-filter-chain", args_string, properties)
