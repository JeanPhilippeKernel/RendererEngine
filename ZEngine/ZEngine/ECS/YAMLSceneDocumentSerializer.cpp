#include <ZEngine/Core/Maths/MathUtils.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/ECS/SceneComponentSchemaRegistry.h>
#include <ZEngine/ECS/YAMLSceneDocumentSerializer.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <yaml-cpp/yaml.h>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using ZEngine::Core::VFS::IVFSContext;
using ZEngine::Core::VFS::VFSError;
using ZEngine::Core::VFS::VFSOpenFlags;
using ZEngine::Core::VFS::VFSPath;
using ZEngine::Core::VFS::VFSResult;

namespace ZEngine::ECS
{
    namespace
    {
        constexpr uint32_t MAX_COMPONENT_KINDS = 256;

        void               Failf(SceneDiagnostics* diagnostics, cstring format, ...)
        {
            char    message[SceneDiagnostics::MAX_MESSAGE_LEN];
            va_list args;
            va_start(args, format);
            vsnprintf(message, sizeof(message), format, args);
            va_end(args);
            SceneDiagnosticError(diagnostics, message);
        }

        constexpr size_t NUMBER_TEXT_MAX = 32;

        bool             CParseUInt64(cstring text, uint64_t* out)
        {
            char* end                  = nullptr;
            errno                      = 0;
            const unsigned long long v = strtoull(text, &end, 10);
            if (errno == ERANGE || end == text || *end != '\0')
            {
                return false;
            }
            *out = static_cast<uint64_t>(v);
            return true;
        }

        bool CParseInt64(cstring text, int64_t* out)
        {
            char* end         = nullptr;
            errno             = 0;
            const long long v = strtoll(text, &end, 10);
            if (errno == ERANGE || end == text || *end != '\0')
            {
                return false;
            }
            *out = static_cast<int64_t>(v);
            return true;
        }

        bool CParseDouble(cstring text, double* out)
        {
            char* end = nullptr;
            *out      = strtod(text, &end);
            return end != text && *end == '\0';
        }

        bool CFormatShortest(double value, bool as_float, char* out, size_t out_len)
        {
            if (!Core::Maths::isfinite(value) || out_len < NUMBER_TEXT_MAX)
            {
                return false;
            }
            char      sci[NUMBER_TEXT_MAX];
            const int max_digits = as_float ? 9 : 17;
            int       digits     = 1;
            for (; digits <= max_digits; ++digits)
            {
                snprintf(sci, sizeof(sci), "%.*e", digits - 1, value);
                if (as_float ? strtof(sci, nullptr) == static_cast<float>(value) : strtod(sci, nullptr) == value)
                {
                    break;
                }
            }

            const int exponent = atoi(strchr(sci, 'e') + 1);
            if (exponent >= -4 && exponent < 16)
            {
                const int decimals = digits - 1 - exponent;
                snprintf(out, out_len, "%.*f", decimals > 0 ? decimals : 0, value);
            }
            else
            {
                snprintf(out, out_len, "%s", sci);
            }
            if (!strchr(out, '.') && !strchr(out, 'e'))
            {
                const size_t len = strlen(out);
                snprintf(out + len, out_len - len, ".0");
            }
            return true;
        }

        bool CFormatFloat(float value, char* out, size_t out_len)
        {
            return CFormatShortest(static_cast<double>(value), true, out, out_len);
        }

        bool CFormatDouble(double value, char* out, size_t out_len)
        {
            return CFormatShortest(value, false, out, out_len);
        }

        bool IsDigits(cstring text, size_t len)
        {
            if (len == 0)
            {
                return false;
            }
            for (size_t i = 0; i < len; ++i)
            {
                if (text[i] < '0' || text[i] > '9')
                {
                    return false;
                }
            }
            return true;
        }

        bool IsBoolText(cstring text, bool* out)
        {
            if (!Helpers::secure_strcmp(text, "true") || !Helpers::secure_strcmp(text, "True") || !Helpers::secure_strcmp(text, "TRUE"))
            {
                *out = true;
                return true;
            }
            if (!Helpers::secure_strcmp(text, "false") || !Helpers::secure_strcmp(text, "False") || !Helpers::secure_strcmp(text, "FALSE"))
            {
                *out = false;
                return true;
            }
            return false;
        }

        bool IsNullText(cstring text)
        {
            return text[0] == '\0' || !Helpers::secure_strcmp(text, "~") || !Helpers::secure_strcmp(text, "null") || !Helpers::secure_strcmp(text, "Null") || !Helpers::secure_strcmp(text, "NULL");
        }

