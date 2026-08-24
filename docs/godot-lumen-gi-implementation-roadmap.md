# Godot Lumen급 GI — 구현 로드맵 / 스파이크 계획

> 자매 문서 [godot-lumen-gi-implementation-research.md](./godot-lumen-gi-implementation-research.md)의 딥리서치 결론을 **실행 가능한 단계**로 전환한 문서.
> Nanite 리서치의 "Phase 0 스파이크 → 게이팅 검증 → 증분 구축" 패턴을 따른다.
> 목표 (AAA 기준, 2026-08-18 개정): **HW-RT 프로덕션화(GH-99119 `experimental` API를 실사용 가능하게)를 1급 목표로.** SW-SDF 경로(b)는 HW-RT 경로(c)의 준비 단계로 재배치. 미러 반사·스킨드 메시 GI 기여는 AAA 필수 항목이며 HW-RT 게이팅.

---

## 0. 의사결정 프레임 — 무엇을 먼저 정해야 하나

로드맵 진입 전 **3개 분기**가 전체 비용을 가른다:

| 분기 | 선택지 | 결과 |
|------|--------|------|
| **품질 목표** | 소프트 확산 GI만 vs. + 샤프 미러 반사 | 전자는 경로 a/b(HW 불요), 후자는 경로 c(HW-RT 필수) |
| **하드웨어 티어** | 비-RT(GTX-1070급)까지 vs. RTX-2000+ 전제 | 소프트웨어 SDF 경로 vs. HW-RT 경로 |
| **엔진 개입 수준** | GDScript/컴퓨트 셰이더 레이어 vs. C++ 코어 개조 | 경로 a/b 상당수는 `CompositorEffect`+컴퓨트로 가능, 경로 c는 코어 협업 |

> **⚠️ 2026-08-18 폐기 — 위 기본값은 "원신급 스타일라이즈드" 전제에서만 성립.** AAA 기준(UE5.4+ / Horizon / CP2077 RT Overdrive) + 포크·업스트림 디스커넥트 전제 하에서: **HW-RT 경로(c)를 1급 목표로, SW-SDF 경로(b)는 (c)의 준비 단계로 재배치.** 미러 반사·스킨드 메시 GI 기여가 AAA에서 필수이며, 코어 RT API를 직접 개조할 수 있다. "원신급"은 하한 참조점일 뿐 목표가 아니다.

---

## 1. Phase 0 — 게이팅 스파이크 (검증 먼저, 구현 나중)

리서치가 남긴 4개 open question을 **저비용 실측**으로 닫는 단계. 각 스파이크는 며칠~1주.

### S0-1. SDFGI 코드 재사용률 실측 🟢 (최우선)
- **질문**: `servers/rendering/renderer_rd/environment/gi.{h,cpp}`의 캐스케이드 SDF 생성·프로브·2-bounce 피드백을 커스텀 GI가 얼마나 재사용 가능한가?
- **방법**: `SDFGI::update()` / `sdfgi_integrate.glsl` / `sdfgi_direct_light.glsl` 읽고, 동적 오클루더/이미시브 기여를 추가할 삽입 지점(재복셀화 트리거) 매핑.
- **산출**: 재사용 가능 모듈 목록 + 신규 작성 필요 모듈 목록.

### S0-2. HW-RT API 성숙도 실측 🟢
- **질문**: GH-99119가 노출한 `blas_create`/`tlas_create`/`raytracing_pipeline_create`로 **최소 RT GI 프로브 1개**를 실제로 쏠 수 있나? 매 프레임 TLAS 재구축을 렌더 그래프에 넣을 수 있나?
- **방법**: `Fahien/godot-raytracing-gdscript-demo`를 현재 master에서 빌드·실행 → RenderingDevice RT 경로의 실동작/제약(experimental 한계, SBT 정렬, device address) 확인.
- **산출**: 경로 (c) 진입 가능 여부 판정 + 잔여 엔진 요구 목록.

### S0-3. Radiance Cascades 벤치 🟡
- **질문**: `Sohojoe/radiance-cascades-godot`가 SDFGI 대비 노이즈·응답 지연을 실제로 개선하나? 3D 확장의 메모리 특성은?
- **방법**: 동일 씬에서 SDFGI vs. RC 프레임타임/수렴 프레임 수 비교.
- **산출**: 경로 (d)를 (a)/(b)의 노이즈 저감 보조로 채택할지 결정.

### S0-4. Bevy Solari 이식 패턴 추출 🟡
- **질문**: Solari의 ReSTIR GI + world-space radiance cache 파이프라인 중 Godot에 옮길 설계 패턴은?
- **방법**: Solari 0.18 소스/블로그 정독 → Lumen 분해(§research 1.3)와 대조표 작성.
- **산출**: 경로 (b)/(c)의 radiance cache·denoiser 설계 참조 노트.

