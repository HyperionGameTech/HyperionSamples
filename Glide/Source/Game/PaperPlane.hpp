#pragma once

#include <Core/Reflection/Handle.hpp>
#include <Core/Containers/Array.hpp>
#include <Core/Math/Vector3.hpp>
#include <Core/Name/Name.hpp>

namespace Hyperion {

class Node;
class Entity;
class Material;

// a folded paper dart built in code, whose wings, winglets and flaps flutter in the wind
class PaperPlane
{
public:
    bool Create(Node* planeNode);
    void Destroy();

    void Update(float delta, float windStrength, float steer, float climb);

private:
    struct Fold
    {
        Handle<Entity> entity;
        Vec3f hingeAxis;
        float flutterAngle = 0.0f;
        float flutterSpeed = 0.0f;
        float phase = 0.0f;
        float steerAngle = 0.0f;
        float climbAngle = 0.0f;
    };

    void CreateBody(Node* planeNode);
    void CreateWing(float side);

    Handle<Entity> AddPart(Name name, Node* parent, const Array<Vec3f>& triangles, const Vec3f& origin, const Vec3f& localPosition);

    Handle<Material> m_material;
    Handle<Entity> m_importedModel;
    Handle<Entity> m_body;
    Array<Fold> m_folds;

    float m_time = 0.0f;
    float m_windStrength = 0.0f;
};

} // namespace Hyperion
