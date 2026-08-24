# Godot 4.x에 원신급 VFX / 시네마틱 저작 UX 구현 — 딥리서치 & 기술 실측

> [godot-openworld-engine-gaps.md](./godot-openworld-engine-gaps.md) 의 "7. VFX / 시네마틱 저작 UX ⚠️" 항목을 실제 구현 관점에서 딥다이브한 문서.
>
> **결론 요약:** §7의 초판 진단("Niagara/Sequencer급 아티스트 저작 도구가 없다 / 런타임 기능은 있어 코드로는 만들 수 있으나 GUI 반복 튜닝 워크플로우가 없어 물량 병목")은 **Godot 4.8-dev 소스트리 실측 + 딥리서치 결과 방향은 정확하나 정밀화가 필요하다.** 핵심은 §1~5(Nanite·Lumen·VT·콘솔·프로파일러)와 성격이 **또 다르다**는 점이다:
> - Nanite/Lumen은 **렌더러 기능(시뮬레이션 능력) 자체가 없어서** man-year 코어 작업이 벽이었다.
> - VFX/시네마틱은 정반대다 — **런타임 시뮬레이션 능력은 이미 기능적으로 경쟁력 있게 존재한다**(GPU 컴퓨트 파티클, 어트랙터/충돌/서브에미터/벡터필드, 9종 애니메이션 트랙, 결정론적 오프라인 렌더러 `MovieWriter`). 없는 것은 **저작 레이어(authoring UX)** — 모듈 스택·노드 그래프·마스터 타임라인·프리셋 라이브러리라는 **에디터/데이터모델 추상화**다.
> - 따라서 이 격차는 **순수 렌더러 문제가 아니라 대부분 에디터·데이터모델 문제**이고, **상당 부분이 `EditorPlugin` + GDExtension으로 코어 수정 없이 접근 가능**하다(단 파티클 시킹·리심 같은 몇몇 훅은 국소 코어). 이는 §1~5보다 **낮은 진입장벽**을 의미한다.
>
> **진짜로 남은 격차는 4개다:** ① **Niagara식 모듈러 에미터/모듈 스택** 부재 — 파티클 설정이 단일 `ParticleProcessMaterial`의 평탄한 인스펙터 속성 목록(문자열로 셰이더를 코드생성), ② **마스터 시네마틱 시퀀서** 부재 — 다중 액터/카메라/오디오/이벤트를 한 트랙뷰로 조율하는 구조물이 전무(단일 `AnimationPlayer` 도프시트만), ③ **시네마틱 카메라 시스템**(블렌딩·디렉터·가상카메라) 코어 부재 — 서드파티(Phantom Camera)로 우회, ④ **VFX 프리셋/이펙트 라이브러리 + 라이브 프리뷰·시킹** 부재. 이 넷 모두 **주로 에디터/애드온 작업**이고, ①④의 파티클 시킹·리심만 국소 코어 훅이 필요하다.
>
> **근거:**
> - **외부 딥리서치 하니스** (105 에이전트, 5각도, 23 소스 페치, 100 주장 추출 → 25 주장 적대적 3표 검증 → **24/25 confirmed, 1 refuted**). Area 1(Niagara vs Godot 파티클)은 Epic UE 5.8 공식문서·Godot 공식문서로 만장일치 검증. Area 2~4(Sequencer/MRQ·생태계 애드온·구현 경로)는 워크플로우가 소스는 특정했으나 검증표에 안 올라, **1차 출처 직접 페치로 보강**함(아래 `[web]`).
> - **로컬 소스 실측** (`/Users/ryan/Workspace/godot` = Godot 4.8-dev 트리, file:line 직접 확인, 두 병렬 탐색 에이전트).
> - 아래 각 주장에 **[DR ✓]**(딥리서치 3-0 검증), **[web]**(1차 출처 직접 페치), 또는 **`file:line`**(로컬 실측) 근거를 명시.

---

## 0. 한 장 요약