        bool IsSpecialFloatText(cstring text)
        {
            cstring body = (text[0] == '+' || text[0] == '-') ? text + 1 : text;
            for (cstring s : {".inf", ".Inf", ".INF"})
            {
                if (!Helpers::secure_strcmp(body, s))
                {
                    return true;
                }
            }
            for (cstring s : {".nan", ".NaN", ".NAN"})
            {
                if (!Helpers::secure_strcmp(text, s))
                {
                    return true;
                }
            }
            return false;
        }

        bool IsFloatText(cstring text)
        {
            size_t i               = (text[0] == '+' || text[0] == '-') ? 1 : 0;
            size_t mantissa_digits = 0;
            while (text[i] >= '0' && text[i] <= '9')
            {
                ++i;
                ++mantissa_digits;
            }
            bool has_point = false;
            if (text[i] == '.')
            {
                has_point = true;
                ++i;
                while (text[i] >= '0' && text[i] <= '9')
                {
                    ++i;
                    ++mantissa_digits;
                }
            }
            if (mantissa_digits == 0)
            {
                return false;
            }
            bool has_exponent = false;
            if (text[i] == 'e' || text[i] == 'E')
            {
                has_exponent = true;
                ++i;
                if (text[i] == '+' || text[i] == '-')
                {
                    ++i;
                }
                const size_t start = i;
                while (text[i] >= '0' && text[i] <= '9')
                {
                    ++i;
                }
                if (i == start)
                {
                    return false;
                }
            }
            return text[i] == '\0' && (has_point || has_exponent);
        }

        bool IsIntText(cstring text)
        {
            const size_t len  = Helpers::secure_strlen(text);
            const size_t sign = (text[0] == '+' || text[0] == '-') ? 1 : 0;
            return IsDigits(text + sign, len - sign);
        }

        bool NeedsQuotes(cstring text)
        {
            bool unused = false;
            return IsNullText(text) || IsBoolText(text, &unused) || IsIntText(text) || IsFloatText(text) || IsSpecialFloatText(text);
        }

        struct ReadContext
        {
            const SceneSerializationLimits& Limits;
            SceneValueWriter&               Writer;
            SceneDiagnostics*               Diagnostics;
            uint64_t                        NodeBudget;
        };

        int Line(const YAML::Node& node)
        {
            return node.Mark().is_null() ? 0 : node.Mark().line + 1;
        }

        cstring PlainText(const YAML::Node& node)
        {
            return node.IsScalar() && !Helpers::secure_strcmp(node.Tag().c_str(), "?") ? node.Scalar().c_str() : "";
        }

        bool ReadNumber(const YAML::Node& node, cstring what, float* out, SceneDiagnostics* diagnostics)
        {
            cstring text  = PlainText(node);
            double  value = 0.0;
            if ((!IsFloatText(text) && !IsIntText(text)) || !CParseDouble(text, &value))
            {
                Failf(diagnostics, "yaml line %d: '%s' must be a number", Line(node), what);
                return false;
            }
            *out = static_cast<float>(value);
            return true;
        }

        SceneValue* ConvertScalar(ReadContext& ctx, const YAML::Node& node)
        {
            cstring      text = node.Scalar().c_str();
            const size_t size = node.Scalar().size();
            if (Helpers::secure_strlen(text) != size)
            {
                Failf(ctx.Diagnostics, "yaml line %d: string contains a NUL byte", Line(node));
                return nullptr;
            }
            if (size > ctx.Limits.MaxStringBytes)
            {
                Failf(ctx.Diagnostics, "yaml line %d: string exceeds %u bytes", Line(node), ctx.Limits.MaxStringBytes);
                return nullptr;
            }

            cstring tag = node.Tag().c_str();
            if (!Helpers::secure_strcmp(tag, "!") || !Helpers::secure_strcmp(tag, "tag:yaml.org,2002:str"))
            {
                return ctx.Writer.NewString(text);
            }
            if (Helpers::secure_strcmp(tag, "?") != 0)
            {
                Failf(ctx.Diagnostics, "yaml line %d: explicit tag '%s' is not supported", Line(node), tag);
                return nullptr;
            }

            bool boolean = false;
            if (IsBoolText(text, &boolean))
            {
                return ctx.Writer.NewBool(boolean);
            }
            if (IsIntText(text))
            {
                int64_t  signed_value   = 0;
                uint64_t unsigned_value = 0;
                if (text[0] == '-' ? !CParseInt64(text, &signed_value) : !CParseUInt64(text, &unsigned_value))
                {
                    Failf(ctx.Diagnostics, "yaml line %d: integer out of range", Line(node));
                    return nullptr;
                }
                return text[0] == '-' ? ctx.Writer.NewSInt(signed_value) : ctx.Writer.NewUInt(unsigned_value);
            }
            if (IsFloatText(text))
            {
                double v = 0.0;
                if (!CParseDouble(text, &v) || !Core::Maths::isfinite(v))
                {
                    Failf(ctx.Diagnostics, "yaml line %d: float is out of range", Line(node));
                    return nullptr;
                }
                return ctx.Writer.NewFloat(v);
            }
            if (IsSpecialFloatText(text))
            {
                Failf(ctx.Diagnostics, "yaml line %d: non-finite float '%s'", Line(node), text);
                return nullptr;
            }
            return ctx.Writer.NewString(text);
        }

