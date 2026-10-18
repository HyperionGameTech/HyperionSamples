#include <Game/PaperPlane.hpp>

#include <Scene/Node.hpp>
#include <Scene/Entity.hpp>
#include <Scene/EntityManager.hpp>
#include <Scene/Components/MeshComponent.hpp>

#include <Rendering/Mesh.hpp>
#include <Rendering/Material.hpp>
#include <Rendering/Vertex.hpp>

#include <Core/Math/Quat4f.hpp>

#include <cmath>

namespace Hyperion {

namespace {

// the nose points down +Z and the right wing along +X
const Vec3f Nose(0.0f, 0.0f, 3.1f);
const Vec3f KeelMiddle(0.0f, -0.34f, 0.4f);
const Vec3f KeelTail(0.0f, -0.68f, -2.78f);

constexpr float TailZ = -2.9f;
constexpr float FlapDepth = 0.45f;
constexpr float WindResponse = 3.0f;

// centres the plane on its flight position
const Vec3f BodyOffset(0.0f, 0.0f, 0.3f);

Vec3f SpineTail(float side)
{
    return Vec3f(side * 0.05f, 0.0f, TailZ);
}

Vec3f CreaseTail(float side)
{
    return Vec3f(side * 0.75f, 0.07f, TailZ);
}

Vec3f WingTip(float side)
{
    return Vec3f(side * 1.75f, 0.3f, TailZ);
}

Vec3f Lerp(const Vec3f& from, const Vec3f& to, float t)
{
    return from + (to - from) * t;
}

Handle<Mesh> BuildPaperMesh(Name name, const Array<Vec3f>& triangles, const Vec3f& origin)
{
    Array<SimpleVertex> vertices;
    Array<uint32> indices;

    for (size_t index = 0; index + 2 < triangles.Size(); index += 3)
    {
        const Vec3f& a = triangles[index];
        const Vec3f& b = triangles[index + 1];
        const Vec3f& c = triangles[index + 2];

        const Vec3f normal = (c - a).Cross(b - a).Normalized();

        // paper has two sides, so each triangle is added facing both ways
        const Vec3f corners[] = { a, b, c, a, c, b };

        for (uint32 corner = 0; corner < 6; corner++)
        {
            const Vec3f& point = corners[corner];
            const Vec3f cornerNormal = corner < 3 ? normal : -normal;
            const Vec2f uv(point.x / 3.6f + 0.5f, point.z / 6.0f + 0.5f);

            indices.PushBack(uint32(vertices.Size()));
            vertices.PushBack(SimpleVertex { point - origin, cornerNormal, uv });
        }
    }

    MeshDesc meshDesc {};
    meshDesc.meshAttributes.inputLayout = { VT_Simple };
    meshDesc.lods[0].numIndices = uint32(indices.Size());
    meshDesc.lods[0].numVertices = uint32(vertices.Size());

    VertexArrayView vertexData {};
    vertexData.floatData = reinterpret_cast<const float*>(vertices.Data());
    vertexData.layoutDesc = meshDesc.meshAttributes.inputLayout;
    vertexData.vertexCount = vertices.Size();

    MeshDataView meshData {};
    meshData.vertices[0] = vertexData;
    meshData.indices[0] = ConstByteView(
        reinterpret_cast<const ubyte*>(indices.Data()),
        reinterpret_cast<const ubyte*>(indices.Data() + indices.Size()));

    Handle<Mesh> mesh = MakeHandle<Mesh>();
    mesh->SetName(name);
    mesh->SetMeshData(meshDesc, meshData);
    InitObject(mesh);

    return mesh;
}

} // namespace

bool PaperPlane::Create(Node* planeNode)
{
    if (planeNode == nullptr)
    {
        return false;
    }

    for (const Handle<Node>& child : planeNode->GetChildren())
    {
        if (!child.IsValid() || !child->IsA<Entity>())
        {
            continue;
        }

        Entity* entity = static_cast<Entity*>(child.Get());
        MeshComponent* meshComponent = entity->TryGetComponent<MeshComponent>();

        if (meshComponent != nullptr && meshComponent->material.IsValid())
        {
            m_importedModel = MakeStrongRef(entity);
            m_material = meshComponent->material;

            break;
        }
    }

    if (!m_material.IsValid())
    {
        return false;
    }

    // the imported model stays in the scene, shrunk out of sight while the folded plane flies
    m_importedModel->SetLocalScale(Vec3f(0.001f), TransformChangeType::Simulation);

    CreateBody(planeNode);
    CreateWing(-1.0f);
    CreateWing(1.0f);

    return true;
}

void PaperPlane::Destroy()
{
    if (m_body.IsValid())
    {
        m_body->Remove();
    }

    if (m_importedModel.IsValid())
    {
        m_importedModel->SetLocalScale(Vec3f::One(), TransformChangeType::Simulation);
    }

    *this = PaperPlane();
}

void PaperPlane::CreateBody(Node* planeNode)
{
    Array<Vec3f> triangles;

    for (float side : { -1.0f, 1.0f })
    {
        const Vec3f spineTail = SpineTail(side);
        const Vec3f creaseTail = CreaseTail(side);

        triangles.Concat(Array<Vec3f> {
            Nose, KeelMiddle, spineTail,
            KeelMiddle, KeelTail, spineTail,
            Nose, spineTail, creaseTail });
    }

    m_body = AddPart(NAME("PaperPlaneBody"), planeNode, triangles, Vec3f::Zero(), BodyOffset);
}

void PaperPlane::CreateWing(float side)
{
    const bool isRight = side > 0.0f;

    const Vec3f creaseTail = CreaseTail(side);
    const Vec3f wingTip = WingTip(side);

    // the outer wing is flat, so a point on it is found from x and z alone
    const Vec3f wingNormal = (creaseTail - Nose).Cross(wingTip - Nose);

    const auto pointOnWing = [&wingNormal](float x, float z)
    {
        const float y = Nose.y - (wingNormal.x * (x - Nose.x) + wingNormal.z * (z - Nose.z)) / wingNormal.y;

        return Vec3f(x, y, z);
    };

    const Vec3f wingletFront = Lerp(Nose, wingTip, 1.45f / 1.75f);
    const Vec3f wingletBack = Lerp(creaseTail, wingTip, 0.7f);
    const Vec3f wingletTop = wingletBack + Vec3f(side * 0.02f, 0.3f, 0.0f);

    const Vec3f flapInnerTail = Lerp(creaseTail, wingTip, 0.17f);
    const Vec3f flapOuterTail = Lerp(creaseTail, wingTip, 0.63f);
    const Vec3f flapInnerHinge = pointOnWing(flapInnerTail.x, flapInnerTail.z + FlapDepth);
    const Vec3f flapOuterHinge = pointOnWing(flapOuterTail.x, flapOuterTail.z + FlapDepth);

    const Array<Vec3f> wingTriangles {
        Nose, creaseTail, flapInnerTail,
        Nose, flapInnerTail, flapInnerHinge,
        Nose, flapInnerHinge, flapOuterHinge,
        Nose, flapOuterHinge, wingletFront,
        flapOuterHinge, flapOuterTail, wingletBack,
        flapOuterHinge, wingletBack, wingletFront
    };

    const Array<Vec3f> wingletTriangles {
        wingletFront, wingletBack, wingletTop
    };

    const Array<Vec3f> flapTriangles {
        flapInnerHinge, flapInnerTail, flapOuterTail,
        flapInnerHinge, flapOuterTail, flapOuterHinge
    };

    Fold wing;
    wing.entity = AddPart(isRight ? NAME("PaperPlaneWingR") : NAME("PaperPlaneWingL"), m_body.Get(), wingTriangles, Nose, Nose);
    wing.hingeAxis = (creaseTail - Nose).Normalized();
    wing.flutterAngle = 0.035f;
    wing.flutterSpeed = 9.0f;
    wing.phase = side * 1.7f;

    Fold winglet;
    winglet.entity = AddPart(isRight ? NAME("PaperPlaneWingletR") : NAME("PaperPlaneWingletL"), wing.entity.Get(), wingletTriangles, wingletFront, wingletFront - Nose);
    winglet.hingeAxis = (wingletBack - wingletFront).Normalized();
    winglet.flutterAngle = 0.12f;
    winglet.flutterSpeed = 15.0f;
    winglet.phase = side * 0.6f + 2.0f;

    // the flaps tilt together to climb and against each other to turn
    Fold flap;
    flap.entity = AddPart(isRight ? NAME("PaperPlaneFlapR") : NAME("PaperPlaneFlapL"), wing.entity.Get(), flapTriangles, flapInnerHinge, flapInnerHinge - Nose);
    flap.hingeAxis = (flapOuterHinge - flapInnerHinge).Normalized();
    flap.flutterAngle = 0.14f;
    flap.flutterSpeed = 21.0f;
    flap.phase = side * 2.3f + 0.4f;
    flap.steerAngle = 0.25f;
    flap.climbAngle = side * 0.3f;

    m_folds.PushBack(wing);
    m_folds.PushBack(winglet);
    m_folds.PushBack(flap);
}

Handle<Entity> PaperPlane::AddPart(Name name, Node* parent, const Array<Vec3f>& triangles, const Vec3f& origin, const Vec3f& localPosition)
{
    Handle<Mesh> mesh = BuildPaperMesh(name, triangles, origin);

    Handle<Entity> entity = MakeHandle<Entity>(name);
    InitObject(entity);

    entity->AddComponent<MeshComponent>(MeshComponent { mesh, m_material });
    entity->SetLocalBounds(mesh->GetAABB());

    parent->AddChild(entity);

    entity->SetIsDynamic(true);
    entity->SetLocalTranslation(localPosition, TransformChangeType::Simulation);

    return entity;
}

void PaperPlane::Update(float delta, float windStrength, float steer, float climb)
{
    m_time += delta;
    m_windStrength += (windStrength - m_windStrength) * (1.0f - std::exp(-WindResponse * delta));

    // slow gusts on top of each fold's own flutter
    const float gust = 0.75f + 0.25f * std::sin(m_time * 1.3f) * std::sin(m_time * 0.47f + 0.8f);

    for (const Fold& fold : m_folds)
    {
        const float wave = m_time * fold.flutterSpeed + fold.phase;
        const float flutter = 0.65f * std::sin(wave) + 0.35f * std::sin(wave * 2.37f + 1.1f);

        const float angle = fold.flutterAngle * m_windStrength * gust * flutter
            + fold.steerAngle * steer
            + fold.climbAngle * climb;

        fold.entity->SetLocalRotation(Quat4f::AxisAngles(fold.hingeAxis, angle), TransformChangeType::Simulation);
    }
}

} // namespace Hyperion
