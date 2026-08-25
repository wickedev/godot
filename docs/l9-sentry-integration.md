# L9 — Sentry + CI 심볼 업로드 (Task #6)

> **레인:** L9 플랫폼/인프라 · **Wave 0** · 로드맵 §14 "다른 모든 것보다 먼저"
> **정본:** [runtime-misc §7](./godot-runtime-gaps-misc-research.md) (크래시/텔레메트리)
> **상태:** 엔진 리포 측 구현 완료 · 게임 프로젝트 측은 착수 조건 미충족(§4)
> **브랜치:** `c3/l9-sentry`

---

## 0. 요약

로드맵이 이 항목을 §1 착수 전 선행으로 지정한 이유는 **"자작 렌더러의 하드 크래시를 필드에서 수집할 수단"** 확보다.
착수해 보니 그 근거가 **원래 문서가 적은 것보다 강하다.** 아래 §1이 이유다.

엔진 리포에서 할 수 있는 일(심볼 파이프라인)은 전부 했고, GDExtension 애드온 배선은 **엔진 리포의 일이 아니다**(§4).

---

## 1. ⚠️ 실측 정정 — 정본 문서 §7(b)의 전제가 출하 빌드에는 성립하지 않는다

runtime-misc §7(b)는 3플랫폼 크래시 핸들러가 실재하며 심볼화까지 한다고 적고, 이렇게 결론짓는다:

> ⇒ **"크래시 나면 심볼 붙은 스택이 stderr에 찍힌다"까지는 이미 된다.** ... **없는 건 "그 출력을 서버로 모으는 것"뿐이다.**

**이 서술에는 빌드 타깃 조건이 빠져 있다.** 실측 결과 4개 크래시 핸들러 전부가 `DEBUG_ENABLED` 게이트 안에 있다:

| 플랫폼 | 파일 | 게이트 | `target=template_release`에서 |
|--------|------|--------|------------------------------|
| Linux/BSD | `platform/linuxbsd/crash_handler_linuxbsd.cpp:42-44` | `#ifndef DEBUG_ENABLED` → `#undef CRASH_HANDLER_ENABLED` | **컴파일 제외** |
| macOS | `platform/macos/crash_handler_macos.mm:45-46` | `#if defined(DEBUG_ENABLED)` → `#define CRASH_HANDLER_ENABLED 1` | **컴파일 제외** |
| Windows (SEH) | `platform/windows/crash_handler_windows.h:36-37` | `#if defined(DEBUG_ENABLED)` → `#define CRASH_HANDLER_EXCEPTION 1` | **컴파일 제외** |
| Windows (signal) | 동일 헤더 게이트 (`crash_handler_windows_signal.cpp:41,312,322`) | 〃 | **컴파일 제외** |

그리고 `SConstruct:535,551-554`:
```python
env.debug_features = env["target"] in ["editor", "template_debug"]
...
if env.debug_features:
    env.Append(CPPDEFINES=["DEBUG_ENABLED"])
```

⇒ **`template_release`에는 `DEBUG_ENABLED`가 없다. 즉 플레이어에게 나가는 빌드에는 크래시 핸들러가 아예 없다.**
§7(b)가 기술한 `atos` 심볼화·로드 주소 출력·엔진 커밋 해시 출력은 **에디터/`template_debug`에서만** 동작한다.

**결론 두 가지:**

1. **Sentry의 가치가 문서가 평가한 것보다 크다.** "이미 stderr에는 찍히니 모으기만 하면 된다"가 아니라, **출하 빌드에는 크래시 진단 수단이 0**이다. 로드맵이 이걸 §1 선행으로 올린 판단은 옳았고, 근거는 오히려 더 강하다.
2. **폴백이 없다.** Sentry 초기화가 실패하면 대체 경로가 없다. 게임 프로젝트 측 배선에서 초기화 실패를 반드시 가시화해야 한다(§4-c).

### 부수 효과 — 핸들러 충돌 없음 (좋은 소식)

sentry-native(crashpad/breakpad)는 자체 시그널/SEH 핸들러를 설치한다. Godot도 `signal(SIGSEGV, handle_crash)`를 쓰므로(`crash_handler_linuxbsd.cpp:275-277`) 원칙적으로 충돌 소지가 있다. 실제로는:

- **출하(`template_release`)**: Godot 핸들러가 없으므로 sentry-native가 **단독 소유**. 충돌 자체가 없다.
- **에디터/`template_debug`**: Godot이 `OS_LinuxBSD::initialize`(`os_linuxbsd.cpp:175`)에서 먼저 설치하고, GDExtension은 `main.cpp:756`(SERVERS 레벨)에서 **나중에** 로드된다 → sentry가 마지막에 설치되어 이긴다. Godot의 stderr 백트레이스는 이때 손실된다.

⇒ **runtime-misc §7(c)의 "크래시 리포트 자동 수집 — 코어 수정 0" 판정은 유지된다.** 개발 중 stderr 백트레이스를 계속 원하면 그때만 코어 수정 대상이고, 지금은 필요 없다.

---

## 2. 엔진 리포 측 구현 (이 브랜치)

### (a) `SConstruct` — ELF 빌드 ID 고정

```python
if env["platform"] == "linuxbsd":
    env.AppendUnique(LINKFLAGS=["-Wl,--build-id=sha1"])
```

**왜 필요한가.** `separate_debug_symbols=yes`는 바이너리를 `--strip-debug --strip-unneeded`로 스트립하고 디버그 정보를 `.debugsymbols`로 뺀다(`platform/linuxbsd/platform_linuxbsd_builders.py:6-9`). 스트립 후 **크래시 주소를 심볼 파일에 이어주는 유일한 식별자가 GNU build ID**다. 배포판 GCC spec은 보통 `--build-id`를 켜두지만 `linker=lld`/`mold`에서는 보장되지 않는다 → 명시 고정.

Android는 이미 `platform/android/detect.py:252`에 `-Wl,--build-id`가 있다. macOS는 Mach-O UUID, Windows MSVC는 PDB GUID+age를 쓰므로 해당 없음.

### (b) `.github/workflows/release_symbols.yml` — 신규

**릴리스 태그(`*-stable`)** 및 수동 실행 전용. 6개 매트릭스(linux/windows/macos × template_release/template_debug).

> 트리거가 `*-stable`인 이유: 이 포크는 업스트림 Godot의 태그 관례를 유지한다. 실측 — `v*` 태그 **0개**, `*-stable` 태그 **72개**(`4.7.1-stable` 등). 초안의 `v*` 트리거는 **한 번도 발화하지 않았을 것**이다. 포크 태그 정책은 메인테이너가 `*-stable` 유지로 확정.

```
production=yes debug_symbols=yes separate_debug_symbols=yes
```

산출 심볼:

| 플랫폼 | 산출물 | 생성 경로 |
|--------|--------|-----------|
| Linux | `bin/*.debugsymbols` (+ 스트립된 바이너리, gnu-debuglink) | `objcopy --only-keep-debug` |
| Windows MSVC | `bin/*.pdb` | `/Zi` + `/DEBUG:FULL` (`SConstruct:808-810`) |
| macOS | `bin/*.dSYM` (arch별) | `dsymutil` (`platform_macos_builders.py:115-124`) |

**러너 준비는 기존 플랫폼 워크플로를 그대로 복제한다** — Windows D3D12 SDK·ANGLE·AccessKit, macOS Vulkan SDK(MoltenVK)·ANGLE·AccessKit, Linux `libwayland-bin`. 심볼은 **출하본과 같은 기능 세트로 빌드된 바이너리**에서 나와야 하므로 이 스텝들은 `linux/windows/macos_builds.yml`과 계속 동기화되어야 한다.

**PR CI에 넣지 않은 이유:** `linux_builds.yml:50`이 *"Debug symbols disabled as they're huge on this build and we hit the 14 GB limit for runners"*라고 이미 적고 있다. 심볼 빌드를 매 PR에 켜면 러너 용량·캐시·시간이 전부 터진다.

**`windows_builds.yml:124`의 `Remove-Item ... *.pdb`는 건드리지 않았다.** 그 잡은 `debug_symbols=no`라 `/DEBUG:NONE`이 걸리고 애초에 의미 있는 `.pdb`가 생기지 않는다. 손대면 L1과의 충돌면만 넓어진다.