        SceneValue* ConvertValue(ReadContext& ctx, const YAML::Node& node, uint32_t depth)
        {
            if (ctx.NodeBudget == 0)
            {
                Failf(ctx.Diagnostics, "yaml line %d: node count exceeds source size (alias expansion?)", Line(node));
                return nullptr;
            }
            --ctx.NodeBudget;

            if (depth > ctx.Limits.MaxNestingDepth)
            {
                Failf(ctx.Diagnostics, "yaml line %d: nesting depth exceeds %u", Line(node), ctx.Limits.MaxNestingDepth);
                return nullptr;
            }

            switch (node.Type())
            {
                case YAML::NodeType::Null:
                    return ctx.Writer.NewNull();
                case YAML::NodeType::Scalar:
                    return ConvertScalar(ctx, node);
                case YAML::NodeType::Sequence:
                {
                    if (node.size() > ctx.Limits.MaxCollectionEntries)
                    {
                        Failf(ctx.Diagnostics, "yaml line %d: sequence exceeds %u entries", Line(node), ctx.Limits.MaxCollectionEntries);
                        return nullptr;
                    }
                    SceneValue* sequence = ctx.Writer.NewSequence(static_cast<uint32_t>(node.size()));
                    for (const auto& item : node)
                    {
                        SceneValue* child = sequence ? ConvertValue(ctx, item, depth + 1) : nullptr;
                        if (!child || !ctx.Writer.Append(sequence, child))
                        {
                            return nullptr;
                        }
                    }
                    return sequence;
                }
                case YAML::NodeType::Map:
                {
                    if (node.size() > ctx.Limits.MaxCollectionEntries)
                    {
                        Failf(ctx.Diagnostics, "yaml line %d: map exceeds %u entries", Line(node), ctx.Limits.MaxCollectionEntries);
                        return nullptr;
                    }
                    SceneValue* map = ctx.Writer.NewMap(static_cast<uint32_t>(node.size()));
                    for (const auto& item : node)
                    {
                        if (!map)
                        {
                            return nullptr;
                        }
                        if (!item.first.IsScalar() || !SceneComponentSchemaRegistry::IsValidKey(item.first.Scalar().c_str()))
                        {
                            Failf(ctx.Diagnostics, "yaml line %d: map key must be a printable, separator-free scalar", Line(item.first));
                            return nullptr;
                        }
                        cstring key = item.first.Scalar().c_str();
                        if (map->Find(key))
                        {
                            Failf(ctx.Diagnostics, "yaml line %d: duplicate key '%s'", Line(item.first), key);
                            return nullptr;
                        }
                        SceneValue* child = ConvertValue(ctx, item.second, depth + 1);
                        if (!child || !ctx.Writer.Set(map, key, child))
                        {
                            return nullptr;
                        }
                    }
                    return map;
                }
                default:
                    break;
            }
            Failf(ctx.Diagnostics, "yaml line %d: unsupported node", Line(node));
            return nullptr;
        }

        bool ExpectMap(const YAML::Node& node, cstring what, SceneDiagnostics* diagnostics)
        {
            if (!node.IsMap())
            {
                Failf(diagnostics, "yaml line %d: '%s' must be a map", Line(node), what);
                return false;
            }
            return true;
        }

        bool RejectUnknownKeys(const YAML::Node& map, const cstring* known, uint32_t known_count, cstring what, SceneDiagnostics* diagnostics)
        {
            bool ok = true;
            for (const auto& item : map)
            {
                cstring key   = item.first.Scalar().c_str();
                bool    found = false;
                for (uint32_t i = 0; i < known_count && !found; ++i)
                {
                    found = !Helpers::secure_strcmp(key, known[i]);
                }
                if (!found)
                {
                    Failf(diagnostics, "yaml line %d: unknown %s field '%s'", Line(item.first), what, key);
                    ok = false;
                }
            }
            return ok;
        }

        bool ReadUInt(const YAML::Node& node, cstring what, uint64_t max, uint64_t* out, SceneDiagnostics* diagnostics)
        {
            cstring text = PlainText(node);
            if (text[0] == '-' || !IsIntText(text))
            {
                Failf(diagnostics, "yaml line %d: '%s' must be an unsigned integer", Line(node), what);
                return false;
            }
            uint64_t value = 0;
            if (!CParseUInt64(text, &value) || value > max)
            {
                Failf(diagnostics, "yaml line %d: '%s' is out of range", Line(node), what);
                return false;
            }
            *out = value;
            return true;
        }

