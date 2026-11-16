// Regression test for a use-after-free WRITE in util::TreeUtil::deleteAll<T>
// (src/util/TreeUtil.h).
//
// The pre-fix body was:
//     for (auto child : aObj->children()) deleteAll<tObj>(child);
//     aObj->children().clear();
//     delete aObj;
// The loop deletes every child, and only then TreeChildren::clear()
// (src/util/TreeNodeBase.h) walks the list it still holds and writes
//     obj->setParent(nullptr); obj->setSibling(nullptr, nullptr);
// into each entry -- i.e. 24 bytes at object offsets +8/+0x10/+0x18
// (mParent/mPrevSib/mNextSib) of children the loop had already deleted. The only
// caller of this template in the repo is the pose editor's "Pull bones" shadow
// tree (src/ctrl/pose/pose_BoneDynamics.cpp), where it ended in a glibc SIGABRT
// ("Process ... dumped core") during a mouse-move drag. The fix detaches each
// child with children().popFront() before recursing, so the list is empty before
// its owner is deleted and no write lands on freed memory.
//
// This test catches the bug WITHOUT a sanitizer. Node has a class-specific
// allocator: operator delete poisons the block with 0xAA and deliberately does
// NOT hand it back to the C++ allocator (it records {pointer,size} and leaks it:
// a bounded leak of one small tree's nodes). The block therefore stays mapped
// memory owned by the test, so inspecting it afterwards is well-defined rather
// than UB, and the assertion below reads it directly -- pre-fix, clear() zeroes
// the poisoned offsets; post-fix every byte is still 0xAA.
//
// Under ASan (-DAE_ENABLE_ASAN=ON) this is still a plain assertion failure: the
// custom operator delete never releases the block to ASan either, so ASan has
// nothing to report and the explicit CHECK (with its offending block offset) is
// what fails. tests/bonekey_regression.cpp is the sibling precedent; it relies
// on ASan's own heap-use-after-free report, this one needs no sanitizer.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
#include "XCAssert.h"
#include "util/TreeNodeBase.h"
#include "util/TreeUtil.h"

class AssertHandler : public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class ErrorHandler : public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};
AssertHandler gAssertHandler;
ErrorHandler gErrorHandler;
XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

namespace {

// Number of children the bug writes into per retired node: setParent (mParent,
// +8) plus setSibling (mPrevSib +0x10, mNextSib +0x18).
const size_t kUafWriteEnd = 0x20;

const unsigned char kPoison = 0xAA;

struct Block {
    void* ptr;
    size_t size;
};

// Blocks handed out by Node::operator new, and blocks already retired by
// Node::operator delete. Function-local statics: no init-order surprises.
std::vector<Block>& allocations() {
    static std::vector<Block> list;
    return list;
}
std::vector<Block>& retired() {
    static std::vector<Block> list;
    return list;
}

// The tree node under test. The custom allocator is the whole point: it lets the
// test read a node's storage after the node was deleted, without UB and without
// a sanitizer.
struct Node : util::TreeNodeBase<Node> {
    Node(): util::TreeNodeBase<Node>(this) {}

    // Bigger than the 24 bytes the bug writes, so a wrong-size write would still
    // leave poison behind after the first 32 bytes (checked for the whole block).
    unsigned char payload[64];

    void* operator new(size_t aSize) {
        void* ptr = std::malloc(aSize);
        if (!ptr)
            throw std::bad_alloc();
        allocations().push_back(Block{ptr, aSize});
        return ptr;
    }

    // Both deallocation forms the standard can call. Neither returns the block to
    // the C++ allocator: poison it and keep it mapped (deliberate, bounded leak).
    void operator delete(void* aPtr, size_t aSize) noexcept {
        std::memset(aPtr, kPoison, aSize);
        retired().push_back(Block{aPtr, aSize});
    }
    void operator delete(void* aPtr) noexcept {
        // Sized deallocation is preferred (C++14); this fallback recovers the size
        // from the allocation record so the poison still covers the whole block.
        for (size_t i = 0; i < allocations().size(); ++i) {
            if (allocations()[i].ptr == aPtr) {
                std::memset(aPtr, kPoison, allocations()[i].size);
                retired().push_back(allocations()[i]);
                return;
            }
        }
        // Unreachable: every block this test deletes came from Node::operator new.
        std::fprintf(stderr, "FAIL: operator delete(%p) has no allocation record\n", aPtr);
        std::abort();
    }
};

static_assert(sizeof(Node) > kUafWriteEnd,
              "Node must be larger than the 24-byte write, or the poison cannot witness it");

#define CHECK(cond)                                                                                 \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            std::printf("FAIL %d: %s\n", __LINE__, #cond);                                          \
            return 1;                                                                               \
        }                                                                                           \
    } while (false)

int run() {
    // root -> c0 (-> g0), c1, c2
    Node* root = new Node();
    Node* c0 = new Node();
    Node* c1 = new Node();
    Node* c2 = new Node();
    Node* g0 = new Node();
    root->children().pushBack(c0);
    root->children().pushBack(c1);
    root->children().pushBack(c2);
    c0->children().pushBack(g0);

    const size_t built = 5;
    CHECK(allocations().size() == built); // sanity: all nodes come from our allocator

    util::TreeUtil::deleteAll(root);

    // (a) Every node was retired, once each, root last (deleteAll is depth-first
    // and deletes an owner only after its subtree).
    CHECK(retired().size() == built);
    CHECK(retired().back().ptr == static_cast<void*>(root));

    const void* expected[built] = {root, c0, c1, c2, g0};
    for (size_t i = 0; i < built; ++i) {
        size_t matches = 0;
        for (size_t j = 0; j < retired().size(); ++j) {
            if (retired()[j].ptr == expected[i])
                ++matches;
        }
        CHECK(matches == 1);
    }

    // (b) The load-bearing assertion: nothing wrote into a retired block. Pre-fix,
    // children().clear() writes 0 to offsets +8/+0x10/+0x18 of every already-freed
    // child, so the poison is destroyed there (the root, deleted last, stays
    // intact -- which is why (a) alone cannot catch the bug).
    for (size_t i = 0; i < retired().size(); ++i) {
        const unsigned char* bytes = static_cast<const unsigned char*>(retired()[i].ptr);
        for (size_t off = 0; off < retired()[i].size; ++off) {
            if (bytes[off] != kPoison) {
                std::printf("FAIL %d: retired block %p (size %zu) was written into at "
                            "offset 0x%zX (+%zu): expected 0x%02X, found 0x%02X\n",
                            __LINE__, retired()[i].ptr, retired()[i].size, off, off, kPoison, bytes[off]);
                return 1;
            }
        }
    }

    std::printf("PASS: deleteAll retired %zu nodes, root last, and wrote into none of them\n",
                retired().size());
    return 0;
}

} // namespace

int main() {
    return run();
}