**Phase 0 종료 기준(gate)**: S0-1/S0-2 완료 시 "소프트 GI를 (a)로 갈지 (b)로 갈지" + "(c) 진입 시점" 결정 가능.

---

## 2. Phase 1 — 소프트 GI 트랙 (경로 a → b)

Phase 0 게이트 통과 후. **HW 신규 기능 불요**, 컴퓨트 셰이더 + 기존 SDFGI 자원 재사용.

### Milestone A — SDFGI 약점 개선 (경로 a) 🟢
증분 위험 낮음. 우선순위순:
1. **응답 지연 완화**: 25프레임 수렴을 적응적으로 — 카메라/라이트 변화량에 따라 캐스케이드 갱신 빈도 조절.
2. **동적 오브젝트 GI 기여(부분)**: 현재 "받기만" → 저해상도 프록시로 근거리 캐스케이드에 이미시브/오클루전 주입(전체 재복셀화 대신 선택적).
3. **낮/밤 전이 품질**: 태양 방향 변화 시 캐스케이드 direct light 재계산 스케줄 최적화.

**완료 정의**: 낮/밤 사이클에서 SDFGI 전이 아티팩트가 육안으로 감소, 동적 오브젝트 주변 GI 응답 개선.

### Milestone B — 커스텀 SDF 소프트 GI (경로 b) 🟢~🟡
Lumen 소프트웨어 경로 모사. 큰 신규 서브시스템:
1. **Surface Cache 유사** — 메시 표면 material을 저해상도 아틀라스(Card)로 캡처, 셰이딩 분리.
2. **Screen-space radiance cache** — GI 다운샘플.
3. **World-space radiance cache** — 원거리 조명.
4. **시간적 importance sampling** — 이전 프레임 조명 재사용(디노이저 대체).

**완료 정의**: SDFGI를 넘어서는 멀티바운스·노이즈 저감을 비-RT GPU에서 달성.

**의존성**: Milestone A가 B의 SDF 인프라를 선검증. S0-4의 Solari 노트가 radiance cache 설계 입력.

---

## 3. Phase 2 — 반사·완전 동적 트랙 (경로 c) 🔴 **1급 목표, Phase 1과 병행**

엔진 HW-RT의 experimental → 프로덕션화에 **게이팅**. AAA 기준에서 이 트랙이 **최종 목표**이며 Phase 1(b)는 그 준비 단계다.

- **선행 조건(엔진 측)**: GH-99119의 `experimental` RT API를 **실사용 가능하게 직접 개조** — ray tracing pipeline 성숙, SBT 정렬/device address 안정화(진행 커밋 존재), 매 프레임 TLAS 재구축의 렌더 그래프 통합, denoiser. 포크 전제이므로 "코어 팀 협업"이 아니라 **직접 코어 RT API를 밀 수 있다**.
- **구현**: BLAS/TLAS 위에 RT GI 프로브 + **샤프 미러 반사** 패스 + **스킨드 메시 GI 기여**(TLAS에 동적 BLAS 포함). ReSTIR류 샘플링(Solari 참조). Surface Cache·radiance cache는 (b)에서 구축된 공유 인프라를 재사용.
- **GPU 게이트**: RTX-2000+/RX 6000+. (b)는 비-RT 티어를 계속 지원.
- **보상**: 미러 반사·동적 캐릭터 GI 등 **완전한 Lumen 동등성**은 이 트랙에서만. AAA에서 이 트랙 없이는 실격.
- **Nanite 지오 풀 연계**: nanite-implementation §10.4-3 — 단일 대형 SSBO 지오 풀을 BLAS에 직접 먹일 수 있다. S3 시점에 BDA + AS-build usage 플래그를 미리 반영할 것.

---

## 4. 리스크 / 안티패턴

| 리스크 | 완화 |
|--------|------|
| experimental RT API가 릴리스마다 변함(시간 민감) | 경로 (c)를 크리티컬 패스에 두지 않기. dev 스냅샷·PR 상태 주기 재확인. |
| Surface Cache "simple interiors" 콘텐츠 제약 상속 | 아트 파이프라인에서 벽/바닥/천장 분리 규칙 사전 합의. |
| "소프트웨어 경로 = Lumen-grade" 과장 | 소프트웨어 경로는 스킨드 메시·미러·WPO 미지원임을 명시. 마케팅 문구 주의. **AAA 기준: 사전 베이크+블렌딩 대안을 택하면 무엇을 포기하는지 lumen-research §5.1 참조.** |
| 64-bit atomics를 GI 게이팅으로 오인 | GI 경로에선 불필요(Nanite SW 래스터와 혼동 금지). |
| 단일 요인 게이팅 오판(리서치 refuted 항목) | GI는 Surface Cache+radiance cache+denoiser 다수 서브시스템 — "RT API만 있으면 된다" 아님. |

