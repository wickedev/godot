# Godot 오픈월드 AAA — 전체 구현 로드맵 (마스터)

> **이 문서의 역할:** `docs/` 19개 리서치 문서의 **순서와 레인 간 동기화 지점만** 확정한다.
> 구현 상세·file:line·알고리즘은 각 문서가 정본이다. 충돌 시 **순서는 이 문서가, 방법은 각 문서가** 이긴다.
>
> **전제 (2026-08-18 확정 전략):** 포크 · 업스트림 디스커넥트(침습적 코어 개조 전면 허용) · full-Nanite 커밋(opaque 전면 deferred 전환 + TAA 강제 수용) · AAA 기준(UE5.4+ Nanite/Lumen · Horizon Forbidden West · Cyberpunk 2077 RT Overdrive). 원신은 하한 참조점.
> **판단축은 "man-year급인가" 하나뿐이다.** "코어 수정 필요"는 탈락 사유가 아니다.
>
> **실행 전제:** 9개 레인 병렬. 캘린더 **~33개월**, 인력 **16~19 FTE**. (2026-08-25 전 스코프 확정으로 §15만큼 증액) 렌더러 코어(L1)가 크리티컬 패스이며 여기에 인원을 더해도 단축 효과가 제한적이다(§10 축소 시나리오 참조).

---

## 0. 이 문서가 해결하는 문제

각 리서치 문서는 **자기 트랙 안에서** P0를 선언했다. 교차시키면 세 가지가 깨진다.

| # | 문제 | 구체 | 손실 |
|---|------|------|------|
| 1 | **같은 스파이크 3중 실행** | VT Phase 0(S0.1~S0.3) · Lumen Phase 0(S0-2) · Nanite 스트리밍 전제가 전부 *async 리드백 왕복 · 스레드 가드 #99750 · bindless/descriptor indexing* 을 각자 묻는다 | 3~4주 × 3레인 |
| 2 | **버려질 코드** | Nanite S2의 자체 deferred-lite 라이팅은 S4 리졸브에서 폐기 확정 ([nanite-decision-brief](./godot-nanite-decision-brief.md) §3) | 1~2개월 |
| 3 | **공유 substrate 뒤늦은 확정** | 통합 GBuffer 레이아웃은 Nanite S4 리졸브 · VFX AOV export · Lumen ReSTIR 재투영의 **공통 입력**([unified-gbuffer](./godot-unified-gbuffer-aov-research.md) §9, [nanite-impl](./godot-nanite-implementation-research.md) §10.4-1) | 4레인 동시 재작업 |

→ 해법: **스파이크를 Wave 0으로 통합**, **substrate를 Wave 1에서 동결**, **폐기 예정 코드는 애초에 짓지 않음**.

---

## 1. 레인 정의 (병렬 축)

| 레인 | 담당 범위 | FTE | 정본 문서 |
|------|-----------|:---:|-----------|
| **L1 렌더러 코어** | 코어 훅 → 통합 GBuffer/전면 deferred → Nanite S3/S4 리졸브 → async compute 렌더그래프 | 3 | [unified-gbuffer](./godot-unified-gbuffer-aov-research.md) · [nanite-perf](./godot-nanite-performance-implementation-research.md) |
| **L2 지오메트리** | DAG 빌더 → HZB/컬링 → SW/HW 하이브리드 래스터 → 지오 스트리밍 | 2 | [nanite-impl](./godot-nanite-implementation-research.md) |
| **L3 조명/GI** | Lumen 스파이크 → SDFGI 개선 → SW SDF GI → HW-RT 프로덕션화 | 2 | [lumen-roadmap](./godot-lumen-gi-implementation-roadmap.md) |
| **L4 월드/스트리밍** | 물리·내비 스트리밍 → 월드 파티션 → Terrain3D → HLOD ／ VT 전 단계 | 2~3 | [world-streaming](./godot-world-streaming-terrain-research.md) · [vt-plan](./godot-virtual-texturing-implementation-plan.md) |
| **L5 시뮬/환경** | Jolt 물리 · 오션/수중 · 기상/대기/구름 | 2~3 | [physics](./godot-physics-simulation-research.md) · [ocean](./godot-ocean-water-research.md) · [weather](./godot-weather-atmosphere-research.md) |
| **L6 캐릭터/애니** | 인러셜라이제이션·레이어드·리타게팅 → 페이셜·물리블렌딩 → 군중 → 압축/스트리밍 | 2 | [animation](./godot-animation-pipeline-research.md) · [runtime-misc](./godot-runtime-gaps-misc-research.md) §6 |
| **L7 저작툴/VFX** | 모듈 스택 · 노드그래프 · 마스터 시퀀서 · MRQ/AOV export | 2 | [vfx-cinematic](./godot-vfx-cinematic-research.md) |
| **L8 오디오** | 미들웨어 결정 → 커스텀 DSP/컨볼루션 → **스트리밍·보이스 매니저** | 1 | [audio](./godot-audio-propagation-research.md) |
| **L9 플랫폼/인프라** | Sentry·CI 심볼 → GPU 프로파일러 → 업스케일러 → 콘솔(W4) | 1~2 | [gpu-profiler](./godot-gpu-profiler-implementation-research.md) · [console](./godot-console-export-research.md) |

**L1은 크리티컬 패스다.** L2·L3·L7이 L1의 게이트에 물려 있고, L1 내부(S4 → S5)는 직렬이다.
**L5·L6·L8은 렌더러와 거의 독립**이라 M0부터 끝까지 자유 진행한다. 단 L5의 오션·구름은 G4(전면 deferred)에서 셰이딩 경로가 바뀐다.

---

## 2. 크리티컬 패스 (한 줄)

```
RD 스파이크 + 코어 훅 3건  →  통합 GBuffer 스키마 동결  →  Nanite S3 하이브리드 래스터
   →  S4 단일 deferred 리졸브  →  async compute 멀티큐 + S5 스트리밍
```

