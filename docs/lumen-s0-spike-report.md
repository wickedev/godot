# Lumen Phase 0 스파이크 (Task #5 / S0-1 · S0-2)

> **레인:** L3 (대행) · **Wave 0** · 정본: [lumen-roadmap](./godot-lumen-gi-implementation-roadmap.md) §1
> **베이스라인:** `6235d6e34b` · **브랜치:** `c3/lumen-spike`
> **상태:** **S0-1 완료 / S0-2 🔴 하드웨어·플랫폼 차단 (미착수)**

---

## 0. 요약

| | 항목 | 결과 |
|:-:|---|---|
| **S0-1** | SDFGI 재사용률 실측 | 🟢 **완료** — §2 재사용/신규 모듈 목록 |
| **S0-2** | GH-99119 RT API로 프로브 1발 → 경로 (c) 진입 판정 | 🔴 **차단** — 개발 머신에서 RT 실행 자체가 불가능(§3) |

---

## 1. 🔴 S0-2 차단 — Apple 플랫폼에 RT 경로가 존재하지 않는다

두 백엔드 모두 막혀 있다. 코드 실측:

**① Vulkan/MoltenVK — RT가 컴파일에서 제외된다**
```c
// drivers/vulkan/rendering_device_driver_vulkan.cpp:50-56
// Disable raytracing support on macOS and iOS due to MoltenVK limitations.
#if !(defined(MACOS_ENABLED) || defined(IOS_ENABLED))
#define VULKAN_RAYTRACING_ENABLED 1
#else
#define VULKAN_RAYTRACING_ENABLED 0
#endif
```
`blas_create`·`tlas_create`·`command_build_*`·`raytracing_pipeline_*` 전부 이 매크로 안에 있다(`:6334` 이하).

**② Metal — 전 엔트리포인트가 스텁이다**
```cpp
// drivers/metal/rendering_device_driver_metal.cpp:2323-2367
RDD::AccelerationStructureID RenderingDeviceDriverMetal::blas_create(...) {
    ERR_FAIL_V_MSG(AccelerationStructureID(), "Ray tracing is not currently supported by the Metal driver.");
}
```
`tlas_create` · `acceleration_structure_instance_write` · `acceleration_structure_free` · `acceleration_structure_get_scratch_size_bytes` · `raytracing_pipeline_create` · `raytracing_pipeline_get_shader_group_handles` · `command_build_blas` · `command_build_tlas` · `command_bind_raytracing_pipeline` — **전부 동일한 `ERR_FAIL`**.

⇒ **macOS에서는 RT 프로브를 한 발도 쏠 수 없다.** S0-2는 이 머신에서 수행 불가능하다.

### 이건 스파이크 일정 문제가 아니라 플랫폼 전략 문제다

로드맵의 Lumen **경로 (c)(HW-RT 프로덕션화)** 는 현재 코드베이스에서 **Apple 플랫폼에 경로가 아예 없다.** 선택지는 셋뿐이다:

| 선택지 | 내용 | 비용 |
|---|---|---|
| **(A)** macOS는 SW-SDF GI만 | 경로 (c)를 비-Apple 한정으로 동결 | 0 — 단 macOS 품질이 영구히 갈림 |
| **(B)** Metal RT 자체 구현 | `MTLAccelerationStructure`(Metal 3)로 스텁 8종 구현 | 실작업. 다만 미지의 영역은 아님 |
| **(C)** macOS 미지원 | — | 제품 결정 |

**G1 §5에서 C1이 제기한 Metal 성립성 우려와 같은 뿌리다.** 그쪽은 vis-buffer 래스터(draw-indirect-count 스텁 · 64b image atomic 부재), 이쪽은 RT 전무 — **Apple 결손이 누적되고 있다.** 개별 게이트가 아니라 한 번에 판단할 사안이다.

### S0-2를 실제로 수행하려면
RT 지원 GPU가 달린 **Windows 또는 Linux** 머신이 필요하다(`VULKAN_RAYTRACING_ENABLED 1` 경로). 확보되면 S0-2는 그대로 진행 가능하다 — API 표면 자체는 아래대로 갖춰져 있다.

