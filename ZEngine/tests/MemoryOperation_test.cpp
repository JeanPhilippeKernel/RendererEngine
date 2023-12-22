#include <gtest/gtest.h>
#include "Helpers/MemoryOperations.h"

using namespace ZEngine::Helpers;

TEST(MemoryOperationsTest, SecureMemset)
{
    char buffer[10];

    EXPECT_EQ(secure_memset(buffer, 'A', 5, sizeof(buffer)), MEMORY_OP_SUCCESS);
    EXPECT_EQ(buffer[0], 'A');
    EXPECT_EQ(buffer[4], 'A');

    EXPECT_EQ(secure_memset(buffer, 'B', sizeof(buffer) + 1, sizeof(buffer)), MEMORY_OP_FAILURE);
    EXPECT_EQ(secure_memset(nullptr, 'C', 5, sizeof(buffer)), MEMORY_OP_FAILURE);
}

TEST(MemoryOperationsTest, SecureMemcpy)
{
    char src[10] = "Test";
    char dest[10];

    EXPECT_EQ(secure_memcpy(dest, sizeof(dest), src, sizeof(src)), MEMORY_OP_SUCCESS);
    EXPECT_STREQ(dest, "Test");

    EXPECT_EQ(secure_memcpy(dest, 5, src, sizeof(src)), MEMORY_OP_FAILURE);

    EXPECT_EQ(secure_memcpy(nullptr, sizeof(dest), src, sizeof(src)), MEMORY_OP_FAILURE);
    EXPECT_EQ(secure_memcpy(dest, sizeof(dest), nullptr, sizeof(src)), MEMORY_OP_FAILURE);
}

TEST(MemoryOperationsTest, SecureMemmove)
{
    char src[20] = "Test";
    char dest[20];

    EXPECT_EQ(secure_memmove(dest, sizeof(dest), src, sizeof(src)), MEMORY_OP_SUCCESS);
    EXPECT_STREQ(dest, "Test");

    std::strcpy(src, "Overlap");
    EXPECT_EQ(secure_memmove(src + 1, sizeof(src) - 1, src, 7), MEMORY_OP_SUCCESS);

    EXPECT_EQ(secure_memmove(dest, 5, src, sizeof(src)), MEMORY_OP_FAILURE);

    EXPECT_EQ(secure_memmove(nullptr, sizeof(dest), src, sizeof(src)), MEMORY_OP_FAILURE);
    EXPECT_EQ(secure_memmove(dest, sizeof(dest), nullptr, sizeof(src)), MEMORY_OP_FAILURE);
}

TEST(MemoryOperationsTest, SecureStrncpy)
{
    char src[] = "Hello";
    char dest[10];

    EXPECT_EQ(secure_strncpy(dest, sizeof(dest), src, 5), MEMORY_OP_SUCCESS);
    EXPECT_STREQ(dest, "Hello");

    EXPECT_EQ(secure_strncpy(dest, 6, src, 5), MEMORY_OP_SUCCESS);
    EXPECT_EQ(dest[4], 'o');
    EXPECT_EQ(dest[5], '\0');

    EXPECT_EQ(secure_strncpy(nullptr, sizeof(dest), src, 5), MEMORY_OP_FAILURE);
    EXPECT_EQ(secure_strncpy(dest, sizeof(dest), nullptr, 5), MEMORY_OP_FAILURE);
}

TEST(MemoryOperationsTest, SecureStrcpy)
{
    char src[] = "Hello";
    char dest[10];

    EXPECT_EQ(secure_strcpy(dest, sizeof(dest), src), MEMORY_OP_SUCCESS);
    EXPECT_STREQ(dest, "Hello");

    EXPECT_EQ(secure_strcpy(dest, 5, src), MEMORY_OP_FAILURE);

    EXPECT_EQ(secure_strcpy(nullptr, sizeof(dest), src), MEMORY_OP_FAILURE);
    EXPECT_EQ(secure_strcpy(dest, sizeof(dest), nullptr), MEMORY_OP_FAILURE);
}

TEST(MemoryOperationsTest, SecureStrlen)
{
    char str[] = "Hello";

    EXPECT_EQ(secure_strlen(str), 5);
    EXPECT_EQ(secure_strlen(""), 0);
    EXPECT_EQ(secure_strlen(nullptr), 0);
}

TEST(MemoryOperationsTest, SecureMemcmp)
{
    char buffer1[10] = "Test";
    char buffer2[10] = "Test";
    char buffer3[10] = "Different";
}
