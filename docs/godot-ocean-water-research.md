# Godot 오픈월드 — 오션·수면·수중 시스템 딥리서치

> **목표 수준 정정 (2026-08-18):** 목표는 풀 AAA — Sea of Thieves / Horizon Forbidden West / Assassin's Creed Odyssey 급 수면. 원신은 하한 참조점.
> **전략:** upstream 분리 포크 + 딥 코어 개조 허용 + Full-Nanite 커밋. "코어 수정 필요"는 탈락 사유가 아님 — 판단 축은 **man-year급인가**.
> **대상:** Godot 4.8-dev 소스트리 (commit `eda2a482e9`). 모든 file:line은 로컬 실측.
>
> [godot-openworld-engine-gaps.md](./godot-openworld-engine-gaps.md) §8의 상세 문서.
> [godot-weather-atmosphere-research.md](./godot-weather-atmosphere-research.md) §2(강수·젖음) / [godot-audio-propagation-research.md](./godot-audio-propagation-research.md) §1·§2(오디오 필터·버스)와 교차참조.

---

## 0. 한 장 요약

| # | 격차 | Godot 현황 (실측) | 판정 | 한 줄 |
|---|------|-------------------|------|-------|
| 1 | Tessendorf FFT 오션 | `ocean\|tessendorf\|Gerstner` → **0매치** | 🔴 | FFT 오션·Gerstner 웨이브·Projected Grid 전무. **전부 자작** |
| 2 | 오션 셰이딩 (반사·굴절·프레넬·거품·코스틱·SSS) | `caustic\|foam\|refraction` → **0매치**. SSR은 있음, Fresnel은 BRDF 내부에만 있음 | 🟡 | SSR·Fresnel·depth는 재활용 가능. foam·caustic·SSS·굴절은 **전부 자작** |
| 3 | CompositorEffect 기반 오션 패스 | `CompositorEffect` + `access_resolved_depth` + `access_resolved_color` 실재 | 🟢 | **별도 렌더 패스로 오션을 GDExtension에서 구현 가능.** 코어 0 |
| 4 | 수중 포스트프로세스 | `underwater\|submerged\|submersion` → **0매치** | 🟡 | `Environment` 전환 + `CompositorEffect`로 코어 0 구현. 수중 전용 파라미터는 자작 |
| 5 | 부력 (GPU readback) | `buffer_get_data_async` 실재 (**1~2프레임 지연**). `apply_force`/`apply_torque` 실재 | 🟡 | CPU-side 근사 부력이 1순위 실용 경로. GPU readback 지연은 게임플레이에 감내 가능 |
| 6 | 웨이크/폼 상호작용 | `GPUParticles3D` + `GPUParticlesCollisionHeightField3D` 실재 | 🟡 | 파티클 기반 웨이크·스프레이는 가능. 폼 텍스처는 렌더타깃 기반 자작 |
| 7 | 수중 볼류메트릭 | `FogVolume` + `Environment.volumetric_fog_*` 실재 | 🟢 | `FogVolume`을 수중에 배치 + `Environment` 파라미터 전환으로 구현 가능 |
| 8 | 수중 오디오 | `AudioServer.add_bus_effect` + `AudioEffectLowPassFilter` 실재 | 🟢 | 버스 전환 + LPF로 코어 0 구현. [오디오 문서](./godot-audio-propagation-research.md) §1-(3) 참조 |
| 9 | 강/시냇물 (Flow Map) | `flow_map` → PBR anisotropy 전용, 수류 아님 | 🟡 | Flow map 셰이더는 자작 셰이더로 GDExtension 가능. Houdini 저작 파이프라인 별도 |
| 10 | GPU Readback Bottleneck | `buffer_get_data_async` → staging buffer + draw_graph 동기화. async-compute/multi-queue **부재**(gaps obs 11757) | 🟡 | 1~2프레임 지연 감내 or CPU-side 근사 우회. 근본 해결은 async-compute 필요 |

**전략 요약:** Godot에 오션 렌더링은 **개념 자체가 없다**(`ocean`, `tessendorf`, `Gerstner`, `caustic`, `foam`, `refraction`, `underwater` 전부 0매치). 그러나 **`CompositorEffect` + `RenderingDevice`의 compute/raster 능력**이 GDExtension에 완전 개방돼 있어 **Tessendorf FFT 오션 파이프라인 전체를 코어 수정 없이 구현 가능**하다. 진짜 병목은 오션 자체가 아니라 **GPU→CPU readback(부력)**과 **async-compute 부재**다.

---

## 1. 오션 렌더링 (Ocean Rendering)

### 1.1 Tessendorf FFT 오션 — Godot에 무엇이 있는가

**실측: 완전 부재.** 검색어와 결과:

| 검색어 | 범위 | 결과 |
|--------|------|------|
| `ocean` | `servers/` `scene/` `modules/` (.cpp .h .glsl) | **0매치** |
| `tessendorf` | 전역 (.cpp .h .glsl) | **0매치** |
| `Gerstner\|gerstner` | 전역 (.cpp .h .glsl) | **0매치** |
| `fft\|FFT` (rendering) | `servers/rendering/` (.cpp .h .glsl) | **0매치** (오디오 `smbFft`만 존재 — `audio_effect_spectrum_analyzer.cpp:37`) |

Godot에는 **어떤 형태의 오션 웨이브 시뮬레이션도 없다.** Gerstner wave도, Tessendorf FFT 스펙트럼 기반 오션도, 그 어떤 파도 시뮬레이션도 빌트인으로 존재하지 않는다.

**AAA에서 필요한 것:**
- **Tessendorf FFT** (Sea of Thieves, SIGGRAPH 2018): JONSWAP/TMA 스펙트럼 → H0 생성(CPU, 1회) → 시간 발전(ω(k) = √(g|k|)) → 2D Inverse FFT(GPU compute, row+col butterfly) → displacement+slope+Jacobian 출력 → Projected Grid로 렌더링.
- **Cascaded FFT** (4단계, golden-ratio 간격): 먼 거리 타일링 방지.
- **Jacobian determinant foam**: `J = (1+∂x)(1+∂z) - (∂z_x)(∂x_z)` < threshold → foam.
- 일반적인 해상도: 256x256~512x512, 4 cascades. Hermitian packing으로 FFT 연산량 절반.

**Godot에서 구현 가능한가:**

- **FFT compute shader:** `RenderingDevice`의 compute shader(`compute_list_add_barrier`, `shader_create_from_bytecode`)가 GLSL compute shader를 지원. Butterfly FFT는 groupshared memory + barrier로 구현 가능. `servers/rendering/rendering_device.h`의 compute API 완비.
- **Displacement mesh:** `ArrayMesh` + `RenderingDevice`의 vertex buffer로 동적 업데이트. 또는 `RenderingServer`의 `mesh_surface_update_vertex_buffer`로 기존 `ArrayMesh`의 버텍스 버퍼를 매 프레임 갱신.
- **Projected Grid:** `ArrayMesh`를 post-perspective 공간에서 구성 → world-space water plane으로 unproject. `Camera3D.get_camera_projection()` + `get_camera_transform()`으로 복원. GDExtension에서 `ArrayMesh` 동적 생성은 완전히 가능.

