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
        m_pending_parent = None;
        Listings.Invalidate();
    }

    void FolderTreeCache::LoadChildren(IVFSContext& vfs, uint32_t parent)
    {
        if (!Count)
            Invalidate();
        PollChildren();
        if (parent >= Count || Nodes[parent].Loaded)
            return;
        if (m_pending_parent != None)
            return;
        Node& node = Nodes[parent];
        if (node.Depth >= VFS_MAX_COMPONENTS)
        {
            node.Loaded = true;
            return;
        }
        if (Count == Capacity)
        {
            Incomplete  = true;
            node.Loaded = true;
            return;
        }
        auto path = VFSPath::Parse(node.Path);
        if (path.Failed())
        {
            Incomplete  = true;
            node.Loaded = true;
            return;
        }
        m_pending_parent = parent;
        Listings.Request(vfs, path.Value(), true);
        PollChildren();
    }

    void FolderTreeCache::PollChildren()
    {
        const auto* result = Listings.Poll();
        if (m_pending_parent == None)
            return;
        if (!result && Listings.GetStatus() != DirectoryListing::Status::Failed)
            return;
        const uint32_t parent = m_pending_parent;
        m_pending_parent      = None;
        Node& node            = Nodes[parent];
        node.Loaded           = true; // Failures require explicit refresh, never a frame retry storm.
        if (result && result->Succeeded)
        {
            Incomplete |= result->Truncated;
            for (uint32_t i = 0; i < result->Count; ++i)
            {
                const auto&  entry  = result->Entries[i];
                const size_t length = ZEngine::Helpers::secure_strlen(entry.full_path);
                if (length >= sizeof(Node::Path) || Count == Capacity)
                {
                    Incomplete = true;
                    continue;
                }
                const uint32_t index = Count++;
                Node&          child = Nodes[index];
                child                = {};
                ZEngine::Helpers::secure_memcpy(child.Path, sizeof(child.Path), entry.full_path, length + 1);
                child.NameOffset  = (uint16_t) (length - ZEngine::Helpers::secure_strlen(entry.name));
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
    }

    void FolderTreeCache::UpdateSearch(IVFSContext& vfs, const char* query, uint32_t listing_budget)
    {
        if (!Count)
            Invalidate();
        PollChildren();
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
                    LoadChildren(vfs, m_scan);
                    ++listed;
                    if (!Nodes[m_scan].Loaded)
                        break; // A pending async request must not advance the cursor.
                }
                ++m_scan;
            }
        }
        else
            LoadChildren(vfs, 0);

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