**왜 이게 크리티컬인가**
- **GBuffer 스키마**는 L1·L2·L3·L7 4레인의 공통 입력이다. ReSTIR가 프라이머리 히트의 albedo/normal/roughness/depth/motion을 리저버 재투영에 쓰므로 **§1도 §2도 단독으로 정할 수 없다**(nanite-impl §10.4-1).
- **async compute 멀티큐**는 Nanite 전용이 아니다. `max_queue_count_per_family = 1`(`drivers/vulkan/rendering_device_driver_vulkan.cpp:1313`)이 Nanite 컬링과 **Lumen TLAS 리빌드의 오버랩을 동시에** 막는다 → §1·§2 공유 게이팅. **한 번만, 마지막에**(nanite-perf §5).

---

## 3. 동기화 게이트 (레인 간 barrier)

| 게이트 | 시점 | 조건 | 해금되는 것 | 실패 시 |
|--------|:----:|------|-------------|---------|
| **G0** | M1 | RD 스파이크 3건 판정 + 코어 훅 3건 머지 + Sentry 라이브 | 전 레인 본격 착수 | bindless 불가 → VT는 단일 아틀라스+UV 인디렉션으로 축소 설계 |

> **G0 중간 판정 (2026-08-25, C3 스파이크 `docs/rd-capability-spike-report.md`):** ⓐ async 리드백 🟡 **수치 재보류 (리뷰 2차 — 해제가 성급했음)**: 2백엔드 관측은 시사적이나 하니스의 프레임 카운터 크로스스레드 레이스·워치독 미사용·원시 로그/빌드 식별자 부재·구성당 단일 런으로 **법칙 확정 불가** 판정. 설계는 보수 가정(분리 스레드 2~3프레임) 유지. 확정 존속 결론: ⓑ 가드 존속 + `call_on_render_thread` 정식 경로(단 0프레임은 `_process` enqueue 한정 관측). 해제 조건: 하니스 자체 시퀀싱 + 워치독 + 원문 로그/빌드 ID 보존 + 구성별 독립 반복. ⓑ 스레드 가드 🟡 — #99750 존속, 승인 우회 `call_on_render_thread` **0프레임 비용**. ⓒ descriptor-indexing 🔴 **FAIL** — 디바이스 생성이 `Vulkan11Features`만 체인, **VT 축소 분기 발동**. 단 회피로 복원용 최소 변경(Vulkan12Features 체인+nonuniform, Task #18)은 코어 훅급이라 Wave 0~1로 편성 — 성공 시 nanite-perf §③ "고정 샘플러 배열 nonuniform" 회피로가 되살아남. 잔여: Windows/Linux 재측정(하니스 동봉). 코어 훅은 ②③ 랜딩·①④ 진행 중, Sentry는 재리뷰 중.
| **G1** | ~~M4~~ | ✅ **통과 (2026-08-25 — M0에 조기 달성).** [동결본](./gbuffer-schema-v1-proposal.md) v1.3: Tier-1 7어태치먼트(노멀 병합·MRT 여유 1)·24-bit objectid·N-1 히스토리 3텍스처·LOD-모션 계약·emission.a 잔차 채널. 5라운드에서 스펙 결함 4건 동결 전 제거, 3레인 서명 전건 실기 근거 | **해금**: L1 deferred 리졸브 / L2 S3 / L3 ReSTIR 입력 / L7 AOV export | — |
| **G2** | ~~M6~~ | ✅ **통과 (2026-08-25 — M0에 조기 달성).** [동결본](./geometry-pool-contract-g2-proposal.md) v1.1: 생성 진입점(vertex/index_buffer_create)+플래그 3종 동결(전건 dgx 실기)·(b) 레이아웃+255·레코드 B+이중 그룹 참조·LOD 오차=기하 편차 의미 규정·§7.7 아티팩트 계약(5-튜플 키·버전 2종)·크기 상한=바인딩 제약(~1.87억 tri/풀). 3레인 서명 전건 실측 근거 | **해금**: L2 스트리밍 레이어 / L3 BLAS 직접 빌드 | — |
| **G3** | M10 | Nanite S2 하니스 판정 (컬링·LOD 컷·HZB 실증) | S3/S4 진입 | DAG 품질 미달 → S1으로 회귀 |
| **G4** | M18 | **S4 리졸브 라이브 — opaque 전면 deferred 전환 완료** | L7 AOV·L3 HW-RT가 GBuffer 소비 시작 / L9 모션블러·업스케일러 착수 | 롤백 지점: 하이브리드(비-Nanite opaque는 포워드 유지) |
| **G5** | M25 | async compute 멀티큐 + 타임라인 세마포어 | S5 스트리밍 · TLAS 오버랩 · 부력 리드백 병렬화 · **L5 GPU 헤어/컴퓨트**(Jolt `ComputeSystemVK::Initialize`가 컴퓨트 큐 인덱스 요구 — `max_queue_count_per_family = 1`이 여기도 막음) | 단일 큐 유지 → 프레임 예산 재조정 |

> **G1은 이 프로젝트에서 가장 중요한 단일 결정이다.** 스키마를 못 정한 채 L2/L3/L7을 진행시키지 말 것.

---

## 4. Wave 0 — 계측·토대 (M0~M1)

> 목적은 "만들기"가 아니라 **리스크 제거 + 측정 수단 확보**. 여기서 3~5주를 쓰지 않으면 이후 전 레인이 장님으로 달린다.

