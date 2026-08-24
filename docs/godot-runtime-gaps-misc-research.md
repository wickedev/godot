# Godot 오픈월드 — 나머지 런타임/렌더 격차 딥리서치 (모션블러·업스케일러·GPU컬링·AI·네트워킹·캐릭터·크래시)

> [godot-openworld-engine-gaps.md](./godot-openworld-engine-gaps.md)가 다룬 7개 축(Nanite·Lumen·VT·콘솔·프로파일러·애니메이션·VFX) **밖**에 남은 런타임/렌더 격차를 실측한 문서.
> 대상: Godot **4.8-dev** 소스트리 (commit `eda2a482e9`). 모든 file:line은 로컬 트리 직접 실측이며, "없다"는 전부 검색어+결과로 뒷받침한다.
>
> **§3(GPU 오클루전 컬링)은 [godot-nanite-performance-implementation-research.md](./godot-nanite-performance-implementation-research.md) §2.2와 주제가 겹친다.** 중복 서술 대신 **"왜 따로 만들면 안 되는가"의 논증**에 집중하고, HZB/SPD 리듀서의 구현 상세는 그쪽을 참조한다.

---

## 0. 요약 표

| # | 격차 | 판정 | 한 줄 결론 |
|---|------|------|-----------|
| 1 | 모션 블러 | 🟢 | 코어엔 0매치지만 **속도 버퍼가 이미 GDExtension에 노출**(`get_velocity_texture`) → CompositorEffect 애드온으로 오늘 해결. 성숙한 MIT 애드온 실재 |
| 2 | 업스케일러 | 🟡 | FSR1/FSR2.2.1/MetalFX(Spatial+Temporal) 실재. DLSS/XeSS 부재는 **기술이 아니라 라이선스**(§4 콘솔과 동형). FSR3.1 업그레이드가 현실 경로 |
| 3 | GPU 오클루전 컬링 | 🟡 | **브리핑 반증** — HiZ는 CPU·GPU 양쪽에 이미 있다. 진짜 벽은 피라미드가 아니라 **GPU 컬링 결과의 소비 경로(indirect draw)** → §1 Nanite와 **반드시 통합** |
| 4 | AI (BT/EQS) | 🟢 | Nav는 성숙(RVO2+Recast+워커스레드). BT/블랙보드는 0매치지만 **LimboAI(MIT, C++ GDExtension, 프리빌트)**가 완전 대체. EQS만 자작 |
| 5 | 네트워킹 | 🟡 | **브리핑 부분 반증** — per-peer 가시성·델타 동기화는 **있다**. 없는 건 *자동 공간 AOI*·리플리케이션 그래프·롤백. 4~8인 co-op엔 충분, 대규모는 자작 |
| 6 | 캐릭터 표현 | 🟡 | anisotropy flowmap·Kajiya-Kay 토대·세퍼러블 SSS·transmittance 완비, 블렌드셰이프 상한 없음. **AAA 재평가:** 카드형 헤어로 최소선 충족, 프리인테그레이티드 SSS LUT 필요(셰이더 문제), 스트랜드 헤어는 장기 로드맵 |
| 7 | 크래시/텔레메트리 | 🟢 | 크래시 핸들러 3플랫폼 실재(심볼화 포함). 수집 파이프라인 부재는 **Sentry 공식 Godot SDK(GDExtension)**가 코어 수정 0으로 메움 |
| 8 | 셰이더 컴파일 스터터 | ✅ **격차 아님** | ubershader + zstd 셰이더 캐시 + 드로우타임 폴백 + PSO 사전 워밍업 + **측정용 퍼포먼스 모니터**까지 완비. 남는 건 "로딩 시간으로 전가"뿐 |

**전략 요약:** 이 7+1개 축 중 **엔진 레벨의 진짜 격차는 사실상 §3 하나뿐**이고, 그마저도 독립 과제가 아니라 §1 Nanite 워크스트림의 부분집합이다. 나머지는 전부 GDExtension/애드온/셰이더 저작/설정 레벨에서 닫힌다. **AAA 목표로 전환 시 §1(모션 블러)·§2(업스케일러)·§6(캐릭터·스트랜드 헤어)의 우선순위가 상승하고, §6의 판정 신호가 🟢→🟡로 변경된다.** 핵심은 **엔진 격차가 아니라 AAA 구현 순서의 재조정**이다.

---

## 1. 모션 블러 🟢 (정정: "부재"는 맞지만 "격차"는 아니다)

### (a) 무엇이 없나
- 빌트인 카메라/오브젝트 모션 블러가 **없다**.
  - 실측: `grep -ril "motion_blur" servers/ scene/ drivers/` → **0 매치**.
  - `WorldEnvironment`/`CameraAttributes` 어디에도 셔터 앵글·블러 샘플 파라미터 없음.

### (b) 이미 있는 토대 (실측)
여기가 반전이다. **모션 블러의 유일한 어려운 입력인 모션 벡터가 이미 완비돼 있고, 심지어 GDExtension에 바인딩돼 있다.**

| 요소 | 상태 | 근거 |
|------|------|------|
| 속도(velocity) 버퍼 생성 | ✅ | `RenderSceneBuffersRD::ensure_velocity()` `render_scene_buffers_rd.cpp:730` |
| 포맷 | `R16G16_SFLOAT` | `get_velocity_format() :845` |
| 씬 렌더가 모션 벡터 기록 | ✅ | `COLOR_PASS_FLAG_MOTION_VECTORS` `render_forward_clustered.h:216`, 셰이더 `#define MOTION_VECTORS` `scene_shader_forward_clustered.cpp:664` |
| **GDExtension 노출** | ✅ **핵심** | `ClassDB::bind_method("get_velocity_texture", ...)` `render_scene_buffers_rd.cpp:68`, `get_velocity_layer` `:69` |
| MultiMesh 모션 벡터 | ✅ | `_multimesh_uses_motion_vectors_offsets` `mesh_storage.cpp:1686` |
| 카메라-only 재투영 경로 | ✅ | `RendererRD::MotionVectorsStore`(`effects/motion_vectors_store.h:36`), 호출 `render_forward_clustered.cpp:2232`. reprojection matrix로 깊이에서 속도 합성 |
| CompositorEffect 콜백 스테이지 | ✅ | `scene/resources/compositor.h` — `PRE_OPAQUE/POST_OPAQUE/POST_SKY/PRE_TRANSPARENT/POST_TRANSPARENT` |

⇒ **오브젝트 모션 블러에 필요한 per-pixel 속도 텍스처를 GDExtension이 그대로 읽을 수 있다.** 자작 경로는 "모션 벡터를 만든다"가 아니라 "이미 있는 걸 읽어서 블러 커널을 돈다"뿐이다.

### ⚠️ 결정적 단서 — 속도 버퍼는 조건부로만 존재한다
`render_forward_clustered.cpp:1865-1870`:
```
if (p_render_data->scene_data->calculate_motion_vectors) {
    color_pass_flags |= COLOR_PASS_FLAG_MOTION_VECTORS;
```
그리고 `:193-194`가 그때만 `ensure_velocity()`를 부른다. `calculate_motion_vectors`는 **TAA / FSR2 / MetalFX Temporal이 켜졌을 때만** 참이다.

⇒ **모션 블러 애드온은 TAA(또는 시간적 업스케일러)를 강제로 켜야 동작한다.** 이건 실무 제약이지 블로커는 아니다(오픈월드 목표에선 어차피 TAA를 켠다). 다만 애드온 문서에 이 의존성이 명시돼 있지 않은 경우가 많아 "왜 안 되지"의 단골 원인이다.

### (c) 경계
- **GDExtension: ✅ 전부.** `CompositorEffect` 서브클래스 + `get_velocity_texture()` + 컴퓨트 셰이더. 코어 수정 **0**.
- 국소 코어: TAA 없이도 속도 버퍼를 강제 할당하는 옵션을 원할 때만(`ensure_velocity` 게이트 완화, ~10줄).
- 대규모 포크: 불필요.

### (d) 공수
- 카메라 모션 블러: **수일**.
- 오브젝트 모션 블러(타일맥스 + 뉴로만-Guertin 리컨스트럭션): **1~3주**.
- 또는 **0일** — 기성 애드온 채택.

