# Godot 기상 / 대기 / 햇빛 시스템 — 딥리서치 & 구현 경계

> `godot-openworld-engine-gaps.md`가 다루지 않은 **날씨·대기·태양광** 축을 같은 방법론으로 조사한 문서.
> 방법론: 로컬 4.8-dev 소스트리 실측(file:line) 우선 → 웹 1차 소스 fan-out → 적대적 재검증("불가" 주장은 반증 시도).
>
> **중복 회피**: 대기 산란·볼류메트릭·그림자는 GI와 물리적으로 겹친다. GI/반사/SDFGI/HW-RT 관련 판단은
> [`godot-lumen-gi-implementation-research.md`](./godot-lumen-gi-implementation-research.md)에 있으므로 **여기서는 재서술하지 않고 교차참조**한다.
> VFX 저작(파티클 에디터 UX·시퀀서)은 [`godot-vfx-cinematic-research.md`](./godot-vfx-cinematic-research.md) 참조.
>
> ---
> **⚠️ 2026-08-18 개정 — 품질 기준이 바뀌었다. 이 헤더가 문서 전체에 우선한다.**
>
> 초판은 **"원신급 스타일라이즈드"**를 목표로 우선순위를 매겼다. **그 전제는 폐기됐다.**
> 새 기준은 **AAA**: UE5 Sky Atmosphere / Volumetric Cloud, Horizon Zero Dawn(Decima/Nubis), RDR2, Cyberpunk 2077.
> **원신은 목표가 아니라 하한 참조점이다.**
>
> 프로젝트 전략도 바뀌었다: upstream 분리 **포크**, **딥 코어 개조 허용**.
> → **"코어 수정 필요"는 더 이상 탈락 사유가 아니다.** 유일한 탈락 기준은 **man-year급인가**이다.
>
> 이에 따라 뒤집힌 판정은 **§14 델타 표**에 전부 정리했다. 본문 §0·§4·§5·§6·§9~§12는 AAA 기준으로 재작성됐다.
> **§1~§8의 실측(file:line·grep·PR 번호)은 아트 디렉션과 무관하므로 그대로 유효하며 수정하지 않았다.**
> ---

---

## 0. 한 장 요약 — **AAA 기준(2026-08-18 재채점)**

기준선: UE5 Sky Atmosphere(Hillaire 2020 4-LUT) / UE5 Volumetric Cloud / Decima-Nubis(HZD·Forbidden West) / RDR2 / CP2077.
"코어 수정 필요"는 감점 요인이 **아니다**(포크 전략). 감점은 **man-year급 신규 서브시스템**일 때만.

| # | 항목 | AAA가 요구하는 것 | 이미 있는 토대 | 경계 | 공수 | 신호 |
|---|------|-------------------|----------------|------|------|------|
| 1 | 비(강수) | 오클루전 마스크 + 스플래시 + 리플. AAA에서도 **탑다운 뎁스 마스크가 표준 기법** | `GPUParticlesCollisionHeightField3D` = 이미 **탑다운 depth-only 패스**(D32_SFLOAT, ≤8192, follow-camera) | 코어 **~10줄**(getter 1개). 단 AAA는 **하이트필드 1개/32콜라이더/dispatch=amount 제약을 정면으로 맞는다** → 전용 탑다운 RT 서브시스템 분리가 정공 | 1~2주(MVP) / +2~3주(전용화) | 🟢 |
| 2 | 젖음(wetness) | 누적 마스크 + 젖은 BRDF + 흐름/낙수 + 리플 상호작용 | `global uniform` 버스가 spatial/particles/sky/fog **전 셰이더 타입**에 도달 + `Texture2DRD` | 코어 0. **BaseMaterial3D 미대응**은 AAA에선 오히려 무의미 — 커스텀 셰이더 라이브러리가 전제 | 3~6주 | 🟢 |
| 3 | 대기 산란 | **Bruneton/Hillaire 4-LUT 다중산란 + 프러스텀 AP 볼륨**. UE5가 하는 그것 | `PhysicalSkyMaterial` = Preetham **단일산란**(three.js Sky.js 포팅) — AAA 미달. sky 셰이더 자유도·global sampler3D·`RenderingDevice` 컴퓨트는 완비 | LUT 생성+sky 조회 = **코어 0** / 씬 픽셀 AP = **국소 코어**(`fog_process` @ `scene_forward_clustered.glsl:1099`에 sampler3D froxel 조회가 **이미 있음** → 같은 자료구조 재사용) | 6~10주 | 🟡 |
| 4 | 볼류메트릭 구름 | **레이마치 구름 + 근경 관통(비행/등반) + 구름 그림자 + 시간적 재투영**. Nubis/UE5 수준 | sky `use_quarter_res_pass`(원경, 코어 0) / `FogVolume`+fog 셰이더 = CSM 그림자·HG·GI 주입·16프레임 재투영이 **작동하는 참여매질 솔버** / **`EFFECT_CALLBACK_TYPE_POST_SKY`**(`compositor.h:46`, 디스패치 `render_forward_clustered.cpp:2333`) = 뎁스 확보 후 sky 위 합성 지점 | 원경 = 코어 0 / **근경 관통 = 코어 0**(POST_SKY + `access_resolved_depth`) / 반사·GI 반영 = 국소 코어 | 2~4개월 | 🟡 |
| 5 | 햇빛·그림자 | 정적/동적 분리 캐싱, 컨택트 섀도우, DF 또는 RT 소프트 섀도우 | CSM 1/2/4, 아틀라스 **최대 16384**(`rendering_server.cpp:3680`), LightmapGI 셰도우마스크, 포지셔널엔 **캐싱 로직 실재** / SDFGI 캐스케이드에 **실제 SDF 텍스처 존재**(`gi.h:581 sdf_tex`) / **RT API가 ClassDB에 전부 바인딩됨**(`rendering_device.cpp:9126-9187`) | 캐싱 = 중간 코어 / DF 섀도우 = SDFGI SDF 재사용 = 중간 코어 / HW-RT = **씬측 BLAS·TLAS 생명주기만 결손** | 2~5개월 | 🟡 |
| 6 | 시간대(TOD) | 궤도·램프·노출 적응·베이크 충돌 해소 | Sky `PROCESS_MODE_INCREMENTAL`, `CameraAttributes` **auto_exposure 실재**(`camera_attributes.h:50-55`), 색보정 LUT, 4-라이트 sky | **코어 0** — 순수 GDScript/GDExtension | 1~2주 | 🟢 |
| 7 | 바람 | 전역+지역 계층 바람, 폴리지/천/파티클/구름 이류 동기화 | `global uniform` 버스 = 폴리지·천·파티클·sky·fog를 하나의 소스로 묶는 정식 경로 | **코어 0** | 1~3주 | 🟢 |

**한 줄 결론 (AAA 재채점)**: 판정 자체는 **살아남았다 — 그러나 이유가 바뀌었다.**
초판은 "물리기반 대기·구름을 **안 만들어도 되니까** 격차가 아니다"였다. **그 논거는 무효다.** AAA에선 둘 다 **필수**다.
그럼에도 결론이 유지되는 이유는 다르다: **둘 다 만들 수 있고, man-year급이 아니기 때문이다.**
- 대기 4-LUT는 **사전계산 알고리즘**이라 엔진 개조가 아니라 컴퓨트 셰이더 저작이다 → sky 적용까지 **코어 0**.
- 근경 관통 구름은 **`POST_SKY` 콜백 + `access_resolved_depth`로 코어 0**이다(초판은 이 콜백을 놓쳤다).
- 남는 진짜 코어 작업은 **씬 픽셀 AP(국소)**, **그림자 캐싱(중간)**, **구름의 반사/GI 반영(중간)**, **HW-RT 씬 통합(중간~대)** — 전부 **월(月) 단위지 연(年) 단위가 아니다.**

**단, 총량은 초판의 2~3배로 재산정된다: P0~P3 합계 3~4주 → AAA 기준 8~14개월(1인 기준).**
"격차 아님"은 **난이도 판정**이지 **공수 판정이 아니다.**

---

## 1. 실측 방법과 근거 표기

- 대상: 로컬 `master` = **Godot 4.8-dev** (`version.py` major=4/minor=8/status=dev, HEAD `eda2a482e9`).
- 모든 `file:line`은 본 세션에서 직접 grep/read한 결과다. **"없다"는 주장도 검색어와 매치 수를 함께 적는다.**
- 렌더러 기준은 **Forward+ (`RenderForwardClustered`)**. Mobile/Compatibility 차이는 해당 지점에서만 언급.

### 1.1 이 문서 전체를 관통하는 단일 발견 — 글로벌 셰이더 유니폼 버스

날씨 시스템의 본질은 "하나의 상태(비 강도, 젖음, 바람, 시각)를 **전 머티리얼에 뿌리는 것**"이다.
Godot에는 이 목적에 정확히 맞는 것이 이미 있고, **모든 셰이더 타입에 연결돼 있다**:

```
global_shader_uniforms 스토리지 버퍼가 바인딩된 셰이더:
  servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered_inc.glsl   (spatial)
  servers/rendering/renderer_rd/shaders/forward_mobile/scene_forward_mobile_inc.glsl          (spatial/mobile)
  servers/rendering/renderer_rd/shaders/particles.glsl:15                                     (particles)
  servers/rendering/renderer_rd/shaders/environment/sky.glsl:56                               (sky)
  servers/rendering/renderer_rd/shaders/environment/volumetric_fog.glsl:22                    (fog)
  servers/rendering/renderer_rd/shaders/canvas_uniforms_inc.glsl                              (canvas_item)
```

- API: `RenderingServer.global_shader_parameter_add/set/set_override/remove` — **전부 ClassDB 바인딩됨**
  (`servers/rendering/rendering_server.cpp:3472~3478`) → GDScript에서 런타임 등록·갱신 가능.
- 프로젝트 설정 경로는 `shader_globals/<name>` (`.../material_storage.cpp:1954`), 에디터 UI는
  `editor/shader/shader_globals_editor.cpp`.
- 타입: bool/int/uint/float/vec/mat/transform + **`sampler2D` / `sampler2DArray` / `sampler3D` / `samplerCube`**
  (`servers/rendering/rendering_server_enums.h:876~905`). → **텍스처 마스크도 전역으로 뿌릴 수 있다.**
- 버퍼 크기 기본 **65536** 슬롯 (`rendering_server.cpp:3794`), RD 백엔드는 최소 4096 강제
  (`material_storage.cpp:1511`).

**성능 실측 (중요)**: `global_shader_parameter_set`은 CPU 배열에 쓰고 1024-슬롯 단위 dirty region만 표시한다
(`material_storage.cpp:1876-1877`, `BUFFER_DIRTY_REGION_SIZE = 1024` @ `material_storage.h:203`).
업로드는 프레임당 dirty region만 (`_update_global_shader_uniforms`, `material_storage.cpp:2117-2138`;
전체의 25% 이상 더러우면 통짜 업로드). → **스칼라/벡터 전역값을 매 프레임 갱신하는 비용은 사실상 0.**

**단, 텍스처 전역값은 다르다**: 값(RID)을 바꾸면 그 텍스처를 쓰는 **모든 머티리얼의 uniform set 재생성이 큐잉**된다
(`material_storage.cpp:1879-1886`). → 젖음 마스크·레인 오클루전 맵은 **RID를 한 번만 설정하고 그 안에 매 프레임 렌더**해야 한다.
`Texture2DRD`(`scene/resources/texture_rd.h:41`)가 RD 텍스처 RID를 `Texture2D`로 감싸주므로 이 패턴이 성립한다.

