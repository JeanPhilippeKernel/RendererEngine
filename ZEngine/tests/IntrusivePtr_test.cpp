#include <gtest/gtest.h>
#include "Helpers/IntrusivePtr.h"



using namespace ZEngine::Helpers;

class MockObject : public RefCounted {
public:
    MockObject(int value = 0) : value_(value) {}
    int GetValue() const { return value_; }

private:
    int value_;
};

// Test default constructor
TEST(IntrusivePtrTest, DefaultConstructor) {
    IntrusivePtr<MockObject> ptr;
    EXPECT_EQ(ptr.get(), nullptr);
}

// Test constructor with object
TEST(IntrusivePtrTest, ConstructorWithObject) {
    MockObject* rawPtr = new MockObject();
    IntrusivePtr<MockObject> ptr(rawPtr);
    EXPECT_EQ(ptr.get(), rawPtr);
    EXPECT_EQ(rawPtr->RefCount(), 1);
}

// Test copy constructor
TEST(IntrusivePtrTest, CopyConstructor) {
    MockObject* rawPtr = new MockObject();
    IntrusivePtr<MockObject> originalPtr(rawPtr);
    IntrusivePtr<MockObject> copyPtr(originalPtr);
    
    EXPECT_EQ(copyPtr.get(), rawPtr);
    EXPECT_EQ(originalPtr.get(), rawPtr);
    EXPECT_EQ(rawPtr->RefCount(), 2);
}

// Test move constructor
TEST(IntrusivePtrTest, MoveConstructor) {
    MockObject* rawPtr = new MockObject();
    IntrusivePtr<MockObject> originalPtr(rawPtr);
    IntrusivePtr<MockObject> movedPtr(std::move(originalPtr));

    EXPECT_EQ(movedPtr.get(), rawPtr);
    EXPECT_EQ(originalPtr.get(), nullptr);
    EXPECT_EQ(rawPtr->RefCount(), 1);
}

// Test Copy Assignment Operator
TEST(IntrusivePtrTest, CopyAssignmentOperator) {
    MockObject* rawPtr1 = new MockObject();
    MockObject* rawPtr2 = new MockObject();
    IntrusivePtr<MockObject> ptr1(rawPtr1);
    IntrusivePtr<MockObject> ptr2(rawPtr2);

    ptr1 = ptr2;

    EXPECT_EQ(ptr1.get(), rawPtr2);
    EXPECT_EQ(ptr2.get(), rawPtr2);
    EXPECT_EQ(rawPtr2->RefCount(), 2);
}

// Test Move Assignment Operator
TEST(IntrusivePtrTest, MoveAssignmentOperator) {
    MockObject* rawPtr = new MockObject();
    IntrusivePtr<MockObject> ptr1(rawPtr);
    IntrusivePtr<MockObject> ptr2;

    ptr2 = std::move(ptr1);

    EXPECT_EQ(ptr2.get(), rawPtr);
    EXPECT_EQ(ptr1.get(), nullptr);
}

// Test Assignment From Raw Pointer
TEST(IntrusivePtrTest, AssignmentFromRawPointer) {
    MockObject* rawPtr = new MockObject();
    IntrusivePtr<MockObject> ptr;

    ptr = rawPtr;

    EXPECT_EQ(ptr.get(), rawPtr);
    EXPECT_EQ(rawPtr->RefCount(), 1);
}

// Test Reset Method
TEST(IntrusivePtrTest, ResetMethod) {
    MockObject* rawPtr = new MockObject();
    IntrusivePtr<MockObject> ptr(rawPtr);

    ptr.reset();

    EXPECT_EQ(ptr.get(), nullptr);
    // Check if rawPtr is deleted; this can be tricky as it may lead to undefined behavior if accessed
}

// Test Swap Method
TEST(IntrusivePtrTest, SwapMethod) {
    MockObject* rawPtr1 = new MockObject();
    MockObject* rawPtr2 = new MockObject();
    IntrusivePtr<MockObject> ptr1(rawPtr1);
    IntrusivePtr<MockObject> ptr2(rawPtr2);

    ptr1.swap(ptr2);

    EXPECT_EQ(ptr1.get(), rawPtr2);
    EXPECT_EQ(ptr2.get(), rawPtr1);
}

// Test Get Method
TEST(IntrusivePtrTest, GetMethod) {
    MockObject* rawPtr = new MockObject();
    IntrusivePtr<MockObject> ptr(rawPtr);

    EXPECT_EQ(ptr.get(), rawPtr);
}