        bool ReadFloats(const YAML::Node& node, cstring what, float* out, uint32_t count, SceneDiagnostics* diagnostics)
        {
            if (!node.IsSequence() || node.size() != count)
            {
                Failf(diagnostics, "yaml line %d: '%s' must be a sequence of %u numbers", Line(node), what, count);
                return false;
            }
            for (uint32_t i = 0; i < count; ++i)
            {
                if (!ReadNumber(node[i], what, &out[i], diagnostics))
                {
                    return false;
                }
            }
            return true;
        }

        bool ReadGrid(const YAML::Node& node, SceneGridSettings& grid, SceneDiagnostics* diagnostics)
        {
            static const cstring known[] = {"enabled", "origin", "orientation", "base_spacing", "major_line_every", "minor_color", "major_color", "u_axis_color", "v_axis_color", "fade_distance", "fade_strength", "line_width", "max_lod"};
            if (!ExpectMap(node, "grid", diagnostics) || !RejectUnknownKeys(node, known, sizeof(known) / sizeof(known[0]), "grid", diagnostics))
            {
                return false;
            }

            bool ok = true;
            if (const auto n = node["enabled"])
            {
                bool value = false;
                if (!IsBoolText(PlainText(n), &value))
                {
                    Failf(diagnostics, "yaml line %d: 'enabled' must be a bool", Line(n));
                    ok = false;
                }
                grid.Enabled = value;
            }

            auto read_vec = [&](cstring key, float* dst, uint32_t count) {
                if (const auto n = node[key])
                {
                    ok = ReadFloats(n, key, dst, count, diagnostics) && ok;
                }
            };
            float origin[3] = {grid.Origin.x, grid.Origin.y, grid.Origin.z};
            float rot[4]    = {grid.Orientation.x, grid.Orientation.y, grid.Orientation.z, grid.Orientation.w};
            read_vec("origin", origin, 3);
            read_vec("orientation", rot, 4);
            grid.Origin      = {origin[0], origin[1], origin[2]};
            grid.Orientation = Core::Maths::Quatf(rot[0], rot[1], rot[2], rot[3]);

            auto read_color  = [&](cstring key, Core::Maths::Vec4f& c) {
                float v[4] = {c.x, c.y, c.z, c.w};
                read_vec(key, v, 4);
                c = {v[0], v[1], v[2], v[3]};
            };
            read_color("minor_color", grid.MinorColor);
            read_color("major_color", grid.MajorColor);
            read_color("u_axis_color", grid.UAxisColor);
            read_color("v_axis_color", grid.VAxisColor);

            auto read_scalar = [&](cstring key, float& dst) {
                if (const auto n = node[key])
                {
                    ok = ReadNumber(n, key, &dst, diagnostics) && ok;
                }
            };
            read_scalar("base_spacing", grid.BaseSpacing);
            read_scalar("fade_distance", grid.FadeDistance);
            read_scalar("fade_strength", grid.FadeStrength);
            read_scalar("line_width", grid.LineWidth);

            uint64_t value = 0;
            if (const auto n = node["major_line_every"])
            {
                ok                  = ReadUInt(n, "major_line_every", UINT32_MAX, &value, diagnostics) && ok;
                grid.MajorLineEvery = static_cast<uint32_t>(value);
            }
            if (const auto n = node["max_lod"])
            {
                ok          = ReadUInt(n, "max_lod", UINT32_MAX, &value, diagnostics) && ok;
                grid.MaxLOD = static_cast<uint32_t>(value);
            }
            return CanonicalizeSceneGridSettings(grid, diagnostics) && ok;
        }

        bool ReadUUID(const YAML::Node& node, cstring what, uuids::uuid* out, SceneDiagnostics* diagnostics)
        {
            if (!node.IsScalar() || !SceneUUIDFromString(node.Scalar().c_str(), out))
            {
                Failf(diagnostics, "yaml line %d: '%s' must be a canonical UUID string", Line(node), what);
                return false;
            }
            return true;
        }

