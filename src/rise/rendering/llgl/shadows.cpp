#include "shadows.hpp"
#include "utils.hpp"
#include "../glm.hpp"

namespace rise::rendering {

    void regPointLight(flecs::entity e) {
        if (!e.has<Position3D>()) e.set<Position3D>({0.0f, 0.0f, 0.0f});
        if (!e.has<DiffuseColor>()) e.set<DiffuseColor>({1.0f, 1.0f, 1.0f});
        if (!e.has<Intensity>()) e.set<Intensity>({1.0f});
        if (!e.has<Distance>()) e.set<Distance>({15.f});
        e.set<LightId>({});
    }

    void initShadowModels(flecs::entity, ApplicationId app) {
        auto &shadows = app.id->shadows;
        auto &manager = app.id->manager;

        for (auto model : manager.light.toInitShadowModels) {
            for (auto &&light : manager.light.states) {
                auto &shadowModel = std::get<eLightShadowModels>(light).get();
                auto &lightState = std::get<eLightState>(light).get();
                LLGL::ResourceHeapDescriptor resourceHeapDesc;

                resourceHeapDesc.pipelineLayout = app.id->shadows.layout;
                resourceHeapDesc.resourceViews.emplace_back(
                        std::get<eModelState>(manager.model.states.at(model.id)).get().uniform);
                resourceHeapDesc.resourceViews.emplace_back(lightState.matrices);
                resourceHeapDesc.resourceViews.emplace_back(lightState.parameters);
                shadowModel.at(model.id).heap =
                        app.id->core.renderer->CreateResourceHeap(resourceHeapDesc);
            }
        }
    }

    void removeShadowModels(flecs::entity, ApplicationId app) {
        auto &manager = app.id->manager;
        for (auto model : manager.light.toRemoveShadowModels) {
            for (auto &&light : manager.light.states) {
                auto &shadowModel = std::get<eLightShadowModels>(light).get();
                app.id->core.renderer->Release(*shadowModel.at(model.id).heap);
                shadowModel.erase(model.id);
            }
        }
    }

    void initPointLight(flecs::entity e, ApplicationRef app, ViewportRef viewport, LightId &id) {
        if (id.id == NullKey) {
            auto &core = app.ref->id->core;
            auto &shadows = app.ref->id->shadows;
            auto &light = app.ref->id->manager.light;

            auto &viewportModels = std::get<eViewportModels>(
                    app.ref->id->manager.viewport.states.at(viewport.ref->id)).get();

            std::map<Key, ShadowModel> shadowModels;
            for (auto model : viewportModels) {
                auto &pm = *flecs::entity(e.world(), model).get<ModelId>();
                shadowModels.emplace(pm.id, ShadowModel{});
                light.toInitShadowModels.push_back({pm});
            }

            std::tuple init{LightState{}, std::move(shadowModels)};
            id.id = app.ref->id->manager.light.states.push_back(std::move(init));
            e.add_trait<Initialized, LightId>();
            auto renderer = core.renderer.get();
            auto matrices = createUniformBuffer<shadowPipeline::PerLightMatrices>(renderer);
            auto parameters = createUniformBuffer<shadowPipeline::PerLightParameters>(renderer);
            light.toInit.emplace_back(LightState{matrices, parameters, 0}, id);
            light.toUpdate.push_back(e);


        } else {
            app.ref->id->manager.light.toRemove.push_back(id);
        }
    }

    void removePointLight(flecs::entity, ApplicationRef app, LightId id, ViewportRef viewport) {
        auto &manager = app.ref->id->manager;
        std::get<eViewportUpdated>(manager.viewport.states.at(viewport.ref->id)).get().light = true;
        app.ref->id->manager.light.toRemove.push_back(id);
    }

    void unregPointLight(flecs::entity e) {
        if (e.has_trait<Initialized, LightId>()) {
            e.remove<LightId>();
            e.remove_trait<Initialized, LightId>();
        }
    }