---

## 5. 요약 — 실행 순서 한 줄

```
Phase 0 스파이크(S0-1·S0-2 우선)  →  게이트 결정
   → Phase 1: Milestone A(SDFGI 개선) → Milestone B(커스텀 SDF 소프트 GI)   [(c)의 준비 단계]
   → Phase 2: 경로 c(HW-RT 반사·완전 동적)           [🔴 1급 목표, Phase 1과 병행]
```

**한 줄 결론 (AAA 기준, 2026-08-18 개정):** HW-RT 프로덕션화가 1급 목표이며, SW-SDF 경로(b)는 (c)의 공유 인프라를 구축하는 준비 단계다. 미러 반사·스킨드 메시 GI 기여는 AAA에서 필수이며 HW-RT에 게이팅된다. "❌ 불가"가 아니라 **경로 (b)를 (c)의 준비 단계로 재배치한 단계적 격차 해소**.

---

## 부록 — 코드 진입점 (로컬 master)

- SDFGI 로직: `servers/rendering/renderer_rd/environment/gi.{h,cpp}`
- SDFGI 셰이더: `servers/rendering/renderer_rd/shaders/environment/sdfgi_{preprocess,direct_light,integrate,debug}.glsl`
- RT API: `servers/rendering/rendering_device.h` (`blas_create`/`tlas_create`/`raytracing_pipeline_create`), `servers/rendering/rendering_device_commons.h` (`SHADER_STAGE_RAYGEN…`, `UNIFORM_TYPE_ACCELERATION_STRUCTURE`)
- Vulkan RT 드라이버: `drivers/vulkan/rendering_device_driver_vulkan.cpp`
- 커스텀 렌더 훅(경로 a/b 진입점): `CompositorEffect` + `RenderingDevice` 컴퓨트

---

## 2026-08-18 개정 — AAA 기준 재채점

> 전제 변경: 원신급 스타일라이즈드에서 AAA(UE5.4+ / Horizon / CP2077 RT Overdrive)로 목표 상향. 자매 문서 [godot-lumen-gi-implementation-research.md](./godot-lumen-gi-implementation-research.md)의 개정 결론을 로드맵에 반영.

### 경로 재배치 — 로드맵에 반영된 변경

| 항목 | 기존 로드맵 | AAA 재배치 | 변경 내용 |
|------|-----------|-----------|-----------|
| **품질 목표** | "소프트 확산 GI만" vs "샤프 미러 반사" | **미러 반사 + 스킨드 메시 GI 기여 필수** | §0 의사결정 프레임에 인라인 폐기 마커 추가 |
| **경로 (c) 위상** | "🟡 장기 병행, 엔진 RT 성숙 편승" | **🔴 1급 목표, Phase 1과 병행** | §3 전면 재작성. "편승" → "직접 RT API 개조" |
| **경로 (b) 위상** | "소프트 GI 목표로 우선, 출시 가능" | **(c)의 준비 단계로 재배치** | §2 설명 갱신. Surface Cache·radiance cache는 (c)와 공유 인프라 |
| **"원신급 소프트 GI는 현실적 도달 가능"** | §5 한 줄 결론 | **폐기.** 대체 결론: HW-RT 프로덕션화가 1급 목표 | §5 전면 재작성 |
| **Phase 0 게이트** | "소프트 GI를 (a)로 갈지 (b)로 갈지" | **"(b)를 (c)의 준비로 삼을지, (c)로 직행할지"로 변경** | `(b)→(c)` 관계 재정립 반영 |
| **Nanite 지오 풀 연계** | 없음 | **Phase 2에 추가.** S3 시점에 BDA+AS-usage 플래그 반영 | nanite-implementation §10.4-3 |
| **사전 베이크+블렌딩** | 언급 없음 | **§4 리스크에 충실도 비용 참조로 추가** | lumen-research §5.1 |

### 델타 표 (로드맵 구조 변경)

| 변경 지점 | 수정 내용 |
|-----------|-----------|
| 헤더 목표문 | "소프트 GI 우선 확보" → "HW-RT 프로덕션화 1급 목표" |
| §0 의사결정 프레임 | 원신급 기본값 폐기 마커 + AAA 재배치 |
| §3 Phase 2 | "🟡 장기 병행" → "🔴 1급 목표, Phase 1과 병행". 전략을 "편승"에서 "직접 개조"로 전환 |
| §5 요약 | 실행 순서 아스키 아트에 (b) 위상 변경, 한 줄 결론 전면 재작성 |
| §4 리스크 | "소프트웨어 경로 = Lumen-grade 과장" 항목에 AAA 기준 충실도 비용 참조 추가 |