### (c) 심볼 **대응** 검증 게이트 — 조용한 실패 차단

존재 확인이 아니라 **출하 바이너리와 사이드카가 같은 디버그 식별자를 갖는지**를 검사하고, 어긋나면 빌드를 실패시킨다.

| 플랫폼 | 검증 |
|---|---|
| Linux | `readelf -n`으로 바이너리와 `.debugsymbols`의 **build ID 동일성** 대조. 미존재/불일치 시 실패 |
| macOS | 두 arch를 `lipo`로 합쳐 **실제 출하 형태(universal)를 만든 뒤**, `dwarfdump --uuid`로 **모든 슬라이스 UUID가 dSYM에 커버되는지** 확인 |
| Windows | `.exe`↔`.pdb` 베이스네임 짝 확인. **PDB GUID+age 동일성은 `sentry-cli`가 업로드 시 검증**한다(불일치 PDB는 거부됨) |

> **macOS에 대한 근거:** `lipo`는 Mach-O 슬라이스의 UUID를 변경하지 않는다. 따라서 arch별 dSYM은 universal 바이너리에 대해 유효하다 — 다만 이 리포트는 그걸 **가정하지 않고 실제로 대조해서 증명**한다.
>
> ⚠️ **범위 한정:** 이 워크플로는 export template 바이너리와 그 심볼까지만 다룬다. `generate_bundle`을 통한 **완전한 `.app`/export-template zip 조립은 하지 않는다** — `generate_bundle`은 release와 debug 바이너리가 **같은 잡에 함께** 있어야 동작하는데 현재 매트릭스는 target별로 잡이 분리돼 있다. 패키징까지 필요하면 별도 태스크로 분리 요청.

### (d) 업로드 / 릴리스 마감

- 업로드 조건: `github.event_name != 'workflow_dispatch' || inputs.upload`
  > ⚠️ 초안의 `inputs.upload != false`는 **버그였다.** 태그 push 이벤트에서 `inputs.upload`는 null이고, GitHub 식은 null과 false를 **둘 다 0으로 캐스팅**하므로 `null != false` → `0 != 0` → **false**가 된다. 즉 자동 릴리스 경로에서 업로드가 통째로 스킵됐을 것이다. 리뷰에서 지적받아 수정.
- **fail-open 금지:** 자격증명이 없으면 **실패**시킨다. 심볼 없이 조용히 릴리스가 나가는 것이 이 워크플로가 막으려는 실패 모드 그 자체다. 빌드 전용 드라이런을 원하면 수동 실행에서 `upload=false`를 쓴다.
- `sentry-cli` 설치는 `continue-on-error` — npm 장애가 드라이런을 죽이지 않게. 실제 업로드가 필요한데 없으면 업로드 스텝이 하드 실패한다.
- **릴리스 생성/커밋연결/파이널라이즈는 매트릭스가 아니라 후속 단일 잡(`finalize-release`)** 에서 1회 실행한다. 매트릭스 6개가 같은 릴리스 객체에 동시 쓰기하면 레이스가 난다. `|| true` 억제도 제거했다 — 실패는 드러나야 한다.
- `--include-sources`는 우리 코드 프레임에 소스 컨텍스트를 심는다 — 렌더러 크래시를 태그 체크아웃 없이 읽기 위함.

---

## 3. 필요한 리포지터리 설정 (미완 — 사람이 해야 함)

| 종류 | 이름 | 용도 |
|------|------|------|
| Secret | `SENTRY_AUTH_TOKEN` | `project:releases` + `org:read` 스코프 |
| Variable | `SENTRY_ORG` | Sentry 조직 슬러그 |
| Variable | `SENTRY_PROJECT` | Sentry 프로젝트 슬러그 |

이게 없으면 워크플로는 빌드·검증까지만 하고 업로드를 스킵한다.

---

## 4. 게임 프로젝트 측 (이 리포의 일이 아님) — 착수 조건 미충족

