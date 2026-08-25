# Godot 오픈월드 — 오디오 전파 / 공간 오디오 딥리서치 (3D 감쇠·오클루전·HRTF·컨볼루션·미들웨어·앰비언스)

> [godot-openworld-engine-gaps.md](./godot-openworld-engine-gaps.md)의 7축, [godot-runtime-gaps-misc-research.md](./godot-runtime-gaps-misc-research.md)의 8축, [godot-physics-simulation-research.md](./godot-physics-simulation-research.md), [godot-weather-atmosphere-research.md](./godot-weather-atmosphere-research.md) **어디에도 없던 축**을 실측한 문서.
> 대상: Godot **4.8-dev** 소스트리 (commit `eda2a482e9`). 모든 file:line은 로컬 트리 직접 실측이며, "없다"는 전부 검색어+결과로 뒷받침한다.
>
> **목표 수준 정정 (2026-08-18):** 초안은 "원신급 스타일라이즈드"를 전제했으나 **목표는 풀 AAA**로 정정됐다. 따라서 이 문서는 **아트 디렉션을 근거로 항목을 할인하지 않는다.** 비교 대상은 Wwise/FMOD 기반 AAA 프로덕션, Steam Audio의 실시간 회절·전파, The Last of Us / Hitman / Cyberpunk 급 공간 오디오다. 이 프로젝트는 **upstream 분리 포크 + 딥 코어 개조 허용**이므로 "코어 수정 필요"는 탈락 사유가 아니다 — **man-year급인지 아닌지**만 본다.
>
> **§5는 [godot-console-export-research.md](./godot-console-export-research.md) §4와 직접 맞물린다.** 미들웨어 채택은 콘솔 포팅(W4 Games 경로)의 전제를 바꾸므로 별도 절로 논증한다.

---

## 0. 요약 표

| # | 격차 | 판정 | 한 줄 결론 |
|---|------|------|-----------|
| 1 | 3D 오디오 기본기 | 🟡 | **"전무"는 과장.** SPCAP 패닝(스테레오~7.1)·감쇠 4모델·도플러·방사각·거리연동 하이셸프·**Area3D 존 리버브 센드**가 전부 실재. 단 전부 *거리 함수*이고 **지오메트리를 전혀 모른다** |
| 2 | 오클루전 / 옵스트럭션 / 회절 | 🔴 | 코어 0매치 확정. **자작 레이캐스트는 AAA 기준 미달** — 회절·전파가 없으면 벽 뒤 소리가 "죽거나 안 죽거나" 이진값이 된다. 핵심 병목은 레이가 아니라 **per-source 필터 API가 스크립트에 미노출**(`set_playback_highshelf_params` 언바인딩) |
| 3 | HRTF / 바이노럴 | 🔴 → 🟡 | 코어 0매치. **그러나 `AudioStreamPlayback::_mix`가 GDVIRTUAL이라 per-source 바이노럴을 GDExtension으로 코어 0에 구현 가능.** 업스트림도 정확히 이 결론으로 proposal #3182를 self-close. 출력은 스테레오/3.1/5.1/7.1만 — **앰비소닉·오브젝트 오디오 없음** |
| 4 | 컨볼루션 리버브 / 커스텀 DSP | 🟢 **핵심 반전** | `AudioEffect`/`AudioEffectInstance`가 **`GDVIRTUAL` 완전 바인딩**(`audio_effect.h:42,55`) + `AudioFrame` 네이티브 struct 등록(`register_server_types.cpp:175`) → **커스텀 DSP는 GDExtension으로 100% 가능, 코어 수정 0.** 파티션 FFT 컨볼루션 자작 4~8주 |
| 5 | 미들웨어 (Wwise/FMOD) vs Steam Audio | 🔴 **결정 지점** | Godot 통합은 셋 다 성숙하게 실재. **그러나 셋 다 콘솔을 지원하지 않는다** — §4 콘솔 경로(W4)와 정면 충돌. AAA 지향이면 이게 최대 리스크 |
| 6 | 오픈월드 앰비언스 / 스트리밍 / 폴리포니 | 🔴 | **가장 과소평가된 격차.** 오디오 에셋이 **전부 RAM 상주**(디스크 스트리밍 0), 믹스가 **단일 스레드**, **보이스 우선순위·컬링·버추얼라이제이션 전무** → AAA의 수백 음원에서 구조적으로 터진다 |

**전략 요약:** 이 문서의 6축은 앞선 문서들과 성격이 다르다. 저기선 "엔진 격차는 사실상 0"이 결론이었지만, **여기선 §2·§5·§6이 진짜 격차이고 그중 §6이 가장 아프다.** 그리고 §5(미들웨어)와 §4 콘솔의 상호작용은 이 프로젝트 전체에서 **가장 늦게 발견하면 가장 비싼 종류의 제약**이다.

---

## 1. 3D 오디오 기본기 🟡 — **"전무"는 과장. 있는 것을 정확히 적는다**

### (a) 무엇이 없나 (정확히)

- **지오메트리 인지가 0.** 아래 (b)의 모든 기능이 **리스너-음원 거리와 방향의 순수 함수**다. 벽·바닥·재질·방 크기를 입력으로 받는 경로가 단 하나도 없다.
- 스피커 방향이 **하드코딩**: `speaker_directions[7]` (`scene/3d/audio_stream_player_3d.cpp:102-110`), 소스 코멘트가 직접 `//TODO: hardcoded main speaker directions ... these are simplified and could also be made configurable`라고 적음(`:101`). 커스텀 스피커 레이아웃 불가.
- **고도(elevation) 큐 없음.** 스테레오 경로는 `_calc_output_vol_stereo`가 `flatrad = sqrt(x² + z²)`로 **y를 명시적으로 버린다**(`:157`). 서라운드 경로의 `speaker_directions`도 전부 `y=0`. ⇒ **머리 위/아래 음원이 수평 음원과 구별되지 않는다.** AAA 체크리스트에서 이건 결손이다.
- `Spcap` 인스턴스를 **매 패닝 갱신마다 재생성**한다 — `Spcap spcap(speaker_count, speaker_directions);` (`:129`), 바로 위에 `//TODO: should only be created/recreated once the speaker mode / speaker positions changes`.

### (b) 이미 있는 토대 (실측) — 여기가 반전이다

**`AudioStreamPlayer3D`가 실제로 하는 것:**

