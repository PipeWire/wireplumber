Script.async_activation = true

local tags = {}

local function checkpoint(tag)
  Log.info("at " .. tag)
  table.insert(tags, tag)
end

local function check_results()
  local i = 0
  local function inc()
    i = i+1
    return i
  end

  assert(tags[inc()] == "simple-1")
  assert(tags[inc()] == "async-start")
  assert(tags[inc()] == "async-start-advance")
  assert(tags[inc()] == "async-step2")
  assert(tags[inc()] == "simple-2")
end

local common_interests = {
  Interest {
    type = "event",
    Constraint { "event.type", "=", "test-event" },
  },
}

AsyncEventHook {
  priority = 10,
  type = "on-event",
  interests = common_interests,
  steps = {
    start = {
      next = "step2",
      execute = function (event, transition)
        checkpoint("async-start")
        Core.idle_add(function ()
          checkpoint("async-start-advance")
          transition:advance()
          return false
        end)
      end,
    },
    step2 = {
      next = "none",
      execute = function (event, transition)
        checkpoint("async-step2")
        transition:advance()
      end,
    },
  },
}:register()

SimpleEventHook {
  priority = 15,
  type = "on-event",
  interests = common_interests,
  execute = function (event)
    checkpoint("simple-1")
  end
}:register()

SimpleEventHook {
  priority = 1,
  type = "after-events",
  interests = common_interests,
  execute = function (event)
    checkpoint("simple-2")
    check_results()
    Script:finish_activation()
  end
}:register()

EventDispatcher.push_event { type = "test-event", priority = 1 }
