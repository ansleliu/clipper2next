#include <benchmark/benchmark.h>

#include <utility>
#include <vector>

#include "clipper2next/batch.h"
#include "clipper2next/clip.h"
#include "clipper2next/core.h"

namespace {

struct batch_clip_fixture final {
    clipper2next::Paths64 subject;
    clipper2next::Paths64 clip;
};

auto make_clip_fixture() -> batch_clip_fixture {
    return {
        {clipper2next::Path64{{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}}},
        {clipper2next::Path64{{250, 250}, {1250, 250}, {1250, 1250}, {250, 1250}}},
    };
}

auto execute_union(const clipper2next::Paths64& subjects, const clipper2next::Paths64& clips)
    -> clipper2next::Paths64 {
    clipper2next::clip_request64 request;
    request.clip_type = clipper2next::ClipType::Union;
    request.fill_rule = clipper2next::FillRule::NonZero;
    request.subjects = subjects;
    request.clips = clips;
    return clipper2next::clip(request).closed;
}

auto make_jobs(int count) -> std::vector<batch_clip_fixture> {
    std::vector<batch_clip_fixture> jobs;
    jobs.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) { jobs.push_back(make_clip_fixture()); }
    return jobs;
}

auto BM_next_batch_scalar(benchmark::State& state) -> void {
    auto jobs = make_jobs(static_cast<int>(state.range(0)));
    for (auto _ : state) {
        for (const auto& job : jobs) {
            auto result = execute_union(job.subject, job.clip);
            benchmark::DoNotOptimize(result);
        }
    }
}

auto BM_next_batch_public_clip(benchmark::State& state) -> void {
    auto jobs = make_jobs(static_cast<int>(state.range(0)));
    std::vector<clipper2next::clip_request64> requests;
    requests.reserve(jobs.size());
    for (const auto& job : jobs) {
        clipper2next::clip_request64 request;
        request.clip_type = clipper2next::ClipType::Union;
        request.fill_rule = clipper2next::FillRule::NonZero;
        request.subjects = job.subject;
        request.clips = job.clip;
        requests.emplace_back(std::move(request));
    }

    for (auto _ : state) {
        auto result = clipper2next::clip_batch(requests);
        benchmark::DoNotOptimize(result);
    }
}

BENCHMARK(BM_next_batch_scalar)->Arg(1);
BENCHMARK(BM_next_batch_public_clip)->Arg(1);
BENCHMARK(BM_next_batch_scalar)->Arg(8);
BENCHMARK(BM_next_batch_public_clip)->Arg(8);
BENCHMARK(BM_next_batch_scalar)->Arg(64);
BENCHMARK(BM_next_batch_public_clip)->Arg(64);
BENCHMARK(BM_next_batch_scalar)->Arg(128);
BENCHMARK(BM_next_batch_public_clip)->Arg(128);
BENCHMARK(BM_next_batch_scalar)->Arg(256);
BENCHMARK(BM_next_batch_public_clip)->Arg(256);
BENCHMARK(BM_next_batch_scalar)->Arg(512);
BENCHMARK(BM_next_batch_public_clip)->Arg(512);
BENCHMARK(BM_next_batch_scalar)->Arg(1024);
BENCHMARK(BM_next_batch_public_clip)->Arg(1024);
BENCHMARK(BM_next_batch_scalar)->Arg(2048);
BENCHMARK(BM_next_batch_public_clip)->Arg(2048);

}  // namespace
