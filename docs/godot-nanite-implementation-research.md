# Godot 4.x에 Nanite급 가상화 지오메트리 구현 — 딥리서치 & 기술 실행 로드맵

> [godot-openworld-engine-gaps.md](./godot-openworld-engine-gaps.md) 의 "1. Nanite ❌ (사실상 비현실적)" 항목을 실제 구현 관점에서 딥다이브한 문서.
> **결론 요약:** 기술적으로 **불가능하지 않다**. 단 **GDExtension만으로는 불가능**하고 상당한 **코어 C++ 개조**가 필요하다. mesh shader는 필수가 아니며 compute 기반 SW 래스터 경로로 우회 가능하다. 가장 어려운 미해결 문제는 **DAG(클러스터 계층) 품질**이다.
>
> 근거: 아래 각 항목은 딥리서치 하니스(101 에이전트, 19개 소스 페치, 25개 주장 적대적 검증 → 23 confirmed)에서 나온 검증된 주장들이다. 인용 출처는 각 절 하단에 명시.

---

## 0. 한 장 요약

| 구성요소 | Nanite 방식 | Godot 현황 | 구현 위치 |
|----------|-------------|-----------|-----------|
| Meshlet 생성 | ~128삼각형 클러스터 | **meshoptimizer 이미 번들** (미호출) | 오프라인 (import) |
| 클러스터 DAG (LOD) | group→merge→simplify½→re-split 반복 | 없음 | 오프라인 — **최난관** |
| 뷰 종속 LOD 컷 | QEM 오차의 스크린 픽셀 투영 | 없음 | 런타임 compute |
| GPU 컬링 | 2-pass HZB occlusion | Forward+ compute 인프라 존재 | 런타임 compute |
| 래스터라이저 | 64b atomic SW raster + HW 폴백 | 없음 (64b atomic 노출 여부 미확인) | 런타임 compute / **코어** |
| Visibility buffer | 30b depth + 34b payload | 없음 | 런타임 |
| 지오메트리 스트리밍 | 페이지 단위 virtual-texture식 | 밉 스트리밍 수준만 | 코어 IO/RD |
| mesh shading | (선택) | **RD에 미노출** (PR #88934 미머지) | **코어 C++** |

**목표 하드웨어:** Forward+ / 데스크톱급 (Vulkan compute). Forward Mobile·Compatibility(GLES)·headless는 타깃 불가.

---

## 1. Nanite 내부 동작 원리 (검증됨)

### 1.1 클러스터 DAG 빌드 (오프라인)
메시를 **정확히 128삼각형 클러스터(meshlet)** 단위로 나눈다. 이후:
1. 여러 클러스터를 **그룹핑** (~~METIS 그래프 분할로~~ → **`meshopt_partitionClusters`(번들 meshopt 1.2)로 대체 — §10.2**. METIS 벤더링 불요) edge-cut 비용 최소화
2. 그룹을 **병합 후 삼각형 수 절반으로 단순화** (QEM)
3. 다시 128삼각형 클러스터로 **재분할**
4. 루트 클러스터 1개가 남을 때까지 **반복** → **DAG** 완성

> `A cluster is 128 triangles ... group ... merge ... split ... Repeat until 1 cluster at root ... We use the popular METIS library` — SIGGRAPH 2021 Karis 원문. Bevy 0.14/0.16, jglrxavpok, SimNanite 재구현이 동일 루프 확인. **[3-0 confirmed]**

### 1.2 뷰 종속 LOD 선택 (런타임)
각 클러스터의 **QEM 오차를 스크린 픽셀로 투영**해 DAG를 런타임에 "컷"한다.
- 그룹 오차 `< 1px` **AND** 부모 오차 `>= 1px` → 그 그룹을 그림 (locally-varying cut)
- 오차는 DAG 상에서 **단조(monotonic)** 강제
- **크랙 방지:** 그룹 경계 정점 잠금 + 그룹 내 동일 LOD 결정 강제

> `view dependent ... screen space projected error ... Quadric Error Metric ... Force error to be monotonic ... group clusters and force same LOD decision` — Karis 원문 + Bevy 0.14 verbatim. **[3-0 confirmed]**

### 1.3 2-pass GPU occlusion culling (HZB)
- **Pass 1:** 지난 프레임에 보였던 클러스터를 그려 HZB(Hierarchical Z-Buffer) 빌드
- **Pass 2:** 나머지 클러스터를 그 HZB로 테스트 → "지금은 보이지만 지난 프레임엔 안 보였던" 지오메트리 발견

> `two pass occlusion culling ... Build HZB ... Test the HZB to determine what is visible now but wasn't in the last frame` — Karis 원문, elopezr "A Macro View of Nanite" RDG 리버스엔지니어링 확인. **[3-0 confirmed]**

### 1.4 소프트웨어 래스터라이저 + Visibility buffer
작은 삼각형(마이크로폴리곤)은 하드웨어 래스터가 비효율적 → **커스텀 compute SW 래스터**:
- 클러스터당 하나의 threadgroup, **64비트 atomic `InterlockedMax`** 로 visibility buffer에 Z-버퍼링
- 패킹: **상위 30비트 depth + 하위 34비트 payload** (27b cluster ID + 7b triangle index)
- 클러스터별로 스크린 오차 기준 SW/HW 래스터 선택
- 마이크로폴리곤에서 **SW 경로가 HW 대비 평균 ~3배 빠름**

> `64b atomics ... InterlockedMax to the visibility buffer ... pack in 34 bits or less ... 3x faster than hardware on average` — Karis 원문. **[2-1 confirmed]** (128-thread 세부는 plausible)

**타당성 근거:** compute SW 래스터가 **20억 포인트를 60fps 브루트포스** 렌더 가능 (Schütz et al., "Software Rasterization of 2 Billion Points in Real Time", HPG 2022). **[3-0 confirmed]**

### 1.5 런타임 파이프라인 전체
`build → stream → decompress → cull → rasterize → shade` — 각 단계가 별개 핵심 스테이지.
> SIGGRAPH 공동저자 Wihlidal: `built, streamed, decompressed, culled, rasterized, and finally shaded`. **[3-0 confirmed]**

**출처:** [Karis SIGGRAPH 2021](https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf) · [Wihlidal Nanite Deep Dive](https://www.wihlidal.com/projects/nanite-deepdive/) · [thecandidstartup: Nanite pipeline](https://www.thecandidstartup.org/2023/04/03/nanite-graphics-pipeline.html) · [Schütz et al. 2022 (arXiv 2204.01287)](https://arxiv.org/pdf/2204.01287)

---

## 2. Godot 4.x 렌더링 아키텍처와 제약

### 2.1 RenderingDevice = 유일한 진입점
Godot 4는 `RenderingServer` 아래 **`RenderingDevice`** 라는 자체 저수준 추상화를 두고 Vulkan/D3D12/Metal/WebGPU를 감싼다. Compute는 first-class·문서화된 워크플로:
- `compute_pipeline_create` / `compute_list_begin` / `compute_list_dispatch`
- ⇒ **Nanite식 파이프라인은 raw Vulkan이 아니라 RenderingDevice 위에 작성해야 한다.**

> 공식 docs: `Godot uses its own abstraction called RenderingDevice ... for working with modern low-level graphics APIs such as Vulkan`. 소스트리에 `rendering_device.h` 싱글턴 + `drivers/vulkan·d3d12·metal`. 4.4에 "Using compute shaders" 전용 튜토리얼. **[3-0 confirmed]**

### 2.2 Forward+ 는 이미 GPU-driven 전제조건 충족
Forward+ 렌더러는 RD/Vulkan 위에서 compute를 광범위하게 사용 (클러스터드 라이팅, SSAO, SSIL, GI). `cluster_builder_rd.cpp` 가 `compute_pipeline`/`compute_list_dispatch` 사용 확인. ⇒ GPU 컬링/래스터 compute 패스 인프라는 **존재한다.**

**제약:**
- **Forward Mobile:** compute 지원이 매우 제한적이거나 없음 (순수 raster). 엔진 설계상 제약이지 하드웨어 제약은 아님 — 현대 모바일 Vulkan GPU는 compute 지원.
- **Compatibility(OpenGL)·headless:** RenderingDevice 자체가 사용 불가 ⇒ GLES/web 경로 타깃 불가.

> **[3-0 / 2-1 confirmed]**

### 2.3 mesh shading = 미노출, 코어 개조 필요 (단 필수 아님)
mesh+task 셰이더는 현재 Godot RenderingDevice에 **노출되어 있지 않다.** open proposal([#6822](https://github.com/godotengine/godot-proposals/issues/6822), [#11272](https://github.com/godotengine/godot-proposals/issues/11272)) + 미머지 PR #88934(2024-02, Vulkan+DX12, Metal dummy)로만 존재.
- 노출하려면 **RenderingDevice·렌더 그래프·드라이버의 코어 C++ 수정** 필요 → GDExtension 도달 불가.
- proposal은 "노출"만 다루고 빌트인 렌더러 사용은 미포함 ⇒ Nanite 구현은 이 proposal을 **넘어서는** 코어 작업.
- **중요:** Nanite는 mesh shader가 **필수가 아니다.** compute SW 래스터 경로로 구현 가능 (mesh shader가 Nanite의 유일한 경로라는 주장은 리서치에서 **0-3 / 1-2로 기각됨**).

> #11272: `this needs to be exposed at the lowest level of our rendering code ... the rendering device, the graph and the drivers`. **[3-0 confirmed]**

### 2.4 meshoptimizer는 이미 번들되어 있다
`thirdparty/meshoptimizer/` (v1.2)에 `meshopt_buildMeshlets` 등 완전한 meshlet 생성 API + 그래프 분할(`partition.cpp`) 포함. **단 Godot 코어는 아직 이 API를 호출하지 않는다.** ⇒ 전처리 기반으로 재사용 가능.

> `MESHOPTIMIZER_VERSION 1020`, `meshopt_buildMeshlets`, `clusterizer.cpp`·`partition.cpp` 확인. Bevy도 동일 사용. **[3-0 confirmed]**

### 2.5 Godot 공식 방향은 Nanite식이 아니다 (중요한 전략적 사실)
리드 아키텍트 reduz의 [GPU-driven renderer 제안](https://gist.github.com/reduz/c5769d0e705d8ab7ac187d63be0099b5)은:
- compute로 셰이더 타입별 오브젝트 카운트→오프셋→indirect draw list 생성
- **occlusion·그림자를 Nanite식 visibility buffer가 아니라 하드웨어 레이트레이싱(+base raster)에 의존** — G-Buffer deferred

⇒ **Godot 코어가 Nanite식 virtualized geometry를 공식 채택할지는 불확실.** 커뮤니티/서드파티(포크·엔진 개조) 경로가 더 현실적일 수 있다. **[3-0 confirmed]**

---

## 3. 선행 연구 — 가져올 수 있는 참조 설계

### 3.1 Bevy virtual geometry = 가장 실용적인 오픈소스 참조
| 버전 | 클러스터링 | DAG | 래스터 |
|------|-----------|-----|--------|
| **0.14** | meshopt-rs 분할 | METIS로 ~4 meshlet 그룹핑 → QEM 단순화·재분할 (리프=원본, 루트=근사) | HW only: `R32Uint` visibility buffer에 packed cluster+tri ID, 하드코드 vertex shader로 `draw_indirect` |
| **0.16** | **meshoptimizer 내장 클러스터링 → METIS 그래프 분할로 전환** (공유/잠금 정점 최소화 → 더 나은 DAG) | 개선 | 여전히 SW raster 미완 |

**SW 래스터 미구현 이유:** wgpu가 **64비트 텍스처 atomic 미지원**. ⇒ Godot에서도 이게 결정적 갈림길.

> jms55(구현자) 1차 서술: `Less shared vertices ... better DAG quality`, `wgpu lacks 64bit texture atomics ... looking forward to implementing software rendering in a FUTURE release`. **[3-0 confirmed]**

### 3.2 DAG 품질 = 최난관 (검증된 컨센서스)
> `DAG quality is the most important part of virtual geometry (and the hardest to get right) ... I'm still not done working on DAG quality — I haven't considered spatial positions of triangles/meshlets for grouping yet` — jms55. **[3-0 confirmed]**

### 3.3 기타 참조
- [nanite-webgpu (scthe)](https://scthe.github.io/nanite-webgpu/) — 브라우저 WebGPU Nanite 구현, 파이프라인 학습용
- [jglrxavpok: Recreating Nanite LOD generation](https://blog.jglrxavpok.eu/2024/01/19/recreating-nanite-lod-generation.html) — DAG 빌드 실전
- [Godot proposal #8560](https://github.com/godotengine/godot-proposals/issues/8560) — "Nanite-style LOD" 커뮤니티 제안
- [meshoptimizer discussion #750](https://github.com/zeux/meshoptimizer/discussions/750) — 저자(zeux) 클러스터링 가이드

**출처:** [Bevy 0.14](https://jms55.github.io/posts/2024-06-09-virtual-geometry-bevy-0-14/) · [Bevy 0.15](https://jms55.github.io/posts/2024-11-14-virtual-geometry-bevy-0-15/) · [Bevy 0.16](https://jms55.github.io/posts/2025-03-27-virtual-geometry-bevy-0-16/)

---

## 4. 실질적 구현 경로 — GDExtension vs 코어 개조 경계

### ✅ GDExtension으로 가능 (RD compute API 위)
- **오프라인 전처리 전체:** meshlet 생성 + DAG 빌드 (~~meshoptimizer + METIS를 GDExtension에 링크~~ → **번들 meshoptimizer 1.2 단독. METIS 링크하지 말 것 — §10.2**). 산출물을 커스텀 리소스로 저장.
- **런타임 compute 패스:** DAG 컷(LOD 선택), 2-pass HZB 컬링, `draw_indirect` 기반 **하드웨어 래스터 경로** (Bevy 0.14 방식 — `R32Uint` visibility buffer + 하드코드 vertex shader).
- 즉 **Bevy 0.14 수준의 MVP는 이론상 GDExtension만으로 도달 가능** — 단, 아래 미확인 리스크에 걸림.

### ✅ Phase 0 스파이크 결과 — 64비트 atomic 조사 완료 (2026-08-03, 소스트리 직접 확인)
> 대상: Godot 4.8-dev (commit `eda2a482e9`). 아래는 실제 소스 grep 근거.

**결론: 스톡 Godot 4.x에서 Nanite SW 래스터는 오늘 당장은 불가능하다. 코어 C++ 수정(커스텀 빌드) 필요.**

| 요소 | 상태 | 근거 |
|------|------|------|
| **R64_UINT 이미지 포맷** | ✅ **완전히 배선됨** | `DATA_FORMAT_R64_UINT` @ `rendering_device_commons.h:193`, GDExtension 바인딩 `rendering_device.cpp:9386`, Vulkan 매핑 `VK_FORMAT_R64_UINT` @ `rendering_device_driver_vulkan.cpp:201`, SPIR-V 리플렉션 `SpvImageFormatR64ui` @ `rendering_shader_container.cpp:241` |
| **64비트 정수 타입(shaderInt64)** | ✅ 활성 | `rendering_device_driver_vulkan.cpp:872` |
| **64비트 BUFFER atomic** | ❌ **미활성** | `shaderBufferInt64Atomics`/`VkPhysicalDeviceVulkan12Features`가 device 생성 `pNext` 체인에 미포함. VK1.1 features만 체인됨(`:1443`). grep 0 matches |
| **64비트 IMAGE atomic (Nanite 핵심)** | ❌ **미활성** | `VK_EXT_shader_image_atomic_int64`가 요청 익스텐션 목록(`:565-618`)에 없음. `shaderImageInt64Atomics` grep 0 matches |
| **capability 플래그** | ❌ 없음 | 32비트만: `SUPPORTS_IMAGE_ATOMIC_32_BIT` @ `rendering_device_commons.h:1032`. 64비트 카운터파트 부재 |
| **코어 GLSL 선례** | ❌ 없음 | 모든 `.glsl`에서 `int64_t`/`uint64_t`/`GL_EXT_shader_atomic_int64` grep 0 matches. 기존 image atomic은 전부 32비트(`r32ui`, volumetric_fog/SDFGI) |

**핵심 진단:** GLSL→SPIR-V 컴파일러(glslang)는 `#extension GL_EXT_shader_image_int64` 문법을 **받아준다**. R64 포맷 계층도 완비돼 있다. **유일한 하드 블로커는 Vulkan device-feature/extension 활성화**인데, 이건 device 생성 시점에만 가능하고 **GDExtension에서 사후 활성화 불가**(코어 C++). → Bevy가 wgpu에서 막힌 것과 동일 계열의 벽이나, Godot에선 수정이 Vulkan RDD device-init 코드에 국소화돼 있어 더 다루기 쉽다.

**코어에서 바꿔야 할 것 (커스텀 빌드):**
1. `_initialize_device_extensions`(`rendering_device_driver_vulkan.cpp:562+`)에 `VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME` 등록 (버퍼 경로는 VK1.2/`VK_KHR_shader_atomic_int64`).
2. `VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT`(이미지) 쿼리·체인 + `shaderBufferInt64Atomics`/`shaderSharedInt64Atomics` 설정 (또는 `VkPhysicalDeviceVulkan12Features`를 실제로 forward) → `_check_device_capabilities` + `_initialize_device` `pNext` 양쪽.
3. `SUPPORTS_IMAGE_ATOMIC_64_BIT` Features 플래그를 `rendering_device_commons.h` + Vulkan/D3D12/Metal `has_feature()`에 추가 (셰이더 분기용).
4. 스토리지 이미지 생성 시 `R64_UINT`의 `VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT` 확인.

**비-포크 우회로:** `VulkanHooks::create_vulkan_device`(`rendering_device_driver_vulkan.cpp:1502` 부근)가 네이티브 플러그인이 `vkCreateDevice`를 가로채도록 허용 → C++ 플랫폼 훅으로 device feature를 켤 수 있다(포크 없이). 단 순수 GDScript/GDExtension 경로로는 불가.

### ✅ 남은 리스크 스파이크 결과 — 전부 조사 완료 (2026-08-03, 소스트리 직접 확인)

#### 리스크 1: HW 래스터 MVP가 순수 GDExtension으로 가능한가? → **✅ 가능 (코어 수정 0)**
Bevy 0.14식 HW 래스터 경로의 6개 필수 능력이 **전부 RenderingDevice GDExtension 바인딩에 노출**돼 있음:

| 능력 | 상태 | 근거 (`rendering_device.cpp`) |
|------|------|------|
| Indirect draw | ✅ | `draw_list_draw_indirect` bound `:9161` (INDIRECT usage 버퍼) |
| 커스텀 raster 파이프라인 | ✅ | `render_pipeline_create :9120`, `framebuffer_create :9082`, `draw_list_begin/bind/end :9147~9171` 전부 bound |
| **프래그먼트 셰이더 32b image atomic** | ✅ | Vulkan `fragmentStoresAndAtomics` 활성 `rendering_device_driver_vulkan.cpp:859`. 코어가 이미 프래그먼트에서 `imageAtomicOr` 사용(`scene_forward_clustered.glsl:2938`) |
| Vertex pulling (SSBO 인덱스) | ✅ | `draw_list_draw` `procedural_vertex_count` `:9160` — vertex_format=0이면 vertex array 불필요, `gl_VertexIndex`로 SSBO fetch |
| Compute→indirect 버퍼 | ✅ | `STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT`가 indirect draw와 동일 비트(`:1456`) — compute가 쓴 버퍼를 draw가 소비 |
| 배리어/동기화 | ✅ | 4.3+ 자동 렌더그래프가 처리(`barrier()`는 deprecated no-op). intra-compute는 `compute_list_add_barrier :9179` |

⇒ **Phase 2 MVP(offline DAG 별도)는 스톡 Godot에서 순수 GDExtension으로 도달 가능.** 유일한 소프트갭: `layout(early_fragment_tests)`가 1급 기능이 아님(raw GLSL로 통과 가능, atomic-max 방식엔 불필요).

#### 리스크 2: 지오메트리 스트리밍을 GDExtension으로? → **✅ 대체로 가능 (단일 버퍼 + device-address 방식)**
| 능력 | 상태 | 근거 |
|------|------|------|
| 대형 버퍼 서브레인지 업데이트 | ✅ | `buffer_update(buffer, offset, size, data)` bound `:9114` — 재생성 없이 페이지 부분 갱신 |
| 논블로킹 업로드 | ✅ (조건부) | 프레임 스테이징 링으로 deferred. 링 소진 시에만 stall(`FLUSH_AND_STALL_ALL`) → 업로드량을 블록 예산 이하로 관리 |
| 대형 영속 버퍼 | ✅ | 크기 캡 없음(VRAM 한도). **단 `maxStorageBufferRange` 쿼리 API 부재** — 플랫폼 한도를 외부에서 알아야 함 |
| **논블로킹 GPU→CPU 리드백** | ✅ | `buffer_get_data_async(buffer, callback, offset, size)` bound `:9117` — Nanite "requested pages" 피드백에 정확히 맞음. (동기 `buffer_get_data`는 full stall `:1310`) |
| **Bindless / descriptor indexing** | ❌ **부재** | `bindless`/`descriptor_indexing`/`runtimeDescriptorArray` grep 0 matches — 코어에도 없음 |

⇒ 스트리밍 레이어는 GDExtension으로 구축 가능하되, **페이지를 per-descriptor가 아니라 단일 대형 버퍼 + `buffer_get_device_address`(`:9118`)/SSBO 인덱싱**으로 주소지정해야 함(bindless 없으므로).

#### 리스크 3: 크로스플랫폼 32b image atomic → **✅ 3대 백엔드 지원(조건부), Apple은 주의**
| 백엔드 | 32b image atomic | 근거 |
|--------|------------------|------|
| Vulkan (Win/Linux/Android) | ✅ | `rendering_device_driver_vulkan.cpp:7402` |
| **Vulkan on Apple (MoltenVK)** | ❌ | `:7398` (Apple에선 Metal 백엔드 써야 함) |
| D3D12 | ✅ 무조건 | `rendering_device_driver_d3d12.cpp:5885` |
| Metal | ✅ **게이트** | Apple6+ GPU(A13/M1) **AND** macOS14/iOS17+ MSL3.1 (`metal_device_properties.cpp:153,157`) |

⇒ SW 래스터가 아닌 **HW 래스터(32b) 폴백은 현대 Win/Linux/Apple에서 다 됨.** 런타임에 `has_feature(SUPPORTS_IMAGE_ATOMIC_32_BIT)` 분기 필수. 구세대 Apple/OS·MoltenVK만 제외.

#### 리스크 4 (최대 관문): 프로덕션 라이팅 통합 → **⚠️ CompositorEffect로 "증강"은 가능, Godot 라이팅으로 "셰이딩"은 코어 필수**
Godot 4.x `CompositorEffect`(GDExtension 서브클래스 가능, `scene/resources/compositor.h:39`)로 커스텀 RD 패스를 씬 렌더에 주입 가능. 콜백 스테이지: `PRE_OPAQUE / POST_OPAQUE / POST_SKY / PRE_TRANSPARENT / POST_TRANSPARENT`.

**✅ CompositorEffect로 되는 것:**
- 자체 컬링/래스터 compute 패스 실행 → visibility buffer 생성
- 씬 depth/color/normal-roughness/velocity 읽기(`RenderSceneBuffersRD.get_depth_texture` 등 bound `:64~68`)
- `PRE_OPAQUE`에서 씬 depth에 기록 → Godot opaque가 가상 지오메트리와 올바르게 depth-test
- tonemap 이전에 자체 셰이딩 결과를 씬 color에 합성

**❌ CompositorEffect로 안 되는 것 (= 코어 C++/포크 강제):**
- **visibility buffer → Godot의 clustered 라이팅/그림자/GI 셰이딩 resolve.** `RenderForwardClustered`는 바인딩 없는 내부 C++(`bind_method` grep 0 matches). 라이트 클러스터·섀도우 아틀라스·VoxelGI/SDFGI 유니폼셋이 opaque 패스 내부에서만 생성돼 외부 노출 안 됨. `RenderSceneData`는 카메라 행렬만 줌(라이트 리스트 없음).
- **raw geometry 제출 API 부재** — 라이팅 받는 모든 지오메트리는 `Mesh`/`MultiMesh` RID를 거침. CompositorEffect 패스는 라이트 컬링 머신에 안 보임.

⇒ **결론:** depth 통합 + visibility 래스터 + 커스텀 합성까지는 순수 GDExtension. 하지만 가상 지오메트리를 **Godot 자체 라이트/그림자/GI로 셰이딩**하려면 (a) 코어 opaque 패스 확장으로 resolve를 라이팅 경로에 편입, 또는 (b) clustered 라이팅·그림자·GI를 CompositorEffect 셰이더에서 **전부 재구현**(엔진이 안 주는 데이터 복제). 둘 다 상당한 작업.

---

### 📌 스파이크 종합 — 3단계 능력 경계 (확정)

| 목표 수준 | 코어 수정 | 실현 방법 |
|-----------|-----------|-----------|
| **A. HW 래스터 + 자체 셰이딩 오버레이** (depth 통합, deferred-lite 자체 라이팅) | **0 (순수 GDExtension)** | RenderingDevice + CompositorEffect. Bevy 0.14 등가. 스트리밍도 단일버퍼+device-address로 가능 |
| **B. SW 래스터(마이크로폴리곤 최적화)** | **소규모 코어** (Vulkan device-init 4곳) 또는 VulkanHooks 네이티브 플러그인 | 64b image atomic 활성화 |
| **C. Godot 네이티브 라이팅/GI/그림자 완전 통합** | **대규모 코어/포크** | opaque 패스 확장 또는 라이팅 전체 재구현 |

**핵심 통찰:** 경로 A는 "진짜로 GDExtension만으로 동작하는 가상 지오메트리"의 실체가 있는 MVP다(코어 0). 비용 절벽은 B(SW 래스터)가 아니라 **C(네이티브 셰이딩 통합)** 에 있다 — 여기서 "증강 vs 대체"의 벽을 만난다.

> ⚠️ **2026-08-18 재채점.** 위 "비용 절벽" 프레이밍은 *C를 회피 대상으로 볼 때만* 유효하다. full-Nanite 커밋(포크 + 딥 코어 개조 승인) 하에서 **C는 절벽이 아니라 목표**이며, A/B는 C로 가는 **단계**다. "GDExtension 코어 0"은 더 이상 설계 제약이 아니라 **조기 검증 수단**일 뿐이다. §10 참조.

### 🔧 코어 C++ 개조 강제 지점
- **mesh shading 노출** (원한다면) — RD·렌더 그래프·드라이버.
- **빌트인 셰이딩/머티리얼·라이팅과의 통합** — visibility buffer를 Godot의 deferred/forward 셰이딩·GI·그림자와 엮는 지점. GDExtension의 커스텀 RD 패스는 렌더러 내부 리소스에 온전히 접근 못 함 → 진짜 프로덕션 통합은 코어(또는 엔진 포크).
- **지오메트리 스트리밍** — 페이지 단위 virtual-texture식 스트리밍을 Godot 리소스/IO·RD 버퍼 관리와 통합.

---

## 5. 실행 권고 — MVP → 프로덕션 로드맵

> ⚠️ 인력·기간 정량치는 검증된 claim으로 뒷받침되지 않음(open question). 아래는 리서치 정황(Bevy가 숙련 구현자 1인이 여러 릴리스에 걸쳐 진행 중이고 DAG 품질이 여전히 미완)에 기반한 **규모감 추정**이며 확정치가 아니다.

### Phase 0 — 결정 스파이크 (1~2주)
- Godot RD의 **64비트 이미지 atomic 노출 여부** 확인 (§4 리스크 1). 결과가 전체 아키텍처(SW 래스터 가능 여부, GDExtension vs 코어)를 가른다.
- meshoptimizer meshlet API를 GDExtension에서 호출해 단일 메시 meshlet 시각화.

### Phase 1 — 오프라인 DAG 빌더 MVP (수 주~수 개월)
- 스태틱 메시 한정. meshoptimizer 분할 + ~~METIS 그룹핑~~ **`meshopt_partitionClusters` 그룹핑(§10.2)** + QEM 단순화·재분할 루프 → DAG 커스텀 리소스.
- **여기가 품질의 8할.** Bevy조차 미완인 영역이니 "동작"과 "품질"을 분리해 목표.

### Phase 2 — 런타임 컬링 + HW 래스터 (Bevy 0.14 등가)
- DAG 컷(스크린 오차 LOD) compute → 2-pass HZB 컬링 → `draw_indirect` HW 래스터 → `R32Uint` visibility buffer → 풀스크린 resolve 패스.
- **mesh shader 불필요.** 데스크톱 Vulkan compute만 요구.

### Phase 3 — SW 래스터 (마이크로폴리곤) — Phase 0 결과 조건부
- 64b atomic 가능 시: compute SW 래스터 + SW/HW 하이브리드 선택.
- 불가 시: 코어 개조 or HW-only 유지(마이크로폴리곤 성능 손해 감수).

### Phase 4 — 스트리밍 + 프로덕션 통합
- 페이지 단위 지오메트리 스트리밍, 라이팅/GI/그림자 통합. **코어 개조 또는 엔진 포크 필요.**

### 규모감 (미확정, 정황 추정)
- **학습/MVP(Phase 0~2):** 렌더링 전문가 1인 기준 최소 수 개월.
- **프로덕션급(Phase 3~4):** 정황상 **최소 수 man-year급**. Bevy 진행 상황이 벤치마크.
- **하드웨어 하한:** Forward+ / 데스크톱 Vulkan(compute 필수). mesh shader 불요. SW 래스터 노리면 64b atomic 지원 GPU 필요.
- **버전 타깃:** Godot 4.x(4.4+) compute 기반. mesh shader는 4.5+ 어느 시점 랜딩 가능성(PR #88934 진행 중, 미머지) — 하지만 의존하지 말 것.

---

## 6. 남은 미확인 질문 (Open Questions)
1. Godot RD의 **64비트 이미지 atomic** 노출 여부 (SW 래스터 결정타, 미조사).
2. GDExtension compute-only 경로가 완전한 Nanite MVP를 감당하는 정확한 경계.
3. 지오메트리 스트리밍을 Godot IO/RD 버퍼 관리와 통합하는 구체적 지점.
4. Godot 특정 인력·기간 정량 추정 (Bevy 벤치마크 외 확정치 없음).

---

## 7. gaps 문서 재평가

기존 [godot-openworld-engine-gaps.md](./godot-openworld-engine-gaps.md)는 Nanite을 **"❌ 불가 / 사실상 비현실적"** 으로 분류했는데, 이 리서치 결과 다음과 같이 **정정**할 수 있다:

- **"불가"는 과함.** 기술적으로 구현 가능하며 오픈소스 선례(Bevy)가 존재한다.
- 정확한 표현은 **"GDExtension만으로는 부분적, 프로덕션급은 코어 개조 필요, 최난관은 DAG 품질"**.
- **mesh shader 필수는 오해** — compute SW 래스터로 우회 가능.
- ~~전략적으로는 여전히 gaps 문서 결론(스타일라이즈드·물량 축소·수동 LOD/HLOD)이 **비용 대비 합리적**~~ **[2026-08-18 폐기 — §10]** 이 판정은 "원신급 스타일라이즈드" 전제에서만 성립했다. AAA 기준(UE5.4+ / Horizon Forbidden West / CP2077 RT Overdrive)에서 물량 축소는 **fallback이지 기본값이 아니다.** 정확한 프레이밍은 "원천적으로 못 한다"가 아니라 "**엄청나게 비싸고, 그 비용을 이미 지불하기로 결정했다**"이다.

---

*리서치 방법: deep-research 하니스 — 5개 각도 병렬 웹서치 → 19개 소스 페치 → 84개 주장 추출 → 25개 적대적 3표 검증(23 confirmed / 2 refuted). 1차 소스(SIGGRAPH 원문, 공식 docs, 엔진 소스트리, 구현자 블로그) 중심.*

---

## 10. 2026-08-18 개정 — AAA 기준 재채점

> **전제 변경.** 이 문서의 §4~§7 판정은 "원신급 스타일라이즈드 오픈월드"를 목표로 가정하고 내려졌다. 사용자가 그 전제를 취소했다 — 비교 기준은 이제 **UE5.4+ Nanite/Lumen, Horizon Forbidden West, Cyberpunk 2077 RT Overdrive**이며 원신은 하한 참조점일 뿐이다.
> 확정 전략(메모리 2026-08-03 + 2026-08-18): ①포크 ②업스트림 디스커넥트(침습적 코어 개조 전면 허용) ③full-Nanite 커밋(deferred 전면 전환 + TAA 강제 수용) ④아트 디렉션 축소 폐기.
> **따라서 "man-year급이라 비합리적"은 더 이상 자동 탈락 사유가 아니다.** 그 비용은 이미 승인된 예산이다.
> **실측 절(§1~§3, §4의 file:line 표, 스파이크 결과)은 수정하지 않았다** — 아트 디렉션과 무관하게 유효하다.

### 10.1 뒤집힌 판정

| 항목 | 기존 결론 (스타일라이즈드 전제) | AAA 재채점 | 이유 |
|------|------|------|------|
| **경로 C(네이티브 라이팅 통합)** | "비용 절벽 = C. ❌ 신중, 원신급 아닌 한 비권장" | **✅ C가 목표.** 유일한 종착점 | full-Nanite가 이미 커밋됨. vis-buffer를 자체 deferred-lite로만 셰이딩하면 Godot 라이트/그림자/SDFGI/반사프로브와 **두 개의 라이팅 레짐**이 공존 → 심(seam)·BRDF 드리프트가 AAA에서 즉시 실격 |
| **경로 A(HW 래스터 오버레이)** | 종착점 후보. "A에서 멈추는 것도 합리적" | **경유지.** 코어 0 제약을 버리고 **C 아키텍처 안의 래스터 백엔드**로 재배치 | "코어 수정 0"은 포크 전제에서 가치가 사라짐. A의 잔존 가치는 **DAG 빌더·컬 풀넬·LOD 컷의 조기 검증**뿐 |
| **경로 B(SW 래스터 / 64b image atomic)** | "⚠️ 조건부 — 커스텀 빌드 감수 시" | **✅ 무조건 포함.** 3-site 코어 패치, 1~2주 | 마이크로폴리곤이 AAA 지오 밀도의 정의. HW 래스터 폴백만으로는 Nanite의 존재 이유가 사라짐. 포크라 "커스텀 빌드 감수"가 비용이 아님 |
| **`VulkanHooks` 우회로** | "비-포크 우회로"로 제시 | **폐기.** 드라이버 자체 수정이 정석 | 포크 전제에서 훅은 순수 기술부채 |
| **DAG 품질 = 최난관** | "🔴 최난관. Bevy도 미완(공간 위치 미반영)" | **🟡 중간으로 하향.** 상당 부분 벤더 해결됨 | **§10.2 — 번들 meshoptimizer 1.2가 공간 인지 파티셔너를 이미 제공** |
| **METIS 링크 필요** | §1.1·§4·Phase 1이 METIS 전제 | **불필요.** 외부 의존성 제거 | 동일 — `meshopt_partitionClusters`가 대체 |
| **"물량 축소가 비용 대비 합리적"(§7)** | 전략적 권고 | **폐기.** fallback으로 강등 | 전제 취소. §10.5 |
| **man-year 무게중심** | "C(라이팅 통합)" | **async compute 렌더그래프 재작업**으로 이동 | 라이팅은 `_inc.glsl` 재사용으로 저렴(perf 문서 §4). 진짜 man-year는 단일 큐 해체 |

### 10.2 ⭐ DAG 품질 재채점 — 번들 meshoptimizer가 이미 따라잡았다

이 문서 §3.2는 jms55(Bevy)를 인용해 *"DAG 품질이 가장 어렵고, 나는 아직 삼각형/메시렛의 **공간 위치를 그룹핑에 반영하지 못했다**"* 를 최난관 근거로 삼았다. **이 인용의 전제가 낡았다.** 로컬 트리 번들 버전 실측:

`thirdparty/meshoptimizer/meshoptimizer.h` — `MESHOPTIMIZER_VERSION 1020` (v1.2, `:15`)

| API | 위치 | Nanite 파이프라인에서의 역할 |
|-----|------|------|
| `meshopt_partitionClusters` | `:852` | **클러스터 그룹핑. `vertex_positions`를 인자로 받는 공간 인지 파티셔너** → METIS 대체. 주석(`:843`)이 명시: 위치 미제공 시에만 "공유 정점 클러스터만 그룹핑"으로 퇴화 |
| `meshopt_buildMeshletsSpatial` | `:757` | 공간 최적 메시렛 분할. 주석(`:748`): *"optimizes cluster subdivision for raytracing"* → §10.4의 BLAS 경로와 직결 |
| `meshopt_buildMeshletsFlex` | `:744` | min/max 삼각형 + split_factor — DAG 레벨별 가변 클러스터 크기 |
| `meshopt_simplifyWithUpdate` | `:560` | 단순화가 정점 위치/속성을 **파괴적으로 갱신** → 그룹 경계 품질 개선 |
| `meshopt_simplifyWithAttributes` | `:537` | `vertex_lock` 인자 = **그룹 경계 정점 잠금**(§1.2 크랙 방지)이 벤더 지원 |
| `meshopt_simplifyPrune` | `:589` | 미세 부유 지오메트리 제거 |
| `meshopt_encodeMeshlet` / `decodeMeshlet` | `:331` / `:349` | **메시렛 코덱** — perf 문서 §2.5-R6 압축/GPU 디코드의 기성 포맷 |
| `meshopt_computeClusterBounds` / `computeSphereBounds` | `:817` / `:827` | 컬링용 콘/스피어 바운드 |

**판정 변경:** §1.1의 "METIS 그래프 분할" 및 §4·Phase 1의 "meshoptimizer + METIS를 GDExtension에 링크"는 **더 이상 필요 없다.** DAG 빌더 전체가 이미 번들된 단일 라이브러리 위에 서고, 외부 의존성(METIS는 별도 벤더링·빌드시스템 통합 필요)이 사라진다. 남는 어려움은 "알고리즘 발명"이 아니라 **루프 배선 + 오차 단조성 강제 + 검증**이다.

⚠️ 단, `meshopt_partitionClusters`는 그룹핑 **한 단계**를 풀 뿐이다. DAG **전체 품질**(레벨 간 오차 단조성, locally-varying cut의 크랙-프리성)은 여전히 자체 검증 대상이다. "최난관"에서 **"1급 난제이되 벤더 지원이 있는 문제"** 로 강등하는 것이지 해결됐다는 뜻이 아니다.

> 코어는 이 API들을 **하나도 호출하지 않는다** (`meshopt_buildMeshlets`/`meshopt_partitionClusters` 전 트리 grep — `thirdparty/` 외 0 매치). 전량 미사용 자산.

### 10.3 A/B/C 재배치 — 경로에서 단계로

기존 3경로는 **"어디서 멈출 것인가"의 선택지**였다. AAA에선 전부 통과 지점이다:

| 단계 | 기존 이름 | AAA에서의 역할 | 코어 작업 | 공수 |
|------|-----------|----------------|-----------|------|
| **S1** | (Phase 1) | 오프라인 DAG 빌더 — meshopt 1.2 단독 | 0 (임포터 모듈) | 1~2개월 |
| **S2** | 경로 A | **런타임 검증 하니스.** CompositorEffect+HW 래스터로 컬링·LOD 컷·HZB를 조기 실증. **출하 대상 아님** | 0 | 1~2개월 |
| **S3** | 경로 B | 64b image atomic 활성화 → R64 단일-atomic vis-buffer + SW/HW 하이브리드 | **3-site 드라이버 패치** | 1~2주(코어) + 1~2개월(래스터) |
| **S4** | **경로 C** | **목표.** vis-buffer → 통합 GBuffer → 단일 deferred 라이팅 리졸브 | **대규모** — perf 문서 §1-⑨ + gbuffer 문서 §8 | **6~12 man-month** |
| **S5** | (Phase 4) | 스트리밍 + async compute 멀티큐 | **최대** — 렌더그래프 재작업 | **12+ man-month** |

**S2가 출하 대상이 아니라는 점이 재배치의 핵심이다.** 기존 문서는 A를 "되돌리기 쉬운 저리스크 Go"로 권했는데, 그 가치(코어 수정 0)는 포크 전제에서 소멸했다. A는 이제 **S4 아키텍처를 먼저 정한 뒤 그 안에서 래스터/컬 부분만 조기에 돌려보는 스파이크**로만 정당화된다 — 즉 **A의 자체 deferred-lite 라이팅은 짓지 말 것**(S4 리졸브에 버려질 코드).

### 10.4 §1↔§2 상호 의존 — 같은 GBuffer, 그리고 BLAS 수렴점

`godot-unified-gbuffer-aov-research.md`가 이미 확정한 대로(§3 "opaque 전면 deferred"), Nanite vis-buffer 리졸브와 비-Nanite opaque가 **동일 `RB_SCOPE_GBUFFER`** 에 emit하고 **단일 deferred 리졸브**가 둘 다 셰이딩한다. 여기에 §2(RT GI)가 얹히는 지점을 실측으로 확인했다:

1. **RT GI/반사는 그 GBuffer의 최대 소비자다.** ReSTIR 계열은 프라이머리 히트의 albedo/normal/roughness/depth/motion을 리저버 재사용·시간적 재투영에 쓴다 — gbuffer 문서 Tier-1 레이아웃이 그대로 입력. **따라서 GBuffer 스키마 확정이 §1·§2 공통의 크리티컬 패스다.** 어느 쪽도 단독으로 정할 수 없다.
2. **`gb_objectid`(R32_UINT)가 RT 히트 셰이딩과도 정합한다.** Godot의 TLAS 인스턴스는 `uint32_t id`를 갖는다(`servers/rendering/rendering_device.h:1379`) → vis-buffer가 쓰는 인스턴스 ID와 **동일 네임스페이스로 배선**하면 래스터 픽셀과 RT 히트가 같은 머티리얼 조회 경로를 쓴다.
3. **⭐ Nanite 지오 풀이 BLAS에 직접 먹는다.** `AccelerationStructureGeometry`(`servers/rendering/rendering_device.h:1360-1370`)는 `Mesh` RID가 아니라 **원시 `vertex_buffer`/`index_buffer` RID + offset/stride/format**을 받는다. 즉 perf 문서 §2.5-R2의 **단일 대형 SSBO 지오 풀에서 DAG의 특정 LOD 레벨을 잘라 그대로 BLAS로 빌드**할 수 있다 — UE가 Nanite용 별도 "fallback mesh"를 유지하는 것과 달리 **자료 중복 없이** 프록시를 얻는다. 이건 포크의 구조적 우위다.
   - 단, 지오 풀 버퍼가 BDA + AS-build usage 플래그로 생성돼야 한다. 이 제약을 **S3 시점에 미리 반영**할 것 — 나중에 바꾸면 스트리밍 레이어 전체를 건드린다.
4. **TLAS 리빌드와 Nanite 컬링은 같은 큐를 다툰다.** perf 문서 §1-①의 async compute 부재(`drivers/vulkan/rendering_device_driver_vulkan.cpp:1313`, `max_queue_count_per_family = 1`)가 §2에도 그대로 상속된다 — 매 프레임 TLAS 리빌드를 그래픽스와 오버랩할 수 없다. **async compute는 §1 전용 항목이 아니라 §1·§2 공유 게이팅이다.**

### 10.5 "현실적 대안" — 유지하되 충실도 비용 명시

gaps 문서 §1의 대안들은 **삭제하지 않는다.** AAA에서 이들은 **기본값이 아니라 fallback**이며, 채택 시 포기하는 것을 명시한다:

| 대안 | 유효한 용도 | 이걸 택하면 무엇을 포기하는가 |
|------|-------------|------|
| 수동 LOD + 임포스터 + HLOD | **영구 유지.** Nanite 비대상(투명·스킨드·폴리지 일부)과 원거리 프록시는 AAA에서도 HLOD/임포스터를 쓴다 | 없음 — 이건 대안이 아니라 **보완재**. UE5도 Nanite와 HLOD를 병행 |
| 저폴리 아트 디렉션(스타일라이즈드) | 프로토타입 단계의 콘텐츠 스톱갭 | **Nanite의 존재 이유 전체.** 폴리 예산을 수동 관리하는 순간 vis-buffer/DAG/스트리밍 전 스택이 순비용이 됨. **S1~S5를 지을 거면 이 선택지와 양립 불가** |
| 경로 A에서 정지(자체 deferred-lite) | 없음 (§10.3) | 라이팅 일관성. 두 BRDF 레짐 공존 → 심·그림자 불일치. 그리고 S4에서 버려질 코드 |
| HW 래스터만(64b atomic 포기) | Metal/구세대 Apple **폴백 경로로는 필수** | 마이크로폴리곤 성능(~3배). **주 경로로는 불가**, 그러나 `fog.cpp:53-71`식 변종 분기로 **폴백으로는 반드시 유지** |

### 10.6 man-year급으로 재확인된 항목

정직한 재산정. "비합리적"이라는 판정 없이 공수만 적는다.

| 항목 | 공수 | 왜 이 규모인가 | 선행 의존 |
|------|------|----------------|-----------|
| **async compute 멀티큐 + 렌더그래프 멀티스트림** | **6~12 man-month** | `rendering_device_graph.{h,cpp}` 2번째 커맨드 스트림 스케줄링 + 큐 소유권 이전 배리어. 단일 큐가 하드코딩(`:1313`)이고 배리어가 `VK_QUEUE_FAMILY_IGNORED`로 고정된 상태에서 전 엔진의 동기화 모델을 재작성 | 타임라인 세마포어(현재 **0 매치** — 전무) |
| **경로 C = vis-buffer→GBuffer→단일 deferred 리졸브** | **6~12 man-month** | `MODE_RESOLVE_MATERIAL` 변종 + analytic-derivative 셰이더 컴파일러 개조(`textureGrad` 재작성) + UE5.4식 셰이딩 빈 분류 + opaque 라이팅 루프 전체 이관 | GBuffer 스키마 확정(§2와 공유) |
| **RT GI/반사 프로덕션화** | **§2 문서 참조** | — | GBuffer 스키마, async compute |
| **지오메트리 스트리밍(페이지·압축·피드백)** | **3~6 man-month** | 단일 샤딩 SSBO 풀 + async 리드백 링 + 컴퓨트 디코드 + 예산 관리. meshopt 메시렛 코덱(`:331/:349`)이 포맷 부담을 일부 덜어줌 | S3 vis-buffer, BDA/AS-usage 버퍼 플래그(§10.4-3) |
| **오프라인 DAG 빌더** | **1~2 man-month** ⬇️ | **하향 조정** — §10.2. meshopt 1.2가 파티셔닝·잠금·코덱을 제공, METIS 통합 불요 | 없음 |
| **bindless / descriptor indexing** | **2~4 man-month** | `Vulkan12Features` 자체가 device-create에 미체인(`:1447`은 `Vulkan11Features`만) → UAB 디스크립터 풀 + RD 추상화에 신규 유니폼 타입 + 셰이더 컨테이너 리플렉션 | 없음 (v1엔 고정 샘플러 배열로 회피 가능) |

**소계: §1 단독으로 대략 1.5~3 man-year.** 여기서 async compute와 경로 C가 무게중심이며, 둘 다 §2와 공유된다.

**저비용·고효과로 재확인된 것 (합계 2~3주):** 64b image atomic 3-site 패치 · `drawIndirectCount` 상위 배선(드라이버는 3백엔드 **전부 완성** — `rendering_device_driver_vulkan.cpp:5764`, `rendering_device_driver_d3d12.cpp:4928`, `rendering_device_driver_metal.cpp:1745`, 인터페이스 `rendering_device_driver.h:698`. `rendering_device.{cpp,h}`·`rendering_device_graph.{cpp,h}`엔 **0 매치** = 상위 미배선 재확인) · `maxStorageBufferRange` LIMIT 1줄.
