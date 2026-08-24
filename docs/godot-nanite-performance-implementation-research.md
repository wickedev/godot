# Godot Nanite급 가상 지오메트리 — 성능·최적화 우선 정석 구현 딥리서치

> [godot-nanite-implementation-research.md](./godot-nanite-implementation-research.md)에서 "Path C = 엔진 내부에 deferred-materials 리졸브 신설"로 결론난 뒤, **부재한 서브시스템 전체를 신설**하기로 결정. 이 문서는 그 신설을 **성능·최적화 우선 + 정석 구현(몽키패칭 배제)** 관점에서 6개 스테이지로 딥리서치한 결과다.
>
> 방법: 6개 스테이지(래스터·컬링/LOD·머티리얼 리졸브/분류·deferred 라이팅/AA·스트리밍/메모리·RD 능력 감사)를 병렬 리서치. 외부 SOTA(Karis SIGGRAPH 2021, UE 5.0→5.4 셰이딩, Bevy/jms55, Schütz HPG 2022, Wihlidal, elopezr, Filmic Worlds) + Godot 4.8-dev(`eda2a482e9`) 소스트리 실측 교차검증. 모든 file:line은 트리 실측.

---

## 0. 헤드라인 — 놀라울 만큼 많은 것이 이미 있다

정석 구현의 좋은 소식: **신설 대부분은 이미 노출된 RenderingDevice API 위에 GDExtension/셰이더 레벨로 지을 수 있다.** 진짜 코어 C++ 개조가 필요한 곳은 **성능을 게이팅하는 소수 지점**에 집중된다.

**코어 수정 없이 오늘 지을 수 있는 것 (실측 확인):**
- GPU-driven 컬링 풀넬 전체 — 자동 렌더그래프가 `compute→indirect→draw` 해저드 체인을 **자동 동기화**(`rendering_device.cpp:6266, 7067`). cull→dispatch args 기록→dispatch→draw args 기록→draw를 **엔진 수정 0**으로 표현 가능.
- 2-pass HZB 오클루전, DAG LOD 컷(Bevy식 indirect-dispatch), 서브그룹 ballot/arithmetic 컴팩션(`cluster_render.glsl:68-148`이 실사용), buffer device address(`:1415`), PSO 캐시 + specialization constant, 버퍼 서브레인지 업데이트(`buffer_update :1160`, 재생성 안 함), persistent-mapped 업로드(`BUFFER_CREATION_DYNAMIC_PERSISTENT_BIT :1084`), 논블로킹 리드백(`buffer_get_data_async :1329`).
- **Deferred 라이팅의 ~80%** — 팻 GBuffer emit 경로가 이미 존재(`MODE_RENDER_MATERIAL`, `scene_forward_clustered.glsl:1039-1045`), froxel 라이트 루프가 화면좌표+깊이만으로 동작(`cluster_get_item_range :1163`)해 컴퓨트에서 재사용 가능, TAA/지터/모션벡터 인프라 완비(`taa_resolve.glsl`, `renderer_scene_cull.cpp:2685`).

**즉 신설의 성격은 "렌더러를 새로 쓴다"가 아니라 "이미 있는 조각들을 vis-buffer 파이프라인으로 재조립 + 성능 게이팅 코어 훅 소수 추가"다.**

---

## 1. 코어 엔진 개조 목록 — 성능 영향 순 (정석 구현의 backbone)

각 항목: 상태 / 몽키패치 대안을 왜 버리는가 / 정석 변경 지점 / 성능 의미.

