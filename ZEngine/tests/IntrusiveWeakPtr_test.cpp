#include <gtest/gtest.h>
#include "Helpers/IntrusiveWeakPtr.h"

using namespace ZEngine::Helpers;

class MockWeakObject : public WeakRefCounted  {
public:
    MockWeakObject(int value = 0) : value_(value) {}
    int GetValue() const { return value_; }

private:
    int value_;
};

// Test default constructor
TEST(IntrusiveWeakPtrTest, DefaultConstructor) {
    IntrusiveWeakPtr<MockWeakObject> weakPtr;
    EXPECT_TRUE(weakPtr.is_expired());
}

// Test constructing from an IntrusivePtr
TEST(IntrusiveWeakPtrTest, ConstructFromIntrusivePtr) {
    IntrusivePtr<MockWeakObject> strongPtr(new MockWeakObject(5));
    IntrusiveWeakPtr<MockWeakObject> weakPtr(strongPtr);
    EXPECT_FALSE(weakPtr.is_expired());
}

// Test reset functionality
TEST(IntrusiveWeakPtrTest, ResetFunctionality) {
    IntrusivePtr<MockWeakObject> strongPtr(new MockWeakObject(35));
    IntrusiveWeakPtr<MockWeakObject> weakPtr(strongPtr);
    weakPtr.reset();
    EXPECT_TRUE(weakPtr.is_expired());
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
        EXPECT_FALSE(weakPtr.is_expired());
    }
    // strongPtr goes out of scope here
    EXPECT_TRUE(weakPtr.is_expired());
}


// Test copy constructor
TEST(IntrusiveWeakPtrTest, CopyConstructor) {
    IntrusivePtr<MockWeakObject> strongPtr(new MockWeakObject(15));
    IntrusiveWeakPtr<MockWeakObject> weakPtr1(strongPtr);
    IntrusiveWeakPtr<MockWeakObject> weakPtr2(weakPtr1);
    EXPECT_FALSE(weakPtr2.is_expired());
}

// Test move constructor
TEST(IntrusiveWeakPtrTest, MoveConstructor) {
    IntrusivePtr<MockWeakObject> strongPtr(new MockWeakObject(20));
    IntrusiveWeakPtr<MockWeakObject> weakPtr1(strongPtr);
    IntrusiveWeakPtr<MockWeakObject> weakPtr2(std::move(weakPtr1));
    EXPECT_TRUE(weakPtr1.is_expired());
    EXPECT_FALSE(weakPtr2.is_expired());
}

// Test copy assignment
TEST(IntrusiveWeakPtrTest, CopyAssignment) {
    IntrusivePtr<MockWeakObject> strongPtr(new MockWeakObject(25));
    IntrusiveWeakPtr<MockWeakObject> weakPtr1(strongPtr);
    IntrusiveWeakPtr<MockWeakObject> weakPtr2;
    weakPtr2 = weakPtr1;
    EXPECT_FALSE(weakPtr2.is_expired());
}

// Test move assignment
TEST(IntrusiveWeakPtrTest, MoveAssignment) {
    IntrusivePtr<MockWeakObject> strongPtr(new MockWeakObject(30));
    IntrusiveWeakPtr<MockWeakObject> weakPtr1(strongPtr);
    IntrusiveWeakPtr<MockWeakObject> weakPtr2;
    weakPtr2 = std::move(weakPtr1);
    EXPECT_TRUE(weakPtr1.is_expired());
    EXPECT_FALSE(weakPtr2.is_expired());
}

// Test reset UseCount
TEST(IntrusiveWeakPtrTest, UseCount) {
    IntrusivePtr<MockWeakObject> strongPtr(new MockWeakObject(35));
    IntrusivePtr<MockWeakObject> strongPtr2(strongPtr);
    IntrusiveWeakPtr<MockWeakObject> weakPtr(strongPtr);
    EXPECT_EQ(weakPtr.use_count(), 2);

    strongPtr.reset();
    EXPECT_EQ(weakPtr.use_count(),1);
    strongPtr2.reset();
    EXPECT_EQ(weakPtr.use_count(),0);
}
