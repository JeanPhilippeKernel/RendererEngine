#include <Tetragrama/Panels/FolderTreeCache.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <cctype>
#include <cstdio>
#include <cstring>

using namespace ZEngine::Core::VFS;

namespace Tetragrama::Panels
{
    namespace
    {
        bool ContainsCI(const char* text, const char* query)
        {
            for (; *text; ++text)
            {
                const char* t = text;
                const char* q = query;
                while (*t && *q && std::tolower((unsigned char) *t) == std::tolower((unsigned char) *q))
                {
                    ++t;
                    ++q;
                }
                if (!*q)
                    return true;
            }
            return !*query;
        }
    } // namespace

    void FolderTreeCache::Invalidate()
    {
        // No multi-megabyte clear: only live nodes are initialized as they are appended.
        Count            = 1;
        Nodes[0]         = {};
        Nodes[0].Path[0] = '/';
        m_scan           = 0;
        m_filtered_count = None;
        Incomplete       = false;
    }

    void FolderTreeCache::LoadChildren(IVFSContext& vfs, ZEngine::Core::Memory::ArenaAllocator& scratch, uint32_t parent)
    {
        if (!Count)
            Invalidate();
        if (parent >= Count || Nodes[parent].Loaded)
            return;
        Node& node  = Nodes[parent];
        node.Loaded = true;
        if (node.Depth >= VFS_MAX_COMPONENTS)
            return;
        if (Count == Capacity)
        {
            Incomplete = true;
            return;
        }
        auto path = VFSPath::Parse(node.Path);
        if (path.Failed())
        {
            Incomplete = true;
            return;
        }
        auto temp    = ZGetScratch(&scratch);
        auto entries = vfs.List(path.Value(), &scratch);
        if (entries.Succeeded())
        {
            for (const auto& entry : entries.Value())
            {
                const auto name = entry.Path.Filename();
                if (!entry.IsDirectory || name.Empty() || name.Data[0] == '.' || entry.Path.Parent() != path.Value())
                    continue;
                if (entry.Path.Length() >= sizeof(Node::Path) || Count == Capacity)
                {
                    Incomplete = true;
                    continue;
                }
                const uint32_t index = Count++;
                Node&          child = Nodes[index];
                child                = {};
                ZEngine::Helpers::secure_memcpy(child.Path, sizeof(child.Path), entry.Path.CStr(), entry.Path.Length() + 1);
                child.NameOffset  = (uint16_t) (entry.Path.Length() - name.Length);
                child.Parent      = parent;
                child.Depth       = node.Depth + 1;
                child.PrevSibling = node.LastChild;
                if (node.LastChild != None)
                    Nodes[node.LastChild].NextSibling = index;
                else
                    node.FirstChild = index;
                node.LastChild = index;
            }
        }
        else
            Incomplete = true;
        ZReleaseScratch(temp);
    }

    void FolderTreeCache::UpdateSearch(IVFSContext& vfs, ZEngine::Core::Memory::ArenaAllocator& scratch, const char* query, uint32_t listing_budget)
    {
        if (!Count)
            Invalidate();
        char next_query[sizeof(m_query)] = {};
        snprintf(next_query, sizeof(next_query), "%s", query ? query : "");
        const bool changed = ZEngine::Helpers::secure_strcmp(m_query, next_query) != 0;
        if (changed)
            ZEngine::Helpers::secure_memcpy(m_query, sizeof(m_query), next_query, sizeof(m_query));
        if (m_query[0])
        {
            uint32_t listed = 0;
            while (m_scan < Count)
            {
                // There is no room to index any additional children. Avoid listing
                // thousands of directories whose results would all be discarded.
                if (Count == Capacity)
                {
                    Incomplete = true;
                    m_scan     = Count;
                    break;
                }
                if (!Nodes[m_scan].Loaded)
                {
                    if (listed == listing_budget)
                        break;
                    LoadChildren(vfs, scratch, m_scan);
                    ++listed;
                }
                ++m_scan;
            }
        }
        else
            LoadChildren(vfs, scratch, 0);

        if (!changed && m_filtered_count == Count)
            return;
        for (uint32_t i = 0; i < Count; ++i)
            Nodes[i].Visible = !m_query[0] || ContainsCI(Nodes[i].Path + Nodes[i].NameOffset, m_query);
        // Every child is appended after its parent: a reverse pass retains all ancestors.
        for (uint32_t i = Count; i-- > 1;)
            if (Nodes[i].Visible)
                Nodes[Nodes[i].Parent].Visible = true;
        m_filtered_count = Count;
    }
} // namespace Tetragrama::Panels
