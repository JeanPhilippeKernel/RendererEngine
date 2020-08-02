#include <Z_Engine.h>

using namespace Z_Engine::Rendering::Renderer;
using namespace Z_Engine::Rendering::Cameras;
using namespace Z_Engine::Rendering::Shaders;
using namespace Z_Engine::Rendering::Buffer;
using namespace Z_Engine::Window;

namespace Sandbox {
	class Sandbox_One : public Z_Engine::Engine {
	public:
		Sandbox_One() 
			: Engine()
		{
			m_layer_stack.PushLayer(new Layers::Layer("1"));
			m_layer_stack.PushOverlayLayer(new Layers::Layer("400"));

			m_camera.reset(new OrthographicCamera(-1.0f, 1.0f, -1.0f, 1.0f));

			m_renderer.reset(new GraphicRenderer());

			const char* v_source = R"(
				#version 430 core

				layout (location = 0) in vec3 a_Position;
				layout (location = 1) in vec4 a_Color;

				uniform mat4 u_ViewProjectionMat;

				out vec4 v_Color;

				void main()
				{	
					gl_Position = u_ViewProjectionMat * vec4(a_Position, 1.0f);
					v_Color = a_Color;
				}
			)";

			const char* f_source = R"(
				#version 430 core

				in vec4 v_Color;

				void main()
				{
					gl_FragColor = v_Color;
				}
			)";

			m_shader.reset(new Shader(v_source, f_source));

			std::vector<float> vertices{
				0.5f, -0.5f, 1.0f, 1.0f, 0.5f, 0.3f, 1.0f,
			  -0.5f, -0.5f, 1.0f, 0.5f, 0.1f, 0.6f, 1.0f,
			   0.0f, 0.5f,	1.0f, 0.1f, 0.3f, 0.2f, 1.0f
			};

			std::vector<unsigned int> indices{ 0, 1, 2 };

			Layout::BufferLayout<float> layout{ Layout::ElementLayout<float>{3, "position"}, Layout::ElementLayout<float>{4, "color"} };
			m_vertex_buffer.reset(new VertexBuffer(vertices, layout));

			m_vertex_array.reset(new VertexArray<float, unsigned int>());
			m_index_buffer.reset(new IndexBuffer(indices));

			m_vertex_array->SetIndexBuffer(m_index_buffer);
			m_vertex_array->AddVertexBuffer(m_vertex_buffer);


			// Drawing second mesh

			std::vector<float> vertices_2{
				-0.5f, 0.7f, 1.0f,	1.1f, 0.2f, 0.2f, 1.0f,
				0.5f, 0.7f, 1.0f,	0.0f, 0.5f, 0.3f, 1.0f,
				0.5f, -0.6f, 1.0f,	0.5f, 0.9f, 0.6f, 1.0f,
				-0.5f, -0.6f, 1.0f, 0.3f, 0.2f, 0.2f, 1.0f
			};

			std::vector<unsigned int> indices_2{
				0, 1, 2,
				0, 2, 3
			};

			m_vertex_buffer_2.reset(new VertexBuffer(vertices_2, layout));

			m_vertex_array_2.reset(new VertexArray<float, unsigned int>());
			m_index_buffer_2.reset(new IndexBuffer(indices_2));

			m_vertex_array_2->SetIndexBuffer(m_index_buffer_2);
			m_vertex_array_2->AddVertexBuffer(m_vertex_buffer_2);

		}
		
		~Sandbox_One() = default;


		void Update(Core::TimeStep delta_time) override {
			
			if(Inputs::IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_LEFT)){
				auto pos = m_camera->GetPosition();
				pos.x += 0.01f;
				m_camera->SetPosition(pos);
			}
			
			if(Inputs::IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_RIGHT)){
				auto pos = m_camera->GetPosition();
				pos.x -= 0.01f;
				m_camera->SetPosition(pos);
			}

			if (Inputs::IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_UP)) {
				auto pos = m_camera->GetPosition();
				pos.y -= 0.01f;
				m_camera->SetPosition(pos);
			}

			if (Inputs::IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_DOWN)) {
				auto pos = m_camera->GetPosition();
				pos.y += 0.01f;
				m_camera->SetPosition(pos);
			}

			Engine::Update(delta_time);
		}

		bool OnEvent(Event::CoreEvent& e) override {

			return Engine::OnEvent(e);
		}

		void Render() override {
		
			RendererCommand::SetClearColor({ 0.2f, 0.2f, 0.2f, 1.0f });
			RendererCommand::Clear();


			m_renderer->BeginScene(m_camera);

			m_shader->SetUniform("u_ViewProjectionMat", m_camera->GetViewProjectionMatrix());
			m_renderer->Submit(m_shader, { m_vertex_array_2, m_vertex_array });

			m_renderer->EndScene();

			Engine::Render();
		}

	private:

		std::shared_ptr<Z_Engine::Rendering::Buffer::VertexBuffer<float>> m_vertex_buffer;
		std::shared_ptr<Z_Engine::Rendering::Buffer::IndexBuffer<unsigned int>> m_index_buffer;
		std::shared_ptr<Z_Engine::Rendering::Buffer::VertexArray<float, unsigned int>> m_vertex_array;

		std::shared_ptr<Z_Engine::Rendering::Buffer::VertexBuffer<float>> m_vertex_buffer_2;
		std::shared_ptr<Z_Engine::Rendering::Buffer::IndexBuffer<unsigned int>> m_index_buffer_2;
		std::shared_ptr<Z_Engine::Rendering::Buffer::VertexArray<float, unsigned int>> m_vertex_array_2;

		std::shared_ptr<Z_Engine::Rendering::Shaders::Shader> m_shader;

		std::shared_ptr<Z_Engine::Rendering::Renderer::GraphicRenderer> m_renderer;
		std::shared_ptr<Z_Engine::Rendering::Cameras::Camera> m_camera;


	};

}

Z_Engine::Engine* Z_Engine::CreateEngine() {
	return new Sandbox::Sandbox_One();
}