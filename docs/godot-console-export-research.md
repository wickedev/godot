# Godot 콘솔 Export (PS5 / Xbox Series X|S / Switch·Switch 2) — 구현 딥리서치

> **문서 목적.** [godot-openworld-engine-gaps.md](./godot-openworld-engine-gaps.md) §4는 콘솔 export를 "❌ 불가 / NDA·라이선스 문제 / W4 Games 등 서드파티"로만 짧게 정리했다. 이 문서는 그 항목을 **실제 구현 관점**으로 확장한다. 핵심 재평가: 다른 §들(Nanite·Lumen·VT)과 달리 **여기서 "불가"는 기술이 아니라 계약·법률의 문제**다. 엔진 아키텍처는 오히려 콘솔 포팅에 **친화적으로 설계**되어 있고, 막는 것은 NDA·폐쇄 SDK·법적 책임과 MIT 라이선스의 근본적 충돌이다.
>
> - **로컬 실측:** `/Users/ryan/Workspace/godot` (Godot `4.7-stable-1310-geda2a482e9`, master). 모든 file:line은 이 트리 기준.
> - **웹 검증:** 딥리서치 하니스 — 5각도 팬아웃, 22개 소스 페치, 25개 주장 3표 적대적 검증(25/25 confirmed, 0 refuted). 출처 URL 병기.
> - **작성일:** 2026-08-03.

---

## TL;DR

1. **엔진 아키텍처는 콘솔 포팅에 이미 열려 있다.** 그래픽 API는 `RenderingDeviceDriver`라는 단일 추상 클래스(136개 순수 가상 메서드) 뒤에 완전히 격리돼 있고, Vulkan·D3D12·Metal이 모두 이 인터페이스의 서브클래스다. 새 콘솔 GPU 백엔드(PS5 AGC/GNM, Switch NVN)는 **상위 렌더러를 건드리지 않고** 드라이버 서브클래스 한 쌍으로 추가 가능하다.
2. **Xbox는 구조적으로 가장 가깝다.** GDK가 D3D12 기반이고, D3D12 드라이버는 이미 2023년 12월 오픈 레포에 병합됐다. Godot 코어 PR들이 명시적으로 "Xbox는 공식 지원 못 하지만 D3D12는 필수"라고 적어 두었다.
3. **그럼에도 "불가"인 이유는 코드가 아니다.** Godot 재단은 **정책적으로** 공식 콘솔 포트를 만들지 않는다 — 콘솔 SDK는 NDA로 봉인돼 있어 MIT 오픈소스로 공개할 수 없기 때문. 이건 man-year 엔지니어링 문제가 아니라 계약 구조 문제다.
4. **실질 경로는 둘.** (A) **W4 Games 위탁** — 세 플랫폼 전부 판매 중(Switch 2 베타), 매출 셰어·런타임 수수료 없이 연 $800~$10,000. (B) **직접 폐쇄소스 platform 모듈** — 기술적으로 가능하나 각 플랫폼 개발자 등록(ID@Xbox 등) → NDA → SDK 접근 → 인증(TRC/Lotcheck) 관문을 스스로 통과해야 한다.
5. **지금 오픈 영역에서 미리 해둘 수 있는 준비 작업이 실재한다** — 아키텍처 seam이 명확하므로, 콘솔 코드 없이도 "콘솔 레디" 상태로 프로젝트를 정렬할 수 있다(§6).

---

## 1. Godot 플랫폼/Export 아키텍처 — 새 플랫폼이 구현해야 하는 seam (로컬 실측)

새 플랫폼(콘솔 포함)은 아래 추상화 계층을 **subclass-and-register** 하면 된다. 대부분 코어 수정 없이 붙는다.

### 1.1 플랫폼 모듈 발견·빌드

- SConstruct가 `platform/<name>/` 를 자동 스캔하고 `detect.py` 가 있으면 플랫폼으로 등록한다 — `SConstruct:77-110`. `detect.can_build()` 가 true여야 후보가 된다(`SConstruct:99-103`).
- 필수 빌드 파일: `detect.py`(계약 함수 `get_name`/`can_build`/`get_opts`/`get_flags`/`configure`), `SCsub`(소스 목록). 상위 `platform/SCsub` 가 전 플랫폼 소스를 `platform` 정적 라이브러리로 묶는다.
- 최소 소스 세트: `os_<name>.cpp`, `display_server_<name>.cpp`, `godot_<name>.cpp`(C `main()` 진입점). 메인 루프는 OS 서브클래스 `run()` 안에 있고 `DisplayServer::process_events()` + `Main::iteration()` 를 돈다(참조: `platform/windows/os_windows.cpp:2348`).

