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

## Claiming global uniform names

This is a general protocol, not a weather-bus detail. Snow accumulation, fluid state and anything
else that publishes global uniforms should follow it, or two subsystems will eventually write the
same name and nothing will report it.

**1. Reserve a prefix and say so.** The bus owns `tod_`, `wind_`, `wetness_` and `weather_`. A
prefix is a declaration, so it belongs in the module's README and in a comment at the declaration
table, not just in someone's head.

**2. Record ownership from acquisition, not from asking.** Use
`RenderingServer::global_shader_parameter_try_add()`, which returns whether the name was actually
free. The void `global_shader_parameter_add()` cannot tell you — and neither can anything else,
because `global_shader_parameter_get()`, `_get_type()` and `_get_list()` all `ERR_FAIL` outside the
editor. A subsystem that assumes it got a name will eventually delete someone else's uniform on
teardown.

**3. Take the whole contract or none of it.** If any name cannot be acquired, hand back the ones
already taken and stand down. Half-owning a contract means writing to some names and not others,
which leaves the shaders reading a mix of your state and someone else's.

**4. Stay silent while stood down.** Keep serving your C++ getters, so CPU consumers still work, but
write nothing. The names belong to whoever holds them.

**5. Check `ProjectSettings` for `shader_globals/<name>` first.** Modules initialize before
`main.cpp` loads those, so claiming a name the project also declares would make that later load fail
as a duplicate. Finding one is a configuration error, not a reason to share.

**6. Remove exactly what you registered.**

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
