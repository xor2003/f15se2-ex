"""Static glTF scene traversal in the file's coordinate system."""


IDENTITY = ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1))


def multiply(left, right):
    return tuple(tuple(sum(left[row][k] * right[k][column] for k in range(4))
                       for column in range(4)) for row in range(4))


def node_matrix(node):
    if "matrix" in node:
        values = node["matrix"]
        if len(values) != 16:
            raise ValueError("glTF node matrix must contain 16 values")
        # glTF serializes matrices column-first; calculations here use rows.
        return tuple(tuple(values[column * 4 + row] for column in range(4))
                     for row in range(4))
    x, y, z, w = node.get("rotation", (0, 0, 0, 1))
    rotation = (
        (1 - 2 * (y*y + z*z), 2 * (x*y - z*w), 2 * (x*z + y*w)),
        (2 * (x*y + z*w), 1 - 2 * (x*x + z*z), 2 * (y*z - x*w)),
        (2 * (x*z - y*w), 2 * (y*z + x*w), 1 - 2 * (x*x + y*y)),
    )
    scale = node.get("scale", (1, 1, 1))
    translation = node.get("translation", (0, 0, 0))
    return tuple(tuple(rotation[row][column] * scale[column] for column in range(3))
                 + (translation[row],) for row in range(3)) + ((0, 0, 0, 1),)


def transform_position(matrix, position):
    return tuple(sum(matrix[row][column] * position[column] for column in range(3))
                 + matrix[row][3] for row in range(3))


def mirrored(matrix):
    a, b, c = (row[:3] for row in matrix[:3])
    determinant = (a[0] * (b[1]*c[2] - b[2]*c[1])
                   - a[1] * (b[0]*c[2] - b[2]*c[0])
                   + a[2] * (b[0]*c[1] - b[1]*c[0]))
    return determinant < 0


def mesh_instances(document):
    meshes = document.get("meshes", [])
    nodes = document.get("nodes", [])
    if not nodes:
        # Older converter output can contain meshes without a scene graph.
        for mesh in meshes:
            yield mesh, IDENTITY
        return
    scenes = document.get("scenes", [])
    if scenes:
        roots = scenes[document.get("scene", 0)].get("nodes", [])
    else:
        children = {child for node in nodes for child in node.get("children", [])}
        roots = [index for index in range(len(nodes)) if index not in children]
        if not roots:
            raise ValueError("glTF node graph has no root")

    def visit(index, parent_matrix, ancestors):
        if index in ancestors:
            raise ValueError("cycle in glTF node graph")
        if not isinstance(index, int) or not 0 <= index < len(nodes):
            raise ValueError("invalid glTF node index")
        node = nodes[index]
        world_matrix = multiply(parent_matrix, node_matrix(node))
        if "mesh" in node:
            mesh_index = node["mesh"]
            if not isinstance(mesh_index, int) or not 0 <= mesh_index < len(meshes):
                raise ValueError("invalid glTF mesh index")
            yield meshes[mesh_index], world_matrix
        for child in node.get("children", []):
            yield from visit(child, world_matrix, ancestors | {index})

    for root in roots:
        yield from visit(root, IDENTITY, set())
