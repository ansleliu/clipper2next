#include "triangulation/private/triangulation_partition.h"
#include "triangulation/private/triangulation_boundary.h"
#include "triangulation/private/triangulation_delaunay.h"
#include "triangulation/private/triangulation_result_builder.h"
#include <iostream>
#include <stdexcept>

namespace in = clipper2next::internal;
using namespace clipper2next;

auto require(bool value) -> void {
    if (!value) { throw std::runtime_error{"partition graph contract failed"}; }
}
auto area2(std::span<const Point64> path) -> std::int64_t {
    std::int64_t value = 0;
    for (std::size_t i = 0; i != path.size(); ++i) {
        const auto a = path[i], b = path[(i + 1) % path.size()];
        value += a.x * b.y - a.y * b.x;
    }
    return value;
}
auto rectangle(std::int64_t x, std::int64_t y, std::int64_t width,
               std::int64_t height, bool hole = false) -> Path64 {
    Path64 path{{x,y},{x+width,y},{x+width,y+height},{x,y+height}};
    if (hole) { std::reverse(path.begin(),path.end()); }
    return path;
}
auto exercise(Paths64 paths, bool delaunay, bool accepted) -> void {
    in::triangulation_context context;
    context.use_delaunay = delaunay;
    require(in::build_triangulation_boundary(context,paths)==TriangulateResult::success);
    const auto count = context.vertices.size();
    const auto domain=in::prepare_triangulation_predicate_domain(context);
    const auto source_edges=context.edges.size();
    require(in::try_build_partitioned_triangulation_graph(context,domain)==accepted);
    if (!accepted) {
        require(context.vertices.size()==count && context.edges.size()==source_edges);
        require(context.triangles.empty());
        return;
    }
    require(context.vertices.size()==count);
    require(in::legalize_pending_delaunay_edges(context,domain));
    const auto mesh=in::validate_triangulation_mesh(context,domain);
    const auto triangles=in::build_triangulation_result(mesh);
    std::int64_t expected=0;
    for (std::size_t ring=0;ring!=context.source_paths.size();++ring) { expected+=area2(context.source_paths[ring]); }
    std::int64_t actual=0;
    for (const auto& triangle:triangles) { actual+=std::abs(area2(triangle)); }
    require(actual==expected);
}
auto main() -> int {
    try {
        const Paths64 hole{rectangle(0,0,40,40),rectangle(10,10,20,20,true)};
        const Paths64 nested{rectangle(0,0,100,100),rectangle(10,10,80,80,true),
                             rectangle(20,20,60,60),rectangle(30,30,40,40,true)};
        const Paths64 disjoint{rectangle(0,0,20,20),rectangle(30,0,20,20)};
        for (const auto& paths:{hole,nested,disjoint}) {
            for (bool delaunay:{false,true}) { exercise(paths,delaunay,true); }
        }
        Paths64 many{rectangle(0,0,180,180)};
        for (std::size_t i=0;i!=64;++i) {
            many.push_back(rectangle(10+static_cast<std::int64_t>(i%8)*20,
                                    10+static_cast<std::int64_t>(i/8)*20,8,8,true));
        }
        exercise(many,false,true);
        exercise(many,true,true);
        exercise({rectangle(0,0,10,10),{{0,5},{2,7},{2,3}}},false,false);
        std::cout<<"partition-graph-contract=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
