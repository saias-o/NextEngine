# NextEngine Scenario System Plan

## Goal

Build a generic scenario system for tutorials, quests, missions, puzzles,
cinematics, enemy waves, boss phases, XR onboarding, and world events.

The canonical rule is:

> A scenario is a declarative asset executed by a native runner. Behaviours own
> local object capabilities; scenarios orchestrate them with typed actions and
> typed conditions.

This prevents large gameplay behaviours full of step counters and `if` chains.

## Canonical Asset

The only authoring format is JSON in `.nescenario` files.

```json
{
  "version": 1,
  "id": "tutorial.move",
  "roles": {
    "player": { "group": "player", "required": true }
  },
  "blackboard": {
    "moved": false
  },
  "steps": [
    {
      "id": "show_prompt",
      "enter": [
        { "objective.show": { "text": "Avance jusqu'au marqueur" } }
      ],
      "wait": {
        "area.entered": { "area": "marker_zone", "by": "player" }
      },
      "next": "done"
    },
    {
      "id": "done",
      "enter": [
        { "objective.complete": {} }
      ],
      "end": "success"
    }
  ]
}
```

Rules:

- `steps[].id` is required and unique.
- `enter` runs instant actions when a step starts.
- `wait` is one typed condition.
- `next` advances to a step.
- `transitions` may branch and replaces `next` when present.
- `end` is one of `success`, `failure`, or `cancelled`.
- `all`, `any`, and `not` compose conditions.
- Scenarios never reference global node names or serialized `NodeId`s.

## Runtime Architecture

Core types live under `src/scenario/`:

- `ScenarioAsset` parses, serializes, and validates `.nescenario`.
- `ScenarioRunnerBehaviour` executes an asset on a node.
- `ScenarioDirector` is an optional autoload for global scenario control.
- `ScenarioContext` is the runner's local state: roles, blackboard, owned spawns,
  step subscriptions, and status.
- `ScenarioActionRegistry` and `ScenarioConditionRegistry` are the only official
  extension points.
- `ScenarioAnchor` is a small behaviour that gives a node a stable scenario key.

The runner compiles step names to indices, validates all action/condition keys,
and subscribes only to event conditions used by the active step. Polling is kept
for time, blackboard, group counts, node existence/enabled state, and input.

## V1 Actions

- `blackboard.set`
- `objective.show`, `objective.complete`, `objective.fail`
- `node.enable`, `node.disable`, `node.setTransform`
- `scene.instantiate`, `scene.freeOwned`
- `signal.emit`
- `slot.call`
- `input.lock`
- `camera.setTarget`
- `timeline.play`, `timeline.stop`
- `audio.play`, `audio.stop`

Optional systems that do not yet have a full runtime implementation are validated
and treated as no-op integration points.

## V1 Conditions

- `time.elapsed`
- `blackboard.equals`
- `area.entered`, `area.exited`
- `signal.received`
- `node.exists`, `node.enabled`
- `group.count`
- `input.pressed`
- `timeline.finished`
- `all`, `any`, `not`

Unknown actions or conditions make the scenario invalid. LLM tools must not
invent keys.

## LLM Authoring Contract

For a flow that has steps, objectives, branches, spawns, cinematic beats, puzzle
state, or victory/failure conditions, author a `.nescenario`.

Do not write a `ScriptBehaviour` to orchestrate scenario steps. Scripts and C++
behaviours are for reusable local capabilities when the action/condition registry
does not have the primitive yet.

Stable references are:

- `role`
- `group`
- `anchor`
- `self`
- `spawned`

## Migration

The legacy `ScenarioBehaviour` / `"Scenario"` timeline has been removed. New
content must use `"ScenarioRunner"` and `.nescenario` assets. Old scene files
that still contain `"type": "Scenario"` should be migrated by creating a
`.nescenario` file and attaching a `ScenarioRunner` to the same node.

## Implementation Phases

1. Add the plan file and pure parser/validator.
2. Add the runner, local blackboard, steps, transitions, and lifecycle.
3. Add stable references and `ScenarioAnchor`.
4. Add V1 actions and conditions.
5. Register with CMake/reflection/editor/MCP.
6. Add parser/runtime/MCP tests.
7. Build and run the relevant test suite.