### 1.2 이 버스의 유일한 구멍 — 빌트인 생성 머티리얼

`BaseMaterial3D`·`ParticleProcessMaterial`·`ProceduralSkyMaterial`·`FogMaterial`은 **C++이 셰이더 코드를 문자열로 생성**하며,
생성 코드에 글로벌 유니폼 참조가 **하나도 없다**:

- `BaseMaterial3D::_update_shader` (`scene/resources/material.cpp:656`) — `global` 키워드 0매치.
- `ParticleProcessMaterial` — `code += "uniform vec3 direction;\n"` 식의 평범한 지역 uniform만
  (`scene/resources/particle_process_material.cpp:208~`).

→ **결론**: 전역 젖음/바람을 쓰려면 **커스텀 `ShaderMaterial`을 프로젝트 표준으로 삼아야 한다.**
회피책 3가지: (a) 에디터의 "Convert to ShaderMaterial"(생성 코드에 그 목적의 주석이 박혀 있다), (b) `next_pass`로 젖음 오버레이 1패스 추가
(`scene/resources/material.h:44`), (c) `BaseMaterial3D` 생성기에 젖음/바람 훅을 넣는 국소 코어 패치.
**(a)가 정답**이다 — 오픈월드는 어차피 커스텀 셰이딩(툰/스타일라이즈드)이 필요하다.

---

## 2. 비(강수)

### (a) 무엇이 없나
- 강수 시스템 자체가 없다. `scene/`·`servers/`에서 `rain|raindrop|snow|puddle|wetness` → **0매치**.
- **레인 오클루전(처마 밑 판정) API가 없다.** 정확히는 — 데이터는 엔진이 이미 만들고 있는데 **핸들이 스크립트에 노출되지 않았다**(아래).

### (b) 이미 있는 토대 (실측)

**GPU 파티클 시뮬 능력** (`servers/rendering/renderer_rd/shaders/particles.glsl`):
- 충돌 빌트인: `COLLIDED` / `COLLISION_NORMAL` / `COLLISION_DEPTH` (`servers/rendering/shader_types.cpp:432~434`).
- **`SUB_EMITTER_AT_COLLISION`** (`scene/resources/particle_process_material.h:99`) → 빗방울 충돌 지점에서 스플래시 파티클 자동 방출. 강수의 핵심 요구가 빌트인.
- 난류(`turbulence_*`), 어트랙터(벡터필드 포함), 트레일(빗줄기), `TRANSFORM_ALIGN_Y_TO_VELOCITY`(빗방울 정렬),
  `local_coords`(카메라 고정 강수 박스), `visibility_aabb`, `fixed_fps`, `interpolate`
  (`scene/3d/gpu_particles_3d.h:44~96`).
- spatial 셰이더에서 `hint_depth_texture` 사용 가능 → 소프트 파티클(지면 관통 방지).

**파티클 수 한계 — 하드 캡은 없다**:
- `GPUParticles3D::set_amount`는 `p_amount < 1`만 막는다 (`scene/3d/gpu_particles_3d.cpp:81`). 상한 없음.
- 메모리: `ParticleData` = mat4(64) + vec3+uint(16) + vec4 color(16) + vec4 custom(16) = **최소 112B/파티클**
  (`particles.glsl:98~`). 10만 파티클 ≈ 11MB + 인스턴스 버퍼.
- **진짜 비용은 dispatch다**: 프로세스 컴퓨트가 `amount` 전체에 대해 매 프레임 도는다
  (`particles_storage.cpp:1230` `compute_list_dispatch_threads(compute_list, process_amount, 1, 1)`).
  `amount_ratio`는 **방출률만 줄이고 dispatch는 안 줄인다**. → 비 강도에 따라 `amount`를 **재설정**해야 실질 절약이 되는데,
  `set_amount`는 `_particles_free_data()`로 버퍼를 통째로 재할당한다(`particles_storage.cpp:341`) → 프레임 히칭.
  **실무 결론: 강도별로 emitter를 몇 개 미리 만들어 켜고 끄는 편이 안전하다.**

**충돌 정확도 — 실측 한계**:
- 콜라이더는 파티클 시스템당 **최대 32개**, 어트랙터도 32개 (`particles_storage.h:76-77` `MAX_ATTRACTORS/MAX_COLLIDERS = 32`).
- **하이트필드 콜라이더는 시스템당 단 1개**만 유효 — 두 번째부터 `if (collision_heightmap_texture != RID()) { continue; }`로 무시
  (`particles_storage.cpp:1057-1058`).
- SDF 콜라이더는 3D 텍스처 슬롯 제한(`MAX_3D_TEXTURES = 7` @ `particles_storage.h:78` — 시스템당 SDF 콜라이더 **최대 7개**)에 걸리고, 베이크 필요, 해상도 16~512
  (`scene/3d/gpu_particles_collision_3d.h:101-108`, 기본 `RESOLUTION_64` @ `:117`).
- 하이트필드 충돌은 2.5D — 셰이더가 높이 텍스처 1샘플로 판정하고 유한차분으로 법선을 만든다
  (`particles.glsl:622-651`). **오버행 표현 불가**는 공식 문서도 명시
  (`doc/classes/GPUParticlesCollisionHeightField3D.xml:9` — *"heightmaps cannot represent overhangs (e.g. indoors or caves)"*,
  같은 줄에 *"a good choice for weather effects such as rain and snow"*).

### (c) 레인 오클루전 — 핵심 반전: **엔진이 이미 그리고 있다**

이 문서에서 가장 중요한 단일 실측이다. `GPUParticlesCollisionHeightField3D`의 내부 구현을 따라가면:

```
scene/3d/gpu_particles_collision_3d.cpp:675,706   → RS::particles_collision_height_field_update()
servers/rendering/renderer_scene_cull.cpp:4155     → scene_render->render_particle_collider_heightfield(...)
servers/rendering/renderer_rd/renderer_scene_render_rd.cpp:1516-1533
    cm.set_orthogonal(-extents.x, extents.x, -extents.z, extents.z, 0, extents.y * 2.0);   // 직교 투영
    cam_pos.y += extents.y;                                                                 // 위에서
    cam_xform.set_look_at(cam_pos, cam_pos - p_transform.basis.get_column(AXIS_Y), ...);     // 아래를 봄
servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp:2910-2940
    scene_data.shadow_pass = true;   // "Not a shadow pass, but should be treated like one."
    PassMode pass_mode = PASS_MODE_SHADOW;                                                   // depth-only
```

그리고 텍스처 포맷 (`particles_storage.cpp:1888-1897`):
```
tf.format     = RD::DATA_FORMAT_D32_SFLOAT;
tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
해상도 = {256, 512, 1024, 2048, 4096, 8192} 중 선택(기본 1024), 긴 축 기준·종횡비 보존
```

즉 **"플레이어 위 직교 카메라로 지오메트리를 depth-only로 그린 32비트 뎁스맵"** — 이게 바로 프로덕션 게임들이 쓰는
레인 오클루전 맵 그 자체이고, `SAMPLING_BIT`까지 이미 켜져 있다. 게다가:
- `follow_camera_mode` (`gpu_particles_collision_3d.h:232`) — 카메라 추종.
- `update_mode = WHEN_MOVED | ALWAYS` (`h:223-226`) — 갱신 빈도 선택.
- `heightfield_mask` 20비트 — 어떤 레이어를 오클루더로 칠지 선택 (`h:229`).

**빠진 것은 단 하나**: `heightfield_texture`를 얻는 공개 API. `particles_collision_get_heightfield_framebuffer`는
`particles_storage.h:600`에 있으나 **`rendering_server.cpp`의 ClassDB 바인딩에 0매치** → 스크립트 불가.

### (c') 구현 경계 — 3가지 경로

| 경로 | 코어 수정 | 비용 | 평가 |
|------|-----------|------|------|
| **A. `particles_collision_get_heightfield_texture(RID) -> RID` getter 추가** | **~10줄** (storage getter + RS 가상 + ClassDB bind) | 추가 렌더 비용 **0** (이미 그리는 패스 재활용) | 🟢 **압도적 1순위.** `Texture2DRD`로 감싸 `global uniform sampler2D rain_occlusion`에 1회 주입 |
| **B. SubViewport + 직교 Camera3D + `CompositorEffect`** | **0** | 씬을 **한 번 더 full forward 패스**로 그림 (depth-only 아님) | 🟡 코어 0이 절대조건일 때만. `RenderSceneBuffersRD.get_depth_texture()`가 스크립트 바인딩돼 있음(`render_scene_buffers_rd.cpp:66`), `Camera3D.compositor`는 SubViewport 카메라에도 적용됨(`renderer_scene_cull.cpp:3751 _render_get_compositor(p_camera, ...)`) |
| **C. 아래를 향한 `DirectionalLight3D`의 그림자맵 재사용** | 코어(그림자 아틀라스 노출) | 0 | 🔴 태양광 그림자와 충돌. 비추천 |

**적대적 재검증**: "레인 오클루전은 Godot에서 불가"라는 주장은 **반증됐다**. B로 코어 0에 가능하고,
A로는 추가 GPU 비용 0에 가능하다. 유일한 진짜 제약은 하이트필드의 **2.5D 한계**(동굴 내부·다층 구조물에서
"위에서 본 첫 표면"만 알 수 있음) — 이건 UE5를 포함한 업계 표준 기법의 공통 한계지 Godot 고유의 결함이 아니다.

### (d) 공수
- 파티클 강수(강도 커브·스플래시·빗줄기): **3~5일** (순수 콘텐츠).
- 레인 오클루전 경로 A(코어 패치 + Texture2DRD + 글로벌 유니폼 + 파티클 셰이더 kill 로직): **3~5일**.
- 실내/처마 판정을 젖음(§3)·오디오·이펙트와 묶는 통합: **1~2주**.

### (e) 현실적 대안 — **충실도 비용 명시 (AAA 재채점)**

| 우회로 | 무엇을 열화시키는가 | AAA 허용 여부 |
|--------|---------------------|---------------|
| 레이캐스트 1회 + 실내 트리거 볼륨 | **픽셀 단위 오클루전이 사라진다.** 처마 경계에서 비가 칼같이 끊기고, 나뭇잎 사이 얼룩진 비 그림자(dappled rain)가 불가. 원신 수준에선 통과, **AAA에선 즉시 티가 난다** | ❌ 프로토타입 한정 |
| 하이트필드 1개를 오클루전+충돌 겸용 | 해상도·커버리지를 두 용도가 나눠 쓴다. 오클루전은 넓게(≥256m), 충돌은 좁고 촘촘하게(≤64m) 원하므로 **둘 중 하나가 반드시 열화**. AAA는 캐스케이드 2~3단 탑다운 마스크를 쓴다 | ⚠️ MVP까지만 |
| **전용 탑다운 RT 서브시스템(정공)** | 열화 없음. `GPUParticlesCollisionHeightField3D`의 직교+depth-only 렌더 경로를 **복제해 파티클 스토리지에서 분리**하고, 근/원 2캐스케이드로 구성 | ✅ **AAA 정답** |