### 1.2 추상화 계층 요약표

| Seam | 서브클래싱 대상 (file:line) | 등록 훅 |
|---|---|---|
| 빌드 | 새 `platform/<name>/detect.py` + `SCsub` | 자동 — `SConstruct:77` |
| OS | `OS` (`core/os/os.h:46`) → `os_<name>.cpp` | `godot_<name>.cpp` 진입점에서 인스턴스화 |
| DisplayServer | `DisplayServer` (`servers/display/display_server.h:62`) | `register_create_function` (`display_server.h:92`), OS `initialize()` 에서 호출 |
| GPU 컨텍스트 | `RenderingContextDriver` (`servers/rendering/rendering_context_driver.h:41`) | DisplayServer 문자열 스위치에서 `memnew` |
| **GPU 디바이스** | **`RenderingDeviceDriver` (`servers/rendering/rendering_device_driver.h:90`, 136개 순수 가상)** | `context->driver_create()` 경유 |
| 셰이더 | `RenderingShaderContainer` (`drivers/*/rendering_shader_container_*.h`) | 컨텍스트/디바이스가 반환 |
| 입력 | `Input::joy_*` 호출 (`core/input/input.h:395,476-478`) | DisplayServer `process_events()` 에서 펌프 |
| Export | `EditorExportPlatform` (`editor/export/editor_export_platform.h:51`) → `platform/<name>/export/export.cpp` | `EditorExport::add_export_platform` (`export.cpp:58`), 자동 발견 `SConstruct:96` |

### 1.3 OS 추상화 — `core/os/os.h:46`

콘솔에서 특히 중요한 순수/기대 가상:
- **동적 라이브러리 로딩** `open_dynamic_library()` `os.h:199` / `close_*` `:200` / `get_dynamic_library_symbol_handle()` `:201` — **GDExtension 지원의 전제**. (일부 콘솔은 동적 로딩을 금지 → 정적 링크 GDExtension 전략 필요, §6.)
- **파일시스템 경로** `get_data_path()` `os.h:319` / `get_config_path()` `:320` / `get_cache_path()` `:321` — 콘솔의 세이브/유저 스토리지 규칙(용량 예약, 타이틀별 격리)에 맞춰 재정의.
- 프로세스/환경/시간 계열 순수 가상 다수. 파일·스레드는 `drivers/unix/` 계층 재사용 가능(콘솔 OS가 POSIX-like면 큰 이득), 비-POSIX면 `DirAccess`/`FileAccess`/`Thread` 자체 구현.

### 1.4 Export 시스템 — `editor/export/editor_export_platform.h:51`

- `class EditorExportPlatform : public RefCounted` 추상 클래스. `export_project()` `:359`, `get_binary_extensions()` `:358`, `get_export_options()` `:315`, `has_valid_export_configuration()` 등이 `= 0` 순수 가상.
- 등록: `platform/<name>/export/export.cpp` 가 `EditorExport::get_singleton()->add_export_platform(...)`(windows 예: `export.cpp:58`). SConstruct가 `platform/<name>/export/export.cpp` 존재를 감지해 자동 배선(`SConstruct:96`) → **코어 수정 0**.