**판정:** 🟡 **국소 코어 없이 GDExtension으로 Tessendorf FFT 오션 전체 파이프라인 구현 가능.** `RenderingDevice` compute API + `ArrayMesh` 동적 갱신 + `CompositorEffect` 별도 패스로 충분.

**공수:** 숙련 그래픽스 엔지니어 기준 **4~8주** (FFT 코어 + 오션 셰이딩), 프로덕션급(카스케이드·폼·쇼어라인·수중·부력)까지 **2~4개월**.

**현실적 대안:**
- `godot4-oceanfft` (github.com/tessarakkt/godot4-oceanfft) — Godot 4용 FFT 오션 구현체가 이미 오픈소스로 존재. 포크/통합으로 시간 단축 가능.
- Gerstner wave(CPU-side)만으로 스타일라이즈드 수면은 가능하나, AAA 수준의 사실적 오션 파면·캐스케이드·스펙트럼 기반 폼에는 FFT가 필수.

### 1.2 Projected Grid / CDLOD — 오션 메시 LOD

**Godot에 무엇이 있는가:**
- `PlaneMesh` (`scene/resources/primitive_meshes.h`) — 평면 메시. subdivision으로 격자 생성 가능하나 **view-dependent LOD가 없다.**
- `ArrayMesh` — 완전히 커스텀한 메시 지오메트리. `surface_update_vertex_*` 계열로 매 프레임 갱신 가능.
- **CDLOD / quadtree terrain:** Godot에 없다. `QuadMesh`는 단일 쿼드, 계층적 LOD 메시 구조는 빌트인 미존재.

**AAA에서 필요한 것:**
- **Projected Grid** (Johanson 2004): 카메라 post-perspective 공간에서 균등 격자 → world-space water plane으로 unproject. 카메라 근처 고밀도·원거리 저밀도가 자연스럽게 생성. **단일 메시**로 LOD 스티칭 불필요. Sea of Thieves, AC: Black Flag가 이 방식.
- **CDLOD** (Strugar 2010): 주로 **지형**용. 쿼드트리 + LOD morphing. 오션에 적용하려면 FFT displacement와의 통합이 추가 과제.

**Godot에서 구현 가능한가:**
- **Projected Grid:** `ArrayMesh`를 GDExtension에서 동적 생성. 카메라 frustum + projection으로 post-perspective grid 생성 → unproject → world-space water plane. `Camera3D.get_camera_projection()`(`scene/3d/camera_3d.h:102`)와 `get_camera_transform()`으로 모든 정보 접근 가능.
- **CDLOD:** `ArrayMesh` + LOD 관리 로직으로 GDExtension 구현 가능하나, Projected Grid가 더 단순하고 오션에 적합.

**판정:** 🟢 **Projected Grid는 GDExtension으로 코어 0 구현 가능.** `ArrayMesh` 동적 생성 + 카메라 파라미터로 충분.

**공수:** 1~2주 (Projected Grid 메시 생성 + FFT displacement 연동).

### 1.3 오션 셰이딩 — 반사·굴절·프레넬·거품·코스틱·SSS

**Godot 실측:**

| 기능 | Godot 현황 | 근거 (file:line) |
|------|-----------|-------------------|
| **반사 (Reflection)** | SSR (Screen-Space Reflection) 실재 | `scene_forward_clustered.glsl:2194-2206` — SSR resolve with mip chain. `Environment.set_ssr_enabled()` |
| **프레넬 (Fresnel)** | Schlick Fresnel, BRDF 내부에서만 사용 | `scene_forward_lights_inc.glsl:76` — `SchlickFresnel(half u)`. LTC 면광원 Fresnel `:1142-1149`. **오션 전용 수면 프레넬은 없음** |
| **굴절 (Refraction)** | **0매치** | `refraction\|REFRACTION\|screen_space_refraction` → `servers/rendering/` 전역 **0매치** |
| **거품 (Foam)** | **0매치** | `foam\|Foam\|FOAM` → `servers/rendering/` 전역 **0매치** |
| **코스틱 (Caustics)** | **0매치** | `caustic\|Caustic` → `servers/rendering/` 전역 **0매치** |
| **SSS (Subsurface Scattering)** | `BaseMaterial3D`의 `subsurf_scatter` 속성 존재 | `scene/resources/material.h` — 간이 근사. 얕은 물 SSS로 활용 가능하나 **오션 전용 아님** |
| **SSR (Screen-Space Reflection)** | 완전 구현, mip chain + reprojection | `scene_forward_clustered.glsl:2195-2206` |
| **Depth buffer 접근** | `CompositorEffect.access_resolved_depth` | `scene/resources/compositor.cpp:52-53` — resolved depth buffer 접근 가능 |

**AAA에서 필요한 것과 Godot 구현 경로:**

- **반사:** SSR로 근사 가능하나 **planar reflection**(수면에 평행한 반사 평면)이 없음. `CompositorEffect`로 별도 패스에서 수면 아래 반사 카메라 렌더링 → 반사 텍스처 생성 → 오션 셰이더에서 샘플링. **GDExtension으로 가능.**
- **굴절:** `access_resolved_color`로 화면 텍스처를 읽고, 수면 normal로 UV displacement → 샘플링. **GDExtension `CompositorEffect`로 가능.**
- **프레넬:** `NdotV`로 Schlick Fresnel 적용 → 반사/굴절 혼합. 셰이더 코드 자체는 간단, **자작 셰이더에 포함.**
- **거품:** Jacobian determinant + distance-based shore foam. **자작 셰이더 + foam render target.** `CompositorEffect`에서 foam mask를 별도 렌더타깃에 기록 가능.
- **코스틱:** GPU Gems Ch.2 방식 — 수면에서 굴절된 광선을 수중 바닥에 투영. `CompositorEffect`에서 별도 패스로 코스틱 라이트 텍스처 생성 → 수중 오브젝트 셰이더에서 샘플링. **GDExtension으로 가능.**
- **SSS (얕은 물):** `BaseMaterial3D.subsurf_scatter`로 근사 가능하나, 깊이 기반 흡수(적색→녹색→청색 순 감쇠)는 자작 셰이더 필요.

**판정:** 🟡 **각 요소는 개별적으로 GDExtension 가능. 통합 오션 셰이딩 파이프라인은 2~4주.** SSR은 재활용, 나머지는 전부 자작.

### 1.4 수중 포스트프로세스

**Godot 실측:**

| 검색어 | 결과 |
|--------|------|
| `underwater` | `servers/` `scene/` `modules/` → **0매치** |
| `under_water` | 전역 → **0매치** |
| `submerged\|submersion` | 전역 → **0매치** |
| `water_surface` | 전역 → **0매치** |

**수중 PP 개념 자체가 Godot에 없다.** 그러나 `CompositorEffect` + `Environment` 전환으로 구현 가능.

