#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/ECS/SceneYAML.h>
#include <ZEngine/ECS/SceneYAMLReader.h>
#include <yaml.h>

namespace ZEngine::ECS
{
    namespace
    {
        struct ParserLease
        {
            yaml_parser_t Parser      = {};
            bool          Initialized = false;
            ~ParserLease()
            {
                if (Initialized)
                    yaml_parser_delete(&Parser);
            }
        };

        struct EventLease
        {
            yaml_event_t Event = {};
            ~EventLease()
            {
                yaml_event_delete(&Event);
            }
        };

        class BoundedTree
        {
        public:
            explicit BoundedTree(SceneYAML::Storage& storage, const SceneDocumentLimits& limits) : m_storage(storage), m_limits(limits) {}

            SceneYAML::Node Root = {};

            bool            Add(const yaml_event_t& event)
            {
                switch (event.type)
                {
                    case YAML_SCALAR_EVENT:
                    {
                        const auto& scalar = event.data.scalar;
                        if (scalar.anchor || scalar.tag || scalar.length > m_limits.MaxScalarBytes)
                            return false;
                        const std::string_view value(reinterpret_cast<const char*>(scalar.value), scalar.length);
                        if (value.find('\0') != std::string_view::npos)
                            return false;
                        const bool is_null = scalar.style == YAML_PLAIN_SCALAR_STYLE && (value.empty() || value == "~" || value == "null" || value == "Null" || value == "NULL");
                        return Append(is_null ? m_storage.Make(SceneYAML::NodeType::Null) : m_storage.Scalar(value));
                    }
                    case YAML_SEQUENCE_START_EVENT:
                        return !event.data.sequence_start.anchor && !event.data.sequence_start.tag && Start(SceneYAML::NodeType::Sequence, event.data.sequence_start.style == YAML_FLOW_SEQUENCE_STYLE);
                    case YAML_MAPPING_START_EVENT:
                        return !event.data.mapping_start.anchor && !event.data.mapping_start.tag && Start(SceneYAML::NodeType::Map, event.data.mapping_start.style == YAML_FLOW_MAPPING_STYLE);
                    case YAML_SEQUENCE_END_EVENT:
                        return End(SceneYAML::NodeType::Sequence);
                    case YAML_MAPPING_END_EVENT:
                        return End(SceneYAML::NodeType::Map);
                    default:
                        return false; // In particular, aliases are not scene source features.
                }
            }

            bool Complete() const
            {
                return m_has_root && m_depth == 0;
            }

        private:
            struct Frame
            {
                SceneYAML::Node Node       = {};
                SceneYAML::Node PendingKey = {};
                uint32_t        Items      = 0;
            };
            SceneYAML::Storage&                     m_storage;
            const SceneDocumentLimits&              m_limits;
            Core::Containers::FixedArray<Frame, 32> m_stack    = {};
            uint32_t                                m_depth    = 0;
            uint32_t                                m_nodes    = 0;
            bool                                    m_has_root = false;

            bool                                    Append(const SceneYAML::Node& node)
            {
                if (!m_storage.Good() || !node.IsDefined() || ++m_nodes > m_limits.MaxNodes)
                    return false;
                if (m_depth == 0)
                {
                    if (m_has_root)
                        return false;
                    Root.reset(node);
                    m_has_root = true;
                    return true;
                }
                auto& parent = m_stack[m_depth - 1];
                if (parent.Node.IsSequence())
                {
                    if (++parent.Items > m_limits.MaxCollectionItems)
                        return false;
                    return m_storage.Append(parent.Node, node);
                }
                else if (!parent.PendingKey.IsDefined())
                {
                    if (!node.IsScalar() || ++parent.Items > m_limits.MaxCollectionItems)
                        return false;
                    parent.PendingKey = node;
                }
                else
                {
                    if (!m_storage.AppendField(parent.Node, parent.PendingKey, node))
                        return false;
                    parent.PendingKey = {};
                }
                return true;
            }

