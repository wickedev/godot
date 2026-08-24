# Godot에서 Lumen급 동적 GI 구현 — 딥리서치

> `godot-openworld-engine-gaps.md` §2("Lumen급 동적 GI ❌ 불가")를 심층 재평가한 문서.
> `godot-nanite-implementation-research.md`와 동일한 방법론: 웹 1차 소스 fan-out → adversarial 검증(주장별 3표, 2/3 반박 시 폐기) → 로컬 코드베이스 교차검증.
>
> **검증 통계**: 6개 각도, 23개 소스 페치, 103개 주장 추출, 25개 검증 → **24 confirmed / 1 refuted**. 로컬 저장소(`servers/rendering/`) 교차검증 포함.

---

## TL;DR — 결론부터

**"불가능"이 아니라 "명확한 제약과 함께 기술적으로 가능"하다.** 핵심 반전 세 가지:

1. **Lumen의 기본 경로는 하드웨어 레이트레이싱이 아니다.** Mesh/Global Signed Distance Field에 대한 **소프트웨어 레이트레이싱**이며, GTX-1070/SM6급 비-RT GPU에서 동작한다. 이는 Godot SDFGI가 이미 쓰는 SDF 트레이싱과 **동일한 알고리즘 계열**이다. → 격차가 gaps 문서가 암시하는 것만큼 근본적이지 않다.

2. **Godot의 하드웨어 레이트레이싱 토대가 이미 엔진에 착지했다.** PR **GH-99119**(Antonio "Fahien" Caggiano)로 Vulkan RT 배관(BLAS/TLAS, ray dispatch, RenderingDevice API)이 4.7 dev 1에 병합됐다. **로컬 master 브랜치에서 직접 확인** — `blas_create()` / `tlas_create()` / `raytracing_pipeline_create()`가 실재하나 전부 `experimental` 플래그.

3. **완전한 Lumen 동등성은 여전히 HW-RT에 게이팅된다.** 샤프한 미러 반사와 스킨드-메시(동적 캐릭터) GI 기여는 소프트웨어-SDF-only 경로로는 불가능하다. 소프트 확산 GI는 SDFGI 개선/커스텀 SDF 트레이싱으로 도달 가능하되, 반사/완전 동적은 RT 파이프라인 완성에 달려 있다.

**AAA 기준(2026-08-18 개정):** 소프트 확산 GI는 SDFGI 개선/커스텀 SDF 트레이싱으로 도달 가능하나, **미러 반사·스킨드 메시(동적 캐릭터) GI 기여는 AAA의 필수 항목이며 HW-RT 경로(c)를 1급 목표로 삼아야 한다.** SW-SDF 경로(b)는 (c)의 준비 단계로 재배치 — (b)의 Surface Cache와 radiance cache는 (c)의 아키텍처와 공유 인프라다. 사전 베이크+블렌딩 대안을 택하면 무엇을 포기하는지 §5.1에 명시.

---

## 1. Lumen은 실제로 어떻게 동작하는가 (아키텍처 분해)

Epic 1차 문서 기준. 각 구성요소가 요구하는 GPU 기능을 함께 표기.

### 1.1 두 개의 트레이싱 경로

| 경로 | 트레이싱 대상 | GPU 요구 | 특성 |
|------|--------------|----------|------|
| **Software Ray Tracing** (기본) | SDF: 첫 ~2m는 per-mesh Distance Field(Detail Tracing), 나머지는 병합된 단일 **Global Distance Field**(Global Tracing) | DX12 + **SM6**, ~GTX-1070. HW-RT **불필요** | 인스턴스 수·중첩과 **무관하게 비용 일정**. Lumen 내 최속. 비-RT GPU 폴백. Fortnite가 이 경로로 출하. |
| **Hardware Ray Tracing** (옵션) | 실제 삼각형 BVH(TLAS/BLAS) | RTX-2000+/RX 6000+, Vulkan `ray_tracing_pipeline`/`ray_query` | 더 정확·더 비쌈. **스킨드 메시 포함** 더 많은 지오메트리 타입. **샤프한 미러 반사에 필수.** 매 프레임 **TLAS 재구축**. 컬링 후 <100,000 인스턴스 권장. |