**CompositorEffect의 수중 PP 활용:**
- `CompositorEffect` (`scene/resources/compositor.h:39-96`)는 `GDVIRTUAL _render_callback(int p_effect_callback_type, RID p_render_data)`를 통해 렌더링 패스에 개입.
- `access_resolved_color` (`compositor.cpp:48-49`)와 `access_resolved_depth` (`compositor.cpp:52-53`)로 화면 텍스처 + depth buffer 접근.
- `EffectCallbackType` (`compositor.h:50-55`): `POST_TRANSPARENT`(투명 오브젝트 이후) 또는 `POST_OPAQUE`(불투명 이후) 콜백에서 depth 기반 수중 색보정·안개·블러 적용 가능.
- `CameraAttributes` (`servers/rendering/rendering_server.h`): 카메라별로 `exposure`·`auto_exposure` 설정 가능. 수중 진입 시 `CameraAttributes`를 전환해 노출·색보정 변경.

**구현 경로:**
1. **수중/수상 감지:** 카메라 y좌표 vs 수면 y좌표 비교. `Camera3D.global_position.y` < `water_surface_y` → 수중.
2. **Environment 전환:** `WorldEnvironment.environment`를 수중용 `Environment`로 교체 — volumetric fog density·albedo·안개색 변경.
3. **CompositorEffect:** `POST_TRANSPARENT` 콜백에서 `access_resolved_depth`로 depth fog(적→녹→청 감쇠) + 색수차 + 블러 적용.
4. **메니스커스(meniscus) 효과:** 수면 통과 시 미세한 라인/왜곡. `CompositorEffect`에서 depth-based threshold로 구현.

**판정:** 🟡 **GDExtension `CompositorEffect` + `Environment` 전환으로 코어 0 구현 가능.** `Environment.set_volumetric_fog_enabled(true)` 전환 시 볼류메트릭 포그 파라미터를 수중용으로 변경하면 수중 볼류메트릭도 가능(§3.1 참조).

**공수:** 1~2주 (수중 감지 + Environment 전환 + CompositorEffect depth fog).

---

## 2. 수면 상호작용 (Water Surface Interaction)

### 2.1 부력 (Buoyancy)

**Godot에 무엇이 있는가:**

- `RigidBody3D.apply_force(force, position)` — `scene/3d/physics/rigid_body_3d.cpp:563-565`
- `RigidBody3D.apply_torque(torque)` — `scene/3d/physics/rigid_body_3d.cpp:568-569`
- `RigidBody3D.apply_central_force(force)` — `scene/3d/physics/rigid_body_3d.cpp:559-560`
- `RigidBody3D.apply_torque_impulse(impulse)` — `scene/3d/physics/rigid_body_3d.cpp:555-556`
- `RigidBody3D.get_contact_count()` — `scene/3d/physics/rigid_body_3d.cpp:542`
- `PhysicsDirectBodyState3D.get_contact_count()` — `rigid_body_3d.cpp:163` (integration step 내부)
- `PhysicsDirectBodyState3D.get_contact_collider(i)` — `rigid_body_3d.cpp:210`
- `PhysicsDirectBodyState3D.get_contact_local_position(i)` — `rigid_body_3d.cpp`에서 사용
- **JoltPhysics:** `modules/jolt_physics/` — Jolt 확장 모듈 존재. `JoltPhysicsServer3D`가 `PhysicsServer3D`를 대체 가능.

**AAA에서 필요한 것과 Godot 구현 경로:**

**경로 A — CPU-side 근사 부력 (1순위 권장, Sea of Thieves 방식):**
- CPU에서 간이 파고(height field)를 계산. FFT displacement를 근사하는 Gerstner wave 수식을 CPU에서 직접 평가.
- 선박/오브젝트 하단의 여러 샘플 점에서 파고 계산 → 아르키메데스 원리로 부력(수면 아래 체적 × 유체 밀도 × 중력) → `apply_force` + `apply_torque`.
- **장점:** GPU readback 지연 없음. physics step과 동기화. Sea of Thieves가 이 방식.
- **단점:** CPU 파고 모델이 GPU FFT와 정확히 일치해야 함. Gerstner wave로 근사 시 미세한 불일치 허용.

**경로 B — GPU readback 부력 (§5 상세):**
- `buffer_get_data_async` (`servers/rendering/rendering_device.h:269`)로 FFT displacement 텍스처를 GPU→CPU 읽기.
- 1~2프레임 지연(60fps 기준 ~16~33ms). 게임플레이에 감내 가능한 수준.
- **단점:** staging buffer 할당·draw_graph 동기화 오버헤드. async-compute 미지원으로 인한 파이프라인 stall 위험.

**판정:** 🟡 **CPU-side 근사 부력(경로 A)이 1순위.** GPU readback(경로 B)은 지연 감내 시 가능하나 async-compute 부재(gaps obs 11757)로 인해 파이프라인 병목 가능성.

**공수:** 경로 A: 1~2주 (Gerstner CPU 평가 + 부력 적분). 경로 B: 1주 (readback 배관 + 지연 보상).

### 2.2 웨이크/폼 (Wake/Foam)

**Godot에 무엇이 있는가:**
- `GPUParticles3D` + `GPUParticlesCollisionHeightField3D` — GPU 파티클 시스템 + 높이장 충돌.
- `GPUParticlesCollisionHeightField3D`는 탑다운 depth FB를 내부에 렌더링하여 파티클 충돌용 높이장 텍스처 생성 (`renderer_scene_cull.cpp:4119-4157`, `render_particle_collider_heightfield`).
- `particles_collision_get_heightfield_framebuffer(RID)` — `renderer_scene_render_rd.cpp:1530` — 높이장 FB에 접근 가능.

**날씨 문서와의 교차참조:**
- [godot-weather-atmosphere-research.md](./godot-weather-atmosphere-research.md) §2의 "레인 오클루전 heightfield"가 `GPUParticlesCollisionHeightField3D`의 내부 depth FB를 재활용하는 전략을 제안. **동일한 heightfield를 wake/foam particles의 충돌 표면으로도 재사용 가능.**
- 날씨 문서 §2.3의 `particles_collision_get_heightfield_texture(RID) -> RID` getter 추가 제안(10줄 코어 패치)이 웨이크에도 적용.

**AAA에서 필요한 것과 Godot 구현 경로:**

- **선박 항적 (Wake):** `GPUParticles3D`로 선박 후방에 파티클 방출. `GPUParticlesCollisionHeightField3D`로 수면에 충돌·정착. 파티클 셰이더에서 foam color로 렌더링.
- **오브젝트 진입 거품:** `GPUParticles3D` one-shot burst. 수면 아래 진입 시 foam 파티클 방출.
- **Foam 텍스처 누적:** `CompositorEffect`에서 별도 render target에 foam mask를 누적 기록 → 시간에 따라 감쇠(blur + subtract). `access_resolved_color`로 이전 프레임 foam을 읽고 새 foam을 blend.

**판정:** 🟡 **GPUParticles3D + CompositorEffect로 코어 0 구현 가능.** Heightfield 충돌은 이미 존재하는 인프라 재활용.

**공수:** 1~2주 (웨이크 파티클 + foam 누적 렌더타깃).

### 2.3 수면 디포메이션 (Surface Deformation)

**Godot에 무엇이 있는가:**
- 수면 변형(오브젝트가 수면을 누르는 상호작용)을 위한 빌트인 시스템은 **없다.**
- `RenderingDevice`의 render target에 임의로 기록 가능. `texture_create` + `framebuffer_create` + `draw_list_begin`으로 커스텀 렌더 패스 구성.