> **웹 교차검증(3-0):** export 아키텍처가 `EditorExportPlatform` 순수 가상 구현으로 등록된다는 점 — 출처 [editor_export_platform.h](https://github.com/godotengine/godot/blob/master/editor/export/editor_export_platform.h).

---

## 2. 그래픽 백엔드 이식성 — 왜 "포팅 친화적"인가 (로컬 + 웹, 핵심 절)

### 2.1 2계층 렌더링 추상화 (검증 3-0)

Godot 4.3에서 `RenderingDevice` 가 **(1) API-비의존 공유층 + (2) 얇은 API-특화 드라이버층**으로 리팩터됐다(PR [#83452](https://github.com/godotengine/godot/pull/83452), 블로그 [rendering-priorities-january-2024](https://godotengine.org/article/rendering-priorities-january-2024/)). 원문: *"keeps a single, non-abstract RenderingDevice with all those API-agnostic elements and moves all the API-specific parts to a thin wrapper around the driver."*

로컬 실측으로 확인한 실제 구조:
- **`RenderingContextDriver`** — `servers/rendering/rendering_context_driver.h:41`. 인스턴스/서피스/디바이스 열거 + `driver_create()`(`:102`)로 디바이스 드라이버 생성.
- **`RenderingDeviceDriver`** — `servers/rendering/rendering_device_driver.h:90`. **전체 GPU 추상화, 136개 순수 가상**(버퍼·텍스처·샘플러·펜스/세마포어·커맨드 큐·스왑체인·셰이더·파이프라인·렌더패스). **이것이 콘솔 이식의 실제 작업량이자 정확한 이식 지점.**
- 구현 3종: **Vulkan**(`drivers/vulkan/`, 서피스 글루는 OS별 서브클래스 `platform/windows/rendering_context_driver_vulkan_windows.h:39`), **D3D12**(`drivers/d3d12/rendering_device_driver_d3d12.h:61`), **Metal**(`drivers/metal/rendering_device_driver_metal.h:59`).

### 2.2 상위 렌더러와의 격리 — opaque 핸들 (검증 3-0)

상위 렌더러(Forward+/Mobile)는 **GPU-API 특화 객체를 절대 직접 만지지 않는다.** RDD 네임스페이스의 opaque 핸들(`RDD::BufferID`/`TextureID`/`ShaderID`/`PipelineID`/`UniformSetID`)만 조작한다 — `rendering_device_driver.h:960` `using RDD = RenderingDeviceDriver;`, `DEFINE_ID` 매크로가 `id{uint64_t}` 기반 opaque ID 생성. 상위 `struct Buffer { RDD::BufferID driver_id; ... }`(`rendering_device.h:184-185`).

**드라이버 선택 지점**(콘솔이 추가로 손댈 유일한 상위 코드 2곳):
1. DisplayServer의 드라이버 문자열 스위치 — `platform/windows/display_server_windows.cpp:8060-8067`(`"vulkan"`→`RenderingContextDriverVulkanWindows`, `"d3d12"`→`RenderingContextDriverD3D12`).
2. `register_create_function` 에 넘기는 렌더링 드라이버 이름 목록.
그 아래 `RenderingDevice::initialize()`(`servers/rendering/rendering_device.cpp:8319`)는 `context->driver_create()` → `driver->initialize()` 만 부른다.

> **결론:** 새 콘솔 GPU 백엔드 = `RenderingContextDriver` + `RenderingDeviceDriver` 서브클래스 쌍 + `RenderingShaderContainer` 포맷. **`RenderingDevice` 이상 렌더러는 무수정.** 이것이 W4 포팅이 실제로 파고드는 지점이다.

### 2.3 콘솔 API별 이식성 평가

| 콘솔 | 그래픽 API | Godot 접점 | 셰이더 경로 |
|---|---|---|---|
| **Xbox Series X\|S** | GDK / **D3D12** | ⭐ **최상** — D3D12 드라이버 이미 병합(2023-12). PR [#64304](https://github.com/godotengine/godot/pull/64304) 가 명시적으로 "Xbox는 공식 지원 못 하지만 D3D12는 필수" | GLSL→SPIR-V→(SPIRV-Cross/NIR)→HLSL→DXIL. 현재는 Mesa NIR 경로(PR [#70315](https://github.com/godotengine/godot/pull/70315)) |
| **PS5** | **AGC / GNM** + PSSL | 중 — 오픈 접점 없음. 드라이버층 신규 구현 | SPIR-V IR 자산 재사용 후 PSSL로 크로스컴파일(SPIRV-Cross 계열 가능성). NDA 하에서만 실증 가능 |
| **Switch / Switch 2** | **NVN** (Vulkan 근친) | 중~중상 — NVN이 Vulkan과 개념적으로 가까워 Vulkan 드라이버 로직 상당 부분 재사용 여지. 저사양 제약이 원신급 목표와 충돌 | Vulkan SPIR-V 파이프라인 상당 부분 전용 가능(NDA 하 실증 필요) |

> **주의(웹 caveat 반영):** PS5(GNM/AGC+PSSL)·Switch(NVN)의 **구체적 셰이더 트랜스파일 작업량은 NDA로 오픈 소스에 없어 본 리서치에서 직접 검증되지 못했다.** Xbox D3D12 경로만 1차 문서로 확정. Sony/Nintendo 경로는 구조적 유추.

---

## 3. 왜 "불가"인가 — 기술이 아니라 라이선스 (검증 3-0)

Godot 재단 공식 입장(출처 [godotengine.org/consoles](https://godotengine.org/consoles/), [docs consoles](https://docs.godotengine.org/en/stable/tutorials/platform/consoles.html)):

> *"To preserve its values of openness and freedom, the Godot Foundation does not maintain official console ports."*
> *"Godot is MIT-licensed and fully open source, no NDAs, no restricted tools, and no legal liability. Console development requires the opposite: legal contracts and closed access."*
> *"Console SDKs are secret, and protected by non-disclosure agreements. Even if we could get access to them, we could not publish the code as open-source."*

**핵심:** 격차의 성격이 §1~3(Nanite/Lumen/VT)과 근본적으로 다르다. 저기선 "man-year 엔지니어링"이 벽이었지만, 여기선 **엔진 코드는 준비돼 있고 계약 구조가 벽**이다. 그래서 해법도 "코어 개조"가 아니라 "**폐쇄소스 platform 모듈 + 등록/인증 관문 통과**"다.

### 3.1 단 하나의 비청결 seam (로컬 실측)

추상화 대부분은 out-of-tree 가능하다: 드라이버·모듈은 `custom_modules`(`SConstruct:280-281`)로 트리 밖에서 붙는다. **그러나** `platform/` 발견 루프(`SConstruct:77`)는 in-tree `platform/` 만 스캔한다 — **out-of-tree 플랫폼 탐색 경로가 없다.** 따라서 클로즈드 콘솔 platform(자체 `detect.py`/`os_*`/`display_server_*`)은 물리적으로 `platform/<console>/` 아래에 있어야 하며, 벤더 포트는 보통 **`platform/<console>/` + `drivers/<console>/`(또는 모듈)를 드롭하는 패치** 형태로 배포된다. W4 포트가 정확히 이 형태다.

---

## 4. 실제 콘솔 포팅 — W4 Games (검증 3-0)

출처: [godotengine.org/consoles](https://godotengine.org/consoles/), [w4games.com/w4consoles](https://www.w4games.com/w4consoles), [W4 발표 블로그](https://www.w4games.com/blog/w4-games-news-1/w4-games-unveils-w4-consoles-a-practical-console-porting-solution-for-godot-game-developers-12), [GamingOnLinux](https://www.gamingonlinux.com/2023/12/godot-43-dev-1-brings-major-rendering-changes-plus-w4-games-on-console-suppor/).

- **커버리지:** Nintendo Switch, PS5, Xbox Series X|S **전부 현재 구매 가능**. Switch 2는 **early beta**. PS4/Xbox One 미지원.
- **성격:** "middleware-approved" 포트 — 플랫폼 제조사 인증 요건을 통과하도록 설계. 재단이 공식 위탁 파트너로 명시.
- **워크플로우:** 프로젝트를 대체로 "unmodified"로 실행, **원클릭 배포 + 온-디바이스 디버깅**(원격 씬 트리, 스크립팅 브레이크포인트).
- **가격(검증 3-0, 출처 [W4 pricing](https://www.w4games.com/blog/w4-games-news-1/w4-games-announces-pricing-model-for-console-ports-5)):** **매출 셰어 없음, 런타임/설치 수수료 없음**. 정액 연 구독.
  - **Starter**(≤8인): 1플랫폼 $800/yr · 2플랫폼 $1,500/yr · 3플랫폼 $2,000/yr
  - **Pro**(≤20인): $4,000 / $7,500 / $10,000/yr
  - **Enterprise**: 맞춤 견적, 무제한 팀
- 포트 소스코드·문서 전체 제공, 언제든 해지 가능(단 미갱신 시 업데이트 중단).

> **시간 민감성 caveat:** 가격은 2023-12 발표 기준으로 2026년 갱신 가능성 있음(현재까지 동일가 표시 지속). "풀-피처/온-디바이스 디버깅" 세부는 벤더 문서 근거이며 독립 벤치마크 아님(존재·플랫폼 커버리지는 독립 매체 교차확인됨).

> **상용 사례:** 딥리서치가 W4 블로그의 Halls of Torment(Chasing Carrots) 인터뷰 등을 소스로 포함. 개별 타이틀별 상세(Brotato/Dome Keeper/Road to Vostok 등)는 본 검증 라운드에서 개별 확정하지 못했으므로 **미확정으로 표기** — 추가 확인 필요.

---

## 5. 직접 구현 경로 — 개발자 등록·인증 관문 (검증 3-0, Xbox 1차 확정)

직접 폐쇄소스 platform 모듈을 만들려면 코드보다 **등록·계약·인증**이 먼저다.

### 5.1 Xbox (ID@Xbox) — 1차 문서 확정

출처 [MS Learn / GDK](https://learn.microsoft.com/en-us/gaming/gdk/docs/gdk-dev/pc-dev/tutorials/pc-e2e-guide/e2e-register-id-at-xbox) (2025-06-26, upd 2025-11-06):
1. **ID@Xbox 등록** → 게임 컨셉 제출.
2. **상호 NDA 서명**(제출 후 이메일로 보통 20분, 최대 영업일 3일).
3. **컨셉 승인**.
4. 승인 시 **GDK Agreement**(개발자 툴/콘솔 GDK 접근 부여) + **TLA(Title Licensing Agreement)**(퍼블리싱 권리) 서명.

> 즉 **콘솔 GDK 툴은 컨셉 승인 이후에만 열린다.** 공개 GitHub `microsoft/GDK`는 PC 전용이라 콘솔 툴링 제외.

### 5.2 PlayStation / Nintendo — 구조적 유추

- PlayStation: PlayStation Partner 등록 → 심사 → 개발자 계약 → SDK 접근. 인증 프레임워크 **TRC**(Technical Requirements Checklist).
- Nintendo: developer.nintendo.com 등록 → 지역 오피스 프로젝트 피치 → 승인 후 포털에서 미들웨어 요청. 인증 **Lotcheck** + 가이드라인.
- Xbox 인증: **TCR/XR**(Technical Certification Requirements).

출처(2차/블로그): [GameMaker console access](https://gamemaker.io/en/help/articles/application-process-for-console-access), [n-ix certification](https://gamestudio.n-ix.com/console-certification-process-and-releasing-a-game-on-playstation-xbox-and-switch-what-you-should-know/), [ixiegaming compliance](https://www.ixiegaming.com/console-compliance-testing/).

> **caveat:** Sony/Nintendo의 구체 요건(저장/트로피/실적 추상화, 서스펜드/리줌, 인증 UI)은 NDA로 오픈에 없어 **직접 검증 안 됨**. Xbox 경로만 1차 확정.

### 5.3 인증이 엔진에 요구하는 것(공통 패턴)

세이브 데이터 규칙(용량 예약·손상 복구), 서스펜드/리줌(sleep) 처리, 계정/유저 전환, 트로피/실적 API, 컨트롤러 연결·해제 UX, 스토어 아이콘/메타데이터 요건 등. 이들이 §6 준비 작업의 방향을 정한다.

---

## 6. 오픈 영역에서 지금 미리 해둘 수 있는 준비 작업 (NDA 불요)

아키텍처 seam이 명확하므로, **콘솔 코드 없이도** 프로젝트를 "콘솔 레디"로 정렬할 수 있다. 우선순위순:

1. **RenderingDevice(RD) 백엔드 강제 + Forward+/Mobile 유지.** 콘솔 드라이버는 전부 RD 위에서 붙는다. Compatibility(GLES3) 전용 기능·GL 직참조를 피하면 이식 표면이 준다.
2. **동적 라이브러리(GDExtension) 의존 최소화 or 정적 링크 전략.** 일부 콘솔은 `open_dynamic_library()`(`os.h:199`)를 금지. GDExtension을 정적 링크 가능하게 설계하거나, 핵심 로직을 스크립트/모듈로 유지.
3. **파일 I/O를 Godot `user://` 추상으로 통일.** 절대경로·OS별 경로 하드코딩 제거 → `get_data_path()`(`os.h:319`) 재정의만으로 콘솔 세이브 규칙에 매핑되도록.
4. **입력을 `Input`/액션 맵으로만.** 컨트롤러 상태를 직접 폴링하지 말고 `Input::joy_*`(`core/input/input.h:395,476-478`) 경유. 콘솔 조이패드 드라이버가 같은 진입점(`process_events()`)에서 펌프하면 게임 코드 무수정.
5. **세이브/실적/스토어를 인터페이스 뒤로 격리.** 자체 `PlatformServices` 추상(세이브, achievement, presence)을 두고 PC는 스텁 구현 → 콘솔 모듈이 나중에 실구현을 주입. 인증(§5.3)이 요구하는 지점을 미리 캡슐화.
6. **서스펜드/리줌·손상복구·계정전환을 이벤트로 흡수.** `NOTIFICATION_APPLICATION_PAUSED/RESUMED` 등에 반응하도록 상태 저장 로직 설계(모바일에서 이미 검증되는 패턴).
7. **셰이더는 RD/godot 셰이더로.** 네이티브 GLSL 확장·플랫폼 특화 셰이더를 피하면 SPIR-V IR 자산이 그대로 크로스컴파일 파이프라인(§2.3)에 태워진다.
8. **아키텍처/컴파일러 청결성.** ARM64(Switch) 대응, 엄격한 aliasing/정렬 가정 회피, 서드파티 의존 라이브러리의 콘솔 컴파일러 호환성 사전 점검.

---

## 7. 판단 — 직접 구현 vs W4 위탁

| 축 | 직접 폐쇄소스 platform 모듈 | W4 Games 위탁 |
|---|---|---|
| 기술 난이도 | 높음 — `RenderingDeviceDriver` 136메서드 × 3 API + platform/OS/DisplayServer + 인증 대응 | 낮음 — 대체로 unmodified 실행, 원클릭 배포 |
| 진입 관문 | **스스로** 등록·NDA·SDK·devkit·인증 전부 통과 | W4가 미들웨어 승인 경로 제공 |
| 비용 | 인력(콘솔 그래픽/플랫폼 엔지니어 인월) + devkit + 인증 재제출 리스크 | **연 $800~$10,000, 매출 셰어·런타임 수수료 0** |
| 유지보수 | Godot 버전 추적 + SDK 갱신 + 재인증 지속 부담 | 구독 유지 시 업데이트 제공 |
| 통제/IP | 완전 통제, 소스 보유 | 포트 소스·문서 제공받음, 구독 종속 |
| 적합 대상 | 콘솔 자체가 사업이거나 다수 타이틀 반복 출시하는 스튜디오 | 대다수 인디~중견 — **1순위 권장** |

**전략적 결론**
- **거의 모든 실무 시나리오에서 W4 위탁이 압도적으로 합리적이다.** 매출 셰어 없는 연 $2,000(3플랫폼 Starter)은 콘솔 그래픽 엔지니어 1인의 며칠치 인건비도 안 되며, 등록·NDA·인증이라는 **비-엔지니어링 관문을 통째로 아웃소싱**한다.
- **직접 구현은 "콘솔 포팅 자체가 제품"인 조직**(제2의 W4가 되려는 경우)에서만 정당화된다. 이 경우도 벽은 코드가 아니라 3사와의 계약·인증 파이프라인이다.
- **지금 당장의 최적 행동은 §6 준비 작업이다.** 어느 경로를 택하든 "콘솔 레디" 정렬은 순이득이고, W4 위탁 시에도 포팅 마찰·비용을 직접 낮춘다.
- **§4 gaps 문서 정정 방향:** "❌ 불가"는 **"엔진은 준비돼 있으나 공식 배포가 라이선스로 막힘 → 상용 위탁(W4)이 확립된 정답, 직접 구현도 계약·인증만 통과하면 기술적으로 열려 있음"** 으로 재서술이 정확하다.

---

## 8. 미해결 질문 (후속 리서치)

1. **PS5(GNM/AGC+PSSL)·Switch(NVN)의 실제 셰이더 트랜스파일 작업량** — Xbox D3D12 같은 오픈 접점이 없는 상태에서 SPIRV-Cross/NIR 재사용이 어디까지 가능한가? (NDA 하에서만 실증 가능)
2. **Sony/Nintendo 등록·인증의 엔진 요구 항목** — 저장/트로피/실적 추상화, 서스펜드/리줌, 인증 UI 요건의 구체 목록. 본 리서치는 Xbox ID@Xbox만 1차 확정.
3. **직접 구현의 정량 공수** — 개발자 인월, 인증 재제출 횟수 등 실측 데이터(오픈 소스에 부재).
4. **W4 2026년 실제 가격·Switch 2 GA 시점** — 발표가(2023-12)와 현재가 재확인 필요.
5. **개별 상용 타이틀 포트 상세**(Brotato/Dome Keeper/Road to Vostok 등) — 본 라운드 미확정, W4/제조사 사례 개별 검증.

---

## 부록 — 핵심 file:line 인덱스 (로컬 `4.7-stable-1310`)

```
플랫폼 발견         SConstruct:77-110 ; custom_modules SConstruct:280-281
OS 추상             core/os/os.h:46 ; dyn-lib :199-201 ; 경로 :319-321
DisplayServer       servers/display/display_server.h:62 ; register :92
GPU 컨텍스트        servers/rendering/rendering_context_driver.h:41 (driver_create :102)
GPU 디바이스        servers/rendering/rendering_device_driver.h:90 (136 순수 가상)
  Vulkan            drivers/vulkan/ ; OS 서피스 platform/windows/rendering_context_driver_vulkan_windows.h:39
  D3D12             drivers/d3d12/rendering_device_driver_d3d12.h:61
  Metal             drivers/metal/rendering_device_driver_metal.h:59
드라이버 선택       platform/windows/display_server_windows.cpp:8060-8067
RD 초기화           servers/rendering/rendering_device.cpp:8319 (driver_create → initialize)
셰이더 컨테이너     drivers/*/rendering_shader_container_*.h
입력 조이패드       core/input/input.h:395,476-478 ; drivers/sdl/joypad_sdl.cpp
Export 플랫폼       editor/export/editor_export_platform.h:51 ; 등록 platform/windows/export/export.cpp:58
```

## 부록 — 출처 (웹, 검증 통과분)

- Godot RD 2계층 분리: [PR #83452](https://github.com/godotengine/godot/pull/83452) · [블로그 2024-01](https://godotengine.org/article/rendering-priorities-january-2024/)
- D3D12 드라이버·Xbox 언급: [PR #64304](https://github.com/godotengine/godot/pull/64304) · [PR #70315](https://github.com/godotengine/godot/pull/70315)
- 재단 콘솔 정책: [godotengine.org/consoles](https://godotengine.org/consoles/) · [docs consoles](https://docs.godotengine.org/en/stable/tutorials/platform/consoles.html)
- W4 Consoles: [w4consoles](https://www.w4games.com/w4consoles) · [발표](https://www.w4games.com/blog/w4-games-news-1/w4-games-unveils-w4-consoles-a-practical-console-porting-solution-for-godot-game-developers-12) · [가격](https://www.w4games.com/blog/w4-games-news-1/w4-games-announces-pricing-model-for-console-ports-5) · [GamingOnLinux](https://www.gamingonlinux.com/2023/12/godot-43-dev-1-brings-major-rendering-changes-plus-w4-games-on-console-suppor/)
- Xbox 등록: [MS Learn ID@Xbox](https://learn.microsoft.com/en-us/gaming/gdk/docs/gdk-dev/pc-dev/tutorials/pc-e2e-guide/e2e-register-id-at-xbox)
- 인증/등록 일반: [GameMaker](https://gamemaker.io/en/help/articles/application-process-for-console-access) · [n-ix](https://gamestudio.n-ix.com/console-certification-process-and-releasing-a-game-on-playstation-xbox-and-switch-what-you-should-know/) · [ixiegaming](https://www.ixiegaming.com/console-compliance-testing/)

*검증: 딥리서치 하니스 5각도 팬아웃, 22소스 페치, 25주장 3표 적대적 검증(25/25 confirmed). 로컬 file:line은 소스트리 직접 실측(Explore 에이전트 + 수동 grep).*