// Test Dereference Operator
TEST(IntrusivePtrTest, DereferenceOperator) {
    MockObject* rawPtr = new MockObject();
    IntrusivePtr<MockObject> ptr(rawPtr);

    EXPECT_EQ(&(*ptr), rawPtr);
}

// Test Member Access Operator
TEST(IntrusivePtrTest, MemberAccessOperator) {
    MockObject* rawPtr = new MockObject();
    IntrusivePtr<MockObject> ptr(rawPtr);

    EXPECT_EQ(ptr->RefCount(), 1);
}

// Test Not Operator
TEST(IntrusivePtrTest, NotOperator) {
    IntrusivePtr<MockObject> ptr;
    EXPECT_TRUE(!ptr);

    ptr = new MockObject();
    EXPECT_FALSE(!ptr);
}

// Test Bool Conversion Operator
TEST(IntrusivePtrTest, BoolConversionOperator) {
    IntrusivePtr<MockObject> ptr;
    EXPECT_FALSE(static_cast<bool>(ptr));

    ptr = new MockObject();
    EXPECT_TRUE(static_cast<bool>(ptr));
}

// Test Equality with nullptr
TEST(IntrusivePtrTest, EqualityWithNullptr) {
    IntrusivePtr<MockObject> ptr;
    EXPECT_TRUE(ptr == nullptr);

    ptr = new MockObject();
    EXPECT_FALSE(ptr == nullptr);
}

// Test Inequality with nullptr
TEST(IntrusivePtrTest, InequalityWithNullptr) {
    IntrusivePtr<MockObject> ptr;
    EXPECT_FALSE(ptr != nullptr);

    ptr = new MockObject();
    EXPECT_TRUE(ptr != nullptr);
}

// Test Equality with Raw Pointer
TEST(IntrusivePtrTest, EqualityWithRawPointer) {
    MockObject* rawPtr = new MockObject();
    IntrusivePtr<MockObject> ptr(rawPtr);
    EXPECT_TRUE(ptr == rawPtr);

    IntrusivePtr<MockObject> otherPtr;
    EXPECT_FALSE(ptr == otherPtr.get());
}

// Test Inequality with Raw Pointer
TEST(IntrusivePtrTest, InequalityWithRawPointer) {
    MockObject* rawPtr = new MockObject();
    IntrusivePtr<MockObject> ptr(rawPtr);
    EXPECT_FALSE(ptr != rawPtr);

    IntrusivePtr<MockObject> otherPtr;
    EXPECT_TRUE(ptr != otherPtr.get());
}

// Test Equality with Another IntrusivePtr
TEST(IntrusivePtrTest, EqualityWithAnotherIntrusivePtr) {
    MockObject* rawPtr = new MockObject();
    IntrusivePtr<MockObject> ptr1(rawPtr);
    IntrusivePtr<MockObject> ptr2(rawPtr);
    EXPECT_TRUE(ptr1 == ptr2);

    IntrusivePtr<MockObject> ptr3;
    EXPECT_FALSE(ptr1 == ptr3);
}

// Test Inequality with Another IntrusivePtr
TEST(IntrusivePtrTest, InequalityWithAnotherIntrusivePtr) {
    MockObject* rawPtr = new MockObject();
    IntrusivePtr<MockObject> ptr1(rawPtr);
    IntrusivePtr<MockObject> ptr2(rawPtr);
    EXPECT_FALSE(ptr1 != ptr2);

    IntrusivePtr<MockObject> ptr3;
    EXPECT_TRUE(ptr1 != ptr3);
}

TEST(IntrusivePtrTest, NonMemberSwapFunction) {
    IntrusivePtr<MockObject> ptr1(new MockObject(10));
    IntrusivePtr<MockObject> ptr2(new MockObject(20));

    swap(ptr1, ptr2); // Using the non-member swap function

    EXPECT_EQ(ptr1->GetValue(), 20);
    EXPECT_EQ(ptr2->GetValue(), 10);
}

// Test make_intrusive Function
TEST(IntrusivePtrTest, MakeIntrusiveFunction) {
    IntrusivePtr<MockObject> ptr = make_intrusive<MockObject>(30);

    EXPECT_TRUE(ptr != nullptr);
    EXPECT_EQ(ptr->GetValue(), 30);
}

// Test std::hash Specialization
TEST(IntrusivePtrTest, HashSpecialization) {
    MockObject* rawPtr = new MockObject();
    IntrusivePtr<MockObject> ptr(rawPtr);

    std::hash<IntrusivePtr<MockObject>> intrusivePtrHash;
    std::hash<MockObject*> rawPtrHash;

    EXPECT_EQ(intrusivePtrHash(ptr), rawPtrHash(rawPtr));
}
