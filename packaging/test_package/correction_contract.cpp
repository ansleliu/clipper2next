#include "correction_contract.h"
#include <clipper2next/clip.h>
#include <clipper2next/geometry.h>
#include <clipper2next/offset.h>
#include <clipper2next/offset/builder.h>
#include <clipper2next/rectclip.h>
#include <clipper2next/triangulation.h>
#include <clipper2next/version.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cfenv>
#include <bit>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#else
#include <link.h>
#endif

namespace {
namespace n = clipper2next;
void require(bool condition, const char* contract) {
    if (!condition) throw std::runtime_error(contract);
}
void modules() {
    std::vector<std::filesystem::path> paths;
#ifdef _WIN32
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    require(snapshot != INVALID_HANDLE_VALUE, "module enumeration");
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    if (Module32FirstW(snapshot, &module)) do {
        if (_wcsicmp(module.szModule, L"clipper2next.dll") == 0) paths.emplace_back(module.szExePath);
    } while (Module32NextW(snapshot, &module));
    CloseHandle(snapshot);
    std::array<wchar_t, 32768> executable{};
    require(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())) != 0, "executable path");
    const auto exe = std::filesystem::canonical(executable.data());
#else
    dl_iterate_phdr([](dl_phdr_info* info, std::size_t, void* opaque) {
        const std::filesystem::path path{info->dlpi_name};
        if (path.filename().string().starts_with("libclipper2next.so"))
            static_cast<std::vector<std::filesystem::path>*>(opaque)->push_back(path);
        return 0;
    }, &paths);
    const auto exe = std::filesystem::canonical("/proc/self/exe");
#endif
    require(paths.size() == 1, "exactly one actual loaded Clipper product");
#ifdef _WIN32
    std::array<char,128> runBuffer{};
    GetEnvironmentVariableA("CLIPPER2NEXT_CORRECTION_RUN_ID",runBuffer.data(),static_cast<DWORD>(runBuffer.size()));
    const auto* run = runBuffer[0] ? runBuffer.data() : nullptr;
#else
    const auto* run = std::getenv("CLIPPER2NEXT_CORRECTION_RUN_ID");
