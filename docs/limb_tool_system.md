# Tardigrade Limb And Tool Direction

This is the current intended direction for creature motion and held tools. It is a design note, not a finished spec.

## Creature Shape

The player should remain a small matter rig, not a sprite animation layered on top of the sim.

- Head, torso, and tail are the main blended body chain.
- The head target should blend between player intent and the local gravity-up direction.
- The blend should scale with gravity strength so weak gravity does not force a strong upright pose.
- The current body/head constraint force is the right first primitive for this.

## Limbs

Limbs should be small physical parts attached to the torso by spring-like targets.

- Legs and arms can use the same basic spring-follow primitive as the held tool center.
- Limb endpoints can grip terrain nodes or field surface points within a short reach.
- Movement force should be applied through gripped limbs so terrain receives equal/opposite force.
- Limb visual nodes should be distinguishable from each other, but still feel physically attached.

The likely rendering requirement is a body-part or merge-group id in GPU node metadata. Material and island id alone are probably not enough if limbs should merge with the torso but not visually merge into every other limb.

## Tools

Tools should behave like lightweight physical objects owned by a creature rig.

- The tool center follows the torso with a spring.
- The mouse controls target angle, not world position.
- Firing lowers angular responsiveness, making the tool feel heavier.
- The mining beam should use the tool muzzle and direction only.
- Later, arms can become constraints between torso/hand/tool points.
- Recoil or torque should eventually flow back into the creature body, but only after the arm model exists.

## Near-Term Implementation Path

1. Keep the current head/torso/tail rig simple.
2. Add invisible limb state first: anchors, grip targets, reach, and phase.
3. Replace current abstract leg grips with limb endpoint grips.
4. Add small visible limb nodes only after the grip behavior feels correct.
5. Add a render merge-group id if visible limbs need clean separation.
6. Move mining tool ownership from player-specific code toward a reusable held-tool primitive once arms exist.

