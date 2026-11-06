#include <ZEngine/Core/Memory/NativePages.h>
#include <gtest/gtest.h>
#include <limits>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#elif defined(__linux__)
#include <sys/mman.h>
#include <cerrno>
#endif

namespace ZEngine::Core::Memory::Tests
{
    namespace
    {
        bool IsUnmapped(void* address)
        {
#if defined(_WIN32)
            MEMORY_BASIC_INFORMATION region = {};
            return VirtualQuery(address, &region, sizeof(region)) && region.State == MEM_FREE;
#elif defined(__APPLE__)
            const auto                     target = reinterpret_cast<mach_vm_address_t>(address);
            mach_vm_address_t              region = target;
            mach_vm_size_t                 bytes  = 0;
            vm_region_basic_info_data_64_t info   = {};
            mach_msg_type_number_t         count  = VM_REGION_BASIC_INFO_COUNT_64;
            mach_port_t                    object = MACH_PORT_NULL;
            const auto                     result = mach_vm_region(mach_task_self(), &region, &bytes, VM_REGION_BASIC_INFO_64, reinterpret_cast<vm_region_info_t>(&info), &count, &object);
            if (object != MACH_PORT_NULL)
            {
                mach_port_deallocate(mach_task_self(), object);
            }
            return result == KERN_INVALID_ADDRESS || (result == KERN_SUCCESS && region > target);
#elif defined(__linux__)
            unsigned char residency = 0;
            errno                   = 0;
            return mincore(address, NativePageSize(), &residency) != 0 && errno == ENOMEM;
#else
            return false;
#endif
        }

        struct Left
        {
            virtual ~Left() = default;
            uint32_t Value  = 7;
        };
        struct Right
        {
            virtual ~Right() = default;
            uint32_t Value   = 11;
        };
        struct alignas(64) NativeObject final : Left, Right, NativePageObject<NativeObject>
        {
            uint64_t Value        = 73;
            size_t*  Destructions = nullptr;
            ~NativeObject() override
            {
                if (Destructions)
                {
                    ++*Destructions;
                }
            }
        };
    } // namespace

    TEST(NativePagesTest, PureFootprintQueriesRejectInvalidAndOverflowingSizes)
    {
        const auto page = NativePageSize();
        ASSERT_GT(page, 0u);
        EXPECT_EQ(page & (page - 1), 0u);
        for (size_t repeat = 0; repeat < 128; ++repeat)
        {
            EXPECT_EQ(NativePageFootprint(1, page), page);
            EXPECT_EQ(NativePageFootprint(page, page), page);
            EXPECT_EQ(NativePageFootprint(page + 1, page), 2 * page);
            EXPECT_EQ(NativePageFootprint(0, page), 0u);
            EXPECT_EQ(NativePageFootprint(1, 0), 0u);
            EXPECT_EQ(NativePageFootprint(1, 3), 0u);
            EXPECT_EQ(NativePageFootprint(std::numeric_limits<size_t>::max(), page), 0u);
        }
    }

    TEST(NativePagesTest, WritableMappingHasTheExactRoundedCapacityAndNoSeparateHeader)
    {
        const auto bytes = NativePageFootprint(NativePageSize() + 1, NativePageSize());
        ASSERT_GT(bytes, 0u);
        auto* mapping = static_cast<uint8_t*>(AllocateNativePages(bytes));
        ASSERT_NE(mapping, nullptr);
        EXPECT_FALSE(IsUnmapped(mapping));
        EXPECT_EQ(reinterpret_cast<uintptr_t>(mapping) % NativePageSize(), 0u);
        EXPECT_EQ(mapping[0], 0u);
        EXPECT_EQ(mapping[bytes - 1], 0u);
        mapping[0]         = 17;
        mapping[bytes - 1] = 29;
        EXPECT_EQ(mapping[0], 17u);
        EXPECT_EQ(mapping[bytes - 1], 29u);
        EXPECT_TRUE(FreeNativePages(mapping, bytes));
        EXPECT_TRUE(IsUnmapped(mapping));
    }

    TEST(NativePagesTest, InvalidMappingArgumentsFailBeforeAnyNativeAllocationOrRelease)
    {
        EXPECT_EQ(AllocateNativePages(0), nullptr);
        EXPECT_EQ(AllocateNativePages(1), nullptr);
        EXPECT_FALSE(FreeNativePages(nullptr, NativePageSize()));
        EXPECT_FALSE(FreeNativePages(nullptr, 0));
        const auto page    = NativePageSize();
        auto*      mapping = static_cast<uint8_t*>(AllocateNativePages(page));
        ASSERT_NE(mapping, nullptr);
        EXPECT_FALSE(FreeNativePages(mapping + 1, page));
        EXPECT_FALSE(FreeNativePages(mapping, page - 1));
        mapping[page - 1] = 31; // Refused release preserved the mapping.
        EXPECT_EQ(mapping[page - 1], 31u);
        EXPECT_TRUE(FreeNativePages(mapping, page));
    }

    TEST(NativePagesTest, FixedObjectDefaultsAndDynamicDeletionThroughAnAdjustedBaseRemainCorrect)
    {
        size_t destructions = 0;
        for (size_t repeat = 0; repeat < 128; ++repeat)
        {
            auto* object = new (std::nothrow) NativeObject{};
            ASSERT_NE(object, nullptr);
            EXPECT_EQ(object->Value, 73u);
            EXPECT_EQ(static_cast<Left*>(object)->Value, 7u);
            EXPECT_EQ(static_cast<Right*>(object)->Value, 11u);
            EXPECT_EQ(reinterpret_cast<uintptr_t>(object) % NativePageSize(), 0u);
            EXPECT_GE(NativeObject::NativeStorageBytes(), sizeof(NativeObject));
            EXPECT_EQ(NativeObject::NativeStorageBytes() % NativePageSize(), 0u);
            object->Destructions = &destructions;
            Right* adjusted      = object;
            EXPECT_NE(static_cast<void*>(adjusted), static_cast<void*>(object));
            void* mapping = object;
            delete adjusted;
            EXPECT_TRUE(IsUnmapped(mapping));
        }
        EXPECT_EQ(destructions, 128u);
    }
} // namespace ZEngine::Core::Memory::Tests