**AAA에서 필요한 것과 Godot 구현 경로:**
- **디스플레이스먼트 렌더타깃:** 오브젝트의 수면 아래 부분을 탑다운 오소그래픽 카메라로 렌더링 → displacement mask 생성 → 오션 셰이더에서 FFT displacement에 더함.
- `CompositorEffect`에서 별도 패스로 displacement contribution을 렌더타깃에 additive blending.
- Temporal decay(시간 감쇠)로 변형이 자연스럽게 복원되도록.

**판정:** 🟡 **GDExtension + CompositorEffect로 구현 가능.** 추가 렌더 패스 + displacement render target.

**공수:** 1~2주.
---

## 3. 수중 환경 (Underwater)

### 3.1 수중 볼류메트릭

**Godot에 무엇이 있는가:**

- `FogVolume` (`scene/3d/fog_volume.h`) — 씬에 배치 가능한 국소 볼류메트릭 포그 볼륨. `set_shape(Ellipsoid/Box/Cone/Cylinder/World)` (`fog_volume.cpp:89-91`), `set_material(material)` (`:101-107`)로 커스텀 `FogMaterial` 할당 가능.
- `Environment.volumetric_fog_*` — 전역 볼류메트릭 포그 파라미터 (`environment.h:203-216`):
  - `volumetric_fog_enabled` (`:203`)
  - `volumetric_fog_density` (0.05 기본, `:204`)
  - `volumetric_fog_albedo` (수중 청록색으로 변경 가능, `:205`)
  - `volumetric_fog_emission` + `emission_energy` (`:206-207`)
  - `volumetric_fog_anisotropy` (0.2, `:208`)
  - `volumetric_fog_length` (64.0m, `:209`)
  - `volumetric_fog_temporal_reproject` + `reproject_amount` (0.9, `:214-215`)
  - `volumetric_fog_gi_inject` / `ambient_inject` (`:211-212`)
- `FogMaterial` (`scene/resources/fog_material.h`) — `FogVolume`용 커스텀 셰이더 머티리얼. `shader_type fog;`로 FogVolume 내부의 볼류메트릭 산란 계산을 커스터마이즈 가능.

**AAA에서 필요한 것과 Godot 구현 경로:**

- **수중 참호 안개(uniform depth fog):** `Environment.volumetric_fog_density`를 수중 진입 시 높은 값(예: 0.5~1.0)으로 전환. `volumetric_fog_albedo`를 청록색(turquoise)으로 변경. `volumetric_fog_length`를 낮은 값(예: 10~20m)으로.
- **파장별 흡수(적→녹→청):** `FogMaterial` 커스텀 셰이더에서 depth 기반 spectral absorption 구현. `access_resolved_depth`로 픽셀 depth를 얻고, depth에 따라 RGB 채널별 감쇠율 적용(적색 ~5m, 녹색 ~15m, 청색 ~60m).
- **God rays (수중):** `Environment.volumetric_fog` + `DirectionalLight3D`로 수중 볼류메트릭 라이트 샤프트(god rays) 생성. DirectionalLight가 `FogVolume`을 통과하면 자동으로 볼류메트릭 산란 계산.
- **수중 거품/플랑크톤 파티클:** `GPUParticles3D`로 부유 파티클(snow-like)을 수중에 배치. `FogVolume` 내부에서 빛 산란에 기여하지는 않으나 시각적 밀도감 제공.

**판정:** 🟢 **`FogVolume` + `Environment` 파라미터 전환 + `FogMaterial` 커스텀 셰이더로 코어 0 구현 가능.** `FogVolume`의 `World` shape으로 전체 수중 영역을 덮거나, `Box` shape로 카메라 주변에 배치.

**공수:** 3~5일 (FogVolume 배치 + Environment 전환 + FogMaterial 셰이더).

### 3.2 수중 오디오

**Godot 실측:**

- `AudioServer.add_bus_effect(bus_idx, effect)` — `servers/audio/audio_server.h`에 바인딩됨. 런타임에 버스 이펙트 추가 가능.
- `AudioServer.get_bus_effect_instance(bus_idx, effect_idx, channel)` — 이펙트 인스턴스 접근.
- `AudioServer.set_bus_effect_enabled(bus_idx, effect_idx, enabled)` — 이펙트 활성화/비활성화.
- `AudioEffectLowPassFilter` — `scene/resources/audio_stream_player_3d.h`에 등록된 내장 이펙트.
- `AudioEffectFilter` — 하이패스/밴드패스/로우패스/노치 등.
- `AudioStreamPlayer3D.audio_bus_override` (`scene/3d/physics/area_3d.h:135-136`) — `Area3D` 진입 시 오디오 버스를 재지정 가능.

**오디오 문서와의 교차참조:**
- [godot-audio-propagation-research.md](./godot-audio-propagation-research.md) §1-(3)에서 **Area3D reverb bus + audio_bus_override**의 드라이 신호 유실 문제 문서화. `audio_bus_override=false` + `reverb_bus_enabled=true`인 Area에서는 **버스 볼륨 맵에 리버브 버스만 남고 드라이 버스가 사라짐**. 실무상 두 옵션을 항상 함께 켜야 함.
- 오디오 문서 §2: per-source 오클루전 LPF가 Godot에 **바인딩되지 않아** 스크립트에서 못 씀 (`AudioServer::set_playback_highshelf_params` 미바인딩). 단, 버스 단위 LPF는 `add_bus_effect`로 가능.

**수중 오디오 구현 경로:**

1. **수중 오디오 버스 생성:** `AudioServer.add_bus("Underwater")` + `add_bus_effect(underwater_bus_idx, AudioEffectLowPassFilter)`.
2. **수중 진입 시 전환:** `Area3D`(수중 존)의 `audio_bus_override = true` + `audio_bus = "Underwater"`로 자동 버스 전환. 또는 스크립트에서 `AudioStreamPlayer3D.bus = "Underwater"`로 개별 음원 전환.
3. **LPF 파라미터:** `AudioEffectLowPassFilter.cutoff_hz`를 500~1000Hz로 설정(수중 머플 효과). `resonance`로 약간의 공명 추가.
4. **리버브:** underwater reverb bus를 추가로 생성해 `AudioEffectReverb`로 수중 잔향(짧은 decay, 높은 density) 적용.

**판정:** 🟢 **버스 전환 + LPF로 코어 0 구현 가능.** 오디오 문서 §1-(3)의 Area3D 드라이 유실 문제만 주의하면 됨(해결: `audio_bus_override`와 `reverb_bus_enabled`를 항상 함께 on).

**공수:** 1~2일 (오디오 버스 구성 + LPF 튜닝 + 전환 스크립트).

### 3.3 수중/수상 전환 시스템

**Godot에 무엇이 있는가:**
- 수중/수상 전환을 위한 빌트인 트리거 시스템은 **없다**(`underwater\|submerged` → 0매치).
- `Area3D`의 `body_entered`/`body_exited` 신호 + `Camera3D`의 global_position으로 수면 감지 가능.

