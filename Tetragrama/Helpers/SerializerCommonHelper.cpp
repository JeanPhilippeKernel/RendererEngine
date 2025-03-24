#include <pch.h>
#include <SerializerCommonHelper.h>
#include <ZEngine/Helpers/MemoryOperations.h>

using namespace ZEngine::Core::Container;

namespace Tetragrama::Helpers
{
    void SerializeStringData(std::ostream& os, ZEngine::Core::Container::StringView str)
    {
        size_t f_count = str.size();
        os.write(reinterpret_cast<const char*>(&f_count), sizeof(size_t));
        os.write(str.data(), f_count + 1);
    }

    void DeserializeStringData(ZEngine::Core::Memory::ArenaAllocator* Arena, std::istream& in, ZEngine::Core::Container::String& d)
    {
        size_t v_count;
        in.read(reinterpret_cast<char*>(&v_count), sizeof(size_t));

        d.init(Arena, v_count + 2);
        in.read(d.data(), v_count + 1);
    }

    void SerializeStringArrayData(std::ostream& os, ZEngine::Core::Container::ArrayView<ZEngine::Core::Container::String> str_view)
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

    void SerializeMapData(std::ostream& os, const std::unordered_map<uint32_t, uint32_t>& data)
    {
        std::vector<uint32_t> flat_data = {};
        flat_data.reserve(data.size() * 2);
        for (auto d : data)
        {
            flat_data.push_back(d.first);
            flat_data.push_back(d.second);
        }

        size_t data_count = flat_data.size();
        os.write(reinterpret_cast<const char*>(&data_count), sizeof(size_t));
        os.write(reinterpret_cast<const char*>(flat_data.data()), sizeof(uint32_t) * flat_data.size());
    }

    void DeserializeStringArrayData(ZEngine::Core::Memory::ArenaAllocator* Arena, std::istream& in, ZEngine::Core::Container::Array<ZEngine::Core::Container::String>& data)
    {
        size_t data_count;
        in.read(reinterpret_cast<char*>(&data_count), sizeof(size_t));
        data.init(Arena, data_count);

        for (int i = 0; i < data_count; ++i)
        {
            size_t v_count;
            in.read(reinterpret_cast<char*>(&v_count), sizeof(size_t));

            String v;
            v.init(Arena, v_count + 2);
            in.read(v.data(), v_count + 1);
            data.push(v);
        }
    }

    void DeserializeMapData(ZEngine::Core::Memory::ArenaAllocator* Arena, std::istream& in, std::unordered_map<uint32_t, uint32_t>& data)
    {
        size_t data_count;
        in.read(reinterpret_cast<char*>(&data_count), sizeof(size_t));

        std::vector<uint32_t> flat_data = {};
        flat_data.resize(data_count);
        in.read(reinterpret_cast<char*>(flat_data.data()), sizeof(uint32_t) * data_count);

        for (int i = 0; i < data_count; i += 2)
        {
            data[flat_data[i]] = flat_data[i + 1];
        }
    }
} // namespace Tetragrama::Helpers