### ① 🔴 Async compute + 멀티 큐 — **부재 (최대 구조적 천장)**
- **상태:** Godot는 패밀리당 큐 1개 하드코딩(`rendering_device_driver_vulkan.cpp:1313`, `max_queue_count_per_family = 1`). 모든 그래픽스+컴퓨트가 `main_queue` 하나로 직렬화(`rendering_device.cpp:8393-8528`). async **transfer**는 있으나 async **compute**는 없음. 배리어는 큐 소유권 이전 없이 `VK_QUEUE_FAMILY_IGNORED` 하드코딩(`:2927,2945`).
- **왜 중요:** Nanite급은 컬링/인스턴스컬/HZB/컴퓨트 래스터를 그래픽스와 **오버랩**해 GPU를 포화시킨다. 단일 큐 직렬화는 이 파이프라인 처리량의 **구조적 상한**이며 최대 격차.
- **정석 변경:** (a) 큐 카운트 파라미터화(`:1313`)+전용 compute 패밀리 선택(`command_queue_family_get :3114`), (b) `compute_queue` 생성(`~:8412`), (c) 최난관 — `rendering_device_graph.{h,cpp}`에 2번째 커맨드 스트림 스케줄링 + 큐 소유권 이전 배리어(`:2927/2945` 필드 채우기) + 크로스큐 동기화. **타임라인 세마포어(⑤) 선행 필요.**

### ② 🔴 64비트 이미지 atomic — **부재 (SW 래스터/vis-buffer 인에이블러)**
- **상태:** `shaderInt64`(산술)만 활성(`rendering_device_driver_vulkan.cpp:872`). `VK_EXT_shader_image_atomic_int64`/`VK_KHR_shader_atomic_int64` 미요청·미체인. R64_UINT 포맷 계층·`TEXTURE_USAGE_STORAGE_ATOMIC_BIT`는 완비(`rendering_device_commons.h:193, 406`).
- **⚠️ 정정(중요):** `shaderImageInt64Atomics`는 **`VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT`에만** 있다 — Vulkan 1.2 core(`Vulkan12Features`)에는 **buffer/shared int64 atomic만** 있고 image는 없다. 이전 문서가 둘을 혼동했음. 이거 틀리면 기능이 조용히 안 켜진다.
- **몽키패치 배제:** `VulkanHooks::create_vulkan_device`(`:1497`)로 device feature를 가로채는 건 포크-로컬 핵. 정석은 드라이버 자체 수정.
- **정석 변경(3-site):** ext 등록(`_initialize_device_extensions ~:562-618`, optional) → `VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT` 쿼리 pNext 체인(`_check_device_capabilities ~:906-1010`) → device-create pNext 체인 활성(`:1443-1490`) → `SUPPORTS_IMAGE_ATOMIC_64_BIT` 플래그(`rendering_device_commons.h:1032` + 3 백엔드 `has_feature`). **포맷 게이트 필수:** R64_UINT는 feature 활성 후에만 `STORAGE_IMAGE_ATOMIC_BIT`를 광고(`:2730`).
- **폴백 패턴 실재:** `fog.cpp:53-71`이 `has_feature`로 4개 셰이더 변종 선택 — 그대로 복제(64b atomic 변종 + R32G32 split-atomic 폴백). Metal은 image int64 atomic 없음 → false 반환·폴백 강제.
- **성능:** 단일 atomic으로 depth+ID 패킹(30b+34b 또는 32b+32b) → UE의 2×32b split 대비 atomic 트랜잭션 절반 + 티어링 제거. 마이크로폴리곤 SW 래스터가 HW 대비 ~3배.

### ③ 🟠 Descriptor indexing / bindless — **부재 (GPU-driven 머티리얼 바인딩)**
- **상태:** bindless/descriptor_indexing 0 매치. device-create가 `Vulkan11Features`만 체인(`:1447`), `Vulkan12Features` 자체를 안 넘김 → core-promoted 필드도 비활성. dynamic indexing만 있음(`:865-868`).
- **성능 뉘앙스(중요):** **지오메트리는 bindless 불필요** — 단일 대형 버퍼 + offset/BDA가 오히려 빠름(Nanite도 선택적으로 이 방식). bindless가 필요한 건 **머티리얼 텍스처 팬아웃**뿐이고, **vis-buffer + 고정 샘플러 배열 nonuniform 인덱싱**이 정석 회피로다. 따라서 v1엔 게이팅 아님, 중기 과제.
- **정석 변경:** `Vulkan12Features` 활성(`:1447`) + UAB(update-after-bind)/partially-bound/variable-count 디스크립터 풀 + RD 추상화에 bindless 유니폼 타입(`rendering_device_commons.h` + 셰이더 컨테이너 리플렉션). 대형 프로젝트.

