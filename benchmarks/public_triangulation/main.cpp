#include "clipper2next/triangulation.h"
#include "clipper2next/api/memory.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sched.h>
#endif

namespace next = clipper2next;
using clock_type = std::chrono::steady_clock;
struct Fixture { std::string name; next::Paths64 paths; };

auto load(const std::string& file) -> std::vector<Fixture> {
    std::ifstream in(file);
    std::size_t count{};
    in >> count;
    std::vector<Fixture> cases(count);
    for (auto& item : cases) {
        std::size_t rings{};
        in >> item.name >> rings;
        item.paths.resize(rings);
        for (auto& ring : item.paths) {
            std::size_t points{};
            in >> points;
            ring.resize(points);
            for (auto& point : ring) { in >> point.x >> point.y; }
        }
    }
    if (!in || count == 0) { throw std::runtime_error("incomplete fixture input"); }
    std::string extra;
    if (in >> extra) { throw std::runtime_error("extra fixture input"); }
    return cases;
}

auto paths_json(const next::Paths64& paths) -> void {
    std::cout << '[';
    bool first_ring = true;
    for (const auto& ring : paths) {
        if (!first_ring) { std::cout << ','; }
        first_ring = false;
        std::cout << '[';
        bool first_point = true;
        for (const auto& p : ring) {
            if (!first_point) { std::cout << ','; }
            first_point = false;
            std::cout << '[' << p.x << ',' << p.y << ']';
        }
        std::cout << ']';
    }
    std::cout << ']';
}

auto capture(const std::vector<Fixture>& cases) -> void {
    std::size_t index{};
    for (const auto& item : cases) {
        for (bool cdt : {false, true}) {
            next::release_thread_caches();
            next::triangulation_request64 request{item.paths, cdt};
            const auto result = next::triangulate_checked(request);
            std::cout << "TRI_PUBLIC {\"version\":1,\"case_index\":" << index++
                      << ",\"use_delaunay\":" << (cdt ? "true" : "false") << ",\"input\":";
            paths_json(request.paths);
            std::cout << ",\"outer_error\":" << (result ? 0 : static_cast<int>(result.error()))
                      << ",\"domain_status\":" << (result ? static_cast<int>(result->status) : 1)
                      << ",\"triangles\":";
            if (result) { paths_json(result->triangles); } else { std::cout << "[]"; }
            std::cout << "}\n";
        }
    }
}

struct Sample { double real{}, cpu{}; std::size_t iterations{}; };
auto measure(const Fixture& fixture, bool cdt, bool cold, double seconds) -> Sample {
    Sample sample;
    while (sample.real < seconds * 1e9) {
        if (cold) { next::release_thread_caches(); }
        const auto cpu_start = std::clock();
        const auto start = clock_type::now();
        {
            next::triangulation_request64 request;
            request.paths = fixture.paths;
            request.use_delaunay = cdt;
            const auto result = next::triangulate(request);
            if (result.status != next::TriangulateResult::success || result.triangles.empty()) {
                throw std::runtime_error("public triangulation failed during measurement");
            }
            // The observable size and owning result lifetime prevent removal.
            if (result.triangles.size() > 1000000) { throw std::runtime_error("invalid result size"); }
        }
        sample.real += std::chrono::duration<double, std::nano>(clock_type::now() - start).count();
        sample.cpu += static_cast<double>(std::clock() - cpu_start) * 1e9 / CLOCKS_PER_SEC;
        ++sample.iterations;
    }
    sample.real /= static_cast<double>(sample.iterations);
    sample.cpu /= static_cast<double>(sample.iterations);
    return sample;
}

auto aggregate(std::vector<double> values, const std::string& kind) -> double {
    const auto mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    if (kind == "mean") { return mean; }
    if (kind == "median") {
        std::sort(values.begin(), values.end());
        return values[values.size() / 2]; // protocol fixes seven repetitions
    }
    double variance{};
    for (double value : values) { variance += (value - mean) * (value - mean); }
    const auto deviation = std::sqrt(variance / static_cast<double>(values.size() - 1));
    return kind == "cv" ? (mean > 0 ? deviation / mean : 0) : deviation;
}

