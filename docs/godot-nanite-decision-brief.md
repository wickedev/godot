# Godot Nanite 구현 — 실행 결정 브리핑

> **목적:** "Godot에 Nanite을 만들 것인가, 만든다면 어디까지"를 결정하기 위한 1페이지 요약.
> **근거 문서:** 상세 기술·file:line 근거는 [godot-nanite-implementation-research.md](./godot-nanite-implementation-research.md), 넓은 격차 맥락은 [godot-openworld-engine-gaps.md](./godot-openworld-engine-gaps.md).
> **검증 수준:** 딥리서치(23/25 주장 적대적 검증) + Godot 4.8-dev(commit `eda2a482e9`) 소스트리 4회 직접 스파이크.

---

## 1. 한 줄 결론

**"불가능"이 아니라 "단계별 비용".** 코어 수정 0으로 실체 있는 MVP(경로 A)까지 갈 수 있고, 비용 절벽은 예상했던 SW 래스터가 아니라 **Godot 네이티브 라이팅 통합(경로 C)** 에 있다.

---

## 2. Go / No-Go — 3단계 경계

| 경로 | 결과물 | 코어 C++ 수정 | 규모감 | 결정 |
|------|--------|---------------|--------|------|
| **A** | HW 래스터 가상 지오메트리 + 자체 deferred-lite 셰이딩 오버레이 (Bevy 0.14 등가) | **0 — 순수 GDExtension** | 렌더 전문가 1인 수 개월 | ✅ **Go 권장** — 리스크 낮음, 되돌리기 쉬움 |
| **B** | + 마이크로폴리곤 SW 래스터(성능) | 소규모(Vulkan device-init 4곳) 또는 VulkanHooks 플러그인 | + 수 주~개월 | ⚠️ 조건부 — 커스텀 빌드 감수 시 |
| **C** | + Godot 네이티브 라이트/그림자/GI 완전 통합 | **대규모 코어/포크** | **수 man-year** | ✅ **목표** — 포크+딥코어 개조 전제 하에 유일한 종착점. A/B는 C로 가는 단계 |

---
>
> ⚠️ **2026-08-18 개정.** 위 표는 과거 "원신급 스타일라이즈드" 전제에서 작성됐다. AAA 기준(UE5.4+ / Horizon / CP2077 RT Overdrive) + full-Nanite 커밋(포크·업스트림 디스커넥트·deferred 전면 전환) 하에서 **C는 회피 대상이 아니라 목표**이며, A/B는 C의 단계적 검증 수단이다. "코어 수정 0"의 가치는 포크 전제에서 소멸했다. §3 재배치 표 참조.

---

## 3. 경로 A/B/C 재배치 — 선택지에서 단계로 (2026-08-18 개정)

> **[godot-nanite-implementation-research.md §10.3](./godot-nanite-implementation-research.md) 확정.** AAA 전제 + 포크 + full-Nanite 커밋 하에서 A/B/C는 **"어디서 멈출 것인가"의 선택지가 아니라 전부 통과 지점**이다.

| 단계 | 기존 이름 | AAA에서의 역할 | 코어 작업 | 공수 |
|------|-----------|----------------|-----------|------|
| S1 | (Phase 1) | 오프라인 DAG 빌더 — meshopt 1.2 단독 | 0 | 1~2개월 |
| S2 | 경로 A | **런타임 검증 하니스.** HW 래스터로 컬링·LOD 컷·HZB 조기 실증. **출하 대상 아님** | 0 | 1~2개월 |
| S3 | 경로 B | 64b image atomic + R64 vis-buffer + SW/HW 하이브리드 | **3-site 드라이버 패치** | 1~2주(코어) + 1~2개월(래스터) |
| S4 | **경로 C** | **목표.** vis-buffer → 통합 GBuffer → 단일 deferred 리졸브 | **대규모** | **6~12 man-month** |
| S5 | (Phase 4) | 스트리밍 + async compute 멀티큐 | **최대** | **12+ man-month** |

**S2의 자체 deferred-lite 라이팅은 짓지 말 것** — S4 리졸브에 버려질 코드다. A의 잔존 가치는 **조기 검증과 위험 조기 발견**뿐이다.