### ④ 🟠 drawIndirectCount — **부분 (드라이버는 완성, 상위 미배선)**
- **상태:** 드라이버 3백엔드 전부 구현됨(Vulkan `vkCmdDrawIndirectCount` `:5755/5768`, Metal `metal3_objects.cpp:1330`, D3D12 `:4928`), `multiDrawIndirect` 활성(`:844`). **하지만** 렌더그래프·public API에 미노출 — `draw_list_draw_indirect`(`:9161`)는 CPU 고정 `draw_count`만 받음.
- **정석 변경(≈80줄, 기계적):** `rendering_device_graph.{h,cpp}`에 `DrawIndirectCount` instruction + `add_draw_list_draw_indirect_count`(두 버퍼를 `INDIRECT_BUFFER_READ`로 등록) → `RenderingDevice::draw_list_draw_indirect_count` → ClassDB 바인드. 드라이버가 이미 있어 쉬움.
- **성능:** GPU가 draw 카운트 결정. 없으면 max 카운트 over-draw(instanceCount=0 낭비) 또는 CPU 리드백(풀스톨). Nanite 스케일(수십만 잠재 draw)에선 실질 커맨드프로세서 오버헤드.

### ⑤ 🟠 타임라인 세마포어 — **부재 (①의 선행조건)**
- 바이너리 세마포어 + VkFence만. 크로스큐 스케줄링에 필요 → ①과 함께 구현.

### ⑥ 🟡 #99750 — RD 스레드 어피니티 가드 (스트리밍)
- **상태:** RD 진입점이 뮤텍스가 아니라 **호출 스레드 하드체크**(`rendering_device.cpp:56-57`, `render_thread_id != caller`면 `ERR_UNAVAILABLE` 하드 no-op). 백그라운드 스레드 RD 호출은 "가끔 되는" 게 아니라 **아예 안 됨**.
- **정석 회피(당장):** 모든 RD 터치를 `call_on_render_thread(Callable)`(`rendering_server.h:1044`, 실행 `rendering_server_default.cpp:464`)로 렌더 스레드에 마샬링. 워커 스레드는 **디스크 IO + 디코드-prep만**. 큐 홉 1회(서브마이크로초), 스톨/레이스 없음.
- **정석 코어 훅(업스트림 기여):** 기존 transfer-worker(`_acquire_transfer_worker :855-877`, 자체 뮤텍스 `operations_mutex`)를 backing으로 **가드-완화 `buffer_update_async`** 노출 — 스레드 어피니티가 아니라 transfer-worker 뮤텍스만 assert. 이게 #99750이 요구하는 sanctioned 경로. **랙: `render_thread_id`를 워커에서 뒤집는 건 커맨드스트림 손상 → 배제.**

### ⑦ 🟡 `maxStorageBufferRange` LIMIT 노출 — 한 줄
- 현재 바이트-레인지 한도 쿼리 부재(`rendering_device_commons.h:980`엔 카운트만). 풀 샤딩(§6-R2) 크기 결정에 필요. `VkPhysicalDeviceLimits::maxStorageBufferRange` 한 줄 노출. 없으면 128MB 안전 하한 하드코딩.

### ⑧ 🟡 서브그룹 사이즈 컨트롤 — 부분 (튜닝)
- 쿼리는 됨(`:1120,1179`), pipeline-create에 `requiredSubgroupSize` 미적용. wave32/64 핀 필요 시 컴퓨트 파이프라인 생성에 배선(`~:5894`). 미적용이어도 size-agnostic 코드로 동작.