            bool Start(SceneYAML::NodeType::value type, bool flow)
            {
                if (m_depth >= m_limits.MaxDepth || (m_depth != 0 && m_stack[m_depth - 1].Node.IsMap() && !m_stack[m_depth - 1].PendingKey.IsDefined()))
                    return false;
                const auto node = m_storage.Make(type, flow ? SceneYAML::EmitterStyle::Flow : SceneYAML::EmitterStyle::Block);
                if (!Append(node))
                    return false;
                m_stack[m_depth++] = {.Node = node};
                return true;
            }

            bool End(SceneYAML::NodeType::value type)
            {
                if (m_depth == 0 || m_stack[m_depth - 1].Node.Type() != type || m_stack[m_depth - 1].PendingKey.IsDefined())
                    return false;
                if (type == SceneYAML::NodeType::Map && !m_storage.UniqueKeys(m_stack[m_depth - 1].Node))
                    return false;
                --m_depth;
                return true;
            }
        };
    } // namespace

    bool ReadSceneYAML(std::string_view source, const SceneDocumentLimits& limits, SceneYAML::Node* out, SceneDiagnostics* diagnostics)
    {
        if (!out || !out->Owner() || !out->Owner()->Good() || source.empty() || source.size() > limits.MaxFileBytes || limits.MaxDepth == 0 || limits.MaxDepth > 32)
        {
            SceneDiagnosticError(diagnostics, "Invalid YAML source or tree limits.");
            return false;
        }
        ParserLease parser = {};
        parser.Initialized = yaml_parser_initialize(&parser.Parser) != 0;
        if (!parser.Initialized)
        {
            SceneDiagnosticError(diagnostics, "Cannot allocate scene YAML parser.");
            return false;
        }
        yaml_parser_set_input_string(&parser.Parser, reinterpret_cast<const unsigned char*>(source.data()), source.size());
        auto& storage = *out->Owner();
        struct Transaction
        {
            SceneYAML::Storage&            Owner;
            SceneYAML::Storage::Checkpoint Checkpoint = {};
            bool                           Keep       = false;
            ~Transaction()
            {
                if (!Keep)
                    Owner.Rollback(Checkpoint);
            }
        } transaction{storage, storage.Mark()};
        BoundedTree tree(storage, limits);
        enum class Stage
        {
            StreamStart,
            DocumentStart,
            Body,
            StreamEnd
        };
        Stage stage = Stage::StreamStart;
        for (;;)
        {
            EventLease event = {};
            if (!yaml_parser_parse(&parser.Parser, &event.Event))
            {
                SceneDiagnosticError(diagnostics, parser.Parser.problem ? parser.Parser.problem : "Scene YAML parsing failed.");
                return false;
            }
            const auto type  = event.Event.type;
            bool       valid = false;
            switch (stage)
            {
                case Stage::StreamStart:
                    valid = type == YAML_STREAM_START_EVENT;
                    stage = Stage::DocumentStart;
                    break;
                case Stage::DocumentStart:
                    valid = type == YAML_DOCUMENT_START_EVENT && !event.Event.data.document_start.tag_directives.start;
                    stage = Stage::Body;
                    break;
                case Stage::Body:
                    if (type == YAML_DOCUMENT_END_EVENT)
                    {
                        valid = tree.Complete();
                        stage = Stage::StreamEnd;
                    }
                    else
                        valid = tree.Add(event.Event);
                    break;
                case Stage::StreamEnd:
                    if (type == YAML_STREAM_END_EVENT)
                    {
                        out->reset(tree.Root);
                        transaction.Keep = true;
                        return true;
                    }
                    break;
            }
            if (!valid)
            {
                SceneDiagnosticError(diagnostics, "Scene requires one bounded YAML document without duplicate keys, aliases, anchors or tags.");
                return false;
            }
        }
    }
} // namespace ZEngine::ECS