`getsentry/sentry-godot`은 **GDExtension 애드온**이고 `addons/` 아래에 들어간다. **이 리포는 엔진 리포이고 게임 프로젝트가 없다.** 따라서 SDK 배선은 여기서 완료할 수 없다.

착수 가능해지는 시점에 필요한 것:

- **(a) 애드온 설치** — `sentry-godot` v1.2.0, `addons/sentry/`. 지원: Windows/Linux/macOS/iOS/Android.
- **(b) DSN 및 릴리스 식별자**

  > **정정 (리뷰 지적 반영).** 이 항목의 초판은 *"런타임 릴리스 문자열이 CI 업로드 릴리스와 일치하지 않으면 심볼이 붙지 않는다"*고 적었다. **틀렸다.**
  >
  > **네이티브 DIF 매칭의 키는 디버그 식별자다** — ELF는 GNU build ID, Mach-O는 UUID, PE/PDB는 GUID+age. 크래시 리포트의 모듈 목록이 이 값을 싣고 오고, Sentry는 그것으로 업로드된 DIF를 찾는다. **`release` 값은 이 조회에 관여하지 않는다.** `--include-sources`가 심는 소스 컨텍스트도 DIF 자체에 박히므로 마찬가지로 release와 무관하다.

  따라서 실제 관계는 이렇게 갈린다:

  | 어긋나면 | 결과 |
  |---|---|
  | **디버그 식별자**(build ID/UUID/GUID+age) | **심볼이 안 붙는다** — 주소 나열만 온다. **이것이 진짜 최우선 항목이며, §2(c) 게이트가 CI에서 강제한다.** |
  | **release 문자열** | 심볼은 정상적으로 붙는다. 대신 이슈 그룹핑·리그레션 추적·release health·`set-commits`의 suspect commit 연결이 어긋난다 |

  게임 측은 워크플로와 같은 값(`inputs.sentry-release \|\| github.sha`)을 쓰는 것이 여전히 옳다 — 다만 그 이유는 **심볼리케이션이 아니라 릴리스 추적**이다.
- **(c) 초기화 실패 가시화** — §1의 결론대로 출하 빌드에는 폴백이 없다. Sentry init 실패를 조용히 넘기면 안 된다.
- **(d) 종단 검증 (최우선)** — 의도적 네이티브 크래시를 태그 빌드에서 발생시켜 Sentry 대시보드에 **함수명·파일·행이 붙은** 스택이 오는지 확인. 여기까지 해야 Task #6이 실제로 끝난 것이다.

---

## 5. 미해결 / 스코프 밖

- **미니덤프.** runtime-misc §7(c)는 Windows SEH에 `MiniDumpWriteDump` 추가(~30줄, 국소 코어)를 제안한다. sentry-native가 자체 미니덤프를 쓰므로 **중복 가능성**이 높다. 애드온 배선 후 실측으로 판정할 것.
- **프로덕션 텔레메트리**(세션·성능·이탈). runtime-misc §7(c)에 있으나 Task #6 범위 밖.
- **Android/iOS 심볼.** 워크플로는 데스크톱 3플랫폼만 다룬다. 모바일은 콘솔(W4)과 함께 별도 판단.
- **패키징 미포함.** macOS `.app`/export-template zip 조립(`generate_bundle`)은 범위 밖 — §2(c) 참조.
- **Windows PDB GUID+age 동일성을 워크플로가 직접 단언하지 않는다.** `sentry-cli`의 업로드 시 거부에 의존한다. 네이티브 도구만으로 PE 디버그 디렉터리를 파싱하는 건 러너에 보장되지 않는 도구(`llvm-pdbutil`/`dumpbin`)를 요구한다.
- **`--build-id` 실기 검증 미완.** 개발 머신이 macOS라 `platform=linuxbsd` 구성이 불가하다(`ERROR: Invalid target platform "linuxbsd"`). 플래그 경로는 코드 검토로만 확인했고, **실증은 워크플로의 `readelf -n` 게이트가 CI에서 수행**한다. 첫 릴리스 실행 시 이 스텝의 출력을 확인할 것.

---

*작성: 2026-08-25 · L9 / godot-contributer-3 · 브랜치 `c3/l9-sentry`*