---

## 4. 왜 이렇게 갈리나 (핵심 3줄)

1. **RenderingDevice가 충분히 열려 있다.** indirect draw, 커스텀 raster 파이프라인, 프래그먼트 32비트 image atomic(`fragmentStoresAndAtomics` 활성, 코어가 이미 사용), vertex pulling, compute→indirect, 자동 배리어 — 경로 A의 6개 필수 능력이 전부 GDExtension 바인딩에 노출. meshoptimizer도 이미 번들.
2. **CompositorEffect가 씬 통합의 문을 반쯤 연다.** 커스텀 RD 패스 주입 + 씬 depth/color/gbuffer 읽기 + depth 기록 + tonemap 전 합성까지 GDExtension으로 됨. → 자체 조명으로 셰이딩하면 오버레이 완성.
3. **하지만 Godot의 조명은 못 빌려온다.** `RenderForwardClustered`가 바인딩 없는 내부 C++. 라이트 클러스터·섀도우·GI 유니폼셋을 외부에 안 줌 → 네이티브 조명 통합은 코어 개조 or 조명 전체 재구현. **이게 절벽.**

---

## 5. 경로 A MVP 아키텍처 스케치

```
[오프라인 / import 시]
 원본 메시
   └─ meshopt_buildMeshlets (~128 tri 클러스터)          ← thirdparty/meshoptimizer (번들됨)
   └─ METIS 그래프 분할로 클러스터 그룹핑
   └─ QEM 단순화(½) → 재분할 반복 → 클러스터 DAG        ← ★최난관: DAG 품질
   └─ 커스텀 리소스(.res)로 저장: 정점 SSBO + 클러스터 메타 + DAG

[런타임 / CompositorEffect._render_callback, PRE_OPAQUE]
 1. compute: DAG 컷 (클러스터 QEM 오차 → 스크린 픽셀 투영, 1px 기준 LOD 선택)
 2. compute: 2-pass HZB occlusion culling → 가시 클러스터 리스트
 3. compute: draw args 버퍼 채움 (STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT)
 4. raster: draw_list_draw_indirect
      - vertex_format=0, gl_VertexIndex로 정점 SSBO fetch (vertex pulling)
      - fragment: imageAtomicMax(r32ui) → visibility buffer (packed cluster+tri ID)
 5. compute/raster resolve: visibility buffer → 씬 depth 기록 + 자체 라이팅
      → 씬 color에 합성 (POST_OPAQUE, tonemap 전)

[스트리밍] (선택, 나중)
 단일 대형 영속 버퍼 + buffer_get_device_address 인덱싱 (bindless 없음)
 buffer_update(offset,size)로 페이지 부분 업로드
 buffer_get_data_async로 "requested pages" 논블로킹 리드백
```

**플랫폼:** Forward+ / 데스크톱 Vulkan·D3D12, Apple은 Metal 백엔드(Apple6+/macOS14+). 런타임 `has_feature(SUPPORTS_IMAGE_ATOMIC_32_BIT)` 분기 필수. Forward Mobile·Compatibility·web 타깃 불가.

---

## 6. 리스크 & 미확정

| 항목 | 리스크 | 완화 |
|------|--------|------|
| **DAG 품질** | 🟡 중간 (하향) | 번들 meshopt 1.2가 공간 인지 파티셔너(`meshopt_partitionClusters`) 제공. METIS 불필요. "동작"과 "품질" 목표 분리. Bevy/jglrxavpok 참조 |
| 자체 라이팅 품질 | 🟡 중간 | deferred-lite로 시작, Godot 조명과 비주얼 매칭은 별도 튜닝 |
| 스트리밍 stall | 🟡 중간 | 업로드량을 스테이징 링 예산 이하로. 대형버퍼 max 쿼리 API 없음(플랫폼 한도 하드코드) |
| bindless 부재 | 🟢 낮음 | 단일버퍼+device-address 방식으로 회피 |
| 인력·기간 정량 | ⚪ 미확정 | Bevy(숙련 1인, 다수 릴리스, 여전히 진행)가 유일 벤치마크 |