| 레인 | 작업 | 공수 | 근거 |
|------|------|------|------|
| **L9** | **Sentry + CI 심볼 업로드** | 2~5일 | 자작 렌더러의 하드 크래시를 필드에서 수집할 수단 없이 §1 착수는 무모 (runtime-misc §9) |
| **L9** | GPU 프로파일러 **P0**(label 강제 활성 + `depth`/`parent` 계층화) + **P1**(Tracy GPU context 배선) | 3~4주 | 성능 우선 구현인데 측정 수단이 먼저 (gpu-profiler §8) |
| **L1** | **코어 훅 3건 일괄:** ① 64b image atomic 3-site ② `drawIndirectCount` 상위배선 + `SUPPORTS_DRAW_INDIRECT_COUNT` 케이퍼빌리티 게이팅 ③ `maxStorageBufferRange` LIMIT enum(4-site) | 1~2주 | 저비용·고효과, 이후 전 스테이지의 토대 (nanite-perf §4-1). ⚠️ **[2026-08-25 정정, C1 실측]** “3백엔드 구현 완료”는 오류 — **Metal은 스텁**(`metal3_objects` `render_draw_indirect_count` = `ERR_FAIL_MSG`, 인덱스/비인덱스 양쪽), **Vulkan은 feature 활성화·게이팅 전무**(1.1 디바이스에서 함수포인터 null 크래시 잠복). D3D12만 진짜 구현. → `has_feature` 게이팅 + 미지원 시 보수적 max-count `draw_indirect` 폴백이 필수 패턴 |
| **L1** | 코어 훅 ④ `particles_collision_get_heightfield_texture` getter(~10줄) | 수일 | 레인 오클루전·폴리지 벤딩·젖음 마스크 **3개가 여기 하나에 물림** (weather §0-1) |
| **전 레인** | **통합 RD 능력 스파이크 1회** — ⓐ `buffer_get_data_async` 왕복 무스톨 ⓑ 스레드 가드 #99750 현재 상태 ⓒ bindless/descriptor indexing 노출 여부 | 1~2주 | VT S0.1~S0.3 + Lumen S0-2 + Nanite 스트리밍 전제의 **합집합**. 산출: `docs/rd-capability-spike-report.md` |
| **L3** | Lumen 스파이크 **S0-1**(SDFGI 재사용률 실측) · **S0-2**(GH-99119 RT API로 프로브 1발) | 1~2주 | 경로 (c) 진입 가능 여부 판정 (lumen-roadmap §1) |

**Wave 0 종료 = G0.** 산출물: 스파이크 판정 3건 + 코어 훅 4건 머지 + 크래시/프로파일링 라이브.

---

## 5. Wave 1 — substrate 확정 & 월드 표현 (M1~M6)