> **핵심**: 소프트웨어 경로(기본)가 Godot SDFGI의 SDF 스피어-트레이싱과 **직접적인 아키텍처 유사물**이다. 단 Lumen은 per-mesh SDF + merged Global SDF를 쓰고, SDFGI는 씬을 동적 복셀화한 글로벌 SDF 캐스케이드를 쓴다 — "동일 계열"이되 "정확한 동형"은 아니다.

### 1.2 Surface Cache — 셰이딩 분리 (성능의 핵심)

- 각 메시 표면의 material 속성을 여러 방향에서 저해상도 아틀라스 **'Cards'**로 캡처.
- 그 표면에 조명(직접광 + 멀티바운스 GI + 그림자 스카이라이트)을 미리 계산 → **레이 히트 지점에서 material을 매번 평가하는 대신 캐시를 조회**.
- 기본 활성. **콘텐츠 제약**: "내부가 단순한(simple interiors)" 메시만 지원 — 벽/바닥/천장은 분리해야 하고, 가구까지 포함한 방 통짜 메시는 미지원.

### 1.3 Final Gather — 노이즈 저감 기계 (Radiance Caching, SIGGRAPH 2021)

확산 GI의 final gather는 3중 캐싱 + 시간적 기법:
- **Screen-space radiance cache**: GI를 크게 다운샘플.
- **World-space radiance cache**: 원거리 조명용.
- **이전 프레임 조명의 importance sampling**: 스크린-공간 디노이저보다 효과적.

> Godot 동등물을 만들려면 이 노이즈-저감 스택(스크린/월드 radiance cache + 시간적 importance sampling)을 **반드시** 갖춰야 한다. SDFGI가 프로브를 쓰지만 Lumen 수준의 스크린+월드 이중 캐시는 아니다.

### 1.4 캐시 기반의 대가 — 응답 지연

캐시 아키텍처라 **전역 조명 변화(예: 태양 끄기)는 완전 전파에 수 초**가 걸린다. 국소 변화는 빠르게 전파. → 낮/밤 사이클처럼 **느린 전역 변화에는 잘 맞고**, 급격한 라이트 스위칭에는 지연이 보인다. (SDFGI의 25프레임 수렴 지연과 성격이 같은 트레이드오프.)

---

## 2. Godot의 현재 상한선 — SDFGI (정확한 격차)

Godot 공식 docs/블로그 1차 확증.

**SDFGI란**: SDF를 레이-마칭하는 **semi-real-time**(완전 실시간 아님) GI. RT 하드웨어 불요. 각 캐스케이드가 두 배씩 커지는 **캐스케이드 복셀-그리드**(로컬 코드 확인: `MAX_CASCADES 8`) → 임의 월드 크기·절차적 레벨·대형 오픈월드 뷰 거리 지원.

**Lumen 대비 격차(전부 3-0 만장일치 검증)**:

| 항목 | SDFGI | Lumen |
|------|-------|-------|
| 동적 라이트 | ✅ 지원 | ✅ |
| 동적 **오클루더** | ❌ 미지원 | ✅ (HW-RT) |
| 동적 **이미시브** | ❌ 미지원 | ✅ |
| 응답 지연 | ~25프레임 수렴, 가시적 전이 | 국소 빠름/전역 수 초 |
| 움직이는 정적/베이크 지오메트리(문 등) | 카메라가 멀어질 때까지 **GI 잘못 표시** | 실시간 반응 |
| 동적 오브젝트의 GI **기여** | ❌ 광을 **받기만**, 씬에 바운스 못 함 | ✅ (매 프레임 재복셀화 비용 감수) |
| 반사 | 러프 반사 근사(SDF 트레이스) | 러프 + **샤프 미러**(HW-RT) |

> **로컬 코드 교차검증** (`servers/rendering/renderer_rd/environment/gi.h`): `DynamicMap` / `has_dynamic_object_data`로 동적 오브젝트 처리 경로가 존재하나, 정적 SDF 대비 제한적. `use_two_bounces` / `bounce_feedback`로 2-bounce 피드백 GI. ping-pong 캐스케이드 처리 → 수렴에 여러 프레임(응답 지연의 근원).

**VoxelGI**: 품질 좋으나 씬 크기 제약(오픈월드 부적합). **LightmapGI**: 정적 베이크, 시간대 변화 불가. → **SDFGI가 Lumen 소프트웨어-RT 티어와 알고리즘적으로 가장 가깝고, 재사용의 출발점**이다.