        bool ReadEntities(ReadContext& ctx, const YAML::Node& node, const YAML::Node& versions, SceneDocument* out_document)
        {
            if (!node.IsSequence())
            {
                Failf(ctx.Diagnostics, "yaml line %d: 'entities' must be a sequence", Line(node));
                return false;
            }
            if (node.size() > ctx.Limits.MaxEntities)
            {
                Failf(ctx.Diagnostics, "scene: %u entities exceed the limit of %u", static_cast<uint32_t>(node.size()), ctx.Limits.MaxEntities);
                return false;
            }

            static const cstring known[] = {"uuid", "components"};
            for (const auto& entity_node : node)
            {
                if (!ExpectMap(entity_node, "entity", ctx.Diagnostics) || !RejectUnknownKeys(entity_node, known, 2, "entity", ctx.Diagnostics))
                {
                    return false;
                }

                SceneEntityRecord record{};
                if (!ReadUUID(entity_node["uuid"], "uuid", &record.UUID, ctx.Diagnostics))
                {
                    return false;
                }

                const YAML::Node components = entity_node["components"];
                const uint32_t   count      = components ? static_cast<uint32_t>(components.size()) : 0;
                if (components && !ExpectMap(components, "components", ctx.Diagnostics))
                {
                    return false;
                }
                if (count > ctx.Limits.MaxComponentsPerEntity)
                {
                    Failf(ctx.Diagnostics, "yaml line %d: %u components exceed the limit of %u", Line(components), count, ctx.Limits.MaxComponentsPerEntity);
                    return false;
                }
                record.Components.init(ctx.Writer.Arena(), count ? count : 1);
                if (!record.Components.data())
                {
                    Failf(ctx.Diagnostics, "scene: document arena exhausted");
                    return false;
                }

                for (const auto& item : components ? components : YAML::Node(YAML::NodeType::Map))
                {
                    cstring key = item.first.IsScalar() ? item.first.Scalar().c_str() : "";
                    if (!SceneComponentSchemaRegistry::IsValidKey(key))
                    {
                        Failf(ctx.Diagnostics, "yaml line %d: invalid component key", Line(item.first));
                        return false;
                    }
                    if (!versions || !versions[key])
                    {
                        Failf(ctx.Diagnostics, "yaml line %d: component '%s' has no entry in component_versions", Line(item.first), key);
                        return false;
                    }
                    uint64_t number = 0;
                    if (!ReadUInt(versions[key], key, UINT32_MAX, &number, ctx.Diagnostics))
                    {
                        return false;
                    }

                    SceneComponentRecord component{};
                    component.SchemaKey = ctx.Writer.CopyString(key);
                    component.Version   = static_cast<uint32_t>(number);
                    component.Payload   = ConvertValue(ctx, item.second, 1);
                    if (!component.SchemaKey || !component.Payload)
                    {
                        return false;
                    }
                    record.Components.push(component);
                }
                out_document->Entities.push(std::move(record));
            }
            return true;
        }

        bool ReadDocument(ReadContext& ctx, const YAML::Node& root, SceneDocument* out_document)
        {
            static const cstring root_keys[]  = {"scene"};
            static const cstring scene_keys[] = {"schema_version", "compatible_features", "uuid", "name", "component_versions", "settings", "entities"};
            if (!ExpectMap(root, "document", ctx.Diagnostics) || !RejectUnknownKeys(root, root_keys, 1, "document", ctx.Diagnostics))
            {
                return false;
            }
            const YAML::Node scene = root["scene"];
            if (!scene || !ExpectMap(scene, "scene", ctx.Diagnostics))
            {
                Failf(ctx.Diagnostics, "document: missing 'scene' map");
                return false;
            }

            uint64_t version = 0;
            if (!scene["schema_version"] || !ReadUInt(scene["schema_version"], "schema_version", UINT32_MAX, &version, ctx.Diagnostics))
            {
                Failf(ctx.Diagnostics, "scene: missing or invalid 'schema_version'");
                return false;
            }
            if (version != SCENE_DOCUMENT_SCHEMA_VERSION)
            {
                Failf(ctx.Diagnostics, "scene: schema_version %u is not supported (expected %u)", static_cast<uint32_t>(version), SCENE_DOCUMENT_SCHEMA_VERSION);
                return false;
            }
            if (!RejectUnknownKeys(scene, scene_keys, sizeof(scene_keys) / sizeof(scene_keys[0]), "scene", ctx.Diagnostics))
            {
                return false;
            }

            const uint32_t entity_count = scene["entities"] && scene["entities"].IsSequence() ? static_cast<uint32_t>(scene["entities"].size()) : 0;
            out_document->Initialize(ctx.Writer.Arena(), entity_count ? entity_count : 1);
            if (!out_document->Entities.data())
            {
                Failf(ctx.Diagnostics, "scene: document arena exhausted");
                return false;
            }
            out_document->SchemaVersion = static_cast<uint32_t>(version);

            if (const auto features = scene["compatible_features"])
            {
                if (!ReadUInt(features, "compatible_features", UINT64_MAX, &out_document->CompatibleFeatures, ctx.Diagnostics))
                {
                    return false;
                }
            }
            if (!scene["uuid"] || !ReadUUID(scene["uuid"], "uuid", &out_document->UUID, ctx.Diagnostics))
            {
                Failf(ctx.Diagnostics, "scene: missing or invalid 'uuid'");
                return false;
            }

            const YAML::Node name = scene["name"];
            if (name)
            {
                if (!name.IsScalar() || name.Scalar().size() > ctx.Limits.MaxStringBytes || Helpers::secure_strlen(name.Scalar().c_str()) != name.Scalar().size())
                {
                    Failf(ctx.Diagnostics, "yaml line %d: 'name' must be a string within limits", Line(name));
                    return false;
                }
                out_document->Name.init(ctx.Writer.Arena(), name.Scalar().c_str());
            }

            const YAML::Node versions = scene["component_versions"];
            if (versions && !ExpectMap(versions, "component_versions", ctx.Diagnostics))
            {
                return false;
            }

            if (const auto settings = scene["settings"])
            {
                static const cstring settings_keys[] = {"grid"};
                if (!ExpectMap(settings, "settings", ctx.Diagnostics) || !RejectUnknownKeys(settings, settings_keys, 1, "settings", ctx.Diagnostics))
                {
                    return false;
                }
                if (const auto grid = settings["grid"])
                {
                    if (!ReadGrid(grid, out_document->Grid, ctx.Diagnostics))
                    {
                        return false;
                    }
                }
            }

            if (const auto entities = scene["entities"])
            {
                if (!ReadEntities(ctx, entities, versions, out_document))
                {
                    return false;
                }
            }
            return true;
        }

