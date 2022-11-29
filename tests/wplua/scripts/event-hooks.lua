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

  assert(tags[inc()] == "simple-first")
  assert(tags[inc()] == "simple-1")
  assert(tags[inc()] == "async-start")
  assert(tags[inc()] == "async-start-advance")
  assert(tags[inc()] == "async-step2")
  assert(tags[inc()] == "simple-last")
end

local common_interests = {
  EventInterest {
    Constraint { "event.type", "=", "test-event" },
  },
}

AsyncEventHook {
  name = "test-async-hook",
  before = "test-last-hook" ,
  after = { "test-first-hook", "test-simple-hook" },
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
  name = "test-first-hook",
  before = { "test-simple-hook", "test-last-hook" },
  interests = common_interests,
  execute = function (event)
    checkpoint("simple-first")
  end
}:register()

SimpleEventHook {
  name = "test-simple-hook",
  after = { "test-first-hook" },
  before = {},
  interests = common_interests,
  execute = function (event)
    checkpoint("simple-1")
  end
}:register()

SimpleEventHook {
  name = "test-last-hook",
  interests = common_interests,
  execute = function (event)
    checkpoint("simple-last")
    check_results()
    Script:finish_activation()
  end
}:register()

EventDispatcher.push_event { type = "test-event", priority = 1 }
