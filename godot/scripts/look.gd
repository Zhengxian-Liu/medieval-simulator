# Reads art/look.json, the single source of truth for every look parameter (each leaf is {v, why}).
# Look.v("sun.energy_day") -> value; Look.col("sun.color_day") -> Color; Look.apply(material, "materials.leaves")
# copies a parameter group onto a ShaderMaterial (arrays of 3 become colours).
extends RefCounted

const PATH := "res://art/look.json"
static var _data: Dictionary

static func _root() -> Dictionary:
	if _data.is_empty():
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(PATH))
		assert(parsed is Dictionary, "art/look.json failed to parse")
		_data = parsed
	return _data

static func v(path: String) -> Variant:
	var node: Variant = _root()
	for key in path.split("."):
		assert(node is Dictionary and node.has(key), "look.json has no '%s'" % path)
		node = node[key]
	assert(node is Dictionary and node.has("v"), "look.json '%s' is not a {v, why} leaf" % path)
	return node["v"]

static func col(path: String) -> Color:
	return to_color(v(path))

static func to_color(a: Variant) -> Color:
	return Color(a[0], a[1], a[2])

static func group(path: String) -> Dictionary:
	var out := {}
	var node: Dictionary = _root()
	for key in path.split("."):
		node = node[key]
	for key in node:
		if not key.begins_with("_"):
			out[key] = node[key]["v"]
	return out

# Shader parameters from a {name: value} dict; [r,g,b] arrays become colours.
static func apply(m: ShaderMaterial, params: Dictionary) -> void:
	for k in params:
		var val: Variant = params[k]
		if val is Array and val.size() == 3:
			val = to_color(val)
		if val is String:
			continue  # non-shader keys (e.g. texture override) are read by the caller
		m.set_shader_parameter(k, val)
