using System;

using Hyperion;

// Drives NpcGraph from C#: gameplay only writes parameters and plays slot clips, the graph picks and blends the clips.
// With AutoTour off, the Speed / Crouched / Aiming fields are written as they are, so they can be changed in the Inspector while playing.
public class NpcAnimationDriver : Script
{
    [HypField(Serialize = true, Editor = true)] public bool AutoTour = true;
    [HypField(Serialize = true, Editor = true)] public float Speed = 0.0f;
    [HypField(Serialize = true, Editor = true)] public bool Crouched = false;
    [HypField(Serialize = true, Editor = true)] public float Aiming = 0.0f;
    [HypField(Serialize = true, Editor = true)] public float PathRadius = 3.5f;

    private const float RunSpeed = 8.6f;
    private const float CrouchSpeed = 1.0f;

    private enum Phase
    {
        Locomotion,
        Crouch,
        Aim,
        Actions,
        Death
    }

    private static readonly (Phase Phase, float Duration)[] Tour =
    {
        (Phase.Locomotion, 12.0f),
        (Phase.Crouch, 6.0f),
        (Phase.Aim, 6.0f),
        (Phase.Actions, 6.0f),
        (Phase.Death, 5.0f)
    };

    private Node? _mover;
    private Vec3f _pathCenter;
    private float _pathAngle;

    private int _phaseIndex;
    private float _phaseTime;
    private float _prevPhaseTime;

    private Animation? _pistolShoot;
    private Animation? _pistolReload;
    private Animation? _pickUp;
    private Animation? _interact;

    public override void OnAdded(Entity entity)
    {
        base.OnAdded(entity);

        // the skinned mesh entity sits under the character's root node, which is the node to move
        _mover = entity;

        while (_mover.Parent != null && _mover.Parent.Parent != null)
        {
            _mover = _mover.Parent;
        }

        _pathCenter = _mover.LocalTranslation;

        AssetRegistry? engineAssets = AssetRegistry.Engine;

        _pistolShoot = engineAssets?.GetAsset<Animation>(AssetBucket.Animations, new Name("PistolShoot"));
        _pistolReload = engineAssets?.GetAsset<Animation>(AssetBucket.Animations, new Name("PistolReload"));
        _pickUp = engineAssets?.GetAsset<Animation>(AssetBucket.Animations, new Name("PickUp"));
        _interact = engineAssets?.GetAsset<Animation>(AssetBucket.Animations, new Name("Interact"));
    }

    public override void Update(float deltaTime)
    {
        ref AnimationComponent animation = ref Entity.GetComponent<AnimationComponent>();

        // the graph instance is made on the graph's first update, writes before that would be lost
        if (animation.GraphInstance == null)
        {
            return;
        }

        if (AutoTour)
        {
            UpdateTour(ref animation, deltaTime);
        }

        animation.SetFloat("Speed", Speed);
        animation.SetBool("Crouched", Crouched);
        animation.SetFloat("Aiming", Aiming);

        MoveAlongPath(deltaTime);
    }

    private void UpdateTour(ref AnimationComponent animation, float deltaTime)
    {
        _prevPhaseTime = _phaseTime;
        _phaseTime += deltaTime;

        if (_phaseTime >= Tour[_phaseIndex].Duration)
        {
            _phaseIndex = (_phaseIndex + 1) % Tour.Length;
            _phaseTime = 0.0f;
            _prevPhaseTime = -1.0f;

            Logger.Log(LogLevel.Info, "NpcAnimationDriver: {0}", Tour[_phaseIndex].Phase);
        }

        float duration = Tour[_phaseIndex].Duration;

        switch (Tour[_phaseIndex].Phase)
        {
        case Phase.Locomotion:
            {
                // idle -> walk -> jog -> run and back, through the Locomotion blend
                float wave = MathF.Sin(MathF.PI * _phaseTime / duration);

                Speed = RunSpeed * wave * wave;
                Crouched = false;
                Aiming = 0.0f;

                break;
            }
        case Phase.Crouch:
            // the Stance state machine crossfades to Crouching while Crouched is true
            Crouched = true;
            Speed = _phaseTime > 1.0f && _phaseTime < duration - 1.0f ? CrouchSpeed : 0.0f;

            break;
        case Phase.Aim:
            Crouched = false;
            Speed = 0.0f;

            // the Aim layer's weight, upper body only through UpperBodyMask
            Aiming = MathF.Min(1.0f, MathF.Min(_phaseTime, duration - _phaseTime) * 2.0f);

            if (Crossed(1.5f) || Crossed(2.5f))
            {
                PlaySlot(ref animation, "UpperBody", _pistolShoot);
            }

            if (Crossed(3.5f))
            {
                PlaySlot(ref animation, "UpperBody", _pistolReload);
            }

            break;
        case Phase.Actions:
            Speed = 0.0f;
            Aiming = 0.0f;

            // full-body one-shots through the Action slot, which blends back to the graph when the clip ends
            if (Crossed(0.0f))
            {
                PlaySlot(ref animation, "Action", _pickUp);
            }

            if (Crossed(3.0f))
            {
                PlaySlot(ref animation, "Action", _interact);
            }

            break;
        case Phase.Death:
            Speed = 0.0f;

            // triggers fire once; Die is an any-state transition, Revive leads back to Standing
            if (Crossed(0.0f))
            {
                animation.SetTrigger("Die");
            }

            if (Crossed(4.0f))
            {
                animation.SetTrigger("Revive");
            }

            break;
        }
    }

    private bool Crossed(float time)
    {
        return _prevPhaseTime < time && _phaseTime >= time;
    }

    private static void PlaySlot(ref AnimationComponent animation, string slot, Animation? clip)
    {
        if (clip != null)
        {
            animation.PlaySlot(slot, clip, 0.15f);
        }
    }

    private void MoveAlongPath(float deltaTime)
    {
        if (_mover == null || PathRadius <= 0.0f)
        {
            return;
        }

        _pathAngle += Speed * deltaTime / PathRadius;

        Vec3f offset = new Vec3f(MathF.Sin(_pathAngle), 0.0f, MathF.Cos(_pathAngle)) * PathRadius;
        Vec3f direction = new Vec3f(MathF.Cos(_pathAngle), 0.0f, -MathF.Sin(_pathAngle));

        _mover.LocalTranslation = _pathCenter + offset;
        _mover.LocalRotation = Quat4f.AxisAngles(new Vec3f(0.0f, 1.0f, 0.0f), MathF.Atan2(direction.X, direction.Z));
    }
}