        struct VersionEntry
        {
            cstring  Key;
            uint32_t Version;
        };

        struct VersionTable
        {
            VersionEntry Entries[MAX_COMPONENT_KINDS] = {};
            uint32_t     Count                        = 0;

            bool         Add(cstring key, uint32_t version, SceneDiagnostics* diagnostics)
            {
                uint32_t at = 0;
                while (at < Count && Helpers::secure_strcmp(Entries[at].Key, key) < 0)
                {
                    ++at;
                }
                if (at < Count && Helpers::secure_strcmp(Entries[at].Key, key) == 0)
                {
                    if (Entries[at].Version != version)
                    {
                        Failf(diagnostics, "write: component '%s' appears with versions %u and %u", key, Entries[at].Version, version);
                        return false;
                    }
                    return true;
                }
                if (Count == MAX_COMPONENT_KINDS)
                {
                    Failf(diagnostics, "write: more than %u distinct component keys", MAX_COMPONENT_KINDS);
                    return false;
                }
                for (uint32_t i = Count; i > at; --i)
                {
                    Entries[i] = Entries[i - 1];
                }
                Entries[at] = {key, version};
                ++Count;
                return true;
            }
        };

        void EmitString(YAML::Emitter& out, cstring text)
        {
            if (NeedsQuotes(text))
            {
                out << YAML::DoubleQuoted << text;
            }
            else
            {
                out << text;
            }
        }

        void EmitFloat(YAML::Emitter& out, double value)
        {
            char       buffer[NUMBER_TEXT_MAX];
            const auto as_float = static_cast<float>(value);
            const bool ok       = static_cast<double>(as_float) == value ? CFormatFloat(as_float, buffer, sizeof(buffer)) : CFormatDouble(value, buffer, sizeof(buffer));
            ZENGINE_VALIDATE_ASSERT(ok, "EmitFloat: value must be finite (validated before emit)")
            (void) ok;
            out << buffer;
        }

        bool IsScalarKind(SceneValueKind kind)
        {
            return kind != SceneValueKind::Sequence && kind != SceneValueKind::Map;
        }

        void EmitValue(YAML::Emitter& out, const SceneValue& value)
        {
            switch (value.Kind)
            {
                case SceneValueKind::Null:
                    out << YAML::Null;
                    break;
                case SceneValueKind::Bool:
                    out << value.BoolValue;
                    break;
                case SceneValueKind::SInt:
                    out << static_cast<long long>(value.SIntValue);
                    break;
                case SceneValueKind::UInt:
                    out << static_cast<unsigned long long>(value.UIntValue);
                    break;
                case SceneValueKind::Float:
                    EmitFloat(out, value.FloatValue);
                    break;
                case SceneValueKind::String:
                    EmitString(out, value.StringValue);
                    break;
                case SceneValueKind::Sequence:
                {
                    bool flat = true;
                    for (uint32_t i = 0; i < value.Sequence.size() && flat; ++i)
                    {
                        flat = IsScalarKind(value.Sequence[i]->Kind);
                    }
                    out << (flat ? YAML::Flow : YAML::Block) << YAML::BeginSeq;
                    for (uint32_t i = 0; i < value.Sequence.size(); ++i)
                    {
                        EmitValue(out, *value.Sequence[i]);
                    }
                    out << YAML::EndSeq;
                    break;
                }
                case SceneValueKind::Map:
                {
                    out << (value.Members.empty() ? YAML::Flow : YAML::Block) << YAML::BeginMap;
                    for (uint32_t i = 0; i < value.Members.size(); ++i)
                    {
                        out << YAML::Key << value.Members[i].Key << YAML::Value;
                        EmitValue(out, *value.Members[i].Value);
                    }
                    out << YAML::EndMap;
                    break;
                }
            }
        }