### (e) 현실적 대안 (웹 실조사)
- **[sphynx-owner/JFA_driven_motion_blur_addon](https://github.com/sphynx-owner/JFA_driven_motion_blur_addon)** — **MIT, 342★**, Jump Flood 기반 속도 딜레이션. Godot 4.3/4.4 브랜치. `GuertinMotionBlur`(범용 최고품질) + `SphynxSimpleJumpFloodMotionBlur` 2종 제공. 명시 제약: **MSAA 켜면 투명 오브젝트가 블러 배경 위에 안 그려짐 → MSAA 끄기 필요**(TAA 전제와 정합).
- [sphynx-owner/godot-motion-blur-addon-simplified](https://github.com/sphynx-owner/godot-motion-blur-addon-simplified) — 코어 병합을 겨냥한 축약판.

### 업스트림 상태 (실조사)
| 항목 | 번호 | 상태 |
|------|------|------|
| 프로포절 "Add support for motion blur" | [#2933](https://github.com/godotengine/godot-proposals/issues/2933) | **Open** (2021-06-28, 4.x 마일스톤) |
| 프로포절 "Port community motion blur addon" | [#12258](https://github.com/godotengine/godot-proposals/issues/12258) | Open |
| 프로포절 "Motion Blur Effect" | [#13556](https://github.com/godotengine/godot-proposals/issues/13556) | Open |
| **PR "Add motion blur compositor effect" (fire)** | [#92711](https://github.com/godotengine/godot/pull/92711) | ❌ **Closed 미머지** (2024-06-03 열림 → 2024-08-21 닫힘) |

**PR #92711이 거부된 이유가 중요하다 — 기술이 아니라 취향 합의 부재였다.** 렌더링 팀(QbieShay)은 "모션 블러가 어떻게 보여야 하는지에 대해 사람마다 니즈가 갈린다", "아무에게도 도움 안 되는 미지근한 솔루션을 피하고 싶다"며 **"유저 공간(GDScript/GLSL)에서 워크그룹이 검증한 뒤에 코어로"**를 요구했다. 즉 **코어가 이걸 막고 있는 게 아니라, 코어가 이걸 애드온으로 남기기로 한 것**이다.

### (f) 판정 🟢 — **격차로 계상하지 말 것**
AAA "룩"에서 모션 블러의 결손이 눈에 띈다는 브리핑의 전제는 맞다. 하지만 **엔진 격차가 아니다.** 진입 비용이 이 문서 8개 축 중 압도적으로 최저이며(GDExtension 100%, 기성 MIT 애드온 실재), 심지어 업스트림이 의도적으로 애드온 레이어에 남겨둔 영역이다.
> **AAA 재평가:** 모션 블러는 AAA 포토리얼 렌더의 필수 요소다 — 카메라 모션 블러(24fps 필름 룩)와 오브젝트 모션 블러(고속 전투·차량의 움직임 가독성) 모두 필요. 애드온 경로(GDExtension)로 가능하므로 **엔진 격차는 아니지만 우선순위는 올라간다** — AAA에서는 "있으면 좋은 것"이 아니라 "있어야 하는 것"이다.

---

## 2. 업스케일러 (DLSS/XeSS) 🟡

### (a) 무엇이 없나
- **DLSS·XeSS 전무.** 실측: `grep -ril "dlss\|xess" --include="*.cpp" --include="*.h" .` → **0 매치**.
- FSR3 / FSR4 / 프레임 생성(frame generation) 전무.

### (b) 이미 있는 토대 (실측)
| 업스케일러 | 상태 | 근거 |
|-----------|------|------|
| FSR **1** (공간) | ✅ | `effects/fsr.cpp`, `shaders/effects/fsr_upscale.glsl` |
| FSR **2.2.1** (시간) | ✅ | `effects/fsr2.cpp` + `thirdparty/amd-fsr2/` 11개 GLSL 패스. 버전: `ffx_fsr2.h:33/38/43` → MAJOR 2 / MINOR 2 / PATCH 1 |
| **MetalFX Spatial** | ✅ | `MFXSpatialEffect` `effects/metal_fx.h:57` |
| **MetalFX Temporal** | ✅ | `MFXTemporalEffect` `effects/metal_fx.h:102` |
| 뷰포트 스케일링 모드 열거 | ✅ 6종 | `rendering_server_enums.h:463-469` — `BILINEAR/FSR/FSR2/METALFX_SPATIAL/METALFX_TEMPORAL/NEAREST` |
| 시간적 업스케일러 = 지터+모션벡터 요구 배선 | ✅ | `:481-483`이 시간적 모드일 때 지터 경로 활성 |

**MetalFX가 실제로 커버하는 것 (실조사):** Godot **4.4**에 [PR #99603 (stuartcarnie)](https://github.com/godotengine/godot/pull/99603)로 랜딩. macOS/iOS, **공간 + 시간 둘 다**. **프레임 생성은 없다.** 알려진 이슈: 미지원 드라이버에서 MetalFX Temporal 폴백 시 TAA 지터가 안 꺼짐([#103782](https://github.com/godotengine/godot/issues/103782)).
⇒ Apple 플랫폼에선 DLSS 부재가 사실상 상쇄된다. **격차는 Windows/NVIDIA에 한정된다.**

### (c) 경계 — 왜 DLSS가 GDExtension으로 안 되는가, 그리고 그게 사실인가
**적대적 재검증:** "DLSS는 코어 개조 필요"라는 통념을 반증 시도했다. 결과는 **부분 반증**이다.

- **기술적으로는 코어가 막고 있지 않다.** Streamline/NGX가 요구하는 입력(컬러·**깊이**·**모션 벡터**·지터 오프셋·exposure)은 전부 `RenderSceneBuffersRD` 바인딩으로 GDExtension에 노출돼 있다(§1 참조). 네이티브 핸들도 `get_driver_resource`로 꺼낼 수 있다.
- **진짜 벽은 배포/라이선스다.** Streamline SDK 프레임워크 자체는 오픈이지만, 실제 추론 가중치인 **`nvngx_dlss.dll`은 NVIDIA 독점 라이선스 프리빌트 바이너리**이며 반드시 함께 셰이핑해야 한다(`sl.interposer.dll`, `sl.common.dll`, `sl.dlss.dll` + `nvngx_dlss.dll`). MIT 오픈소스 코어에 넣을 수 없다.
  - ⇒ **이건 §4 콘솔 export와 정확히 같은 구조의 문제다** — "코드가 아니라 계약이 막는다". 그리고 **같은 해법**을 갖는다: 클로즈드 배포 레이어(여기선 GDExtension 플러그인)로 분리.
- Vulkan 지원: Streamline은 D3D11/D3D12/**Vulkan 1.2+** 지원 → Godot Vulkan 백엔드와 원리상 호환.

| 경로 | 코어 수정 | 판정 |
|------|-----------|------|
| **DLSS를 클로즈드 GDExtension 플러그인으로** | **0** | ✅ 가능. 프로포절 [#11142 "Replacing Rendering components from GDExtension"](https://github.com/godotengine/godot-proposals/issues/11142)가 정확히 이 방향 |
| DLSS를 코어에 | — | ❌ 라이선스상 영구 불가 |
| **FSR2 → FSR3.1 업그레이드** | 국소 코어 (thirdparty 교체 + 패스 배선) | ✅ **가장 현실적** |
| FSR4 | — | ❌ **DX12 전용, Vulkan 미지원** → Godot에 무의미 |

### (d) 공수
- FSR2 → **FSR3.1**: FidelityFX SDK는 **MIT**(소수 파일 예외), **Vulkan 지원 O**. 기존 `fsr2.cpp` 구조를 그대로 따라가면 **2~6주**. 프레임 생성은 Vulkan에서 스왑체인 모델이 DX12와 달라 별도 난이도.
- DLSS GDExtension: **1~3개월** (Streamline 인터포저 + RD 네이티브 핸들 브리징 + NVIDIA 개발자 등록).
- XeSS: SDK 자체는 공개지만 커널 바이너리 재배포 조건이 DLSS와 유사 → 동일 경로.

### 업스트림 상태 (실조사)
| 항목 | 번호 | 상태 |
|------|------|------|
| DLSS 2.0 support | [#808](https://github.com/godotengine/godot-proposals/issues/808) | 2020-05-08 |
| Add DLSS 2.0 support | [#2239](https://github.com/godotengine/godot-proposals/issues/2239) | ❌ **Closed / archived** |
| GDExtension으로 렌더링 컴포넌트 교체 | [#11142](https://github.com/godotengine/godot-proposals/issues/11142) | Open — **DLSS를 명시적 유스케이스로 거론** |
| MetalFX | [PR #99603](https://github.com/godotengine/godot/pull/99603) | ✅ Merged (4.4) |

### (e) 현실적 대안
- **FSR2.2.1 내장 + MetalFX Temporal로 AAA 4K/60 목표를 부분 충족 가능.** 단 FSR2.2.1은 DLSS/FSR3.1 대비 품질 열세가 AAA 임계 해상도(1440p→4K)에서 두드러진다. **충실도 비용: 세부 디테일 손실(고스트·플리커링)이 포토리얼 AAA에서 수용 가능한지 분기마다 판단 필요.**
- GDExtension DLSS 플러그인(클로즈드 배포)으로 격차 해소 시 **충실도 비용 0** — DLSS가 현재 최고 품질 시간적 업스케일러이며, FSR3.1도 동급에 근접.
- 모바일/Switch급 타깃엔 FSR1(공간)이 오히려 정답(시간적 비용 없음).

### (f) 판정 🟡 — **AAA에서는 우선순위 상승, 단 시간적 순서는 §1~3 이후**
DLSS 부재는 "AAA 대비 -15~25% 프레임"이지 "룩의 결손"이 아니다. 단 AAA 포토리얼 목표에서는 4K/60 유지가 전제이고, FSR2.2.1의 품질 열세(고스트·디스오클루전 아티팩트)가 지오메트리·GI 품질을 올린 뒤에는 **눈에 띄는 병목**이 된다. §1~3(Nanite·GI·VT)이 안정화된 시점에 DLSS GDExtension 또는 FSR3.1 업그레이드를 진입한다. **지금은 순서가 틀렸지만, AAA 로드맵에서는 P1~P2로 격상된다.**

---

## 3. GPU 오클루전 컬링 🟡 — **브리핑 반증: HiZ는 이미 있다**

### ⚠️ 적대적 재검증 결과 (브리핑의 "HiZ/뎁스 피라미드 부재"는 **틀렸다**)

브리핑은 "HiZ/뎁스 피라미드 부재 확인"을 전제로 줬다. 반증 시도했고 **반증에 성공했다.** Godot 4.8-dev에는 **두 개의 서로 다른 HZB가 실재한다.**

**① CPU HZB — 이미 오클루전 컬링에 실사용 중**
```
class RendererSceneOcclusionCull {
  class HZBuffer {
    LocalVector<float> data;
    LocalVector<Size2i> sizes;
    LocalVector<float *> mips;      // ← 뎁스 피라미드 그 자체
```
`servers/rendering/renderer_scene_occlusion_cull.h:40-56`. `_is_occluded()`(`:60`)가 AABB 8코너를 투영해 스크린 사각형을 구하고, **대각선 길이로 밉 레벨을 선택**해(`:113-116`) 계층적으로 테스트한다. 샘플 상한 512(`:120`). **완전한 교과서적 HZB 알고리즘이 CPU에 구현돼 있다.**
채우는 쪽은 `modules/raycast/raycast_occlusion_cull.cpp` — Embree(`<embree4/rtcore.h>` `raycast_occlusion_cull.h:38`)로 **카메라 레이를 멀티스레드 캐스팅**(`_camera_rays_threaded :63`, `_raycast :149`)해 소프트웨어 뎁스를 만든다.

**② GPU Hi-Z 뎁스 피라미드 — SSR용으로 이미 존재**
| 요소 | 근거 |
|------|------|
| 전용 컴퓨트 리듀서 셰이더 | `shaders/effects/screen_space_reflection_hiz.glsl` (8×8 워크그룹, 2×2 `max` 리듀스, 홀수 폭/높이 4변종) |
| 밉체인 텍스처 | `ss_effects.cpp:1458` — `RB_HIZ`, `R32_SFLOAT`, `STORAGE_BIT`, `p_ssr_buffers.mipmaps` |
| 밉 생성 루프 | `ss_effects.cpp:1573-1601` — 밉 m-1을 읽어 m에 쓰는 표준 다운샘플 체인 |
| 계층적 트래버설 소비자 | `screen_space_reflection.glsl:189` — `texelFetch(source_hiz, cell_index, cur_level)` 로 셀 단위 레벨 하강 |

⇒ **"GPU 뎁스 피라미드를 만드는 코드"는 이미 트리에 있고 매 프레임 돌고 있다.** 새로 발명할 게 아니다.

### (a) 그래서 진짜로 없는 것은 무엇인가
반증을 거치고 나면 격차의 정의가 훨씬 좁고 정확해진다.

1. **오클루전용 폴라리티가 아니다.** SSR HiZ는 `max` 리듀스다. Godot은 reverse-Z(`Projection::set_depth_correction(flip_y, reverse_z, remap_z)` `core/math/projection.cpp:787`)를 쓰므로 max = **가장 가까운** 깊이다. 오클루전 컬링은 보수적으로 **가장 먼** 깊이가 필요하다 → `min` 변종 1개 추가면 끝(셰이더 3줄).
2. **오클루더가 GPU에 없다.** 현재 오클루전 입력은 `OccluderInstance3D`가 CPU 측 Embree BVH에 올린 **전용 오클루더 지오메트리**다. GPU 씬 뎁스를 오클루더로 쓰려면 렌더 순서가 바뀐다(전 프레임 뎁스 재투영 또는 2-pass).
3. **🔴 진짜 벽 — 컬링 결과를 소비할 경로가 없다.** Godot의 가시성 결정은 `RendererSceneCull`에서 **CPU가** 내리고, 드로우 콜도 CPU가 제출한다. GPU가 컬링해서 얻은 비트마스크를 CPU 가시성 리스트로 되돌리려면 리드백이 필요한데:
   - 동기 `buffer_get_data`는 **무조건 풀스톨**(성능 문서 §2.5-R3).
   - 비동기 `buffer_get_data_async`는 **N프레임 지연** → 그 지연만큼 팝인/오클루전 오류.
   ⇒ **GPU 컬링은 결과가 GPU에 남아 `draw_indirect`로 곧장 소비될 때만 의미가 있다.**

### (b) 이미 있는 토대 종합
- CPU HZB 알고리즘 + 오클루더 저작 워크플로(`OccluderInstance3D`) — **재사용 가능**
- GPU 뎁스 피라미드 리듀서 + 계층 트래버설 셰이더 선례 — **재사용 가능**
- 컴퓨트 → indirect 버퍼 → draw 체인의 **자동 렌더그래프 동기화** (성능 문서 §1 "코어 0")
- `draw_list_draw_indirect` 바인딩 실재 (Nanite 문서 §4 리스크1)

### (c) 경계 — **왜 §1 Nanite와 반드시 통합해야 하는가 (핵심 논증)**

> **둘을 따로 만들면 두 번째 것은 첫 번째 것의 부분집합을 재구현하는 순수 낭비다.**

근거 넷:

1. **결과 소비 경로가 물리적으로 동일하다.** GPU 오클루전 컬링을 리드백 없이 쓰려면 → GPU-driven 드로우 제출이 필요하다 → 그건 정확히 Nanite 파이프라인의 `cull → compact → indirect args → draw_indirect` 체인이다. Nanite를 만들면 GPU 오클루전 컬링은 **그 안의 한 스테이지로 공짜로 딸려온다**. 반대로 GPU 오클루전 컬링만 먼저 만들면, 소비 경로가 없어서 **CPU 리드백으로 되돌아가고 → 스톨 → 결국 Embree CPU 방식보다 나을 게 없다.**
2. **HZB가 문자 그대로 같은 리소스다.** Nanite 2-pass 오클루전이 요구하는 HZB(성능 문서 §2.2: "Pass1 전 프레임 가시집합 → HZB 빌드 → Pass2 나머지 테스트", SPD 단일 디스패치 리듀서, ~0.1-0.3ms@1440p)와, 일반 메시 오클루전 컬링이 요구하는 HZB는 **동일한 텍스처**다. 두 번 만들면 메모리·대역폭·리듀스 디스패치가 그대로 2배.
3. **하이브리드 씬에서 정합성이 강제된다.** Nanite 지오메트리와 일반 메시가 한 화면에 공존하면(성능 문서 §2.4 "스텐실 하이브리드"), **일반 메시가 Nanite 지오메트리를 가려야 하고 그 역도 성립해야 한다.** HZB가 분리돼 있으면 서로를 오클루더로 못 쓴다 → 두 시스템이 각자 과도하게 보수적으로 컬링 → **통합했을 때보다 성능이 나쁘다.** 이건 단순 낭비를 넘어 **음의 시너지**다.
4. **업스트림 방향도 수렴한다.** reduz의 [GPU-driven renderer 제안](https://gist.github.com/reduz/c5769d0e705d8ab7ac187d63be0099b5)은 오클루전을 "레이트레이싱으로 작은 스크린 버퍼에 뎁스를 생성"하는 방식으로 그린다 — 방법은 달라도 **"오클루전은 GPU-driven 제출의 하위 스테이지"**라는 구조 판단은 동일하다.

| 경로 | 코어 수정 | 비고 |
|------|-----------|------|
| SSR HiZ에 `min` 리듀스 변종 추가 | 국소 (셰이더 3줄 + 모드 등록) | 사소 |
| GPU 컬링 컴퓨트 패스 자체 | **0 (GDExtension)** | RD 컴퓨트 + 자동 렌더그래프 |
| **결과를 CPU 가시성에 반영** | 🔴 **대규모 코어** | `RendererSceneCull` 재작업 + 리드백 지연 감수 — **비권장** |
| **결과를 GPU indirect draw로 소비** | = **§1 Nanite 워크스트림** | ✅ **권장. 별도 과제로 세우지 말 것** |

### (d) 공수
- **독립 과제로 세우면:** 수 개월 + 리드백 스톨로 인한 실효 이득 불확실. **ROI 음수 가능성.**
- **§1에 흡수시키면:** 성능 문서 §2.2가 이미 계상한 "2-pass HZB + SPD 리듀서, **코어 수정 0**, 최대 단일-패스 win(우선순위 4위)"에 포함. **증분 비용 ≈ 0.**

### (e) 현실적 대안 (지금 당장)
- **현행 Embree CPU 오클루전 컬링을 그대로 쓴다.** 오픈월드에서 실제로 효과가 크며(지형/건물이 좋은 오클루더), 멀티스레드다. `OccluderInstance3D` 베이크를 CLI로 자동화.
- 포털/셀 기반 수동 가시성 + 월드 스트리밍 셀 언로드가 오클루전보다 먼저 효과를 낸다.
- HLOD 병합으로 드로우콜 자체를 줄이는 게 컬링 정밀도보다 ROI 높다.

### (f) 판정 🟡 — **독립 격차로 계상 금지. §1의 하위 항목으로 병합할 것.**

---

## 4. AI (내비게이션 / 행동) 🟢

### (a) 무엇이 없나
- **행동 트리·블랙보드 전무.** 실측: `grep -ril "behavior_tree|behaviortree|blackboard" --include="*.cpp" --include="*.h" .` (thirdparty 제외) → **0 매치**.
- **유틸리티 AI / GOAP / EQS 전무.** `grep -ril "utility_ai|goap|environment_query"` → **0 매치**.
- 군중 시뮬레이션 / 플로우필드 전무.
- ⚠️ 주의: `AnimationNodeStateMachine`은 있지만 이건 **애니메이션 FSM**이지 AI FSM이 아니다. 혼동 금지.

### (b) 이미 있는 토대 (실측) — 내비게이션은 오히려 성숙하다
`modules/navigation_3d/` 전체 구조:

| 구성요소 | 파일 |
|----------|------|
| 맵 | `nav_map_3d.{h,cpp}` |
| 에이전트 | `nav_agent_3d.{h,cpp}` |
| 링크(점프/사다리) | `nav_link_3d.{h,cpp}` |
| 동적 장애물 | `nav_obstacle_3d.{h,cpp}` |
| 리전 | `nav_region_3d.{h,cpp}` |
| 서버 | `3d/godot_navigation_server_3d.{h,cpp}` |
| **네브메시 생성** | `3d/nav_mesh_generator_3d.{h,cpp}`, `3d/navigation_mesh_generator.{h,cpp}` |
| 쿼리 | `3d/nav_mesh_queries_3d.{h,cpp}` |
| 증분 빌더 | `3d/nav_map_builder_3d.{h,cpp}`, `3d/nav_region_builder_3d.{h,cpp}` |

**서드파티 (둘 다 번들됨):** `thirdparty/recastnavigation/` (네브메시 생성), `thirdparty/rvo2/` (회피 — **2D·3D 양쪽**).

**스레딩 (핵심):**
- 맵 이터레이션 빌드가 **비동기 워커 태스크**: `WorkerThreadPool::add_native_task(&NavMap3D::_build_iteration_threaded, ..., SNAME("NavMapBuilder3D"))` `nav_map_3d.cpp:396`. 완료 폴링(`is_task_completed :445`) → **네브메시 재빌드가 메인 스레드를 안 막는다.** 오픈월드 청크 스트리밍에 정확히 맞는 설계.
- 회피는 **그룹 태스크 병렬화**: `add_template_group_task(..., compute_single_avoidance_step_2d/3d, ...)` `:616, :631`.
- 더티 요청 큐잉으로 증분 갱신: `add_region_sync_dirty_request :709`, `add_agent_sync_dirty_request :725`, `add_obstacle_sync_dirty_request :732`.

**⚠️ 실측된 성능 약점:** 회피 그룹 태스크 직후 **`wait_for_group_task_completion`**(`:617, :632`)로 즉시 조인한다. 즉 회피는 병렬이지만 **비동기가 아니다** — `NavMap3D::sync()`(`:430`)가 매 프레임 회피 완료까지 블록한다. 에이전트 수 N에 대해 RVO는 KdTree 이웃 질의라 대략 O(N log N)이며, 이 비용이 **메인 루프에 그대로 노출**된다. 공식 문서도 "회피 처리는 등록된 에이전트가 많으면 상당한 비용이며 실제로 필요한 에이전트에만 켜야 한다"고 명시.
추가로 RVO 라이브러리가 `std::vector`를 요구해 매 스텝 `LocalVector`를 못 쓰고 벡터를 재구성한다(`:497-498, :567-568, :578` — "Cannot use LocalVector here as RVO library expects std::vector to build KdTree").

### (c) 경계
| 목표 | 코어 수정 | 방법 |
|------|-----------|------|
| **행동 트리 + HSM + 블랙보드** | **0** | **LimboAI GDExtension 프리빌트** |
| 유틸리티 AI / GOAP | **0** | GDScript/GDExtension 자작 (알고리즘일 뿐) |
| **EQS 대응물** | **0** | 자작. Godot의 `PhysicsDirectSpaceState3D` 오버랩 질의 + `NavigationServer3D.map_get_closest_point` 조합으로 스코어링 그리드 구현 가능 |
| 대규모 NPC용 비동기 회피 | 국소 코어 | `wait_for_group_task_completion`을 다음 프레임으로 지연(1프레임 latency 감수) |
| 플로우필드 군중 | **0** | 자작 (네브메시에서 코스트필드 → GPU 컴퓨트) |

### (d) 공수
- LimboAI 도입: **1일** (프리컴파일 바이너리 드롭인).
- EQS 대응물 자작: **2~4주** (질의 템플릿 + 스코어러 + 에디터 디버그 그리기).
- 비동기 회피 코어 패치: **수일**.

### (e) 현실적 대안 (웹 실조사)
- **[LimboAI](https://github.com/limbonaut/limboai)** — **MIT, 약 3,000★**. **C++ 모듈 AND GDExtension 양쪽 지원**, 릴리스/Actions에 **프리컴파일 바이너리 제공 → 엔진 재컴파일 불필요.** v1.8.x가 GDExtension으로 Godot **4.6+** 지원(모듈은 4.7). 제공: 행동 트리 비주얼 에디터, 컴포짓/데코레이터/컨디션, **블랙보드**, **계층적 상태 머신(HSM)**, `BTState`로 BT↔HSM 상호운용, **비주얼 디버거**, 성능 모니터링 툴, 빌트인 클래스 문서, 유닛 테스트, GDScript+C# 바인딩. 커스텀 태스크는 GDScript 또는 C++로.
  - ⚠️ 실측 유의: GDExtension 빌드는 "모듈 대비 기능이 일부 제한"이라고 공식 문서가 명시(구체 항목은 미기재) → **채택 전 GDExtension 빌드로 스파이크 필수.**
  - 딥코어 개조를 이미 허용한 이 프로젝트에선 **C++ 모듈로 통합하는 게 오히려 자연스럽다**(기능 제한 회피 + 성능).
- [beehave](https://github.com/bitbrain/beehave) — 순수 GDScript 행동 트리. 가볍고 읽기 쉬우나 **대규모 NPC에서 컴파일드 C++ 대비 느림.**
- **대규모 NPC 전략:** LimboAI BT를 티어링(근거리=매프레임, 중거리=N프레임마다, 원거리=상태만) + 회피는 근접 에이전트에만 활성 + 원거리는 단순 웨이포인트 추종. 원신도 동일 패턴.

### (f) 판정 🟢 — **격차 아님. 애드온/모듈로 완결.**
내비게이션은 오히려 이 문서에서 **가장 성숙한 서브시스템**이다(비동기 베이킹·병렬 회피·증분 갱신·링크·동적 장애물 전부 실재). 남은 실질 과제는 **EQS 대응물 자작**과 **회피 비동기화** 둘뿐이며 둘 다 소규모다.

---

## 5. 네트워킹 🟡 — **브리핑 부분 반증: 관심영역이 "전무"는 아니다**

### (a) 무엇이 없나 (정확히)
- **롤백/예측/재조정/랙보상 전무.** `grep -ril "rollback|snapshot_interpolat|client_prediction|reconciliation|lag_compensation"` → 2 매치인데 **둘 다 무관한 false positive**(`scene/resources/animation.cpp`, `servers/rendering/shader_language.cpp` — 파서 백트래킹 문맥). ⇒ **실질 0.**
- **자동 관심영역·리플리케이션 그래프 전무.** `grep -ril "interest_management|area_of_interest|relevancy|replication_graph"` → **0 매치**.
- 대역폭 양자화/비트패킹 전무 (`Variant` 인코딩 그대로).
- 서버 권위 이동·틱레이트 추상화 전무.

### ⚠️ (b) 이미 있는 토대 — **per-peer 가시성과 델타 동기화는 있다** (브리핑 정정)
| 기능 | 상태 | 근거 |
|------|------|------|
| `MultiplayerSynchronizer` | ✅ | `modules/multiplayer/multiplayer_synchronizer.{h,cpp}` |
| `MultiplayerSpawner` | ✅ | `multiplayer_spawner.{h,cpp}` |
| RPC | ✅ | `scene_rpc_interface.{h,cpp}` + `scene_cache_interface.{h,cpp}`(NodePath 캐싱) |
| 리플리케이션 설정 리소스 | ✅ | `scene_replication_config.{h,cpp}` |
| **per-peer 가시성 집합** | ✅ | `HashSet<int> peer_visibility` `multiplayer_synchronizer.h:60`, `set_visibility_for(peer, visible)` `:111` |
| **커스텀 가시성 필터 (Callable)** | ✅ | `HashSet<Callable> visibility_filters` `:59`, `add_visibility_filter` `:115` |
| 가시성 갱신 모드 | ✅ | `VisibilityUpdateMode` (IDLE/PHYSICS/NONE) `:58`, `set_visibility_update_mode :114` |
| **델타(변경분) 동기화** | ✅ | `get_delta_state(cur_usec, last_usec, &indexes)` `multiplayer_synchronizer.cpp:407`, `get_delta_properties :437`, 인터벌 `set_delta_interval :322` |
| 델타 송신 경로 | ✅ | `_send_delta(peer, syncs, usec, last_watch_usecs)` `scene_replication_interface.cpp:710`, MTU 관리 `:711` |
| 멀티플레이어 프로파일러 | ✅ | `multiplayer_debugger.{h,cpp}` |
| 트랜스포트 | ✅ | `modules/enet`, `modules/websocket`, `modules/webrtc` |

⇒ **"관심영역 부재"는 과했다.** 정확한 표현은 **"수동 관심영역은 있고, 자동(공간 인덱스 기반) 관심영역이 없다"**이다.

### 실측된 진짜 한계
1. **가시성 평가가 O(peers × synchronizers).** `_update_sync_visibility(0, sync)`가 전 피어 루프(`scene_replication_interface.cpp:320-332`), 매 sync 사이클이 `for (KeyValue<int, PeerInfo> &E : peers_info)`로 피어별 노드 집합을 순회(`:145-152`). 피어 8명 × 동기화 노드 500개면 4,000 평가/틱 — 감당 가능. 피어 64명 × 5,000노드면 320,000 — **터진다.** 공간 인덱스(그리드/옥트리)가 없어 "가까운 것만"을 엔진이 못 좁혀준다.
2. **필터 로직이 전부 유저 책임.** `add_visibility_filter(Callable)`는 훅일 뿐, 거리 기반 AOI를 원하면 GDScript로 직접 짜야 하고 그게 곧 1번의 O(n²)를 유발한다.
3. **보간·예측 없음** → 지연 하에서 캐릭터가 튄다. 유저가 직접 구현해야.
4. ENet 피어 상한 **4095** (`modules/enet/enet_connection.cpp:308` — "The number of clients must be set between 1 and 4095"). 기본 32(`:376-377`). MMO급 아니면 무관.

### (c) 경계
| 목표 | 코어 수정 | 방법 |
|------|-----------|------|
| 거리 기반 AOI | **0** | `visibility_filters`에 그리드 해시 기반 Callable. 유저 공간에서 공간 인덱스 직접 유지 |
| 스냅샷 보간 | **0** | 동기화 프로퍼티를 버퍼링 후 렌더 시각에 보간 (GDScript/GDExtension) |
| 클라이언트 예측 + 재조정 | **0** | 자작 (입력 시퀀스 버퍼 + 서버 확인 롤포워드) |
| **엔진 레벨 리플리케이션 그래프** | 🔴 대규모 | `SceneReplicationInterface` 재작업 + 공간 인덱스 도입 — **오픈월드 co-op 규모(4~8인)엔 불필요** |
| 대역폭 양자화 | 국소 코어 or 0 | 프로퍼티를 미리 패킹한 `PackedByteArray`로 동기화하면 코어 0 |

### (d) 공수
- 거리 AOI + 스냅샷 보간 자작: **2~4주**.
- 예측/재조정까지: **1~3개월** (게임플레이 결합도 높음).

### (e) 현실적 대안 (웹 실조사)
- **[godot-rollback-netcode](https://gitlab.com/snopek-games/godot-rollback-netcode)** (David Snopek, **MIT**) — Godot 4 포트 완료(v1.0.0-alpha9, 2023-12-24), 최신 **v1.0.0-alpha10 (2024-05-04)**. 타이머·애니메이션·난수·사운드까지 롤백 지원 + 디버깅 툴. [maximkulkin 포크](https://github.com/maximkulkin/godot-rollback-netcode) 존재.
  - ⚠️ **오픈월드 co-op엔 부적합하다.** 롤백은 **완전 결정론 시뮬레이션 + 소수 플레이어 + 저지연**(격투/대전 게임)을 전제한다. 오픈월드는 물리·AI·스트리밍이 비결정론적이고 상태량이 커서 롤백 리시뮬 비용이 폭발한다. 여전히 `alpha` 단계인 점도 리스크.
- **원신급 co-op의 실제 요구는 4인이다.** 빌트인 `MultiplayerSynchronizer` + 델타 + 수동 가시성 + 자작 보간으로 **충분히 도달 가능**하다. 이 규모에서 리플리케이션 그래프는 오버엔지니어링.
- 자동 AOI 전용 애드온은 실조사에서 **성숙한 것을 찾지 못했다** — 자작 전제로 계획할 것.

### (f) 판정 🟡 — **우선순위 낮음(단, 싱글플레이어 우선이면 🟢)**
목표 규모(4~8인 co-op)에서 엔진 격차는 **보간/예측 부재** 하나로 좁혀지고, 그건 유저 공간에서 푸는 게 정석이다. **MMO를 하려는 게 아니면 여기 엔진 개조 비용을 쓰지 말 것.**

---

## 6. 캐릭터 표현 (헤어·이방성·SSS·블렌드셰이프) 🟢

### (a) 무엇이 없나
- **헤어/퍼 시스템 전무.** `grep -ril "hair_|strand_" servers/ scene/ modules/` → **0 매치**(audio_stream_player_3d의 "chair" 오탐 제외). 스트랜드 기반 헤어, 헤어 그루밍, 헤어 시뮬레이션 전부 없음.
- 전용 헤어 BSDF(Marschner/Chiang) 없음.
- 클로스 시뮬레이션은 `SoftBody3D` 수준(헤어용 아님).

### (b) 이미 있는 토대 (실측) — 예상보다 훨씬 좋다

**이방성(anisotropy) — 완비. Kajiya-Kay 헤어의 토대가 이미 다 있다.**
| 요소 | 근거 |
|------|------|
| 머티리얼 피처 | `FEATURE_ANISOTROPY` `scene/resources/material.h:214` |
| **flowmap 텍스처 슬롯** | `TEXTURE_FLOWMAP` `material.h:155`, 프로퍼티 `anisotropy_flowmap` `material.cpp:3636` |
| flowmap 셰이더 힌트 | `uniform sampler2D texture_flowmap : hint_anisotropy` `material.cpp:1093` |
| flowmap 샘플링(트라이플래너 포함) | `material.cpp:1899, 1901` |
| **접선 프레임 회전** | `scene_forward_clustered.glsl:1451-1457` — `anisotropy_flow`로 tangent/binormal을 회전 |
| **Filament식 bent normal IBL** | `:1693-1698, :2055-2060` — `anisotropic_direction`→`bent_normal`, 반사 프로브/스카이에도 이방성 반영 |
| 직접광 이방성 | `:2684-2686` — `light_compute`에 tangent+anisotropy 전달 |

⇒ **Kajiya-Kay / 이중 스페큘러 헤어 셰이딩을 순수 셰이더 코드로 즉시 만들 수 있다.** 원신 헤어의 핵심(플로우맵 기반 이방성 하이라이트)이 정확히 이 조합이다.

**SSS — 스크린스페이스 세퍼러블 SSS 완비.**
| 요소 | 근거 |
|------|------|
| 피처/텍스처 | `FEATURE_SUBSURFACE_SCATTERING` `material.h:217`, `TEXTURE_SUBSURFACE_SCATTERING` `:158` |
| 셰이더 | `shaders/effects/subsurface_scattering.glsl` — **13탭 커널**, 일반 커널 + **`skin_kernel`(RGB 분리 산란 프로파일)** 2종 |
| 품질 4단계 | `SUB_SURFACE_SCATTERING_QUALITY_{DISABLED,LOW,MEDIUM,HIGH}` `rendering_server_enums.h:750-753`, 프로젝트 설정 `rendering_server.cpp:3790` (기본 Low) |
| 스케일/깊이스케일 | `ss_effects.cpp:346-348` |
| **투과(transmittance)** | `transmittance_color/_depth/_boost` `material.h:479-481, 553-555` — 귀/손가락 역광 투과 |
| **백라이트** | `backlight` `material.h:482, 557` |

⇒ 원신급 피부/옥 표현에 필요한 것은 이미 다 있다. 없는 건 **프리인테그레이티드 SSS LUT**와 볼류메트릭 SSS. **AAA 재평가:** 프리인테그레이티드 SSS LUT는 AAA 포토리얼 피부 렌더링의 사실상 표준이다 — Penner 2011/GPU Gems 3 접근법으로, 세퍼러블 SSS만으로는 곡률이 높은 피부 영역(코·귀·손가락)에서 산란 프로파일이 정확하지 않다. 세퍼러블 SSS는 일차 근사로는 유효하나, **AAA 피부 퀄리티를 위해서는 프리인테그레이티드 LUT가 필요**하며 이는 셰이더 + 2D LUT 텍스처 에셋 문제로 엔진 격차가 아니다.

**블렌드셰이프 — 개수 하드 상한 없음, GPU 컴퓨트 스키닝.**
| 요소 | 근거 |
|------|------|
| **개수 상한** | **없다.** `mesh_storage.cpp:252`는 `ERR_FAIL_COND(p_blend_shape_count < 0)` — **음수만** 거부. `MAX_BLEND_SHAPE` 류 상수 grep 0 매치 |
| GPU 컴퓨트 적용 | `shaders/skeleton.glsl:134-151`(2D), `:209-225`(3D) — 워크그룹당 정점, 셰이프 가중 누산 |
| 정규화 모드 | `normalized_blend_shapes` `skeleton.glsl:51` |
| 디스패치 | `MeshStorage::update_mesh_instances()` `mesh_storage.cpp:1179-1240` |
| 법선 옥타헤드럴 압축 | `decode_uint_oct_to_norm` `skeleton.glsl:225` |

**⚠️ 실측된 성능 특성:** `skeleton.glsl:138`의 `for (uint i = 0; i < params.blend_shape_count; i++)`는 **모든 셰이프를 무조건 순회**한다(가중치 0도). 즉 **비용이 셰이프 개수에 정확히 선형**이며 스킨드 메시마다 매 프레임 전체 정점을 디스패치한다. 페이셜 리그 50~100 셰이프 × 캐릭터 20체면 실측 필요 구간.
→ 완화책(코어 0): 셰이프를 표정 프리셋으로 사전 병합, 원거리 캐릭터는 `MeshInstance3D` 블렌드셰이프 비활성 LOD, 활성 셰이프만 담는 압축 인덱스 배열(국소 코어 ~30줄).

### (c) 경계
| 목표 | 코어 수정 | 방법 |
|------|-----------|------|
| Kajiya-Kay 헤어 셰이딩 | **0** | 커스텀 셰이더 (`ANISOTROPY`/`ANISOTROPY_FLOW` 빌트인 사용) |
| 카드형 헤어(원신 방식) | **0** | 아트 + 알파 정렬 셰이더 |
| 셸 기반 퍼 | **0** | 기성 애드온 |
| 헤어 시뮬(스트랜드) | **0** | `SpringBoneSimulator3D`(애니메이션 문서 §"이미 있는 것") + 본 체인 |
| 스트랜드 래스터/헤어 BSDF | 🔴 대규모 코어 | **AAA 재평가:** 포토리얼 AAA 캐릭터에서 스트랜드 기반 헤어 + Marschner/Chiang BSDF는 사실상 필수다. UE Groom/Hair Strands, Frostbite Strand Hair, Horizon Forbidden West의 헤어 파이프라인이 이 레벨. 단 Godot은 Jolt의 `Physics/Hair` 폴더를 번들에서 제외했고(물리 문서 §8c), GPU 헤어 시뮬레이션은 Jolt 업스트림 대기. **당장은 카드형 헤어 + Kajiya-Kay로 프로토타입을 진행하고, AAA 최종 목표로 스트랜드 헤어를 계획에 포함한다.** |
| 블렌드셰이프 희소 최적화 | 국소 코어 | `skeleton.glsl` + 디스패치 배선 |

### (d) 공수
- 헤어 셰이더(Kajiya-Kay + 플로우맵 + 알파 정렬): **1~2주** (아트 이터레이션 별도).
- 셸 퍼 도입: **1일** (애드온).
- 블렌드셰이프 희소화: **수일**.

### (e) 현실적 대안 (웹 실조사)
- **[Arnklit/ShellFurGodot](https://github.com/Arnklit/ShellFurGodot)** — 셸 기반 퍼 노드 애드온.
- **[maxmuermann/sofluffy](https://github.com/maxmuermann/sofluffy)** — Godot 4 셸 퍼. 난류/지터 스트랜드 변위, 두께 프로파일, **거리 기반 셸 비활성 동적 LOD**.
- **Godot Character Creation Suite (Pelatho)** — **Kajiya-Kay 이방성 + 플로우맵 스페큘러 + 램버트 랩 디퓨즈 + 헤어 서브서피스**(어두운 머리에 붉은 글로우)를 이미 구현 → 참조 구현으로 유용.
- **원신 자체가 스트랜드 헤어를 안 쓴다.** 카드형 메시 + 플로우맵 이방성 + 그라디언트 램프다. **Godot이 이미 그걸 할 수 있다.**

### (f) 판정 🟡 — **AAA 재평가: 셰이더 저작 문제가 주를 이루지만 일부는 진짜 격차**
"헤어 시스템 부재"는 사실이지만, 카드형 헤어 + Kajiya-Kay + 이방성 플로우맵 + SSS + transmittance 조합은 **AAA의 최소 요구선을 충족한다.** 단 **AAA 포토리얼 최종 목표**에서는 다음이 추가로 필요하다:
- **프리인테그레이티드 SSS LUT** — 포토리얼 피부의 필수 요소 (셰이더 + LUT 에셋으로 해결, 엔진 격차 아님)
- **스트랜드 기반 헤어 + Marschner/Chiang BSDF** — Horizon/UE Groom 급 헤어 파이프라인 (Jolt `Physics/Hair` 업스트림 대기 + 커스텀 래스터라이저 필요, 대규모 과제)
- 안면 리그 최적화 — 블렌드셰이프 희소화로 AAA급 100+ 셰이프 실현 가능 (국소 코어, 수일)

**우선순위 전략:** 카드형 헤어 + 세퍼러블 SSS로 AAA 프로토타입을 진행하고, 프리인테그레이티드 SSS LUT와 안면 블렌드셰이프 희소화는 P1, 스트랜드 헤어는 P3(장기 로드맵)으로 배치한다.

---

## 7. 크래시 핸들링 / 텔레메트리 🟢

### (a) 무엇이 없나
- **크래시 리포트 수집·업로드 파이프라인 전무.** `grep -ril "sentry|breakpad|crashpad|telemetry" --include="*.cpp" --include="*.h" .` (thirdparty 제외) → 4 매치인데 **전부 무관 false positive**(`platform/linuxbsd/dbus-so_wrap.h`, `editor/editor_node.{h,cpp}`, `editor/debugger/debug_adapter/debug_adapter_types.h`). ⇒ **실질 0.**
- **미니덤프 생성 없음.** Windows SEH 핸들러도 `MiniDumpWriteDump`를 호출하지 않는다(`crash_handler_windows_seh.cpp` grep 0).
- 심볼 서버 / 릴리스 빌드 심볼리케이션 워크플로 없음.
- 프로덕션 텔레메트리(세션·성능·이탈 지표) 없음. — §5 GPU 프로파일러는 **개발 시점 도구**로 성격이 다르다.

### (b) 이미 있는 토대 (실측) — 크래시 핸들러 자체는 3플랫폼 다 있고, 심볼화까지 한다
| 플랫폼 | 파일 | 구현 |
|--------|------|------|
| macOS | `platform/macos/crash_handler_macos.mm` | `backtrace()` 256프레임(`:70`) → **`atos` 심볼화**(`StackTraceMacOS::symbolize_with_atos` `:98`) → 실패 시 `dladdr` + `abi::__cxa_demangle` 폴백(`:118-125`). 엔진 버전+커밋 해시 출력(`:89-91`), 로드 주소 출력(`:96`) |
| Windows (SEH) | `platform/windows/crash_handler_windows_seh.cpp` | **dbghelp** — `StackWalk64`(`:240`), `SymGetSymFromAddr64`(`:78`) |
| Windows (signal) | `platform/windows/crash_handler_windows_signal.cpp` | **libbacktrace** — `backtrace_create_state`(`:277`), `<thirdparty/libbacktrace/backtrace.h>`(`:43`) |
| Linux/BSD | `platform/linuxbsd/crash_handler_linuxbsd.cpp` | 동등 |

번들: `thirdparty/libbacktrace/`.

⇒ **"크래시 나면 심볼 붙은 스택이 stderr에 찍힌다"까지는 이미 된다.** 로드 주소를 함께 찍는 건 ASLR 하에서 사후 심볼화를 가능케 하는 좋은 설계다. **없는 건 "그 출력을 서버로 모으는 것"뿐이다.**

### (c) 경계
| 목표 | 코어 수정 | 방법 |
|------|-----------|------|
| **크래시 리포트 자동 수집** | **0** | **Sentry Godot SDK (GDExtension)** |
| 미니덤프 생성 | 국소 코어 | Windows SEH 핸들러에 `MiniDumpWriteDump` 추가 (~30줄) |
| 심볼 서버 파이프라인 | **0 (빌드 인프라)** | CI에서 `.pdb`/`.dSYM` 아카이빙 + `sentry-cli upload-dif` |
| 프로덕션 텔레메트리 | **0** | 자작 (HTTPRequest + 이벤트 큐) 또는 Sentry 성능/로그 |

### (d) 공수
- Sentry 도입 + CI 심볼 업로드: **2~5일**.
- 자체 텔레메트리 백엔드: **1~2주**.

### (e) 현실적 대안 (웹 실조사)
- **[getsentry/sentry-godot](https://github.com/getsentry/sentry-godot)** — **공식 Sentry SDK**, **GDExtension 애드온**(엔진 재컴파일 불필요). **v1.2.0 안정**, 지원: **Windows / Linux / macOS / iOS / Android**.
  - 결정적 능력: **"Godot의 C++ 엔진 코드와 GDExtension에서 발생한 하드 크래시(segfault/access violation)를 캡처·해소"**한다. GDScript 에러뿐 아니라 **네이티브 크래시**를 잡는다는 뜻이며, 이 프로젝트처럼 **딥 코어 개조를 하는 포크에선 정확히 필요한 물건**이다.
  - 로그·유저 피드백 기능도 추가됨. 문서: [docs.sentry.io/platforms/godot](https://docs.sentry.io/platforms/godot)
- ⚠️ **포크 특이 주의:** 커스텀 엔진 빌드는 공식 심볼이 없다. **CI가 반드시 자체 빌드의 디버그 심볼(`.pdb`/`.dSYM`/split-debug)을 아카이빙하고 업로드해야** 스택이 의미를 갖는다. 이걸 안 하면 Sentry를 붙여도 주소 나열만 온다.

### (f) 판정 🟢 — **격차 아님. 도입 결정만 하면 되는 항목.**
단, **딥 코어 개조 프로젝트에선 우선순위를 올려야 한다.** 자작 렌더러 코드(§1 Nanite 등)는 필연적으로 GPU/드라이버 관련 하드 크래시를 낳고, 그때 "유저 PC에서 뭐가 터졌는지"를 모으는 수단이 없으면 디버깅이 불가능해진다. **§1 착수 전에 깔아두는 게 맞다.**

---

## 8. ✅ 셰이더 컴파일 스터터 — **격차 아님으로 확정** (다른 엔진 이식의 단골 함정이므로 명시 기록)

> 다른 엔진에서 이식할 때 거의 항상 문제가 되는 항목이라, **"확인했고 문제 없음"을 명시적으로 남기는 것 자체에 가치가 있다.**

### 실측 — 4계층 방어가 전부 갖춰져 있다

**① Ubershader (전 렌더러)** — `grep -ri "ubershader" servers/ drivers/` → **131 매치.**
| 렌더러 | 근거 |
|--------|------|
| Forward+ | `scene_shader_forward_clustered.cpp:646-647` — `for (ubershader = 0; ubershader < 2; ubershader++)` / `#define UBERSHADER` |
| Forward Mobile | `render_forward_mobile.cpp:2567-2569` — `ubershader_iterations` |
| 전역 스위치 | `disable_ubershaders` `rendering_device_commons.h:1056` |

**② 디스크 셰이더 캐시 (zstd 압축)** — `renderer_compositor_rd.cpp:320-355`
```
rendering/shader_compiler/shader_cache/{enabled, compress, use_zstd_compression, strip_debug}
```
`user://shader_cache` + `res://.godot/shader_cache` 2단(`:331-355`).

**③ 드로우 타임 폴백 — 여기가 핵심이다** — `render_forward_clustered.cpp:497-532`
```
const uint32_t ubershader_iterations = 2;
while (pipeline_key.ubershader < ubershader_iterations) {
    ...
    RSE::PipelineSource pipeline_source = pipeline_key.ubershader
        ? RSE::PIPELINE_SOURCE_DRAW : RSE::PIPELINE_SOURCE_SPECIALIZATION;
    pipeline_rd = shader->pipeline_hash_map.get_pipeline(
        pipeline_key, pipeline_hash, pipeline_key.ubershader, pipeline_source);
    ...
    pipeline_key.ubershader++;
```
⇒ **특화 PSO가 아직 컴파일 안 됐으면 그 프레임은 ubershader로 그리고 넘어간다. 절대 블록하지 않는다.** 이게 스터터 방지의 본질이며, Godot은 이걸 제대로 하고 있다.

**④ PSO 사전 워밍업** — 메시/서피스 등록 시점에 파이프라인 컴파일:
- `_mesh_compile_pipelines_for_surface(surface, global, PIPELINE_SOURCE_SURFACE)` `:4833`
- `_mesh_compile_pipelines_for_surface(..., PIPELINE_SOURCE_MESH, &pipeline_pairs)` `:5027`
- `pair.first->pipeline_hash_map.wait_for_pipeline(pair.second.hash())` `:5033`
- 어떤 변종을 미리 만들지는 프로젝트 설정/라이트 사용 현황에서 유도: `_update_global_pipeline_data_requirements_from_project()` `:4144`, `..._from_light_storage()` `:4153`. 모션벡터·멀티뷰·VoxelGI·SDFGI·라이트맵·리플렉션프로브·그림자 비트폭까지 각각 요구 플래그로 승격(`:1861-2002, :3116, :4148-4156`).

### ✅ **PSO 사전 워밍업이 충분한지 — 검증 수단까지 엔진에 있다**
브리핑이 요구한 "워밍업이 실제로 충분한지 검증"에 대한 답:

**`RENDERING_INFO_PIPELINE_COMPILATIONS_*` 퍼포먼스 모니터가 소스별로 노출돼 있다** — `rendering_server_enums.h:917-921`:
`CANVAS / MESH / SURFACE / DRAW / SPECIALIZATION`
(구현: `RendererSceneCull::get_pipeline_compilations` `renderer_scene_cull.cpp:1591`, 카운터 `scene_shader_forward_clustered.h:377`)

**해석 규칙 (이게 실전 진단법이다):**
| 모니터 | 의미 | 판정 |
|--------|------|------|
| `PIPELINE_COMPILATIONS_MESH` / `_SURFACE` | 워밍업이 정상 동작 중 | 정상. 단 **로딩 시간**으로 나타남 |
| `PIPELINE_COMPILATIONS_SPECIALIZATION` | 특화 PSO를 백그라운드 컴파일 중 | 정상 |
| **`PIPELINE_COMPILATIONS_DRAW`** | **드로우 시점에 ubershader로 폴백했다** = 워밍업이 놓친 조합 | **0이 아니면 워밍업 부족** |

⇒ **런타임에 `DRAW`가 0인지 보면 워밍업 충분성을 정량 검증할 수 있다.** 추측할 필요가 없다.

### ⚠️ 남는 리스크 두 가지 (오픈월드 특유)
1. **스터터가 사라진 게 아니라 로딩 시간으로 전가된다.** `:5033`의 `wait_for_pipeline`은 **동기 대기**다. 씬 로드 시 파이프라인 컴파일을 기다린다. 대형 오픈월드에서 이건 **초기 로딩 폭증**으로 나타날 수 있다.
2. **월드 스트리밍이 이 모델과 충돌한다.** 오픈월드는 정의상 런타임에 새 메시가 계속 등장한다 → 그때마다 `PIPELINE_SOURCE_MESH` 워밍업이 발생한다. 셀 프리로드 시점이 잘못되면 **스트리밍 히치**가 된다.
   - **대응(코어 0):** ① 월드 셀을 **실제 진입보다 한 셀 앞서** 프리로드해 워밍업을 히치 예산 밖으로 밀어냄. ② 첫 실행 시 대표 머티리얼 세트를 오프스크린으로 한 번 렌더해 캐시 워밍(`shader_cache`가 zstd로 디스크에 남으므로 2회차부터 무료). ③ `PIPELINE_COMPILATIONS_DRAW`를 QA 빌드 HUD에 상시 표시해 회귀 감시.

### 판정 ✅ **해결된 축 — 별도 작업 불요. 감시만 할 것.**
Ubershader·zstd 셰이더 캐시·논블로킹 폴백·사전 워밍업·**측정 수단**의 5종 세트가 완비돼 있다. 이는 상용 엔진과 동급이며 일부(폴백을 명시적 소스 태그로 계측하는 부분)는 오히려 앞선다.

---

## 9. 우선순위 종합 — AAA 포토리얼 목표 기준 (2026-08-18 개정)

### 무엇이 실제로 아픈가

| 순위 | 항목 | 왜 | 액션 |
|------|------|-----|------|
| **1** | **§3 GPU 오클루전 컬링** | 유일한 진짜 엔진 격차. **단 독립 과제가 아니다** | **§1 Nanite 워크스트림에 흡수.** 별도 예산 세우지 말 것 |
| **2** | **§7 크래시 수집** | 딥 코어 개조 프로젝트에서 **없으면 프로덕션 디버깅 불가** | **§1 착수 전에** Sentry + CI 심볼 업로드 (2~5일) |
| **3** | **§6 캐릭터 — 프리인테그레이티드 SSS LUT + 안면 블렌드셰이프** | AAA 포토리얼 피부의 표준 경로. 세퍼러블 SSS는 일차 근사일 뿐 | 셰이더 + LUT 에셋 (1~2주) + 블렌드셰이프 희소화 (수일) |
| **4** | **§1 모션 블러** | AAA 포토리얼에서 카메라/오브젝트 모션 블러는 **필수** — 필름 룩·움직임 가독성 | GDExtension 애드온(0일) 또는 자작(1~3주). §1 Nanite 안정화 후 |
| **5** | **§2 업스케일러 — DLSS/FSR3.1** | AAA 4K/60 유지에 필요. FSR2.2.1은 품질 열세 존재 | GDExtension DLSS(1~3개월) 또는 FSR3.1 업그레이드(2~6주). §1~3 안정화 후 |
| **6** | §4 EQS 대응물 | 오픈월드 전투 AI의 실질 병목(엄폐·포지셔닝 선택) | LimboAI 도입 후 자작 (2~4주) |
| **7** | §5 스냅샷 보간 | co-op 한다면 필수, 안 하면 0 | co-op 확정 시에만 (2~4주) |
| **8** | **§6 스트랜드 헤어/헤어 BSDF** | AAA 최종 목표(Horizon/UE Groom 급). Jolt 업스트림 대기 | P3 장기 로드맵. 당장은 카드형 헤어 + Kajiya-Kay |

### 무엇이 무시 가능한가

| 항목 | 판정 |
|------|------|
| **§4 내비게이션 코어** | 이미 이 문서에서 가장 성숙한 서브시스템 (비동기 베이킹·병렬 RVO·증분 갱신·링크·동적 장애물) |
| **§5 리플리케이션 그래프** | 4~8인 co-op엔 오버엔지니어링. per-peer 가시성·델타가 이미 있음 |
| **§5 롤백 넷코드** | 오픈월드에 **구조적으로 부적합**(결정론 요구·리시뮬 비용). 격투게임 기술 |
| **§8 셰이더 스터터** | ✅ 해결됨. 감시만 |

### 기존 문서와의 관계
- **§3은 [Nanite 성능 문서](./godot-nanite-performance-implementation-research.md) §2.2("2-pass HZB + SPD 리듀서, 코어 0, 우선순위 4위")의 중복이다.** 이 문서는 "왜 통합이 강제되는가"만 담당하고 구현 상세는 그쪽을 정본으로 삼는다.
- **§7은 [GPU 프로파일러 문서](./godot-gpu-profiler-implementation-research.md)와 구분된다** — 그쪽은 *개발 시점* GPU 타이밍, 이쪽은 *프로덕션* 크래시/텔레메트리다. 겹치지 않는다.
- **§2 DLSS의 "코드가 아니라 라이선스가 막는다"는 [콘솔 export 문서](./godot-console-export-research.md) §4와 동형 구조다** — 해법도 같다(클로즈드 배포 레이어 분리).

### 최종 판단 (2026-08-18 AAA 개정)
**이 8개 축을 실측한 결과, 엔진 레벨 신규 격차는 사실상 0개다.**
- 1개(§3)는 이미 계획된 §1 워크스트림의 부분집합,
- 1개(§8)는 이미 해결됨,
- 나머지 6개는 GDExtension/애드온/셰이더 저작으로 닫힌다.

**AAA 목표로 전환 시 핵심 변화:**
- **이전 결론("코어 개조 인력을 배분하는 것은 오배분")은 AAA에서도 유지된다.** — 엔진 격차가 없기 때문이다. 달라진 것은 **우선순위 순서**다.
- AAA에서는 모션 블러(§1), DLSS/FSR3.1(§2), 프리인테그레이티드 SSS LUT(§6), 스트랜드 헤어(§6 장기)가 중요해지고, 이들은 **엔진 개조가 아니라 셰이더/에셋/GDExtension 작업**이다.
- **"오배분" 판단은 유지** — 단 "엔진 개조 예산"을 §1~3에 집중하고, 위 항목들은 **별도 아트·셰이더·GDExtension 예산으로** 처리한다. 이전보다 **총 작업량이 증가**했지만(AAA 충실도 요구), **코어 개조가 필요한 부분은 여전히 §1~3뿐**이다.

⇒ **엔진 개조 예산은 §1~3(Nanite·GI·VT)에 집중하는 것이 옳다.** 유일한 예외는 **§7 크래시 수집이며, 이건 개조가 아니라 도입이고 §1보다 먼저 해야 한다** — 자작 렌더러의 하드 크래시를 필드에서 수집할 수단 없이 §1에 착수하는 것은 무모하다.

---

*리서치 방법: Godot 4.8-dev(`eda2a482e9`) 소스트리 grep/read 실측 + 업스트림 proposal/PR·애드온 웹 실조사 교차검증. 브리핑이 준 전제 중 **2건을 적대적 재검증으로 반증**했다 — (1) "HiZ/뎁스 피라미드 부재" → CPU HZB(`renderer_scene_occlusion_cull.h:45`)와 GPU Hi-Z 밉체인(`ss_effects.cpp:1458`, `screen_space_reflection_hiz.glsl`) 양쪽 실재, (2) "관심영역 부재" → per-peer 가시성(`multiplayer_synchronizer.h:60`)·델타 동기화(`multiplayer_synchronizer.cpp:407`) 실재. 미해결: LimboAI GDExtension 빌드의 구체적 기능 제한 항목(공식 문서가 "somewhat limited"라고만 기재) — 채택 전 스파이크 필요.*

## 10. 2026-08-18 개정 — AAA 기준 재채점

### 델타 표: 뒤집힌 판정

| 항목 | 기존 판정 | AAA 판정 | 뒤집힌 이유 |
|------|----------|----------|------------|
| **§1 모션 블러** | 🟢 "스타일라이즈드 목표에서 우선순위 낮음" (원신은 안 씀) | 🟢 유지, **단 P0→P1 격상** (AAA 필름 룩 필수) | AAA 포토리얼에서 카메라/오브젝트 모션 블러는 필수 요소. 애드온 경로로 해결 가능하므로 신호는 유지, 우선순위 상승 |
| **§2 업스케일러** | 🟡 "우선순위 낮음" (스타일라이즈드는 업스케일러 의존도 낮음) | 🟡 유지, **단 AAA 로드맵 P1~P2** (4K/60 유지) | FSR2.2.1 품질 열세가 AAA 포토리얼에서 더 눈에 띔. DLSS GDExtension 또는 FSR3.1 업그레이드가 §1~3 안정화 후 필요 |
| **§6 캐릭터 — SSS** | 🟢 "프리인테그레이티드 SSS LUT 불필요(스타일라이즈드)" | 🟡 전체 격상 (프리인테그레이티드 LUT 필요) | AAA 포토리얼 피부는 세퍼러블 SSS만으로 부족. Penner 2011 LUT가 표준이며 셰이더+LUT 에셋 문제(엔진 격차 아님) |
| **§6 캐릭터 — 스트랜드 헤어** | 🔴 "불필요 — 스타일라이즈드 타깃" | 🔴→🟡 P3 장기 로드맵 (AAA 최종 목표) | Horizon/UE Groom 급 스트랜드 헤어는 AAA 캐릭터의 정점. Jolt `Physics/Hair` 업스트림 대기 + 커스텀 래스터라이저 필요. 당장은 카드형 헤어로 프로토타입 |
| **§6 캐릭터 — 종합 신호** | 🟢 | 🟡 | 위 SSS+헤어 변경으로 인해 전체 판정 신호 변경 |
| **§9 우선순위** | 원신급 스타일라이즈드 기준 | AAA 포토리얼 기준으로 재정렬 | 모션 블러·DLSS·SSS LUT·스트랜드 헤어가 순위 진입, 전체 순위 재조정 |
| **결론 "오배분"** | "코어 개조 인력 배분은 오배분" | **유지** (AAA에서도 유효) | 엔진 격차가 여전히 0이므로 코어 개조 불필요 판단은 유지. 단 AAA 충실도를 위한 **셰이더/에셋/GDExtension 작업량이 증가** |

### 유지된 판정 (AAA에서도 변함없음)

| 항목 | 판정 | 이유 |
|------|------|------|
| §3 GPU 오클루전 컬링 | 🟡 (§1 부분집합) | AAA에서도 독립 격차 아님. Nanite 통합이 정답 |
| §4 AI/Nav | 🟢 | LimboAI + 자작 EQS. 내비게이션 성숙 |
| §5 네트워킹 | 🟡 | 4~8인 co-op 한정. MMO는 자작 |
| §7 크래시/텔레메트리 | 🟢 | Sentry GDExtension + CI 심볼 |
| §8 셰이더 스터터 | ✅ 해결됨 | 5종 방어 완비 |

### AAA 마스터 요약표 행

| # | 격차 | AAA 신호 | AAA 한 줄 |
|---|------|---------|-----------|
| 1 | 모션 블러 | 🟢 | GDExtension 애드온으로 해결. AAA 우선순위 P1 (필름 룩 필수) |
| 2 | 업스케일러 | 🟡 | DLSS GDExtension 또는 FSR3.1 업그레이드. §1~3 안정화 후 P1~P2 |
| 3 | GPU 오클루전 컬링 | 🟡 | §1 Nanite 부분집합. 독립 과제 금지 |
| 4 | AI | 🟢 | LimboAI + 자작 EQS |
| 5 | 네트워킹 | 🟡 | 4~8인 co-op 충분. MMO는 자작 |
| 6 | 캐릭터 표현 | 🟡 | 카드형 헤어+세퍼러블 SSS로 최소선 충족. 프리인테그레이티드 SSS LUT(P1), 스트랜드 헤어(P3 장기) |
| 7 | 크래시/텔레메트리 | 🟢 | Sentry GDExtension. §1 착수 전 필수 |
| 8 | 셰이더 스터터 | ✅ | 해결됨. 감시만 |