### 진입 조건 자체는 갖춰져 있다 (API 표면 확인)
`UNIFORM_TYPE_ACCELERATION_STRUCTURE`(`rendering_device_commons.h`) · `blas_create`/`tlas_create`(`rendering_device.h:1377-1378`) · `AccelerationStructureInstance`(`:1382`) · `blas_build`/`tlas_build`(`:1391-1392`) · `raytracing_list_*` · `hit_sbt_*` 전부 실재하고 스크립트 바인딩도 있다. **막는 것은 API 부재가 아니라 백엔드 구현 부재다.**

---

## 2. S0-1 — SDFGI 재사용률 실측

대상: `gi.h`(850) · `gi.cpp`(4,323) · `sdfgi_*.glsl`(2,686) = **약 7,859줄**.

### (a) 🟢 그대로 재사용 — Lumen SW 경로의 토대가 이미 있다

| 모듈 | 위치 | Lumen에서의 역할 |
|---|---|---|
| **SDF 클립맵 캐스케이드** | `Cascade::sdf_tex`, `CASCADE_SIZE=128`, `MAX_CASCADES=8` (`gi.h:560-625`) | SW 트레이싱의 가속 구조 **그 자체** |
| **캐스케이드 워킹 스피어 트레이스** | `sdfgi_integrate.glsl:199-260` | **이미 SW SDF 레이트레이서다.** 캐스케이드 넘어가며 행진 → 히트 → 그래디언트 노멀 → 라이트 조회 |
| **복셀화 → JFA → SDF 생성** | `sdfgi_preprocess.glsl`(1,064줄), `jump_flood_uniform_set` | SDF 생성 파이프라인 전체 |
| **캐스케이드 스크롤링** | `dirty_regions`, `scroll_uniform_set`, `Cascade::DIRTY_ALL`(`gi.cpp:45`) | 클립맵 재중심화를 전체 재빌드 없이 — 오픈월드 필수 |
| **방향성 라디언스 저장** | `light_cascades` · `aniso0/1_cascades` | 라디언스 캐시의 복셀 계층 |
| **직접광 주입** | `sdfgi_direct_light.glsl`(575줄), static 1024 / dynamic 128 라이트 | 캐스케이드로의 광원 주입 |
| **라이트프로브 + 히스토리** | `lightprobe_texture` · `lightprobe_history_tex` · `lightprobe_average_tex`, oct 6×6, SH 16 | 시간적 누적 토대가 **이미 있다** |

⇒ **경로 (b)(SW SDF GI)의 하부 절반은 신규 개발이 아니라 개조다.** 로드맵이 SDFGI 재사용을 전제한 것은 옳았다.

### (b) 🔴 신규 개발 — Lumen의 상부 절반은 없다

| 모듈 | 왜 없나 |
|---|---|
| **스크린 프로브 gather** | SDFGI는 월드공간 프로브 격자다. Lumen의 스크린공간 적응 프로브는 개념이 다름 |
| **ReSTIR 리저버** (시공간 리샘플링) | 0. G1 §4 히스토리 계약이 이걸 위한 것 |
| **서피스 캐시 / 메시 카드** | Lumen의 실제 라디언스 캐시는 **카드 기반**이지 복셀이 아니다. SDFGI 복셀 라디언스는 근사 대체일 뿐 |
| **BRDF 중요도 샘플링** | SDFGI 프로브 샘플링은 코사인 가중 고정 |
| **ReSTIR 전용 디노이저** | 기존 시간적 필터는 프로브 평균이라 리저버 분산 구조와 안 맞음 |
| **HW-RT 경로** | §1 — 백엔드 자체가 없음 |

### (c) ⚠️ 개조 필요 (재사용도 신규도 아님)

- **프로브 밀도·응답 지연** — `PROBE_DIVISOR=16` 고정. 로드맵 Wave 1의 "SDFGI 응답 지연·동적 오브젝트 기여·TOD 전이 개선"이 여기에 걸린다.
- **`lightprobe_history_tex`의 재투영** — 현재는 프로브 격자 로컬. ReSTIR가 요구하는 **G1 §4 GBuffer 기반 재투영**과 다른 물건이다. 히스토리 저장소는 재사용해도 **재투영 로직은 신규**.

### (d) 🔗 ⓒ(bindless) 결론과의 교차 — 중요한 실증

