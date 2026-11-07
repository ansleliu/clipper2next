#include <gtest/gtest.h>
#include "triangulation/private/triangulation_types.h"
#include <array>
#include <memory_resource>
#include <new>
#include <utility>

namespace in = clipper2next::internal;
namespace {
class Resource final : public std::pmr::memory_resource {
public:
    bool reject_all = false;
    bool reject_next = false;
    std::size_t attempts = 0;
    std::size_t live = 0;
private:
    auto do_allocate(std::size_t bytes, std::size_t alignment) -> void* override {
        ++attempts;
        if (reject_all || std::exchange(reject_next, false)) { throw std::bad_alloc{}; }
        auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++live;
        return result;
    }
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        --live;
    }
    auto do_is_equal(const std::pmr::memory_resource& other) const noexcept -> bool override {
        return this == &other;
    }
};
}

TEST(Clipper2NextTriangulationIncidentStorageTests, BoundaryAndInteriorPairNeedNoExternalStorage) {
    Resource resource;
    resource.reject_all = true;
    std::array<in::triangulation_edge, 4> edges{};
    EXPECT_NO_THROW(([&] {
        in::triangulation_vertex vertex{{17, 23}, &resource};
        for (auto& edge : edges) { vertex.edges.push_back(&edge); }
        EXPECT_EQ(vertex.pt, clipper2next::Point64(17, 23));
        ASSERT_EQ(vertex.edges.size(), edges.size());
        for (std::size_t i = 0; i < edges.size(); ++i) { EXPECT_EQ(vertex.edges[i], &edges[i]); }
    }()));
    EXPECT_EQ(resource.attempts, 0U);
    EXPECT_EQ(resource.live, 0U);
}

TEST(Clipper2NextTriangulationIncidentStorageTests, GrowthFailureKeepsRefsAndAllowsRetry) {
    Resource resource;
    std::array<in::triangulation_edge, 65> edges{};
    bool observed = false;
    {
        in::triangulation_vertex vertex{{0, 0}, &resource};
        for (std::size_t i = 0; i < edges.size(); ++i) {
            resource.reject_next = true;
            try { vertex.edges.push_back(&edges[i]); }
            catch (const std::bad_alloc&) {
                resource.reject_next = false;
                observed = true;
                ASSERT_EQ(vertex.edges.size(), i);
                for (std::size_t j = 0; j < i; ++j) { EXPECT_EQ(vertex.edges[j], &edges[j]); }
                vertex.edges.push_back(&edges[i]);
                ASSERT_EQ(vertex.edges.size(), i + 1);
                EXPECT_EQ(vertex.edges[i], &edges[i]);
            }
            resource.reject_next = false;
        }
        resource.reject_all = true;
    }
    EXPECT_TRUE(observed);
    EXPECT_EQ(resource.live, 0U);
}

TEST(Clipper2NextTriangulationIncidentStorageTests, EraseKeepsOrderAndDestructionDoesNotAllocate) {
    for (const std::size_t count : {4U, 65U}) {
        Resource resource;
        std::array<in::triangulation_edge, 65> edges{};
        std::size_t attempts_before = 0;
        {
            in::triangulation_vertex vertex{{0, 0}, &resource};
            for (std::size_t i = 0; i < count; ++i) { vertex.edges.push_back(&edges[i]); }
            resource.reject_all = true;
            attempts_before = resource.attempts;
            vertex.edges.erase(vertex.edges.begin() + count / 2);
            ASSERT_EQ(vertex.edges.size(), count - 1);
            for (std::size_t i = 0; i < vertex.edges.size(); ++i) {
                EXPECT_EQ(vertex.edges[i], &edges[i < count / 2 ? i : i + 1]);
            }
            while (!vertex.edges.empty()) { vertex.edges.erase(vertex.edges.begin()); }
            EXPECT_EQ(vertex.edges.begin(), vertex.edges.end());
        }
        EXPECT_EQ(resource.attempts, attempts_before);
        EXPECT_EQ(resource.live, 0U);
    }
}
