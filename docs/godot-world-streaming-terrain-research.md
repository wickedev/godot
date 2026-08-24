# Godot 오픈월드 — 월드 스트리밍·지형·폴리지·오브젝트 스트리밍 딥리서치

> [godot-openworld-engine-gaps.md](./godot-openworld-engine-gaps.md) §0의 상세 문서.
> Godot 4.8-dev (commit `eda2a482e9`). 모든 file:line은 로컬 트리 직접 실측.
> 목표는 풀 AAA. 원신은 하한 참조점. 포크 + 딥 코어 개조 허용.

---

## 0. 한 장 요약

| 항목 | Godot 현황 | 판정 | 공수 | 핵심 근거 |
|------|-----------|------|------|-----------|
| **월드 파티션 / 스트리밍** | `ResourceLoader.load_threaded`만 존재. 자동 공간 파티션·셀 단위 비동기 로드/언로드·LOD 통합·우선순위 기반 스트리밍 전무 | 🔴 man-year | 스트리밍 매니저 신설 + IO 파이프라인 |
| **지형 (Terrain)** | Terrain3D(클립맵, 65km², 리전 스트리밍 미구현) / godot_voxel(복셀 옥트리, 동굴 가능, 성능 제한) — 둘 다 GDExtension | 🟡 국소 코어 | Terrain3D에 리전 스트리밍·인스턴스 충돌·동굴 직접 구현 필요 |
| **폴리지 / 인스턴싱** | MultiMesh AABB all-or-nothing 컬링. GPU 구동 인스턴싱(indirect draw)은 4.4에 머지됐으나 컬링은 여전히 수동 | 🟡 국소 코어 | HZB 기반 GPU 컬링 compute 패스 신설 (indirect count 배선됨) |
| **오브젝트/메시 스트리밍** | `lod_bias` per-instance만 존재. HLOD 전무. ImporterMesh LOD는 임포트타임만. 자동 LOD 전환·스트리밍 미통합 | 🔴 man-year | LOD 관리자 + HLOD 빌더 + 스트리밍 IO |
| **충돌 스트리밍** | Jolt Physics 번들(5.6.0). 물리 바디 스트리밍·활성 영역 개념 없음. `space_create`/`space_set_active`로 분할은 가능 | 🟢 GDExtension | 스트리밍 매니저로 바디 로드/언로드 — 물리 문서 §1.5 |
| **내비게이션 스트리밍** | `map_create`/`map_set_active`로 맵 분할 가능. `region_set_map`으로 리전 재할당. 런타임 베이킹 지원 | 🟢 GDExtension | 네비메시 리전 단위 로드/언로드 + 비동기 리베이크 |
| **업스트림 현황** | terrain·partition·open world·foliage·nanite 공식 우선순위 **전부 0매치**. 텍스처 스트리밍만 유일한 초록불(PR #113429) | 🔴 | 포크에서 직접 만드는 것이 유일한 경로 |

---

## 1. 월드 스트리밍 / 월드 파티션

### 1.1 (a) 현재 존재하는 것

**씬 단위 로드/언로드.** Godot의 월드 관리 기반은 `Node3D` → `Viewport` → `World3D` 계층이다. `PackedScene` 리소스를 `ResourceLoader`로 로드하고 `add_child()`/`remove_child()`로 씬 트리에 연결한다.

**스레드 로딩 API.** `ResourceLoader`는 비동기 리소스 로딩을 위한 3가지 메서드를 제공한다:

| 메서드 | 시그니처 | 위치 |
|--------|----------|------|
| `load_threaded_request` | `Error load_threaded_request(const String &p_path, const String &p_type_hint = "", bool p_use_sub_threads = false, CacheMode p_cache_mode = CACHE_MODE_REUSE)` | `core/io/resource_loader.h:249` |
| `load_threaded_get_status` | `ThreadLoadStatus load_threaded_get_status(const String &p_path, float *r_progress = nullptr)` | `core/io/resource_loader.h:250` |
| `load_threaded_get` | `Ref<Resource> load_threaded_get(const String &p_path, Error *r_error = nullptr)` | `core/io/resource_loader.h:251` |

- 구현: `core/io/resource_loader.cpp:700`(`load_threaded_request`), `:887`(`load_threaded_get_status`), `:932`(`load_threaded_get`).
- GDScript 바인딩: `core/io/resource_loader.cpp`에서 `ClassDB::bind_method`로 `load_threaded_request`/`load_threaded_get`/`load_threaded_get_status` 3개 전부 노출.
- 내부적으로 `WorkerThreadPool`을 사용하며, `LoadToken` 기반 재사용/중복 감지 로직 포함(`:705-763`).

**멀티플레이어 스폰 시스템.** `modules/multiplayer/`의 `MultiplayerSpawner`/`MultiplayerSynchronizer`는 네트워크 동기화 스폰 시스템이지만, **스트리밍 매니저의 참조 패턴**으로 사용 가능하다:
- `MultiplayerSpawner`는 `spawn_function` + `spawn_path`로 씬을 동기화하며 `add_child`/`remove_child`를 자동화한다.
- 이 패턴을 셀 단위 로드/언로드로 일반화할 수 있다.

**RenderingServer 인스턴스 API.** `servers/rendering/rendering_server.h:729-775`에 저수준 `instance_create2()`/`instance_set_base()`/`instance_set_scenario()`/`instance_set_transform()`/`instance_set_visible()`/`instance_set_layer_mask()`/`instance_geometry_set_visibility_range()` 등이 전부 노출돼 있다. 이는 씬 트리 없이 렌더링 인스턴스를 직접 관리할 수 있는 경로다.

### 1.2 (b) 없는 것 — 상세

**자동 공간 파티션.** `streaming`·`chunk`·`cell.*load`·`world.*partition`·`spatial.*partition` grep 결과 (`servers/`·`physics/`·`modules/`·`scene/`) — **0매치** (오디오·텍스트 레이아웃의 "chunk"만 검출, 모두 무관).

**셀 단위 비동기 로드/언로드.** `load_threaded_request`는 **단일 리소스**에 대한 저수준 API다. 셀(셀 = 여러 씬의 묶음)을 단일 트랜잭션으로 로드/언로드하는 상위 개념이 없다.

**LOD 통합.** 월드 스트리밍 셀과 메시 LOD 계층이 연동된 시스템이 없다. `GeometryInstance3D`의 `set_lod_bias()`(`scene/3d/visual_instance_3d.h:188`)는 per-instance 수동 바이어스일 뿐, 셀 거리 기반 자동 LOD 전환과 통합되지 않는다.

**우선순위 기반 스트리밍.** 카메라 방향·거리·예측 이동에 기반한 로드 우선순위 큐가 없다. `load_threaded_request`는 호출 순서대로 처리된다.

**공간 인덱스.** `core/templates/`에 `HashMap`·`HashSet`·`RBMap`·`OAHashMap`이 있으나, 월드 스트리밍용 공간 해시 그리드·옥트리·BVH는 **존재하지 않는다**. Godot Physics 내부의 broadphase는 있으나 외부 노출되지 않는다.

### 1.3 (c) `load_threaded_request`로 자체 스트리밍 매니저 구축 가능성

**가능한 것:**
- 3개 API로 단일 리소스의 비동기 로드 완료 콜백 패턴을 만들 수 있다.
- `load_threaded_get_status`의 `r_progress`로 로딩 진행률을 폴링할 수 있다.
- `PackedScene`을 로드한 후 `instance()`로 인스턴스화하고 `add_child()`로 씬 트리에 연결할 수 있다.

**제약:**
- 우선순위 큐·취소·로드 예산 관리가 API에 없다. 자체 레이어로 감싸야 한다.
- `load_threaded_request`는 내부 큐에 넣을 뿐, **실행 중인 로드를 취소할 수 없다** (API 부재).
- `load_threaded_get`은 완료될 때까지 블로킹하지 않지만, 실패 시 `r_error`만 설정하고 `Ref<Resource>`를 반환한다. 부분 실패 처리 로직을 자체 구축해야 한다.
- 메모리 예산 관리(언로드 정책)는 전적으로 자체 구현해야 한다.

**판정:** 🟡 국소 코어. `load_threaded_*` API 위에 자체 스트리밍 매니저를 구축하는 것은 가능하나, 우선순위 큐·취소·예산 관리는 수주~수개월의 GDExtension/C++ 모듈 작업이다.

### 1.4 (d) 업스트림 현황

| 출처 | 내용 | 날짜 |
|------|------|------|
| **fire on #11728** | "stuck", "probably best to move it out of the engine core" | 2025-02-10 |
| **reduz proposal #6121** | "Terrain will not be added to Godot" — 180👍 | — |
| **clayjohn on #6109** | "Nobody works on the renderer full time… We don't have anyone lined up." | 2024-09 |
| **공식 우선순위 페이지** | terrain·partition·open world·foliage·nanite **전부 0매치** | — |
| **텍스처 스트리밍만 유일한 초록불** | PR #113429 (마일스톤 4.x) | 진행 중 |

→ **§0의 월드 스트리밍·지형·폴리지는 기다려도 오지 않는다. 포크에서 직접 만드는 것이 유일한 경로다.**

### 1.5 (e) 피직스 서버 기준 참조

Godot Physics 서버(`servers/physics_3d/physics_server_3d.h:90-95`)는 `space_create()`/`space_set_active()`로 공간을 분할할 수 있다. Jolt Physics도 동일 패턴을 따른다. 이는 월드 스트리밍이 물리 공간을 셀 단위로 분할할 수 있는 기반이 된다. 상세: [godot-physics-simulation-research.md §1.5](./godot-physics-simulation-research.md).

### 1.6 (f) 판정: 🟡 국소 코어 (스트리밍 매니저) + 🔴 man-year (통합 LOD 스트리밍)

- **🟡** GDExtension/C++ 모듈로 `load_threaded_*` 위에 자체 셀 기반 로드/언로드 매니저 구축은 가능 (수주~수개월).
- **🔴** LOD 통합 + 우선순위 기반 예측 스트리밍 + IO 파이프라인 최적화는 man-year급.

---

## 2. 지형 (Terrain)

### 2.1 (a) Terrain3D — 현재 상태

**Terrain3D** (MIT, C++ GDExtension, 4.2k stars, `TokisanGames/Terrain3D`):
- 클립맵 기반 지형. 최대 65,536m/side (1024 리전 x 최대 2048m/리전) = 4,295km².
- `MAX_TEXTURES = 32` (텍스처 페인팅).
- 최대 10레벨 LOD + 폴리지 인스턴싱(10 LOD + 그림자 임포스터).
- 스컬프팅·홀·텍스처 페인팅·디타일링·컬러/습도 페인팅.
- 하이트맵 임포트 (HTerrain, Gaea, World Creator, World Machine, Unity, Unreal 등).

### 2.2 (b) Terrain3D — 없는 것 (실측)

| 기능 | 상태 | 근거 |
|------|------|------|
| **리전 스트리밍** | ❌ 미구현 | Issue #491 open, 42👍, 마일스톤 1.2. PR #1020 (2026-08-07, Empt-y) — `update_streaming()` 거리 기반 로드/언로드 — **unmerged**, 리뷰 0. TokisanGames: "We'll look at it all in 1.2" |
| **인스턴스 단위 컬링/LOD** | ❌ 32m 셀 단위만 | 개별 인스턴스 컬링 없음. 전체 셀 단위로만 LOD 결정 |
| **인스턴스 충돌** | ❌ 미구현 | PR #699 draft (2025-05-23부터), unmerged |
| **동굴 / 오버행** | ❌ 구조적 불가능 | 클립맵(2.5D 하이트필드) 기반 — 수직 기둥·오버행·동굴은 원천 불가능 |

### 2.3 (b) godot_voxel — 현재 상태

**godot_voxel** (MIT, C++ GDExtension, 3.8k stars, `Zylann/godot_voxel`):
- 복셀 옥트리 기반 지형.
- **동굴·오버행·터널 가능** (3D 복셀 데이터).
- Transvoxel 알고리즘으로 smooth LOD.
- 청크 기반 페이징 — "Infinite terrains made by paging chunks in and out" (스트리밍 내장).
- 8-bit/16-bit 복셀 채널, 폴리지/장식 인스턴싱 시스템.
- 블록형(Minecraft식) 복셀 + ambient occlusion.
- 5,015 commits.

**한계:**
- 성능이 하이트필드 대비 크게 낮음 (복셀 옥트리 순회 + 메시 생성).
- AAA급 지형 디테일을 복셀로 표현하려면 엄청난 해상도가 필요.
- 아직 "Make GDExtension work"는 로드맵 항목 (일부 기능 미완).

### 2.4 (d) 대안 평가

| 해결 경로 | 장점 | 단점 | 판정 |
|-----------|------|------|------|
| **Terrain3D + 자체 리전 스트리밍** | 고품질 클립맵, 65km², 32 텍스처 | 동굴 불가, 인스턴스 충돌 미구현, PR #1020 unmerged | 🟡 국소 코어 |
| **godot_voxel** | 동굴/오버행 가능, 청크 스트리밍 내장 | 성능 열세, AAA 디테일 어려움 | 🟢 GDExtension |
| **자체 지형 (from scratch)** | 완전 제어 | man-year급 | 🔴 man-year |
| **Terrain3D + godot_voxel 하이브리드** | 지형=Terrain3D, 동굴 구간만 godot_voxel | 통합 복잡도 | 🟡 국소 코어 |

### 2.5 (e) 업스트림

- Godot 엔진 코어에 지형 시스템 전무. `terrain` grep 0매치.
- reduz의 #6121: "Terrain will not be added to Godot" (180👍).
- `ImporterMesh::generate_lods()`가 지형 LOD 생성에 사용될 수 있으나(`scene/resources/3d/importer_mesh.h:120`), 이는 임포트타임 전용이다.

### 2.6 (f) 판정: 🟡 국소 코어 (Terrain3D 확장)

Terrain3D에 리전 스트리밍·인스턴스 충돌을 직접 구현하는 경로. 동굴/오버행은 별도 시스템(godot_voxel 또는 자체) 필요. 자체 지형 엔진은 🔴 man-year.

---

## 3. 폴리지 (Foliage) / 인스턴싱

### 3.1 (a) MultiMesh — 현재 상태

**MultiMesh** (`scene/resources/multimesh.h:58-101`):
- `instance_count` — 인스턴스 개수.
- `visible_instance_count` — CPU에서 그릴 인스턴스 수 제한 (기본 -1 = 전부).
- `multimesh_allocate_data(p_multimesh, p_instances, p_transform_format, p_use_colors, p_use_custom_data, p_use_indirect)` — `servers/rendering/rendering_server.h:260`.
- `use_indirect = true`로 GPU 구동 indirect draw 활성화 (4.4 머지, PR #99455).

### 3.2 (b) MultiMesh 컬링 — all-or-nothing (핵심 한계)

MultiMesh의 AABB 컬링은 **전체 MultiMesh 단위**로 동작한다. 공식 문서(`doc/classes/MultiMesh.xml:131-157`): `visible_instance_count`는 "By default, all instances are drawn but you can limit this with visible_instance_count" — 이는 CPU에서 N개까지만 그리고, 컬링이 아니라 **개수 제한**이다.

**즉:** MultiMesh의 AABB가 절반만 화면에 걸쳐도, **전체 인스턴스가 그려지거나 전혀 안 그려진다.** 개별 인스턴스 단위 프러스텀 컬링은 **존재하지 않는다.** "millions of objects will be always or never drawn" (작업 명세 인용)은 정확하다.

**렌더러 내부 확인** (`servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp:4389-4463`):
- `ginstance->instance_count = mesh_storage->multimesh_get_instances_to_draw(ginstance->data->base)` — 인스턴스 수를 한 번에 가져온다.
- `multimesh_uses_indirect(ginstance->data->base)` — indirect 모드일 때만 GPU가 인스턴스 수를 결정.
- **개별 인스턴스 컬링은 전혀 수행되지 않는다.** CPU 컬링은 전체 MultiMesh의 AABB에만 적용.

### 3.3 (a) RenderingServer 인스턴스 API — 코드 기반 인스턴싱 경로

`servers/rendering/rendering_server.h:729-775`:
```cpp
virtual RID instance_create2(RID p_base, RID p_scenario);  // :729
virtual RID instance_create() = 0;                          // :731
virtual void instance_set_base(RID p_instance, RID p_base); // :733
virtual void instance_set_scenario(RID p_instance, RID p_scenario); // :734
virtual void instance_set_transform(RID p_instance, const Transform3D &p_transform); // :737
virtual void instance_set_visible(RID p_instance, bool p_visible); // :741
virtual void instance_set_layer_mask(RID p_instance, uint32_t p_mask); // :735
virtual void instance_set_extra_visibility_margin(RID p_instance, real_t p_margin); // :749
virtual void instance_set_visibility_parent(RID p_instance, RID p_parent_instance); // :750
virtual void instance_set_ignore_culling(RID p_instance, bool p_enabled); // :752
virtual void instance_geometry_set_visibility_range(RID p_instance, float p_min, float p_max, float p_min_margin, float p_max_margin, RSE::VisibilityRangeFadeMode p_fade_mode); // :767
virtual void instance_geometry_set_lod_bias(RID p_instance, float p_lod_bias); // :769
```

`instance_create2()` 구현 (`servers/rendering/rendering_server.cpp:3631-3636`):
```cpp
RID RenderingServer::instance_create2(RID p_base, RID p_scenario) {
    RID instance = instance_create();
    instance_set_base(instance, p_base);
    instance_set_scenario(instance, p_scenario);
    return instance;
}
```

이 API는 `GeometryInstance3D` 노드 없이 렌더링 인스턴스를 직접 생성·제어할 수 있다. **clayjohn이 권장한 코드 기반 인스턴싱 경로**가 바로 이것이다.

### 3.4 (a) MultiMesh Indirect Draw (PR #99455, 4.4 머지)

**PR #99455** (Bonkahe, 2025-01-14 머지, Godot 4.4): `multimesh_allocate_data`의 `use_indirect = true`로 GPU 커맨드 버퍼를 생성, compute shader가 인스턴스 수를 직접 제어할 수 있게 한다.

- `servers/rendering/renderer_rd/storage_rd/mesh_storage.cpp:1706`: `if (multimesh->indirect) { ... }` — indirect 모드일 때 커맨드 버퍼 사용.
- `servers/rendering/renderer_rd/storage_rd/mesh_storage.cpp:2238`: `if (multimesh->indirect) { //we have to update the command buffer for the instance counts, in each stride this will be the second integer.`
- `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp:4459`: `if (mesh_storage->multimesh_uses_indirect(ginstance->data->base)) { ... }`

**이것이 의미하는 것:** GPU compute shader에서 인스턴스 카운트를 기록 → GPU가 그만큼만 그린다. **GPU 구동 인스턴싱은 가능해졌다.**

**단, 컬링은 여전히 수동이다.** indirect draw는 "몇 개를 그릴지"만 GPU가 결정할 뿐, **어떤 인스턴스를 그릴지**(컬링)는 compute shader로 자체 구현해야 한다.

### 3.5 (b) GPU 구동 컬링의 필요성 — HZB는 존재하나 소비 경로가 없다

**HZB/Hi-Z depth pyramid 존재:**
- `servers/rendering/renderer_scene_occlusion_cull.h:45` — `class HZBuffer` — mip 체인, `_is_occluded()` 메서드 완비.
- `ss_effects.cpp:1458`에서 HZB 빌드.

**그러나 소비 경로(indirect draw)가 없다:**
- HZB를 읽어 GPU에서 인스턴스 컬링을 수행하는 compute 패스가 빌트인에 없다.
- indirect draw(#99455)로 GPU가 인스턴스 수를 결정할 수 있지만, **HZB와 연결된 컬링 compute shader는 자체 작성해야 한다.**
- [godot-nanite-implementation-research.md §4](./godot-nanite-implementation-research.md)의 vis-buffer HZB 컬링과 통합 가능 — 동일한 HZBuffer를 폴리지 GPU 컬링에도 재사용.

### 3.6 (d) GPU 컬링 구현 경로

**drawIndirectCount 사용 가능:**
- `servers/rendering/rendering_device_driver.h:697-698`:
  ```cpp
  virtual void command_render_draw_indirect(CommandBufferID p_cmd_buffer, BufferID p_indirect_buffer, uint64_t p_offset, uint32_t p_draw_count, uint32_t p_stride) = 0;
  virtual void command_render_draw_indirect_count(CommandBufferID p_cmd_buffer, BufferID p_indirect_buffer, uint64_t p_offset, BufferID p_count_buffer, uint64_t p_count_buffer_offset, uint32_t p_max_draw_count, uint32_t p_stride) = 0;
  ```
- 드라이버 3백엔드 전부 구현됨: `rendering_device_driver_vulkan.cpp:5764`, `rendering_device_driver_d3d12.cpp:4928`, `rendering_device_driver_metal.cpp:1745`.
- **단, `rendering_device.{cpp,h}`·`rendering_device_graph.{cpp,h}`에 0매치** — 상위 미배선. draw list API(`draw_list_draw_indirect`)는 있으나 count variant는 노출되지 않음.

**필요한 compute 패스 (자체 구현):**
1. HZB 읽기 → 각 인스턴스 AABB의 HZB 오클루전 테스트.
2. 프러스텀 컬링 (AABB vs view frustum).
3. 생존 인스턴스의 인덱스를 indirect draw 버퍼에 기록.
4. `draw_list_draw_indirect`로 GPU 구동 렌더.

### 3.7 (e) MultiMesh vs 개별 인스턴스

| 방식 | 장점 | 단점 | 판정 |
|------|------|------|------|
| **MultiMesh indirect** | GPU 구동 인스턴싱, 배치 드로우 | 컬링 직접 구현 필요, 개별 인스턴스 컬링 없음 | 🟡 국소 코어 (컬링 compute만) |
| **RenderingServer instance_create2** | 개별 인스턴스 컬링(CPU), `visibility_range` 자동 | CPU 바운드, 인스턴스 1개당 드로우콜 1개 | 🟢 GDExtension |
| **자체 GPU 컬링 + indirect MultiMesh** | 최적 성능, HZB 재사용 | compute 패스 자체 구현 | 🟡 국소 코어 |

### 3.8 (f) 판정: 🟡 국소 코어

GPU 컬링 compute 패스 + indirect draw 통합은 수주~수개월. `instance_create2` API도 보완재로 사용 가능. man-year는 아니다.

---

## 4. 오브젝트/메시 스트리밍

### 4.1 (a) 메시 LOD — 현재 상태

**`GeometryInstance3D` / `VisualInstance3D`** (`scene/3d/visual_instance_3d.h:136-189`):
- `lod_bias` (float, 기본 1.0) — `set_lod_bias()` / `get_lod_bias()`.
- `visibility_range_begin` / `visibility_range_end` + margin + fade mode.
- `set_render_layers()` — 상속된 `VisualInstance3D`에서.

**작동 방식:** `instance_geometry_set_lod_bias()` (`servers/rendering/rendering_server.h:769`) — per-instance LOD 바이어스. `instance_geometry_set_visibility_range()` (`:767`) — 거리 기반 가시성. 이 두 기능이 결합돼 기본적인 LOD 전환을 수행한다.

**제한:**
- LOD 바이어스는 수동 설정. 자동 거리 기반 LOD 선택 로직은 빌트인에 없다.
- Mesh 리소스의 LOD 체인(여러 LOD 레벨을 가진 단일 Mesh)과 인스턴스의 LOD 바이어스가 연동은 되지만, **"어떤 LOD를 선택할지"는 엔진이 자동으로 결정하지 않는다** — 각 인스턴스에 수동으로 설정해야 한다.
- `ImporterMesh::generate_lods()` (`scene/resources/3d/importer_mesh.h:120`)는 임포트 시점에만 작동. 런타임에 LOD 생성 불가.

### 4.2 (b) HLOD — 전무

`hlod`·`hierarchical.*lod`·`HLOD` grep (`modules/`·`scene/`·`servers/`·`editor/`) — **0매치**. Godot에는 HLOD(Hierarchical Level of Detail) 개념이 존재하지 않는다.

**UE5의 HLOD:** 여러 오브젝트를 하나의 프록시 메시로 병합해 원거리 렌더링을 최적화하는 시스템. UE5는 Nanite와 HLOD를 병행한다.

**Godot에서 구축해야 할 것:**
- 공간 클러스터링으로 오브젝트 그룹화.
- 그룹별 프록시 메시 생성 (단순화 + 텍스처 베이킹).
- 런타임 LOD 전환 (개별 오브젝트 → HLOD 프록시).

### 4.3 (a) 텍스처 스트리밍 — 유일한 초록불

PR #113429 (마일스톤 4.x) — 텍스처 밉레벨 스트리밍. 상세: [godot-virtual-texturing-research.md](./godot-virtual-texturing-research.md).

### 4.4 (d) 업스트림 현황 (clayjohn on #6109, 2024-09)

> "Nobody works on the renderer full time… We don't have anyone lined up."

메시 스트리밍·HLOD·자동 LOD 전환 모두 업스트림에서 기대할 수 없다.

### 4.5 (e) 구축해야 할 것

| 구성요소 | 설명 | 공수 |
|----------|------|------|
| **LOD 관리자** | 거리·화면 점유율 기반 자동 LOD 선택 | 2~4주 |
| **HLOD 빌더** | 오프라인 클러스터링 + 프록시 메시 생성 | 1~2개월 |
| **스트리밍 IO** | LOD 레벨별 메시 로드/언로드 파이프라인 | 2~4주 |
| **GPU 컬링 통합** | §3의 GPU 컬링을 LOD 선택과 통합 | §3 공수에 포함 |

### 4.6 (f) 판정: 🔴 man-year

LOD 관리자만으로는 🟡 국소 코어이나, HLOD 빌더 + 자동 LOD 전환 + 스트리밍 IO 통합은 **man-year급**. 이 중 HLOD 빌더가 가장 무겁다.

---

## 5. 충돌 스트리밍

### 5.1 (a) 물리 서버 공간 관리

**Jolt Physics** (MIT, 5.6.0, `thirdparty/jolt_physics/`):
- `modules/jolt_physics/`에 Godot 통합 계층 존재.
- `servers/physics_3d/physics_server_3d.h:90-95`:
  ```cpp
  virtual RID space_create() = 0;
  virtual void space_set_active(RID p_space, bool p_active) = 0;
  virtual void space_set_param(RID p_space, PS3DE::SpaceParameter p_param, real_t p_value) = 0;
  virtual real_t space_get_param(RID p_space, PS3DE::SpaceParameter p_param) const = 0;
  virtual PhysicsDirectSpaceState3D *space_get_direct_state(RID p_space) = 0;
  ```

**공간 분할 가능:** `space_create()`로 여러 물리 공간을 만들고, `space_set_active()`로 활성/비활성화할 수 있다. 이는 월드 셀 단위로 물리 공간을 분할할 수 있는 기반이다.

**`body_create()` (`servers/physics_3d/physics_server_3d.h:154`)** — 개별 바디 생성 후 특정 space에 할당.

### 5.2 (b) Jolt 대규모 정적 바디 처리

Jolt Physics는 대규모 정적 바디를 broadphase에서 효율적으로 처리한다. `modules/jolt_physics/spaces/`에 Jolt space 구현이 있다.

**제한:**
- `max_bodies` 기본값 10240 (`modules/jolt_physics/` 설정). **재시작 필요** — Godot의 `set_max_bodies`는 런타임 변경 불가.
- `body_streaming`·`activation_region`·`physics_lod` grep **각 0매치** — 물리 바디 스트리밍·활성 영역·물리 LOD 개념이 전무하다.

**참조:** [godot-physics-simulation-research.md §1.5](./godot-physics-simulation-research.md) — 물리 스트리밍 매니저는 **🟢 순수 GDScript/GDExtension** (1~2주).

### 5.3 (a) 물리 쿼리 API

`PhysicsDirectSpaceState3D` 쿼리 (`servers/physics_3d/direct_states/physics_direct_space_state_3d.cpp`):
- `intersect_ray()` — `_intersect_ray` (`:36-40`)
- `intersect_point()` — `_intersect_point` (`:58-64`)
- `intersect_shape()` — `_intersect_shape` (`:83+`)
- `PhysicsServer3DExtension`에 전부 `GDVIRTUAL` 바인딩 (`servers/physics_3d/physics_server_3d_extension.cpp:44-46`)

GDExtension에서 물리 서버를 완전히 교체할 수 있는 확장 포인트가 존재한다.

### 5.4 (f) 판정: 🟢 GDExtension

공간 분할 + 바디 로드/언로드 + 쿼리는 전부 기존 API로 가능. 스트리밍 매니저만 GDScript/GDExtension으로 구축 (1~2주). 물리 문서 §1.5 참조.

---

## 6. 내비게이션 스트리밍

### 6.1 (a) NavigationServer3D 맵 관리

**`NavigationServer3D`** (`servers/navigation_3d/navigation_server_3d.h:49-108`):
```cpp
virtual RID map_create() = 0;                          // :64
virtual void map_set_active(RID p_map, bool p_active) = 0; // :66
virtual Vector3 map_get_up(RID p_map) const = 0;       // :70
virtual RID region_create() = 0;                       // :112
virtual void region_set_map(RID p_region, RID p_map) = 0; // :135
virtual RID region_get_map(RID p_region) const = 0;    // :136
virtual void region_set_navigation_mesh(RID p_region, Ref<NavigationMesh> p_navigation_mesh) = 0; // :144
virtual void region_bake_navigation_mesh(Ref<NavigationMesh> p_navigation_mesh, Node *p_root_node) = 0; // :147
virtual TypedArray<RID> map_get_regions(RID p_map) const = 0; // :98
virtual Vector<Vector3> map_get_path(RID p_map, Vector3 p_origin, Vector3 p_destination, bool p_optimize, uint32_t p_navigation_layers = 1) = 0; // :90
```

**맵 분할 가능:** `map_create()`로 여러 네비게이션 맵을 만들고, `map_set_active()`로 활성/비활성화할 수 있다. `region_set_map()`으로 리전을 다른 맵으로 재할당할 수 있다. 이는 월드 셀 단위로 네비게이션 맵을 분할할 수 있는 기반이다.

### 6.2 (a) 런타임 네비메시 베이킹

**`NavigationRegion3D`** (`scene/3d/navigation/navigation_region_3d.h:106`):
```cpp
void bake_navigation_mesh(bool p_on_thread);
```

구현 (`scene/3d/navigation/navigation_region_3d.cpp:222-240`):
```cpp
void NavigationRegion3D::bake_navigation_mesh(bool p_on_thread) {
    // ...
    if (p_on_thread) {
        NavigationServer3D::get_singleton()->bake_from_source_geometry_data_async(
            navigation_mesh, source_geometry_data, 
            callable_mp(this, &NavigationRegion3D::_bake_finished));
    } else {
        NavigationServer3D::get_singleton()->bake_from_source_geometry_data(
            navigation_mesh, source_geometry_data, 
            callable_mp(this, &NavigationRegion3D::_bake_finished));
    }
}
```

**비동기 베이킹:** `bake_from_source_geometry_data_async()` (`servers/navigation_3d/navigation_server_3d.h:290`) — 콜백 기반 비동기 베이킹이 존재한다. 런타임 스트리밍 중 리전의 네비메시를 백그라운드로 베이킹할 수 있다.

### 6.3 (b) 제한 및 구축할 것

**제한:**
- 맵 간 경로 찾기(inter-map pathfinding)가 없다. `map_get_path()`는 단일 맵 내에서만 경로를 찾는다.
- 맵을 가로지르는 경로는 수동으로 연결 지점을 관리해야 한다.

**구축할 것:**
- 셀 단위 네비게이션 맵 로드/언로드 매니저.
- 맵 경계 연결 지점 관리.
- 비동기 리베이킹 파이프라인.

### 6.4 (f) 판정: 🟢 GDExtension

`map_create`·`region_set_map`·`bake_from_source_geometry_data_async` API가 전부 존재. 스트리밍 매니저만 GDScript/GDExtension으로 구축 가능 (1~2주). man-year가 아니다.

---

## 7. 경계 종합

| 섹션 | 항목 | 🟢 GDExtension | 🟡 국소 코어 | 🔴 man-year |
|------|------|:---:|:---:|:---:|
| **§1** | 월드 파티션 / 스트리밍 매니저 | | 🟡 | 🔴 (LOD 통합) |
| **§2** | 지형 (Terrain3D 확장) | | 🟡 | 🔴 (자체 엔진) |
| **§2** | 동굴/오버행 (godot_voxel) | 🟢 | | |
| **§3** | 폴리지 GPU 컬링 | | 🟡 | |
| **§3** | MultiMesh indirect | 🟢 | | |
| **§4** | LOD 관리자 | | 🟡 | |
| **§4** | HLOD 빌더 | | | 🔴 |
| **§5** | 충돌 스트리밍 | 🟢 | | |
| **§6** | 내비게이션 스트리밍 | 🟢 | | |

**총괄:**
- **🟢 GDExtension으로 가능:** 충돌·내비게이션 스트리밍, godot_voxel, MultiMesh indirect 기본 사용.
- **🟡 국소 코어:** 월드 파티션 매니저, Terrain3D 확장(리전 스트리밍·충돌), 폴리지 GPU 컬링, LOD 관리자. 합계 2~4 man-month.
- **🔴 man-year:** HLOD 빌더, LOD 통합 스트리밍, 자체 지형 엔진. 이 중 HLOD 빌더가 가장 무겁다.

**핵심 통찰:**
- **폴리지 GPU 컬링(§3)이 가장 높은 ROI.** MultiMesh indirect draw가 이미 4.4에 머지됐고, HZB 인프라가 존재하며, 컬링 compute shader만 작성하면 된다. 공수는 수주.
- **Terrain3D 리전 스트리밍(§2)은 차단 상태.** PR #1020이 unmerged이고 TokisanGames가 "We'll look at it all in 1.2"라고 했지만, 언제 머지될지 불확실. 포크에서 자체 구현하거나 PR #1020을 기반으로 작업.
- **업스트림에서 오는 건 텍스처 스트리밍(PR #113429)뿐.** §0·§1·§2·§3·§4의 지오메트리 쪽은 전부 포크에서 직접 구축해야 한다.

---

## 8. 우선순위 로드맵

### P0 — 즉시 가능 (합계 2~3주)

| 항목 | 내용 | 판정 |
|------|------|:---:|
| 물리 스트리밍 매니저 | `space_create` + 바디 로드/언로드 | 🟢 |
| 내비게이션 스트리밍 매니저 | `map_create` + 리전 재할당 + 비동기 리베이크 | 🟢 |
| `maxStorageBufferRange` 1줄 | `servers/rendering/rendering_device_commons.h` LIMIT 추가 | 🟡 |

### P1 — 1~2개월

| 항목 | 내용 | 판정 |
|------|------|:---:|
| 월드 파티션 매니저 | `load_threaded_*` 기반 셀 로드/언로드 + 우선순위 큐 | 🟡 |
| 폴리지 GPU 컬링 | HZB + compute cull + indirect MultiMesh | 🟡 |
| Terrain3D 리전 스트리밍 | PR #1020 기반 또는 자체 구현 | 🟡 |

### P2 — 2~4개월

| 항목 | 내용 | 판정 |
|------|------|:---:|
| LOD 관리자 | 거리 기반 자동 LOD 선택 | 🟡 |
| Terrain3D 인스턴스 충돌 | PR #699 기반 | 🟡 |
| 동굴/오버행 | godot_voxel 통합 또는 자체 | 🟡 |

### P3 — 6~12개월 (man-year)

| 항목 | 내용 | 판정 |
|------|------|:---:|
| HLOD 빌더 | 공간 클러스터링 + 프록시 메시 생성 | 🔴 |
| LOD 통합 스트리밍 | LOD + HLOD + 월드 스트리밍 통합 | 🔴 |

---

## 9. gaps 문서 §0 반영

[godot-openworld-engine-gaps.md](./godot-openworld-engine-gaps.md) §0 "월드 스트리밍·지형·폴리지" 요약표를 본 문서의 §0 마스터 요약표로 교체한다.

**§0은 §1(Nanite)과 §3(Virtual Texturing)의 선행 조건이다.** 스트리밍할 월드 표현이 없으면 지오메트리 스트리밍도 텍스처 스트리밍도 성립하지 않는다. 따라서 §0의 작업은 §1·§3보다 **먼저** 완료되어야 한다.

**업스트림에서 기대할 수 있는 건 텍스처 스트리밍(PR #113429)뿐이다.** terrain·partition·open world·foliage는 공식 우선순위에서 전부 0매치이며, 지형은 reduz가 "Terrain will not be added to Godot"으로 명시적으로 거부했다. **포크에서 직접 구축하는 것이 유일한 경로다.**

---

*리서치 방법: Godot 4.8-dev 소스트리 (commit `eda2a482e9`) 직접 grep·file read 실측. Terrain3D·godot_voxel GitHub 최신 상태 WebFetch. MultiMesh indirect draw PR #99455 머지 상태 확인. 모든 file:line은 로컬 트리 기준.*