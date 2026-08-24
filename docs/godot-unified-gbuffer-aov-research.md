# Godot 포크 — 통합 GBuffer / AOV 서브시스템 딥리서치

> [교차 트랙 충돌 #4] "Nanite Path-C deferred 리졸브 vs VFX AOV"가 forward-clustered MRT 프레임버퍼 + `MODE_RENDER_MATERIAL` emit 블록을 **각자 확장**하려는 중복. 사용자 결정([[nanite-openworld-strategic-decisions]]: 포크 + 업스트림 디스커넥트 + full-Nanite 커밋)에 따라 **하나의 통합 GBuffer/AOV 서브시스템으로 흡수**하기 위한 리서치.
>
> 방법: 3개 병렬(Godot 통합 스코프 설계 · 외부 deferred GBuffer-as-AOV · VFX AOV/#7916 정합). Godot 4.8-dev `eda2a482e9` 실측 + UE/Unity/Substrate 1차소스 교차검증.

---

## 0. 핵심 결론

**deferred에서 지오메트리 AOV는 GBuffer 그 자체다** — 별도 MRT 레이아웃이 아니라 동일 메모리. UE의 buffer-visualization·render-pass가 라이팅이 소비하는 바로 그 GBuffer를 읽는다(interplayoflight·lordned·Unity URP 3소스 교차확인). 따라서 Nanite deferred 리졸브와 VFX AOV는 **원래 하나의 서브시스템**이며, 충돌은 "둘이 같은 걸 각자 지으려 해서" 생긴 것.

**결정적 재정 — full-Nanite 커밋 하에서는 "opaque 전면 deferred"가 정답이다.** 소스 에이전트는 리스크를 이유로 Hybrid B2(비-Nanite opaque는 포워드 beauty 유지 + 프리패스만 GBuffer로 확장)를 권고했으나, 그 논거("포워드는 어차피 투명/라이트맵 때문에 살아남으니 전면 deferred 이득이 미미")는 **사용자 결정이 제거한 제약을 최적화한 것**이다. 아래 §3에서 재정.

---

## 1. 충돌의 정확한 공유면 (왜 통합이 중복을 제거하나)

Nanite Path-C와 VFX AOV가 독립적으로 건드리려는 **동일한 두 지점**:

**(a) forward-clustered MRT 컬러 프레임버퍼 확장** — `get_color_pass_fb`(`render_forward_clustered.cpp:179-206`) + 파이프라인 포맷(`:4516-4565`). 둘 다 albedo/ORM/emission/objectID 어태치먼트 + 새 `COLOR_PASS_FLAG_*`를 추가하려 함.

**(b) `MODE_RENDER_MATERIAL` emit 블록 전용** — `scene_forward_clustered.glsl:1037-1045`(선언) / `:2998-3014`(기록). 오늘은 VoxelGI 동적 베이크(`gi.cpp:3279`)·UV2 라이트맵 베이크만 이 5-MRT(albedo/normal/orm/emission/depth)를 스크린이 아닌 오프라인 FB로 emit. 둘 다 이걸 **스크린스페이스 MRT로 전용**하려 함.

→ 통합 = **하나의 스크린스페이스 머티리얼-emit 경로**(단일 `MODE_RENDER_GBUFFER` 변종 + 확장된 `get_gbuffer_fb` + superset 플래그). Nanite 리졸브는 이걸 머티리얼 G-buffer로 읽고, VFX는 같은 어태치먼트를 `CompositorEffect`+`texture_get_data`로 읽어 multipart EXR로 팩. 각자 어태치먼트 추가·변종 등록·instance-ID 배선하던 걸 **한 번만** 한다.

---

## 2. 통합 레이아웃 — Tier 모델 (외부 SOTA + Godot 스코프)

AOV는 3계열로 갈리며 **오직 지오메트리 계열만 GBuffer로 통합**된다(어느 엔진도 라이팅 분해 AOV는 GBuffer에 안 들어감 — UE도 AO/SSS AOV 미지원).

### Tier 0 — Visibility buffer (항상, Nanite)
- packed `instanceId | triangleId` (+depth). 실시간 컬링/셰이딩 드라이버 **이자** ObjectID/Cryptomatte instance ID의 무료 소스.

### Tier 1 — 코어 GBuffer (항상, 얇게, 대역폭-critical) = `RB_SCOPE_GBUFFER` 신설
`RenderBufferDataForwardClustered`(`render_forward_clustered.cpp:280`) 패턴 그대로 `RB_SCOPE_GBUFFER` 스코프 + `ensure_gbuffer()`(모델 `ensure_normal_roughness_texture :63-73`).

| RB tex | 포맷 | 채널 | 비고 |
|--------|------|------|------|
| `gb_albedo` | RGBA8 (베이크) / **RGBA16F 런타임** | albedo.rgb + alpha | 라이팅 + Albedo AOV |
| `gb_normal` | **RG16 oct (R8G8B8A8에서 정밀도 상향)** | world normal | 스페큘러 정밀도 위해 상향 필수(`glsl:3003` encode24 대체) |
| `gb_orm` | RGBA8 | ao/rough/metal/sss | `glsl:3007-3010` 그대로 |
| `gb_emission` | RGBA16F | emission | `gi.cpp:2767` 포맷 |
| `gb_depth` | R32F view-Z | 위치 재구성 | `glsl:3005` `-vertex.z` |
| **`gb_objectid`** | **R32_UINT (신설)** | instance/object ID | §4 — 킬러 채널 |
| `gb_motion` | RG16F (기존 `motion_vector` `glsl:1069` 재사용) | 스크린 모션 | TAA + Motion AOV |

**모든 Tier-1 타깃이 지오메트리 AOV를 겸한다 — 추가 패스 0.** 이게 통합의 페이오프.

### Tier 2 — 선택 AOV (export 시에만 할당)
Cryptomatte 커버리지(Tier-0 instance field 해시), worldpos(depth 재구성), 추가 머티리얼 채널(anisotropy/clearcoat — Substrate식 per-pixel 비트스트림). **"export/compositing active" 플래그로 게이트** → 실시간 프레임은 대역폭 안 냄.

### Tier 3 — 라이팅 분해 AOV (패스 기반, GBuffer 아님)
diffuse/spec/reflection/AO/SSS 라이팅. **deferred 리졸브 패스에서 tap** — GBuffer 채널이 될 수 없음(어느 엔진도 안 됨). optional light-pass write-out으로 설계.

**공유 채널(TAA+오프라인 동일 버퍼):** depth·velocity·specular는 UE에서도 실시간 TAA와 오프라인 컴포지팅이 **같은 단일 리소스**를 쓴다. "AOV용 velocity"와 "TAA용 velocity"가 따로 없음 → Godot도 `gb_depth`/`gb_motion`/specular 하나로.

---

## 3. 하이브리드 vs 전면 deferred — 재정 (크럭스)

**소스 에이전트 권고(B2):** 비-Nanite opaque는 포워드 beauty 유지, 이미 도는 depth/normal-roughness 프리패스(`render_forward_clustered.cpp:218-233`, emit `glsl:3016-3023`)를 풀 GBuffer로 확장해 AOV만 채움. 이유: 포워드는 투명/라이트맵 때문에 어차피 살아남으니 전면 deferred 이득 미미, 저리스크.

**재정 — full-Nanite 커밋 하에선 opaque 전면 deferred가 맞다.** B2 논거는 **사용자 결정이 제거한 제약**(포워드 opaque 보존·리스크 회피)을 최적화한 것:

1. **라이팅 경로 수:** full-Nanite면 opaque 대부분이 이미 Nanite vis-buffer 리졸브(=deferred)로 감. 선택은 "비-Nanite **opaque**를 포워드로 둘 것인가"뿐. B2는 여기서 포워드를 유지 → **3개 라이팅 경로**(Nanite-deferred + 포워드-opaque + 포워드-투명) 유지. 전면 deferred는 **2개**(opaque-deferred + 포워드-투명). B2가 오히려 유지보수 **악화** — 소스 에이전트의 "포워드 어차피 살아남음" 논거가 여기서 역전.
2. **심(seam) 리스크:** B2는 비-Nanite opaque(포워드-lit)와 Nanite(deferred-lit) 두 beauty 경로 → 미묘한 BRDF/그림자 불일치. 전면 deferred는 **모든 opaque가 동일 코드로 lit** → 심 없음.
3. **한계비용이 작다:** deferred 라이팅 리졸브는 **Nanite가 어차피 짓는다**(Path C). 비-Nanite opaque를 여기 편입하는 추가비용 = "프리패스를 풀 GBuffer로 확장"(B2가 이미 하는 것) + "라이팅을 리졸브로 이동"뿐. **B2의 프리패스 확장 메커니즘이 곧 전면-deferred의 비-Nanite-opaque GBuffer emit 구현**이다 — 두 에이전트 작업이 충돌이 아니라 합성됨.
4. **AA:** full-Nanite가 이미 TAA 강제 수용. 전면 deferred + TAA는 깔끔. B2의 포워드-opaque MSAA 여지는 오히려 "한 프레임 2 AA 레짐" 문제를 부활.

**따라서 확정:**
- **opaque → 전면 deferred.** Nanite opaque는 vis-buffer 리졸브로, 비-Nanite opaque(스킨드/특수)는 래스터 GBuffer emit(=확장된 프리패스, `MODE_RENDER_GBUFFER`)으로 → **동일 `RB_SCOPE_GBUFFER`** → **단일 deferred 라이팅 리졸브** 1개가 둘 다 셰이딩.
- **투명 → 포워드** (deferred가 OIT 불가 — UE 포함 모든 엔진 동일. opaque-deferred에 대한 반례 아님). 포워드 라이트 경로는 **투명 전용으로만** 잔존.
- **라이트맵/SH:** deferred 리졸브에서 `gb_objectid`로 인스턴스의 라이트맵 바인딩 조회(§4가 가능케 함). UE의 shading-model-ID + custom-data 채널 방식.

이건 외부 에이전트의 "go fully deferred = AOV 완결성의 정석 해결"과 정합하며, full-Nanite 결정의 자연스러운 귀결이다.

---

## 4. ObjectID — 킬러 공유 채널

오늘 스크린스페이스 instance/object ID 타깃 **없음**. 하지만 배선은 완비: `instance_index`가 버텍스에서 계산(`glsl:769-771`)돼 `flat`으로 프래그먼트에 전달(`layout(location=10) out flat uint instance_index_interp` `glsl:135`, 수신 `:917`, 사용 `:1203`). **출력 write 하나면 됨.**

- `gb_objectid` = **R32_UINT** 신설. emit 블록(`glsl:2998-3014`)에 `objectid_output = instances.data[instance_index].object_id` 추가. `InstanceData`(`scene_forward_clustered_inc.glsl:27`)에 안정적 `object_id` 필드 라우팅(CPU측 `GeometryInstanceForwardClustered`가 RID 보유).
- **Nanite 리졸브**는 vis-buffer(cluster/tri → instance)에서 **동일 R32_UINT** 채널에 기록 → deferred 라이팅·Cryptomatte가 **어느 경로가 픽셀을 만들었든 하나의 정규 채널**을 읽음.
- **Cryptomatte 거의 무료:** instance ID는 vis-buffer 부산물로 공짜(Filmic Worlds). 유일한 추가비용은 **AA 엣지 커버리지 랭킹**(단일-ID AOV는 AA/모션 엣지에서 "거짓말" — cglounge/Notch) — 기존 temporal/MSAA 샘플로 누적. UE의 Object-Ids 패스는 재래스터가 필요한데 vis-buffer는 불필요 → **구조적 우위**.

**이 채널이 Nanite deferred 픽셀과 (비-Nanite) 래스터 opaque 픽셀을 downstream 라이팅·컴포지팅에 구별불가로 만든다** — 통합의 심장.

---

## 5. #7916 "Rendering Compositor" 정합

reduz #7916(2023-09, "Needs consensus"): opaque를 순차 커스텀 패스로 분할, 머티리얼이 `compositor_opaque_pass N`으로 자가할당, **커스텀 버퍼 최대 4개**(R8~RGBA32F). `CompositorEffect`(패스 **사이** 코드 삽입, 이미 shipped)와 병존 설계.

**정합 판정:** #7916는 **신규 채널의 write-side 컨테이너로는 sanctioned 방향이나 서브시스템 전체로는 불충분** — 4버퍼 캡이 풀 시네마틱 AOV 셋(albedo+ORM+emission+objectID+lighting-only)에 부족하고, 선형화·multipart-EXR·MovieWriter 누적은 다루지 않음.
- **채택:** 코어 GBuffer는 **first-class `RB_SCOPE_GBUFFER` 스코프**로 짓는다(#7916 4버퍼 모델에 끼워넣지 않음 — 포크라 자유). #7916의 `compositor_opaque_pass N` 머티리얼-라우팅 문법은 **유저 저작 추가 AOV**(게임별 출력)용으로만 채택. 할당/read-side는 기존 `CompositorEffect`의 `needs_*` 플래그 모델(`compositor.h:59-61` → `render_forward_clustered.cpp:1731-1733`) 재사용.

---

## 6. 기존 버퍼 재사용 맵 (중복 금지)

통합 레이아웃은 아래 기존 RB 텍스처를 **재선언 말고 alias/재사용**:

| 기존 버퍼 | ensure fn / 플래그 | AOV |
|-----------|-------------------|-----|
| color `RB_TEX_COLOR` | 항상 | Beauty |
| depth `RB_TEX_DEPTH` | 항상 (`access_resolved_depth` MSAA resolve) | Depth (+linear 재구성) |
| normal_roughness R8G8B8A8 | `ensure_normal_roughness_texture :63` / `needs_normal_roughness` | Normal(+rough .a) — **oct 상향해 `gb_normal`로 승격** |
| specular R16G16B16A16F | `ensure_specular :51` / `needs_separate_specular` | Specular |
| velocity R16G16F | `ensure_velocity`(호출 `:194`) / `needs_motion_vectors` | Motion → `gb_motion` |

opaque 패스는 **이미 MRT**(`get_color_pass_fb` `[color, specular?, velocity?, depth]` `:179-204`) → 통합은 이 기존 MRT를 베이스로 확장. "재사용 가능한 여섯"(color/depth/normal_roughness/specular/velocity/linear-depth)은 `needs_*` 계약으로 그대로 공유, **신규(albedo/ORM/emission/objectID/lighting-only)만 한 번 추가**.

---

## 7. MovieWriter / EXR export 경로 (포크에서 자유 개조)

- **캡처 게이트(destructive):** `movie_writer.cpp:234-238`이 HDR-2D 뷰포트를 무조건 `convert(RGBA8)`+`linear_to_srgb`로 붕괴. `write_frame(Ref<Image>)`는 **프레임당 단일 이미지** 하드와이어(`:247`).
- **EXR 캡:** `image_saver_tinyexr.cpp:164` `max_channels=4` + single-part. 번들 tinyexr에 multipart API(`SaveEXRMultipartImage*`) 이미 존재 → 래퍼 우회면 됨.
- **코어 변경(포크라 즉시 가능):** (1) `:234` 선형-HDR 게이트를 "writer가 linear HDR 원함" 플래그로 → RGBAH 보존, (2) `:164` multipart API 직접 호출로 4채널 캡 우회, (3) `write_frame` 시그니처를 N-AOV/타일로 확장.
- **통합 GBuffer → multipart EXR:** post-transparent **빌트인 CompositorEffect**가 `RB_SCOPE_GBUFFER` 채널을 `texture_get_data`로 CPU 스테이징에 복사 → MovieWriter가 named layer로 muxing. read-side는 이미 충분(`RenderDataRD::get_render_scene_buffers` `render_data_rd.h:43`, 콜백 `renderer_scene_render_rd.cpp:311-316`, `get_texture(context,name)` `render_scene_buffers_rd.cpp:52`).

---

## 8. 코어 변경 지점 (file:line 요약)

- **셰이더 emit:** `scene_forward_clustered.glsl:1039-1045` & `:2998-3014`(채널 추가 + normal 정밀도 상향) + `:135/774/917`(objectID) + `:3016-3023`(프리패스 emit → 풀 GBuffer 승격).
- **InstanceData:** `scene_forward_clustered_inc.glsl:27`에 안정 `object_id`.
- **RB 스코프:** `render_forward_clustered.h:49-56` 신규 defines + `ensure_gbuffer()`(모델 `:63-73`).
- **FB/포맷:** `:179-206` 모델 새 `get_gbuffer_fb`, `:218-233`(프리패스 FB 확장), `:470-479`(파이프라인 버전), `:4516-4565` 새 `_get_gbuffer_framebuffer_format_for_pipeline`.
- **셰이더 변종:** `scene_shader_forward_clustered.cpp:650-655`에 `MODE_RENDER_GBUFFER`(+multiview twin).
- **드라이버:** `_render_material`(`:2958-3018`)의 런타임 변종 → 비-Nanite opaque GBuffer emit; 새 deferred 라이팅 리졸브(Nanite와 공유); opaque 라이팅 루프(`glsl:1500-2900`) 리졸브로 이관.
- **read/export:** `render_data_rd.h:43`, `renderer_scene_render_rd.cpp:311-316`, `movie_writer.cpp:234-247`, `image_saver_tinyexr.cpp:164`.

---

## 9. 통합이 제거하는 것 / 교차 트랙 함의

- **Nanite Path-C ∩ VFX AOV 중복 제거:** 하나의 `MODE_RENDER_GBUFFER` + 하나의 `RB_SCOPE_GBUFFER` + 하나의 deferred 리졸브. Nanite는 리졸브 소비자, VFX는 export 소비자.
- **`gb_objectid`가 Nanite(vis-buffer 재구성)·비-Nanite(instance_index)·Cryptomatte를 하나로.**
- **단일 deferred 라이팅 리졸브가 opaque 전체를 셰이딩** → §2.3-2.4 nanite-perf 문서의 froxel-재사용 deferred 라이팅과 동일 물건. reduz 공식 GPU-driven deferred 방향과 수렴.
- 남는 조율: 이 리졸브가 바인드하는 private set 0/1 레이아웃을 Nanite·Lumen 컴퓨트 패스가 바이트-동일하게 선언해야(공유 include).

---

*리서치: 3 병렬(Godot 통합 스코프·외부 GBuffer-as-AOV·VFX/#7916). Godot 4.8-dev `eda2a482e9` 실측 + UE/Unity/Substrate/Nanite/Cryptomatte 1차소스. 미해결: 라이트맵/SH의 deferred 편입(shading-model-ID + custom-data 채널) 상세, Cryptomatte 커버리지 랭킹의 정확한 temporal 누적 비용.*