        void EmitFloats(YAML::Emitter& out, cstring key, const float* values, uint32_t count)
        {
            out << YAML::Key << key << YAML::Value << YAML::Flow << YAML::BeginSeq;
            for (uint32_t i = 0; i < count; ++i)
            {
                EmitFloat(out, values[i]);
            }
            out << YAML::EndSeq;
        }

        void EmitGrid(YAML::Emitter& out, const SceneGridSettings& grid)
        {
            auto color = [&](cstring key, const Core::Maths::Vec4f& c) {
                const float v[4] = {c.x, c.y, c.z, c.w};
                EmitFloats(out, key, v, 4);
            };
            const float origin[3] = {grid.Origin.x, grid.Origin.y, grid.Origin.z};
            const float rot[4]    = {grid.Orientation.x, grid.Orientation.y, grid.Orientation.z, grid.Orientation.w};

            out << YAML::Key << "grid" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "enabled" << YAML::Value << grid.Enabled;
            EmitFloats(out, "origin", origin, 3);
            EmitFloats(out, "orientation", rot, 4);
            out << YAML::Key << "base_spacing" << YAML::Value;
            EmitFloat(out, grid.BaseSpacing);
            out << YAML::Key << "major_line_every" << YAML::Value << grid.MajorLineEvery;
            color("minor_color", grid.MinorColor);
            color("major_color", grid.MajorColor);
            color("u_axis_color", grid.UAxisColor);
            color("v_axis_color", grid.VAxisColor);
            out << YAML::Key << "fade_distance" << YAML::Value;
            EmitFloat(out, grid.FadeDistance);
            out << YAML::Key << "fade_strength" << YAML::Value;
            EmitFloat(out, grid.FadeStrength);
            out << YAML::Key << "line_width" << YAML::Value;
            EmitFloat(out, grid.LineWidth);
            out << YAML::Key << "max_lod" << YAML::Value << grid.MaxLOD;
            out << YAML::EndMap;
        }

        void EmitDocument(YAML::Emitter& out, const SceneDocument& document, const VersionTable& versions)
        {
            char uuid[SCENE_UUID_STRING_LEN + 1];

            out << YAML::BeginMap << YAML::Key << "scene" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "schema_version" << YAML::Value << document.SchemaVersion;
            if (document.CompatibleFeatures != 0)
            {
                out << YAML::Key << "compatible_features" << YAML::Value << static_cast<unsigned long long>(document.CompatibleFeatures);
            }
            SceneUUIDToString(document.UUID, uuid);
            out << YAML::Key << "uuid" << YAML::Value << uuid;
            out << YAML::Key << "name" << YAML::Value;
            EmitString(out, document.Name.c_str() ? document.Name.c_str() : "");

            if (versions.Count > 0)
            {
                out << YAML::Key << "component_versions" << YAML::Value << YAML::BeginMap;
                for (uint32_t i = 0; i < versions.Count; ++i)
                {
                    out << YAML::Key << versions.Entries[i].Key << YAML::Value << versions.Entries[i].Version;
                }
                out << YAML::EndMap;
            }

            out << YAML::Key << "settings" << YAML::Value << YAML::BeginMap;
            EmitGrid(out, document.Grid);
            out << YAML::EndMap;

            out << YAML::Key << "entities" << YAML::Value << (document.Entities.empty() ? YAML::Flow : YAML::Block) << YAML::BeginSeq;
            for (const auto& entity : document.Entities)
            {
                SceneUUIDToString(entity.UUID, uuid);
                out << YAML::BeginMap << YAML::Key << "uuid" << YAML::Value << uuid;
                out << YAML::Key << "components" << YAML::Value << (entity.Components.empty() ? YAML::Flow : YAML::Block) << YAML::BeginMap;
                for (const auto& component : entity.Components)
                {
                    out << YAML::Key << component.SchemaKey << YAML::Value;
                    EmitValue(out, *component.Payload);
                }
                out << YAML::EndMap << YAML::EndMap;
            }
            out << YAML::EndSeq;

            out << YAML::EndMap << YAML::EndMap;
        }
    } // namespace

