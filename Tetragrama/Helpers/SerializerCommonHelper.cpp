#include <pch.h>
#include <SerializerCommonHelper.h>
#include <ZEngine/Helpers/MemoryOperations.h>

using namespace ZEngine::Core::Containers;

namespace Tetragrama::Helpers
{
    void SerializeStringData(std::ostream& os, ZEngine::Core::Containers::StringView str)
    {
        size_t f_count = str.size();
        os.write(reinterpret_cast<const char*>(&f_count), sizeof(size_t));
        os.write(str.data(), f_count + 1);
    }

    void DeserializeStringData(ZEngine::Core::Memory::ArenaAllocator* Arena, std::istream& in, ZEngine::Core::Containers::String& d)
    {
        size_t v_count;
        char   buf[DEFAULT_STR_BUFFER] = {0};
        in.read(reinterpret_cast<char*>(&v_count), sizeof(size_t));
        in.read(buf, v_count + 1);

        d.init(Arena, buf);
    }

    void SerializeStringArrayData(std::ostream& os, ZEngine::Core::Containers::ArrayView<ZEngine::Core::Containers::String> str_view)
    {
        size_t count = str_view.size();
        os.write(reinterpret_cast<const char*>(&count), sizeof(size_t));
        for (unsigned i = 0; i < count; ++i)
        {
            size_t f_count = str_view[i].size();
            os.write(reinterpret_cast<const char*>(&f_count), sizeof(size_t));
            os.write(str_view[i].data(), f_count + 1);
        }
    }

    void SerializeMapData(std::ostream& os, ZEngine::Core::Containers::HashMap<uint32_t, uint32_t>& data)
    {
        std::vector<uint32_t> flat_data = {};
        flat_data.reserve(data.size() * 2);

        auto view = data.view();
        for (auto d : view)
        {
            flat_data.push_back(d.first);
            flat_data.push_back(d.second);
        }

        size_t data_count = flat_data.size();
        os.write(reinterpret_cast<const char*>(&data_count), sizeof(size_t));
        os.write(reinterpret_cast<const char*>(flat_data.data()), sizeof(uint32_t) * flat_data.size());
    }

    void DeserializeStringArrayData(ZEngine::Core::Memory::ArenaAllocator* Arena, std::istream& in, ZEngine::Core::Containers::Array<ZEngine::Core::Containers::String>& data)
    {
        size_t data_count;
        in.read(reinterpret_cast<char*>(&data_count), sizeof(size_t));
        data.init(Arena, data_count);

        for (int i = 0; i < data_count; ++i)
        {
            size_t v_count;
            char   buf[DEFAULT_STR_BUFFER] = {0};
            in.read(reinterpret_cast<char*>(&v_count), sizeof(size_t));
            in.read(buf, v_count + 1);

            String v;
            v.init(Arena, buf);
            data.push(v);
        }
    }

    void DeserializeMapData(ZEngine::Core::Memory::ArenaAllocator* Arena, std::istream& in, ZEngine::Core::Containers::HashMap<uint32_t, uint32_t>& data)
    {
        size_t data_count;
        in.read(reinterpret_cast<char*>(&data_count), sizeof(size_t));
        data.init(Arena, data_count);

        uint32_t buf[DEFAULT_STR_BUFFER] = {0};
        in.read(reinterpret_cast<char*>(buf), sizeof(uint32_t) * data_count);

        for (int i = 0; i < data_count; i += 2)
        {
            data[buf[i]] = buf[i + 1];
        }
    }
} // namespace Tetragrama::Helpers