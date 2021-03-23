#include "mesh.hpp"
#include "state.hpp"
#include "pipeline.hpp"
#include "../core/utils.hpp"
#include "../core/state.hpp"
#include <tiny_obj_loader.h>

namespace rise::rendering {
    std::pair<std::vector<scenePipeline::Vertex>, std::vector<uint32_t>> loadMesh(
            tinyobj::attrib_t const &attrib, std::vector<tinyobj::shape_t> const &shapes) {
        std::vector<scenePipeline::Vertex> vertices;
        std::vector<uint32_t> indices;

        std::unordered_map<scenePipeline::Vertex, uint32_t> uniqueVertices{};

        for (const auto &shape : shapes) {
            for (const auto &idx : shape.mesh.indices) {
                scenePipeline::Vertex vertex{};

                vertex.pos.x = attrib.vertices[3 * idx.vertex_index + 0];
                vertex.pos.y = attrib.vertices[3 * idx.vertex_index + 1];
                vertex.pos.z = attrib.vertices[3 * idx.vertex_index + 2];
                vertex.normal.x = attrib.normals[3 * idx.normal_index + 0];
                vertex.normal.y = attrib.normals[3 * idx.normal_index + 1];
                vertex.normal.z = attrib.normals[3 * idx.normal_index + 2];
                vertex.texCoord.x = attrib.texcoords[2 * idx.texcoord_index + 0];
                vertex.texCoord.y = attrib.texcoords[2 * idx.texcoord_index + 1];

                vertices.push_back(vertex);

                if (uniqueVertices.count(vertex) == 0) {
                    uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                    vertices.push_back(vertex);
                }

                indices.push_back(uniqueVertices[vertex]);

            }
        }

        return {vertices, indices};
    }

    void regMesh(flecs::entity e) {
        if (!e.has<Path>()) e.set<Path>({});
        e.set<MeshRes>({});
    }

    void unregMesh(flecs::entity e) {
        auto &core = *e.get<RegTo>()->e.get<CoreState>();
        auto &mesh = *e.get_mut<MeshRes>();
        core.queue->WaitIdle();
        core.renderer->Release(*mesh.vertices);
        core.renderer->Release(*mesh.indices);
        mesh.vertices = nullptr;
        mesh.indices = nullptr;
    }

    void updateMesh(flecs::entity e, RegTo state, MeshRes &mesh, Path const &path) {
        auto &core = *state.e.get<CoreState>();
        auto &scene = *state.e.get<SceneState>();
        auto file = core.root + "/models/" + path.file;

        tinyobj::ObjReaderConfig readerConfig;
        readerConfig.triangulate = true;
        readerConfig.vertex_color = false;

        tinyobj::ObjReader reader;

        if (!reader.ParseFromFile(file, readerConfig)) {
            if (!reader.Error().empty()) {
                std::string err = "TinyObjReader: " + reader.Error();
                std::cerr << err << std::endl;
                return;
            }
        }

        if (!reader.Warning().empty()) {
            std::cout << "TinyObjReader: " << reader.Warning();
        }

        auto[vertices, indices] = loadMesh(reader.GetAttrib(), reader.GetShapes());
        if (vertices.empty() || indices.empty()) {
            std::cout << "Loading mesh error: " << file << std::endl;
            return;
        }

        if (mesh.vertices && mesh.indices) {
            core.renderer->Release(*mesh.vertices);
            core.renderer->Release(*mesh.indices);
        }

        mesh.vertices = createVertexBuffer(core.renderer.get(), scene.format, vertices);
        mesh.indices = createIndexBuffer(core.renderer.get(), indices);
        mesh.numIndices = static_cast<uint32_t>(indices.size());
    }

    void importMesh(flecs::world &ecs) {
        ecs.system<>("regMesh", "Mesh").kind(flecs::OnAdd).each(regMesh);
        ecs.system<>("unregMesh", "Mesh").kind(flecs::OnRemove).each(unregMesh);
        ecs.system<const RegTo, MeshRes, const Path>("updateMesh", "Mesh").
                kind(flecs::OnSet).each(updateMesh);
    }
}