#endif
    std::cout << "correction_run_id=" << (run ? run : "unbound") << '\n'
              << "correction_executable=" << exe.string() << '\n'
              << "correction_header_version=" << CLIPPER2NEXT_VERSION << '\n'
              << "correction_module_clipper=" << std::filesystem::canonical(paths.front()).string() << '\n' << std::flush;
}
struct runtime_capture {
    runtime_capture() {
#ifdef _WIN32
        if (GetEnvironmentVariableA("CLIPPER2NEXT_CAPTURE_RUNTIME",nullptr,0) == 0) return;
#else
        if (!std::getenv("CLIPPER2NEXT_CAPTURE_RUNTIME")) return;
#endif
        modules();
    }
};
const runtime_capture capture;
void arguments() {
    n::clip_request64 clip;
    clip.clip_type = static_cast<n::ClipType>(255);
    const auto invalid = n::clip_checked(clip);
    require(!invalid && invalid.error() == n::clipper_error_code::invalid_argument, "invalid enum precedes empty shortcut");
    n::borrowed_offset_request64 borrowed;
    struct observed_source {
        n::Paths64 paths{{{0,0},{10,0},{10,10},{0,10}}};
        mutable int reads{};
        auto begin() const { return paths.begin(); }
        auto end() const { return paths.end(); }
        auto size() const { ++reads; return paths.size(); }
    } source;
    const n::borrowed_offset_group64 group{n::borrow_paths64(source)};
    borrowed.groups=std::span{&group,1U};
    borrowed.delta = std::numeric_limits<double>::quiet_NaN();
    borrowed.limits.maximum_input_point_count = 0;
    const auto bad = n::offset_stage_checked(borrowed);
    require(!bad && bad.error() == n::clipper_error_code::invalid_argument && source.reads==0, "borrowed argument priority");
    borrowed.delta=1;
    borrowed.limits.maximum_input_point_count=100;
    const auto measured=n::offset_stage_checked(borrowed);
    require(measured && source.reads>0 && measured->paths.size()==1 && measured->stats.input_point_count==4,
            "matching installed measurement thunk ABI");
    n::rect_clip_request64 rect;
    rect.paths = {{{(std::numeric_limits<int64_t>::max)(), 0}}};
    const auto range = n::rect_clip_checked(rect);
    require(!range && range.error() == n::clipper_error_code::coordinate_range, "empty rectangle still validates coordinates");
}
void offsets() {
    const n::Path64 square{{0,0},{100,0},{100,100},{0,100}};
    for (const bool first : {false, true}) {
        auto builder = n::offset_builder{}.delta(-10).join(n::JoinType::Miter);
        if (first) builder.add(n::Path64{{300,300}});
        builder.add(square);
        if (!first) builder.add(n::Path64{{300,300}});
        require(n::area(builder.execute()) == 6400, "negative offset with degenerate group");
    }
    for (const bool clockwise : {false, true}) for (const bool line_first : {false, true}) {
        auto ring = square;
        if (clockwise) std::ranges::reverse(ring);
        auto builder = n::offset_builder{}.delta(10).join(n::JoinType::Miter);
        if (line_first) builder.end(n::EndType::Butt).add(n::Path64{{200,0},{300,0}});
        builder.end(n::EndType::Polygon).add(ring);
        if (!line_first) builder.end(n::EndType::Butt).add(n::Path64{{200,0},{300,0}});
        const auto result = builder.execute();
        require(result.size() == 2 && n::area(result) == (clockwise ? -16400 : 16400), "mixed group area and direction");
        for (const auto& path : result) require((n::area(path) < 0) == clockwise, "each final shell direction");
    }
    n::PolyTree64 tree;
    n::offset_builder{}.delta(10).join(n::JoinType::Miter).add(n::Paths64{
        {{0,0},{200,0},{200,200},{0,200}}, {{50,50},{50,150},{150,150},{150,50}},
        {{80,80},{120,80},{120,120},{80,120}}}).execute_into(tree);
    require(tree.count() == 1 && tree.area() == 45600, "offset tree area");
    const auto shell = tree.child(tree.root(), 0);
    require(tree.count(shell) == 1, "offset hole owner");
    const auto hole = tree.child(shell, 0);
    require(tree.is_hole(hole) && tree.count(hole) == 1, "offset island owner");
    require(!tree.is_hole(tree.child(hole, 0)), "offset island parity");
    n::offset_request64 round;
    round.paths = {{{0,0}}}; round.delta = 2.5;
    round.join_type = n::JoinType::Round; round.arc_segments_per_quadrant = 1;
    round.coordinate_rounding = geotypes::CoordinateRounding::NearestEven;
    const auto result = n::offset(round).closed;
    require(result.size() == 1 && result.front().size() == 4, "round singleton cardinality");
    for (const auto p : n::Path64{{2,0},{0,2},{-2,0},{0,-2}})
        require(std::ranges::find(result.front(), p) != result.front().end(), "nearest even axis point");
}
struct failing_writer {
    bool cancelled{};
    n::clipper_error_code begin(const n::topology_layout64&) { return n::clipper_error_code::ok; }
    n::clipper_error_code acquire(const n::topology_ring_layout64&, std::span<n::Point64>&) { return n::clipper_error_code::sink_failure; }
    n::clipper_error_code finish() { return n::clipper_error_code::ok; }
    void cancel() noexcept { cancelled = true; }
};
void ownership() {
    n::clip_request64 request;
    request.clip_type = n::ClipType::Union; request.fill_rule = n::FillRule::NonZero;
    request.subjects = {{{0,0},{10,0},{10,10},{0,10}}};
    request.options = {.preserve_collinear=false, .reverse_solution=true,
                       .intersection_policy={n::precision_mode::precise}};
    auto source = n::prepare_clip_request(request);
    const auto moved = std::move(source);
    const auto& r = source.request(); const auto& m = source.metadata();
    require(r.clip_type == n::ClipType::NoClip && r.fill_rule == n::FillRule::EvenOdd &&
        r.options.preserve_collinear && !r.options.reverse_solution &&
        r.options.intersection_policy.mode == n::precision_mode::fast &&
        r.subjects.empty() && r.clips.empty() && r.open_subjects.empty() &&
        !m.single_subject_rect && !m.single_clip_rect && !m.subject_path_count &&
        !m.clip_path_count && !m.open_subject_path_count && !m.subject_point_count &&
        !m.clip_point_count && !m.open_subject_point_count && n::clip(source).closed.empty(), "moved-from complete defaults");
    require(n::area(n::clip(moved).closed) == -100, "moved-to options retained");
    auto rectangle=n::prepare_rect_clip_request({{0,0,4,4},request.subjects});
    const auto rectangleOwner=std::move(rectangle);
    require(rectangle.request().rect==n::Rect64{} && rectangle.request().paths.empty() &&
        n::rect_clip(rectangle).paths.empty() && n::area(n::rect_clip(rectangleOwner).paths)==16,
        "moved-from rectangle complete defaults");
    n::borrowed_clip_request64 borrowed;
    borrowed.clip_type = n::ClipType::Union; borrowed.fill_rule = n::FillRule::NonZero;
    borrowed.subjects = n::borrow_paths64(request.subjects);
    failing_writer writer;
    const auto result = n::clip_topology_checked(borrowed, n::make_topology_writer64(writer));
    require(!result && result.error() == n::clipper_error_code::sink_failure && writer.cancelled, "writer failure cancels");
}
void mesh_constraints(const n::Paths64& rings, const n::Paths64& triangles) {
    using edge = std::array<int64_t,4>;
    const auto key=[](n::Point64 a,n::Point64 b) {
        if (a.x>b.x || (a.x==b.x && a.y>b.y)) std::swap(a,b);
        return edge{a.x,a.y,b.x,b.y};
    };
    std::map<edge,int> boundary,actual;
    for (const auto& ring:rings) for (std::size_t i=0;i<ring.size();++i)
        ++boundary[key(ring[i],ring[(i+1)%ring.size()])];
    for (const auto& triangle:triangles) {
        require(triangle.size()==3 && n::area(triangle)>0,"positive nondegenerate final triangle");
        for (std::size_t i=0;i<3;++i) {
            require(std::ranges::any_of(rings,[&](const auto& ring) {
                return std::ranges::find(ring,triangle[i])!=ring.end();
            }),"mesh uses only declared vertices");
            ++actual[key(triangle[i],triangle[(i+1)%3])];
        }
    }
    for (const auto& [segment,count]:boundary)
        require(count==1 && actual[segment]==1,"every boundary constraint occurs exactly once");
    for (const auto& [segment,count]:actual)
        require(count==(boundary.contains(segment)?1:2),"interior edge paired exactly twice");
    // These fixed fixtures have |coordinate| <= 2^27: signed 64-bit cross products are exact.
    const auto orient=[](int64_t ax,int64_t ay,int64_t bx,int64_t by,int64_t cx,int64_t cy) {
        const auto value=(bx-ax)*(cy-ay)-(by-ay)*(cx-ax);
        return (value>0)-(value<0);
    };
    for (const auto& [a,unusedA]:actual) for (const auto& [b,unusedB]:actual) {
        (void)unusedA; (void)unusedB;
        const auto a1=orient(a[0],a[1],a[2],a[3],b[0],b[1]);
        const auto a2=orient(a[0],a[1],a[2],a[3],b[2],b[3]);
        const auto b1=orient(b[0],b[1],b[2],b[3],a[0],a[1]);
        const auto b2=orient(b[0],b[1],b[2],b[3],a[2],a[3]);
        require(!(a1*a2<0 && b1*b2<0),"final mesh edges do not cross");
    }
}
void triangulation() {
    constexpr int64_t side = int64_t{1} << 27;
    n::triangulation_request64 quad;
    quad.use_delaunay = true;
    quad.paths = {{{-side,0},{0,-side},{side,1},{0,side}}};
    const auto result = n::triangulate(quad);
    require(result.status == n::TriangulateResult::success && result.triangles.size() == 2, "final CDT quadrilateral");
    mesh_constraints(quad.paths,result.triangles);
    int diagonal = 0;
    for (const auto& tri : result.triangles) for (std::size_t i=0; i<tri.size(); ++i) {
        const auto a=tri[i], b=tri[(i+1)%tri.size()];
        if ((a == n::Point64{0,-side} && b == n::Point64{0,side}) ||
            (b == n::Point64{0,-side} && a == n::Point64{0,side})) ++diagonal;
    }
    require(diagonal == 2, "exact near-cocircular final inner edge");
    quad.paths = {{{0,0},{5,0},{5,5},{0,5}}, {{2,2},{2,3},{3,3},{3,2}}};
    const auto hole = n::triangulate(quad);
    require(hole.status == n::TriangulateResult::success && hole.triangles.size() == 8, "unit hole triangle count");
    mesh_constraints(quad.paths,hole.triangles);
    double area = 0;
    for (const auto& tri : hole.triangles) {
        require(tri.size() == 3, "triangle cardinality");
        area += std::abs(n::area(tri));
        const double x=(tri[0].x+tri[1].x+tri[2].x)/3.0, y=(tri[0].y+tri[1].y+tri[2].y)/3.0;
        require(!(x>2 && x<3 && y>2 && y<3), "hole stays empty");
    }
    require(area == 24, "unit hole exact final area");
}
void binary64_boundaries() {
    struct rounding_guard {
        int saved{std::fegetround()};
        rounding_guard() { require(saved != -1 && std::fesetround(FE_TONEAREST) == 0, "binary64 rounding precondition"); }
        ~rounding_guard() { std::fesetround(saved); }
    } guard;
    struct row { int precision; double input; double other; double projected; double scale; };
    // Independent binary64 lattice derivation with legacy nearest-away scaling.
    // Literals are complete expected vertices, including both half-grid signs.
    const std::array rows{
        row{0, 0x1.fffffffffffffp-2, 0x1.0000000000000p+2, 0x0.0p+0, 0x1.0000000000000p+0},
        row{0, 0x1.0000000000000p-1, 0x1.0000000000000p+2, 0x1.0000000000000p+0, 0x1.0000000000000p+0},
        row{0, 0x1.0000000000001p-1, 0x1.0000000000000p+2, 0x1.0000000000000p+0, 0x1.0000000000000p+0},
        row{0, -0x1.0000000000001p-1, 0x1.0000000000000p+2, -0x1.0000000000000p+0, 0x1.0000000000000p+0},
        row{0, -0x1.0000000000000p-1, 0x1.0000000000000p+2, -0x1.0000000000000p+0, 0x1.0000000000000p+0},
        row{0, -0x1.fffffffffffffp-2, 0x1.0000000000000p+2, 0x0.0p+0, 0x1.0000000000000p+0},
        row{0, 0x1.7ffffffffffffp+0, 0x1.0000000000000p+2, 0x1.0000000000000p+0, 0x1.0000000000000p+0},
        row{0, 0x1.8000000000000p+0, 0x1.0000000000000p+2, 0x1.0000000000000p+1, 0x1.0000000000000p+0},
        row{0, 0x1.8000000000001p+0, 0x1.0000000000000p+2, 0x1.0000000000000p+1, 0x1.0000000000000p+0},
        row{0, -0x1.8000000000001p+0, 0x1.0000000000000p+2, -0x1.0000000000000p+1, 0x1.0000000000000p+0},
        row{0, -0x1.8000000000000p+0, 0x1.0000000000000p+2, -0x1.0000000000000p+1, 0x1.0000000000000p+0},
        row{0, -0x1.7ffffffffffffp+0, 0x1.0000000000000p+2, -0x1.0000000000000p+0, 0x1.0000000000000p+0},
        row{2, 0x1.47ae147ae147ap-8, 0x1.47ae147ae147bp-5, 0x0.0p+0, 0x1.9000000000000p+6},
        row{2, 0x1.47ae147ae147bp-8, 0x1.47ae147ae147bp-5, 0x1.47ae147ae147bp-7, 0x1.9000000000000p+6},
        row{2, 0x1.47ae147ae147cp-8, 0x1.47ae147ae147bp-5, 0x1.47ae147ae147bp-7, 0x1.9000000000000p+6},
        row{2, -0x1.47ae147ae147cp-8, 0x1.47ae147ae147bp-5, -0x1.47ae147ae147bp-7, 0x1.9000000000000p+6},
        row{2, -0x1.47ae147ae147bp-8, 0x1.47ae147ae147bp-5, -0x1.47ae147ae147bp-7, 0x1.9000000000000p+6},
        row{2, -0x1.47ae147ae147ap-8, 0x1.47ae147ae147bp-5, 0x0.0p+0, 0x1.9000000000000p+6},
        row{-1, 0x1.dffffffffffffp+3, 0x1.4000000000000p+5, 0x1.4000000000000p+4, 0x1.999999999999ap-4},
        row{-1, 0x1.e000000000000p+3, 0x1.4000000000000p+5, 0x1.4000000000000p+4, 0x1.999999999999ap-4},
        row{-1, 0x1.e000000000001p+3, 0x1.4000000000000p+5, 0x1.4000000000000p+4, 0x1.999999999999ap-4},
        row{-1, -0x1.e000000000001p+3, 0x1.4000000000000p+5, -0x1.4000000000000p+4, 0x1.999999999999ap-4},
        row{-1, -0x1.e000000000000p+3, 0x1.4000000000000p+5, -0x1.4000000000000p+4, 0x1.999999999999ap-4},
        row{-1, -0x1.dffffffffffffp+3, 0x1.4000000000000p+5, -0x1.4000000000000p+4, 0x1.999999999999ap-4},
    };
    for (const auto& r : rows) for (const bool cdt : {false,true}) {
        require(std::bit_cast<uint64_t>(std::pow(10.0,r.precision)) == std::bit_cast<uint64_t>(r.scale), "binary64 scale bits");
        n::triangulation_requestd request;
        request.decimal_precision = r.precision; request.use_delaunay = cdt;
        request.paths = {{{r.input,0},{r.other,0},{r.input,r.other}}};
        const auto result = n::triangulate_checked(request);
        require(result && result->status == n::TriangulateResult::success && result->triangles.size() == 1 && result->triangles[0].size() == 3, "binary64 final triangle");
        const n::PathD expected{{r.projected,0},{r.other,0},{r.projected,r.other}};
        for (const auto point : expected) {
            require(std::ranges::any_of(result->triangles[0], [&](const auto actual) {
                return std::bit_cast<uint64_t>(actual.x) == std::bit_cast<uint64_t>(point.x) &&
                       std::bit_cast<uint64_t>(actual.y) == std::bit_cast<uint64_t>(point.y);
            }), "binary64 complete projected vertex bits");
        }
        require(n::area(result->triangles[0]) > 0, "binary64 positive orientation");
    }
    for (const double sign : {-1.0,1.0}) {
        n::triangulation_requestd request;
        request.decimal_precision=0;
        const double edge=sign*0x1.ffffffffffff0p60;
        request.paths={{{edge,0},{edge-sign*1024,0},{edge,1024}}};
        const auto admitted=n::triangulate_checked(request);
        require(admitted && admitted->status==n::TriangulateResult::success && admitted->triangles.size()==1, "binary64 guard inclusive");
        request.paths[0][0].x=sign*0x1.ffffffffffff1p60;
        const auto rejected=n::triangulate_checked(request);
        require(!rejected && rejected.error()==n::clipper_error_code::coordinate_range, "binary64 guard first neighbour");
    }
}
}
auto run_correction_contract() -> int {
    try {
        modules(); arguments(); offsets(); ownership(); triangulation(); binary64_boundaries();
        std::cout << "correction_contract=PASS\ncorrection_cases=61\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "correction_contract=FAIL " << error.what() << '\n';
        return 1;
    }
}
