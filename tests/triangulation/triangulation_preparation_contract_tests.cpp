#include <gtest/gtest.h>
#include "triangulation/private/triangulation_monotone.h"
#include <algorithm>

namespace next=clipper2next;
namespace in=next::internal;
TEST(Clipper2NextMonotonePreparationTests, SupportingCornerKeepsDirectionWithCollinearSidesAndWideCoordinates) {
    const next::Path64 original{{0,0},{4,0},{8,0},{8,4},{8,8},{4,8},{0,8},{0,4}};
    for(int64_t scale:{int64_t{1},int64_t{1}<<20,int64_t{1}<<57})
    for(std::size_t start=0;start<original.size();++start)for(bool reverse:{false,true}) {
        auto path=original;
        for(auto& point:path){point.x=point.x*scale+(int64_t{1}<<59);point.y=point.y*scale-(int64_t{1}<<59);}
        std::rotate(path.begin(),path.begin()+static_cast<std::ptrdiff_t>(start),path.end());
        if(reverse)std::reverse(path.begin(),path.end());
        const auto domain=in::triangulation_predicate_domain::from_points(path.size(),[&](auto i){return path[i];});
        in::monotone_workspace workspace;
        const auto layout=in::prepare_monotone_layout(path,domain,workspace);
        ASSERT_TRUE(layout);EXPECT_EQ(layout->direction,reverse?-1:1);EXPECT_EQ(layout->order.size(),path.size());
    }
}
TEST(Clipper2NextMonotonePreparationTests, ConvexSupportingTurnStillRequiresCompleteRingCertificate) {
    const next::Paths64 invalid{
        {{0,0},{4,4},{0,4},{4,0}},
        {{0,0},{4,0},{4,0},{4,4},{0,4}},
        {{0,0},{4,0},{2,0},{4,4},{0,4}}};
    for(const auto& original:invalid)for(std::size_t start=0;start<original.size();++start)for(bool reverse:{false,true}) {
        auto path=original;std::rotate(path.begin(),path.begin()+static_cast<std::ptrdiff_t>(start),path.end());
        if(reverse)std::reverse(path.begin(),path.end());
        const auto domain=in::triangulation_predicate_domain::from_points(path.size(),[&](auto i){return path[i];});
        in::monotone_workspace workspace;
        EXPECT_FALSE(in::prepare_monotone_layout(path,domain,workspace));
    }
}