### ⑨ 🟢 셰이더 레벨 신설 — `MODE_RESOLVE_MATERIAL` 변종 + 컴파일러 2개 변경
- 코어이나 RD가 아닌 scene-renderer/셰이더-컴파일러 레벨. `MODE_RENDER_MATERIAL` 선례를 그대로 따름(`scene_shader_forward_clustered.cpp:648-656` 변종 등록 패턴).
  - (i) `*_interp` 배링 입력을 **vis-buffer 재구성 attribute**로 치환(cluster/tri ID → 정점 3개 페치 → `CalcFullBary` → barycentric 가중).
  - (ii) 유저 `texture(s,uv)`를 **`textureGrad(s,uv,ddx,ddy)`로 재작성**(analytic gradient 스레딩). `textureGrad` 빌트인 이미 존재(`shader_language.cpp:3296`).

**코어 작업 없이 확정된 것(재확인):** 서브그룹 ops, BDA, PSO 캐시/spec constant, indirect dispatch·고정카운트 draw, 자동 렌더그래프 동기화, 버퍼 서브레인지 업데이트, persistent-mapped 업로드, async 리드백, TAA 인프라, froxel 라이트 루프, MODE_RENDER_MATERIAL MRT emit.

---

## 2. 스테이지별 성능 설계

### 2.1 래스터라이저 (SW/HW 하이브리드)
- **vis-buffer 포맷:** 단일 atomic **R64_UINT**(30b depth + 34b payload = 16b instance + 18b cluster/tri). 단일 `imageAtomicMax` → 트랜잭션 절반 + 티어링 무. (코어 ②)
- **SW 래스터:** persistent-threads, **클러스터당 1 threadgroup**, 정점 1회 변환 후 LDS 캐시, 마이크로폴리곤은 bounding-box 스캔. Schütz(20억 포인트 60fps)가 타당성 근거. 대형 삼각형만 scanline 분기.
- **SW/HW 분기:** 컬 패스에서 **클러스터 단위** 화면 extent 임계로 append 버퍼 2개 분리. HW 경로 = 정점 풀링(`procedural_vertex_count :9159`) + 단일 mega `draw_indirect`(`:9161`), 프래그먼트가 **같은** R64 atomic에 기록해 SW/HW가 하나의 vis-buffer로 수렴(HW는 early-Z 무료).
- **서브그룹:** ballot/exclusive-bit-count로 컴팩션(1 atomic/subgroup). **wave 사이즈 하드코딩 금지**(`limit_get(LIMIT_SUBGROUP_SIZE)` 쿼리, size-agnostic).
- **배리어:** SW+HW 래스터를 **단일 read-write epoch**로(둘 다 `STORAGE_IMAGE_READ_WRITE`, GENERAL 유지), resolve 직전 배리어 1회. 중간 layout flip 금지. persistent-threads 단일 디스패치로 intra-SW 배리어도 제거.

### 2.2 컬링 / LOD 선택
- **2-pass HZB 오클루전(최대 win):** Pass1 지난 프레임 가시 집합(보통 최종의 90-99%) 그려 HZB 빌드 → Pass2 나머지 테스트. **단일 패스 SPD 밉 리듀서**(AMD FidelityFX 패턴, LDS + atomic 카운터)로 `log2N` 디스패치·배리어를 **1 디스패치 1 배리어**로 축소. reverse-Z 보수적 max-Z 풋프린트. ~0.1-0.3ms@1440p.
- **컬 풀넬:** per-instance(frustum+HZB) → per-cluster-group → per-cluster(frustum+HZB+메시렛 콘) → per-tri(백페이스+small-tri). 각 단계 `atomicAdd` 컴팩션 + 다음 단계 dispatch args 기록. **자동 렌더그래프가 전 체인 배리어 자동 삽입**(`:7067`) → 코어 수정 0.
- **DAG LOD 컷:** QEM 오차 스크린 투영, monotonic 강제, locally-varying cut. **per-cluster 독립 테스트**(트리 워크 아님) — 자기+부모그룹 오차만 읽음. Bevy식 indirect-dispatch-per-stage 먼저(완전 지원), 프로파일링 후 필요 시에만 persistent-threads(Godot는 크로스-워크그룹 forward-progress 미보장 → 이식성 리스크).
- **코어:** drawIndirectCount(④)만. 나머지 0.

