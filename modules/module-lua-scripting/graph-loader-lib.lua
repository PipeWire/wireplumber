objects = {}

function static_object(object_desc)
  table.insert(objects, object_desc)
end

function createChild(child_id, object_desc)
  object_desc["child_id"] = child_id
  wp.create_object(object_desc)
end
