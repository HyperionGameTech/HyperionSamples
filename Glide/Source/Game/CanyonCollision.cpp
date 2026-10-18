#include <Game/CanyonCollision.hpp>

#include <Scene/World.hpp>
#include <Scene/Scene.hpp>
#include <Scene/Node.hpp>
#include <Scene/Instancing/InstanceGroup.hpp>

#include <Core/Math/Ray.hpp>
#include <Core/Math/MathUtil.hpp>

namespace Hyperion {

namespace {

// rocks are rounded, so their boxes are drawn in a little
constexpr float RockBoxShrink = 0.8f;

} // namespace

void CanyonCollision::SetCanyon(const Handle<Node>& canyon)
{
    m_canyon = canyon;
}

void CanyonCollision::FindRocks(World* world)
{
    m_rocks.Clear();

    if (world == nullptr)
    {
        return;
    }

    for (const Handle<Scene>& scene : world->GetScenes())
    {
        if (scene.IsValid() && scene->IsForegroundScene())
        {
            AddRocks(scene->GetRoot().Get());
        }
    }
}

void CanyonCollision::Clear()
{
    m_canyon.Reset();
    m_rocks.Clear();
}

void CanyonCollision::AddRocks(Node* sceneRoot)
{
    if (sceneRoot == nullptr)
    {
        return;
    }

    for (const Handle<Node>& child : sceneRoot->GetChildren())
    {
        if (!child.IsValid() || !child->IsA<InstanceGroup>())
        {
            continue;
        }

        InstanceGroup* group = static_cast<InstanceGroup*>(child.Get());
        const BoundingBox& bounds = group->GetPrefabBounds();

        // an instance group only knows its bounds after its first update, so this can come up empty early on
        if (!bounds.IsValid() || bounds.IsZero())
        {
            continue;
        }

        Array<Transform> transforms;
        group->GetInstanceTransforms(transforms);

        for (const Transform& transform : transforms)
        {
            Rock rock;
            rock.center = transform.GetTranslation() + transform.GetRotation().RotateVector(bounds.GetCenter() * transform.GetScale());
            rock.inverseRotation = transform.GetRotation().Inverse();
            rock.halfExtents = bounds.GetExtent() * transform.GetScale() * (0.5f * RockBoxShrink);
            rock.radius = rock.halfExtents.Length();

            m_rocks.PushBack(rock);
        }
    }
}

bool CanyonCollision::HitsSegment(const Vec3f& from, const Vec3f& to) const
{
    const Vec3f offset = to - from;
    const float distance = offset.Length();

    if (distance < 0.0001f)
    {
        return false;
    }

    return GetClearDistance(from, offset / distance, distance) < distance;
}

bool CanyonCollision::HitsRock(const Vec3f& point, float margin) const
{
    for (const Rock& rock : m_rocks)
    {
        const Vec3f offset = point - rock.center;

        if (offset.Length() > rock.radius + margin)
        {
            continue;
        }

        const Vec3f local = rock.inverseRotation.RotateVector(offset);

        const bool insideX = MathUtil::Abs(local.x) < rock.halfExtents.x + margin;
        const bool insideY = MathUtil::Abs(local.y) < rock.halfExtents.y + margin;
        const bool insideZ = MathUtil::Abs(local.z) < rock.halfExtents.z + margin;

        if (insideX && insideY && insideZ)
        {
            return true;
        }
    }

    return false;
}

float CanyonCollision::GetClearDistance(const Vec3f& from, const Vec3f& direction, float maxDistance) const
{
    float closest = GetRockDistance(from, direction, maxDistance);

    if (!m_canyon.IsValid())
    {
        return closest;
    }

    RayTestResults results;

    if (!m_canyon->TestRay(Ray { from, direction }, results))
    {
        return closest;
    }

    for (const RayHit& hit : results)
    {
        if (!hit.isApproximate && hit.distance < closest)
        {
            closest = hit.distance;
        }
    }

    return closest;
}

float CanyonCollision::GetRockDistance(const Vec3f& from, const Vec3f& direction, float maxDistance) const
{
    float closest = maxDistance;

    for (const Rock& rock : m_rocks)
    {
        const Vec3f offset = from - rock.center;
        const float along = -offset.Dot(direction);

        const bool behind = along + rock.radius < 0.0f;
        const bool beyond = along - rock.radius > closest;
        const bool missesSphere = (offset + direction * along).Length() > rock.radius;

        if (behind || beyond || missesSphere)
        {
            continue;
        }

        // slab test against the rock's box, in the rock's own space
        const Vec3f localFrom = rock.inverseRotation.RotateVector(offset);
        const Vec3f localDirection = rock.inverseRotation.RotateVector(direction);

        float enter = 0.0f;
        float exit = closest;

        for (int axis = 0; axis < 3 && enter <= exit; axis++)
        {
            if (MathUtil::Abs(localDirection[axis]) < 1e-6f)
            {
                if (MathUtil::Abs(localFrom[axis]) > rock.halfExtents[axis])
                {
                    enter = exit + 1.0f;
                }

                continue;
            }

            const float t0 = (-rock.halfExtents[axis] - localFrom[axis]) / localDirection[axis];
            const float t1 = (rock.halfExtents[axis] - localFrom[axis]) / localDirection[axis];

            enter = MathUtil::Max(enter, MathUtil::Min(t0, t1));
            exit = MathUtil::Min(exit, MathUtil::Max(t0, t1));
        }

        if (enter <= exit)
        {
            closest = enter;
        }
    }

    return closest;
}

} // namespace Hyperion