### 2.3 머티리얼 리졸브 + 셰이딩 분류 (성능의 심장)
- **UE 5.4 컴퓨트 셰이딩 빈을 직접 타깃**(5.0 material-depth/fullscreen-quad, 5.1 raster-bin은 dominated waypoint → 스킵). **Count → Reserve(atomic prefix-sum, indirect args 기록) → Scatter(2×2 quad, 8×8 워크그룹)** → 논엠프티 빈당 indirect 컴퓨트 디스패치 1회.
  - 왜: 낭비 depth-compare 제거, **풀 wave occupancy**(빈당 단일 머티리얼 = divergence 0), 빈 empty는 `(0,0,0)` args로 거의 무료(실측 ~81% empty).
  - ⚠️ **naive per-pixel 컴퓨트 리졸브(빈 없이)는 5.0보다 느리다** — 정렬이 성능의 핵심.
- **Analytic derivative:** barycentric 미분은 **삼각형당 상수**(afine). 삼각형당 `rcp(det 2x2)` 1회 + 정점 3개, 픽셀당 dot 3개/attribute. `SampleGrad`로 텍스처. **Morton 2×2 quad**로 HW-quad 인접성 복원 + homogeneous-quad 검출(top-left만 셰이딩 후 broadcast = SW-VRS). 재료 그래프의 ~5-10% 노드만 derivative 추적.
- **GBuffer emit vs inline 라이팅 → GBuffer emit 확정:** Filmic Worlds — 한 셰이더에 material+lighting이면 컴파일러가 **양쪽 최악 레지스터 합집합** 할당 → VGPR 스파이크 → occupancy 붕괴. 분리 시 deferred 라이팅은 머티리얼 변종 수와 **완전 디커플**(포워드는 0-100 라이트 → ~100× 순열), 픽셀당 정확히 1회, GBuffer는 SSR/GI/TAA가 재사용. inline은 소수 라이트+MSAA+투명 좁은 영역에서만 유리.
- **Godot 통합:** `MODE_RENDER_MATERIAL`(변종7)이 **이미** 유저 `#CODE:FRAGMENT`를 라이팅 없이 5-MRT(albedo/normal/orm/emission/depth, `:1039-1043` 선언, `:3000-3013` 기록)로 emit — GBuffer emit 절반의 기성 선례. 신설 = `_resolve_material()`(`_render_material :2958` 모델) + `MODE_RESOLVE_MATERIAL` 변종(코어 ⑨).

### 2.4 Deferred 라이팅 + AA
- **GBuffer(팻하지 않게, ~13B/px):** GB1 `normal_roughness`(**이미 존재**, `ensure_normal_roughness_texture :63`, best-fit normal+roughness+dynamic bit)를 **확장·재사용**, GB0 albedo(RGBA8) + GB2 ORM(RGBA8) 추가. emission은 라이팅 accumulator에 폴드 또는 R11G11B10. **별도 linear-depth 타깃 금지** — HW depth + inv_proj로 재구성이 더 쌈.
- **froxel 재사용:** 새 풀스크린 컴퓨트가 private set 0(`render_base_uniform_set :3189`: omni b3/spot b4/dir b7/reflection b6/DFG b17) + set 1(`:3382`: **cluster buffer b9 :3534**, shadow atlas b5 :3457, dir shadow b6, voxelgi b8, sdfgi b30/31)를 바인드. froxel은 화면좌표+depth만 필요 → 깊이에서 `vertex` 재구성 후 **미수정** `light_process_omni/spot/area`(`:466/767/958`)·`sdfgi_process`·`voxel_gi_compute` 호출. ⚠️ 컴퓨트 셰이더가 set 0/1 레이아웃을 바이트-동일하게 선언해야(`scene_forward_clustered_inc.glsl` 동일 include).
- **프레임그래프 삽입:** `_pre_opaque_render`(`:2184`, SSAO/GI resolve) 직후 ~ opaque color(`:2190`) 직전. **하이브리드**(UE Nanite식): Nanite 지오메트리는 vis-buffer/머티리얼 패스에서 **스텐실 비트** 기록 → deferred 컴퓨트가 그 픽셀만 셰이딩, 일반 메시는 opaque 패스에서 forward 유지(depth-equal, 이중 셰이딩 방지). BRDF 드리프트 방지 위해 컴퓨트가 `scene_forward_lights_inc.glsl` 동일 include → `light_compute`(`:101`) 문자 그대로 공유.
- **AA = TAA 확정:** vis-buffer+deferred는 셰이딩 MSAA 불가. 지터(Halton `renderer_scene_cull.cpp:2685`)+모션벡터(`COLOR_PASS_FLAG_MOTION_VECTORS :193`)+`taa_resolve.glsl`(Catmull-Rom 히스토리, variance clip) **완비** → 리졸브가 모션벡터만 출력하면 됨. MSAA-on-visibility(UE)는 GBuffer 대역폭 2배 → Godot 성숙 TAA 감안 불채택.
- **occupancy:** `gl_FragCoord`→`GlobalInvocationID`, derivative는 `vec3(0)` 또는 analytic. **타일 기반**(8×8/16×16, 타일당 Z-range 1회) + **LDS 라이트 리스트**(froxel 마스크 LDS 로드 후 레인 셰이딩). 이미 `half`/`hvec3` 사용 → 레지스터 유리. `LIGHT_CODE_USED` 고압 분기는 deferred에서 컴파일아웃 → forward보다 occupancy 개선. 단일 uber 변종으로 정적 레지스터 바운드.