**구현 경로:**
1. **수면 감지:** `Area3D`(Box shape, 수면~해저)의 `body_entered`/`body_exited` 신호로 카메라 진입/이탈 감지. 또는 `Camera3D.global_position.y` < `water_surface_y` 조건.
2. **Environment 전환:** `WorldEnvironment.environment`를 수중용/수상용으로 교체 — volumetric fog, 안개색, sky, ambient light 변경.
3. **CompositorEffect 전환:** `CompositorEffect.set_enabled(true/false)`로 수중 전용 포스트프로세스(§1.4) 활성화/비활성화.
4. **오디오 전환:** `AudioServer.set_bus_effect_enabled`로 수중 LPF 버스 활성화/비활성화. 또는 `Area3D.audio_bus_override`로 자동 전환.
5. **메니스커스(meniscus):** 전환 직전/직후 0.5m 구간에서 `CompositorEffect`가 depth-based threshold로 수면 경계선 효과.

**판정:** 🟢 **Area3D + Environment 교체 + CompositorEffect + AudioServer로 코어 0 구현.** 전환 로직만 GDScript/GDExtension으로 작성.

**공수:** 2~3일 (전환 상태 머신 + 파라미터 튜닝).

---

## 4. 강/시냇물 (Rivers/Streams)

### 4.1 Flow Map 기반 강

**Godot에 무엇이 있는가:**
- `flow_map` → `scene/resources/material.cpp:624` — `TEXTURE_FLOWMAP`은 PBR anisotropy 방향 텍스처. **수류(water flow)가 아니다.**
- `flow\|water flow\|river\|stream` → `servers/rendering/` (.glsl) → **0매치** (rendering shader에서 수류 개념 없음).
- Godot에는 **flow map 기반 강 셰이더가 빌트인으로 존재하지 않는다.**

**AAA에서 필요한 것 (Valve, Portal 2, SIGGRAPH 2010):**
- **Flow Map 텍스처:** RG 채널에 2D 유속 벡터([-1,1] 범위) 인코딩. Houdini에서 "comb" 도구로 저작(4 texels/m 해상도).
- **Dual-layer crossfade:** 두 개의 normal map layer를 half-cycle offset으로 스크롤, crossfade로 이음새 제거.
- **Per-pixel phase offset noise:** crossfade의 전역 펄싱을 noise texture로 탈동기화.
- **Speed-dependent normal strength:** 유속이 느린 곳은 normal 약화 → 잔잔한 수면 표현.

**Godot에서 구현 가능한가:**
- `ShaderMaterial` + `CompositorEffect`로 flow map 셰이더 구현. `RenderingDevice`의 `texture_create` + `sampler_create`로 flow map 텍스처 업로드.
- `ArrayMesh` + vertex shader에서 Gerstner wave + flow map displacement 조합 → 강의 굽이(wave + flow) 표현.
- `GPUParticles3D`로 급류 표면의 spray 파티클 방출.

**판정:** 🟡 **Flow map 셰이더 + ArrayMesh는 GDExtension으로 코어 0 구현 가능.** Houdini 저작 파이프라인은 별도 구축 필요.

**공수:** 1~2주 (flow map 셰이더 + dual-layer crossfade + Gerstner 통합). Houdini 저작 파이프라인 별도 1주.

### 4.2 폭포·급류

**Godot에 무엇이 있는가:**
- `GPUParticles3D` — GPU 파티클 시스템으로 스프레이(spray)·미스트(mist) 파티클 가능.
- `CPUParticles3D` — CPU 파티클. GPU보다 적은 파티클 수에 적합.
- `GPUParticlesCollisionHeightField3D` — 지형 충돌로 파티클이 바닥에 정착.
- `ParticleProcessMaterial` — `scene/resources/particle_process_material.h` — 파티클 수명·속도·중력·터뷸런스·서브에미터 등.
- `ArrayMesh` — 폭포 시트(waterfall sheet) 메시. vertex shader에서 flow map or procedural UV scrolling.

**폭포 구현 경로:**
1. **폭포 시트 메시:** `ArrayMesh`로 폭포 면(waterfall sheet) 생성. vertex shader에서 UV scrolling + normal map distortion. `ShaderMaterial`로 폭포 셰이더(반투명·거품·프레넬).
2. **스프레이 파티클:** `GPUParticles3D`로 폭포 하단(bottom)에서 스프레이·미스트 방출. `GPUParticlesCollisionHeightField3D`로 지면 충돌.
3. **급류 표면:** §4.1의 flow map 셰이더를 유속이 빠른 구간에 적용. `GPUParticles3D`로 표면 foam 파티클.

**판정:** 🟢 **ArrayMesh + GPUParticles3D + ShaderMaterial로 코어 0 구현 가능.**

**공수:** 3~5일 (폭포 시트 셰이더 + 스프레이 파티클 + 급류 튜닝).

---

## 5. Buoyancy: GPU Readback Bottleneck

### 5.1 `buffer_get_data_async` 실측

**선언:** `servers/rendering/rendering_device.h:269`
```cpp
Error buffer_get_data_async(RID p_buffer, const Callable &p_callback,
                            uint32_t p_offset = 0, uint32_t p_size = 0);
```

**구현 분석** (`servers/rendering/rendering_device.cpp:1329-1413`):

1. **Staging buffer 할당:** `_staging_buffer_allocate()` (`:1358`) — GPU→CPU 전송용 staging buffer를 download_staging_buffers pool에서 할당. `required_align = 32` bytes (`:1351`).
2. **Buffer copy command:** `draw_graph.add_buffer_get_data()` (`:1406`) — GPU copy command를 draw graph에 추가. 비동기적으로 GPU DMA transfer 수행.
3. **Frame-local queue:** `frames[frame].download_buffer_get_data_requests` (`:1409`) — 프레임별 요청 큐에 저장. 콜백은 GPU 작업 완료 후 호출.
4. **Staging buffer flush:** `STAGING_REQUIRED_ACTION_FLUSH_AND_STALL_ALL` (`:1363`) — staging buffer가 가득 차면 `draw_graph.add_synchronization()` (`:1367`)로 파이프라인 동기화 → GPU 작업 완료까지 **CPU가 stall**.

**지연 특성:**
- `buffer_get_data_async`는 **draw_graph에 copy command를 추가**하고 즉시 반환.
- GPU가 copy를 실행 → staging buffer에 데이터 기록 → 다음 프레임(또는 그 이후)에 콜백 호출.
- **전형적 지연: 1~2프레임** (60fps: 16~33ms, 30fps: 33~66ms).
- `draw_graph.add_synchronization()` 호출 시 **추가 지연 + pipeline bubble** 가능.

### 5.2 Async-compute / Multi-queue 부재 (gaps obs 11757)

**문제:** Godot의 `draw_graph`는 단일 그래픽스 큐 모델. 다음이 없다:
- **Async compute queue** — 그래픽스와 독립적으로 실행되는 compute queue. GPU readback이 그래픽스 작업과 병렬로 실행되지 못함.
- **Multi-queue 동기화** — `VK_EXT_calibrated_timestamps` 등 미사용 (gaps 문서 §5에서 확인).
- **Transfer queue** — DMA transfer 전용 큐 미분리.

