#pragma once
#include <string>
#include <uuid.h>

namespace ZEngine::Rendering::Components {
    struct UUIComponent {
        UUIComponent() {
            std::random_device                                     rd;
            std::ranlux48_base                                     generator(rd());
            uuids::basic_uuid_random_generator<std::ranlux48_base> gen(&generator);
            Identifier = gen();
        }

        UUIComponent(std::string_view uuid_string) {
            Identifier = uuids::uuid::from_string(uuid_string).value();
        }

        UUIComponent(uuids::uuid uuid) {
            Identifier = uuid;
        }

        ~UUIComponent() = default;

        uuids::uuid Identifier;
    };
} // namespace ZEngine::Rendering::Components