`sdfgi_integrate.glsl:13-16`은 이미 **고정 크기 텍스처 배열**을 쓴다:
```glsl
layout(set = 0, binding = 1) uniform texture3D sdf_cascades[MAX_CASCADES];
```
그리고 `:206`에서 `for (uint j = params.cascade; j < params.max_cascades; j++)` 루프로 `sdf_cascades[j]`를 인덱싱한다.

**`j`는 푸시상수 `params.cascade`에서 파생된 루프 변수 = dynamically uniform이다.** 그래서 현재 활성화된 `shaderSampledImageArrayDynamicIndexing`(`rendering_device_driver_vulkan.cpp:867`)만으로 동작한다.

⇒ **[RD 스파이크 리포트](./rd-capability-spike-report.md) §2-1의 구분이 in-tree 실례로 확인된다.** "고정 배열 + dynamic 인덱싱"은 오늘 잘 돌아간다 — SDFGI가 증거다. Nanite 머티리얼 팬아웃이 안 되는 이유는 배열이 고정이라서가 아니라 **인덱스가 픽셀마다 달라 non-uniform이기 때문**이다. 두 경우를 뭉뚱그리면 안 된다.

---

## 3. G2 지오 풀 RFC — ⚪ 항목 확인 결과 (양자화 position → BLAS 입력)

G2 §6이 C3 서명 전 확인 항목으로 남긴 건이다.

### 확인 ① Godot은 AS 정점 포맷을 **검증하지 않는다**
`blas_create`(`rendering_device.cpp`)의 유일한 포맷 검사는:
```cpp
ERR_FAIL_COND_V_MSG(rd_geometry.vertex_format >= DataFormat::DATA_FORMAT_MAX, RID(), "An invalid vertex format was specified.");
```
**`DATA_FORMAT_MAX` 미만이면 무엇이든 통과**한다. 드라이버는 그대로 넘긴다:
```cpp
// rendering_device_driver_vulkan.cpp:6353
vk_geometry.geometry.triangles.vertexFormat = RD_TO_VK_FORMAT[geometry.vertex_format];
```
그리고 **`VK_FORMAT_FEATURE_2_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR`를 조회하는 코드가 코드베이스에 0건이다.**

⇒ **지원되지 않는 포맷을 넣으면 Godot 레벨 에러가 아니라 검증 레이어 에러/UB로 나타난다.** 조용한 실패 위험.

### 확인 ② 양자화 직접 입력은 **가능하다 — 단 포맷이 제한된다**
Vulkan 스펙의 AS 정점 버퍼 **필수 지원 포맷**은 다음뿐이다:
`R32G32_SFLOAT` · `R32G32B32_SFLOAT` · `R16G16_SFLOAT` · `R16G16B16A16_SFLOAT` · `R16G16_SNORM` · `R16G16B16A16_SNORM`

그 외(`R8G8B8A8_UNORM`, `R16G16B16A16_UNORM`, `A2B10G10R10` 등)는 **`bufferFeatures` 조회 후에만** 사용 가능하다.

**⇒ G2 §6에 대한 답:**
- **디코드 스테이징은 불필요하다.** 클러스터 로컬 양자화 position을 **`R16G16B16A16_SNORM`(필수 지원)으로 직접** BLAS에 넣을 수 있다.
- **단 3성분 16비트 포맷은 필수 목록에 없다.** `R16G16B16_SNORM/SFLOAT`는 쓸 수 없다 → **4성분(8바이트/정점), 넷째 성분은 패딩**이어야 한다.
- 더 촘촘한 패킹(10:10:10:2 등)을 원하면 **런타임 `bufferFeatures` 조회가 선행**되어야 하고, 그 조회 코드는 **지금 없다**.

### ✅ 실기 검증 완료 (2026-08-25, DGX Spark / NVIDIA GB10 / Linux aarch64 / driver 580.173.02)

RT 하드웨어에서 `VK_FORMAT_FEATURE_2_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR`를 직접 조회했다. **결과가 위 결론을 정정한다 — 정확히는 "이식성 하한"과 "이 하드웨어의 실제 능력"을 분리해야 한다.**