### 2.5 지오메트리 스트리밍 + 메모리
- **R1 (#99750):** 모든 RD 작업 `call_on_render_thread` 마샬링, 워커는 IO+디코드-prep만. (코어 ⑥)
- **R2 (무-bindless):** **단일 대형 SSBO 풀 + offset 인덱싱**(Bevy식, 클러스터가 명시 offset 저장). `buffer_update` 서브레인지 설치(`:1160`, 재생성 안 함). BDA는 optional 고속 경로(`:1415`, ext 게이트). **128MB 세그먼트 샤딩**(`offset >> 27`) — `maxStorageBufferRange` 미쿼리 대응. **PCIe 회피:** per-cluster 매핑을 매 프레임 업로드 금지(Bevy 측정 8B/cluster ≈ 122MB/frame ≈ 60fps의 46%) → per-instance 배열만 올리고 cluster→instance는 GPU 이진탐색.
- **R3 (리드백):** N개 피드백 버퍼 링 + `buffer_get_data_async`(`:1329`, N-프레임 지연). **동기 `buffer_get_data` 절대 금지**(`:1310` 무조건 풀스톨).
- **R4 (예산):** 고정 지오 예산 + 프레임당 설치 캡(Nanite 512MB풀/128설치/128KB페이지). staging max(`staging_buffer/max_size_mb :8537`) 아래로 유지 안 하면 주기적 `FLUSH_AND_STALL_ALL`. **`DYNAMIC_PERSISTENT_BIT` 업로드 링**(`:1084`, staging 우회 memcpy)이 최대 업로드 win.
- **R5:** 루트 페이지 상주(항상 뭔가 그림) + **완전 상주 그룹만** 래스터(크랙-프리). N-프레임 지연·설치 캡이 안전한 이유.
- **R6 (압축):** GPU-transcode 디스크 포맷, **컴퓨트 디코드**로 풀에 언팩. Bevy 110MB→64MB, 로드 77ms→12ms(bytemuck 제로카피). 위치/인덱스 bit-pack(공유 exponent = 크랙프리 필수), UV는 v1 무압축.

---

## 3. 성능이 실제로 있는 곳 (우선순위 요약)

| 순위 | 성능 레버 | 코어 개조 | 비고 |
|------|-----------|-----------|------|
| 1 | **Async compute 멀티큐** | ① + ⑤ | 최대 구조적 천장. 없으면 오버랩 불가 |
| 2 | **64b 이미지 atomic** | ② | SW 래스터/단일-atomic vis-buffer 인에이블러 |
| 3 | **UE 5.4 셰이딩 빈 분류** | ⑨ | 머티리얼 리졸브의 심장, 정렬이 핵심 |
| 4 | **2-pass HZB + SPD 리듀서** | 0 | 최대 단일-패스 win, 코어 수정 없음 |
| 5 | **GBuffer emit(≠inline 라이팅)** | ⑨ | 레지스터/occupancy + 순열 폭발 회피 |
| 6 | **drawIndirectCount** | ④ | ~80줄, GPU 결정 draw 카운트 |
| 7 | **persistent-mapped 업로드 + 고정예산** | 0 | staging 스톨 제거 |
| 8 | **bindless(머티리얼만)** | ③ | vis-buffer+고정샘플러배열로 v1 회피 가능 |

---

## 4. 권고 빌드 순서 (성능 우선)

1. **코어 훅 선착(정석 기반):** ② 64b atomic(3-site) → ④ drawIndirectCount(~80줄) → ⑦ maxStorageBufferRange(1줄). 저비용·고효과, 이후 전 스테이지의 토대.
2. **지오 프론트엔드:** 오프라인 DAG 빌더 → 2-pass HZB + SPD 리듀서 + 컬 풀넬(코어 0) → SW/HW 하이브리드 래스터(② 의존) → R64 vis-buffer.
3. **셰이딩(정석 핵심):** `MODE_RESOLVE_MATERIAL` 변종 + 컴파일러 2변경(⑨) → UE 5.4 셰이딩 빈 분류 + analytic derivative → GBuffer emit → froxel 재사용 deferred 라이팅 컴퓨트(private set 0/1 바인드) → 스텐실 하이브리드 → TAA 모션벡터. **⚠️ AAA 재채점(2026-08-18):** 이 단계 이전의 자체 deferred-lite 오버레이(경로 A)는 **검증 하니스일 뿐 출하 대상이 아니다.** vis-buffer→GBuffer→단일 deferred 리졸브가 S4 목표이며, A의 자체 deferred-lite는 S4에서 버려질 코드. nanite-implementation §10.3 참조.
4. **스트리밍:** `call_on_render_thread` 마샬링(⑥) + 단일 샤딩 SSBO 풀 + async 리드백 링 + persistent-mapped 설치 + 루트 상주 + 컴퓨트 디코드.
5. **최대 처리량(마지막·최난):** ⑤ 타임라인 세마포어 → ① async compute 멀티큐(그래프 멀티스트림) → ⑨ 파인 배리어. 여기가 man-year 무게중심.

**규모감:** ②④⑦(1~2주) → 지오 프론트엔드+셰이딩(수 개월, 렌더링 전문가) → 스트리밍(수 주) → async compute(수 개월, 그래프 재작업). 프로덕션급 총합 man-year+. 단 "라이트 물리 재구현"은 비용 아님(`_inc.glsl` 재사용) — 비용은 async-compute 그래프 개조 + UE5.4 분류 + analytic-derivative 컴파일러.

---

## 5. 교차 트랙 함의 (다른 오픈월드 격차와의 관계)

- **② async compute 멀티큐는 §1 전용이 아니다** — TLAS 리빌드도 같은 큐를 다퉈 §2(RT GI)와 공유되는 게이팅이다. `max_queue_count_per_family = 1`(`drivers/vulkan/rendering_device_driver_vulkan.cpp:1313`)이 Nanite 컬링과 TLAS 재구축의 오버랩을 동시에 막는다. nanite-implementation §10.4-4 참조.
- **deferred GBuffer 리졸브 = reduz 공식 GPU-driven 방향 + Lumen HW-RT와 수렴점.** vis-buffer를 "GBuffer 가속기"로 구현(§2.3-2.4)하면 Nanite는 공식 deferred 방향과 충돌이 아니라 공유 인프라가 된다.

---

*리서치 방법: 6 스테이지 병렬 딥리서치(래스터·컬링/LOD·머티리얼분류·deferred라이팅/AA·스트리밍·RD감사). 외부 SOTA 1차소스 + Godot 4.8-dev `eda2a482e9` 소스트리 실측 교차검증. 미해결: SIGGRAPH 2021 슬라이드 Encoding 절 per-cluster bit 예산(21b/axis, ~17b/tri)은 압축 포맷 확정 전 원문 재확인 필요.*

---

## 2026-08-18 개정 — AAA 기준 재채점

> 이 문서는 원래 [godot-nanite-implementation-research.md](./godot-nanite-implementation-research.md)의 "Path C = 엔진 내부 deferred-materials 리졸브 신설" 결론을 성능·최적화 관점에서 딥리서치한 것이다. `nanite-implementation-research.md` §10이 AAA 재채점을 완료했고, 그 결론과 충돌하지 않도록 아래 개정을 적용한다.

### 상위 문서와의 정합성 확인

| 항목 | nanite-implementation §10 확정 | 본 문서 적용 상태 |
|------|------|------|
| **경로 C = 목표, A/B = 단계** | §10.3 S1→S2→S3→S4→S5 | ✅ 본 문서 §4(권고 빌드 순서)는 이미 C를 종착점으로 가정. §0의 "신설의 성격" 기술도 "렌더러를 새로 쓴다"가 아닌 "재조립"으로 AAA와 정합 |
| **64b image atomic 3-site 패치** | §10.2 Vulkan device-init | ✅ 본 문서 §1-②가 3-site 정석 변경으로 기술. `VulkanHooks` 우회로는 명시적 배제 |
| **drawIndirectCount 상위 배선** | §10.6 저비용고효과 | ✅ 본 문서 §1-④가 "드라이버 완성, 상위 미배선"으로 기술. 정합 |
| **DAG 품질 하향 조정** | §10.2 meshopt 1.2 | N/A — 본 문서는 DAG 품질을 다루지 않음(성능 문서) |
| **METIS 불필요** | §10.2 | N/A — 동일 |
| **man-year 무게중심 = async compute** | §10.6 | ✅ 본 문서 §3 우선순위 1순위 "Async compute 멀티큐" + §4 5단계 "최대 처리량"이 가장 큰 비용으로 기술. 정합 |
| **GBuffer 리졸브에서 스타일라이즈드 축소 논거** | §10.5 폐기 | ✅ 본 문서는 §0에서 "재조립"으로 프레이밍, §2.3에서 "UE 5.4 컴퓨트 셰이딩 빈을 직접 타깃"으로 기술. 스타일라이즈드 언급 없음 |
| **경로 A의 자체 deferred-lite 폐기** | §10.3 "S2는 출하 대상 아님" | 해당 없음 — 본 문서는 경로 A 자체 deferred-lite를 다루지 않음. §2.4 GBuffer emit은 C 아키텍처로 가정 |

### 델타 표

| 항목 | 기존 기술 | AAA 재채점 | 변경 |
|------|-----------|-----------|------|
| **경로 C의 위상** | "규모감: 프로덕션급 총합 man-year+" (§4) | **목표.** man-year는 승인된 예산 | **무변경** — 본 문서의 C를 "비권장"으로 해석한 적 없음 |
| **경로 A의 위상** | 별도 기술 없음 (성능 문서) | **검증 하니스.** 자체 deferred-lite 짓지 말 것 | **인라인 주석 추가** (§4 "권고 빌드 순서"에 A의 재해석 명시) |
| **async compute 우선순위** | "1순위, 최대 구조적 천장" (§3) | **§1·§2 공유 게이팅으로 재확인** | **§5 교차 트랙 함의에 추가** — TLAS 리빌드도 같은 큐를 다툼 |
| **bindless 우선순위** | "8순위, v1 회피 가능" (§3) | **동일.** vis-buffer+고정샘플러배열로 v1 우회 | **무변경** |
| **스타일라이즈드 축소** | 미포함 | — | **무변경** — 본 문서는 원문부터 AAA 성능을 다룸 |

### 인라인 수정

**§4 권고 빌드 순서** — 3단계 셰이딩 설명에 주석 추가.
