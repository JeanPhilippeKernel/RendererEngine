#pragma once
#include <Helpers/IntrusivePtr.h>
#include <future>

namespace ZEngine::Core
{
    struct SerializeInformation
    {
        /**
         * A boolean that describes whether the serialization was a success or not.
         */
        bool IsSuccess{true};

        /**
         * A log information that describes errors happened during the serialization.
         */
        std::string ErrorMessage;
    };

    struct ISerializer : public Helpers::RefCounted
    {
        ISerializer()          = default;
        virtual ~ISerializer() = default;

        virtual SerializeInformation Serialize(std::string_view filename)   = 0;
        virtual SerializeInformation Deserialize(std::string_view filename) = 0;
    };
} // namespace ZEngine::Core
