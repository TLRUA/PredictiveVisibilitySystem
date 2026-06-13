# PredictiveVisibilitySystem

Experimental Unreal Engine C++ plugin for predictive prefetch.

The plugin generates coarse, direction-aware prefetch candidates in the editor, then combines them with runtime trajectory history to submit low-priority World Partition streaming, StaticMesh and Material async-load, and component PSO precache requests.

> This plugin prepares likely future content. It does not replace Unreal Engine visibility determination or modify the renderer.

## Features

- **Editor Bake:** Samples `APredictiveVisibilityBakeVolume` by position and view direction. Distance, FOV, and optional line traces are used to generate `UPredictiveVisibilityBakeData`.

- **Runtime Prediction:** Combines the nearest BakeData record with a SaveGame-backed adaptive trajectory cache.

- **World Partition Prefetch:** Provides low-priority spatial streaming sources through `IWorldPartitionStreamingSourceProvider`.

- **Asset Prefetch:** Asynchronously loads predicted `StaticMesh` and `Material` assets.

- **PSO Precache:** Collects component PSO parameters and submits precache requests for `StaticMesh`, `ISM`, `HISM`, and static-mesh components collected from World Partition HLOD actors.

## Architecture

```text
BakeVolume
  -> Editor Bake
  -> PredictiveVisibilityBakeData
  -> RuntimeSubsystem
     -> World Partition streaming source
     -> StaticMesh / Material async load
     -> Component PSO precache
```

## Non-Goals

- Does not replace World Partition, HLOD, Nanite, frustum culling, occlusion culling, or the renderer
- Does not predict or directly load Unreal Engine internal World Partition Runtime Cells
- Does not modify Renderer, RDG, or RHI, and does not use custom GPU async compute
- Does not currently cover SkeletalMesh, Niagara, or Decal PSO precaching
- Does not claim fixed hitch or frame-rate improvements

The plugin uses a coarse, plugin-owned spatial prediction key. It is not an Unreal Engine Runtime Cell identifier.

## Requirements

- Developed against Unreal Engine 5.5.4
- A C++ Unreal Engine project
- A World Partition map to use the streaming-source integration

Compatibility with other Unreal Engine versions has not yet been verified.

## Installation

Copy the repository into your project:

```text
<ProjectRoot>/Plugins/PredictiveVisibilitySystem/
```

Then:

1. Regenerate project files
2. Build `<ProjectName>Editor`
3. Enable **Predictive Visibility System** in **Edit > Plugins**
4. Restart the editor

## Quick Start

1. Place a small `PredictiveVisibilityBakeVolume` in a World Partition map
2. Run **Tools > Predictive Visibility > Bake Current World**
3. Confirm that a `BakeData_<MapName>` asset is generated
4. Assign the generated asset to `DefaultBakeData` in the plugin settings
5. Enable the desired streaming, asset-prefetch, and PSO-prefetch options
6. Use a non-editor Development or Packaged build for component PSO validation

## Validation Notes

PIE is suitable for validating BakeData loading, candidate generation, streaming-source creation, and asset requests.

Validate component PSO precaching in a non-editor Development or Packaged build with component PSO precaching enabled.

No public performance benchmark is included in this repository.

## Current Limits

- Runtime automatic asset prefetch currently covers only `StaticMesh` and `Material` assets
- HLOD data is used as a source of static-mesh-family candidates, not as a standalone HLOD asset loader
- Candidate selection is conservative and coarse; it is not renderer-accurate visibility

## License

No license has been selected for this release. All rights are reserved unless a license is added later.

This repository does not include Unreal Engine source code, binaries, sample-project content, or generated project assets.