| 기능 | 값/종류 | 근거 |
|------|---------|------|
| 감쇠 모델 | **4종** `INVERSE_DISTANCE` / `INVERSE_SQUARE_DISTANCE` / `LOGARITHMIC` / `DISABLED` | `audio_stream_player_3d.h:51-56`, 구현 `_get_attenuation_db` `.cpp:235-263` |
| `unit_size` / `max_db` | 10.0 / 3.0 dB | `.h:79-80` |
| `max_distance` | 0(무제한) 기본, 초과 시 리스너별 스킵 | `.h:116`, `.cpp:460-471` |
| 거리 페이드 | `multiplier *= MAX(0, 1 - dist/max_distance)` | `.cpp:475-477` |
| **패닝** | **SPCAP**(Speaker-Placement Correction Amplitude Panning), 스테레오는 전용 코사인 패닝 법칙 | `.cpp:112-151`, `:156-163` (근거로 issue #103989 인용) |
| `panning_strength` | 노드별 × 전역(`audio/general/3d_panning_strength`, 기본 0.5) | `.h:125-126`, `core/config/project_settings.cpp:1771` |
| **도플러** | `DISABLED`/`IDLE_STEP`/`PHYSICS_STEP`, 음속 343 m/s, 피치 배율 1/8~8 클램프, 리스너 속도까지 상대 계산 | `.h:58-62`, `.cpp:522-548` (`speed_of_sound` `:535`, 클램프 `:540`) |
| **방사각(원뿔 지향성)** | `emission_angle` 45°, 각 밖이면 `emission_angle_filter_attenuation_db`(-12 dB) | `.h:108-110`, `.cpp:481-488` |
| **거리 연동 하이셸프 필터** | `attenuation_filter_cutoff_hz` 5000, `attenuation_filter_db` -24 → 멀수록 고역 감쇠 (= 공기 흡수 근사) | `.h:111-112`, `.cpp:479`, 실제 필터 `audio_server.cpp:457-494` (`AudioFilterSW::HIGHSHELF`, Q=1, 1스테이지) |
| 다중 리스너 | 뷰포트별 `AudioListener3D` 또는 `Camera3D`, **전 리스너 순회 후 채널별 max 합성** | `.cpp:405-406, 427-445`, `_apply_max_volume_from_vector` `:364-373` |
| 폴리포니 | `max_polyphony` 노드별 | `.h:174-175` |

**`Area3D` 존 리버브 — 원시적이지만 진짜로 작동한다 (과소평가 금지):**

| 요소 | 근거 |
|------|------|
| `audio_bus_override` + `audio_bus` (존 진입 시 드라이 신호를 다른 버스로) | `scene/3d/physics/area_3d.h:135-136`, 세터 `.cpp:593-612` |
| `reverb_bus_enabled` + `reverb_bus`(센드 대상) | `.h:138-139`, `.cpp:614-633` |
| `reverb_bus_amount` (0~1 센드량) | `.h:140`, `.cpp:635-641` |
| **`reverb_bus_uniformity`** (0~1: 방향성 리버브 ↔ 확산 리버브 보간) | `.h:141`, `.cpp:643-649` |
| 존 판정 | `PhysicsDirectSpaceState3D::intersect_point`, 최대 32개 오버랩(`MAX_INTERSECT_AREAS`), **첫 매치 승** | `audio_stream_player_3d.cpp:310-350` (`.h:67`) |
| 확산 리버브의 공간 정위 | `space_state->get_closest_point_to_object_volume(area->get_rid(), listener_pos)` → **리버브를 "가장 가까운 방 표면"에 정위** | `.cpp:455-456` |
| 확산/직접 보간 | `reverb_vol[i] = direct_path_vol[i].lerp(reverb_vol[i] * attenuation, uniformity)` | `.cpp:223` |

⇒ **이건 장난감이 아니다.** "존 진입 시 리버브 버스로 센드 + 리버브의 방향성을 방 표면 쪽으로 정위 + uniformity로 확산도 조절"은 **Wwise `AkEnvironment`/aux send의 축소판에 해당하는 정상적인 존 리버브 프리미티브**다. AAA에 미달하는 지점은 "존 리버브가 없다"가 아니라 **"존이 수동 저작 볼륨이고, 방의 음향 특성을 지오메트리에서 유도하지 못한다"**이다.

### ⚠️ 실측된 함정 4가지 (문서에 없거나 문서와 다른 것)

1. **`area_mask` 기본값은 0이다** (`.h:104`, `doc/classes/AudioStreamPlayer3D.xml` `default="0"`). ⇒ **Area3D 리버브는 기본적으로 꺼져 있고 음원마다 opt-in해야 한다.** 켜는 순간 그 음원은 **물리 틱마다 `intersect_point` 쿼리 1회**를 낸다.
2. **존 판정 기준점이 리스너가 아니라 음원이다.** `_get_overriding_area()`는 `get_global_transform().origin`(=플레이어 노드=음원)으로 쿼리한다(`.cpp:319`). ⇒ 리스너가 동굴 안, 음원이 바깥이면 **동굴 리버브가 안 걸린다.** AAA에서 기대하는 "리스너가 있는 방의 리버브"와 반대 의미론이다.
3. **리버브 전용 Area는 드라이 신호를 떨어뜨린다.** `.cpp:562-576`에서 `area`가 non-null이면 `else` 분기(`bus_volumes[internal->bus] = ...`)를 타지 않는다. `audio_bus_override=false` + `reverb_bus_enabled=true`인 Area 안에서는 **버스 볼륨 맵에 리버브 버스만 남고 드라이가 사라진다.** ⇒ 실무상 **두 옵션을 항상 함께 켜야** 한다(암묵적 계약, 문서 미기재).
4. **`attenuation_filter`는 거리에 강결합돼 있어 오클루전에 재활용할 수 없다.** `db_att = (1 - MIN(1, multiplier)) * attenuation_filter_db` (`.cpp:479`). 근거리에선 `multiplier ≥ 1` → `db_att = 0` → **가까운데 벽 뒤인 음원에는 필터를 걸 수단이 이 경로엔 없다.** §2의 핵심 제약이 여기서 나온다.
   - 덤: `linear_attenuation`은 최소 `db_to_linear(0) = 1.0`이라 **0이 되지 않는다** → `_mix_step_for_channel`의 `if (p_highshelf_gain != 0)`가 항상 참 → **가청 3D 음원 하나마다 채널쌍당 바이쿼드 2개가 상시 돈다**(플랫 응답이라 소리는 안 변하는데 CPU는 쓴다). 512프레임 × 음원 수 × 채널쌍 수.

### (c) 경계

| 목표 | 코어 수정 | 방법 |
|------|-----------|------|
| 감쇠 커브 커스터마이즈 | **0** | `attenuation_model=DISABLED` + `volume_db`를 스크립트에서 커브로 직접 구동 |
| 고도 큐 / 커스텀 스피커 배치 | 국소 코어 (~50줄) | `speaker_directions` 상수화 해제 + `Spcap` 캐싱 (`.cpp:101,129`의 TODO 2개를 그대로 처리) |
| 존 리버브를 "리스너 기준"으로 | 국소 코어 (~10줄) | `_get_overriding_area()`의 쿼리 위치를 리스너로 바꾸는 옵션 플래그 |
| 리버브 전용 Area의 드라이 유실 | 국소 코어 (~5줄) | `.cpp:562-576`에 드라이 폴백 추가 |
| 상시 도는 플랫 하이셸프 제거 | 국소 코어 (~3줄) | `p_highshelf_gain != 0` → `!= 1.0` |

### (d) 공수
- 위 국소 코어 5건 합쳐 **수일**. 전부 10~50줄급이고 이 포크에선 비용이 사실상 0이다.

### (e) 현실적 대안
- 없다 — 이건 "대안" 절이 필요 없는, **이미 있고 고치면 되는** 영역이다.

### (f) 판정 🟡 — **격차가 아니라 "손봐야 할 토대". 단 그 토대는 §2가 요구하는 것을 못 준다.**

---

## 2. 오클루전 / 옵스트럭션 / 회절 🔴 — **AAA 기준 자작 미달**

### (a) 무엇이 없나 — 검색으로 확정

`servers/audio/`, `scene/audio/`, `scene/3d/audio_stream_player_3d.cpp`, `scene/resources/audio/`, `modules/interactive_music/` 전체에 대해:

| 검색어 | 매치 |
|--------|------|
| `occlusion` | **0** |
| `obstruction` | **0** |
| `diffraction` | **0** |
| `sound_propagation` | **0** |
| `portal` | **0** |
| `early_reflection` | **0** |
| `impulse_response` | **0** |
| `audio_zone` / `reverb_zone` | **0** / **0** |

(`servers/audio/` 전체에 `occlusion|hrtf|convolution` → **0 매치**, 사용자 브리핑 재확인 완료.)

⇒ 빌트인 오클루전·옵스트럭션·회절·전파·조기반사·IR 전부 **부재 확정**.

### (b) 이미 있는 토대 (실측)

| 요소 | 상태 | 근거 |
|------|------|------|
| per-source 하이셸프 필터가 **믹서에 이미 배선돼 있음** | ✅ **그러나 스크립트 미노출** | `AudioServer::set_playback_highshelf_params` `audio_server.cpp:1210-1220`. **`ClassDB::bind_method` 57개 전수 확인 결과 이 메서드는 바인딩되지 않음** |
| 대신 노출된 우회 손잡이 | ⚠️ 거리 결합 | `attenuation_filter_cutoff_hz` / `attenuation_filter_db` (바인딩 `.cpp:911-915`) — §1-(4)의 제약을 그대로 상속 |
| 물리 레이캐스트 | ✅ | `PhysicsDirectSpaceState3D::intersect_ray`, Jolt 백엔드 |
| **Embree 4.4.0이 이미 번들** | ✅ **미노출** | `thirdparty/embree/include/embree4/rtcore_config.h:13` (`RTC_VERSION 40400`). 래퍼 `core/math/static_raycaster.h:35-97` (`intersect(Ray&)`, `intersect(Vector<Ray>&)`, `add_mesh`, `commit`, `set_mesh_filter`) |
| Embree 플랫폼 커버리지 | ✅ x86_64 / **arm64** / wasm32 (+x86_32 Windows) | `modules/raycast/config.py` |
| 오디오 버스 상한 | **255** (`ERR_FAIL_INDEX(p_count, 256)`) | `audio_server.cpp:575` |
| 재생 1개당 동시 버스 | **6** | `audio_server_constants.h` `MAX_BUSES_PER_PLAYBACK = 6` |
| 버스 이펙트 런타임 조작 | ✅ 바인딩됨 | `add_bus_effect` `:1845`, `get_bus_effect_instance` `:1850`, `set_bus_effect_enabled` `:1853` |

### ⚠️ 실측된 진짜 병목 — **레이캐스트가 아니라 필터 라우팅이다**

브리핑은 "자작 경로 = 레이캐스트 → 로우패스+감쇠 자동화"를 상정했다. 실측 결과 **레이캐스트는 쉬운 쪽이고, "로우패스"가 어려운 쪽**이다.

**per-source 로우패스를 스크립트에서 거는 방법이 사실상 없다:**
1. `set_playback_highshelf_params` — **미바인딩.** GDExtension에서도 못 부른다.
2. `attenuation_filter_db` — 바인딩돼 있으나 **거리 스칼라와 곱해진다**(`.cpp:479`). 가까운 음원에 필터를 못 건다. 오클루전은 정의상 "가까운데 안 들려야 하는" 케이스가 핵심이다.
3. **음원마다 전용 버스 + `AudioEffectLowPassFilter`** — 동작한다. 상한 **255 버스**. AAA 씬의 동시 오클루전 대상이 수십~수백이면 버스 풀 관리가 필요하고, 버스 전환 시 §1-(3)의 드라이 유실 문제와 얽힌다. **가장 현실적인 코어-0 경로이지만 우아하지 않고 상한이 낮다.**
4. **커스텀 `AudioStream` 래퍼**(§4에서 확립) — `_mix`에서 직접 필터링. **코어 0, 상한 없음, 가장 강력.** 대신 `AudioStreamPlayer3D`의 패닝을 쓸 수 없어 3D 처리를 통째로 자작해야 한다.

### 자작 레이캐스트 오클루전의 비용 (실측 기반 산정)

**경로 A — `PhysicsDirectSpaceState3D` (브리핑이 상정한 경로):**
- **하드 제약:** `intersect_ray must not be called while the physics space is being stepped.` (`modules/jolt_physics/spaces/jolt_physics_direct_space_state_3d.cpp:452`) ⇒ 쿼리는 **물리 틱 안**에서만. 기본 60 Hz(`main/main.cpp:2179`).
- 비용 = 음원 수 N × 음원당 레이 수 R × 60 Hz. 단일 오클루전 판정에 R=1은 이진값이 되어 팝핑이 심하므로 실무상 **R=5~9**(음원 주위 오프셋 샘플)이 필요.
- N=100, R=7 → **42,000 레이/초**. Jolt 레이캐스트는 이 규모를 감당하지만 **물리 스레드 예산을 그만큼 잠식**한다. 교차참조: [physics 문서 §7](./godot-physics-simulation-research.md#7-멀티스레딩--성능--대규모-오픈월드--p0) — Jolt는 `WorkerThreadPool`로 병렬화돼 있고 `BODY_STATIC_BIG` 브로드페이즈 레이어로 지형 쿼리 비용을 낮춰둔 상태라 여력은 있다.
- **결정적 한계:** 물리 콜라이더는 **음향 형상이 아니다.** 오픈월드 지형/건물의 콜리전은 보통 단순화돼 있어 창문·문틈·얇은 벽이 콜리전상 존재하지 않거나 반대로 과하다. 그리고 **재질별 투과(transmission) 정보가 없다** — `PhysicsMaterial`엔 마찰·반발만 있다.

**경로 B — 번들 Embree 재사용 (권장):**
- `StaticRaycaster`는 `GDCLASS`이지만 **`_bind_methods`도 `GDREGISTER_CLASS`도 없다**(`core/math/static_raycaster.cpp` 전문 확인, `register_types.cpp:46`은 팩토리만 설치). ⇒ **바인딩 ~50줄의 국소 코어 패치**로 노출 가능.
- 얻는 것: (1) **물리 틱에서 분리** → 워커 스레드에서 임의 레이트로 음향 전용 레이 트레이싱, (2) **음향 전용 지오메트리**(별도 저폴리 메시 + 재질 ID)를 물리 콜라이더와 독립적으로 저작, (3) `intersect(Vector<Ray>&)` 배치 API, (4) x86_64/arm64/wasm 커버.
- 이것이 **Steam Audio가 내부적으로 하는 것과 같은 구조**다(Steam Audio도 Embree를 레이 트레이서 백엔드로 쓴다).

### 🔴 그럼에도 AAA 기준 자작이 미달인 이유 — 정면 판단

**레이캐스트 오클루전은 "가려짐/안 가려짐"만 준다.** AAA 공간 오디오가 요구하는 것은 그 위 세 층이다:

| 층 | 요구 | 자작 레이캐스트로 되나 |
|----|------|------------------------|
| **오클루전** (직접경로 차단 → 감쇠+LPF) | 필수 | ✅ 된다. 이게 전부다 |
| **회절(diffraction)** — 모서리를 돌아오는 저역, 방향이 **모서리 쪽**으로 이동 | AAA 필수. 문 밖 발소리가 "문 쪽에서" 들려야 함 | 🔴 **안 된다.** UTD/BTM 회절은 에지 추출 + 에지 그래프가 필요하며 레이캐스트의 파생이 아니다 |
| **전파/패스파인딩(propagation)** — 복도를 우회한 경로, 유효 방향 재계산 | AAA 필수. 벽 뒤 소리가 열린 문 방향에서 들려야 함 | 🔴 **안 된다.** 프로브 그래프 + 베이크 또는 실시간 경로 탐색 필요 |
| **조기반사 + 실시간 리버브 추정** — 방 형상에서 IR/파라메트릭 리버브 유도 | AAA 필수 | 🔴 **안 된다.** 레이 트레이싱 기반 에너지 히스토그램 → IR 합성이 필요 |

⇒ **회절·전파·리버브 추정을 자작하면 그게 곧 Steam Audio를 다시 만드는 것이다.** Steam Audio는 Valve가 CS2/Half-Life: Alyx에 쓰는 물건이고 **Apache-2.0 오픈소스**다. 여기서 자작을 선택하는 것은 **man-year급 재발명**이며, 이 프로젝트가 딥 코어 개조를 허용한다 해도 **§1 Nanite / §2 GI에 배정해야 할 예산을 오디오 R&D에 태우는 오배분**이다.

### (c) 경계

| 목표 | 코어 수정 | 판정 |
|------|-----------|------|
| 레이캐스트 이진 오클루전 (감쇠만) | **0** (GDScript) | ✅ 며칠. **AAA엔 부족** |
| + per-source LPF (버스 풀 방식, ≤255) | **0** | ✅ 1~2주. 상한 있음 |
| + per-source LPF (커스텀 `AudioStream` 방식) | **0** | ✅ §4와 동일 기술. 상한 없음 |
| Embree를 스크립트에 노출 | 국소 코어 ~50줄 | ✅ **권장. ROI 최고의 코어 패치** |
| `set_playback_highshelf_params` 바인딩 | 국소 코어 ~5줄 | ✅ 즉시 |
| **회절 + 전파 + 실시간 리버브 추정 자작** | 대규모 (코어 무관, 순수 R&D) | ~~🔴 **man-year급. 비권장**~~ → 🔴 **man-year급. 스코프 내 (2026-08-25)** — 단 **자작이 1순위는 아니다.** §5 미들웨어(Steam Audio) 결정이 선행하며, 미들웨어가 콘솔 제약으로 탈락할 때 이 경로가 유일한 대안이 된다. 순서: 미들웨어 결정 → 탈락 시 자작 착수 |
| **Steam Audio 통합 채택** | **0 (GDExtension)** | ✅ **AAA 기준 정답** — §5 참조 |

### (d) 공수
- 이진 오클루전 MVP: **3~5일**
- 버스 풀 LPF + 히스테리시스/스무딩 + 재질별 투과 테이블: **2~3주**
- Embree 노출 + 음향 전용 지오메트리 파이프라인: **2~4주**
- 회절/전파/리버브 추정 자작: **9~18개월 (man-year급)** — Steam Audio·Wwise Spatial Audio가 각각 수년의 산물이다

### (e) 현실적 대안 — **충실도 비용 명시**

| 대안 | 충실도 비용 |
|------|-------------|
| Area3D 존 리버브 + 수동 포탈 볼륨 저작 | 회절·전파 없음. **레벨 디자이너가 방마다 손으로 저작** → AAA 규모 오픈월드에서 저작 비용이 폭발. 동적 지오메트리(부서지는 벽·열리는 문) 대응 불가 |
| 레이캐스트 이진 오클루전 + LPF | 소리가 "죽거나 산다"의 이진. **방향이 안 바뀐다** → 문 밖 적의 위치를 소리로 못 찾음. 경쟁 FPS·스텔스에선 게임플레이 결함 |
| 위 둘 + 수동 포탈 노드(문/창에 `AudioPortal` 자작) | 전파 방향은 근사 가능. 회절 저역·에너지 보존은 여전히 근사. **저작 비용 여전히 높음** |
| **Steam Audio** | 회절·전파·반사 전부 실시간+베이크. 충실도 비용 **거의 0**. 대신 §5의 콘솔 리스크 |

### (f) 판정 🔴 — **진짜 격차. 그리고 이 문서에서 자작을 권하지 않는 유일한 축.**

---

## 3. HRTF / 바이노럴 🔴 → 🟡 — **부재는 맞지만 경로가 열려 있다**

### (a) 무엇이 없나
- **HRTF·바이노럴·앰비소닉 전부 0매치** (§2의 검색 표 참조: `hrtf` 0, `binaural` 0, `ambisonic` 0, `spatializ` 0).
- **출력 채널 능력 실측 — 4종뿐:** `SPEAKER_MODE_STEREO` / `SPEAKER_SURROUND_31` / `SPEAKER_SURROUND_51` / `SPEAKER_SURROUND_71` (`servers/audio/audio_server_enums.h:37-40`). 내부적으로 버스당 최대 4채널쌍 = **8채널 상한**(`audio_server_constants.h` `MAX_CHANNELS_PER_BUS = 4`).
  ⇒ **앰비소닉 버스도, 오브젝트 기반 오디오(Atmos/DTS:X)도, 7.1.4 같은 하이트 채널도 없다.**
- 헤드폰 감지·헤드폰 전용 출력 모드 없음.

### (b) 이미 있는 토대 (실측) — **여기가 결정적이다**

**`AudioStreamPlayback::_mix`가 GDVIRTUAL로 노출돼 있다:**
```
scene/resources/audio/audio_stream.h:89
  GDVIRTUAL3R_REQUIRED(int, _mix, GDExtensionPtr<AudioFrame>, float, int)
:146
  GDVIRTUAL2R_REQUIRED(int, _mix_resampled, GDExtensionPtr<AudioFrame>, int)
:172
  GDVIRTUAL0RC_REQUIRED(Ref<AudioStreamPlayback>, _instantiate_playback)
```
그리고 `AudioFrame`은 **GDExtension 네이티브 struct로 정식 등록**돼 있다:
```
servers/register_server_types.cpp:175
  GDREGISTER_NATIVE_STRUCT(AudioFrame, "float left;float right");
```

⇒ **GDExtension이 `AudioStream`을 상속해 "오디오 스레드에서 임의 DSP를 돌려 스테레오를 직접 뱉는" 음원을 만들 수 있다.** 그 안에서 HRTF 컨볼루션을 돌리고 결과를 그냥 `AudioStreamPlayer`(비-3D)로 재생하면, **Godot의 SPCAP 패닝을 완전히 우회한 per-source 바이노럴 렌더링**이 코어 수정 0으로 성립한다.

### 🔎 업스트림이 정확히 이 결론에 도달했다 (실조사)

proposal **[#3182 "Overhaul audio spatialization to allow for greater flexibility (occlusion, HRTF, …)"](https://github.com/godotengine/godot-proposals/issues/3182)** — 2021-08-22 개설, **같은 날 작성자(@ellenhp)가 self-close**. 종료 코멘트 원문:

> "I do think that this won't be necessary once audio stream playback objects can be sent to the audio server bypassing all the audio player nodes. That combined with the new native extension system will allow **custom audio player nodes to be created that can do whatever the user requires**."

그리고 @Calinou의 코멘트:
> "HRTF audio is also controversial among gamers… **It's the motion blur of audio**, if you prefer 🙂"

⇒ **[misc 문서 §1 모션 블러](./godot-runtime-gaps-misc-research.md#1-모션-블러--정정-부재는-맞지만-격차는-아니다)와 동형 구조다** — 코어가 막고 있는 게 아니라, 코어가 의도적으로 확장 레이어에 남겼다. 다만 **AAA 기준에서는 결론이 반대로 간다**: 모션 블러는 실제로 안 써도 되지만, **HRTF는 AAA 체크리스트 항목이다**(TLOU2·Hitman·Cyberpunk 전부 헤드폰 바이노럴 옵션 제공). "논쟁적이다"는 것은 **끄는 옵션을 주라는 뜻이지 안 만들어도 된다는 뜻이 아니다.**

### 관련 업스트림 (실조사)

| 항목 | 번호 | 상태 |
|------|------|------|
| Overhaul audio spatialization (occlusion, HRTF…) | [#3182](https://github.com/godotengine/godot-proposals/issues/3182) | ❌ Closed (2021-08-22, 작성자 self-close, "GDExtension이 답") |
| SoundProbe based reverb/attenuation baking + HRTF for XR | [#949](https://github.com/godotengine/godot-proposals/issues/949) | ❌ Closed (2020-05-29) |
| Implement support for **ambisonic audio** | [#2748](https://github.com/godotengine/godot-proposals/issues/2748) | **Open** (2021-05-20, 👍5) |
| Create a Resource type for **audio spatialization models** | [#4377](https://github.com/godotengine/godot-proposals/issues/4377) | **Open** (2022-04-10, 👍16) |
| Implement **audio bus overhaul** in support of improved spatialization ("Spatial bus": 믹스 **전에** 이펙트 적용) | [#4435](https://github.com/godotengine/godot-proposals/issues/4435) | **Open** (2022-04-23, 👍14) |

**#4377/#4435가 이 문서의 §1~§3을 코어에서 정공법으로 푸는 설계다.** #4377 본문이 제약을 정확히 적어뒀다: *"We must retain surround sound support (at least 3.1 and 5.1) which means we must retain SPCAP"*, *"We can't allow callbacks into script from the audio thread"*. ⇒ **오디오 스레드에서 스크립트 콜백은 영구히 안 된다. GDExtension(네이티브)만 가능하다** — `AudioEffectInstance.xml`이 명시하는 바와 정확히 일치.

### (c) 경계

| 목표 | 코어 수정 | 방법 |
|------|-----------|------|
| per-source 바이노럴 (커스텀 `AudioStream`) | **0** | GDExtension. HRIR 데이터셋(MIT KEMAR / SADIE II, 둘 다 연구 라이선스 무료) + 파티션 컨볼루션 |
| 바이노럴을 **버스 이펙트**로 | **0이지만 구조적으로 부적합** | `AudioEffectInstance`는 **채널쌍마다 독립 인스턴스**로 호출된다(`audio_server.cpp:356-361`) → 크로스채널 상태를 못 가짐. **버스 이펙트는 per-source 방향 정보도 못 본다** |
| 앰비소닉 버스 (B-format → 바이노럴 디코드) | 국소~중간 코어 | `MAX_CHANNELS_PER_BUS=4`(8ch)로 1차(4ch)·2차(9ch 불가) 앰비소닉. **2차 이상은 상수 확장 필요** |
| 오브젝트 기반(Atmos/DTS:X) | 🔴 대규모 + 라이선스 | Dolby SDK는 NDA. §5·§4와 동형 구조 |
| 헤드폰/스피커 자동 전환 | 국소 코어 | 플랫폼 API 노출 필요 |

### (d) 공수
- 커스텀 `AudioStream` 바이노럴 렌더러 (HRIR 로딩 + 방향 보간 + 파티션 컨볼루션 + 근거리 보정): **6~10주** (§4의 컨볼루션 엔진을 공유하면 **4~6주**)
- 기성 채택(Steam Audio): **0** — Steam Audio가 HRTF를 내장한다

### (e) 현실적 대안 — 충실도 비용
| 대안 | 충실도 비용 |
|------|-------------|
| SPCAP 스테레오 + 헤드폰용 크로스피드/스테레오 확장 | **고도 큐 없음**(§1-(a)). 앞/뒤 혼동. AAA 체크리스트 미충족 |
| 5.1 렌더 후 정적 바이노럴 다운믹스(가상 스피커 7개) | 중간 충실도. 음원별 HRTF보다 정위 정밀도 낮으나 **구현이 훨씬 싸다**(버스 이펙트 1개, 채널쌍 문제는 마스터 다운믹스 단계에서 우회) |
| **Steam Audio** | 충실도 비용 0. §5 리스크 |

### (f) 판정 🟡 — **자작 가능하고 코어 0. 단 §5를 채택하면 자동으로 해결된다.**

---

## 4. 컨볼루션 리버브 / 커스텀 DSP 🟢 — **이 문서의 핵심 반전**

### (a) 무엇이 없나
- **컨볼루션 리버브 부재 확정** (`convolution` 0매치, `impulse_response` 0매치).
- 빌트인 리버브는 **Freeverb / Schroeder-Moorer 알고리즘 리버브**다 — `MAX_COMBS = 8`, `MAX_ALLPASS = 4` (`servers/audio/effects/reverb_filter.h:42-43`), 파라미터는 predelay/predelay_fb/room_size/damping/spread/hpf/dry/wet (`audio_effect_reverb.h:60-67`). 입력 버퍼 상한 1024(`reverb_filter.h:36`).
  ⇒ **실측 IR을 쓸 수 없다.** AAA에서 실제 공간(성당·터널·동굴)의 IR을 쓰는 워크플로가 불가능하다.
- 프로덕션급 FFT 라이브러리 부재. 트리에 있는 건 `smbFft`(Bernsee 스칼라 구현, `audio_effect_spectrum_analyzer.cpp:37`) 하나이고 스펙트럼 분석 전용이다. FFTW/pffft/KissFFT **미번들**.
- 사이드체인/버스 간 라우팅 그래프 없음 — 버스는 **단일 `send` 체인**뿐(`audio_server.cpp:377-391`).

### (b) 이미 있는 토대 (실측) — **`AudioEffect`는 GDExtension으로 완전히 확장 가능하다**

사용자 브리핑이 "이게 자작 가능 여부의 분기점"이라고 지목한 지점. **실측 결과 분기는 🟢 쪽이다.**

```
servers/audio/audio_effect.h:38-49
class AudioEffectInstance : public RefCounted {
    GDVIRTUAL3_REQUIRED(_process, GDExtensionPtr<const AudioFrame>, GDExtensionPtr<AudioFrame>, int)   // :42
    GDVIRTUAL0RC(bool, _process_silence)                                                               // :43
:51-61
class AudioEffect : public Resource {
    GDVIRTUAL0R_REQUIRED(Ref<AudioEffectInstance>, _instantiate)                                       // :55
```
디스패치도 실제로 연결돼 있다 — `GDVIRTUAL_CALL(_process, p_src_frames, p_dst_frames, p_frame_count)` (`audio_effect.cpp:36`), `GDVIRTUAL_BIND(_process, "src_buffer", "r_dst_buffer", "frame_count")` (`:45`), `GDVIRTUAL_BIND(_instantiate)` (`:57`).

그리고 **공식 문서가 이 경로를 명시적으로 인정한다** — `doc/classes/AudioEffectInstance.xml`:
> "**Note:** It is not useful to override this method in GDScript or C#. **Only GDExtension can take advantage of it.**"

| 요소 | 상태 | 근거 |
|------|------|------|
| `AudioFrame` 네이티브 struct 등록 | ✅ | `servers/register_server_types.cpp:175` |
| `GDVIRTUAL_NATIVE_PTR(AudioFrame)` | ✅ | `core/variant/native_ptr.h:114` |
| 처리 블록 크기 | **512 프레임 고정** | `audio_server.cpp:1310` (`buffer_size = 512`), TODO `:1308`에 "프로젝트 설정화하고 싶다" 기재 |
| 기본 믹스 레이트 | 44100 (설정 가능) | `audio_driver.h:165`, `audio_driver.cpp:215` |
| ⇒ 블록당 예산 | **~11.6 ms** | 512 / 44100 |
| 런타임 이펙트 추가/교체 | ✅ 바인딩 | `add_bus_effect` `:1845`, `get_bus_effect_instance` `:1850` |
| **이펙트별 CPU 프로파일러** | ✅ (`DEBUG_ENABLED`) | `prof_time` 누적 → `EngineDebugger::profiler_add_frame_data("servers", …)` `audio_server.cpp:1360-1375` (`"audio_thread"`/`"audio_server"`/`"audio_driver"`) |
| 무음 시 강제 처리(리버브 테일 유지) | ✅ | `_process_silence` `audio_effect.h:43`, 소비 `audio_server.cpp:357,365` |
| 기존 이펙트 20종 | ✅ | `servers/audio/effects/` — amplify·capture·chorus·compressor·delay·distortion·eq(6/10/21)·filter(LP/HP/BP/BandLimit/Notch/HighShelf/LowShelf)·hard_limiter·limiter·panner·phaser·pitch_shift·record·reverb·spectrum_analyzer·stereo_enhance |

### ⚠️ 실측된 제약 2가지
1. **이펙트 인스턴스는 채널쌍마다 독립적이다.** `bus->channels.write[k].effect_instances.write[j]->process(...)` (`audio_server.cpp:360`) — 채널 `k`마다 별도 인스턴스, 별도 상태. ⇒ **크로스채널 DSP(진짜 멀티채널 컨볼루션, 앰비소닉 디코드, M/S 처리)를 버스 이펙트로 못 만든다.** 스테레오 IR 컨볼루션은 문제없다.
2. **오디오 스레드는 단일 스레드다.** `audio_server.cpp`에 `WorkerThreadPool` 사용 0. `_mix_step`의 전 과정(디코드 → per-source 필터 → 버스 이펙트 → 센드)이 한 스레드에서 직렬 실행된다. ⇒ 컨볼루션의 무거운 파티션을 워커로 분산하려면 **이펙트 내부에서 자체 스레딩 + 1블록 지연**을 감수해야 한다(표준 기법이며 Wwise/FMOD도 동일하게 한다).

### (c) 경계

| 목표 | 코어 수정 | 방법 |
|------|-----------|------|
| **스테레오 파티션 FFT 컨볼루션 리버브 (버스 이펙트)** | **0** | `AudioEffect`+`AudioEffectInstance` GDExtension. FFT는 **pffft**(BSD-like, 단일 파일) 또는 KissFFT를 애드온에 번들 |
| per-source HRTF 컨볼루션 | **0** | 커스텀 `AudioStream`(§3) — 같은 컨볼루션 커널 재사용 |
| 멀티채널(진짜 5.1 IR) 컨볼루션 | 국소 코어 | 이펙트 인스턴스를 버스당 1개로 바꾸고 전 채널을 넘기는 API 추가 (= proposal #4435 방향) |
| 사이드체인/DSP 그래프 | 국소~중간 코어 | 버스 `send` 단일 체인 확장 |
| FFT를 코어에 번들 | 국소 코어 | 자작 애드온에 번들하면 **불필요** |

### (d) 공수 — 진지 산정
파티션(uniform-partitioned) FFT 컨볼루션은 **잘 정의된 문제**다. 512프레임 블록이 그대로 파티션 크기가 되므로 latency 0의 uniform partitioning이 자연스럽다.
- pffft 통합 + 512-포인트 파티션 overlap-save 컨볼루션 + IR 로더(WAV) + wet/dry/predelay: **3~5주**
- + 긴 IR(3~5초 = 260~430 파티션 @44.1k) 최적화(주파수 도메인 딜레이라인 + FDL 누산, SIMD): **+2~3주**
- + 비균일 파티션(low-latency 하이브리드)·IR 크로스페이드(존 전환)·스레드 분산: **+2~4주**
- **합계 4~8주 (1인).** man-year급이 아니다. **이 문서에서 자작 ROI가 가장 확실한 항목.**

### (e) 현실적 대안 — 충실도 비용
| 대안 | 충실도 비용 |
|------|-------------|
| 빌트인 `AudioEffectReverb`(Freeverb) 프리셋 셋 | 실측 IR 불가. 특징적 공간(터널·성당·협곡)의 "그 소리"가 안 남. 8-comb Freeverb는 1990년대 알고리즘이라 **금속성 잔향**이 남기 쉽다. AAA 기준 미달 |
| Freeverb + EQ + 딜레이 조합으로 수동 튜닝 | 사운드 디자이너 공수로 상당히 메울 수 있음. **존별 수동 저작 비용** |
| **자작 컨볼루션** | 충실도 비용 0. 4~8주 |
| **Steam Audio / Wwise / FMOD** | 전부 컨볼루션 리버브 내장. 충실도 비용 0, 공수 0 |

### (f) 판정 🟢 — **자작 가능·코어 0·공수 명확. 브리핑이 지목한 "분기점"은 통과했다.**

---

## 5. 미들웨어 경로 (Wwise / FMOD / Steam Audio) 🔴 — **§4 콘솔과 충돌하는 결정 지점**

<!--MIDDLEWARE-->

---

## 6. 오픈월드 앰비언스 / 스트리밍 / 폴리포니 🔴 — **가장 과소평가된 격차**

### (a) 무엇이 없나

| 격차 | 검색어 / 근거 |
|------|---------------|
| **오디오 디스크 스트리밍 전무** | 아래 (b) 참조 — **모든 오디오 에셋이 RAM 전량 상주** |
| 존 기반 앰비언스 시스템 | `audio_zone`/`reverb_zone`/`ambience` 0매치. `Area3D`+`AudioStreamPlayer3D` 수동 조합이 전부 |
| **보이스 우선순위 / 컬링 / 버추얼라이제이션** | `voice_limit`/`sound_priority` 0매치. `AudioServer`에 전역 보이스 상한 **없음** (`playback_list`는 무제한 `SafeList`) |
| 오디오 LOD (거리별 품질/레이트 저하) | 없음 |
| 믹스 스테이트 / 스냅샷 / 덕킹 | 없음 (사이드체인 부재와 동근) |
| RTPC(실시간 파라미터 제어) 대응물 | `AudioStreamPlayback::_set_parameter`(`audio_stream.h:91`)가 있으나 **스트림 내부 파라미터**용이지 글로벌 믹스 제어가 아님 |

### (b) 이미 있는 토대 (실측)

**적응형 음악은 진짜로 있다 (🟢):**
`modules/interactive_music/` — `AudioStreamInteractive` / `AudioStreamPlaylist` / `AudioStreamSynchronized` + 전용 에디터 플러그인(`editor/`).
- `AudioStreamInteractive`: 클립 **최대 63개**(`audio_stream_interactive.h:84`, "Because we use bitmasks for transition matching"), 전이 행렬, `TransitionFromTime`/`TransitionToTime`(박/마디 정렬), `FadeMode`, `AUTO_ADVANCE_DISABLED/ENABLED/RETURN_TO_HOLD`(`:66-68`).
- ⇒ **Wwise Music Segment / FMOD Transition Timeline의 축소판이 코어에 있다.** 이 축만은 미들웨어 없이도 AAA 워크플로에 근접한다.

**폴리포니 관련 실재 요소:**
| 요소 | 값 | 근거 |
|------|-----|------|
| 노드별 `max_polyphony` | 기본 1 | `scene/audio/audio_stream_player_internal.h:76`, 초과분 제거 `.cpp:93` |
| `AudioStreamPolyphonic` (동적 보이스 풀) | **기본 32**, `play_stream`/`set_stream_volume`/`set_stream_pitch_scale` | `scene/resources/audio/audio_stream_polyphonic.h:40,115-117` |
| `AudioStreamRandomizer` | ✅ | `scene/resources/audio/audio_stream_randomizer.*` |
| 무음 버스 채널 자동 비활성 | -60 dB / 2초 | `audio_server.cpp:1306-1307` (`audio/buses/channel_disable_threshold_db`, `channel_disable_time`), 판정 `:434-436` |
| 버스 상한 | 255 | `:575` |
| `AudioStreamGenerator` (스크립트 PCM 주입) | ✅ `push_frame`/`push_buffer`/`get_frames_available` | `scene/resources/audio/audio_stream_generator.h:99-102` |
| WAV 압축 포맷 | 8bit / 16bit / **IMA-ADPCM** / **QOA** | `scene/resources/audio/audio_stream_wav.h:99-103` |

### 🔴 실측된 구조적 한계 3가지 — **AAA 규모에서 순서대로 터진다**

**① 오디오 에셋이 전부 RAM에 상주한다. 디스크 스트리밍이 없다.**
```
modules/vorbis/audio_stream_ogg_vorbis.cpp:698
  const Vector<uint8_t> stream_data = FileAccess::get_file_as_bytes(p_path);
modules/mp3/audio_stream_mp3.cpp:315
  const Vector<uint8_t> stream_data = FileAccess::get_file_as_bytes(p_path);
```
그리고 Ogg는 **전 페이지를 파싱해 메모리에 보관**한다 — `Vector<Vector<PackedByteArray>> page_data` (`modules/ogg/ogg_packet_sequence.h:47`).
⇒ Godot이 하는 것은 **디코드 스트리밍**(압축 상태로 RAM에 두고 재생 중 디코드)이지 **디스크 스트리밍**이 아니다.
- **AAA 영향:** 대사(수만 라인)·시네마틱 음악·지역별 앰비언스 베드를 합치면 압축 상태로도 수백 MB~수 GB다. 콘솔 메모리 예산에서 이건 협상 불가능한 크기다. **미들웨어의 스트리밍 뱅크가 정확히 이 문제를 푸는 물건이다.**
- 우회: 오디오를 청크로 쪼개 `ResourceLoader.load_threaded_request`로 월드 셀과 함께 로드/언로드 — **가능하지만 프레임/샘플 경계에서 끊김 없이 이어붙이는 것을 전부 자작**해야 한다.

**② 믹스가 단일 스레드다.**
`servers/audio/audio_server.cpp` 전체에 `WorkerThreadPool` 사용 **0매치**. `_mix_step`은 `for (AudioStreamPlaybackListNode *playback : playback_list)` 단일 루프(`:176`)로 **디코드 + per-source 하이셸프 + 채널별 볼륨 램프**를 직렬 수행하고, 그 뒤 버스 이펙트를 직렬 수행한다.
⇒ **음원 수 N에 대해 전부 한 코어**. 11.6 ms 블록 예산 안에 N개의 Vorbis 디코드가 들어가야 한다. Vorbis 디코드는 스트림당 대략 코어의 1~2% 수준이므로 **N≈50~80에서 한 코어를 다 쓴다.** AAA 씬의 200~500 보이스는 **구조적으로 불가능**하다.

**③ 거리 컬링이 디코드 비용을 줄여주지 않는다.**
`_update_panning()`이 `max_distance` 밖 리스너를 스킵하고 `bus_volumes`를 비워도(`:560-577`), `_mix_step`은 그것과 무관하게 **매 블록 `playback->stream_playback->mix(...)`를 무조건 호출한다**(`:199`). 유일한 스킵 조건은 `PAUSED`(`:178`)와 sample 재생(`:182`)뿐.
⇒ **"멀어서 안 들리는 음원"도 풀 디코드 비용을 낸다.** 오픈월드에서 이건 치명적이다.
- 우회(코어 0): 게임 로직이 거리 기준으로 `stream_paused`를 직접 토글 — **보이스 매니저를 유저 공간에서 전부 자작**해야 하고, 상태 복원(재개 위치·페이드)도 직접 관리해야 한다.
- 우회(국소 코어): `_mix_step`에 "현재 블록의 모든 버스 볼륨이 0이고 지난 N블록도 0이면 디코드 스킵" 게이트 추가. **~30줄.** `channel_disable_threshold_db`와 같은 아이디어를 per-playback 레벨로 내리는 것.

### (c) 경계

| 목표 | 코어 수정 | 방법 |
|------|-----------|------|
| 존 기반 앰비언스 (레이어드 베드 + 크로스페이드 + 원샷 스캐터) | **0** | GDScript/GDExtension 자작. `Area3D` + `AudioStreamPolyphonic` + `AudioStreamRandomizer` 조합 |
| 보이스 매니저 (거리 우선순위 + 상한 + 스틸링) | **0** | 자작. `stream_paused` 토글 + `AudioStreamPolyphonic` 풀링 |
| **무음 음원 디코드 스킵** | 국소 코어 ~30줄 | `_mix_step`에 볼륨 게이트 |
| **믹스 멀티스레딩** (디코드를 워커로) | 중간 코어 (2~6주) | `playback_list`를 `WorkerThreadPool` 그룹 태스크로 분할 후 버스 누산만 직렬화. **버스별 누산이 경합하므로 per-thread 부분버퍼 + 리듀스 필요** |
| **오디오 디스크 스트리밍** | 중간~대규모 코어 (1~3개월) | `AudioStreamOggVorbis`를 파일 핸들 기반 링버퍼 프리페치로 재작성 + IO 스레드. `AudioStreamMP3`도 동일. 뱅크/청크 포맷 설계 포함 |
| 믹스 스냅샷 / 덕킹 / 사이드체인 | 0 (스냅샷) ~ 국소 코어 (사이드체인) | 스냅샷은 버스 볼륨 보간으로 자작 가능 |

### (d) 공수
- 존 앰비언스 시스템 자작: **2~4주**
- 보이스 매니저 자작: **2~3주**
- 무음 디코드 스킵 코어 패치: **수일** — **ROI 최고**
- 믹스 멀티스레딩: **2~6주** (오디오 스레드 실시간성 때문에 신중해야 함)
- 오디오 디스크 스트리밍: **1~3개월** — **man-year급은 아니다**

### (e) 현실적 대안 — 충실도 비용
| 대안 | 충실도 비용 |
|------|-------------|
| 오디오 총량을 RAM 예산에 맞춰 삭감 | AAA 대사량·음악량과 정면 충돌. **콘텐츠 축소 = 제품 축소** |
| 지역별 오디오 팩을 월드 셀과 함께 로드/언로드 | 상당히 유효. 단 **셀 경계에서 음악·앰비언스가 끊기지 않게 하는 것을 자작.** 대사는 여전히 문제(어디서든 재생됨) |
| 보이스 수를 60 이하로 유지 + 앰비언스는 소수 레이어드 베드로 | 실무적으로 AAA도 실제 동시 보이스는 64~128 수준으로 관리한다. **단 Godot은 "안 들리는 것도 디코드"라 실효 상한이 더 낮다** |
| **미들웨어** | 스트리밍 뱅크·보이스 관리·멀티스레드 믹스·버추얼 보이스가 전부 기본 제공. 충실도 비용 0 |

### (f) 판정 🔴 — **이 문서에서 가장 아픈 축. 그리고 미들웨어 채택의 가장 강한 근거.**
§2(오클루전)는 "Steam Audio 하나로 닫힌다"가 답이지만, **§6은 Steam Audio로 안 닫힌다** — Steam Audio는 공간화 엔진이지 스트리밍·보이스 관리 엔진이 아니다. **§6을 미들웨어 없이 가려면 코어 개조 2~4개월이 확정된다.**

---

<!--CONCLUSION-->
