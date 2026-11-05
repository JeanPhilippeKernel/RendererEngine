#include <ZEngine/Core/Serialization/YAMLAllocation.h>
#include <yaml.h>

namespace
{
    // This helper deliberately links yaml only, not zEngineLib, GTest, renderer,
    // logging, VFS, or allocator initialization. Exercise the public parser and
    // its routed failure path in a genuinely independent headless executable.
    struct DeniedRoute final : ZEngine::Core::Memory::IAllocationRoute
    {
        size_t Calls = 0;
        size_t Frees = 0;

        void*  Allocate(size_t) noexcept override
        {
            ++Calls;
            return nullptr;
        }
        void* Reallocate(void*, size_t, size_t) noexcept override
        {
            ++Calls;
            return nullptr;
        }
        void Free(void*) noexcept override
        {
            ++Frees;
        }
    };

    bool Parse()
    {
        yaml_parser_t parser = {};
        if (!yaml_parser_initialize(&parser))
            return false;
        constexpr unsigned char source[] = "root: [one, two, three]\n";
        yaml_parser_set_input_string(&parser, source, sizeof(source) - 1);
        bool complete = false;
        for (size_t i = 0; i < 32; ++i)
        {
            yaml_event_t event  = {};
            const bool   parsed = yaml_parser_parse(&parser, &event) != 0;
            complete            = parsed && event.type == YAML_STREAM_END_EVENT;
            yaml_event_delete(&event);
            if (!parsed || complete)
                break;
        }
        yaml_parser_delete(&parser);
        return complete;
    }
} // namespace

int main()
{
    if (!Parse())
        return 1;
    DeniedRoute route = {};
    {
        ZEngine::Core::Serialization::YAMLAllocationScope scope(route);
        if (Parse() || !route.Calls || route.Frees)
            return 2;
    }
    return Parse() ? 0 : 3;
}
