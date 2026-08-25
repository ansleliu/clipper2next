#include <gtest/gtest.h>

#include "../support/random_path_generator.h"

#include "clipper2next/batch.h"
#include "clipper2next/clipper.h"

#include <algorithm>
#include <cstdint>
#include <future>
#include <vector>

namespace next = clipper2next;
namespace support = clipper2next::tests::support;

namespace {

[[nodiscard]] auto make_request(std::uint32_t seed, int max_complexity) -> next::clip_request64 {
    support::random_path_generator generator{seed};
    next::clip_request64 request;
    request.clip_type = generator.clip_type();
    request.fill_rule = generator.fill_rule();
    request.subjects = generator.paths(1, max_complexity);
    request.open_subjects = generator.paths(0, max_complexity / 2);
    request.clips = generator.paths(0, max_complexity);
    return request;
}

}  // namespace

TEST(Clipper2NextFuzzSmokeTests, SeededRandomClipRequestsAreDeterministic) {
    for (std::uint32_t seed = 1; seed <= 96; ++seed) {
        const auto request = make_request(seed, std::max(2, static_cast<int>(seed / 4U)));

        const auto first = next::clip(request);
        const auto second = next::clip(request);

        EXPECT_EQ(second.closed, first.closed) << seed;
        EXPECT_EQ(second.open, first.open) << seed;
    }
}

TEST(Clipper2NextFuzzSmokeTests, SeededBatchClipMatchesScalarClip) {
    std::vector<next::clip_request64> requests;
    for (std::uint32_t seed = 100; seed < 132; ++seed) {
        requests.push_back(make_request(seed, 12));
    }

    const auto batch_results = next::clip_batch(requests);
    ASSERT_EQ(batch_results.size(), requests.size());
    for (std::size_t index = 0; index < requests.size(); ++index) {
        const auto scalar = next::clip(requests[index]);
        EXPECT_EQ(batch_results[index].closed, scalar.closed) << index;
        EXPECT_EQ(batch_results[index].open, scalar.open) << index;
    }
}

TEST(Clipper2NextFuzzSmokeTests, ParallelSeededClipRequestsMatchSerialResults) {
    std::vector<next::clip_request64> requests;
    for (std::uint32_t seed = 200; seed < 216; ++seed) {
        requests.push_back(make_request(seed, 16));
    }

    std::vector<next::paths64_result> serial;
    serial.reserve(requests.size());
    for (const auto& request : requests) { serial.push_back(next::clip(request)); }

    std::vector<std::future<next::paths64_result>> futures;
    futures.reserve(requests.size());
    for (const auto& request : requests) {
        futures.push_back(
            std::async(std::launch::async, [request] { return next::clip(request); }));
    }

    for (std::size_t index = 0; index < futures.size(); ++index) {
        const auto parallel = futures[index].get();
        EXPECT_EQ(parallel.closed, serial[index].closed) << index;
        EXPECT_EQ(parallel.open, serial[index].open) << index;
    }
}