---

## 3. Godot 하드웨어 레이트레이싱 지원 현황 — "기초 배관 착지" 단계

이 문서의 **가장 큰 상태 변화**. gaps 문서 §2가 쓰인 이후 엔진이 움직였다.

### 3.1 공식 현황 (검증 3-0)

- **PR GH-99119** (Antonio Caggiano) — Vulkan RT 토대가 **4.7 dev 1에 병합**. BLAS/TLAS 가속 구조 + ray dispatch + 저수준 RenderingDevice API 노출, **GDScript에서 접근 가능**(데모 프로젝트 포함: `github.com/Fahien/godot-raytracing-gdscript-demo`).
- 그러나 **초기 인프라일 뿐 완성 기능이 아니다**. 프로덕션 RT 그림자/반사/패스트레이서는 **아직 없고**, "레이를 쏠 수 있는 능력"만 착지. 완전 RT는 **장기 목표**로 프레이밍.
- 독립 확증: Phoronix "Godot 4.7 Making Progress On Vulkan Ray-Tracing", 저자 Vulkanised 2026 / GodotCon 2026 강연.

### 3.2 로컬 master 브랜치 직접 확인 (본 세션 grep)

공식 블로그 주장을 **코드로 교차검증**했다 — RenderingDevice에 RT API가 **실재**:

```
servers/rendering/rendering_device.h:
  RID blas_create(Span<AccelerationStructureGeometry>, BitField<...Flags>)
  RID tlas_create(uint32_t max_instance_count, BitField<...Flags>)
  RID raytracing_pipeline_create(Span raygen, Span miss, Span hit_groups, uint32_t max_recursion)
  _raytracing_pipeline_create_sbt_buffer(...)   // Shader Binding Table
  RID_Owner<AccelerationStructure> acceleration_structure_owner

servers/rendering/rendering_device_commons.h:
  SHADER_STAGE_RAYGEN / ANY_HIT / CLOSEST_HIT / MISS / INTERSECTION
  UNIFORM_TYPE_ACCELERATION_STRUCTURE  // "TLAS+BLAS, for raytracing only"
  enum AccelerationStructureType { BOTTOM_LEVEL, TOP_LEVEL }

drivers/vulkan/rendering_device_driver_vulkan.cpp  // Vulkan 구현 실재
```

