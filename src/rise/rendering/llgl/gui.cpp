#include "gui.hpp"
#include "utils.hpp"
#include <backends/imgui_impl_sdl.h>

namespace rise::rendering {
    void configImGui() {
        ImGuiStyle &style = ImGui::GetStyle();
        style.Colors[ImGuiCol_TitleBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.6f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
        style.Colors[ImGuiCol_Header] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
    }

    void initGuiPipeline(CoreState const &core, GuiState &gui, std::string const& root) {
        gui.format.AppendAttribute(LLGL::VertexAttribute{"inPos", LLGL::Format::RG32Float});
        gui.format.AppendAttribute(LLGL::VertexAttribute{"inUV", LLGL::Format::RG32Float});
        gui.format.AppendAttribute(LLGL::VertexAttribute{"inColor", LLGL::Format::RGBA8UNorm});

        gui.layout = guiPipeline::createLayout(core.renderer.get());

        auto program = createShaderProgram(core.renderer.get(),
                root + "/shaders/gui", gui.format);
        gui.pipeline = guiPipeline::createPipeline(core.renderer.get(), gui.layout, program);
    }

    void initGuiState(flecs::entity e, ApplicationState &state, Path const& path) {
        auto const &root = path.file;
        auto& gui = state.gui;
        auto renderer = state.core.renderer.get();

        initGuiPipeline(state.core, gui, root);

        auto context = ImGui::CreateContext();
        ImGui::SetCurrentContext(context);
        e.set<GuiContext>(GuiContext{context});

        ImGui_ImplSDL2_InitForVulkan(state.platform.window);
        configImGui();

        unsigned char *fontData;
        int texWidth, texHeight;
        ImGuiIO &io = ImGui::GetIO();
        io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);

        auto fontTexture = createTextureFromData(renderer, LLGL::ImageFormat::RGBA,
                fontData, texWidth, texHeight);

        gui.uniform = createUniformBuffer(renderer, guiPipeline::Global{});

        LLGL::ResourceHeapDescriptor resourceHeapDesc;
        resourceHeapDesc.pipelineLayout = gui.layout;
        resourceHeapDesc.resourceViews.emplace_back(gui.uniform);
        resourceHeapDesc.resourceViews.emplace_back(state.core.sampler);
        resourceHeapDesc.resourceViews.emplace_back(fontTexture);
        gui.heap = renderer->CreateResourceHeap(resourceHeapDesc);
    }

    void regGuiState(flecs::entity) {

    }

    void importGuiState(flecs::world &ecs) {
        ecs.system<>("regGuiState", "Application").
                kind(flecs::OnAdd).each(regGuiState);
    }

    void updateResources(flecs::entity, ApplicationId app, GuiContext context) {
        auto& core = app.id->core;
        auto& gui = app.id->gui;

        ImGui::SetCurrentContext(context.context);
        ImGuiIO &io = ImGui::GetIO();
        ImDrawData *imDrawData = ImGui::GetDrawData();

        guiPipeline::Global parameters = {};
        parameters.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
        parameters.translate.x = -1.0f - imDrawData->DisplayPos.x * parameters.scale.x;
        parameters.translate.y = -1.0f - imDrawData->DisplayPos.y * parameters.scale.y;

        updateUniformBuffer(core.renderer.get(), gui.uniform, parameters);

        if ((imDrawData->TotalVtxCount == 0) || (imDrawData->TotalIdxCount == 0)) {
            return;
        }

        std::vector<ImDrawVert> vertices;
        std::vector<ImDrawIdx> indices;

        for (int n = 0; n < imDrawData->CmdListsCount; n++) {
            const ImDrawList *cmd_list = imDrawData->CmdLists[n];
            vertices.insert(vertices.end(), cmd_list->VtxBuffer.Data,
                    cmd_list->VtxBuffer.Data + cmd_list->VtxBuffer.Size);

            indices.insert(indices.end(), cmd_list->IdxBuffer.Data,
                    cmd_list->IdxBuffer.Data + cmd_list->IdxBuffer.Size);
        }

        auto &mesh = gui.mesh;
        auto renderer = core.renderer.get();

        if (mesh.numVertices != imDrawData->TotalVtxCount) {
            if (mesh.vertices) {
                renderer->Release(*mesh.vertices);
            }
            mesh.numVertices = imDrawData->TotalVtxCount;
            mesh.vertices = createVertexBuffer(renderer, gui.format, vertices);
        } else {
            renderer->WriteBuffer(*mesh.vertices, 0, vertices.data(),
                    vertices.size() * sizeof(ImDrawVert));
        }

        if (mesh.numIndices != imDrawData->TotalIdxCount) {
            if (mesh.indices) {
                renderer->Release(*mesh.indices);
            }
            mesh.numIndices = imDrawData->TotalIdxCount;
            mesh.indices = createIndexBuffer(renderer, indices);
        } else {
            renderer->WriteBuffer(*mesh.indices, 0, indices.data(),
                    indices.size() * sizeof(ImDrawIdx));
        }
    }