**AAA 기준 추가 결손 3건** (초판이 "실무 결론"으로 우회했으나 AAA에선 정면으로 맞는다):
1. **하이트필드 시스템당 1개**(`particles_storage.cpp:1057-1058`) — 캐스케이드 오클루전 불가. → 전용화로만 해결.
2. **파티클 dispatch = `amount` 전량**(`particles_storage.cpp:1230`) — 폭우 10만 파티클을 상시 dispatch. AAA는 강도에 따라 실제 워크로드가 스케일해야 한다. `set_amount` 재할당 히칭(`:341`) 때문에 emitter 프리셋 전환이 우회로지만, **정공은 `amount_ratio`를 dispatch 크기에 반영하는 국소 코어 패치(~20줄)**다. 포크 전략에선 이쪽이 맞다.
3. **2.5D 오버행 불가** — 이건 UE5 포함 업계 공통 한계이므로 **감점 아님**. AAA도 실내는 볼륨으로 처리한다.

### (f) 신호: 🟢 — **AAA에서도 🟢 유지.** 기법이 업계 표준과 동일하고, 결손 3건이 전부 수십 줄 국소 패치다.
초판 대비 공수만 상향(1~2주 → 3~5주): 전용 탑다운 RT 캐스케이드 + dispatch 패치가 추가된다.

---

## 3. 젖음 (Wetness)

### (a) 무엇이 없나
- `wetness|puddle` grep → `scene/`·`servers/` **0매치**. 젖음 개념 자체가 엔진에 없다.
- 전역 젖음 누적 마스크(RT ping-pong), 젖은 BRDF 변조, 웅덩이·리플 — 전부 없음.

### (b) 이미 있는 토대 (실측)
- **§1.1의 글로벌 유니폼 버스**가 그대로 답이다. `global uniform float wetness;`,
  `global uniform sampler2D wetness_mask;`, `global uniform vec4 wetness_world_bounds;`를 등록하면
  spatial·particles·sky·fog 셰이더가 **전부** 읽는다.
- 누적 마스크 ping-pong: `RenderingServer.get_rendering_device()` / `create_local_rendering_device()`
  (`servers/rendering/rendering_server.h:1037-1038`)로 컴퓨트 패스를 돌리고 `Texture2DRD`로 노출.
  또는 `SubViewport` 2개 + `ColorRect` 셰이더로 GDScript만으로도 성립.
- 젖은 BRDF에 필요한 재료는 `BaseMaterial3D`에도 전부 있다: `FEATURE_CLEARCOAT`, `FEATURE_DETAIL`,
  roughness/specular 채널 (`scene/resources/material.h:210~222`). 다만 §1.2 때문에 **커스텀 셰이더로 옮겨야** 전역 주입이 된다.
- 웅덩이 반사: `Environment` **SSR** 내장 (`scene/resources/environment.h:123-128`, `ssr_max_steps` 기본 64) +
  `ReflectionProbe` 아틀라스(기본 256px×64개, `rendering_server.cpp:3709-3711`).
  스타일라이즈드에는 SSR + 스카이 큐브맵으로 충분.

### (c) 구현 경계
- **코어 수정 0.** 전부 GDScript/GDExtension + 커스텀 셰이더 영역.
- 유일한 코어 유혹은 "`BaseMaterial3D`가 글로벌 젖음을 읽게 하기"인데, 이건 **하지 마라** —
  스타일라이즈드 오픈월드는 어차피 커스텀 셰이더 라이브러리를 갖게 된다. `BaseMaterial3D` 개조는
  업스트림 diff만 키우고 이득이 없다.

### (d) 공수
- 전역 젖음 파라미터 + 젖은 BRDF 함수(공용 `.gdshaderinc`) + 머티리얼 컨벤션: **1주**.
- 월드 공간 누적 마스크(ping-pong RT, 카메라 추종, 건조 감쇠) + 레인 오클루전 연동: **1~2주**.
- 웅덩이 마스크(하이트/AO 기반), 리플 노멀, 낙수/흐름: **1~2주**.

### (e) 현실적 대안
- 마스크 없이 **전역 스칼라 젖음 하나**만으로도 체감 품질의 70%가 나온다(원신·젤다급 스타일에서).
  웅덩이는 지형 머티리얼의 고정 마스크로.
- 젖음 마스크 해상도는 플레이어 주변 **한 장(예: 512²이 256m 커버)**이면 충분.

### (f) 신호: 🟢

---

## 4. 대기 산란 (Atmospheric Scattering)

### (a) 무엇이 없나 — 실측된 Godot의 실제 모델

`scene/resources/3d/sky_material.cpp`를 직접 읽어 확인했다. 셰이더 코드가 C++ 문자열로 박혀 있어 모델이 명시적이다.

**`ProceduralSkyMaterial`** (`sky_material.cpp:300~`) — **물리 모델이 아니다.**
- `sky_top_color`↔`sky_horizon_color`, `ground_bottom_color`↔`ground_horizon_color` 2단 그라디언트 + 커브,
  태양 디스크 근사(`sun_angle_max`/`sun_curve`), 옵션 `sky_cover` 파노라마 텍스처 오버레이 + `sky_cover_modulate`.
- 산란 계산 **없음**. 순수 아트 디렉션 도구. → **스타일라이즈드엔 이게 정답이다**(§9).