관련 커밋 히스토리(로컬): `27e4f24800 raytracing: Initial Vulkan support` → `04a2ae7ed4 raytracing-base` → `83d2b84a51 Refactor raytracing pipelines` → **`b6ae515dab Mark all raytracing functionality experimental`** (2026-04, PR #118377).

> **결론**: gaps 문서 §2의 "GI는 렌더 패스 알고리즘, 스크립트로 못 얹는다"는 여전히 옳다. 하지만 **엔진 레벨에서는 HW-RT 기반(경로 c의 최대 게이팅 요소)이 이미 깔리는 중**이다. 남은 것은 "레이를 쏘는 능력" 위에 **GI/반사 렌더 패스를 얹는 작업**.

---

## 4. 참조 구현 / 선행 사례

| 사례 | 무엇 | Godot 경로에 주는 것 |
|------|------|---------------------|
| **Bevy Solari** (`jms55.github.io`, 0.17→0.18) | 프로덕션 오픈소스 엔진이 **실시간 HW-RT 확산 GI+DI** 출하. ReSTIR GI + world-space radiance cache 파이프라인이 Lumen 분해와 매핑. | 오픈소스 RT GI **참조 아키텍처**. Godot HW-RT 경로(c)가 그대로 참고할 설계 패턴(ReSTIR, world cache). **2026-08-18 업데이트:** Solari는 Bevy에 정식 병합되어 0.17/0.18 릴리스에 포함 — 실사용 검증된 프로덕션 참조다. |
| **제안 GH-6033** (2023-01, `godot-proposals`) | "raytraced dynamic lightmaps to achieve lighting like UE5 Lumen." 이전 프레임 동적 라이트맵을 광원으로 되먹여 바운스 GI를 시간 누적(사실상 무한 바운스). | **not_planned로 종료**. 저자 주장(거리 독립·광 누수 없음·SDFGI보다 빠름)은 검증된 엔진 동작이 아닌 **논증**. RT 하드웨어 + 라이트맵 UV 전제. |
| **radiance-cascades-godot** (`Sohojoe`) | Alexander Sannikov의 Radiance Cascades 기법 Godot 실험 구현. | 경로 (d)의 실증 시드. 단 성능/품질 특성·SDFGI 대비 개선 여부는 **미검증**(open question). |
| **제안 GH-5162 / 이슈 GH-4870** | Ray tracing support 커뮤니티 논의 스레드. | 메인테이너/커뮤니티의 RT 노출 포지션 인용원. |

> **주의(검증 반영)**: Bevy solari의 재사용 패턴, radiance cascades의 Godot 적용 실측, 64-bit atomics 등 RenderingDevice 세부 요구는 **원본 리서치 클레임 세트에 실측 근거가 없어** open questions로 남긴다(§7).

---

## 5. 구현 경로 옵션 — 난이도 / 실현 가능성

목표 재정의: **"낮/밤 변화·동적 지오메트리에 실시간 반응하는 소프트 GI + 반사"**.

### (a) SDFGI 개선·확장 — 🟢 가장 현실적, 낮은 위험

- **무엇**: 기존 SDFGI C++/GLSL(캐스케이드 SDF 생성, 프로브, 2-bounce)을 재사용, 약점(응답 지연, 동적 오클루더 미지원, 동적 오브젝트 기여 불가)을 겨냥해 개선.
- **게이팅**: 신규 하드웨어 기능 **불요**. 순수 렌더 백엔드 알고리즘 작업.
- **한계**: 미러 반사·완전 동적 캐릭터 GI는 이 경로로 **도달 불가**(구조적). 원신급 "소프트 GI"에는 충분할 수 있음.
- **난이도**: 중. 기존 코드 위에 증분.

### (b) 소프트웨어-SDF 트레이싱 커스텀 GI — 🟢 (c)의 준비 단계

- **무엇**: Lumen 기본 경로(SDF 소프트웨어 RT) + Surface Cache + Screen/World radiance cache + 시간적 importance sampling을 Godot에 신규 구축. SDFGI를 넘어서는 노이즈 저감·멀티바운스.
- **게이팅**: HW-RT **불요**(GTX-1070/SM6 티어에서 동작). SDFGI 인프라 부분 재사용 가능.
- **난이도**: 상. Surface Cache(Card 캡처/아틀라스)와 3중 radiance cache가 큰 신규 서브시스템.
- **한계**: 소프트웨어 경로의 근본 한계 상속 — 스킨드 메시 DF 미지원, WPO 미지원, 미러 반사 불가.

> ⚠️ **2026-08-18 개정 — (b)와 (c)의 관계 재정립.** (b)는 (c)의 독립 경로가 아니라 **준비 단계**다. Surface Cache와 radiance cache(스크린/월드)는 SW-SDF 트레이싱과 HW-RT 모두에서 셰이딩 분리·노이즈 저감을 담당하는 공유 인프라다. (b)에서 이 인프라를 먼저 구축하고, (c)에서 트레이싱 백엔드를 SDF→BVH로 교체한다. (b)를 생략하고 (c)로 직행하면 Surface Cache·radiance cache 설계를 HW-RT 환경에서 처음부터 해야 해 리스크가 증폭된다.

### (c) Vulkan HW-RT 노출 완성 후 RT GI/반사 — 🔴 **1급 목표 (AAA 필수 게이팅)**

- **무엇**: GH-99119가 깐 BLAS/TLAS·ray dispatch 위에 **프로덕션 RT GI + 샤프 미러 반사** 렌더 패스 구현. Bevy Solari/ReSTIR GI가 참조.
- **게이팅**: (1) 엔진 RT API가 아직 **experimental** — 프로덕션 RT 그림자/반사/GI **미제공**. (2) GPU 요구 상승(RTX-2000+/RX 6000+). (3) 매 프레임 TLAS 재구축 스케줄링, ray tracing pipeline/SBT 성숙 필요.
- **난이도**: 최상. 엔진 코어 팀 협업 or 상당한 C++ 렌더러 작업.
- **보상**: 미러 반사·동적 캐릭터 GI 등 **완전한 Lumen 동등성**은 이 경로에서만. AAA 목표에서 소프트 GI만으로는 실격 — HW-RT 미러 반사·스킨드 메시 GI 기여가 필수.

> ⚠️ **2026-08-18 개정 — AAA에서 (c)는 장기 병행이 아니라 1급 목표.** GH-99119의 `experimental` RT API를 실사용 가능하게 프로덕션화하는 것이 최우선 게이팅 과제. (b)는 (c)의 준비 단계로 재배치.

### (d) Radiance Cascades (2D/3D, Sannikov) — 🟡 유망하나 미검증

- **무엇**: Radiance Cascades로 노이즈·응답 지연 개선 시도. `radiance-cascades-godot` 선행 실험 존재.
- **게이팅**: 3D 확장의 메모리/성능 특성이 오픈월드에서 검증 안 됨. Godot 적용 실측 부재.
- **난이도**: 중~상. 연구성 위험.

**추천 우선순위 (AAA 기준, 2026-08-18 개정):** (c)를 **1급 목표**로 — GH-99119의 `experimental` RT API를 실사용 가능하게 프로덕션화하는 것이 최우선 게이팅. (a)→(b)는 (c)의 **준비 단계**로 재배치 — Surface Cache와 radiance cache는 SW/HW-RT 양 경로가 공유하는 인프라다. (d)는 (a)/(b)/(c)의 노이즈 저감 보조 실험으로.

---

## 6. 요구되는 엔진 레벨 변경 (구체성)

**이미 있는 것(로컬 확인)**: BLAS/TLAS 생성, ray tracing pipeline 생성, SBT 버퍼, RT 셰이더 스테이지 enum, `UNIFORM_TYPE_ACCELERATION_STRUCTURE`, Vulkan 드라이버 구현. → **경로 (c)의 최대 진입장벽이 대부분 착지**.

**아직 필요한 것 (경로별)**:
- **경로 (a)/(b)** — RenderingDevice 신규 하드웨어 기능 **불요**. 순수 컴퓨트 셰이더 + 기존 SDFGI 자원 재사용. Surface Cache 아틀라스 관리, radiance cache 볼륨/텍스처, 시간적 재투영이 핵심 신규 코드.
- **경로 (c)** — experimental RT API의 **프로덕션화**: 매 프레임 TLAS 재구축 스케줄링을 렌더 그래프에 통합, SBT 정렬/디바이스 주소 안정화(진행 커밋 존재: `raytracing: Fix SBT record alignment`, `Add device address flag to SBT buffer`), denoiser, ReSTIR류 샘플링.

**64-bit atomics**: Nanite 리서치에서 SW 래스터라이저의 게이팅 요소로 지목됐으나, **Lumen급 GI 경로에서는 필수 게이팅 요소가 아니다**(SDF 트레이싱·radiance cache는 표준 이미지/버퍼 연산). RT 경로는 acceleration structure가 원자 연산을 하드웨어가 처리.

**기존 SDFGI 코드 재사용성**: 캐스케이드 SDF 생성·프로브·2-bounce 피드백은 (a)/(b)에서 **직접 재사용 가능**. 정확히 얼마나 재사용되고 동적 오클루더/이미시브 기여를 어떻게 추가할지는 세부 설계 필요(open question §7).

---

## 7. 랭크된 Findings (검증 점수 포함)

신뢰도·검증 투표 순.

| # | Finding | 신뢰도 | 투표 |
|---|---------|--------|------|
| 1 | Lumen 기본 경로 = SDF 소프트웨어 RT(per-mesh DF 첫 2m + merged Global SDF), 인스턴스 무관 비용 일정, 비-RT GPU 폴백. Godot SDFGI와 동일 계열. | high | 3-0 |
| 2 | Lumen 소프트웨어 경로는 HW-RT 불요, DX12+SM6(~GTX-1070)에서 동작 → **Lumen급 동적 GI는 비-RT GPU에서 달성 가능**. | high | 3-0 |
| 3 | Lumen HW-RT는 옵션 가속 경로(더 정확/비쌈), 스킨드 메시 지원, **미러 반사 필수**, 매 프레임 TLAS 재구축, <100k 인스턴스. → 소프트웨어-only로는 미러 반사 불가. | high | 3-0 |
| 4 | Lumen은 캐시 기반: Surface Cache(Card 아틀라스로 셰이딩 분리) + Radiance Cache(스크린/월드) + 시간적 importance sampling. 전역 변화는 전파에 수 초. | high | 3-0 |
| 5 | Godot 상한선 = SDFGI(semi-real-time). 동적 라이트 O, 동적 오클루더/이미시브 X, ~25프레임 수렴, 동적 오브젝트는 광을 받기만·기여 못 함. | high | 3-0 |
| 6 | 선행 제안 GH-6033("Lumen 유사 raytraced dynamic lightmaps")은 **not_planned 종료**. 저자 주장은 논증이지 검증된 동작 아님. | high | 3-0 |
| 7 | Godot HW-RT는 **기초 배관 단계**(4.7 dev 1, GH-99119): BLAS/TLAS·ray dispatch·RenderingDevice API 착지, GDScript 접근 가능. 단 프로덕션 RT GI/반사는 미제공(experimental). | high | 3-0 |

**Refuted (0-3 반박)**: "Lumen 유사 시스템이 RenderingDevice RT API + 라이트맵 zero-copy 전송에만 게이팅된다" — **단일 요인으로 과도 축소**한 오류. GI 시스템은 Surface Cache, radiance cache, denoiser 등 다수 서브시스템을 요구.

---

## 8. Open Questions (실측 근거 부재 — 후속 조사 필요)

1. **Radiance Cascades(Sannikov) 2D/3D의 Godot 적용 실증** — 성능/품질, SDFGI 대비 노이즈·응답 지연 실제 개선 여부? (`radiance-cascades-godot` 벤치 필요)
2. **Bevy Solari의 재사용 가능 설계 패턴** — ReSTIR GI + world radiance cache가 Godot HW-RT 경로에 어떻게 이식되나?
3. **경로 (b) SDFGI 코드 재사용률** — 캐스케이드 SDF/프로브를 얼마나 재사용하고, 동적 오클루더/이미시브 기여를 더하려면 어떤 렌더 백엔드 변경이 필요한가?
4. **경로 (c) RenderingDevice 잔여 요구** — GH-99119 위에 ray tracing pipeline 성숙, SBT, denoiser, 매 프레임 TLAS 재구축 스케줄링 중 무엇이 미비하고 로드맵 우선순위는?

---

## 9. gaps 문서 §2 갱신 제안

현재 gaps 문서는 "Lumen급 동적 GI ❌ 불가 / 현재 상한은 SDFGI"로 단정한다. 본 리서치에 근거한 재기술 제안:

> **§2 재평가**: "❌ 불가"가 아니라 **"⚠️ 제약과 함께 부분 가능, HW-RT 완성에 게이팅"**.
> - Lumen 기본 경로가 SDF 소프트웨어 RT라는 점에서 SDFGI와 **동일 계열** — 소프트 확산 GI는 SDFGI 개선/커스텀 SDF 트레이싱(경로 a/b)으로 **엔진 신규 하드웨어 기능 없이** 접근 가능.
> - Godot HW-RT 토대가 **이미 착지**(GH-99119, 4.7 dev 1, experimental) — 미러 반사·완전 동적 캐릭터 GI(경로 c)는 이 RT의 프로덕션화에 게이팅.
> - **원신급 목표엔 소프트 GI로 충분** — 원신 자체가 미러 반사·완전 동적 GI를 쓰지 않는 사전계산+소프트 GI 조합.

> ⚠️ **2026-08-18 개정 — 위 문단은 폐기.** AAA 기준(UE5.4+ / Horizon / CP2077 RT Overdrive)에서 "소프트 GI로 충분"은 성립하지 않는다. 미러 반사·스킨드 메시 GI 기여가 필수이며, HW-RT 경로(c)를 1급 목표로 삼아야 한다. 원신은 하한 참조점일 뿐 목표가 아니다. 정확한 재기술은 아래 §9 개정판 참조.

### 5.1 "현실적 대안"의 충실도 비용 (2026-08-18 추가)

사전 베이크+블렌딩(기존 Godot LightmapGI + SDFGI 혼합)을 택하면 무엇을 포기하는가:

| 포기 대상 | AAA에서의 중요도 | 대체 불가능성 |
|-----------|-----------|------|
| **동적 오클루더 GI 반응** | 🔴 필수 | 문이 열리면 그 뒤 공간의 간접광이 변해야 한다. 베이크는 불가 |
| **동적 이미시브의 GI 기여** | 🔴 필수 | 폭발·마법 이펙트·네온사인이 주변을 비춰야 한다. 베이크는 불가 |
| **미러 반사** | 🔴 필수 | 젖은 아스팔트·금속·유리에서의 샤프 반사. Screen-space reflection만으로는 occlusion edge에서 파열 |
| **스킨드 메시(캐릭터) GI 기여** | 🔴 필수 | 캐릭터가 벽 옆에 서면 벽에 바운스된 빛이 캐릭터 쪽으로 반사돼야 한다. 베이크는 불가 |
| **낮/밤 사이클의 자연스러운 전이** | 🟡 중요 | LightmapGI로는 시간대 1개만 베이크 가능. 여러 시간대 베이크+블렌드는 전이 시점에 심 발생 |
| **절차적/파괴 가능 지오메트리** | 🟡 중요 | 베이크는 정적 지오메트리만. 실시간 GI 없이는 파괴 후 잔해의 간접광이 어색 |

**결론:** 사전 베이크+블렌딩은 "원신급 스타일라이즈드"에선 유효한 대안이었으나, **AAA(UE5.4+ / Horizon / CP2077)에서는 위 6개 항목 중 4개가 필수(🔴)이며 전부 실시간 GI를 요구한다.** 따라서 이 대안은 AAA에서 **fallback이 아니라 불가**다.

---

## 부록 — 소스 (품질 등급)

**1차(primary)**:
- Epic: [Lumen Technical Details](https://dev.epicgames.com/documentation/unreal-engine/lumen-technical-details-in-unreal-engine?lang=en-US) · [Lumen GI & Reflections](https://dev.epicgames.com/documentation/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine) · [Lumen 발표 tech blog](https://www.unrealengine.com/en-US/tech-blog/unreal-engine-5-goes-all-in-on-dynamic-global-illumination-with-lumen) · [Lumen Performance Guide](https://dev.epicgames.com/documentation/unreal-engine/lumen-performance-guide-for-unreal-engine)
- Godot: [SDFGI docs](https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/using_sdfgi.html) · [SDFGI 발표 블로그](https://godotengine.org/article/godot-40-gets-sdf-based-real-time-global-illumination/) · [4.7 dev 1 스냅샷](https://godotengine.org/article/dev-snapshot-godot-4-7-dev-1/) · [rendering_device.h](https://github.com/godotengine/godot/blob/master/servers/rendering/rendering_device.h)
- GitHub: [제안 #6033](https://github.com/godotengine/godot-proposals/issues/6033) · [Fahien RT 통합 글](https://www.antoniocaggiano.eu/posts/integrating-vulkan-ray-tracing-in-godot/) · [RT GDScript 데모](https://github.com/Fahien/godot-raytracing-gdscript-demo)
- Bevy Solari: [0.17](https://jms55.github.io/posts/2025-09-20-solari-bevy-0-17/) · [0.18](https://jms55.github.io/posts/2025-12-27-solari-bevy-0-18/)
- [radiance-cascades-godot](https://github.com/Sohojoe/radiance-cascades-godot)

**2차/보조**: [Phoronix 4.7 RT](https://www.phoronix.com/news/Godot-4.7-Dev-1-Vulkan-RT) · [제안 #5162 RT 논의](https://github.com/godotengine/godot-proposals/discussions/5162) · [Lumen 대규모 오픈월드 최적화 블로그](https://www.strayspark.studio/blog/lumen-optimization-large-open-worlds-ue5-2026)

**로컬 교차검증**: `servers/rendering/renderer_rd/environment/gi.{h,cpp}`, `.../shaders/environment/sdfgi_*.glsl`, `servers/rendering/rendering_device{,_commons}.h`, `drivers/vulkan/rendering_device_driver_vulkan.cpp` (본 세션 grep, master 브랜치).

---

## 2026-08-18 개정 — AAA 기준 재채점

> 전제 변경: 원신급 스타일라이즈드에서 AAA(UE5.4+ / Horizon / CP2077 RT Overdrive)로 목표 상향. 자매 문서 [godot-nanite-implementation-research.md §10](./godot-nanite-implementation-research.md)이 확정한 포크+업스트림 디스커넥트+full-Nanite 커밋을 전제로 한다.

### 경로 재정립 — (b)는 (c)의 준비 단계

| 경로 | 기존 위상 (스타일라이즈드) | AAA 재배치 | 근거 |
|------|------|------|------|
| **(a) SDFGI 개선** | "가장 현실적, 낮은 위험" | **1단계.** (b)의 SDF 인프라 선검증. 단독 종착점 아님 | 증분 개선으로는 미러 반사·스킨드 메시 도달 불가 |
| **(b) SW-SDF 커스텀 GI** | "소프트 GI 목표로 우선" | **2단계 — (c)의 준비.** Surface Cache + radiance cache는 (c)와 공유 인프라 | (b)에서 공유 인프라 구축 → (c)에서 트레이싱 백엔드 교체(SDF→BVH) |
| **(c) HW-RT GI/반사** | "장기 트랙으로 병행" | **🔴 목표 단계.** 1급 게이팅 | AAA에서 미러 반사·스킨드 메시 GI 기여가 필수 |
| **(d) Radiance Cascades** | "유망하나 미검증" | **보조.** (a)/(b)/(c)의 노이즈 저감 실험 | 변경 없음 |

### 뒤집힌 판정

| 항목 | 기존 결론 | AAA 재채점 | 이유 |
|------|-----------|-----------|------|
| **"원신급엔 소프트 GI로 충분"** | TL;DR + §9의 핵심 논거 | **완전 폐기.** AAA에서 미러 반사·스킨드 메시 GI 기여가 필수 | Horizon/CP2077은 미러 반사 없이 성립 불가. 원신은 하한 참조점 |
| **HW-RT 프로덕션화** | "장기 병행, 엔진 RT 성숙에 편승" | **🔴 1급 목표.** GH-99119 `experimental` API를 실사용 가능하게 직접 밀어야 함 | 포크 전제에서 "편승"은 전략적 낭비. 코어 RT API를 직접 개조 가능 |
| **미러 반사** | "경로 (c)에서만, 소프트웨어-only로는 불가" (기술적 사실) | **AAA 필수 게이팅으로 재프레이밍.** "불가능해도 괜찮다" → "불가능하면 안 된다" | 기술적 사실은 동일, 판정 축만 변경 |
| **스킨드 메시 GI 기여** | "소프트웨어 경로의 근본 한계" | **AAA 필수.** 동적 캐릭터가 GI에 기여하지 못하면 실격 | §5.1 충실도 비용 표 |
| **SW-SDF(b)와 HW-RT(c)의 관계** | 독립 경로, (b)가 먼저 (c)는 나중 | **(b)는 (c)의 준비 단계.** 공유 인프라(Surface Cache, radiance cache)를 (b)에서 구축 | (b)를 생략하고 (c)로 직행하면 공유 인프라 설계 리스크 증폭 |
| **사전 베이크+블렌딩 대안** | "원신급에선 현실적" (암묵) | **AAA에서 불가.** 포기 항목 6개 중 4개가 필수 | §5.1 충실도 비용 표 |
| **Bevy Solari 참조** | "구현 중, 참조 패턴 미검증" | **프로덕션 검증 참조로 격상.** Solari는 Bevy 0.17/0.18에 정식 병합 완료 | 실사용 검증된 오픈소스 RT GI — 설계 패턴 신뢰도 상승 |

### §9 gaps 문서 갱신 제안 — 재기술

기존 제안(§9)에는 "원신급 목표엔 소프트 GI로 충분"이 포함돼 있었다. AAA 기준 재기술:

> **§2 재평가 (AAA)**: "❌ 불가"가 아니라 **"⚠️ SW-SDF 경로로 소프트 GI는 가능, HW-RT 프로덕션화가 AAA 완성의 게이팅"**.
> - Lumen 기본 경로가 SDF 소프트웨어 RT라는 점에서 SDFGI와 **동일 계열** — 소프트 확산 GI는 SDFGI 개선/커스텀 SDF 트레이싱(경로 a→b)으로 **엔진 신규 하드웨어 기능 없이** 접근 가능.
> - Godot HW-RT 토대가 **이미 착지**(GH-99119, 4.7 dev 1, experimental) — **미러 반사·스킨드 메시 GI 기여(AAA 필수)는 이 RT의 프로덕션화에 게이팅.**
> - **SW-SDF(b)는 HW-RT(c)의 준비 단계로 재배치** — Surface Cache·radiance cache는 양 경로가 공유하는 인프라다.
> - ~~원신급 목표엔 소프트 GI로 충분~~ → **[폐기]** AAA 기준에서 성립하지 않음. 원신은 하한 참조점.