| 포맷 | 크기 | 스펙 필수? | **GB10 실측** |
|---|---:|:---:|:---:|
| `R32G32B32_SFLOAT` | 12B | ✅ | YES |
| `R16G16B16A16_SFLOAT` | 8B | ✅ | YES |
| **`R16G16B16A16_SNORM`** | **8B** | ✅ | **YES** |
| `R16G16B16_SNORM` | **6B** | ❌ | **YES** |
| `R16G16B16_SFLOAT` | 6B | ❌ | **YES** |
| `A2B10G10R10_UNORM_PACK32` | **4B** | ❌ | **YES** |
| `R8G8B8A8_SNORM` | 4B | ❌ | **YES** |

**정정:** 앞 절의 *"3성분 16비트가 필수 목록에 없으니 4성분 8바이트여야 한다"* 는 **이식성 하한으로는 맞지만 하드웨어 제약은 아니다.** NVIDIA GB10은 6B(`R16G16B16_SNORM`)도, 4B(`A2B10G10R10`)도 받는다.

**⇒ G2 동결 권고 (수정):**
- **계약(동결)은 `R16G16B16A16_SNORM` 8B — 스펙 필수 포맷이라 벤더 무관 보장.** 지오 풀 레이아웃은 이걸 기준으로 잡는다.
- **더 촘촘한 패킹(6B/4B)은 벤더별 최적화**로 남긴다. 활성화하려면 **런타임 `bufferFeatures` 조회 + 폴백 경로**가 선행되어야 하고, **그 조회 코드는 Godot에 없다**(코드베이스 0건). 이건 별도 태스크다.
- **1벤더 실측이다.** AMD/Intel/모바일은 미확인 — 바로 그 이유로 런타임 조회가 필요하다.

**남은 미검증:** 실제 `blas_create` → `blas_build` 왕복은 아직 안 태웠다. 포맷 수용 여부는 확정됐으나 **Godot 경로 전체를 통과시킨 것은 아니다** — Godot 빌드가 선행되어야 한다(§4).

---

## 4. 다음 단계

### ✅ RT 하드웨어 확보 — 드라이버 프로브 완료 (DGX Spark / NVIDIA GB10)

| 확장 | GB10 |
|---|:---:|
| `VK_KHR_acceleration_structure` | **YES** |
| `VK_KHR_ray_tracing_pipeline` | **YES** |
| `VK_KHR_ray_query` | **YES** |
| `VK_KHR_deferred_host_operations` | **YES** |
| `VK_KHR_buffer_device_address` | **YES** |
| `VK_EXT_descriptor_indexing` | **YES** |
| `VK_EXT_calibrated_timestamps` | **YES** (프로파일러 P2 선행조건) |

⇒ **S0-2의 하드웨어 전제는 충족됐다.** 남은 건 이 박스에서 Godot을 빌드하는 것뿐이다.

### 🔗 RD 스파이크 ⓒ 판정의 하드웨어측 확증

같은 프로브에서 `VkPhysicalDeviceVulkan12Features`를 읽었다:

| 기능 비트 | GB10 |
|---|:---:|
| `shaderSampledImageArrayNonUniformIndexing` | **1** |
| `runtimeDescriptorArray` | **1** |
| `descriptorBindingPartiallyBound` | **1** |
| `descriptorBindingVariableDescriptorCount` | **1** |
| `bufferDeviceAddress` | **1** |

⇒ **[RD 스파이크](./rd-capability-spike-report.md) ⓒ의 "하드웨어 한계가 아니라 디바이스 생성 누락" 판정이 실측으로 확증됐다.** 하드웨어·드라이버는 bindless를 **전부 지원한다.** Godot이 `Vulkan12Features`를 체인하지 않아 꺼져 있을 뿐이다. **L1의 "최소 nonuniform 활성화" 태스크는 없는 기능을 만드는 게 아니라 이미 있는 것을 켜는 일이다.**

1. **[사용자 승인 대기]** DGX에서 Godot 빌드 → S0-2 프로브 + BLAS 왕복. 빌드에 dev 패키지 8종 apt 설치가 필요해 사용자 확인 중
2. **[전략 결정 필요]** Apple RT 부재에 대한 (A)/(B)/(C) 판단 — G1 §5 Metal 성립성과 **묶어서**
3. S0-1 후속: 경로 (b) 진입 시 §2(c) 개조 항목의 공수 산정

---

*작성: 2026-08-25 · L3-대행 / godot-contributer-3 · 베이스라인 `6235d6e34b`*