auto timing(const Fixture& fixture, bool cdt, bool cold, const std::string& row, double seconds) -> void {
    next::release_thread_caches();
    if (!cold) { static_cast<void>(measure(fixture, cdt, false, 1.0)); }
    std::vector<Sample> samples;
    for (int repetition = 0; repetition < 7; ++repetition) {
        samples.push_back(measure(fixture, cdt, cold, seconds));
    }
    std::cout << std::setprecision(17) << "{\"benchmarks\":[";
    bool first = true;
    auto prefix = [&](const std::string& name, const std::string& type) {
        if (!first) { std::cout << ','; }
        first = false;
        std::cout << "{\"name\":\"" << name << "\",\"run_name\":\"" << row
                  << "\",\"run_type\":\"" << type << "\",\"time_unit\":\"ns\",\"repetitions\":7";
    };
    std::vector<double> real, cpu;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto& s = samples[i];
        prefix(row, "iteration");
        std::cout << ",\"repetition_index\":" << i << ",\"iterations\":" << s.iterations
                  << ",\"real_time\":" << s.real << ",\"cpu_time\":" << s.cpu << '}';
        real.push_back(s.real); cpu.push_back(s.cpu);
    }
    for (const std::string kind : {"mean", "median", "stddev", "cv"}) {
        prefix(row + "_" + kind, "aggregate");
        std::cout << ",\"aggregate_name\":\"" << kind << "\",\"aggregate_unit\":\""
                  << (kind == "cv" ? "percentage" : "time") << "\",\"real_time\":"
                  << aggregate(real, kind) << ",\"cpu_time\":" << aggregate(cpu, kind) << '}';
    }
    std::cout << "]}\n";
}

int main(int argc, char** argv) {
    try {
        std::string fixture, mode, row, ready, go;
        double seconds = 0.5;
        for (int i = 1; i < argc; i += 2) {
            if (i + 1 == argc) { throw std::runtime_error("missing argument value"); }
            const std::string arg = argv[i], value = argv[i + 1];
            if (arg == "--fixtures") { fixture = value; }
            else if (arg == "--mode") { mode = value; }
            else if (arg == "--row") { row = value; }
            else if (arg == "--ready-file") { ready = value; }
            else if (arg == "--go-file") { go = value; }
            else if (arg == "--seconds") { seconds = std::stod(value); }
            else { throw std::runtime_error("unknown argument"); }
        }
        const auto cases = load(fixture);
        if (!ready.empty()) {
            if (go.empty() || std::filesystem::exists(ready) || std::filesystem::exists(go)) {
                throw std::runtime_error("handshake paths must be fresh");
            }
            std::ofstream(ready) << "ready\n";
            const auto deadline = clock_type::now() + std::chrono::seconds(30);
            while (!std::filesystem::exists(go)) {
                if (clock_type::now() > deadline) { throw std::runtime_error("observer handshake timeout"); }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        if (mode == "records") { capture(cases); return 0; }
        if (mode == "placement") {
            std::vector<unsigned> observed(16);
            std::vector<std::thread> workers;
            for (std::size_t i = 0; i < observed.size(); ++i) {
                workers.emplace_back([&, i] {
#ifdef _WIN32
                    PROCESSOR_NUMBER cpu{};
                    GetCurrentProcessorNumberEx(&cpu);
                    observed[i] = cpu.Group * 64u + cpu.Number;
#else
                    observed[i] = static_cast<unsigned>(sched_getcpu());
#endif
                });
            }
            for (auto& worker : workers) { worker.join(); }
            for (auto cpu : observed) { std::cout << "WORKER_CPU " << cpu << '\n'; }
            return 0;
        }
        for (const auto& item : cases) {
            for (bool cdt : {false, true}) {
                for (bool cold : {true, false}) {
                    const auto name = item.name + (cdt ? "/cdt" : "/sweep") + (cold ? "/cold" : "/default");
                    if (mode == "list") { std::cout << name << '\n'; }
                    else if (mode == "timing" && row == name && seconds > 0 && std::isfinite(seconds)) {
                        timing(item, cdt, cold, row, seconds); return 0;
                    }
                }
            }
        }
        if (mode != "list") { throw std::runtime_error("unknown row/mode"); }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