| 레인 | 작업 | 비고 |
|------|------|------|
| **L1** | **통합 GBuffer/AOV 서브시스템 설계 → 스키마 v1 동결(G1)** → `MODE_RENDER_GBUFFER` 변종 + `RB_SCOPE_GBUFFER` + `get_gbuffer_fb` + `gb_objectid` 배선 | unified-gbuffer §8이 file:line까지 확정. **전면 deferred 전환의 부수 영향(투명·라이트맵·Forward Mobile)은 착수 전 스코핑 필요 — 어느 문서에도 견적 없음** |
| **L2** | **Nanite S1 — 오프라인 DAG 빌더** (meshopt 1.2 단독, METIS 불요) | 렌더러와 커플링 0이라 **M0부터 즉시 병렬 가능. 품질의 8할이 여기.** `meshopt_partitionClusters`/`simplifyWithAttributes(vertex_lock)` |
| **L4** | **P0(2~3주):** 물리 스트리밍 매니저(`space_create`) · 내비 스트리밍(`map_create`) → **P1:** 월드 파티션 매니저 · Terrain3D 리전 스트리밍(PR #1020) | world-streaming §8 |
| **L4** | **폴리지 GPU 컬링** (HZB + compute cull + indirect MultiMesh) | Wave 0 훅②에 의존. **Nanite의 2-pass HZB 컬링과 같은 코드 — 작은 스코프의 리허설** |
| **L4** | **VT Phase 1 — RVT식 런타임 캐시** (지형 블렌딩, 코어 0) | 지형이 서는 즉시 붙음. Phase 0은 Wave 0 통합 스파이크로 대체됨 |
| **L3** | Lumen **Phase 1 Milestone A** — SDFGI 응답 지연·동적 오브젝트 기여·TOD 전이 개선 | (b)/(c)의 SDF 인프라 선검증 |
| **L5** | 물리 **P0**(캐릭터 step-up · 물리 바디 스트리밍 · 결정론 훅 · 차량 MVP, 3~5주) → **P1**(클로스 B1+B3 · 조인트 break force, 6~11주) | physics §10 |
| **L5** | 기상 **인프라 선행**: 글로벌 유니폼 상태 버스(`weather_*`/`wind_*`/`tod_*`/`wetness_*`) + 공용 `.gdshaderinc` 컨벤션 → TOD 싱글턴 → 전역 바람 버스 | **이 하나가 기상 트랙 절반을 해금**(weather §10-1). 갱신 비용 사실상 0 |
| **L5** | 오션 **Phase 1**(Tessendorf FFT → Projected Grid → 기본 셰이딩 → Gerstner 부력, 6~10주) | ocean §7.1 |
| **L6** | 인러셜라이제이션(ROI 최고) → 레이어드/본마스크 스테이트머신 → 런타임 리타게팅 프록시 | 전부 GDExtension, 코어 0 (animation 재채점 ①②③) |
| **L7** | Niagara-lite **모듈 스택 데이터모델** + VisualShader 파티클 노드그래프 (EditorPlugin, 코어 0) | vfx 재채점 P3→P1 |
| **L8** | **미들웨어 결정 스파이크** → 커스텀 DSP/파티션 FFT 컨볼루션 리버브(GDExtension, 4~8주) | `AudioEffect`가 GDVIRTUAL 완전 바인딩 = 코어 0 |
| **L9** | GPU 프로파일러 **P2**(CPU↔GPU 캘리브레이션 + pipeline statistics) | `VK_EXT_calibrated_timestamps` |

**M6 = G2**(지오 풀 버퍼 계약 동결).

---

## 6. Wave 2 — Nanite 프론트엔드 & GI 본선 (M6~M12)

| 레인 | 작업 |
|------|------|
| **L2** | **S2 — 런타임 검증 하니스**(CompositorEffect + compute cull + indirect HW raster). ⚠️ **자체 deferred-lite 라이팅은 짓지 말 것** — S4에서 버려질 코드 |
| **L2** | **S3 — 64b atomic 활성 → R64 vis-buffer + SW/HW 하이브리드 래스터**. ~~Metal/구세대 Apple용 HW-only 폴백 변종 유지 필수~~ **[2026-08-25 정정, C1 훅① 실측]** Metal은 Apple8+Mac2/Apple9에서 **64b 이미지 아토믹 네이티브 지원**(`metal_device_properties.cpp:154`에 이미 계산되던 죽은 값 — 훅①이 `SUPPORTS_IMAGE_ATOMIC_64_BIT`로 노출, M2 Pro 실측 true). **R64 vis-buffer는 Metal 네이티브 경로에서 성립** — HW-only 폴백은 구세대(Apple7 이하)만. 단 **MoltenVK 경로는 32b 아토믹조차 false → macOS는 반드시 `--rendering-driver metal`.** Metal 잔여 결손은 draw-indirect-count 1건(ICB, Task #10). **+ Metal `MTLIndirectCommandBuffer` 신규 도입**(드라이버 전체 ICB 사용처 0건 실측) — Metal에서 GPU-결정 드로우 카운트를 소비하는 유일한 경로. `drawIndirectCount` 스텁 해소와 S3 하이브리드 래스터가 같은 ICB 인프라를 씀 |
| **L1** | 비-Nanite opaque GBuffer emit 경로 + deferred 라이팅 리졸브 골격(froxel 재사용, private set 0/1) |
| **L1** | `MODE_RESOLVE_MATERIAL` 변종 + 셰이더 컴파일러 2변경(analytic derivative) |
| **L3** | **Phase 1 Milestone B** — 커스텀 SDF 소프트 GI(Surface Cache · screen/world-space radiance cache · 시간적 importance sampling) ／ **병행 Phase 2 착수: HW-RT 프로덕션화**(GH-99119 experimental 직접 개조, TLAS 생명주기, SBT/BDA 안정화). **[2026-08-25 C3 실측] Apple 플랫폼 RT 전무** — MoltenVK 경로 컴파일 제외(`VULKAN_RAYTRACING_ENABLED=0` on macOS/iOS), Metal 드라이버 9종 엔트리포인트 전부 스텁. **판정: (B) Metal RT 백엔드 자체 구현 채택**(`MTLAccelerationStructure`, Metal 3 — Task #21, L1+L3 공동, Wave 2~3). 전부-구현 방침과 정합하며 man-year 아님. macOS 출하 타깃 여부는 사용자 확인 항목. **S0-2 판정 (2026-08-25, dgx/GB10 실측): 🟢 경로 (c) 진입 가능** — ray query·RT pipeline 노출 확인, BLAS 빌드 3포맷(float32x3·snorm16x4·**snorm16x3 6B**) 통과. 단 **ray query만 써도 hit SBT 강제**(`rendering_device.cpp:543` — SBT는 RT 파이프라인 개념인데 BLAS 유효 시 hit_sbt_range≠0 요구) → RT API 프로덕션화 목록에 추가(Task #24) |
| **L4** | VT **Phase 2** 소프트웨어 SVT(스트리밍) ／ **Phase 3** HW 스파스 VT(코어 C++, bounded) ／ LOD 관리자 · Terrain3D 인스턴스 충돌 |
| **L5** | 오션 **Phase 2**(Jacobian foam · 굴절 · 코스틱 · Cascaded FFT) → **Phase 3**(수중·상호작용) ／ 기상: 레인 오클루전 + 탑다운 RT 인프라 재사용(**하나 지으면 3개 따라옴**) + 젖은 BRDF |
| **L5** | **[확정]** 대기 **Bruneton/Hillaire 4-LUT 다중산란**(사전계산 = 컴퓨트 셰이더 저작, sky 적용까지 **코어 0**. 씬 픽셀 AP만 국소 코어 — `fog_process` @ `scene_forward_clustered.glsl:1099`의 sampler3D froxel 조회 재사용). 6~10주 |
| **L6** | 페이셜(FACS/ARKit 52 블렌드셰이프 + 립싱크 솔버) · 물리 기반 애니 블렌딩(래그돌 전이) · SSS 프리인테그레이티드 LUT |
| **L7** | **마스터 시네마틱 시퀀서**(EditorPlugin, 코어 0) · Spawnables/Possessables · Take Recorder |
| **L8** | **오디오 스트리밍 + 보이스 우선순위/컬링/버추얼라이제이션** ← *가장 과소평가된 격차*. 현재 전 에셋 RAM 상주 · 믹스 단일 스레드 |
| **L9** | GPU 프로파일러 **P3**(Metal 타이밍 stub 교체) |

**M10 = G3**(S2 판정).

---

## 7. Wave 3 — S4 통합 리졸브 (M12~M18) ⭐ 무게중심 ①

| 레인 | 작업 |
|------|------|
| **L1+L2** | **S4: vis-buffer → 통합 GBuffer → 단일 deferred 라이팅 리졸브.** UE 5.4식 셰이딩 빈 분류 + 스텐실 하이브리드 + TAA 모션벡터. **6~12 man-month** |
| **L3** | HW-RT GI 프로브 + **샤프 미러 반사** + **스킨드 메시 GI 기여**(동적 BLAS). 지오 풀에서 DAG 특정 LOD를 잘라 **자료 중복 없이 BLAS 빌드**(포크의 구조적 우위) |
| **L7** | **AOV export가 여기서 거의 공짜로 따라온다** — 같은 어태치먼트를 `CompositorEffect`+`texture_get_data`로 읽어 multipart EXR. MovieWriter → MRQ 승격 · Cryptomatte |
| **L5** | **[확정]** 구름 **근경 관통**(`EFFECT_CALLBACK_TYPE_POST_SKY` @ `compositor.h:46` + `access_resolved_depth`, **코어 0**) · 구름의 반사/GI 반영(국소 코어) · 정적/동적 그림자 분리 캐싱. 2~4개월 |
| **L6** | 군중 하이브리드 LOD(L0 스켈레탈 / L1 URO 틱 스로틀 / L2~L3 VAT) — **L1 근거리 군중(20~100)이 진짜 병목** |
| **L9** | **G4 통과 후:** 모션블러 · DLSS/FSR3.1 업스케일러. **그 전에 하면 모션벡터·리졸브 경로 변경으로 재작업** |

**M18 = G4**(전면 deferred 라이브).

---

## 8. Wave 4 — 처리량 최대화 (M18~M28) ⭐ 무게중심 ②

| 레인 | 작업 |
|------|------|
| **L1** | **타임라인 세마포어 → async compute 멀티큐(렌더그래프 멀티스트림) → 파인 배리어.** 12+ man-month, 최난 |
| **L2** | **S5 스트리밍:** `call_on_render_thread` 마샬링 + 단일 샤딩 SSBO 풀 + async 리드백 링 + persistent-mapped 설치 + 루트 상주 + 컴퓨트 디코드 |
| **L4** | **HLOD 빌더**(공간 클러스터링 + 프록시 메시) + LOD/HLOD/월드 스트리밍 통합 — man-year급 |
| **L3** | ReSTIR 디노이저 성숙 · TLAS 리빌드를 async 큐로 오버랩(**G5 수혜**) |
| **L5** | 부력 GPU 리드백 병렬화(G5 수혜) · 프로덕션급 볼류메트릭 구름 · 구름의 반사/GI 반영 |
| **L6** | **애니메이션 압축·스트리밍**(ACL/ozz급) — #7144가 "blocked on the rendering team"이라 한 항목. **VT 스트리밍 인프라 위에 얹으므로 Wave 2 이후에만 가능** |
| **L7** | GPU 이벤트/데이터 인터페이스(코어, man-year) · 유체·연기 시뮬 |
| **L9** | 콘솔 포팅 실행(W4 위탁 또는 직접) |

**M25 = G5.** M28~M30: 통합 안정화 · 프로파일링 기반 튜닝 · 폴리시.

---

## 9. 의존성 지도

```
                    Wave0        Wave1        Wave2        Wave3        Wave4
                    M0-1         M1-6         M6-12        M12-18       M18-28
L1 렌더러코어  [훅3+④]────[통합GBuffer═G1]──[deferred골격]──[★S4 리졸브]──[★async compute]
                   │             │  │  │           │             │              │
L2 지오메트리  [────]──────[S1 DAG빌더]──[S2═G3][S3 하이브리드]──[S4 공동]────[S5 스트리밍]
                   │             │     ╲  (G2 지오풀 계약)                      │
L3 조명/GI     [S0-1,2]────[SDFGI 개선]───[SW SDF GI ∥ HW-RT 프로덕션화]──[RT 반사/스킨드]──[TLAS 오버랩]
                   │             │                                │
L4 월드/VT     [ RD  ]─────[물리·내비 ▸ 파티션 ▸ Terrain ▸ 폴리지컬링 ▸ VT-RVT]──[SVT/HW스파스]──[HLOD]
                   │             │
L5 시뮬/환경   [훅④ ]─────[유니폼버스 ▸ TOD ▸ 물리P0/P1 ▸ 오션P1]──[오션P2/P3 ▸ 4-LUT 대기]──[구름 ▸ AP]
L6 캐릭터/애니 ────────────[인러셜 ▸ 레이어드 ▸ 리타게팅]────[페이셜 ▸ 물리블렌딩 ▸ SSS]──[군중]──[애니압축*]
L7 저작툴/VFX  ────────────[모듈스택 ▸ 노드그래프]──────[마스터 시퀀서]──[AOV/MRQ†]──[GPU이벤트 ▸ 유체]
L8 오디오      [미들웨어결정]──[커스텀DSP ▸ 컨볼루션]──[★스트리밍 ▸ 보이스매니저]──────────────────
L9 인프라      [Sentry+프로파일러P0/P1]──[P2]──[P3]──────────[모션블러 ▸ 업스케일러‡]──[콘솔]

  ═ 게이트   ★ man-year 무게중심   * VT 스트리밍 의존   † G4 이후 거의 공짜   ‡ G4 이전 착수 금지
```

---

## 10. 인력 축소 시나리오

| 규모 | 전략 | 캘린더 |
|------|------|--------|
| **14~17 FTE** | 위 로드맵 그대로 | ~30개월 |
| **8~10 FTE** | L1·L2·L3·L4만 풀가동. L5는 오션 또는 기상 **택1**, L6는 P0 3건만, L7 시퀀서만, L8 미들웨어 도입으로 대체 | ~38개월 |
| **5~6 FTE** | L1+L2+L4만. GI는 SDFGI 개선(Milestone A)에서 정지, HW-RT 포기. 환경·VFX·오디오는 애드온/미들웨어 | ~48개월+ |
| **1~3 FTE** | **S4/S5 포기 권고.** S1~S2에서 정지하고 HLOD+임포스터 병행(nanite-impl §10.5의 fallback) | — |

**L1에 인원을 추가해도 단축 효과가 제한적이다** — S4 → S5가 아키텍처 직렬 의존이고, GBuffer 스키마는 합의 비용이 인원에 비례해 증가한다. L1은 3명이 상한선에 가깝다.

---

## 11. 리드타임이 긴 조기 결정 (M0에 시작)

| # | 결정 | 왜 지금 | 리스크 |
|---|------|---------|--------|
| 1 | **오디오 미들웨어 vs 콘솔** | ✅ **결정 완료 (2026-08-25): Steam Audio 벤더링 + §6 자체 구현** ([결정 브리핑](./godot-audio-middleware-console-decision.md)). 실사 결과 병목은 라이선스가 아니라 Godot 통합 레이어의 콘솔 재빌드였고, Apache-2.0 소스 벤더링이 포크 전략과 정합 | ✅ |
| 2 | **콘솔 개발자 등록** | Xbox는 관문 확정, PS5/Switch는 NDA. 기술이 아니라 라이선스가 막는다. 심사 리드타임이 김 | 🟡 |
| 3 | **W4 위탁 vs 직접 포팅** | 포크 전제에서 self-managed 전환 재검토 필요(console §7) | 🟡 |
| 4 | **DLSS 라이선스** | "코드가 아니라 라이선스"가 막는 구조 — 콘솔과 동형. 클로즈드 배포 레이어 분리 설계 필요 | 🟡 |
| 5 | **TAA 강제 수용 확인** | full-Nanite 커밋의 부산물. 아트/UX가 이 제약을 알고 있어야 함 | 🟢 |
| 7 | **macOS 출하 타깃** | ✅ **확정 (2026-08-25, 사용자): 출하 타깃 맞음.** → Metal RT 백엔드(Task #21) **Wave 2 확정**, Metal ICB(Task #10)와 함께 Apple 결손 2건이 정식 스코프. macOS 검증은 네이티브 Metal 드라이버 경로 기준(MoltenVK 금지) | ✅ |
| 8 | **RT 검증 하드웨어** | ✅ **확정 (2026-08-25, 사용자): DGX Spark(Blackwell, Linux/aarch64) 사용 승인.** S0-2 + G2 BLAS 실기 검증 차단 해제 | ✅ |
| 6 | **전면 deferred의 투명·라이트맵 영향 스코핑** | **어느 문서에도 견적이 없다.** G1 전에 반드시 | 🔴 |

---

## 12. 문서 정합성 주의 (stale 구간)

리서치 문서 중 일부는 **AAA 재채점 이전 절이 그대로 남아 있다.** 아래는 무시하고 재채점 절을 정본으로 삼는다.

> **2026-08-25 사용자 최종 결정:** *"AAA 재채점이 그 전제를 뒤집어서 둘 다 필수"* — **물리 정확 다중산란 대기(Bruneton/Hillaire 4-LUT)와 근경 관통 볼류메트릭 구름은 확정 스코프다.** weather 문서의 stale 구간은 폐기 배너를 삽입해 처리 완료. 나머지 5건은 미처리 상태이므로 계획 시 주의.

| 문서 | stale 구간 | 정본 |
|------|-----------|------|
| [weather](./godot-weather-atmosphere-research.md) | ~~§10 로드맵 · §12 gaps 반영~~ → **✅ 2026-08-25 폐기 처리 완료** (문서에 ⛔ 배너 삽입) | **§0 AAA 재채점 = 최종 결정.** 물리 정확 다중산란 대기 · 근경 관통 구름 **둘 다 필수**. 총량 3~4주 → **8~14개월(1인)** |
| [nanite-decision-brief](./godot-nanite-decision-brief.md) | ~~§2의 "경로 C 비권장"~~ — **오기. §2 표는 이미 "C = ✅ 목표"로 갱신돼 있다.** 남은 stale은 A행 "Go 권장·리스크 낮음" / B행 "조건부"뿐 | §3 재배치 — **S1~S5 전부 통과 지점** |
| [nanite-impl](./godot-nanite-implementation-research.md) | ~~§1.1·§4·Phase 1의 METIS 전제~~ → **✅ 인라인 정정 완료** | §10.2 — `meshopt_partitionClusters`가 대체. **METIS 벤더링 착수 금지** |
| [vt-plan](./godot-virtual-texturing-implementation-plan.md) | "다음 액션 = Phase 0 스파이크 착수" | Wave 0 통합 스파이크로 **흡수됨**. 단독 실행 금지 |
| [ocean](./godot-ocean-water-research.md) | §7.5 "Full-Nanite ocean 통합 = 장기 P5" | G4 이후 **필수** — 전면 deferred에서 오션 셰이딩 경로가 바뀐다 |
| [runtime-misc](./godot-runtime-gaps-misc-research.md) | §9 "모션블러·업스케일러는 §1 안정화 후" | **G4 이후**로 더 미룸 — deferred 전환이 모션벡터 경로를 바꾼다 |

---

## 13. 리스크 레지스터

| 리스크 | 확률 | 영향 | 완화 / 롤백 |
|--------|:----:|:----:|-------------|
| **GBuffer 스키마 재작업** | 중 | 🔴 4레인 | G1을 M4에 고정. superset 레이아웃으로 여유 채널 확보 |
| **DAG 품질 미달**(레벨 간 오차 단조성, 크랙-프리 cut) | 중 | 🔴 | meshopt 1.2로 하향됐으나 **여전히 1급 난제**. S1에 인력 집중, G3에서 냉정히 판정 |
| **전면 deferred의 투명·라이트맵 회귀** | 중 | 🟡 | 롤백 지점 = Hybrid B2(비-Nanite opaque는 포워드 유지, 프리패스만 GBuffer 확장) |
| **async compute 렌더그래프 재작업 실패** | 중 | 🔴 | Wave 4 최후 배치 — 실패해도 G4까지의 산출물은 살아있음 |
| **오디오 미들웨어 ↔ 콘솔 데드락** | 높 | 🔴 | M0 결정 강제. 폴백 = 자작 DSP(코어 0 확인됨) |
| **군중 L1(20~100) 성능 미달** | 중 | 🟡 | `Skeleton3D`×100 URO 틱 스로틀 벤치를 Wave 2에 선행. MultiMesh 스켈레톤(man-year)은 최후 |
| **HW-RT experimental API 불안정** | 중 | 🟡 | Phase 1(b)가 비-RT 티어를 계속 지원 → GI가 0이 되지 않음 |
| **인력 이탈(L1 크리티컬 패스)** | 중 | 🔴 | GBuffer/리졸브 설계를 문서화 강제(unified-gbuffer §8 수준의 file:line 원장 유지) |

---

## 14. 즉시 착수 액션 (M0 첫 2주)

- [ ] **L9** — Sentry SDK 통합 + CI 심볼 업로드 파이프라인 (2~5일). **다른 모든 것보다 먼저.**
- [ ] **L1** — 코어 훅 4건 브랜치 개설: 64b atomic(3-site) · `drawIndirectCount` 배선 · `maxStorageBufferRange` · heightfield getter
- [ ] **전 레인** — 통합 RD 능력 스파이크 착수 → `docs/rd-capability-spike-report.md`
- [ ] **L2** — S1 DAG 빌더 임포터 모듈 스캐폴딩 (렌더러 의존 0, 지금 바로 시작)
- [ ] **L3** — Lumen S0-1/S0-2 스파이크
- [ ] **L8** — 오디오 미들웨어 vs 콘솔 결정 회의 소집 (§11-1)
- [ ] **L1** — 전면 deferred의 투명·라이트맵·Forward Mobile 영향 스코핑 문서 (§11-6)
- [ ] **L5** — 글로벌 유니폼 상태 버스 + `.gdshaderinc` 컨벤션 확립 (기상 트랙 절반 해금)
- [x] **L5** — Jolt 벤더링 범위 확대 스파이크 — **완료(2026-08-25)**: 3폴더 벤더링+CPU 스모크(c4/l5-jolt-vendoring, 병합 대기). SoftBody 셀프 콜리전 = 소스 감사로 **미지원 확정**(자체 구현, L5). 부수 확정: Compute는 헤어 전용 추상화(§15-B 2차 정정)

---

## 15. 스코프 복귀 — "불필요/비권장" 판정 전수 재검토 (2026-08-25 확정)

> **사용자 최종 결정: "불필요한 건 없어, 전부 구현한다."**
> 19개 문서에서 배제 판정 전체를 수집해 **4종으로 분류**했다. 전부를 기계적으로 뒤집지 않은 이유는 (C)·(D)가 스코프 축소가 아니라 **수단 오류/이미 해결**이기 때문이다 — 이것까지 뒤집으면 없는 일을 만든다.

### (A) 비용회피로 뺀 것 → **스코프 복귀**

| 항목 | 기존 배제 근거 | 복귀 후 위치 |
|------|----------------|--------------|
| 물리 정확 다중산란 대기(4-LUT) | "스타일라이즈드엔 실수" | **L5 / Wave 2.** sky 적용까지 코어 0, 6~10주 |
| 근경 관통 볼류메트릭 구름 | "게임플레이에 없으면 0가치" | **L5 / Wave 3.** `POST_SKY`+`access_resolved_depth`로 코어 0, 2~4개월 |
| 거리필드/HW-RT 소프트 섀도우 | "lumen에 게이팅, 예산 밖" | **L3 / Wave 3.** SDFGI `sdf_tex`(`gi.h:581`) 재사용 |
| albedo·Cryptomatte 풀 AOV | "VFX 하우스급에만 정당화" | **L7 / Wave 3.** vis-buffer 부산물이라 **추가 비용 ≈ 0** |
| 회절·전파·실시간 리버브 자작 | "man-year급, 비권장" | **L8.** 단 §5 미들웨어 결정이 선행 — 미들웨어 탈락 시 유일 대안 |
| 파티클 → 월드 물리 피드백 | "원신도 안 한다" | **L5 / G5 이후.** async 리드백 비용이 내려간 뒤 |
| GPU 컬링 결과의 CPU 가시성 반영 | "대규모 코어, 비권장" | **L2.** Nanite 워크스트림에 흡수 |
| 프리인테그레이티드 SSS LUT · 스트랜드 헤어 | "스타일라이즈드엔 불필요" | **L6.** 이미 AAA 재채점에서 복귀 완료 |

### (B) "업스트림 대기"로 뺀 것 → **항목별로 갈린다 (2026-08-25 upstream 감사로 최종 확정)**

경위: 1차 재검토는 *"Jolt에 없는 게 아니라 Godot이 안 가져온 것 → 벤더링하면 끝"* 으로 세 항목을 일괄 반전했으나, **upstream 감사(C4)가 그 일괄 판정을 기각**했다 — `Jolt/Compute`는 헤어 전용 컴퓨트 추상화이고 GPU 게임플레이 물리는 upstream에도 없다. 항목별 최종 상태:

> 원 근거였던 `thirdparty/README.md`의 3폴더 제외 문구는 **Jolt 벤더링 브랜치(c4/l5-jolt-vendoring, master 병합 대기)에서 이미 갱신**됐다 — master의 :519는 병합 시점까지만 유효.

| 항목 | 기존 판정 | 재판정 |
|------|-----------|--------|
| **GPU 게임플레이 물리** | 🔴 P4 "자체 구현 금지, 업스트림과 충돌" | 🔴 **자체 솔버 (2026-08-25 재판정, C4 upstream 실측).** `Jolt/Compute`(51파일/4,967줄)는 범용 GPU 물리가 아니라 **헤어 솔버 전용 컴퓨트 추상화** — `Physics/` 전체에서 소비자가 `Hair/` 하나뿐, GPU 리지드바디/브로드페이즈/콜리전/소프트바디 전무. 벤더링으로 얻는 건 **추상화 토대**(외부 VkDevice 주입·순수가상 얼로케이터)이고 솔버는 우리가 쓴다. Wave 4(G5 이후) 배치 |
| **스트랜드 헤어 물리** | 🔴 P3 장기 "Jolt `Physics/Hair` 대기" | 🟡 **스코프 내.** 시뮬 = 벤더링(`Hair/` 8파일/2,611줄 실존 확인) / 렌더 = 스트랜드 래스터 + Marschner·Chiang BSDF(L1+L6). ⚠️ **벤더링 즉시 얻는 건 CPU 경로뿐**(`JPH_USE_CPU_COMPUTE` — upstream 주석 "debugging purposes, not optimized") — **GPU 경로는 G5(컴퓨트 큐) + HLSL→SPIR-V/metallib 오프라인 툴체인 신설 선행**(셰이더가 HLSL, upstream이 빌드 미제공, Godot에 DXC 의존 없음). **성숙도 캐비엇(C4, `Hair.h:22-32` upstream 자체 주석 "still in development"):** Wind forces 부재 → **바람 연동은 자체 구현**(Task #8 전역 바람 버스와 접점), LOD 부재, 충돌은 ConvexHullShape 한정, CPU/GPU 이중 저장 메모리 낭비 |
| **소프트바디 셀프 콜리전** | 🔴 "Jolt 업스트림 대기" | 🔴 **자체 구현 확정 (2026-08-25 실측, C4).** 번들 5.6.0의 `SoftBody/` 전체에서 셀프 콜리전 심볼 0건 — 있는 것은 `Shape::CollideSoftBodyVertices`(버텍스 vs 외부 shape) + `SoftBodyShape.cpp:118`(바디 A 버텍스 vs 바디 B shape)뿐, **인트라바디 경로 부재**. constraint도 Edge/DihedralBend/Volume/LRA/Skinned뿐. (B)에서 유일하게 벤더링으로 안 풀리는 항목 — L5 자체 구현, XPBD 셀프 콜리전 브로드페이즈 신설 |

### (C) 기술적으로 틀린 *수단* → 목표는 유지, **수단만 교체** (뒤집지 않음)

| 배제된 수단 | 왜 여전히 틀렸나 | 같은 목표의 올바른 수단 |
|-------------|------------------|------------------------|
| froxel 확장으로 구름층 커버 | froxel은 프러스텀 정렬 + `volumetric_fog_length` 기본 64m → **구름층 1,500~8,000m가 구조적으로 안 들어감** | `POST_SKY` 레이마치 (=(A)에서 복귀한 그 항목) |
| 바이노럴을 **버스 이펙트**로 | `AudioEffectInstance`가 채널쌍마다 독립 호출(`audio_server.cpp:356-361`) → 크로스채널 상태·per-source 방향 접근 불가 | `AudioStreamPlayback::_mix` GDVIRTUAL로 per-source 구현 |
| `BaseMaterial3D`에 젖음/바람 훅 이식 | 어차피 전 머티리얼이 커스텀 셰이더 라이브러리로 간다 → 이득 0 | 글로벌 유니폼 버스 + `.gdshaderinc`(Wave 1 L5) |
| 파티클 `amount` 동적 조절 | `set_amount`가 버퍼 재할당 → 히칭(`particles_storage.cpp:341`) | 강도별 emitter 프리셋 전환 |
| Nanite S2의 자체 deferred-lite 라이팅 | **스코프 축소가 아니라 중복 제거** — S4 리졸브에서 폐기될 코드 | S4 단일 deferred 리졸브 |
| 저폴리 아트 디렉션으로 물량 축소 | Nanite의 존재 이유와 **양립 불가**(nanite-impl §10.5) | — (fallback으로만) |

### (D) 이미 해결됐거나 대체됨 → **구현할 것이 없음**

셰이더 컴파일 스터터(해결 완료, 감시만) · 내비게이션 코어(가장 성숙한 서브시스템) · METIS 벤더링(`meshopt_partitionClusters`가 대체) · OpenEXR 벤더링(번들 tinyexr가 멀티파트 API 포함) · mesh shader 의존(Nanite는 compute 기반).

### 조건부 (게임 디자인 결정 대기)

| 항목 | 무엇에 달렸나 |
|------|---------------|
| 엔진 레벨 리플리케이션 그래프 | **멀티플레이 규모.** 4~8인 co-op이면 불요, 그 이상이면 스코프 내 |
| 롤백 넷코드 | 오픈월드에 구조적 부적합(결정론·리시뮬 비용)은 유효. 격투/대전 모드를 넣을 때만 |
| 파괴 Phase A+B(오프라인 프랙처) | 대규모 환경 파괴가 디자인에 있는가 |

### 스코프 증분 요약

| | 이전 | 이후 |
|---|---|---|
| 캘린더 | ~30개월 | **~33개월** |
| FTE | 14~17 | **16~19** |
| 주 증가 레인 | — | **L5**(GPU 물리·파티클 피드백) · **L6+L1**(스트랜드 헤어 시뮬+래스터) · **L8**(회절 자작 분기) |

~~**(B)가 증분을 크게 줄였다** — GPU 물리와 스트랜드 헤어 시뮬을 man-year 신규 개발로 잡았다면 +12개월이었을 것이 벤더링 작업으로 바뀌었다.~~ **[2026-08-25 재정정, C4 upstream 실측]** (B)의 절감은 **헤어 몫만 유효**하다. GPU 게임플레이 물리는 `Jolt/Compute`가 헤어 전용 추상화로 판명되어 **자체 솔버로 되살아났다** — 단 컴퓨트 추상화 토대·디바이스 공유 설계는 공짜로 확보되므로 완전 원점은 아님. Wave 4 배치로 캘린더 영향 없음(G5 이후 슬랙 구간).

---

---

*작성: 2026-08-25. 근거: `docs/` 19개 리서치 문서 교차 종합. 순서 결정의 1차 제약은 (1) §0 월드 스트리밍이 §1·§3의 선행조건 (2) Sentry가 §1 선행 (3) 통합 GBuffer가 §1·§2·§7 공유면 (4) async compute가 §1·§2 공유 게이팅 (5) VT/Lumen/Nanite 스파이크 3중 중복. 미해결: 전면 deferred 전환의 투명/라이트맵 회귀 견적(어느 문서에도 없음), L1 근거리 군중 벤치마크, 오디오 미들웨어-콘솔 양립 가능 조합.*