**부력에 미치는 영향:**
- `buffer_get_data_async`의 copy command가 **그래픽스 렌더링과 동일 큐**에서 실행 → 복사가 렌더링을 blocking하거나 렌더링이 복사를 지연.
- `add_synchronization()` 호출 시 **파이프라인 bubble** 발생 → 프레임 드롭 가능.
- FFT displacement 텍스처(256x256 RGBA32F ≈ 1MB) readback: 1MB 복사는 빠르지만, 동기화 지점(stall)이 문제.

### 5.3 CPU-side 근사 부력 vs GPU Readback

| 기준 | CPU-side Gerstner 근사 | GPU Readback (buffer_get_data_async) |
|------|----------------------|--------------------------------------|
| **지연** | 0 (physics step과 동기) | 1~2프레임 (16~33ms @60fps) |
| **정확도** | Gerstner 근사 vs FFT — 미세 불일치. 작은 오브젝트에선 무시 가능 | FFT displacement와 정확히 일치 |
| **CPU 비용** | Gerstner wave 계산 (일반적으로 수십 µs per sample) | 거의 0 (GPU→CPU copy는 비동기) |
| **구현 복잡도** | 낮음. Gerstner 수식 평가 + 아르키메데스 부력 | 중간. async readback 배관 + 콜백 + 지연 보상 |
| **파이프라인 영향** | 없음 | `add_synchronization()` 시 stall 위험 |
| **AAA 사용** | **Sea of Thieves, 다수 AAA** — CPU-side physics | UE5 Niagara, Unity HDRP Water — GPU readback |

**권장 경로:**
1. **1순위: CPU-side Gerstner 근사 부력** — Sea of Thieves 방식. GPU FFT displacement를 근사하는 Gerstner wave 수식을 CPU에서 평가. FFT 파라미터와 동일한 JONSWAP/TMA 스펙트럼 사용. 수 일~1주.
2. **2순위: GPU readback + 지연 보상** — `buffer_get_data_async`로 FFT displacement 읽기. 지연(1~2프레임)을 velocity prediction으로 보상. 1주.
3. **장기: async-compute 도입** — gaps 문서 §5의 async-compute 과제 해결 시 GPU readback이 병렬화 → 지연·stall 문제 해소. **man-year급 코어 작업.**

### 5.4 Jolt Physics 접촉 기반 부력

**Godot에 무엇이 있는가:**
- `JoltPhysicsServer3D` (`modules/jolt_physics/jolt_physics_server_3d.h`) — Jolt Physics를 Godot physics server로 사용 가능.
- `PhysicsDirectBodyState3D.get_contact_count()` — `scene/3d/physics/rigid_body_3d.cpp:163` — integration step 내 접촉점 개수.
- `PhysicsDirectBodyState3D.get_contact_local_position(i)` — 접촉점 local position.
- `PhysicsDirectBodyState3D.get_contact_collider(i)` — `rigid_body_3d.cpp:210` — 접촉 콜라이더 RID.

**접촉 기반 부력 근사:** 통합 스텝에서 `get_contact_count()` + `get_contact_local_position()`으로 수면 메시와의 접촉점을 얻고, 접촉점 깊이 × 면적 × 유체 밀도로 부력 계산. **단, 이 방식은 수면을 물리 콜라이더(정적 메시)로 표현해야 함** — FFT 오션의 동적 메시를 매 프레임 물리 콜라이더로 갱신하는 것은 비현실적.

**판정:** 🟡 **CPU-side Gerstner 근사(경로 A)가 1순위.** GPU readback(경로 B)은 async-compute 부재로 인한 stall 위험을 감내해야 함. 접촉 기반 부력은 동적 수면에 부적합. **장기적으로 async-compute 도입이 근본 해결.**


---

## 6. 경계 종합

### 6.1 판정 집계

| # | 격차 | Godot 실측 | 판정 | 공수 | 비고 |
|---|------|-----------|------|------|------|
| 1.1 | Tessendorf FFT 오션 | 0매치 | 🟡 | 4~8주 | GDExtension. `RenderingDevice` compute + `ArrayMesh` |
| 1.2 | Projected Grid LOD | 0매치 (ArrayMesh 동적 메시는 가능) | 🟢 | 1~2주 | GDExtension. `Camera3D` projection 활용 |
| 1.3 | 오션 셰이딩 (반사·굴절·프레넬·거품·코스틱·SSS) | SSR·Fresnel·depth 있음. 나머지 0매치 | 🟡 | 2~4주 | GDExtension. `CompositorEffect` + `access_resolved_depth/color` |
| 1.4 | 수중 포스트프로세스 | 0매치 | 🟡 | 1~2주 | GDExtension. `CompositorEffect` + `Environment` 전환 |
| 2.1 | 부력 | CPU-side 물리 + GPU readback 모두 가능 | 🟡 | 1~2주 | GDExtension. CPU 근사 권장 |
| 2.2 | 웨이크/폼 | GPUParticles3D + HeightField3D 실재 | 🟡 | 1~2주 | GDExtension. `CompositorEffect` foam RT |
| 2.3 | 수면 디포메이션 | 0매치 | 🟡 | 1~2주 | GDExtension. `CompositorEffect` displacement RT |
| 3.1 | 수중 볼류메트릭 | FogVolume + Environment.volumetric_fog 실재 | 🟢 | 3~5일 | GDExtension. FogMaterial 커스텀 셰이더 |
| 3.2 | 수중 오디오 | AudioServer.add_bus_effect + LPF 실재 | 🟢 | 1~2일 | GDScript/GDExtension. 오디오 문서 §1-(3) 주의 |
| 3.3 | 수중/수상 전환 | Area3D + Environment 전환으로 구현 | 🟢 | 2~3일 | GDScript/GDExtension. 전환 상태 머신 |
| 4.1 | Flow Map 강 | 0매치 (flow_map은 PBR anisotropy) | 🟡 | 1~2주 | GDExtension. Houdini 파이프라인 별도 |
| 4.2 | 폭포·급류 | ArrayMesh + GPUParticles3D 실재 | 🟢 | 3~5일 | GDExtension. |
| 5 | GPU Readback Bottleneck | async-compute 부재, staging buffer stall 위험 | 🟡 | 우회 1주 / 근본 man-year | CPU 근사로 우회. 근본 해결은 gaps §5 |

### 6.2 경계선 (Boundary) 분석

**GDExtension으로 가능 (🟢):**
- Projected Grid 메시 생성
- `CompositorEffect` 기반 오션 렌더 패스
- `FogVolume` + `Environment` 전환 기반 수중 볼류메트릭
- 수중 오디오 버스 전환 + LPF
- 수중/수상 전환 상태 머신
- 폭포 시트 + 스프레이 파티클

**GDExtension으로 가능하나 상당한 공수 (🟡):**
- Tessendorf FFT 오션 파이프라인 (4~8주)
- 오션 셰이딩 전체: 거품·코스틱·굴절·SSS (2~4주)
- Flow map 강 셰이더 (1~2주)
- 부력 시스템 (CPU 근사 or GPU readback, 1~2주)
- 웨이크/폼/수면 디포메이션 (1~2주)

**국소 코어 수정 필요 (🟡):**
- `particles_collision_get_heightfield_texture(RID) -> RID` getter (10줄 패치) — 날씨 문서 §2.3에서 제안된 코어 패치. 웨이크/폼 collision texture 재활용용.
- `Environment` 수중 전용 파라미터 추가 (수중 fog density·albedo·length 등) — 있으면 편리하나 없어도 구현 가능.

