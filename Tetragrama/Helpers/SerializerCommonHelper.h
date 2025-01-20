#pragma once
#include <iostream>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Tetragrama::Helpers
{
    void SerializeStringData(std::ostream&, std::string_view);
    void DeserializeStringData(std::istream& in, std::string& data);
    void SerializeStringArrayData(std::ostream&, std::span<std::string>);
    void DeserializeStringArrayData(std::istream&, std::vector<std::string>&);
    void SerializeMapData(std::ostream&, const std::unordered_map<uint32_t, uint32_t>&);
    void DeserializeMapData(std::istream&, std::unordered_map<uint32_t, uint32_t>&);
} // namespace Tetragrama::Helpers