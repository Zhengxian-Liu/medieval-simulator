"""Per-asset bakes into one vertex colour layer (exported as COLOR_0), read by shaders/weathered.gdshader:
  R = Cycles ambient occlusion (crevices, under eaves, contact with a temporary ground plane)
  G = convexity (worn, lighter edges)
Vertex colours need vertex resolution: a large flat face with few vertices bakes one value across its whole area
(the 3-vertex gable once baked solid black) - subdivide such faces before baking.
"""
import bpy


def bake_ao(obj, samples=64):
    """Cycles ambient occlusion baked into a vertex colour layer ("AO", exported as COLOR_0): soft, path-traced
    darkening in every crevice, under eaves and where the asset meets the ground (a temporary ground plane occludes).
    The game multiplies it in (shaders/weathered.gdshader), so it survives the real-time day/night cycle."""
    bpy.ops.mesh.primitive_plane_add(size=60, location=(0, 0, 0))
    ground = bpy.context.active_object
    ca = obj.data.color_attributes.new("AO", "BYTE_COLOR", "CORNER")
    obj.data.color_attributes.active_color = ca
    obj.data.color_attributes.render_color_index = obj.data.color_attributes.active_color_index
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = samples
    scene.world = scene.world or bpy.data.worlds.new("w")
    scene.world.light_settings.distance = 1.5  # AO reach in metres: contact/crevice scale, not whole-room darkening
    for o in bpy.context.scene.objects:
        o.select_set(o == obj)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.bake(type="AO", target="VERTEX_COLORS")
    bpy.data.objects.remove(ground)
    bake_edges(obj, ca)


def bake_edges(obj, ca, reach=0.04):
    """Convexity into the green channel of the same colour layer: 1 on outward edges and corners (where timber and
    stone get worn and catch light), 0 on flats and in hollows. The shader lightens and roughens those edges."""
    me = obj.data
    me.calc_loop_triangles()
    convex = [0.0] * len(me.vertices)
    count = [0] * len(me.vertices)
    for e in me.edges:
        a, b = e.vertices
        va, vb = me.vertices[a], me.vertices[b]
        d = vb.co - va.co
        if d.length < 1e-6:
            continue
        # neighbour below the vertex's normal plane => the vertex sits on a convex ridge
        convex[a] += max(0.0, -d.normalized().dot(va.normal))
        convex[b] += max(0.0, d.normalized().dot(vb.normal))
        count[a] += 1
        count[b] += 1
    val = [min(1.0, (c / n) * 2.5) if n else 0.0 for c, n in zip(convex, count)]
    data = ca.data
    for loop in me.loops:
        r, g, bb, a = data[loop.index].color
        data[loop.index].color = (r, val[loop.vertex_index], bb, a)