**대규모 코어 / man-year급 (🔴):**
- **Async-compute / multi-queue** — GPU readback 병렬화, 부력 실시간성 개선. gaps 문서 §5(obs 11757)와 동일한 과제.
- **Planar reflection** — SSR 이상의 고품질 수면 반사. 별도 반사 카메라 패스의 렌더링 파이프라인 통합 필요.
- **Underwater spectral rendering** — 파장별 흡수·산란의 물리 기반 구현. `FogMaterial`로 근사 가능하나 완전한 물리 정확도는 대규모 작업.
- **Full-Nanite ocean integration** — vis-buffer + deferred + streaming 파이프라인에서 오션 렌더링을 통합. gaps §1(경로 C)의 Nanite 네이티브 라이팅 통합과 동일한 man-year급.

### 6.3 의존 관계

```
Projected Grid (1.2) ← Tessendorf FFT (1.1) ← 오션 셰이딩 (1.3)
                                                  ↓
                          부력 (2.1) ← 웨이크/폼 (2.2) ← 수면 디포메이션 (2.3)
                                                  ↓
                          수중 PP (1.4) ← 수중 볼류메트릭 (3.1) ← 수중 오디오 (3.2)
                                                  ↓
                          수중/수상 전환 (3.3)
```

**핵심 경로:**
1. **Tessendorf FFT (1.1) + Projected Grid (1.2) + 오션 셰이딩 (1.3)** — 이 셋이 오션 렌더링의 최소 코어. 6~12주.
2. **부력 (2.1) + 수중/수상 전환 (3.3)** — 게임플레이 연동. 2~3주.
3. **수중 PP (1.4) + 수중 볼류메트릭 (3.1) + 수중 오디오 (3.2)** — 수중 경험. 1~2주.
4. **웨이크/폼 (2.2) + 수면 디포메이션 (2.3)** — 상호작용. 2~4주.
5. **Flow Map 강 (4.1) + 폭포 (4.2)** — 담수 시스템. 2~3주.

---

## 7. 우선순위 로드맵

### 7.1 Phase 1: 최소 오션 MVP (6~10주, 그래픽스 엔지니어 1명)

| 단계 | 항목 | 공수 | 누적 |
|------|------|------|------|
| P1.1 | Tessendorf FFT core (H0 + time evolution + IFFT) | 3~4주 | 3~4주 |
| P1.2 | Projected Grid + FFT displacement 연동 | 1~2주 | 4~6주 |
| P1.3 | 기본 오션 셰이딩 (SSR + Fresnel + depth) | 1~2주 | 5~8주 |
| P1.4 | CPU-side Gerstner 근사 부력 | 1~2주 | 6~10주 |

**P1 종료 시점:** FFT 오션이 렌더링되고, 오브젝트가 파도에 반응해 뜨고 가라앉는다. 수면 반사(SSR)·프레넬이 적용된다.

### 7.2 Phase 2: 오션 품질 향상 (4~6주, 그래픽스 엔지니어 1명)

| 단계 | 항목 | 공수 | 누적 |
|------|------|------|------|
| P2.1 | Jacobian foam + shore foam | 1~2주 | 7~12주 |
| P2.2 | 굴절 (screen-space, CompositorEffect) | 1주 | 8~13주 |
| P2.3 | 코스틱 (GPU Gems Ch.2 방식) | 1주 | 9~14주 |
| P2.4 | Cascaded FFT (4 cascades) | 1~2주 | 10~16주 |

**P2 종료 시점:** 거품·굴절·코스틱·카스케이드가 적용된 AAA급 오션 표면.

### 7.3 Phase 3: 수중 + 상호작용 (3~5주, 그래픽스 1명 + 오디오 0.5명)

| 단계 | 항목 | 공수 | 누적 |
|------|------|------|------|
| P3.1 | 수중/수상 감지 + Environment 전환 | 2~3일 | 10~17주 |
| P3.2 | 수중 depth fog + 색수차 (CompositorEffect) | 3~5일 | 11~18주 |
| P3.3 | 수중 볼류메트릭 (FogVolume + FogMaterial) | 3~5일 | 11~19주 |
| P3.4 | 수중 오디오 (버스 전환 + LPF) | 1~2일 | 11~19주 |
| P3.5 | 웨이크 파티클 + foam 누적 RT | 1~2주 | 12~21주 |
| P3.6 | 수면 디포메이션 (displacement RT) | 1~2주 | 13~23주 |

**P3 종료 시점:** 수중 경험과 수면 상호작용이 완성된 상태.

### 7.4 Phase 4: 담수 + 폴리시 (3~4주)

| 단계 | 항목 | 공수 | 누적 |
|------|------|------|------|
| P4.1 | Flow map 강 셰이더 | 1~2주 | 14~25주 |
| P4.2 | 폭포 시트 + 스프레이 | 3~5일 | 15~26주 |
| P4.3 | 급류 foam + spray | 2~3일 | 15~27주 |
| P4.4 | 전체 폴리시·튜닝·버그 | 1~2주 | 16~29주 |

**총 공수: 16~29주 (그래픽스 엔지니어 1명 풀타임). 오디오·파이프라인 엔지니어 부분 투입.**

### 7.5 장기: 코어 개조 (P5 이후)

| 항목 | 공수 | 설명 |
|------|------|------|
| Async-compute / multi-queue (gaps §5) | man-year | GPU readback 병렬화. 부력 실시간성. 렌더링 파이프라인 전반 성능 |
| Planar reflection 통합 | 2~3개월 | 별도 반사 카메라 패스의 렌더러 통합 |
| Heightfield texture getter (10줄) | 수 일 | 날씨 문서 §2.3 제안. `particles_collision_get_heightfield_texture` 코어 패치 |
| Full-Nanite ocean 통합 | man-year | vis-buffer + deferred + streaming에서 오션 렌더링 |

---

## 8. gaps 문서 §8 반영

### 8.1 마스터 요약표 §8 행

| # | 격차 | Godot 현황 (실측) | 대체 난이도 | 해결 경로 | 공수 |
|---|------|-------------------|-------------|-----------|------|
| 8 | **오션·수면·수중** | `ocean\|tessendorf\|Gerstner\|caustic\|foam\|refraction\|underwater` 전부 0매치. `CompositorEffect` + `RenderingDevice` compute API는 완비 | 🟡 (GDExtension 가능, 대규모 자작) | FFT 오션·셰이딩·부력·수중 전부 GDExtension으로 구현. `godot4-oceanfft` 오픈소스 기반 가속 가능. GPU readback은 async-compute 부재로 병목 — CPU-side 근사 부력으로 우회 | 16~29주 (그래픽스 1명) + man-year급 코어 개조(장기) |

### 8.2 gaps 문서 §8 삽입 위치

[godot-openworld-engine-gaps.md](./godot-openworld-engine-gaps.md)의 §7(VFX·시네마틱, 175행) 이후에 §8로 삽입. 현재 gaps 문서는 §7 이후 "판단 기준 정리" (194행)와 "전략적 결론" (208행)으로 이어지므로, §8을 §7과 "판단 기준 정리" 사이에 삽입.

