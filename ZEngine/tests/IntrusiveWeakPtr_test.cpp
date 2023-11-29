#include <gtest/gtest.h>
#include "Helpers/IntrusivePtr.h"

using namespace ZEngine::Helpers;

class MockWeakObject : public RefCounted
{
public:
    MockWeakObject(int value = 0) : value_(value) {}
    int GetValue() const
    {
        return value_;
    }

private:
    int value_;
};

class MockObjectChild : public MockWeakObject
{
public:
    MockObjectChild(int value = 0) : MockWeakObject(value) {}
};

// Test default constructor
TEST(IntrusiveWeakPtrTest, DefaultConstructor) {
    IntrusiveWeakPtr<MockWeakObject> weakPtr;
    EXPECT_TRUE(weakPtr.expired());
}

// Test constructing from an IntrusivePtr
TEST(IntrusiveWeakPtrTest, ConstructFromIntrusivePtr) {
    IntrusivePtr<MockWeakObject> strongPtr(new MockWeakObject(5));
    IntrusiveWeakPtr<MockWeakObject> weakPtr(strongPtr);
    EXPECT_FALSE(weakPtr.expired());
}

// Test reset functionality
TEST(IntrusiveWeakPtrTest, ResetFunctionality) {
    IntrusivePtr<MockWeakObject> strongPtr(new MockWeakObject(35));
    IntrusiveWeakPtr<MockWeakObject> weakPtr(strongPtr);
    weakPtr.reset();
    EXPECT_TRUE(weakPtr.expired());
}

// Test swap functionality
TEST(IntrusiveWeakPtrTest, SwapFunctionality) {
    IntrusivePtr<MockWeakObject> strongPtr1(new MockWeakObject(40));
    IntrusivePtr<MockWeakObject> strongPtr2(new MockWeakObject(45));
    IntrusiveWeakPtr<MockWeakObject> weakPtr1(strongPtr1);
    IntrusiveWeakPtr<MockWeakObject> weakPtr2(strongPtr2);
    weakPtr1.swap(weakPtr2);
    EXPECT_EQ(weakPtr1.lock()->GetValue(), 45);
    EXPECT_EQ(weakPtr2.lock()->GetValue(), 40);
}

// Test lock functionality
TEST(IntrusiveWeakPtrTest, LockFunctionality) {
    IntrusivePtr<MockWeakObject> strongPtr(new MockWeakObject(10));
    IntrusiveWeakPtr<MockWeakObject> weakPtr(strongPtr);
    auto lockedPtr = weakPtr.lock();
    ASSERT_NE(lockedPtr, nullptr);
    EXPECT_EQ(lockedPtr->GetValue(), 10);
}

// Test expiration after strongPtr is reset
TEST(IntrusiveWeakPtrTest, ExpirationAfterStrongPtrReset) {
    IntrusiveWeakPtr<MockWeakObject> weakPtr;
    {
        IntrusivePtr<MockWeakObject> strongPtr(new MockWeakObject(50));
        weakPtr = IntrusiveWeakPtr<MockWeakObject>(strongPtr);
        EXPECT_FALSE(weakPtr.expired());
    }
    // strongPtr goes out of scope here
    EXPECT_TRUE(weakPtr.expired());
}


// Test copy constructor
TEST(IntrusiveWeakPtrTest, CopyConstructor) {
    IntrusivePtr<MockWeakObject> strongPtr(new MockWeakObject(15));
    IntrusiveWeakPtr<MockWeakObject> weakPtr1(strongPtr);
    IntrusiveWeakPtr<MockWeakObject> weakPtr2(weakPtr1);
    EXPECT_FALSE(weakPtr2.expired());
}

// Test move constructor
TEST(IntrusiveWeakPtrTest, MoveConstructor) {
    IntrusivePtr<MockWeakObject> strongPtr(new MockWeakObject(20));
    IntrusiveWeakPtr<MockWeakObject> weakPtr1(strongPtr);
    IntrusiveWeakPtr<MockWeakObject> weakPtr2(std::move(weakPtr1));
    EXPECT_TRUE(weakPtr1.expired());
    EXPECT_FALSE(weakPtr2.expired());
}

// Test copy assignment
TEST(IntrusiveWeakPtrTest, CopyAssignment) {
    IntrusivePtr<MockWeakObject> strongPtr(new MockWeakObject(25));
    IntrusiveWeakPtr<MockWeakObject> weakPtr1(strongPtr);
    IntrusiveWeakPtr<MockWeakObject> weakPtr2;
    weakPtr2 = weakPtr1;
    EXPECT_FALSE(weakPtr2.expired());
}

// Test move assignment
TEST(IntrusiveWeakPtrTest, MoveAssignment) {
    IntrusivePtr<MockWeakObject> strongPtr(new MockWeakObject(30));
    IntrusiveWeakPtr<MockWeakObject> weakPtr1(strongPtr);
    IntrusiveWeakPtr<MockWeakObject> weakPtr2;
    weakPtr2 = std::move(weakPtr1);
    EXPECT_TRUE(weakPtr1.expired());
    EXPECT_FALSE(weakPtr2.expired());
}

TEST(IntrusiveWeakPtrTest, BaseDerivedType)
{
    std::shared_ptr<MockWeakObject> sharedPtr = std::make_shared<MockObjectChild>(5);
    std::weak_ptr<MockWeakObject>   weakSharedPtr2(sharedPtr);

    std::shared_ptr<MockWeakObject> sharedPtr3 = std::make_shared<MockObjectChild>(5);
    std::weak_ptr<MockWeakObject>   weakSharedPtr3;
    weakSharedPtr3 = sharedPtr3;

    IntrusivePtr<MockWeakObject>     intrusivePtr = make_intrusive<MockObjectChild>(45);
    IntrusiveWeakPtr<MockWeakObject> intrusiveWeakPtr2(intrusivePtr);

    IntrusivePtr<MockWeakObject>     intrusivePtr3 = make_intrusive<MockObjectChild>(5);
    IntrusiveWeakPtr<MockWeakObject> intrusiveWeakPtr4;
    intrusiveWeakPtr4 = intrusivePtr3;
}