---

## 7. 권고 실행 순서

1. **결정 스파이크 (완료됨)** — 64b atomic·GDExtension 경계·통합·크로스플랫폼 4건 확인 끝. → 결과: 경로 A는 코어 0.
2. **오프라인 DAG 빌더 PoC** — meshoptimizer+METIS를 GDExtension에 링크, 단일 스태틱 메시로 DAG 빌드·시각화. (품질의 8할이 여기)
3. **경로 A 런타임 PoC** — CompositorEffect + compute cull + indirect HW raster + visibility resolve, 자체 조명 오버레이. 코어 수정 없이 화면에 띄우기.
4. **판단 분기** — PoC 결과로 S3(SW 래스터)·S4(네이티브 통합) 진입 시점 결정. **AAA 기준(UE5.4+/Horizon/CP2077)에서는 S4가 목표, S2는 검증 하니스일 뿐 출하 대상이 아니다.**

---

## 8. 최종 판단 (2026-08-18 개정)

- **AAA(UE5.4+ / Horizon / Cyberpunk RT Overdrive) 목표라면** → **S4(C)가 목표, S5(스트리밍+async compute)가 완성.** man-year+급 코어/포크는 이미 승인된 예산이다. **"C는 비권장" 판정 취소.**
- **A/B는 C의 단계적 검증 수단** — S2(자체 deferred-lite)는 출하 대상이 아니다. S2의 조기 검증 가치는 S4 아키텍처를 확정한 뒤 그 안에서만 발휘된다.
- 어느 쪽이든 **오프라인 DAG 품질이 성패를 가른다** — 여기에 노력을 집중할 것. 단, meshopt 1.2(번들)가 공간 인지 파티셔너(`meshopt_partitionClusters`)를 제공해 METIS 의존성이 제거됐고, DAG 최난관 등급은 "🔴 최난관"에서 "🟡 1급 난제(벤더 지원 있음)"으로 하향 조정.
- ~~원신 그 자체를 목표로 한다면~~ → **[폐기]** 원신은 하한 참조점일 뿐 목표가 아니다.
- ~~스타일라이즈드 오픈월드에서 "먼 거리 고밀도 지오메트리" 오버레이가 목표라면~~ → **[폐기]** AAA 목표에서 이 선택지는 Nanite의 존재 이유를 부정한다. §10.5 참조.

---

## 2026-08-18 개정 — AAA 기준 재채점

### 델타 표

| 항목 | 기존 결론 | AAA 재채점 | 이유 |
|------|-----------|-----------|------|
| **경로 C** | "❌ 신중 — 원신급 아닌 한 비권장" | **✅ 목표.** 유일한 종착점 | full-Nanite + 포크 커밋. man-year는 승인된 예산 |
| **경로 A** | "✅ Go 권장 — 리스크 낮음, 되돌리기 쉬움" | **경유지.** 출하 대상 아님 | "코어 수정 0"의 가치가 포크 전제에서 소멸 |
| **경로 B** | "⚠️ 조건부 — 커스텀 빌드 감수 시" | **✅ 무조건 포함.** 3-site 패치, 1~2주 | 포크라 "커스텀 빌드 감수"가 비용이 아님 |
| **A/B/C 관계** | "어디서 멈출 것인가"의 선택지 | **S1→S2→S3→S4→S5** 전부 통과 지점 | §3 재배치 |
| **스타일라이즈드 축소** | "A에서 멈추는 것도 합리적" | **폐기.** AAA 목표와 양립 불가 | 전제 취소 |
| **DAG 품질** | "🔴 최난관. Bevy도 미완" | **🟡 하향.** meshopt 1.2가 공간 파티셔너 제공 | `meshopt_partitionClusters` + `simplifyWithAttributes`(vertex_lock) |
| **METIS 의존성** | 필요 | **불필요.** meshopt 1.2로 대체 | 번들 라이브러리 단독 사용 |
| **man-year 무게중심** | "C(라이팅 통합)" | **async compute 렌더그래프 재작업**으로 이동 | 라이팅은 `_inc.glsl` 재사용. 진짜 비용은 단일 큐 해체 |
