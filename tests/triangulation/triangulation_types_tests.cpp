#include <gtest/gtest.h>

#include "triangulation/private/triangulation_types.h"

namespace next = clipper2next;

TEST(Clipper2NextTriangulationTypesTests, VertexStoresPoint) {
    next::internal::triangulation_vertex vertex{{1, 2}};

    EXPECT_EQ(vertex.pt.x, 1);
    EXPECT_EQ(vertex.pt.y, 2);
}