**`PhysicalSkyMaterial`** (`sky_material.cpp:732~820`) — **Preetham 계열 단일 산란 아날리틱 모델**.
소스 주석이 직접 말한다:
```
// mie coefficients from Preetham                                         (sky_material.cpp:778)
// Hack from https://github.com/mrdoob/three.js/.../objects/Sky.js         (sky_material.cpp:799)
const float rayleigh_zenith_size = 8.4e3;  // Optical length at zenith for molecules
const float mie_zenith_size = 1.25e3;
float optical_mass = 1.0 / (zenith + 0.15 * pow(3.885 + 54.5 * zenith, -1.253));   // Kasten-Young 공기질량
vec3 extinction = exp(-(rayleigh_beta * rayleigh_scatter + mie_beta * mie_scatter));
float rayleigh_phase = (3.0 / (16.0 * PI)) * (1.0 + pow(cos_theta * 0.5 + 0.5, 2.0));
float mie_phase = henyey_greenstein(cos_theta, mie_eccentricity);
```
즉 **three.js `Sky.js`(Preetham/O'Neil 하이브리드)의 직역 포팅**이다. 확정 한계:
1. **단일 산란만**. 다중 산란(multi-scattering) 항 없음 → 지평선·박명·고고도 하늘이 실제보다 어둡다.
2. **Hosek-Wilkie도 Bruneton도 아니다.** 사전계산 LUT 전무 — 매 픽셀 아날리틱 평가.
3. **행성 곡률/고도 개념 없음.** 카메라 고도에 따른 대기 변화 불가(비행·등산에서 티가 난다).
4. **`LIGHT0`만 사용** — 달빛(LIGHT1)은 sky 셰이더 빌트인엔 있지만 이 머티리얼은 안 쓴다.
5. `Lin`에 `pow(..., 1.5)`, 최종 `pow(color, 1/(1.2+1.2*sun_fade))` 같은 **비물리 톤 해킹**이 박혀 있다.

**Aerial perspective(공기원근)** — Godot에 이름은 있으나 **실체가 다르다.**
`Environment.fog_aerial_perspective` (`scene/resources/environment.h:190`)의 구현은
`scene_forward_clustered.glsl:1102-1120`:
```glsl
if (scene_data_block.data.fog_aerial_perspective > 0.0) {
    vec3 cube_view = scene_data_block.data.radiance_inverse_xform * vertex;
    // mip_level always reads from the second mipmap and higher so the fog is always slightly blurred
    sky_fog_color = <radiance 큐브맵 샘플>;
    fog_color = mix(fog_color, sky_fog_color, scene_data_block.data.fog_aerial_perspective);
}
```
→ **"안개 색을 시선 방향 스카이 큐브맵 색으로 치환"**하는 근사다. 거리별 in-scattering/transmittance 적분이 아니다.
UE5/Hillaire식 aerial-perspective 프로스텀 볼륨과는 다른 물건. 이름만 같다.

### (b) 이미 있는 토대
- `shader_type sky`는 **커스텀 셰이더를 완전히 허용**한다. 빌트인 (`servers/rendering/shader_types.cpp:460~500`):
  `EYEDIR`, `POSITION`(카메라 월드 위치 → **고도 기반 대기 가능**), `TIME`, `RADIANCE`(samplerCube),
  `AT_CUBEMAP_PASS`/`AT_HALF_RES_PASS`/`AT_QUARTER_RES_PASS`, **`LIGHT0..LIGHT3`의 ENABLED/DIRECTION/ENERGY/COLOR/SIZE**
  (→ 태양+달+보조광 4개), 출력 `COLOR`/`ALPHA`/**`FOG`**(볼류메트릭 포그 유니폼셋이 sky에도 바인딩됨,
  `sky.cpp:256`,`sky.cpp:781 actions.renames["FOG"] = "custom_fog"`).
- **글로벌 유니폼이 sky 셰이더에 도달한다**(`sky.glsl:56`) → TOD 상태를 sky에 그대로 주입.
- LUT 텍스처는 `global uniform sampler2D`/`sampler3D`로 주입 가능, 생성은 `RenderingDevice` 컴퓨트
  (`RenderingServer.get_rendering_device()` / `create_local_rendering_device()`, `rendering_server.h:1037-1038`) 또는
  오프라인 베이크 → `.exr`/`Texture3D` 임포트.

### (c) 구현 경계

| 목표 | 코어 수정 | 방법 |
|------|-----------|------|
| **A. sky 셰이더 내 Hosek-Wilkie / 개선 단일산란 / 다중산란 근사항** | **0** | `shader_type sky` 재작성. 계수 LUT은 상수 배열 또는 global uniform 텍스처 |
| **B. Bruneton/Hillaire LUT(투과율·다중산란·sky-view) 사전계산 후 sky에서 조회** | **0** | LUT을 **오프라인 베이크**(에디터 툴 or `create_local_rendering_device()`)해 `Texture2D/3D` 리소스로. sky 셰이더는 조회만 → 초저비용 |
| **C. 씬 지오메트리에 진짜 aerial perspective 적용** | **국소 코어** or 우회 | ① 우회: `FogVolume(WORLD)` + 커스텀 fog 셰이더로 froxel에 대기 in-scattering 주입(코어 0, 단 `volumetric_fog_length` 사거리 제약 §5) ② 우회: `CompositorEffect`(POST_TRANSPARENT)에서 뎁스 읽고 AP 볼륨 합성(코어 0) ③ 정공: `scene_forward_clustered.glsl`의 fog 계산부를 AP LUT 조회로 교체(**국소 코어**, 30~80줄) |
| **D. 매 프레임 LUT 재계산(태양 이동 반영)** | **0** | `CompositorEffect` 또는 별도 컴퓨트로 LUT 갱신 후 global uniform 텍스처에 in-place 렌더(§1.1의 "RID 고정" 패턴) |

**경계의 핵심**: **하늘을 예쁘게 만드는 건 코어 0이고, 그 대기를 "씬 픽셀"에 정확히 입히는 것만 코어를 부른다.**
그리고 그 코어 작업조차 30~80줄 수준이지 서브시스템 신규 개발이 아니다.

### (d) 공수
- A(개선 아날리틱 sky, 다중산란 근사 포함): **3~5일**.
- B(오프라인 LUT 베이커 + sky 조회): **2~3주**.
- C-①/②(FogVolume 또는 CompositorEffect 우회 AP): **1~2주**.
- C-③(코어 fog→AP 교체): **1~2주** + 회귀 검증.

### (e) 현실적 대안
- **하늘 텍스처 시퀀스 + 그라디언트 램프**. `ProceduralSkyMaterial`의 `sky_cover` + TOD 램프 텍스처.
  원신 계열의 실제 방식에 가깝다(§9).
- 씬 대기감은 `Environment`의 **height fog + `fog_sun_scatter` + `fog_aerial_perspective`** 조합으로 충분히 팔린다.
  실측: `fog_sun_scatter`도 `scene_forward_clustered.glsl:1123`에 존재.

### (f) 신호: 🟡 (물리 정확도를 요구하면 🟡, 스타일라이즈드 목표면 🟢)

> **GI 문서와의 경계**: 대기에서 산란된 빛이 씬을 **조명**하는 부분(스카이 앰비언트·라디언스 큐브맵)은
> `Environment.ambient_source = AMBIENT_SOURCE_SKY` / `REFLECTION_SOURCE_SKY`
> (`scene/resources/environment.h:53-62`)를 통해 이미 SDFGI/VoxelGI/라이트맵과 물린다.
> 그 라인의 판단은 [lumen 문서 §2·§5](./godot-lumen-gi-implementation-research.md) 참조. 여기서 재론하지 않는다.

---

## 5. 볼류메트릭 구름

### (a) 무엇이 없나
- 구름 레이어 노드/머티리얼 없음. 구름 그림자 없음. Perlin-Worley 조합 노이즈 프리셋 없음.
- 단, 재료는 있다: `NoiseTexture3D`(`modules/noise/noise_texture_3d.h:40`)가 **seamless 3D 노이즈 텍스처를 에디터에서 굽고**,
  `FastNoiseLite`에 `TYPE_CELLULAR`(=Worley) + Perlin/Simplex + FBM이 전부 있다(`modules/noise/fastnoise_lite.h:47`).
  → Perlin-Worley 볼륨 노이즈는 **에셋 파이프라인으로 즉시 생성 가능**.

### (b) 이미 있는 토대 — 두 개의 서로 다른, 둘 다 쓸 만한 엔진 기능

**토대 ①: sky 셰이더의 half/quarter-res 패스 = 원경 구름 레이마칭용 정식 장치**

`sky.cpp:1402-1464` (`SkyRD::update_res_buffers`) 실측:
```
if (shader_data->uses_quarter_res) {  // render_mode use_quarter_res_pass;
    Size2i quarter_size = sky->screen_size / 4;
    texture = create_texture(RB_SCOPE_SKY("sky_buffers"), RB_QUARTER_TEXTURE("quarter_texture"), ...);
    _render_sky(... SKY_VERSION_QUARTER_RES ...);       // 1/4 해상도로 sky() 실행
}
if (shader_data->uses_half_res) { ... screen_size / 2 ... }   // 1/2 해상도로 sky() 실행
```
그리고 full-res 패스에서 `HALF_RES_COLOR` / `QUARTER_RES_COLOR`로 **읽어서 업샘플**한다
(`shader_types.cpp:497-498`, `sky.cpp:778-779`).
→ **"비싼 레이마치를 1/16 픽셀에서 돌리고 풀해상도에서 합성"**이 엔진이 공식 제공하는 패턴이다.
이건 구름 레이마칭을 위해 설계된 장치다.

- 라디언스 큐브맵 갱신에도 같은 half/quarter 로직이 있다(`sky.cpp:1314-1348`, `roughness_layers` 기준).
- 갱신 빈도 제어: `Sky.process_mode` (`scene/resources/sky.h:52-55`).
  `PROCESS_MODE_AUTOMATIC`은 셰이더가 `TIME`/`POSITION`을 쓰면 **자동으로 REALTIME으로 내려간다**
  (`doc/classes/Sky.xml:49`) — 애니메이션되는 구름 셰이더는 여기 걸린다.
  `REALTIME`은 **radiance_size를 256으로 강제**(`sky.cpp:618-620`, `REAL_TIME_SIZE = 256` @ `sky.h:260`).
  → 움직이는 구름 + 고품질 반사는 트레이드오프. `INCREMENTAL`(기본 `roughness_layers=8` 프레임 분할,
  `rendering_server.cpp:3703`)이 TOD처럼 느린 변화에 맞는 절충.

**한계(확정)**: sky 셰이더에는 **뎁스 접근이 없다**. → sky 구름은 항상 씬 지오메트리 **뒤**에만 존재한다.
산 사이로 들어가는 구름, 구름 속 비행 같은 근경 구름은 이 경로로 **불가**.

**토대 ②: `FogVolume` + `shader_type fog` = 이미 작동하는 참여매질 솔버**

`FogMaterial`의 기본 셰이더는 시시하지만(`fog_material.cpp:141~` — density/albedo/emission/height_falloff/edge_fade/3D텍스처),
**그 아래 볼류메트릭 포그 패스가 하는 일이 진짜다**. `volumetric_fog_process.glsl` 실측:
- **디렉셔널 라이트 × CSM 4캐스케이드 그림자 샘플링** (`:398-439`) — `directional_shadow_atlas`를 froxel마다 조회.
- **Henyey-Greenstein 위상 함수** (`:261-263`, `params.phase_g` = `Environment.volumetric_fog_anisotropy`).
- **옴니/스팟 라이트 + 그림자** (`:492-520`), **VoxelGI/SDFGI 주입** (`:98-148`), 앰비언트/스카이 기여 (`:444-459`).
- **시간적 재투영**(`MAX_TEMPORAL_FRAMES = 16` @ `fog.h:303`, `temporal_reproject_amount` 기본 0.9).

즉 **"태양 그림자를 받는 볼류메트릭 매질"은 커스텀 fog 셰이더 몇 줄로 이미 된다.** 갓레이·안개·연무는 여기.

**프리징(froxel) 해상도 실측**:
```
rendering/environment/volumetric_fog/volume_size    기본 64, 범위 16~512   (rendering_server.cpp:3805)
rendering/environment/volumetric_fog/volume_depth   기본 64, 범위 16~512   (rendering_server.cpp:3806)
rendering/environment/volumetric_fog/use_filter     기본 1 (Yes)            (rendering_server.cpp:3807)
Environment.volumetric_fog_length         기본 64m, 힌트 0.01~1024 (or_greater)  (environment.cpp:1570)
Environment.volumetric_fog_detail_spread  0.5~6.0 CLAMP                          (environment.cpp:987)
```
깊이 분포는 지수적: `d = pow(z/depth, detail_spread); view_z = -fog_frustum_end * d`
(`volumetric_fog_process.glsl:224-227, 318-322`). 뷰 프러스텀 정렬 froxel이다.
`FOG_VOLUME_SHAPE_WORLD`(`rendering_server_enums.h:437`)는 그리드 전체를 덮는다(`fog.cpp:703`) →
**"전역 커스텀 대기/구름 밀도 함수"의 주입점**.

**한계(확정)**: froxel 그리드는 **카메라 프러스텀에 붙어 있고 `volumetric_fog_length`까지만** 존재한다.
기본 64m, 늘려도 현실적으로 수백 m. **구름층(1,500~8,000m)은 이 그리드에 들어오지 않는다.**
그리고 froxel 텍스처는 `set_custom_data(RB_SCOPE_FOG)`로만 보관되며(`fog.h:42`),
`RenderSceneBuffersRD`의 ClassDB 바인딩에 `get_custom_data`가 **없다**(`render_scene_buffers_rd.cpp:52-82`)
→ **CompositorEffect에서 froxel에 직접 쓰는 것은 코어 수정 없이는 불가.**
(반면 sky의 half/quarter 텍스처는 `get_texture("sky_buffers","half_texture")`로 접근 가능 — `sky.cpp:49-51`.)

### (c) 구현 경계

| 목표 | 코어 수정 | 방법 | 신호 |
|------|-----------|------|------|
| **원경 구름층**(Horizon/Decima식 레이마치, 지평선 위 전부) | **0** | `shader_type sky` + `use_quarter_res_pass` + 3D 노이즈 텍스처(오프라인 생성) + HG 위상 | 🟢 |
| **근경 안개·갓레이·계곡 연무** | **0** | `FogVolume` + 커스텀 `shader_type fog`. 태양 그림자·GI 주입이 공짜로 따라옴 | 🟢 |
| **구름이 지오메트리와 상호 관통**(산 사이 구름, 구름 속 비행) | **국소 코어** or CompositorEffect | ① `CompositorEffect`(PRE_TRANSPARENT, `access_resolved_depth`)에서 뎁스 읽고 자체 레이마치 → 코어 0 ② froxel 확장 = 코어 | 🟡 |
| **구름 그림자**(지면에 구름 그림자) | **0 (근사)** / 코어(정확) | 근사: 구름 커버리지 2D 텍스처를 `global uniform sampler2D`로 뿌리고 spatial 셰이더/`DirectionalLight3D` 대신 머티리얼에서 감쇠. 정확: CSM에 구름 감쇠 텍스처 곱하기 = 국소 코어 | 🟢/🟡 |
| **구름이 대기·GI에 기여** | 코어 | radiance 큐브맵엔 자동 반영(sky 경로), SDFGI에는 미반영 | 🟡 |

**적대적 재검증**: "Godot에선 볼류메트릭 구름 불가"는 **거짓**이다. sky 경로(원경)는 코어 0이고,
엔진이 quarter-res 패스라는 전용 장치까지 제공한다. 진짜 제약은 **뎁스 미접근**이라 근경 관통이 안 되는 것이고,
그것도 `CompositorEffect`로 우회 가능하다(대신 라디언스 큐브맵엔 안 들어간다 — 반사에 구름이 빠진다).

### (d) 공수
- sky 레이마치 구름 MVP(노이즈 3D 텍스처 + 커버리지 맵 + HG + quarter-res): **1~2주**.
- 프로덕션급(고도별 타입, 디테일 노이즈, curl, 시간적 재투영, TOD 라이팅): **1~2개월**.
- 구름 그림자 근사(커버리지 텍스처 전역 주입): **2~3일**.
- CompositorEffect 근경 구름: **3~4주**.

### (e) 현실적 대안
- **2D 스크롤 구름 판 + 하늘 도우(dome) 텍스처**. 스타일라이즈드에서 가장 흔하고 가장 싸다(§9).
- `sky_cover` 텍스처(ProceduralSkyMaterial 내장)를 여러 장 블렌딩 + 패럴랙스로 볼륨감 위조.
- 구름 그림자는 **하나의 스크롤링 2D 마스크**로 충분 — 실제 구름 형상과 일치시킬 필요가 없다.

### (f) 신호: 🟡 (원경만이면 🟢)

---

## 6. 햇빛 · 그림자

### (a) 무엇이 없나 — 지시받은 grep 결과

```
$ grep -rni 'shadow_cach|static_shadow|cached_shadow|shadow_static' servers/rendering/ scene/3d/ drivers/
(0 matches)
```
→ **정적 지오메트리 섀도우 캐싱은 존재하지 않는다.** 확정.

추가로 없는 것:
- **거리 필드 소프트 섀도우** (UE의 DFShadow) — `distance_field_shadow|dfshadow` 0매치.
- **컨택트 섀도우 / 스크린스페이스 섀도우** — `contact_shadow` 0매치.
- **HW 레이트레이싱 그림자** — `ShaderRD::setup_raytracing`(`servers/rendering/renderer_rd/shader_rd.cpp:184`)와
  `PIPELINE_TYPE_RAYTRACING` 배관은 **렌더러 레벨까지 들어와 있으나 호출자가 0개**
  (`grep -rn 'setup_raytracing' servers/ modules/ drivers/ --include=*.cpp` → 정의 1건뿐).
  RT 그림자/반사/GI 패스는 아직 없다. → [lumen 문서 §3](./godot-lumen-gi-implementation-research.md)의
  "기초 배관 착지 단계" 판정이 4.8-dev에서도 그대로 유효하며, **`RenderingDevice` 레벨을 넘어 `ShaderRD`까지 올라온 것이 진전**이다.

### (b) 이미 있는 토대 (실측)

**CSM 구조**:
- 분할 수 **1 / 2 / 4** 뿐 (`scene/3d/light_3d.h:166-168` `SHADOW_ORTHOGONAL / PARALLEL_2_SPLITS / PARALLEL_4_SPLITS`,
  `renderer_scene_cull.cpp:2168-2178`).
- 분할 위치는 **수동 오프셋 3개**(`PARAM_SHADOW_SPLIT_1/2/3_OFFSET`) — PSSM 람다 자동 분할 없음
  (`renderer_scene_cull.cpp:2182-2185`).
- 아틀라스: 단일 텍스처, 기본 **4096**(mobile 2048), 16비트 기본 켜짐
  (`rendering_server.cpp:3680-3684`). 4분할 시 `r.size /= 2` → **캐스케이드당 2048²**
  (`light_storage.cpp:2813-2833`). 디렉셔널 라이트가 2개면 그 절반.
- 텍셀 스냅 있음(`renderer_scene_cull.cpp:2318-2322` `Math::snapped(..., unit)`) → 카메라 이동 시 그림자 셰이더링 억제.
- `blend_splits`, `PARAM_SHADOW_FADE_START`, `PARAM_SHADOW_MAX_DISTANCE`, `PARAM_SHADOW_PANCAKE_SIZE`,
  `shadow_caster_mask`(32비트, `light_3d.h:77`) 존재.
- 소프트 섀도우: PCSS류 6단계 품질(`rendering_server.cpp:3682`), 라이트 `SIZE`(각지름) 기반 커널.

**핵심 병목 — 디렉셔널 그림자는 매 프레임 무조건 전부 다시 그린다**:
`renderer_scene_cull.cpp:3486-3500`
```cpp
for (uint32_t i = 0; i < cull.shadow_count; i++) {
    for (uint32_t j = 0; j < cull.shadows[i].cascade_count; j++) {
        ...
        render_shadow_data[max_shadows_used].instances.merge_unordered(
            scene_cull_result.directional_shadows[i].cascade_geometry_instances[j]);
        max_shadows_used++;          // ← dirty 검사가 전혀 없다
    }
}
```
대조군: **포지셔널 라이트에는 재사용 로직이 있다** — `light->is_shadow_dirty()` /
`shadow_atlas_update_light(..., light->last_version)`가 재드로우 여부를 결정한다
(`renderer_scene_cull.cpp:3615-3649`, `light_storage.cpp:2589`).
→ **디렉셔널만 캐싱 개념이 통째로 빠져 있다.** 오픈월드에서 이게 프레임 예산을 먹는 지점이다.
캐스케이드 4개 × (지형 + 폴리지 + 건물) 전체 지오메트리를 매 프레임 depth-only로 다시 그린다.

**이미 있는 완화 장치 — LightmapGI 셰도우마스크 (4.4+, 이 문서의 두 번째 반전)**:
`scene/3d/lightmap_gi.h:48-53`
```
enum ShadowmaskMode { SHADOWMASK_MODE_NONE, REPLACE, OVERLAY, ONLY };
```
셰이더에서(`scene_forward_clustered.glsl:2296-2313, 2526-2530`):
```glsl
shadowmask = textureArray_bicubic(lightmap_textures[MAX_LIGHTMAP_TEXTURES + ofs], uvw, ...).x;
if (shadowmask_mode == LIGHTMAP_SHADOWMASK_MODE_REPLACE) {
    shadow = mix(shadow, shadowmask, smoothstep(directional_lights.data[i].fade_from,
                                                directional_lights.data[i].fade_to, vertex.z));
}
```
→ **CSM 사거리 밖에서 실시간 그림자를 베이크된 셰도우마스크로 크로스페이드**한다.
즉 "먼 거리 정적 그림자 캐싱"의 **오프라인 버전은 이미 엔진에 있다.**
대가: 셰도우마스크는 특정 태양 방향으로 구워지므로 **TOD와 정면 충돌**(§7).

### (c) 구현 경계

| 목표 | 코어 수정 | 방법 | 신호 |
|------|-----------|------|------|
| 캐스케이드별 갱신 주기 분리(먼 캐스케이드를 N프레임마다) | **국소 코어** | `Cull::Shadow::Cascade`에 dirty/frame-counter 추가, `render_shadow_data` 큐잉을 조건부로. 포지셔널의 `last_version` 패턴을 그대로 이식 | 🟡 |
| 정적 지오메트리 그림자 캐싱(정적/동적 분리 2-레이어 아틀라스) | **중간 코어** | 캐스케이드별 static-only 아틀라스 1장 + 동적 오브젝트만 매 프레임 → 샘플 시 min(). UE/Unity의 표준 기법 | 🟡 |
| 원거리 그림자 오프라인화 | **0** | `LightmapGI` 셰도우마스크 + `PARAM_SHADOW_MAX_DISTANCE` 축소. **오늘 당장 가능** | 🟢 |
| 폴리지를 먼 캐스케이드에서 제외 | **0** | `GeometryInstance3D.cast_shadow` + `DirectionalLight3D.shadow_caster_mask`(`light_3d.h:77`) + 라이트를 2개로 나눠 근/원 담당 | 🟢 |
| 거리필드 소프트 섀도우 | **대규모 코어** | 글로벌 SDF 필요 — SDFGI 캐스케이드 SDF 재사용이 유일한 현실 경로([lumen 문서 §5(a)](./godot-lumen-gi-implementation-research.md)) | 🔴 |
| HW-RT 그림자 | 엔진 성숙 대기 | `ShaderRD` RT 배관 위에 그림자 패스 구현. RT API는 여전히 `experimental` | 🔴 |

### (d) 공수
- 캐스케이드 갱신 주기 분리: **1~2주**(코어).
- 정적/동적 그림자 분리 캐싱: **1~2개월**(코어, 회귀 위험 있음).
- 셰도우마스크 + caster_mask 튜닝만으로 얻는 성과: **2~3일**. **ROI가 압도적이므로 여기부터.**

### (e) 현실적 대안
- `shadow_max_distance`를 짧게(예: 80~150m) + 그 밖은 셰도우마스크/버텍스 AO/그라운드 다크닝으로 위조.
  원신도 원거리 캐릭터·오브젝트 그림자를 사실상 포기한다.
- 캐릭터 그림자는 **블롭 섀도우(데칼)** 로 대체하고 CSM은 지형·건물에만.
- 4분할 대신 **2분할 + 짧은 사거리**가 오픈월드에선 더 나은 경우가 많다(아틀라스 2048→2048×2 절약).

### (f) 신호: 🟡

---

## 7. 시간대(TOD) 시스템

### (a) 무엇이 없나
```
$ grep -rniw 'time_of_day|day_night|daynight|timeofday' scene/ servers/ editor/ modules/
(0 matches)
```
→ **TOD 개념이 엔진에 전혀 없다.** 라이트 컬러/강도/각도 자동화, 하늘 전이, 달, 별, 노출 적응 자동화 전무.

### (b) 이미 있는 토대
- sky 셰이더가 **`LIGHT0..LIGHT3`**를 전부 받는다(`shader_types.cpp:470-488`) → 태양+달을 하나의 하늘에서 동시 처리.
- `DirectionalLight3D.sky_mode`(`SKY_MODE_LIGHT_AND_SKY / LIGHT_ONLY / SKY_ONLY`, `light_3d.h:172-174`)
  → "하늘에만 그리는 달", "조명만 하는 보조광"을 분리 가능.
- `Environment`: `ambient_source`/`reflection_source`를 `SKY`로 두면 하늘 변화가 앰비언트·반사에 자동 전파
  (`environment.h:53-62`). 색보정 LUT `adjustment_color_correction`(`environment.h:224`)로 시간대별 그레이딩.
- `Sky.process_mode` (`sky.h:52-55`) — TOD 갱신 비용의 유일한 조절 손잡이. 실측 의미:
  - `QUALITY`: importance sampling 풀 품질, 매 프레임이면 비쌈.
  - `INCREMENTAL`: 같은 품질을 **`roughness_layers`(기본 8) 프레임에 나눠** 계산 (`doc/classes/Sky.xml:55`,
    `rendering_server.cpp:3703`). → **TOD처럼 초 단위로 변하는 하늘에 정확히 맞는 모드.**
  - `REALTIME`: fast filter, **radiance 256 강제**(`sky.cpp:618-620`).
  - `AUTOMATIC`: 셰이더가 `TIME`/`POSITION`을 쓰면 REALTIME, `LIGHT_*`나 커스텀 uniform을 쓰면 INCREMENTAL
    (`doc/classes/Sky.xml:49`). ← **커스텀 TOD 셰이더는 대개 INCREMENTAL로 떨어진다. 좋은 기본값.**
- 전역 상태 전파는 §1.1의 글로벌 유니폼 버스.

### (c) 구현 경계
**코어 수정 0.** 순수 GDScript/GDExtension + 리소스 설계 문제다. 구성 요소:
1. `TimeOfDay` 싱글턴(시각→태양/달 방위·고도, 위도·계절 옵션).
2. 커브/그라디언트 리소스(`Gradient`, `Curve`)로 라이트 컬러·강도, 하늘 색, 안개, 노출을 시각에 매핑.
3. 매 프레임 `RenderingServer.global_shader_parameter_set("tod_*", ...)` 로 전 셰이더에 방송.
4. `Environment` 프로퍼티(안개·앰비언트·색보정)와 `DirectionalLight3D` 트랜스폼 보간.

### (d) 공수
- 기본 TOD(태양/달 궤도, 램프 기반 라이트·하늘·안개 보간, 글로벌 유니폼 방송): **3~5일**.
- 날씨 상태 머신(맑음↔흐림↔비↔폭풍 전이, 프리셋 블렌딩)과 통합: **1~2주**.

### (e) 현실적 대안 — TOD와 베이크의 충돌 해소
이게 TOD의 **진짜 어려운 부분**이고, gaps 문서 §2의 "시간대별 라이트맵 사전 베이크 + 블렌딩"과 직결된다.
실측 기준 현실 옵션:
- **(1) 시간대 N개 라이트맵 + 블렌딩** — `LightmapGIData`는 텍스처 배열을 갖지만
  (`scene/3d/lightmap_gi.h:57-62`) **런타임 크로스페이드 API가 없다**. 두 `LightmapGI` 노드를 두고
  셰이더에서 섞으려면 커스텀 셰이더 + 국소 코어(`INSTANCE_FLAGS_USE_LIGHTMAP` 경로)가 필요.
  → 🟡, 비용 있음.
- **(2) 셰도우마스크는 낮 한 시점에만 사용** — 낮에는 REPLACE로 원거리 그림자를 얻고,
  석양·밤에는 셰도우마스크를 끄고 사거리를 줄인다. **코어 0, 오늘 가능.** 🟢 **1순위.**
- **(3) 정적 GI를 아예 SDFGI로** — TOD와 자연스럽게 맞물리나 응답 지연 ~25프레임
  ([lumen 문서 §2](./godot-lumen-gi-implementation-research.md)). TOD는 느린 변화라 **오히려 궁합이 좋다.**
- **(4) 앰비언트만 시각 램프로 강제** — 가장 싸고 스타일라이즈드에선 가장 잘 먹힌다.

### (f) 신호: 🟢

---

## 8. 바람 시스템

### (a) 무엇이 없나 — grep 결과와 오탐 제거

지시대로 `grep -rniw 'wind' scene servers`를 돌리고 오탐(window/winding/Win32)을 제거했다:
```
scene/3d/physics/area_3d.cpp:179,181,797      → Area3D의 "Wind" 프로퍼티 그룹
(그 외 scene/·servers/ 렌더링 코드에는 0매치)
```
전체 트리로 확장해도 소비자는 하나뿐:
```
scene/3d/physics/area_3d.h:62-64   wind_force_magnitude / wind_attenuation_factor / wind_source_path
modules/godot_physics_3d/godot_area_3d.cpp:166,176   (검증만)
modules/jolt_physics/objects/jolt_soft_body_3d.cpp:237,285-305   ← 유일한 실제 소비자
```
→ **Godot의 "바람"은 `Area3D` → `SoftBody3D` 전용 물리 힘이다. 렌더링과는 완전히 무관하다.**
폴리지 흔들림, 천, 파티클, 물결에 연결된 전역 바람은 **전무**.

또한 §1.2에서 확인했듯 `BaseMaterial3D`에도 버텍스 애니메이션/바람 기능이 없다
(`FEATURE_*` 목록 `material.h:210-222`에 바람 항목 없음).

### (b) 이미 있는 토대
- **§1.1의 글로벌 유니폼 버스가 그대로 정답이다.** `global uniform vec4 wind_dir_speed;`,
  `global uniform sampler2D wind_gust_noise;`를 등록하면
  **spatial(폴리지 버텍스) · particles(비·낙엽·먼지) · sky(구름 이류) · fog(안개 흐름)** 이 전부 같은 값을 읽는다.
  이건 UE의 Wind Directional Source / Unity WindZone에 해당하는 데이터 모델을 **엔진 수정 없이** 재현한다.
- 노이즈: `FastNoiseLite` + `NoiseTexture2D/3D`로 거스트 노이즈 텍스처 생성(`modules/noise/`).
- 국소 바람(폭발·달리기 바람): `GPUParticlesAttractorVectorField3D`(`gpu_particles_collision_3d.h:343`)로 파티클엔 즉시,
  폴리지엔 "영향 구체 배열"을 글로벌 유니폼(`vec4[N]`)으로 넘겨 처리.
- 천: `SoftBody3D` + 기존 `Area3D` 물리 바람 — **이미 있는 유일한 바람 소비자를 그대로 재사용**하고,
  같은 TOD/날씨 상태에서 `Area3D.wind_force_magnitude`를 스크립트로 구동하면 **물리 바람과 셰이더 바람이 동기화**된다.

### (c) 구현 경계
**코어 수정 0.** 구성:
1. `WindManager` 싱글턴: 기본 방향·세기 + 거스트(노이즈) + 날씨 상태 연동 → 글로벌 유니폼 방송.
2. 공용 `wind.gdshaderinc`: `apply_wind(vertex, world_pos, stiffness, phase)` — 폴리지/나무/풀 공통.
3. 파티클 셰이더에서 같은 유니폼으로 가속 적용.
4. `Area3D.wind_*`를 같은 매니저가 구동 → 천/소프트바디 동기화.
5. sky 구름 셰이더의 UV 스크롤 속도도 같은 유니폼.

### (d) 공수
- 전역 바람 버스 + 폴리지 셰이더 인클루드 + 파티클 연동: **1주**.
- 계층적 바람(전역 + 지역 볼륨 + 캐릭터 상호작용 벤딩): **2~3주**.

### (e) 현실적 대안
- 상호작용(캐릭터가 풀을 밀침)이 필요하면 §2의 레인 오클루전과 **같은 패턴을 재사용**한다 —
  탑다운 RT에 캐릭터 위치를 그려 `global uniform sampler2D` 로 뿌리고 폴리지 셰이더가 읽는다.
  **레인 오클루전 인프라를 그대로 재활용**할 수 있다는 게 이 아키텍처의 큰 이득이다.

### (f) 신호: 🟢

---

## 9. 경계 종합 — GDExtension vs 국소 코어 vs 대규모 포크

| 작업 | 순수 GDExtension | 국소 코어 | 대규모 포크 |
|------|------------------|-----------|-------------|
| 글로벌 유니폼 상태 버스(비/젖음/바람/TOD 방송) | ✅ (§1.1, `rendering_server.cpp:3472~3478`) | | |
| 커스텀 셰이더 표준화(빌트인 머티리얼 우회) | ✅ (§1.2, 에디터 변환 플러그인) | | |
| 파티클 강수 + 스플래시 + 빗줄기 | ✅ (`SUB_EMITTER_AT_COLLISION`) | | |
| 레인 오클루전 **경로 B**(SubViewport + `CompositorEffect`) | ✅ (§2 c') | | |
| **레인 오클루전 경로 A**(하이트필드 텍스처 getter) | | ✅ (**~10줄**, `particles_storage.h:600` 노출) | |
| 전역 젖음 + 젖은 BRDF + 누적 마스크 ping-pong | ✅ (§3 c) | | |
| TOD 싱글턴(태양/달 궤도·램프·글로벌 방송) | ✅ (§7 c) | | |
| 전역 바람 버스 + 폴리지/파티클/천 동기화 | ✅ (§8 c) | | |
| sky 셰이더 재작성(Hosek-Wilkie/다중산란 근사) | ✅ (§4 c-A) | | |
| Bruneton/Hillaire LUT 오프라인 베이크 + sky 조회 | ✅ (§4 c-B, `Texture3DRD` @ `texture_rd.h:131`) | | |
| aerial perspective **우회**(`FogVolume(WORLD)` or `CompositorEffect`) | ✅ (§4 c-C ①②) | | |
| **aerial perspective 정공**(씬 fog 계산부 → AP LUT 조회) | | ✅ (**30~80줄**, `scene_forward_clustered.glsl:1102-1120`) | |
| 원경 볼류메트릭 구름(sky + `use_quarter_res_pass`) | ✅ (§5 c) | | |
| 근경 안개·갓레이·계곡 연무(`FogVolume` + `shader_type fog`) | ✅ (§5 c) | | |
| 구름 그림자 **근사**(커버리지 텍스처 전역 주입) | ✅ (§5 c) | | |
| 근경 관통 구름(`CompositorEffect` + `access_resolved_depth`) | ✅ (`compositor.h:58`) | | |
| **구름 그림자 정확**(CSM에 감쇠 곱) | | ✅ | |
| **froxel 접근**(`RenderSceneBuffersRD.get_custom_data` 노출) | | ✅ (바인딩만, `render_scene_buffers_rd.cpp:52-82`에 부재) | |
| 원거리 그림자 오프라인화(셰도우마스크 + `caster_mask`) | ✅ (§6 c, **오늘 가능**) | | |
| **캐스케이드별 갱신 주기 분리** | | ✅ (1~2주, 포지셔널 `last_version` 패턴 이식) | |
| **정적/동적 그림자 분리 캐싱** | | ✅ *중간* (1~2개월, 회귀 위험) | |
| 라이트맵 런타임 크로스페이드(TOD×베이크) | | ✅ (`INSTANCE_FLAGS_USE_LIGHTMAP` 경로) | |
| `BaseMaterial3D`에 젖음/바람 훅 이식 | | ✅ (하지 말 것 — §3 c) | |
| 거리필드 소프트 섀도우 | | | 🔴 글로벌 SDF ([lumen §5(a)](./godot-lumen-gi-implementation-research.md)) |
| HW-RT 그림자 | | | 🔴 엔진 RT 성숙 대기 ([lumen §3](./godot-lumen-gi-implementation-research.md)) |
| 구름의 SDFGI 기여 | | | 🔴 GI 경로 개조 |

**핵심 통찰 1:** 이 표의 **"대규모 포크" 3줄은 전부 이 문서의 항목이 아니라 [lumen 문서](./godot-lumen-gi-implementation-research.md)에 이미 게이팅된 항목**이다. 즉 **날씨·대기 축 고유의 대규모 포크는 0개다.** 물리 문서(§9)가 "절벽이 없다"였다면, 이 축은 **"절벽이 남의 문서에 있다."**

**핵심 통찰 2:** 국소 코어 칸의 항목들이 전부 **바인딩·노출·패턴 이식**이지 알고리즘 신규 개발이 아니다. 최대값(정적 그림자 캐싱 1~2개월)조차 UE/Unity의 표준 기법을 옮기는 작업이고, 최소값(레인 오클루전 getter ~10줄)은 **엔진이 이미 그리고 있는 데이터에 손잡이를 다는 것**이다(§2 c).

**핵심 통찰 3:** GDExtension 칸이 압도적으로 길다. 그리고 그 대부분이 **§1.1 글로벌 유니폼 버스 하나**에 의존한다. → **§1.1을 먼저 세우면 §2·§3·§7·§8이 동시에 열린다.** 이것이 §10 로드맵의 P0가 기능이 아니라 인프라인 이유다.

---

## 10. 우선순위 로드맵

원신급 **스타일라이즈드** 목표 기준. "물리적으로 정확한 대기·구름"은 이 목표에서 **비용이 아니라 실수**다(§4 e, §5 e).

### 🟢 P0 — 즉시 (합계 3~4주, 코어 수정 **0**)

1. **글로벌 유니폼 상태 버스 + 커스텀 셰이더 표준 확립 (3~5일).**
   기능이 아니라 **인프라**다. `weather_*`/`wind_*`/`tod_*`/`wetness_*`를 `RenderingServer.global_shader_parameter_add`로 등록하고, 프로젝트의 모든 3D 머티리얼을 `ShaderMaterial` + 공용 `.gdshaderinc` 컨벤션으로 고정한다.
   실측 지원: **§1.2가 지적한 6종 빌트인 머티리얼 전부에 원클릭 변환 플러그인이 이미 있다** — `StandardMaterial3D`/`ORMMaterial3D`/`ProceduralSkyMaterial`/`PanoramaSkyMaterial`/`PhysicalSkyMaterial`/`FogMaterial`(`editor/scene/3d/material_3d_conversion_plugins.cpp:38,71,104,117,130,143`) + `ParticleProcessMaterial`/`CanvasItemMaterial`(`editor/scene/material_editor_plugin.cpp:525,538`).
   갱신 비용은 사실상 0(dirty region 1024슬롯, §1.1). **이 하나가 P0~P2의 절반을 해금한다.**
2. **TOD 싱글턴 (3~5일).** 태양/달 궤도 + `Gradient`/`Curve` 램프 → 라이트·하늘·안개·노출. `Sky.process_mode = INCREMENTAL`이 TOD에 정확히 맞는 모드(§7 b).
3. **그림자 예산 튜닝 (2~3일). ROI 최고.** `shadow_max_distance` 80~150m + `LightmapGI` 셰도우마스크 REPLACE + `shadow_caster_mask`로 폴리지를 원거리 캐스케이드에서 제외 + 4분할→2분할 검토. **디렉셔널 그림자를 매 프레임 전부 다시 그리는 병목(§6 b)을 코어 수정 없이 가장 크게 깎는 수단.**
4. **전역 바람 버스 + 폴리지 셰이더 인클루드 (1주).** `Area3D.wind_*`를 같은 매니저가 구동해 천/소프트바디까지 동기화(§8 b).
5. **전역 스칼라 젖음 + 젖은 BRDF (1주).** 마스크 없이 스칼라 하나로 체감 품질 70%(§3 e).

### 🟢 P1 — 다음 (합계 3~5주, 코어 **~10줄**)

6. **레인 오클루전 경로 A (3~5일).** `particles_collision_get_heightfield_texture()` getter + ClassDB 바인드 → `Texture2DRD`로 감싸 `global uniform sampler2D`에 **1회** 주입(RID 고정 패턴, §1.1). **추가 GPU 비용 0** — 엔진이 이미 그리는 depth-only 탑다운 패스를 재활용한다.
7. **파티클 강수 + 스플래시 (3~5일).** 강도는 `amount_ratio`가 아니라 **강도별 emitter 프리셋 전환**으로(§2 b).
8. **탑다운 RT 인프라 재사용 (1주).** 같은 패턴으로 캐릭터 폴리지 벤딩·젖음 누적 마스크를 얹는다. **레인 오클루전 하나를 지으면 3개가 따라온다**(§8 e) — 이 축 아키텍처의 최대 이득.
9. **`ProceduralSkyMaterial` 기반 TOD 하늘 (3~5일).** `sky_cover` + 램프 텍스처. **`PhysicalSkyMaterial`은 쓰지 않는다** — Preetham 단일산란 + 비물리 톤 해킹(§4 a)이라 스타일라이즈드에 이득이 없다.

### 🟡 P2 — 조건부 (게임 디자인이 요구할 때만)

10. **sky 레이마치 원경 구름 MVP (1~2주, 코어 0).** `use_quarter_res_pass` + `NoiseTexture3D` Perlin-Worley. 하늘이 게임의 룩에서 중요할 때만.
11. **구름 그림자 근사 (2~3일).** 스크롤링 2D 커버리지 마스크 전역 주입. 실제 구름 형상과 일치시키지 않는다(§5 e).
12. **`FogVolume` 갓레이·계곡 연무 (1~2주, 코어 0).** 태양 그림자·GI 주입이 공짜로 따라오는 유일한 지점(§5 b ②).
13. **캐스케이드 갱신 주기 분리 (국소 코어, 1~2주).** **P0-3 이후에도** 프로파일러가 디렉셔널 그림자를 병목으로 지목할 때만 착수. 순서를 뒤집지 말 것.

### 🟡 P3 — 비싸고 나중 (합계 2~4개월)

14. **정적/동적 그림자 분리 캐싱 (중간 코어, 1~2개월).** 오픈월드 규모가 실제로 커진 뒤.
15. **Bruneton/Hillaire LUT + 진짜 aerial perspective (2~3주 + 코어 1~2주).** 고도 변화(비행·등산)가 게임플레이에 들어올 때만.
16. **프로덕션급 볼류메트릭 구름 (1~2개월).** 고도별 타입·curl·시간적 재투영.

### 🔴 P4 — **만들지 말 것** (현 목표 기준)

| 항목 | 안 만들어도 되는 근거 |
|------|----------------------|
| **물리 정확 다중산란 대기** | 스타일라이즈드 룩에서 지평선 밝기의 물리 정확도는 **아무도 못 알아본다**. `ProceduralSkyMaterial` 그라디언트 + TOD 램프가 오히려 아트 디렉션 가능(§4 a·e) |
| **근경 관통 구름**(`CompositorEffect`, 3~4주) | 구름 속 비행·산 사이 구름이 게임플레이에 없으면 0가치. 게다가 라디언스 큐브맵에 안 들어가 **반사에서 구름이 사라진다**(§5 c) |
| **froxel 확장으로 구름층 커버** | froxel은 프러스텀 정렬 + `volumetric_fog_length`(기본 64m) 제약이라 **구름층 1,500~8,000m는 구조적으로 안 들어온다**(§5 b). 설계 오용 |
| **거리필드 소프트 섀도우 / HW-RT 그림자** | 글로벌 SDF·RT 성숙에 게이팅([lumen 문서](./godot-lumen-gi-implementation-research.md)). 이 문서의 예산으로 접근 금지 |
| **`BaseMaterial3D` 젖음/바람 훅** | 업스트림 diff만 키우고 이득 0. 어차피 커스텀 셰이더 라이브러리를 갖게 된다(§3 c) |
| **시간대 N개 라이트맵 + 런타임 크로스페이드** | 런타임 API 부재로 국소 코어 필요한데, **SDFGI(응답 ~25프레임)가 느린 TOD와 오히려 궁합이 좋고**, 앰비언트 램프 강제가 스타일라이즈드에선 더 잘 먹힌다(§7 e-3·e-4) |
| **파티클 `amount` 동적 조절** | `set_amount`가 버퍼를 통째로 재할당해 히칭(`particles_storage.cpp:341`). emitter 프리셋 전환이 정답(§2 b) |

### 요약

**P0(3~4주, 코어 0) + P1(3~5주, 코어 ~10줄)만으로 원신급 날씨·대기·TOD 체감 품질의 대부분이 나온다.**
P2 이후는 전부 조건부이며, P4는 **적극적으로 만들지 않는 것이 이득**이다.

---

## 11. 자기 반증 — "불가"라고 쓸 뻔한 주장들의 재검증

기존 문서 7개가 전부 초판 "❌ 불가" → "⚠️ 단계별 비용"으로 정정된 이력이 있으므로, 이 문서에서 "없다/못 한다"로 적으려던 주장을 한 번 더 공격했다.

| 초안 주장 | 반증 시도 | 결론 |
|-----------|----------|------|
| "레인 오클루전(처마 밑 판정)은 Godot에서 불가 — 탑다운 뎁스 렌더 인프라가 없다" | **틀림.** `GPUParticlesCollisionHeightField3D`가 **직교 투영 + 위에서 아래 + `PASS_MODE_SHADOW`(depth-only) + D32_SFLOAT + `SAMPLING_BIT` + follow-camera + 레이어 마스크**를 이미 전부 수행한다(`renderer_scene_render_rd.cpp:1516-1533`, `render_forward_clustered.cpp:2910-2940`, `particles_storage.cpp:1888-1897`). 코어 소스 주석마저 *"Not a shadow pass, but should be treated like one."* | **"인프라 부재" → "getter 미바인딩(~10줄)"** |
| "레인 오클루전은 코어 수정 없이는 절대 불가" | **틀림.** SubViewport + 직교 `Camera3D` + `CompositorEffect`로 코어 0 달성 가능. `get_depth_texture`가 스크립트 바인딩됨(`render_scene_buffers_rd.cpp:66`), `access_resolved_depth` 플래그도 노출됨(`scene/resources/compositor.h:58`) | **"불가" → "더 비쌀 뿐"** |
| "Godot엔 볼류메트릭 구름이 불가하다" | **틀림.** sky 셰이더의 `use_quarter_res_pass`/`use_half_res_pass`는 **"비싼 레이마치를 1/16 픽셀에서 돌리고 풀해상도에서 업샘플"을 위해 설계된 전용 장치**다(`sky.cpp:1402-1464`, `HALF_RES_COLOR`/`QUARTER_RES_COLOR` @ `shader_types.cpp:497-498`). 원경 구름은 **코어 0** | **"불가" → "엔진이 장치를 제공"** |
| "sky에 뎁스가 없으니 구름은 아예 쓸 수 없다" | **과장.** 뎁스 미접근은 사실이나 결과는 "**근경 관통만** 불가"다. 그리고 그마저 `CompositorEffect`(PRE_TRANSPARENT + `access_resolved_depth`)로 우회 가능 — 대가는 라디언스 큐브맵 미반영 | **범위 축소 + 우회로 명시** |
| "태양 그림자를 받는 볼류메트릭 매질을 만들어야 한다" | **불필요.** `volumetric_fog_process.glsl`이 이미 **CSM 4캐스케이드 그림자 샘플링(`:398-439`) + HG 위상(`:261-263`) + 옴니/스팟 그림자 + VoxelGI/SDFGI 주입 + 16프레임 시간적 재투영**을 수행한다. 커스텀 `shader_type fog` 몇 줄로 올라탄다 | **"신규 개발" → "기존 솔버 재사용"** |
| "젖음/바람은 전 머티리얼에 상태를 뿌릴 방법이 없어서 어렵다" | **틀림.** `global_shader_uniforms` 스토리지 버퍼가 **spatial/mobile/particles/sky/fog/canvas_item 6종 셰이더 전부**에 바인딩돼 있고 API가 ClassDB에 노출됨(`rendering_server.cpp:3472-3478`). `sampler2D/2DArray/3D/Cube`까지 전역 가능 | **"경로 부재" → "정식 경로 실재"** |
| "빌트인 머티리얼이 글로벌 유니폼을 안 읽으니 이 버스는 반쪽이다" | **부분적으로 틀림.** 읽지 않는 건 사실이나(`material.cpp:656` `global` 0매치), **엔진이 8종 머티리얼에 원클릭 `ShaderMaterial` 변환 플러그인을 이미 제공**하고(`material_3d_conversion_plugins.cpp:38~143`, `material_editor_plugin.cpp:525,538`) 생성 셰이더에 *"useful when converting to ShaderMaterial"* 주석까지 박아 뒀다(`material.cpp:739-741`) | **"구멍" → "에디터 워크플로로 메워짐"** |
| "정적 그림자 캐싱이 없으니 오픈월드 그림자 예산은 답이 없다" | **틀림.** ① `LightmapGI` **셰도우마스크(4.4+)** 가 CSM 사거리 밖 실시간 그림자를 베이크 그림자로 크로스페이드한다(`lightmap_gi.h:48-53`, `scene_forward_clustered.glsl:2296-2313`) → "원거리 정적 그림자 캐싱의 오프라인 버전"이 **이미 엔진에 있다**. ② 캐싱 로직 자체도 **포지셔널 라이트엔 이미 존재**(`light->is_shadow_dirty()`, `renderer_scene_cull.cpp:3615-3649`) → 디렉셔널에 **이식**하는 문제이지 설계하는 문제가 아니다 | **"전무" → "오프라인 버전 실재 + 이식 대상 패턴 실재"** |
| "TOD가 없으니 만들려면 코어를 건드려야 한다" | **틀림.** grep 0매치는 맞으나 필요한 손잡이가 전부 스크립트에 있다 — sky의 `LIGHT0..LIGHT3`, `DirectionalLight3D.sky_mode`, `ambient_source=SKY`, `adjustment_color_correction`, `Sky.process_mode=INCREMENTAL`. **코어 0** | **"부재" → "리소스 설계 문제"** |
| "Godot에 바람 시스템이 있다(Area3D wind)" | **틀림(반대 방향).** `Area3D`의 wind는 **`SoftBody3D` 전용 물리 힘**이고 유일한 소비자가 `jolt_soft_body_3d.cpp:237,285-305`다. 렌더링과 완전 무관 | **오해 제거 — 단, 이 물리 바람을 같은 매니저로 구동하면 셰이더 바람과 동기화되는 이득이 있다** |
| "aerial perspective가 이미 있다(`fog_aerial_perspective`)" | **틀림(반대 방향).** 구현은 "안개 색을 시선 방향 라디언스 큐브맵 색으로 치환"하는 근사다(`scene_forward_clustered.glsl:1102-1120`). 거리별 in-scattering/transmittance 적분이 아니다. **이름만 같은 다른 물건** | **유지(부재 확정) — 단 스타일라이즈드엔 이 근사로 충분** |
| "`PhysicalSkyMaterial`이 물리 기반이니 대기는 해결됐다" | **틀림(반대 방향).** three.js `Sky.js` 직역 포팅(`sky_material.cpp:778,799`)의 **Preetham 단일산란**이고, 다중산란·LUT·행성 곡률·달빛(LIGHT1)이 전부 없으며 `pow(color, 1/(1.2+1.2*sun_fade))` 같은 비물리 톤 해킹이 박혀 있다 | **유지(한계 확정)** |
| "`amount_ratio`로 비 강도를 조절하면 성능이 절약된다" | **틀림(반대 방향).** 방출률만 줄이고 dispatch는 `amount` 전체로 돈다(`particles_storage.cpp:1230`). `set_amount`는 버퍼 재할당(`:341`) → 히칭 | **유지 — 실무 결론은 emitter 프리셋 전환** |
| "하이트필드 콜라이더를 레인 오클루전용과 파티클 충돌용으로 각각 둔다" | **틀림(반대 방향).** 두 번째부터 무시된다 — `if (collision_heightmap_texture != RID()) { continue; }`(`particles_storage.cpp:1057-1058`). 시스템당 1개 | **유지 — 하나를 공유하고 셰이더에서 용도 분기** |
| "`CompositorEffect`로 froxel에 대기를 주입하면 된다" | **틀림(반대 방향).** froxel 텍스처는 `RB_SCOPE_FOG` custom data로만 보관되는데 `get_custom_data`가 ClassDB 바인딩에 없다(`render_scene_buffers_rd.cpp:52-82` 실측 — `get_texture`/`get_texture_slice`/`get_depth_texture` 등은 있으나 custom data 접근자 없음) | **유지 — 코어 바인딩 1줄이 필요한 유일한 지점** |

**메타 결론:** 이 축에서 **"진짜 불가"로 살아남은 항목은 하나도 없다.** 4건(§4 aerial perspective, §4 PhysicalSky 한계, §2 파티클 dispatch/하이트필드 제약, §5 froxel 접근)이 "부재/한계 확정"으로 유지됐지만, 그중 셋은 **스타일라이즈드 목표에서 애초에 필요 없는 것**이고 하나(froxel 접근)는 **바인딩 1줄**이다.

---

## 12. gaps 문서 반영

기존 [godot-openworld-engine-gaps.md](./godot-openworld-engine-gaps.md)엔 날씨·대기 항목이 없다. 아래 행을 요약 표에 추가한다:

| 격차 | 대체 난이도 | 해결 경로 |
|------|-------------|-----------|
| 기상 / 대기 / 햇빛 (비·젖음·산란·구름·TOD·바람) | ⚠️ **격차 아님에 가까움** — man-year급 코어 벽 0개 | 글로벌 셰이더 유니폼 버스가 spatial/particles/sky/fog 전부에 이미 도달 → 젖음·바람·TOD는 **코어 0**. 레인 오클루전은 엔진이 이미 그리는 탑다운 depth 패스의 **getter ~10줄**. 원경 구름은 sky `quarter_res_pass`로 코어 0. 남는 국소 코어는 그림자 캐싱뿐 ([상세](./godot-weather-atmosphere-research.md)) |

**새 섹션(§8. 기상 / 대기 / 햇빛)에 들어갈 요약 문구:**

> ## 8. 기상 / 대기 / 햇빛 ⚠️ (정정됨: "격차"가 아니라 "미조립")
>
> **무엇이 없나**
> - 강수·젖음·시간대·전역 바람 시스템이 엔진에 **개념 자체로** 없다(`rain|wetness|puddle`, `time_of_day`, 렌더링 측 `wind` 전부 grep 0매치). 대기는 Preetham 단일산란(`PhysicalSkyMaterial`)에 머물고, 볼류메트릭 구름과 **정적 지오메트리 섀도우 캐싱**도 없다.
>
> **핵심 반전 — 부품은 전부 있고 조립만 안 돼 있다**
> - **글로벌 셰이더 유니폼 버스**(`rendering_server.cpp:3472~3478`)가 spatial·mobile·particles·**sky**·**fog**·canvas_item 6종 셰이더 전부에 바인딩돼 있고 `sampler2D/3D`까지 전역 가능 → "하나의 날씨 상태를 전 머티리얼에 방송"이 **코어 수정 0**. 갱신 비용도 dirty-region 단위라 사실상 0.
> - **레인 오클루전 맵을 엔진이 이미 그리고 있다** — `GPUParticlesCollisionHeightField3D`가 직교 투영 + 탑다운 + depth-only + D32_SFLOAT + `SAMPLING_BIT` + follow-camera. 빠진 건 텍스처 핸들 **getter 하나(~10줄)**.
> - **볼류메트릭 구름용 전용 장치가 이미 있다** — sky 셰이더의 `use_quarter_res_pass`/`use_half_res_pass` + `QUARTER_RES_COLOR` 업샘플(원경 구름 = 코어 0). 근경 참여매질은 `FogVolume` + `shader_type fog`가 **CSM 그림자·HG 위상·GI 주입·시간적 재투영을 이미 수행**한다.
> - **원거리 정적 그림자의 오프라인 버전도 이미 있다** — `LightmapGI` 셰도우마스크(4.4+)가 CSM 사거리 밖을 베이크 그림자로 크로스페이드. 실시간 캐싱 로직도 **포지셔널 라이트엔 존재**해 디렉셔널로 이식하면 된다.
>
> **경로 (난이도순)**
> - 🟢 P0 (3~4주, **코어 0**): 글로벌 유니폼 상태 버스 + 커스텀 셰이더 표준(빌트인 머티리얼 8종에 원클릭 변환 플러그인 실재) → TOD 싱글턴 → **그림자 예산 튜닝(셰도우마스크 + `caster_mask` + 짧은 사거리, ROI 최고)** → 전역 바람 → 전역 젖음.
> - 🟢 P1 (3~5주, **코어 ~10줄**): 레인 오클루전 getter → 파티클 강수 → 같은 탑다운 RT 인프라로 폴리지 벤딩·젖음 마스크 재사용.
> - 🟡 P2 (조건부, 코어 0~국소): sky 레이마치 원경 구름 · 구름 그림자 근사 · `FogVolume` 갓레이 · 캐스케이드 갱신 주기 분리.
> - 🔴 P4 (**만들지 말 것**): 물리 정확 다중산란 대기, 근경 관통 구름, froxel 확장, 거리필드/HW-RT 그림자, `BaseMaterial3D` 개조, TOD 라이트맵 크로스페이드.
>
> **현실적 대안 (여전히 유효)**
> - **원신급 목표엔 P0+P1로 충분하고, 물리기반 대기·볼류메트릭 구름은 애초에 만들지 않는 것이 이득이다.** `ProceduralSkyMaterial` 그라디언트 + TOD 램프 + 스크롤 구름 판이 스타일라이즈드의 정답이며, 원신도 원거리 그림자를 사실상 포기하고 실내/실외를 레벨 볼륨으로 판정한다.
> - **§6 그림자·§4 대기 산란은 [lumen 문서](./godot-lumen-gi-implementation-research.md)와 겹친다** — 거리필드 소프트 섀도우·HW-RT 그림자·스카이 라디언스의 GI 기여는 그쪽이 정본이고, 이 문서는 CSM 예산과 sky/fog 저작만 담당한다.

---

## 13. 남은 미확인 질문 (Open Questions)

1. **레인 오클루전 getter의 업스트림 수용 가능성** — `particles_collision_get_heightfield_framebuffer`(`particles_storage.h:600`)는 프레임버퍼를 반환한다. 텍스처 RID getter를 `RenderingServer` 가상 함수로 올릴 때, 파티클 전용 스토리지를 범용 API로 승격하는 설계에 업스트림이 동의할지 미확인. 로컬 포크로만 갈지 PR을 낼지 결정 필요.
2. **하이트필드 갱신 비용 실측** — `update_mode = ALWAYS` + 해상도 2048~4096에서 오픈월드 지오메트리 전체를 매 프레임 depth-only로 그리는 비용을 측정하지 않았다. `WHEN_MOVED` + follow-camera의 실제 갱신 빈도도 미측정. **이 값에 따라 P1의 "추가 GPU 비용 0" 주장이 조정될 수 있다.**
3. **디렉셔널 캐스케이드 갱신 주기 분리 시 아티팩트 범위** — 텍셀 스냅(`renderer_scene_cull.cpp:2318-2322`)이 있으나, 먼 캐스케이드를 N프레임 유지할 때 카메라 이동에 따른 그림자 팝핑의 실제 체감 정도가 미검증. N의 실용 상한 미확인.
4. **`Sky.process_mode = INCREMENTAL`의 TOD 실측 지연** — `roughness_layers`(기본 8) 프레임 분할이 초 단위 TOD 변화에서 라디언스 큐브맵 지연으로 보이는지, 특히 일출/일몰의 급격한 색 변화 구간에서 미측정.
5. **셰도우마스크 REPLACE의 TOD 크로스페이드 전략** — §7 e-2("낮에만 셰도우마스크")의 전이 지점(석양 시작 시각)에서 원거리 그림자가 사라지는 것이 얼마나 눈에 띄는지, `fade_from`/`fade_to`(`scene_forward_clustered.glsl:2296-2313`) 튜닝만으로 감출 수 있는지 미검증.
6. **`FogVolume(WORLD)` 커스텀 fog 셰이더의 froxel 예산** — `volume_size`/`volume_depth` 기본 64³에서 커스텀 밀도 함수(노이즈 샘플링 포함)를 돌릴 때의 실측 비용과, `detail_spread`를 어디까지 올려야 근경 품질이 유지되는지 미측정.
7. **`CompositorEffect` 근경 구름의 라디언스 큐브맵 부재 영향** — 반사에서 구름이 사라지는 것이 스타일라이즈드 룩에서 실제로 문제가 되는지. 물·금속 표면 비중에 따라 P4 판정이 뒤집힐 여지.
8. **`Texture2DRD`로 감싼 글로벌 유니폼 텍스처의 uniform-set 재생성 회피 검증** — §1.1의 "RID 한 번만 설정하고 그 안에 매 프레임 렌더" 패턴이 `Texture2DRD.set_texture_rd_rid`(`texture_rd.h:59`) 경유에서도 재생성을 실제로 피하는지 런타임 미확인. **틀리면 젖음 마스크·레인 오클루전 두 항목의 비용 모델이 바뀐다.**
9. **`shadow_caster_mask`로 라이트를 근/원 2개로 분리했을 때의 아틀라스 비용** — 디렉셔널 2개면 아틀라스가 반으로 쪼개진다(`light_storage.cpp:2813-2833`). 4096 아틀라스에서 근거리 라이트에 실질 몇 텍셀이 남는지 미계산.

---

*리서치 방법: Godot 4.8-dev(`eda2a482e9`) 로컬 소스트리 실측 우선 — 모든 부정 주장에 검색어와 매치 수를 병기(`rain|wetness|puddle`, `time_of_day|day_night`, `wind`, `shadow_cach|static_shadow`, `distance_field_shadow`, `contact_shadow` 등이 전부 0매치). C++ 문자열로 박힌 생성 셰이더(`sky_material.cpp`, `material.cpp`, `fog_material.cpp`)와 GLSL(`sky.glsl`, `volumetric_fog_process.glsl`, `scene_forward_clustered.glsl`, `particles.glsl`)을 직접 판독해 알고리즘 정체를 확정. **적대적 재검증에서 "불가"로 살아남은 주장이 0건**이며, 가장 큰 반전 셋은 (1) 레인 오클루전 depth 패스가 `GPUParticlesCollisionHeightField3D` 내부에 이미 완제품으로 존재, (2) sky의 `use_quarter_res_pass`가 구름 레이마칭 전용 장치, (3) `LightmapGI` 셰도우마스크가 원거리 정적 그림자 캐싱의 오프라인 구현으로 4.4부터 존재 — 였다. 미해결: 항목 2·8(하이트필드 갱신 비용, `Texture2DRD` uniform-set 재생성 회피)은 런타임 계측이 필요하며, 두 값에 따라 P1의 비용 모델이 조정될 수 있다.*
