#include <g7/asset/Procedural.hpp>

#include <doctest/doctest.h>

using namespace g7;
using namespace g7::asset;

TEST_CASE("Procedural: plane faces up with consistent tangents")
{
    const MeshData plane = makePlane(10.0f, 2.0f, Vec4(0.5f));
    REQUIRE(plane.vertices.size() == 4);
    CHECK(plane.indices.size() == 6);
    CHECK(plane.bounds.min == Vec3(-5, 0, -5));
    CHECK(plane.bounds.max == Vec3(5, 0, 5));
    CHECK(plane.materials[0].baseColor == Vec4(0.5f));
    for (const Vertex& v : plane.vertices)
    {
        CHECK(v.normal == Vec3(0, 1, 0));
        CHECK(glm::length(Vec3(v.tangent)) == doctest::Approx(1.0f));
        CHECK(glm::dot(Vec3(v.tangent), v.normal) == doctest::Approx(0.0f));
    }
    // 10 m with 2 m tiles: UVs span 0..5.
    CHECK(plane.vertices[1].uv.x == doctest::Approx(5.0f));
    // Counter-clockwise seen from above: the triangle normal points up.
    const Vec3 a = plane.vertices[plane.indices[0]].position;
    const Vec3 b = plane.vertices[plane.indices[1]].position;
    const Vec3 c = plane.vertices[plane.indices[2]].position;
    CHECK(glm::cross(b - a, c - a).y > 0.0f);
}

TEST_CASE("Procedural: box faces point outwards")
{
    const MeshData box = makeBox(Vec3(1, 2, 3));
    CHECK(box.vertices.size() == 24);
    CHECK(box.indices.size() == 36);
    CHECK(box.bounds.min == Vec3(-1, -2, -3));
    CHECK(box.bounds.max == Vec3(1, 2, 3));
    for (usize i = 0; i < box.indices.size(); i += 3)
    {
        const Vertex& a = box.vertices[box.indices[i]];
        const Vec3 b = box.vertices[box.indices[i + 1]].position;
        const Vec3 c = box.vertices[box.indices[i + 2]].position;
        const Vec3 geometric = glm::cross(b - a.position, c - a.position);
        CHECK(glm::dot(geometric, a.normal) > 0.0f);                   // winding matches the normal
        CHECK(glm::dot(a.normal, (a.position + b + c) / 3.0f) > 0.0f); // and points away from the centre
    }
}