    void updateLightUniforms(flecs::entity, ApplicationId app) {
        auto &manager = app.id->manager;
        auto &core = app.id->core;

        for (auto eLight : manager.light.toUpdate) {
            auto lightId = eLight.get<LightId>();
            if (lightId->id != NullKey) {
                auto &lightState = std::get<eLightState>(
                        manager.light.states.at(lightId->id)).get();


                float aspect = (float) shadowPipeline::resolution.width /
                        (float) shadowPipeline::resolution.height;
                float near = 1.0f;
                float far = 25.0f;
                glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), aspect, near, far);

                auto position = getOrDefault(eLight, Position3D{0, 0, 0});

                auto lightPos = toGlm(*position);

                auto parameters = mapUniformBuffer<shadowPipeline::PerLightParameters>(
                        core.renderer.get(), lightState.parameters);

                parameters->lightPos = lightPos;
                parameters->farPlane = far;

                core.renderer->UnmapBuffer(*lightState.parameters);

                auto matrices = mapUniformBuffer<shadowPipeline::PerLightMatrices>(
                        core.renderer.get(), lightState.matrices);

                auto &transforms = matrices->lightSpaceMatrix;

                transforms[0] =
                        shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(1.0, 0.0, 0.0),
                                glm::vec3(0.0, -1.0, 0.0));
                transforms[1] =
                        shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(-1.0, 0.0, 0.0),
                                glm::vec3(0.0, -1.0, 0.0));
                transforms[2] =
                        shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0.0, 1.0, 0.0),
                                glm::vec3(0.0, 0.0, 1.0));
                transforms[3] =
                        shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0.0, -1.0, 0.0),
                                glm::vec3(0.0, 0.0, -1.0));
                transforms[4] =
                        shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0.0, 0.0, 1.0),
                                glm::vec3(0.0, -1.0, 0.0));
                transforms[5] =
                        shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0.0, 0.0, -1.0),
                                glm::vec3(0.0, -1.0, 0.0));

                core.renderer->UnmapBuffer(*lightState.matrices);
            }
        }
    }

    void updateShadowMaps(flecs::entity, ApplicationRef ref, ViewportRef viewportRef,
            LightId lightId) {
        auto &manager = ref.ref->id->manager;
        auto &&row = manager.viewport.states.at(viewportRef.ref->id);
        auto &updated = std::get<eViewportUpdated>(row).get();
        auto &viewport = std::get<eViewportState>(row).get();
        auto cmd = ref.ref->id->core.cmdBuf;

        auto &light = std::get<eLightState>(manager.light.states.at(lightId.id)).get();
        auto &shadowModels = std::get<eLightShadowModels>(
                manager.light.states.at(lightId.id)).get();

        cmd->BeginRenderPass(*viewport.cubeTarget[light.id].target);
        cmd->SetPipelineState(*viewport.cubeTarget[light.id].pipeline);
        cmd->Clear(LLGL::ClearFlags::Depth);
        cmd->SetViewport(LLGL::Viewport(shadowPipeline::resolution));

        for (auto p : shadowModels) {
            cmd->SetResourceHeap(*p.second.heap);
            auto &meshes = std::get<eModelMeshes>(manager.model.states.at(p.first)).get();
            for (auto &mesh : meshes) {
                auto &meshState = std::get<eMeshState>(manager.mesh.states.at(mesh)).get();
                cmd->SetVertexBuffer(*meshState.vertices);
                cmd->SetIndexBuffer(*meshState.indices);
                cmd->DrawIndexed(meshState.numIndices, 0);
            }
        }
        cmd->EndRenderPass();
    }

    void importShadowsState(flecs::world &ecs) {
        ecs.system<>("regPointLight", "PointLight").kind(flecs::OnAdd).each(regPointLight);
        ecs.system<>("unregPointLight", "PointLight").kind(flecs::OnRemove).each(unregPointLight);
        ecs.system<const ApplicationRef, const ViewportRef, LightId>("initLight", "PointLight").
                kind(flecs::OnSet).each(initPointLight);
        ecs.system<const ApplicationRef, const LightId, const ViewportRef>("removeLight").
                kind(EcsUnSet).each(removePointLight);
    }

    void initShadowsState(flecs::entity, ApplicationState &state, Path const &path) {
        auto const &core = state.core;
        auto const &root = path.file;
        auto &shadows = state.shadows;
        auto &presets = state.presets;

        shadows.format.AppendAttribute({"position", LLGL::Format::RGB32Float});
        shadows.format.SetStride(sizeof(float) * (3 + 3 + 2));

        shadows.layout = shadowPipeline::createLayout(core.renderer.get());
        shadows.program = createShaderProgram(core.renderer.get(),
                root + "/shaders/shadows", shadows.format);

    }
}