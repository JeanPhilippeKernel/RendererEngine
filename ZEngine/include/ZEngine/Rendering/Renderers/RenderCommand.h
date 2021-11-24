#pragma once
#include <glad/include/glad/glad.h>
#include <Maths/Math.h>

#include <Rendering/Shaders/Shader.h>
#include <Rendering/Buffers/VertexArray.h>
#include <ZEngineDef.h>

namespace ZEngine::Rendering::Renderers {

    class RendererCommand {
    public:
        RendererCommand()                       = delete;
        RendererCommand(const RendererCommand&) = delete;
        ~RendererCommand()                      = delete;

        static void SetClearColor(const Maths::Vector4& color) {
            glClearColor(color.r, color.g, color.b, color.a);
        }

        static void SetViewport(int x, int y, int width, int height) {
            glViewport(x, y, width, height);
        }

        static void Clear() {
            glClear(GL_COLOR_BUFFER_BIT);
        }

        static void Clear(unsigned int graphic_card_bit) {
            glClear(graphic_card_bit);
        }

        template <typename T, typename K>
        static void DrawIndexed(const Ref<Buffers::VertexArray<T, K>>& vertex_array) {
            vertex_array->Bind();
            glDrawElements(GL_TRIANGLES, vertex_array->GetIndexBuffer()->GetDataSize(), GL_UNSIGNED_INT, 0);
        }

        template <typename T, typename K>
        static void DrawIndexed(const Ref<Shaders::Shader>& shader, const Ref<Buffers::VertexArray<T, K>>& vertex_array) {
            shader->Bind();
            vertex_array->Bind();
            glDrawElements(GL_TRIANGLES, vertex_array->GetIndexBuffer()->GetDataSize(), GL_UNSIGNED_INT, 0);
        }

        template <typename T, typename K>
        static void DrawIndexed(const Ref<Shaders::Shader>& shader, const std::vector<Ref<Buffers::VertexArray<T, K>>>& vertex_array_list) {
            shader->Bind();
            for (const auto& vertex_array : vertex_array_list) {
                vertex_array->Bind();
                glDrawElements(GL_TRIANGLES, vertex_array->GetIndexBuffer()->GetDataSize(), GL_UNSIGNED_INT, 0);
            }
        }
    };
} // namespace ZEngine::Rendering::Renderers
