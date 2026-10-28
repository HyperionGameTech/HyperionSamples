# AnimGraph

Two characters showing the two ways to use an animation graph.

## Player: no code

`Player > CharacterModel` has a `CharacterModelComponent`. Its `Graph` is left empty, so it runs the AnimToolkit's built-in locomotion graph, and `CharacterModelSystem` drives the character's motion every frame.

## Npc: driven from C#

`Npc > ... > BaseHuman_Primitive0` has:

- an `AnimationComponent` whose `Graph` is `Content/Animations/NpcGraph`
- a `ScriptComponent` running `Content/Scripts/NpcAnimationDriver.cs`

**NpcGraph** (open it with the Open button next to the `Graph` field):

| Node | Action |
| --- | --- |
| `Locomotion` (Blend1D on `Speed`) | Idle, Walk, Jog, Run. Clips play at `Speed / ReferenceSpeed` and stay in step through the `Locomotion` sync group |
| `Crouch` (Blend1D on `Speed`) | CrouchIdle, CrouchWalk |
| `Stance` (state machine) | Standing <-> Crouching based on `Crouched`. any state -> Dead on the `Die` trigger. Dead -> Standing on `Revive` |
| `Aim` (Layer) | PistolAimNeutral over the upper body (`UpperBodyMask`, from `spine_01` down), weighted by `Aiming` |
| `Action` (Slot) | full-body one-shots played from code |
| `UpperBody` (Slot) | upper-body one-shots played from code, through `UpperBodyMask` |

**NpcAnimationDriver** walks the NPC around a circle and loops through:
speed ramp, crouch, aim + shoot + reload, pick up + interact, die + revive.

The graph does all the clip choice and blending.

The script only does this:

```csharp
ref AnimationComponent animation = ref Entity.GetComponent<AnimationComponent>();

animation.SetFloat("Speed", Speed);
animation.SetBool("Crouched", Crouched);
animation.SetFloat("Aiming", Aiming);

animation.SetTrigger("Die");
animation.PlaySlot("UpperBody", pistolShoot, 0.15f);
```

Turn `AutoTour` off in the script's fields and the `Speed`, `Crouched` and `Aiming` fields drive the graph directly, so you can change them in the Inspector while playing.

The graph instance only exists once the graph has run a frame (`animation.GraphInstance` is null before that), so the script skips writes until then.