    VFSResult<void> YAMLSceneDocumentSerializer::Read(IVFSContext& vfs, const VFSPath& source, const SceneSerializationLimits& limits, Core::Memory::ArenaAllocator* document_arena, SceneDocument* out_document, SceneDiagnostics* diagnostics) const
    {
        if (!document_arena || !out_document)
        {
            SceneDiagnosticError(diagnostics, "read: document arena and output must not be null");
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }

        auto stat = vfs.Stat(source);
        if (stat.Failed())
        {
            Failf(diagnostics, "read: cannot stat '%s'", source.CStr());
            return VFSResult<void>::Fail(stat.Error());
        }
        const uint64_t size = stat.Value().SizeBytes;
        if (stat.Value().IsDirectory || size > limits.MaxSourceBytes)
        {
            Failf(diagnostics, "read: '%s' is a directory or exceeds %llu bytes", source.CStr(), static_cast<unsigned long long>(limits.MaxSourceBytes));
            return VFSResult<void>::Fail(stat.Value().IsDirectory ? VFSError::NotAFile : VFSError::SizeLimitExceeded);
        }

        YAML::Node root;
        {
            auto  scratch = ZGetScratch(document_arena);
            char* buffer  = ZPushString(scratch.Arena, static_cast<size_t>(size) + 1);
            if (!buffer)
            {
                ZReleaseScratch(scratch);
                SceneDiagnosticError(diagnostics, "read: arena cannot hold the source file");
                return VFSResult<void>::Fail(VFSError::OutOfMemory);
            }

            auto open = vfs.Open(source, VFSOpenFlags::Read);
            if (open.Failed())
            {
                ZReleaseScratch(scratch);
                Failf(diagnostics, "read: cannot open '%s'", source.CStr());
                return VFSResult<void>::Fail(open.Error());
            }
            auto* file = open.Value();
            auto  read = file->ReadAll({reinterpret_cast<uint8_t*>(buffer), static_cast<size_t>(size)});
            vfs.Close(file);
            if (read.Failed() || read.Value() != size)
            {
                ZReleaseScratch(scratch);
                Failf(diagnostics, "read: short or failed read of '%s'", source.CStr());
                return VFSResult<void>::Fail(read.Failed() ? read.Error() : VFSError::IOError);
            }
            buffer[size] = '\0';
            if (Helpers::secure_strlen(buffer) != size)
            {
                ZReleaseScratch(scratch);
                SceneDiagnosticError(diagnostics, "read: source contains NUL bytes");
                return VFSResult<void>::Fail(VFSError::Corrupted);
            }

            try
            {
                root = YAML::Load(buffer);
            }
            catch (const YAML::Exception& e)
            {
                ZReleaseScratch(scratch);
                Failf(diagnostics, "yaml line %d: %s", e.mark.is_null() ? 0 : e.mark.line + 1, e.msg.c_str());
                return VFSResult<void>::Fail(VFSError::Corrupted);
            }
            ZReleaseScratch(scratch);
        }

        SceneValueWriter writer;
        writer.Initialize(document_arena);
        ReadContext ctx{limits, writer, diagnostics, size};

        bool        ok = false;
        try
        {
            ok = ReadDocument(ctx, root, out_document);
        }
        catch (const YAML::Exception& e)
        {
            Failf(diagnostics, "yaml line %d: %s", e.mark.is_null() ? 0 : e.mark.line + 1, e.msg.c_str());
            ok = false;
        }
        if (writer.Failed())
        {
            SceneDiagnosticError(diagnostics, "read: document arena exhausted");
            return VFSResult<void>::Fail(VFSError::OutOfMemory);
        }
        if (!ok)
        {
            return VFSResult<void>::Fail(VFSError::Corrupted);
        }

        SortSceneDocument(*out_document);
        if (!ValidateSceneDocument(*out_document, nullptr, limits, diagnostics))
        {
            return VFSResult<void>::Fail(VFSError::Corrupted);
        }
        return VFSResult<void>::Ok();
    }

    VFSResult<void> YAMLSceneDocumentSerializer::WriteAtomic(IVFSContext& vfs, const VFSPath& destination, const SceneDocument& document, SceneDiagnostics* diagnostics) const
    {
        if (!ValidateSceneDocument(document, nullptr, SceneSerializationLimits::Default(), diagnostics))
        {
            return VFSResult<void>::Fail(VFSError::Corrupted);
        }

        VersionTable versions;
        for (const auto& entity : document.Entities)
        {
            for (const auto& component : entity.Components)
            {
                if (!versions.Add(component.SchemaKey, component.Version, diagnostics))
                {
                    return VFSResult<void>::Fail(VFSError::Corrupted);
                }
            }
        }

        YAML::Emitter out;
        out.SetIndent(2);
        out.SetNullFormat(YAML::LowerNull);
        out.SetBoolFormat(YAML::TrueFalseBool);
        out.SetBoolFormat(YAML::LowerCase);
        EmitDocument(out, document, versions);
        out << YAML::Newline;
        if (!out.good())
        {
            Failf(diagnostics, "write: emitter error: %s", out.GetLastError().c_str());
            return VFSResult<void>::Fail(VFSError::IOError);
        }

        auto result = Core::VFS::WriteFileAtomically(vfs, destination, {reinterpret_cast<const uint8_t*>(out.c_str()), out.size()});
        if (result.Failed())
        {
            Failf(diagnostics, "write: atomic replace of '%s' failed; previous file kept", destination.CStr());
        }
        return result;
    }
} // namespace ZEngine::ECS