    void prepareImgui(flecs::entity, ApplicationId app, GuiContext context) {
        auto window = app.id->platform.window;
        ImGui::SetCurrentContext(context.context);

        int width, height;
        SDL_GetWindowSize(window, &width, &height);

        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

        ImGui_ImplSDL2_NewFrame(window);
        ImGui::NewFrame();
    }

    void processImGui(flecs::entity, GuiContext context) {
        ImGui::SetCurrentContext(context.context);
        ImGui::Render();
    }


    void renderGui(flecs::entity, ApplicationId app, GuiContext context, Extent2D size) {
        auto& core = app.id->core;
        auto& gui = app.id->gui;

        //core.cmdBuf->BeginRenderPass(*app.id->platform.context);

        ImGui::SetCurrentContext(context.context);

        LLGL::Viewport viewport;
        viewport.width = static_cast<float>(size.width);
        viewport.height = -static_cast<float>(size.height);
        viewport.x = 0;
        viewport.y = static_cast<float>(size.height);

        auto cmdBuf = core.cmdBuf;

        cmdBuf->SetViewport(viewport);

        cmdBuf->SetPipelineState(*gui.pipeline);
        cmdBuf->SetResourceHeap(*gui.heap);

        ImDrawData *imDrawData = ImGui::GetDrawData();
        float fb_width = (imDrawData->DisplaySize.x * imDrawData->FramebufferScale.x);
        float fb_height = (imDrawData->DisplaySize.y * imDrawData->FramebufferScale.y);
        ImVec2 clip_off = imDrawData->DisplayPos;
        ImVec2 clip_scale = imDrawData->FramebufferScale;
        int32_t vertexOffset = 0;
        int32_t indexOffset = 0;

        if (imDrawData->CmdListsCount > 0) {
            cmdBuf->SetVertexBuffer(*gui.mesh.vertices);
            cmdBuf->SetIndexBuffer(*gui.mesh.indices);

            for (int32_t i = 0; i < imDrawData->CmdListsCount; i++) {
                const ImDrawList *cmd_list = imDrawData->CmdLists[i];
                for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++) {
                    const ImDrawCmd *pCmd = &cmd_list->CmdBuffer[j];
                    ImVec4 clip_rect;
                    clip_rect.x = (pCmd->ClipRect.x - clip_off.x) * clip_scale.x;
                    clip_rect.y = (pCmd->ClipRect.y - clip_off.y) * clip_scale.y;
                    clip_rect.z = (pCmd->ClipRect.z - clip_off.x) * clip_scale.x;
                    clip_rect.w = (pCmd->ClipRect.w - clip_off.y) * clip_scale.y;

                    if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f &&
                            clip_rect.w >= 0.0f) {
                        LLGL::Scissor scissor;
                        scissor.x = std::max((int32_t) (clip_rect.x), 0);
                        scissor.y = std::max((int32_t) (clip_rect.y), 0);
                        scissor.width = static_cast<int32_t>(clip_rect.z - clip_rect.x);
                        scissor.height = static_cast<int32_t>(clip_rect.w - clip_rect.y);

                        cmdBuf->SetScissor(scissor);
                        cmdBuf->DrawIndexed(pCmd->ElemCount, indexOffset + pCmd->IdxOffset,
                                vertexOffset + static_cast<int32_t>(pCmd->VtxOffset));
                    }
                }
                vertexOffset += cmd_list->VtxBuffer.Size;
                indexOffset += cmd_list->IdxBuffer.Size;
            }
        }

        //core.cmdBuf->EndRenderPass();
    }
}
