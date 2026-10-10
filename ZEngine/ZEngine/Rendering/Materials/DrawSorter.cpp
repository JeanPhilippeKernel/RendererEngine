#include <ZEngine/Rendering/Materials/DrawSorter.h>
#include <cmath>

namespace ZEngine::Rendering::Materials
{
    namespace
    {
        int CompareDepth(float left, float right, bool descending)
        {
            const bool left_nan  = std::isnan(left);
            const bool right_nan = std::isnan(right);
            if (left_nan && right_nan)
                return 0;
            if (left_nan)
                return 1;
            if (right_nan)
                return -1;
            if (left == right)
                return 0;
            const bool left_before = descending ? left > right : left < right;
            return left_before ? -1 : 1;
        }

        bool IsOpaqueBefore(const MaterialDrawItem& left, const MaterialDrawItem& right)
        {
            if (left.PSOKeyHash != right.PSOKeyHash)
                return left.PSOKeyHash < right.PSOKeyHash;
            if (left.MaterialIndex != right.MaterialIndex)
                return left.MaterialIndex < right.MaterialIndex;
            const int depth_comparison = CompareDepth(left.ViewDepth, right.ViewDepth, false);
            if (depth_comparison != 0)
                return depth_comparison < 0;
            return left.SubmissionOrder < right.SubmissionOrder;
        }

        bool IsTransparentBefore(const MaterialDrawItem& left, const MaterialDrawItem& right)
        {
            const int depth_comparison = CompareDepth(left.ViewDepth, right.ViewDepth, true);
            if (depth_comparison != 0)
                return depth_comparison < 0;
            return left.SubmissionOrder < right.SubmissionOrder;
        }

        using DrawComparisonFn = bool (*)(const MaterialDrawItem& left, const MaterialDrawItem& right);

        void StableSort(Core::Memory::ArenaAllocator* arena, Core::Containers::Array<MaterialDrawItem>& items, DrawComparisonFn comparison)
        {
            const uint32_t item_count = static_cast<uint32_t>(items.size());
            if (item_count < 2)
                return;

            Core::Containers::Array<MaterialDrawItem> scratch = {};
            scratch.init(arena, item_count, item_count);
            for (uint32_t width = 1; width < item_count;)
            {
                for (uint32_t begin = 0; begin < item_count; begin += width * 2)
                {
                    const uint32_t middle = begin + width < item_count ? begin + width : item_count;
                    const uint32_t end    = middle + width < item_count ? middle + width : item_count;
                    uint32_t       left   = begin;
                    uint32_t       right  = middle;
                    uint32_t       output = begin;

                    while (left < middle && right < end)
                    {
                        if (comparison(items[right], items[left]))
                            scratch[output++] = items[right++];
                        else
                            scratch[output++] = items[left++];
                    }
                    while (left < middle)
                        scratch[output++] = items[left++];
                    while (right < end)
                        scratch[output++] = items[right++];
                }

                for (uint32_t item_index = 0; item_index < item_count; ++item_index)
                    items[item_index] = scratch[item_index];

                if (width >= item_count - width)
                    break;
                width *= 2;
            }
        }

        void PrepareOutputArray(Core::Memory::ArenaAllocator* arena, Core::Containers::Array<MaterialDrawItem>& items, uint32_t capacity)
        {
            if (items.data())
                items.clear();
            else
                items.init(arena, capacity);
        }
    } // namespace

    void DrawSorter::Sort(Core::Memory::ArenaAllocator* arena, Core::Containers::ArrayView<const MaterialDrawItem> draws, MaterialDrawLists* out_lists)
    {
        ZENGINE_VALIDATE_ASSERT(arena != nullptr, "Material draw sorting requires an arena")
        ZENGINE_VALIDATE_ASSERT(out_lists != nullptr, "Material draw sorting output is null")

        const uint32_t draw_count = static_cast<uint32_t>(draws.size());
        PrepareOutputArray(arena, out_lists->Opaque, draw_count);
        PrepareOutputArray(arena, out_lists->Transparent, draw_count);
        for (uint32_t draw_index = 0; draw_index < draw_count; ++draw_index)
        {
            const MaterialDrawItem& draw = draws[draw_index];
            ZENGINE_VALIDATE_ASSERT(draw.Material != nullptr, "Material draw sorting requires a material instance")
            if (draw.Material->IsTransparent())
                out_lists->Transparent.push(draw);
            else
                out_lists->Opaque.push(draw);
        }

        StableSort(arena, out_lists->Opaque, &IsOpaqueBefore);
        StableSort(arena, out_lists->Transparent, &IsTransparentBefore);
    }
} // namespace ZEngine::Rendering::Materials
