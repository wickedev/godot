# Weather bus

Canonical world state — time of day, wind, wetness, precipitation — broadcast to every shader.

A weather system's real job is getting *one* state in front of *every* material. Godot already
binds the global shader uniform buffer to spatial, particles, sky, fog and canvas_item shaders
alike, so this module owns the state and pushes it there instead of adding a second channel.

## Uniform contract v1

These names are reserved. Renaming or retyping one breaks every consumer at once, so treat this
table the way the GBuffer schema is treated — a cross-lane agreement, not an implementation detail.
It is asserted in `tests/test_weather_bus.h`.

### `tod_*` — time of day

| Name | Type | Meaning |
|---|---|---|
| `tod_time` | `float` | Normalized day fraction, `[0,1)` |
| `tod_sun_direction` | `vec3` | Points **toward** the sun, world space, normalized |
| `tod_moon_direction` | `vec3` | Points **toward** the moon |
| `tod_sun_intensity` | `float` | `0.0` below the horizon |

### `wind_*` — wind

| Name | Type | Meaning |
|---|---|---|
| `wind_direction` | `vec3` | Direction the wind blows **toward**, world space, normalized |
| `wind_speed` | `float` | Base speed in m/s, gust excluded |
| `wind_gust` | `float` | Gust multiplier; `1.0` means no gust |
| `wind_turbulence` | `float` | High-frequency jitter, `[0,1]` |

### `wetness_*` — wetness

| Name | Type | Meaning |
|---|---|---|
| `wetness_amount` | `float` | `[0,1]` |
| `wetness_porosity` | `float` | How far albedo darkens when wet, `[0,1]` |

### `weather_*` — weather state

| Name | Type | Meaning |
|---|---|---|
| `weather_rain_intensity` | `float` | `[0,1]` |
| `weather_snow_intensity` | `float` | `[0,1]` |
| `weather_temperature` | `float` | Degrees Celsius; drives the rain/snow transition |
| `weather_fog_density` | `float` | `[0,1]` |

### Direction conventions point opposite ways, deliberately

`tod_sun_direction` points *toward* the sun because that is the lighting L-vector convention.
`wind_direction` points where the wind *goes* because that is the fluid convention. Unifying them
would make one of the two surprising to the people who work in that domain, so both are spelled out
at every declaration instead — here, in the shader library, and in the class reference.

### Reserved but not yet declared

`sampler2D` globals are deliberately absent from v1. Changing a texture global's value queues a
uniform set rebuild for *every* material consuming it (`MaterialStorage::global_shader_parameter_set`),
so the only sane usage is to set the RID once and render into it every frame — and nothing creates
those RIDs yet. Rain occlusion in particular waits on the `particles_collision_get_heightfield_texture`
getter. These names are reserved for when that lands: `wind_gust_noise`, `weather_rain_occlusion`.

## Shader side

```glsl
#include "engine://shaderlib/weather.gdshaderinc"

void vertex() {
    VERTEX += weather_wind_sway(NODE_POSITION_WORLD, 0.2, float(INSTANCE_ID), TIME);
}

void fragment() {
    vec3 albedo = ALBEDO;
    float roughness = ROUGHNESS;
    weather_apply_wetness(albedo, roughness, 1.0);
    ALBEDO = albedo;
    ROUGHNESS = roughness;
}
```

Include the library rather than declaring the globals yourself — that keeps the spelling and the
conventions in one place.

### How `engine://` works

The engine cannot drop `res://` files into someone's project, so the include is published from C++
instead. `EngineShaderLib::publish()` builds a `ShaderInclude` and calls `set_path()` on it, which
registers it in `ResourceCache`. The shader preprocessor resolves includes through
`ResourceLoader::exists()` / `::load()`, and both consult `ResourceCache` before asking any format
loader — so it is simply found. No new `ResourceFormatLoader`, no core change. The scheme survives
path normalization because `String::is_absolute_path()` treats anything containing `://` as
absolute, so `ResourceLoader` leaves it alone rather than rewriting it to `res://`.

`EngineShaderLib` is generic; other lanes can publish their own libraries through it.

## CPU side

Not every consumer is a shader. Hair simulation, buoyancy and `Area3D` wind run on the CPU:

```cpp
const Vector3 wind = WeatherBus::get_singleton()->get_wind_velocity();
```

`get_wind_velocity()` and the shader's `weather_wind_velocity()` compute the same thing. Use one of
them rather than recomputing, so the two sides cannot drift.

### Velocity is not acceleration

Simulations want a force, and the bus carries a velocity. Converting between them is the consumer's
job, and it needs a coefficient:

```cpp
// Linear drag: acceleration proportional to wind speed. `drag` is in 1/s and is a per-material
// tuning value -- hair strands, a flag and a tree branch all respond differently to the same wind.
hair->SetExternalAcceleration(to_jolt(WeatherBus::get_singleton()->get_wind_velocity() * drag));
```

Real drag goes as v², not v. Linear is the cheap approximation, and it is the one worth starting
from: it cannot blow up at high wind speeds, and the difference is a tuning curve rather than a
visible behavior change. Whatever a consumer picks, it should not silently treat the velocity as an
acceleration — the units do not match and the result only looks plausible.

Jolt's hair solver had no external force input at all; `Hair::SetExternalAcceleration()` comes from
`thirdparty/jolt_physics/patches/0002-hair-external-acceleration.patch`. Upstream lists wind forces
among the hair system's missing features, so there was nothing to hook.

## Producers

The bus is transport. These put values on it:

| Node | Drives | Notes |
|---|---|---|
| `TimeOfDay` | `tod_*` | Real solar position — declination from day of year, hour angle from the clock, altitude/azimuth from latitude. Gets seasons, latitude and polar night right, which a fixed rotation axis cannot. Optionally aims `DirectionalLight3D` nodes for the sun and moon. |
| `WindDriver` | `wind_*` | Gusting, plus the bridge to `Area3D` wind so `SoftBody3D` cloth sees the same wind foliage does. |

Both advance themselves once in the scene tree, and both expose `advance(delta)` so a test or a
cutscene can step them deterministically.

Nothing drives `wetness_*` or `weather_*` yet — those belong to the weather state machine.

### Two sign conventions worth knowing about

Both bite silently, and both are covered by tests because of it.

`DirectionalLight3D` emits along its local `-Z`, so `TimeOfDay` stands the light where the sun is
and looks back at the origin. `Area3D` reads wind direction off the `-Z` of whatever node its
`wind_source_path` points at, so `WindDriver` aims itself along the wind and hands each listed area
a path to itself. In both cases `look_at()` is called with `use_model_front` left false — passing
true flips forward to `+Z` and reverses the result without erroring.

## Threading

Main thread only. The setters are plain member writes with no synchronization, and
`RenderingServer`'s global uniform calls carry the same expectation. A CPU consumer running off the
main thread -- a physics step on a worker, say -- should be handed a value copied on the main
thread rather than calling the getters itself.

## Ownership

For values that are also gameplay state — `weather_temperature` most obviously — this bus is a
**mirror**, not the source of truth. Whatever simulates the weather owns the value and pushes it
here. Two writers racing to own one value is a bug this module cannot detect.

## Limits

`BaseMaterial3D`, `ParticleProcessMaterial`, `ProceduralSkyMaterial` and `FogMaterial` generate
their shader code in C++ and emit no global uniform references at all, so they cannot read this bus.
Materials that need wind or wetness have to be `ShaderMaterial`.

Nothing drives the bus yet. Gust animation, sun orbits and weather transitions belong to the
managers that come next; this module only owns the contract and the transport.
