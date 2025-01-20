#include <pch.h>
#include <SerializerCommonHelper.h>

namespace Tetragrama::Helpers
{
    void SerializeStringData(std::ostream& os, std::string_view str)
    {
        size_t f_count = str.size();
        os.write(reinterpret_cast<const char*>(&f_count), sizeof(size_t));
        os.write(str.data(), f_count + 1);
    }

    void DeserializeStringData(std::istream& in, std::string& d)
    {
        size_t v_count;
        in.read(reinterpret_cast<char*>(&v_count), sizeof(size_t));

        d.resize(v_count);
        in.read(d.data(), v_count + 1);
    }

    void SerializeStringArrayData(std::ostream& os, std::span<std::string> str_view)
    {
        size_t count = str_view.size();
        os.write(reinterpret_cast<const char*>(&count), sizeof(size_t));
        for (std::string_view str : str_view)
        {
            size_t f_count = str.size();
            os.write(reinterpret_cast<const char*>(&f_count), sizeof(size_t));
            os.write(str.data(), f_count + 1);
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

    void DeserializeStringArrayData(std::istream& in, std::vector<std::string>& data)
    {
        size_t data_count;
        in.read(reinterpret_cast<char*>(&data_count), sizeof(size_t));

        for (int i = 0; i < data_count; ++i)
        {
            size_t v_count;
            in.read(reinterpret_cast<char*>(&v_count), sizeof(size_t));

            std::string v;
            v.resize(v_count);
            in.read(v.data(), v_count + 1);
            data.push_back(v);
        }
    }

    void DeserializeMapData(std::istream& in, std::unordered_map<uint32_t, uint32_t>& data)
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