#pragma once

#include <Core/Reflection/Handle.hpp>
#include <Core/Containers/Array.hpp>
#include <Core/Math/Vector3.hpp>
#include <Core/Math/Quat4f.hpp>

namespace Hyperion {

class World;
class Node;

// the canyon walls are tested with rays against their meshes, the rocks as boxes around each instance
class CanyonCollision
{
public:
    void SetCanyon(const Handle<Node>& canyon);
    void FindRocks(World* world);
    void Clear();

    bool HasRocks() const
    {
        return m_rocks.Any();
    }

    bool HitsSegment(const Vec3f& from, const Vec3f& to) const;
    bool HitsRock(const Vec3f& point, float margin) const;
    float GetClearDistance(const Vec3f& from, const Vec3f& direction, float maxDistance) const;

private:
    struct Rock
    {
        Vec3f center;
        Quat4f inverseRotation;
        Vec3f halfExtents;
        float radius = 0.0f;
    };

    void AddRocks(Node* sceneRoot);
    float GetRockDistance(const Vec3f& from, const Vec3f& direction, float maxDistance) const;

    Handle<Node> m_canyon;
    Array<Rock> m_rocks;
};

} // namespace Hyperion