**권장 삽입 텍스트 (gaps.md에 추가):**

```markdown
---
## 8. 오션·수면·수중 시스템 ⚠️ (정정됨: "전무"가 아니라 "GDExtension으로 대부분 구현 가능, 대규모 자작")
> **2026-08-18 실측.** Godot에 오션 렌더링은 개념 자체가 없다. 그러나 `CompositorEffect` + `RenderingDevice` compute API가 GDExtension에 완전 개방돼 있어 Tessendorf FFT 오션 파이프라인 전체를 코어 수정 없이 구현 가능하다. 진짜 병목은 오션 자체가 아니라 GPU→CPU readback(부력)과 async-compute 부재다. 상세: [godot-ocean-water-research.md](./godot-ocean-water-research.md).

**무엇이 없나**
- `ocean|tessendorf|Gerstner|caustic|foam|refraction|underwater|submerged` → `servers/`·`scene/`·`modules/` 전역 **0매치**. 어떤 형태의 오션 웨이브·셰이딩·수중 효과도 빌트인으로 존재하지 않는다.

**핵심 반전 — 이미 있는 토대**
- `CompositorEffect` (`scene/resources/compositor.h:39`) + `access_resolved_depth` (`compositor.cpp:52`) + `access_resolved_color` (`compositor.cpp:48`) — 별도 렌더 패스로 오션을 GDExtension에서 구현 가능. 코어 0.
- `RenderingDevice` compute API (`servers/rendering/rendering_device.h`) — GLSL compute shader로 FFT butterfly 구현 가능. groupshared memory + barrier 지원.
- `ArrayMesh` 동적 갱신 — Projected Grid 메시를 매 프레임 생성·갱신 가능.
- `FogVolume` + `Environment.volumetric_fog_*` — 수중 볼류메트릭을 코어 0으로 구현 가능.
- `AudioServer.add_bus_effect` + `AudioEffectLowPassFilter` — 수중 오디오 LPF 전환 가능.
- `GPUParticles3D` + `GPUParticlesCollisionHeightField3D` — wake/foam/spray 파티클 가능.
- `buffer_get_data_async` (`rendering_device.h:269`) — GPU→CPU readback 가능하나 1~2프레임 지연.

**진짜 남는 격차**
- ① **Tessendorf FFT 오션** — 전무. GDExtension으로 구현 가능하나 4~8주. `godot4-oceanfft` 오픈소스로 가속 가능.
- ② **오션 셰이딩(거품·코스틱·굴절·SSS)** — 전무. SSR·Fresnel·depth는 재활용. 나머지는 자작 셰이더. 2~4주.
- ③ **GPU Readback Bottleneck** — async-compute/multi-queue 부재(gaps obs 11757)로 인한 pipeline stall 위험. CPU-side Gerstner 근사 부력으로 우회 가능. 근본 해결은 man-year.
- ④ **Planar reflection** — SSR 이상의 수면 반사. 별도 반사 카메라 패스 필요. 2~3개월.
- ⑤ **Flow map 강** — 0매치. 자작 셰이더 + Houdini 저작 파이프라인. 1~2주.

**경로 (난이도순)**
- 🟢 P1: Projected Grid + FogVolume 수중 + 수중 오디오 + 수중/수상 전환 (1~2주) → 🟡 P2: Tessendorf FFT + 기본 오션 셰이딩 + CPU 부력 (6~10주) → 🟡 P3: foam·caustics·굴절·cascades·wake·surface deformation (4~6주) → 🟡 P4: flow map 강·폭포·폴리시 (3~4주) → 🔴 P5: async-compute·planar reflection·Nanite ocean 통합 (man-year).
```

### 8.3 gaps 문서 전략적 결론 영향

현재 gaps 문서의 "전략적 결론" (208~225행)은 §0~§7만 반영. §8(오션) 추가 시:

1. **"man-year급으로 재확인되는 항목" (223행)에 추가:** §8은 man-year급 항목이 아님 — GDExtension으로 대부분 구현 가능. 단, async-compute(§5)와 planar reflection은 man-year급.
2. **"업스트림에서 기대할 수 있는 건 사실상 하나뿐" (220행) 영향 없음:** 오션은 업스트림에 proposal조차 없으므로 포크에서 직접 구축이 유일한 경로.
3. **"선행 순서" (216행):** §0(월드 스트리밍·지형) → §1(Nanite) → §8(오션). 오션은 지형·월드가 존재해야 배치 가능.

### 8.4 교차참조 요약

| 참조 문서 | 관련 절 | 내용 |
|-----------|---------|------|
| `godot-weather-atmosphere-research.md` | §2 (강수·젖음) | `GPUParticlesCollisionHeightField3D`의 depth FB를 rain occlusion + wake/foam 충돌로 재활용. heightfield texture getter(10줄 코어 패치) 공유 |
| `godot-audio-propagation-research.md` | §1-(3) (Area3D reverb) | 수중 오디오 버스 전환 시 Area3D의 `audio_bus_override` + `reverb_bus_enabled` 동시 on 필요. 드라이 신호 유실 주의 |
| `godot-audio-propagation-research.md` | §2 (per-source LPF) | `AudioServer::set_playback_highshelf_params` 미바인딩 → per-source LPF 불가. 버스 단위 LPF(`add_bus_effect`)로 수중 오디오 구현 |
| `godot-openworld-engine-gaps.md` | §5 (obs 11757) | async-compute/multi-queue 부재 → GPU readback 병목. FFT displacement → 부력의 GPU→CPU 읽기에 직접 영향 |
| `godot-openworld-engine-gaps.md` | §3 (Virtual Texturing) | `buffer_get_data_async`의 동일 메커니즘이 VT feedback pass와 오션 부력 readback에 공통 사용 |

---

> *리서치 방법: Godot 4.8-dev(`eda2a482e9`) 로컬 소스트리 실측 우선 — 모든 부정 주장에 검색어와 매치 수를 병기(`ocean|tessendorf|Gerstner|caustic|foam|refraction|underwater|submerged|flow_map` 등이 전부 0매치). `CompositorEffect` (`compositor.h:39-96`, `compositor.cpp:39-157`), `RenderingDevice` (`rendering_device.h:268-269`, `rendering_device.cpp:1329-1413`), `Environment` (`environment.h:203-216`), `FogVolume` (`fog_volume.cpp:41-107`), `AudioServer`, `GPUParticlesCollisionHeightField3D` (`renderer_scene_cull.cpp:4119-4157`)를 직접 판독해 기능 경계를 확정. 웹 리서치로 Sea of Thieves (SIGGRAPH 2018), Horizon Forbidden West (SIGGRAPH 2022), AC: Black Flag (GDC 2014), Portal 2 flow map (SIGGRAPH 2010), Tessendorf FFT 구현체(timethy.com, barthpaleologue.github.io)의 기술 상세를 보강. **적대적 재검증 결과:** "오션은 코어 개조 없이 불가능하다"는 주장은 기각 — `CompositorEffect` + `RenderingDevice` compute로 FFT 파이프라인 전체가 GDExtension 가능. 진짜 병목은 오션 자체가 아니라 GPU readback의 async-compute 의존성.*