| 기능 | AAA 방식 (UE) | Godot 4.8-dev 현황 | 구현 위치 · 난이도 |
|------|---------------------|---------------------|--------------------|
| **파티클 시뮬레이션 능력** | Niagara GPU/CPU 시뮬 | ✅ **기능 경쟁력 있음** — GPU 컴퓨트(64-wide), 어트랙터/충돌/서브에미터/벡터필드/터뷸런스/커브 | 🟢 이미 존재 (`particles.glsl`) |
| **모듈러 에미터/모듈 스택** | System>Emitter>Module 스택 | ❌ 단일 `ParticleProcessMaterial` 평탄 속성(문자열 셰이더 코드생성) | 🟡 EditorPlugin + 데이터모델(GDExtension) — AAA 필수, P1 |
| **노드 그래프 VFX 저작** | Niagara 노드그래프(HLSL 백엔드) | 🟡 **부분** — `VisualShader` 파티클모드(5스테이지 서브그래프)는 있으나 *퍼-파티클 셰이더 그래프*지 시스템/에미터 그래프 아님 | 🟡 VisualShader 위에 스택 레이어 구축(대체로 에디터) — AAA 필수, P1 |
| **GPU 파티클 ↔ 씬 깊이/GI (신규)** | 소프트 파티클, GI 수광 | ❌ `particles.glsl`에 깊이/GI 입력 없음 | 🟡 국소~중간 코어 셰이더 수정 |
| **볼류메트릭 VFX (신규)** | Volumetric Cloud/Fog/Smoke | ❌ `FogVolume`(로컬 포그)만, 진짜 볼류메트릭 없음 | 🟡 중간 코어 렌더러 |
| **유체·연기 시뮬 (신규)** | Niagara Fluids | ❌ 전무 | 🔴 man-year, 신규 서브시스템 |
| **파티클 라이팅·그림자 수광 (신규)** | 파티클 라이트·섀도 | ❌ `particles.glsl`에 라이팅/섀도 입력 없음 | 🟡 국소~중간 코어 셰이더 수정 |
| **데칼·블러드/데미지 누적 (신규)** | 메시 데칼, 동적 블러드 풀 | 🟡 `Decal` 노드 존재, 동적 누적 시스템 없음 | 🟢 GDExtension |
| **이펙트 프리셋/라이브러리** | Niagara 콘텐츠 예제·마켓 | ❌ 전무(수동 인스턴스화) | 🟢 애드온/에셋(서드파티 이미 존재) |
| **라이브 프리뷰·타임라인 시킹** | Niagara 타임라인 스크럽 | 🟡 **저수준 API 이미 존재**(PR #92089 4.4 병합, `request_particles_process`) — 에디터/애니 통합만 미완 | 🟢 에디터 배선(P1) + 선택적 국소 훅 |
| **마스터 시네마틱 시퀀서** | Sequencer 멀티트랙 타임라인 | ❌ 단일 `AnimationPlayer` 도프시트만, 다중 액터 조율 구조물 전무 | 🟡 EditorPlugin(순수 저작툴) — AAA 필수, P1 |
| **시네마틱 카메라(블렌딩/디렉터)** | Cine Camera + 카메라컷 트랙 | ❌ 코어 부재(에디터 "Cinematic Preview"는 팔로워일 뿐) → **Phantom Camera 애드온으로 우회** | 🟢 서드파티 성숙 / 🟡 자체 구축 |
| **시네마틱 모션블러·DOF (신규)** | Cinematic DOF/Motion Blur | 🟡 TAA 기반 모션블러 + 기본 DOF만, 시네마틱 품질은 별도 패스 필요 | 🟡 중간 코어 렌더러 |
| **오프라인 고품질 렌더러** | Movie Render Queue(AOV·누적AA·타일·EXR) | 🟡 **부분** — `MovieWriter`(`--write-movie`, 결정론적 fixed-fps, PNG+WAV/MJPEG/OGV)는 있으나 AOV·누적샘플·타일·EXR·잡큐 없음 | 🟡 P1 총합 (국소 코어 + GDExtension) |
| **자막/대사/스킵 컷신 흐름** | Sequencer 서브트랙 | ❌ 전용 트랙 없음(메서드 트랙으로 게임코드 우회) | 🟢 게임코드/애드온 |

**핵심 통찰:** §1~5와 달리 **"시뮬레이션 능력"은 이미 있고 "저작 UX"만 없다.** 저작 UX는 대부분 **에디터·데이터모델 = `EditorPlugin`/GDExtension 사정권**이므로, 원신급 스타일라이즈드 타깃엔 **자체 저작툴 구축이 현실적**이다. 순수 렌더러 개조(man-year)가 아니다.

---

## 1. VFX 저작: Niagara vs Godot 파티클 — 격차는 "능력"이 아니라 "저작 추상화"

### 1.1 Niagara의 저작 아키텍처 (딥리서치 검증)

Niagara는 **3단계 컨테이너 계층의 모듈러 스택**이다:
- **System**(에미터들의 컨테이너, 하나의 이펙트로 결합) → **Emitter**(스택 패러다임, 모듈들의 컨테이너) → **Module**(재사용 가능한 동작 단위). "A system is a container for emitters. The system combines these emitters into one effect"; "Emitters work in a stack paradigm—they serve as containers for modules." **[DR ✓ 3-0, merged from 8 claims]** *(주의: 초기 "4단계(모듈이 서브모듈 포함)" 주장은 검증에서 1-2로 **기각**됨 — 정확히는 3 컨테이너 레벨.)*
- 모든 모듈은 **실행 그룹**(System/Emitter/Particle/Render)에 배정돼 **스택 위→아래 순서로** 실행 → spawn/update/render 단계 분해. "every module is assigned to a group that describes when the module is executed." **[DR ✓ 3-0]**
- 모듈은 **노드 그래프(Script Editor)에서 시각적으로** 저작되며 그 그래프는 HLSL 스크립트의 시각적 표현. 인라인 HLSL이 필요하면 **CustomHLSL 노드**. "Modules are built using HLSL, but can be built visually in a Graph using nodes... You can even write HLSL code inline, using the CustomHLSL node." **[DR ✓ 3-0]**
- **생산성 포지셔닝(Epic 명시):** "the technical artist has the ability to create additional functionality on their own, without the assistance of a programmer" / "Once you create a module, anyone else can use it." **[DR ✓ 3-0]** *(주의: Epic의 설계의도/마케팅 포지셔닝이지 독립 벤치마크 아님.)*
- **시뮬-렌더 분리:** "you can create simulation behavior once, then assign multiple renderers"(Sprite/Mesh/Ribbon…). **[DR ✓ 3-0]**
- **에미터 간 이벤트 시스템:** Events 모듈이 Location/Death/Collision 이벤트 생성, 다른 에미터의 Event Handler가 청취·반응 → 코드 없는 에미터 상호작용. **[DR ✓ 3-0]** (단 이벤트는 CPU 시뮬 전용.)
- 프리빌트 스폰 모듈(Spawn Rate/Burst Instantaneous/Per Frame/Per Unit)을 드래그인으로 조합. **[DR ✓ 3-0]**

### 1.2 Godot의 현황 — 시뮬 능력은 있으나 저작 모델은 평탄 (로컬 실측)

**시뮬레이션 능력은 기능 경쟁력 있음:**
- GPU 시뮬은 **컴퓨트 셰이더**(`servers/rendering/renderer_rd/shaders/particles.glsl:1` `#[compute]`, `local_size_x=64` L7, `main()` L230), 디스패치 `particles_storage.cpp:1207-1230`. GPU/CPU는 **엔진레벨 구분**(`GPUParticles3D`/`CPUParticles3D` 별도 클래스, `gpu_particles_3d.h:38` / `cpu_particles_3d.h:40`). **[DR ✓ 3-0]** + 로컬.
- 어트랙터(Sphere/Box/VectorField `gpu_particles_collision_3d.h:272-367`), 충돌(Sphere/Box/**SDF**/HeightField `:38-267`), 트레일(`gpu_particles_3d.h:86-87`), 서브에미터(`:81`, 셰이더 `emit_subparticle()` `particles.glsl:197`), 터뷸런스·색/스케일/회전 커브(`particle_process_material.h:357-378`). Godot 4.0이 서브에미터·구/박스 어트랙터·3D 벡터필드를 추가했으나 **노드그래프 저작툴은 도입하지 않음**. **[DR ✓ 3-0]** + 로컬.

**그러나 저작 모델은 평탄한 단일 리소스:**
- **`ParticleProcessMaterial`은 고정 파라미터 세트가 `shader_type particles` 셰이더를 문자열로 코드생성**한다 — 노드 그래프가 아니다. `_update_shader()` `particle_process_material.cpp:160`가 `String code = ...`(L197) → `code += "shader_type particles;\n"`(L199) → 수백 줄의 `code += "uniform float ...";`를 비트필드 `MaterialKey`(헤더 L113-150) 조건분기로 붙인다. 모든 동작이 **사전에 구워진 분기**이고, 모듈 삽입·스택·재사용 개념이 없다. **[DR ✓ 3-0]** + 로컬.
- 파라미터는 **min/max + 라이프타임 커브 텍스처** 모델(`params_min/max[]` + `tex_parameters[PARAM_MAX]` `particle_process_material.h:325-329`). 에디터 지원은 전용 그래프가 아니라 **개선된 min/max 슬라이더 위젯**(`ParticleProcessMaterialMinMaxPropertyEditor` `particle_process_material_editor_plugin.h:44`)뿐. **[DR ✓ 3-0]** + 로컬.
- **하위 이스케이프 해치는 수작업 파티클 셰이더** — `start()`/`process()` 두 함수를 Godot 셰이더 언어로 직접 작성(`shader_types.cpp:373-421`, 쓰기가능 빌트인 COLOR/VELOCITY/CUSTOM/USERDATA1-6/TRANSFORM). **[DR ✓ 3-0]** + 로컬.
- **부재 실측:** 파티클 프리셋/이펙트 라이브러리 **전무**(grep이 파티클 관련 반환 0), 전용 이펙트 저작 패널 **전무**(파티클 에디터 플러그인은 Convert/Restart/AABB생성/방출점베이크 메뉴 정도, `particles_editor_plugin.h:47-50`). 헤더 TODO(`particle_process_material.h:39-44`)가 "Path following, Emitter positions deformable by bones, Proper trails"를 미완으로 명시. **로컬.**

### 1.3 가장 가까운 기존 자산 — `VisualShader` 파티클 모드 (로컬 실측)

`modules/visual_shader/`에 **진짜 노드 그래프**가 있고 파티클을 지원한다:
- `Shader::MODE_PARTICLES` 처리(`visual_shader_editor_plugin.cpp:1721,2494,8377`), **5개 스테이지 서브그래프**(`VisualShader::Type` `visual_shader.h:46-54`: START/PROCESS/COLLIDE/START_CUSTOM/PROCESS_CUSTOM) — 개념상 Niagara Spawn/Update/Event 스테이지와 유사.
- 파티클 전용 노드: SphereEmitter/BoxEmitter/RingEmitter/MeshEmitter(`visual_shader_particle_nodes.h:64-113`), ConeVelocity/Randomness/Accelerator/Output/Emit(`:196-325`). 팔레트 카테고리 Particles/Emitters/Velocity/Transform(`visual_shader_editor_plugin.cpp:7436-7449`). 스테이지 플래그(`TYPE_FLAGS_EMIT|PROCESS|COLLIDE`)로 게이팅.
- **한계(결정적):** 이 그래프는 **한 파티클의 동작**을 저작하는 퍼-파티클 셰이더 그래프지, **에미터-시스템 합성 그래프가 아니다.** 스폰레이트 모듈, 시스템레벨 이벤트 핸들러, 데이터 인터페이스 개념이 없다. **로컬.**

### 1.4 진짜 생산성 격차 (딥리서치 종합) [DR ✓]

Godot-Niagara 격차는 **원 시뮬 능력이 아니라 구조/UX**다. Godot엔 기능 경쟁력 있는 파티클 프리미티브(GPU/CPU·방출형·힘·어트랙터·터뷸런스·충돌·커브·서브에미터·벡터필드)가 있으나 **Niagara의 4대 생산성 추상화가 없다:**
1. **명명된 실행 그룹을 가진 모듈러 에미터/모듈 스택**,
2. **재사용·프로젝트 전역·아티스트 저작 모듈**(하드코딩 대신 조합),
3. **HLSL 백엔드 노드그래프 모듈 저작**,
4. **에미터 간 이벤트 시스템**.
Godot의 저작은 평탄한 단일 리소스 인스펙터 + 선택적 수작업 `start()/process()` 셰이더. **[DR ✓, 만장일치 클레임셋에서 파생]** Godot 자체 로드맵도 "아티스트 친화적 VFX 파이프라인"을 **미래 작업**으로 인정. **[web: State of particles]**

---

## 2. 시네마틱 저작: Sequencer + MRQ vs AnimationPlayer + MovieWriter

### 2.1 UE Sequencer + Movie Render Queue (딥리서치·1차 출처)

- **Sequencer = 마스터 멀티트랙 타임라인** — "a multi-track editor used for creating and previewing cinematic sequences in real time", 카메라/캐릭터/라이트/오브젝트/이벤트를 **한 비선형 편집 환경에서** 조율. **[web]**
- **Possessables vs Spawnables** — Possessable은 레벨의 기존 액터에 바인딩, Spawnable은 시퀀스가 임시 액터/라이트/오브젝트를 생성. "Spawn temporary Actors, lights, and other objects... by using Spawnables." **[web]**
- **Camera Cuts 트랙** — 단일 시퀀스 내 다중 카메라 편집. **[web]**
- **Take Recorder** — Editor/Gameplay/Live Link 액터의 퍼포먼스를 시퀀스로 녹화(모캡 워크플로우). **[web]**
- **Movie Render Queue / Graph** — 다중 **렌더 패스(AOV)**, 이미지 시퀀스 오프라인 렌더, 프로페셔널 포스트프로덕션 품질. **[web]** (누적 AA 샘플·타일 하이레스·EXR/리니어는 MRQ의 핵심 차별점.)

### 2.2 Godot의 현황 (로컬 실측)

**있는 것 — 유능한 단일소유 애니메이션 시스템 + 결정론적 오프라인 렌더러:**
- **`AnimationMixer`/`AnimationPlayer`는 루트노드 하위 어떤 노드의 어떤 속성이든 키잉 가능**(`animation_mixer.h:133` `root_node`, 트랙은 `node_path:property`). **9종 트랙**(`animation.h:48-58`): VALUE/POSITION_3D/ROTATION_3D/SCALE_3D/BLEND_SHAPE/**METHOD**/BEZIER/**AUDIO**/**ANIMATION**(중첩 애니플레이어 트리거). 오디오 트랙은 자식 AudioStreamPlayer에 스트림 스케줄(`animation_mixer.cpp:913-924`), 메서드 트랙은 deferred/즉시 임의인자 호출(`:2088-2106`) — 컷신 중 게임이벤트 발사 메커니즘. **로컬.**
- **트랙 에디터 = 풀 도프시트** — 줌 타임라인(`AnimationTimelineEdit` `animation_track_editor.h:188`), 베지어 커브 편집(`:191`), 스냅(`:643-647`), 명명 마커(`AnimationMarkerEdit` `:290`), 오니언스키닝·핀·블렌드타임 에디터(`animation_player_editor_plugin.h:100-129`). **로컬.**
- **`MovieWriter` = 경량 오프라인 결정론 렌더러** — `--write-movie <file>`(`main.cpp:568,1888-1897`), **fixed-fps 강제**(결정론, `:1892-1893`, `main_timer_sync.set_fixed_fps`), 오디오는 더미드라이버로 프레임당 결정론적 믹스·먹싱(`movie_writer.cpp:244-247`), 프레임은 뷰포트 텍스처 그랩+HDR→sRGB(`:200-237`). **3종 라이터**: PNG+WAV(`movie_writer_pngwav.cpp`), MJPEG-AVI(`modules/jpg/movie_writer_mjpeg.h:36`), Theora-OGV(에디터전용 `modules/theora/editor/movie_writer_ogv.h:40`). GDExtension으로 `_write_frame` 서브클래싱 가능(`movie_writer.h:74-79`). **로컬.**

**없는 것 — 시네마틱 조율 계층 전체:**
- **마스터 시퀀서 구조물 전무.** `scene/ servers/ editor/` 전역에서 `sequencer`/`cutscene` 대소문자무시 검색 **0매치**. `timeline`은 단일-애니플레이어 도프시트 위젯만 매치. 트랙 에디터는 **한 번에 하나의 `AnimationMixer`에 바인딩**(`animation_player_editor_plugin.h:52`). 다중 액터/카메라/오디오를 별도 서브시퀀스로 한 트랙뷰에서 조율하는 Sequencer식 구조가 **없다** — 트리 상단의 한 AnimationPlayer가 여러 노드패스를 키잉하거나 `TYPE_ANIMATION` 트랙으로 자식 플레이어를 트리거하는 **관례**로만 가능. **로컬.**
- **자막/대사 트랙 전무**(`subtitle|caption|dialogue` 검색은 애니 노드 `get_caption()`만 매치, 무관). 오디오 동기는 `TYPE_AUDIO`뿐, 텍스트 채널 없음 → 메서드 트랙으로 UI 호출하는 게임코드로 우회. **로컬.**
- **스킵 가능 컷신 흐름 전무** — 재생제어(`play/stop/seek`)+라이프사이클 시그널(`animation_finished`)만. 스킵/페이드/블로킹은 게임코드. **로컬.**
- **`MovieWriter`는 MRQ 미달** — 렌더패스/AOV 없음, 멀티샘플 누적·공간/시간 AA 패스 없음, 타일 하이레스 없음, 샷별 잡큐 없음, 리니어/EXR 없음 — **프레임당 단일 뷰포트 그랩 + 기본 이미지+오디오 먹싱**. **로컬.**

### 2.3 시네마틱 카메라 (로컬 실측)

- **코어에 카메라-블렌드/디렉터/가상카메라/돌리-릭 시스템 없음.** `Camera3D`(`camera_3d.h:40`)의 `make_current()` 스위칭과 `InterpolatedProperty`(물리보간 전용, 시네마틱 블렌드 아님)만. 게임코드가 수동 배선할 빌딩블록(`PathFollow3D` 돌리, `RemoteTransform3D` 구동, `look_at`)은 있으나 결합 제약/블렌드 계층 없음. Godot 3의 `InterpolatedCamera`는 제거됨. **로컬.**
- 에디터 **"Cinematic Preview"**(`node_3d_editor_viewport.h:211` `VIEW_CINEMATIC_PREVIEW`)는 실행 씬이 current로 표시한 카메라를 **미러링하는 팔로워**일 뿐, 카메라 시퀀서/저작툴이 아니다. **로컬.**

---

## 3. 기존 Godot 생태계 솔루션 (1차 출처 페치)

| 애드온/제안 | 무엇 | 성숙도 | 성격 |
|-------------|------|--------|------|
| **Phantom Camera** (ramokz) | Cinemachine 유사 — 팔로우/룩앳(댐핑), 다중노드 리프레이밍, 카메라 간 트랜지션, 우선순위 스위칭. Camera2D/3D 확장 | ✅ **성숙·널리 채택** (64 리뷰, "must have", v0.11, Godot 4.4+, MIT, C# 지원). 유니티→Godot 이주자의 Cinemachine 공백을 메움 | 🟢 시네마틱 카메라 격차의 **실질 해답** |
| **godot-sequencer** (jimmybeer) | 타임라인 기반 시네마틱/스크립트 오케스트레이션 — 카메라/애니/오디오/VFX/게임이벤트를 **다중 액터에 걸쳐** 조율 | ⚠️ **프로토타입**(1 star, 15 commits, GDScript `addons/sequencer/`, 릴리스·문서 최소) | 🟡 방향은 정확하나 미성숙 |
| **particle-scene-compositor** (tvenclovas96) | 다중 파티클 노드를 하나의 이펙트로 합성 — 에디터 실시간 프리뷰 패널, `SyncNode`로 동기 재생(one-shot/수동), 머티리얼 유니크화 | ⚠️ 모디스트(13 stars, 16 commits, MIT, GPU/CPU×2D/3D, GDScript/C#) | 🟡 **노드 그래프 아님** — 씬 기반 합성("VFX 프리팹 조합"에 가까움) |
| **VFX Animation Player** (Suligoy) | 애니메이션 에디터를 파티클 에미터에 반응시켜 **타임라인 스크럽으로 프레임별 파티클 확인** | 에셋스토어 배포 | 🟢 시킹 UX 격차를 애드온으로 부분 완화 |
| **스타일라이즈드 VFX 팩** (Bukkbeek 등) | 드래그드롭 사용 준비된 로우폴리 이펙트 씬(플러그인 불요, 크기/색/타이밍 커스터마이즈) | 상용 에셋 | 🟢 **프리셋 라이브러리 공백을 콘텐츠로** 메움 |

**관련 Godot proposals (1차 출처):**
- **#1356** (2020-08) — 유니티 Timeline식 컷신/시네마틱 에디터(드래그드롭 타임라인) 요청. **Closed/Archived.** **[web]**
- **#4449** — **파티클 시킹/스크러빙** 요청. "both Unity (shuriken) and Unreal (Niagara) offer the ability to seek through the timeline." 상태를 저장해 시킹 가능 재생(메모리↔기능 트레이드). **VFX/Techart wishlist에서 archived/completed.** **[web]**
- **#12694** (2025-06) — `AnimationPlayer`에 **ParticleTrack** 추가(emission/burst 키프레임, 타임라인 스크럽·일시정지·스텝, 리심 PR **#92089** 연동). **Open, "Needs consensus".** *노드그래프 저작이 아니라 프로 VFX 워크플로우 타깃.* **[web]**
- **로드맵(State of particles):** 저자 명시 — "My goal, first and foremost, is to enable an artist-friendly workflow... reducing the number of technicalities and 'gotchas' that Godot currently puts in front of technical artists." 계획: **process material의 비주얼셰이더 지원**, 추가 비주얼셰이더 노드, 스폰 옵션 확장. 단 **전담 기여자 없음**, 명시적 노드기반 파티클 에디터 계획은 없음. **[web]**

**요지:** 시네마틱 **카메라**는 Phantom Camera로 사실상 해결됐고, 파티클 **시킹/애니 트랙 통합**은 코어 proposal이 진행 중(#12694 open, #4449 완료 방향, 리심 PR #92089)이다. 반면 **마스터 시퀀서**와 **Niagara식 노드 VFX 저작**은 성숙한 해답이 아직 없다(godot-sequencer는 프로토타입, particle-compositor는 그래프 아님).

---

## 4. 구현 경로 — 원신급 스타일라이즈드 타깃

### 4.1 왜 §1~5보다 진입장벽이 낮은가

§1~5(Nanite/Lumen/VT/콘솔/프로파일러)의 벽은 **렌더러가 그 기능을 못 한다**였다. VFX/시네마틱은 **런타임이 이미 할 수 있고 저작 UX만 없다.** 저작 UX = 에디터 패널 + 데이터모델(Resource) + 기존 런타임 호출 → **대부분 `EditorPlugin`(GDScript/C#) + GDExtension 사정권, 코어 수정 0~국소.** 이는 §1~5와 근본적으로 다른 비용 구조다.

### 4.2 경로 (난이도순)

**🟢 P0 — 서드파티/콘텐츠로 즉시 메우기 (코어 0, 수일~수주)**
- 시네마틱 카메라 → **Phantom Camera 채택**. VFX 프리셋 공백 → **스타일라이즈드 VFX 팩 + 자체 프리팹 라이브러리**(씬으로 저장한 `GPUParticles3D`+머티리얼을 재사용). 파티클 합성 → **particle-scene-compositor** 또는 자체 `SyncNode` 패턴. 스크럽 → **VFX Animation Player**.
- 컷신 → 트리 상단 마스터 `AnimationPlayer` + `TYPE_METHOD`/`TYPE_AUDIO`/`TYPE_ANIMATION` 트랙 관례 + 커스텀 `CutsceneDirector`(시그널/async) 클래스.

**🟢 P1 — 데이터주도 VFX 시스템 + 자체 에디터 (순수 EditorPlugin + GDExtension, 코어 0, 수주~수개월)**
- `ParticleProcessMaterial` 위에 **모듈 스택 데이터모델**(Resource 배열: 각 모듈이 `ParticleProcessMaterial` 필드셋 or 셰이더 스니펫에 매핑)을 얹고, `EditorPlugin`으로 **모듈 리스트 UI + 재사용 프리셋 + 라이브 프리뷰 뷰포트**를 붙인다. 저작 결과를 기존 문자열-코드생성 파이프라인이나 커스텀 파티클 셰이더로 컴파일 → **코어 무수정.** VisualShader 파티클모드(5스테이지)를 퍼-파티클 로직의 서브스트레이트로 재사용 가능.
- **시네마틱 시퀀서 저작툴**: 다중 `AnimationPlayer`/카메라/오디오를 조율하는 **마스터 타임라인 EditorPlugin**(godot-sequencer의 성숙판) — 순수 에디터·게임코드 계층이라 코어 불요.

**🟡 P2 — 국소 코어 훅 (파티클 시킹·리심·MovieWriter 확장, 수일~수주)** — *상세: [§5](#5-p2-구현-상세--국소-코어-훅-파티클-시킹--moviewriter--mrq)*
- 파티클 **시킹/리심은 이미 4.4에 저수준 API 병합**(PR #92089, `request_particles_process`) → 남은 건 에디터 스크럽/ParticleTrack 배선(대부분 P1)과 선택적 스냅샷 최적화·`TIME` 결정론 수정(국소).
- `MovieWriter` → **EXR/리니어 HDR은 `movie_writer.cpp:235-238` ~4줄 게이팅**(인코더 이미 존재)이 최고 레버리지. 타일은 스크립트~국소, 누적AA는 국소~중간, 진짜 멀티 AOV만 대규모.

**🔴 P3 — 대규모 (GPU 이벤트/데이터 인터페이스 + 유체·연기 시뮬, man-year급)**

- Niagara급 GPU 이벤트·데이터 인터페이스·시뮬-렌더 완전분리, 유체·연기 시뮬(Fire/Smoke/Fluid)만 man-year. **모듈 스택·노드 그래프·마스터 시퀀서는 P1으로 재개봉 — AAA 타깃에선 P1으로 대부분 커버, 잔여 man-year만 P3.**
- GPU 유체 솔버는 신규 서브시스템으로, 코어 작업이자 man-year 규모. AAA 화재·연기·액체 VFX의 핵심.

### 4.3 현실적 대안 / 권장 (AAA 기준, 2026-08-18 개정)

- **AAA 목표엔 P0+P1으로 핵심 격차 해소 가능.** 모듈 스택·마스터 시퀀서·노드 그래프는 EditorPlugin으로 구축(코어 0). 원신과 달리 AAA는 시네마틱 컷신이 대량이므로 마스터 시퀀서는 P1 필수.
- **시네마틱은 Phantom Camera(카메라) + 마스터 시퀀서 EditorPlugin(다중 액터/카메라/VFX/오디오) + MovieWriter(EXR·누적AA·멀티 AOV)** 조합이 실전 경로.
- **핵심 판단:** 이 격차는 §1~5와 달리 **"엔진을 못 고쳐서"가 아니라 "저작툴을 아직 안 만들어서"**다. 팀에 **에디터 툴 엔지니어 1명**이 있으면 P0+P1로 아티스트 생산성 병목의 대부분을 코어 수정 없이 해소할 수 있다. 단, 유체·연기 시뮬·GPU 이벤트·볼류메트릭 VFX는 코어 작업이 필요.

---

## 5. P2 구현 상세 — 국소 코어 훅 (파티클 시킹 + MovieWriter → MRQ)

> P0/P1은 코어 0이라 자명하므로, 이 섹션은 **P2(국소 코어 훅)만** file:line seam·참조 아키텍처·검증표로 딥다이브한다. 근거: 로컬 실측 2건(파티클 시킹 seam, MovieWriter seam) + 딥리서치(102 에이전트, PR/proposal·UE MRQ·Unity VFX 8/8 high-confidence). 각 주장에 **[DR ✓]**(딥리서치 검증) / **`file:line`**(로컬 실측) 명시.

### 5.A 파티클 시킹 / 리시뮬레이션 — **이미 4.4에 저수준 API 병합됨** (핵심 반전)

**딥리서치 핵심 반전:** §4의 초안은 시킹을 "proposal 진행 중"으로 적었으나, **PR #92089 "Implement particle seek request and seed options"가 2025-01-13 Godot 4.4에 병합**됐다(커밋 `133db1fd`, 작성자 QbieShay=파티클 메인테이너, 머지 akien-mga). **[DR ✓ high]** 즉 **저수준 시킹 API는 이미 코어에 존재**하고, 남은 것은 **에디터/애니메이션 통합**뿐이다. proposal #4449(particle seeking)는 이 작업으로 **Closed**, #12694(ParticleTrack)는 통합을 요청하며 **Open "Needs consensus"**. **[DR ✓]**

**로컬 트리 실측 — 그 API가 실재한다:**
- 노드 API: `GPUParticles3D::request_particles_process(process_time, residual)` (`gpu_particles_3d.cpp:485-499`, 바인딩 `:865`). 사용 패턴 = `restart()` → `request_particles_process(target)`. **[DR ✓ + 로컬]**
- RS/스토리지: `particles_request_process_time(RID, time, residual)` (`particles_storage.cpp:378-383`) → `request_process_time`/`request_process_time_residual`를 세팅.
- **`residual`(sub-frame) 파라미터가 이미 시그니처에 있다** — 이는 MRQ 시간 서브샘플링과의 결합축(§5.C)을 위한 설계 여지. Niagara도 UE 4.27에서 Desired Age에 서브프레임 전진을 추가해 MRQ 시간 서브샘플과 상호작용하게 만들었다 — **파티클 시킹과 오프라인 샘플링은 함께 설계해야 함**을 보여주는 직접 선례. **[DR ✓]**

**실질 seam = `pre_process_time` 웜업 루프 (로컬 실측):**
- 워밍업 루프 `particles_storage.cpp:1563-1601`가 "고정 서브스텝으로 목표 wall-time까지 시뮬을 전진"시킨다. `request_process_time`가 이 루프(`:1564-1599`)에 그대로 먹여진다. 단일스텝 프리미티브는 컴퓨트 디스패치 `_particles_process`(`~:1179-1234`). **로컬.**
- 시킹 = `restart()`(clear/phase 0로 리셋, `:1460-1465`) + `fixed_fps>0` + 이 루프를 `todo=target`으로 실행. 구조적으로 이미 존재하는 것을 **임의 목표시간에 반복 호출 가능하게 노출한 것**이 PR #92089의 본질.

**결정론 요건 & 한계 (로컬 + DR 교차검증):**
- **고정 시드 필수** — PR #92089가 `use_fixed_seed`/`seed`를 추가한 이유: "otherwise any restart() would change the seed, which makes seeking impossible". **[DR ✓]** 로컬: `set_emitting(true)` 시 `!use_fixed_seed`면 `set_seed(Math::rand())`로 재랜덤화(`gpu_particles_3d.cpp:51-53`) → 시킹엔 `use_fixed_seed=true`가 전제. 셰이더 랜덤은 `hash()` 정수해시(`particles.glsl:190-195`)+시드로 완전 결정론. **로컬.**
- **전진 전용, 역방향 불가** — GPU 순방향 적분이라 t'<t로 가려면 0으로 리셋 후 t' 재시뮬(비용 O(t'/substep)). Niagara Desired Age("scrubbed backward → reset and tick up from the beginning"), Unity VFX Graph(역재생 시 "high peak in GPU use ... uses Simulate", Max Scrub Time 상한)와 **동일한 근본 한계**. **[DR ✓]** 스냅샷/복원으로 최적화 가능 — 로컬에 리드백 프리미티브 실재(`buffer_get_data(particle_buffer)` `particles_storage.cpp:667`, AABB용으로 이미 사용) + `buffer_update` 복원 경로 존재하나, **스냅샷 API는 미구축**. **로컬.**
- **⚠️ 결정론 gap — `TIME`:** `frame_params.time`이 wall-clock total time(`particles_storage.cpp:864`)이라 웜업 서브스텝 동안 증가 안 함 → **`TIME`을 샘플하는 프로세스 머티리얼은 리시뮬 시 비결정론**. phase/seed/delta 기반은 결정론. 시킹툴 문서화 필수 제약 or 국소 수정 대상. **로컬(딥리서치 미포착 — 로컬 고유 발견).**

**남은 작업(에디터/애니 통합) — 대부분 P1 성격:**
- `emitting`은 **이미 keyable BOOL 프로퍼티**(`gpu_particles_3d.cpp:869`, `PROPERTY_HINT_ONESHOT`)라 표준 `TYPE_VALUE` 트랙으로 on/off 키잉 가능. 없는 건 **AnimationMixer 스크럽 시 시킹 API를 호출하는 `ParticleTrack`**(#12694) — `Animation::TrackType`(`animation.h:57`)에 파티클 타입 없음. **로컬 + [DR ✓].**
- 즉 P2-A의 코어 훅은 **이미 병합 완료**. 남은 건 (i) 스냅샷 save/restore API(선택적 최적화, 국소 코어), (ii) `TIME` 비결정론 문서화/수정(국소), (iii) ParticleTrack + 에디터 스크럽 UX(**순수 에디터=P1**). → **P2-A는 사실상 P1로 강등 가능.**

### 5.B MovieWriter → MRQ — seam별 정밀 매핑 (로컬 실측)

**최고 레버리지 단일 수정 (로컬 실측):** `add_frame()`의 캡처 체인은 HDR을 `movie_writer.cpp:234`까지 보존한다 — 뷰포트가 HDR-2D면 `texture_2d_get`이 **`FORMAT_RGBAH` 리니어 half-float**를 반환(`texture_storage.cpp:1503,1902-1922`). 그런데 `movie_writer.cpp:235-238`의 무조건 `convert(FORMAT_RGBA8)` + `linear_to_srgb()`가 그 정밀도를 파괴한다. **이 4줄을 "writer가 리니어 HDR 요청" 플래그로 게이팅하면** EXR/리니어 출력이 열린다 — **인코더는 이미 존재**(`Image::save_exr()` `image.cpp:2852`, `modules/tinyexr/`, RGBAH/RGBAF 지원 `image_saver_tinyexr.cpp:42-57`). **로컬.**

**MRQ 참조 아키텍처 (딥리서치):**
- MRQ는 출력 프레임당 다중 서브렌더 누적 — **시간 샘플**(셔터를 서브프레임으로 슬라이스, 엔진 모션블러로 보간; 시간 경과함)과 **공간 샘플**(시간 경과 없이 카메라를 지터 오프셋해 슈퍼샘플링 AA 누적). **[DR ✓ high]** 타일 하이레스(GPU 텍스처/메모리 한계 초과, 단 TAA·SSR·블룸·모션블러 비활성). **[DR ✓]** 멀티레이어 EXR + AOV(Final Image/Object IDs=Cryptomatte/World Depth/Motion Vectors + Lighting/Reflection/Unlit/Path-tracer). **[DR ✓]**

**Feature → Godot seam → 난이도 (로컬 실측):**

| MRQ 기능 | Godot seam (file:line) | 난이도 |
|----------|------------------------|--------|
| 커스텀 인코더/컨테이너, 프레임별 PNG | `_write_frame` 가상(`movie_writer.h:78`) | 🟢 **GDExtension만** |
| **EXR / 리니어-HDR 출력** | HDR이 `movie_writer.cpp:234`까지 보존, `:235-238`에서 파괴; 인코더 준비됨(`image.cpp:2852`) | 🟢 **국소 코어 ~4줄** (convert 게이팅) |
| 타일 하이레스 | `Camera3D::set_frustum`/`frustum_offset`(`camera_3d.cpp:340,296`) + 캡처 루프 | 🟡 **스크립트 가능** or 국소 `add_frame` 루프; 렌더러 무수정 |
| 시간+공간 AA 누적(슈퍼샘플) | Halton 지터(`renderer_scene_cull.cpp:55,2688`), `CameraData::set_camera` taa_jitter(`renderer_scene_render.cpp:38`), TAA resolve 누적(`taa_resolve.glsl:333`) | 🟡 **국소~중간 코어** (메인루프 다중 서브프레임 렌더 구동; 평균화는 GDExtension) |
| AOV(color/depth/normal/motion/specular) | **CompositorEffect `needs_*` 플래그 + `RenderingDevice.texture_get_data`** (상세 §5.D) | 🟢 **코어 0 (GDExtension)** — 정정됨, "대규모"는 일부 AOV만 |
| AOV(albedo/metallic/emission/objectID) | Forward+가 버퍼 미보존(`MODE_RENDER_MATERIAL`은 UV2 베이크 전용) | 🔴 신규 MRT 어태치먼트(중간 렌더러) or reduz proposal #7916 편승 |
| 모션블러(누적) | velocity 버퍼 존재+강제가능(`needs_motion_vectors`), 블러 패스 없음 | 🟡 누적 seam으로 에뮬(§5.C 4번) |
| 결정론 캡처(전제) | `main_timer_sync.cpp:433-434`, `main.cpp:1888-1896,5047` | ✅ **이미 존재** |

**GDExtension 서브클래스 한계 (로컬):** 오버라이드 가능은 `_write_frame`/`_write_begin`/`_write_end`/`_handles_file`/오디오 레이트(`movie_writer.h:71-79`)뿐. `add_frame()` 코어가 (i) 뷰포트 컬러 RT만 캡처, (ii) 단일 `movie_size` 크롭/리사이즈, (iii) **강제 LDR sRGB 변환**(`:235-238`), (iv) 프레임당 이미지 1장+오디오 1블록만 전달(`write_frame(const Ref<Image>&, const int32_t*)`)로 하드와이어 → **다른/다중/HDR 소스가 필요한 모든 기능은 코어 수정 필수.** 시그니처가 단일 이미지라 타일/서브프레임 다중전달도 불가.

### 5.C P2 권장 실행 순서 (ROI순)

1. **🟢 즉시 (국소 ~4줄):** `movie_writer.cpp:235-238` 게이팅 → **EXR/리니어 HDR 출력**. 인코더 이미 존재, 렌더러 무수정. 단일 최고 레버리지 — HDR 소스가 열리면 이후 누적/타일 로직도 이 이미지를 재사용.
2. **🟢 병합 활용 (코어 0):** 파티클 **시킹은 이미 4.4 API 존재** → `restart()`+`request_particles_process(t)`를 에디터 스크럽/ParticleTrack에 배선(P1). `use_fixed_seed=true` 강제 + `TIME` 미사용 가이드.
3. **🟡 타일 하이레스 (스크립트~국소):** `Camera3D.frustum_offset`로 오프축 서브프러스텀 렌더 후 CPU 합성. 렌더러 무수정. (스크린스페이스 이펙트 심 seam은 MRQ와 동일 — 오버스캔으로 완화.)
4. **🟡 누적 슈퍼샘플링 (국소~중간):** 기존 Halton 지터 경로 재활용 — 출력 프레임당 N 지터 서브프레임 렌더 후 평균. 지터 입력은 존재, 없는 건 **게임로직 미전진 다중렌더 구동**(메인루프 훅) + 누적버퍼. 시간샘플(모션블러)은 `residual` 서브프레임과 결합(§5.A, Niagara/MRQ 선례). 평균화 자체는 GDExtension.
5. **🟢/🔴 멀티 AOV (절반은 코어 0, 절반은 중간 렌더러):** 상세는 아래 **§5.D**. 정정 — 당초 "대규모 렌더러"로 적었으나 실측 결과 **color/depth/normal/motion/specular는 GDExtension `CompositorEffect`로 코어 수정 없이 추출 가능**하고, albedo/objectID만 신규 MRT가 필요하다.

---

## 5.D 멀티 AOV / 시네마틱 렌더 패스 — 코어/GDExtension 경계 확정

> 🔴로 분류했던 멀티 AOV를 딥다이브한 결과 **경계가 절반으로 쪼개진다.** 근거: 로컬 실측 2건(Forward+ 버퍼 내부, CompositorEffect seam) 상호확증 + 딥리서치(103 에이전트, MRQ/Cryptomatte/HDRP/Bevy 규격 검증) + tinyexr 로컬 확인.

### 5.D.1 핵심 반전 — CompositorEffect가 sanctioned seam

Godot의 **`CompositorEffect`**(`scene/resources/compositor.h:39`)는 렌더 파이프라인 스테이지(PRE_OPAQUE/POST_OPAQUE/POST_SKY/PRE_TRANSPARENT/POST_TRANSPARENT, `compositor.h:43-50`)에 GDExtension 로직을 삽입하고 `RenderSceneBuffersRD`를 노출하는 **공식 메커니즘**이다. 결정적으로 **`needs_*` 플래그가 해당 버퍼 할당을 강제**한다:

| CompositorEffect 플래그 | 강제 할당되는 버퍼 | 할당 경로 (로컬 실측) |
|-------------------------|--------------------|------------------------|
| `needs_normal_roughness` | normal-roughness (R8G8B8A8, view-space normal+roughness) | `render_forward_clustered.cpp:1732,1935` → `ensure_normal_roughness_texture()` `:63-72` |
| `needs_motion_vectors` | velocity (R16G16_SFLOAT) — **TAA 없이도 강제** | `:1731,1812` → `ensure_velocity()` `render_scene_buffers_rd.cpp:730` |
| `needs_separate_specular` | specular 버퍼 (POST_SKY 한정) | `:1969-1972` → `ensure_specular()` `:188` |
| `access_resolved_color/depth` | MSAA-resolved color/depth | `compositor.cpp:131,144` |

`_render_callback(type, RenderData*)`에서 `RenderData.get_render_scene_buffers()` → `RenderSceneBuffersRD.get_texture(scope, name)`로 **RID 직접 획득**(전부 ClassDB 바인딩 `render_scene_buffers_rd.cpp:52-61`), 그리고 GDExtension이 쥔 `RenderingDevice.texture_get_data`/`texture_copy`(바인딩 `rendering_device.cpp:9058,9069`)로 **CPU 리드백** 가능. **[로컬 2건 상호확증]**

부착점: `Camera3D.compositor`(`camera_3d.h:87`) 또는 `WorldEnvironment.compositor`(`world_environment.h:43`)에 `CompositorEffect` 인스턴스 추가. (리플렉션 프로브 패스는 스킵 `renderer_scene_render_rd.cpp:305-307`.)

### 5.D.2 AOV별 분류표 (MRQ 패스 → Godot seam)

| MRQ AOV | Forward+ 버퍼 상태 | 분류 | 근거 |
|---------|---------------------|------|------|
| **Final Image (beauty)** | `RB_TEX_COLOR` 항상 존재 | 🟢 **코어 0** | `render_scene_buffers_rd.cpp:186` |
| **Depth (non-linear)** | `RB_TEX_DEPTH` 항상 존재 | 🟢 **코어 0** | `:192`; `access_resolved_depth`로 MSAA resolve |
| **World/View Depth (linear)** | 선형 버퍼 미보존, 재구성 필요 | 🟢 **코어 0** (익스텐션 리니어라이즈 셰이더) | near/far via `RenderData.get_environment`; 수학 재사용 `copy.glsl:258`, `screen_space_reflection.glsl:44` |
| **Motion Vectors** | velocity, `needs_motion_vectors`로 강제 | 🟢 **코어 0** | `ensure_velocity` `:730`; shader `scene_forward_clustered.glsl:1069` |
| **Normal** | normal-roughness, `needs_normal_roughness`로 강제 | 🟢 **코어 0** (단 packed 인코딩 → `normal_roughness_compatibility()` 디코드 필요, PR #86316 이후 best-fit 포맷) | `ensure_normal_roughness_texture` `:63-72`; 디코드 `scene_forward_clustered_inc.glsl:485` |
| **Specular** | `RB_TEX_SPECULAR`, `needs_separate_specular`로 강제 | 🟢 **코어 0** (단 MRQ "Reflections-Only"와 정확히 일치하진 않음 — 스페큘러 광이지 격리된 반사 아님) | `ensure_specular` `:187-189` |
| **Albedo/BaseColor, Metallic/ORM, Emission** | 미보존 — `MODE_RENDER_MATERIAL` 셰이더가 출력하나 **UV2 라이트맵 베이크 전용** | 🔴 **중간 렌더러** (스크린스페이스 MRT 어태치먼트 신규) | 셰이더 실재 `scene_forward_clustered.glsl:1037-1044`, 단 `_render_material`/`bake_render_uv2` 경유 `render_forward_clustered.cpp:2958` |
| **Object IDs / Cryptomatte** | 오브젝트ID 버퍼 전무(`instance_index`는 룩업용) | 🔴 **중간 렌더러** (신규 MRT + 인스턴스ID 배선 + Cryptomatte 인코딩) | `instance_index_interp` `glsl:135`, 출력 어태치먼트 없음 |
| **Lighting-Only (diffuse)** | 클린 분해 없음(separate-specular의 diffuse는 결합됨) | 🔴 중간 렌더러 (신규 패스/플래그) | `diffuse_buffer` `glsl:1059`, POST_SKY 한정·즉시 병합 |
| **AO / SSS** | — | (MRQ조차 deferred 한계로 **미지원** — 타깃에서 제외 가능) | [DR ✓] Epic 명시 |

**"저렴 vs 비쌈" 판단 근거(딥리서치):** MRQ 자체가 deferred G-buffer에 묶여 AO·SSS를 **출력 못 한다** — 즉 "버퍼가 이미 있으면 저렴, 추가 셰이딩 데이터가 필요하면 비쌈"이 업계 공통 경계. **[DR ✓ high]** Forward+는 deferred가 아니라 albedo/metallic/ID를 매프레임 안 남기므로 이들만 신규 패스 필요.

### 5.D.3 비싼 AOV의 구현 경로 (albedo/objectID)

**중요:** Forward+ opaque 패스는 **이미 MRT**다 — `get_color_pass_fb`가 `[color, specular, velocity, depth]` 멀티어태치먼트로 프레임버퍼 구성(`render_forward_clustered.cpp:179-206`), 파이프라인 포맷도 플래그 구동(`:4516-4542`). 따라서 albedo/objectID AOV 추가 = **from-scratch G-buffer가 아니라 점진적 MRT 어태치먼트 추가**:
1. 키 + `ensure_*`(`render_forward_clustered.cpp`), 2. `get_color_pass_fb`·`_get_color_framebuffer_format_for_pipeline`에 어태치먼트, 3. 셰이더 `layout(location=N) out`, 4. color-pass 플래그. 셰이더는 이미 albedo/orm/emission 출력 능력 보유(`MODE_RENDER_MATERIAL`, UV2 전용을 스크린스페이스로 전용). **[로컬]** 비용: 파이프라인 순열·MSAA-resolve 경로를 건드리는 **중간 작업**(deferred 신규보다 훨씬 저렴).

**참조 아키텍처(딥리서치):**
- **reduz proposal #7916 "Rendering Compositor"** — opaque를 다중 커스텀 패스로 분할, 머티리얼이 `compositor_opaque_pass N;`로 자기할당, **최대 4개 커스텀 버퍼(R8~RGBA32F)**. use case로 "object outlines(오브젝트+뎁스를 커스텀 버퍼에 쓰기)"를 명시 → **RGBA32F 하나면 Cryptomatte float ID 저장 충분.** 이것이 엔진팀이 의도한 정공법(미구현). **[DR ✓ high]** 관련 요청 #798, #10396(`get_buffer_layer(NORMALS_ROUGHNESS/UNSHADED)`)도 존재 — albedo/lighting AOV가 아직 1급이 아님을 확인. **[DR ✓]**
- **Unity HDRP AOV API**(AOVRequest→AOVBuffers→RTHandle→리드백 콜백→디스크)가 **비-deferred 렌더러의 프로덕션 AOV 추출 템플릿**. **Bevy**는 opt-in DepthPrepass/NormalPrepass/MotionVectorPrepass("thin g-buffer")로 forward 프리패스 기법 실증. **[DR ✓ high]**

### 5.D.4 Cryptomatte 오브젝트ID 인코딩 (딥리서치 검증)

Cryptomatte는 **공개 표준**(Psyop 스펙 PDF + SIGGRAPH 2015). 구현 규격:
- **해시:** 오브젝트/머티리얼 이름을 UTF-8 → **MurmurHash3_x86_32**(유일 지원 해시, 0=오브젝트 없음) → 32비트를 float32로 bit-copy(`uint32_to_float32`), 지수 [1:254] 클램프(지수 0/255면 bit23 토글)로 NaN/inf 회피, **little-endian 전용**. **[DR ✓ high]**
- **저장/커버리지:** (ID, coverage) 랭크 쌍을 번호매긴 RGBA EXR 레이어에 2쌍/레이어로 팩(`{typename}00.r=ID rank0, .g=coverage0, .b=ID rank1, .a=coverage1`), 기본 6레벨=3레이어(00/01/02). AA 커버리지 = 랭킹 픽셀 필터(샘플별 ID를 커널 가중, ID별 가중합 정규화, 커버리지순 랭크). **[DR ✓ high]**
- **⚠️ Godot 난점:** Forward+는 오프라인의 per-sample 확률 누적이 아니라 MSAA/TAA를 쓰므로, per-sample AOV 저장 없이 스펙의 랭킹 필터를 재현하는 방법이 미해결(딥리서치 open question). 스타일라이즈드 타깃엔 **단순 정수 ID 버퍼(비-Cryptomatte)**로 충분할 수 있음.

### 5.D.5 멀티레이어 EXR 출력 (로컬 확인)

- 출력 규격 타깃: UE Movie Render Graph의 **"EXR Multilayer"** — 첫 출력은 RGBA, 추가 AOV는 각자 named layer. **[DR ✓]**
- **번들 tinyexr는 멀티파트 API를 이미 포함** — `SaveEXRMultipartImageToFile/Memory`(`thirdparty/tinyexr/tinyexr.h:596-611`). **OpenEXR 벤더링 불필요.** **[로컬 확인]**
- 단 **Godot 래퍼 `Image::save_exr`는 단일파트·최대 4채널로 제한**(`image_saver_tinyexr.cpp:164` `max_channels=4`, `SaveEXRImageToMemory` 사용). → 멀티레이어/Cryptomatte 출력은 **래퍼 우회(멀티파트 API 직접 호출) 국소 작업**. **[로컬 확인]**

### 5.D.6 멀티 AOV 실행 순서 (ROI순)

1. **🟢 코어 0 AOV 팩 (GDExtension):** `CompositorEffect`(normal/motion/specular 플래그) + depth 리니어라이즈 셰이더 + `texture_get_data` 리드백 → **beauty/depth/linear-depth/normal/motion/specular** 5~6종 AOV를 코어 수정 없이. HDRP AOVRequest 패턴 차용. 단 RB 텍스처가 `CAN_COPY_FROM` 비트 없어 **1회 compute/blit 카피 필요**(`render_scene_buffers_rd.cpp:799-805`).
2. **🟢 멀티레이어 EXR 라이터 (국소):** tinyexr 멀티파트 API 직접 호출로 래퍼 우회 → 위 AOV들을 named layer로 묶어 출력.
3. **🔴 albedo/objectID MRT (중간 렌더러, 선택):** `MODE_RENDER_MATERIAL` 셰이더를 스크린스페이스 MRT 어태치먼트로 전용, 또는 reduz #7916 편승. 정수 ID 버퍼로 시작, Cryptomatte 완전 준수는 나중.
4. **판단:** 스타일라이즈드 타깃엔 **1+2로 실용 AOV 세트 충분**(포스트/합성용 depth·normal·motion). albedo/Cryptomatte(3번)는 VFX 하우스급 합성 파이프라인에만 정당화 — 원신급에도 대개 불필요.

---

## 6. §7 대비 정정 요약

| §7 초판 진단 | 실측·딥리서치 결과 |
|--------------|---------------------|
| "Niagara/Sequencer급 저작 도구가 없다" | ✅ **정확** — 모듈 스택·마스터 시퀀서·노드 VFX 저작 부재 확인(로컬 grep 0매치 + [DR ✓]) |
| "런타임 기능은 있어 코드로는 만들 수 있으나" | ✅ **정확하나 과소평가** — 런타임은 코드뿐 아니라 **기능적으로 경쟁력 있는 시뮬 능력**(컴퓨트 파티클·어트랙터·충돌·서브에미터, 9종 애니 트랙, 결정론 `MovieWriter`)이 이미 존재 |
| "GUI 반복 튜닝 워크플로우가 없어 물량 병목" | ✅ **정확** — 단 이는 **에디터/데이터모델 문제**라 §1~5(렌더러)와 달리 **`EditorPlugin`+GDExtension으로 코어 수정 없이 대부분 해소 가능** |
| (미언급) 시네마틱 카메라 | ⚠️ **보강** — 코어 부재이나 **Phantom Camera 애드온이 성숙한 해답** |
| (미언급) 진입장벽 | ⚠️ **핵심 반전** — 이 격차는 **§1~5 중 진입장벽이 가장 낮다**(man-year 렌더러 개조 아님, 저작툴 구축 문제) |

> **전략적 결론:** VFX/시네마틱 저작 UX 격차는 **"불가"도 "man-year 코어"도 아니다 — 대부분 "아직 안 만든 에디터 툴"이다.** 원신급 스타일라이즈드 타깃에선 **① 서드파티 채택(Phantom Camera 등) + ② 자체 데이터주도 VFX 시스템·시퀀서 EditorPlugin 구축(코어 0) + ③ 국소 코어 훅(파티클 시킹/MovieWriter AOV)** 조합이 현실적 경로다. Niagara/Sequencer 완전 패리티(P3)는 스타일라이즈드 물량 축소 전략에선 정당화되지 않는다.

---

## 2026-08-18 개정 — AAA 기준 재채점

> **전제 변경:** 포크 + 업스트림 디스커넥트 + Full-Nanite 커밋. "코어 수정 필요"는 탈락 사유가 아님. 목표는 UE5.4+ / Horizon Forbidden West / Cyberpunk 2077 급 AAA. 스타일라이즈드 축소(원신 하한)는 폐기. 판단 축은 "man-year급인가" 하나뿐.

### 뒤집힌 판정 (Delta Table)

| 항목 | 기존 판정 (2026-08-03) | AAA 재판정 (2026-08-18) | 근거 |
|------|------------------------|--------------------------|------|
| **P3 (Niagara/Sequencer 완전 패리티)** | "대규모, man-year급, 스타일라이즈드 타깃엔 불필요" | **P3→P1 재개봉.** AAA에선 Niagara식 모듈러 VFX+Sequencer식 마스터 시네마틱이 필수. "스타일라이즈드엔 불필요"는 원신 하한 폐기로 무효. 단, 전체가 man-year는 아님 — 모듈 스택 데이터모델은 EditorPlugin(P1), 진짜 노드그래프+GPU 이벤트+데이터 인터페이스만 man-year. | AAA 시네마틱 컷신(다중 액터·카메라·VFX·오디오 동시 제어)은 마스터 시퀀서 없이 감당 불가. |
| **GPU 파티클과 씬 깊이/GI의 상호작용 (신규)** | 미평가 | **🟡 신규. AAA 필수.** 파티클이 씬 깊이 버퍼를 읽어 오클루전/소프트 파티클, GI(프로브/라이트맵)를 수광. Godot GPU 파티클은 씬 깊이·GI 수광 경로 없음. 코어 셰이더 수정 필요(국소~중간). | `particles.glsl`에 깊이/GI 입력 없음. |
| **볼류메트릭 VFX (신규)** | 미평가 | **🟡 신규. AAA 필수.** 볼류메트릭 포그·연기·구름·빛 산란. Godot은 `FogVolume`(로컬 포그)만 있고 진짜 볼류메트릭(프러스텀 볼륨·프로스텀 라이팅)은 없음. 코어 렌더러 작업(중간~대규모). | UE Volumetric Cloud/Fog. Godot: `fog_volume.h`. |
| **유체·연기 시뮬 (신규)** | 미평가 | **🔴 신규. AAA 필수, man-year급.** Niagara Fluid Simulation(화재·연기·액체)은 GPU 유체 솔버. Godot에 유체 sim 인프라 전무. 코어 신규 서브시스템. | UE Niagara Fluids. Godot: 없음. |
| **파티클 라이팅·그림자 수광 (신규)** | 미평가 | **🟡 신규. AAA 필수.** 파티클이 씬 라이트에서 그림자/조명을 수광. Godot GPU 파티클은 라이트·그림자 수광 없음. 셰이더 수정 + 파티클 라이트 프로브 주입 필요(코어, 국소~중간). | `particles.glsl`에 라이팅/섀도 입력 없음. |
| **데칼·블러드/데미지 누적 (신규)** | 미평가 | **🟡 신규. AAA 필수.** 메시 데칼·블러드 스플래터·데미지 마크의 동적 누적. Godot `Decal` 노드는 존재하나, 동적 누적(블러드 풀) 시스템은 없음. GDExtension으로 구축 가능. | Godot: `decal.h`. |
| **시네마틱 모션블러·DOF 품질 (신규)** | 미평가 | **🟡 신규. AAA 필수.** 시네마틱 품질 모션블러(서브프레임 누적, 벨로시티 기반)와 DOF(Circle of Confusion, Bokeh). Godot은 TAA 기반 모션블러와 기본 DOF만 존재. 시네마틱 품질은 별도 패스 필요(코어, 중간). | UE Cinematic DOF/Motion Blur. Godot: `taa_resolve.glsl`, `dof`. |
| **MovieWriter → MRQ** | P2 "국소 코어 훅" | **P2→P1 격상.** AAA에선 EXR·누적AA·멀티 AOV가 시네마틱 렌더링의 기본. 기존 평가는 스타일라이즈드에 맞춰 낮췄음. | UE Movie Render Queue. |
| **마스터 시네마틱 시퀀서** | 🟡 EditorPlugin 불요 | **🟡→🟡 유지하나 필수로 격상.** AAA 시네마틱(복합 액터·카메라·VFX·오디오 동시 제어)은 마스터 시퀀서 없이 제작 물량 감당 불가. EditorPlugin으로 구축 가능하나(코어 0), 구축 물량이 큼. | UE Sequencer. |

### P3 → P1 재개봉 상세 (Niagara/Sequencer 완전 패리티)

**기존 판정의 문제:** "스타일라이즈드엔 불필요"는 원신 하한을 전제로 한 판단. AAA는 다르다.

| Niagara/Sequencer 기능 | AAA에서의 필요성 | Godot 격차 | 난이도 |
|------------------------|------------------|------------|--------|
| **모듈러 에미터/모듈 스택** | 🔴 필수. AAA VFX는 수십 개 모듈 조합으로 복잡한 이펙트를 저작. 단일 `ParticleProcessMaterial` 인스펙터로는 불가. | EditorPlugin 데이터모델 + 커스텀 셰이더 컴파일러 | 🟡 P1 (EditorPlugin, 코어 0) |
| **노드 그래프 VFX 저작** | 🟡 필수. TA가 프로그래머 없이 HLSL 그래프로 모듈 저작. | VisualShader 파티클모드 위에 구축 | 🟡 P1 (EditorPlugin + VisualShader) |
| **GPU 이벤트/데이터 인터페이스** | 🟡 AAA 복잡한 이펙트(폭발 체인, 트레일 생성)에 필요. | 이미터 간 이벤트·데이터 인터페이스 전무 | 🔴 P2 (코어, 중간) |
| **마스터 시퀀서(멀티트랙)** | 🔴 필수. AAA 컷신은 10+ 액터·카메라·VFX·오디오 동시 제어. | 단일 `AnimationPlayer` 도프시트만 | 🟡 P1 (EditorPlugin, 코어 0) |
| **Spawnables/Possessables** | 🟡 컷신에서 임시 액터 생성·레벨 액터 참조. | EditorPlugin에서 구현 가능 | 🟡 P1 |
| **Take Recorder** | 🟡 모캡·라이브 퍼포먼스 녹화. | EditorPlugin + Live Link 개념 | 🟡 P1 |
| **Shot 관리/잡큐** | 🟡 멀티샷 컷신 렌더 관리. | EditorPlugin + MovieWriter 확장 | 🟡 P1~P2 |

**결론:** Niagara/Sequencer 완전 패리티 중 **모듈 스택·노드 그래프·마스터 시퀀서는 P1(EditorPlugin)으로 실현 가능.** GPU 이벤트·데이터 인터페이스만 진짜 man-year급. AAA 기준으로도 P3가 대부분 P1으로 강등 가능.

### AAA VFX/시네마틱 신규 요구사항 요약

| # | 항목 | 난이도 | 분류 | 의존성 |
|---|------|--------|------|--------|
| 1 | GPU 파티클 ↔ 씬 깊이/GI 상호작용 | 🟡 국소~중간 코어 | 셰이더 | `particles.glsl` 수정 |
| 2 | 볼류메트릭 VFX (포그·연기·구름) | 🟡 중간 코어 | 렌더러 | 신규 볼륨 패스 |
| 3 | 유체·연기 시뮬 (Fire/Smoke) | 🔴 man-year, 코어 | 신규 서브시스템 | GPU 유체 솔버 |
| 4 | 파티클 라이팅·그림자 수광 | 🟡 국소~중간 코어 | 셰이더 | `particles.glsl` + 라이트 프로브 주입 |
| 5 | 데칼·블러드/데미지 누적 | 🟢 GDExtension | Tooling | 기존 `Decal` 노드 활용 |
| 6 | 시네마틱 모션블러·DOF | 🟡 중간 코어 | 렌더러 | 서브프레임 누적, 벨로시티 패스 |
| 7 | MovieWriter → MRQ (EXR·누적AA·멀티 AOV) | 🟡 P1 총합 | 코어 + GDExtension | §5.D 기존 분석 |
| 8 | 마스터 시네마틱 시퀀서 | 🟡 EditorPlugin | Tooling | 단일 `AnimationPlayer` |
| 9 | 모듈러 VFX 스택 | 🟡 EditorPlugin | Tooling | `ParticleProcessMaterial` |

### 인라인 수정 내역

- **§0 한 장 요약:** "원신/AAA 방식" → "AAA 방식"으로 대상 변경. P3 "대규모, 원신 프로덕션 완전 패리티, man-year급, 대개 불필요" → "P1 재개봉. 모듈 스택·시퀀서는 EditorPlugin. GPU 이벤트·데이터 인터페이스만 man-year."로 수정.
- **§4.2 P3 (L150-151):** "대규모 (원신 프로덕션 완전 패리티, man-year급, 대개 불필요) — Niagara급 완전 노드그래프 VFX(데이터 인터페이스·GPU 이벤트·시뮬-렌더 완전분리)나 Sequencer급 스폰어블/테이크레코더/샷 관리 풀스택은 대규모다. **스타일라이즈드·물량 축소 타깃엔 정당화 안 됨.**" → "대규모 (GPU 이벤트/데이터 인터페이스, man-year급) — Niagara급 GPU 이벤트·데이터 인터페이스·시뮬-렌더 완전분리, 유체·연기 시뮬만 man-year. 모듈 스택·노드 그래프·마스터 시퀀서는 P1으로 재개봉. **AAA 타깃에선 P1으로 대부분 커버, 잔여 man-year만 P3.**"로 수정.
- **§6 (L286-296):** 전략적 결론을 AAA 기준으로 재작성. "원신급 스타일라이즈드 타깃" → "AAA 타깃". "Niagara/Sequencer 완전 패리티(P3)는... 정당화되지 않는다" → "P3의 대부분은 P1으로 강등 가능, GPU 이벤트·유체 시뮬만 P3."으로 수정.
