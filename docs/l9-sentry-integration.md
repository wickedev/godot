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

태그(`v*`) 및 수동 실행 전용. 6개 매트릭스(linux/windows/macos × template_release/template_debug).

```
production=yes debug_symbols=yes separate_debug_symbols=yes
```

산출 심볼:

| 플랫폼 | 산출물 | 생성 경로 |
|--------|--------|-----------|
| Linux | `bin/*.debugsymbols` (+ 스트립된 바이너리, gnu-debuglink) | `objcopy --only-keep-debug` |
| Windows MSVC | `bin/*.pdb` | `/Zi` + `/DEBUG:FULL` (`SConstruct:808-810`) |
| macOS | `bin/*.dSYM` (arch별) | `dsymutil` (`platform_macos_builders.py:115-124`) |

**PR CI에 넣지 않은 이유:** `linux_builds.yml:50`이 *"Debug symbols disabled as they're huge on this build and we hit the 14 GB limit for runners"*라고 이미 적고 있다. 심볼 빌드를 매 PR에 켜면 러너 용량·캐시·시간이 전부 터진다. 릴리스 태그에서만 돈다.

**`windows_builds.yml:124`의 `Remove-Item ... *.pdb`는 건드리지 않았다.** 그 잡은 `debug_symbols=no`라 `/DEBUG:NONE`이 걸리고 애초에 의미 있는 `.pdb`가 생기지 않는다. 손대면 L1과의 충돌면만 넓어진다.

### (c) 심볼 검증 게이트 — 조용한 실패 차단

업로드 직전에 플랫폼별로 산출물을 **검증하고 없으면 빌드를 실패**시킨다. Linux는 `readelf -n`으로 build ID 존재까지 확인한다.

이 게이트를 넣은 이유: **심볼 파이프라인의 실패는 첫 필드 크래시가 주소 나열로 돌아올 때까지 보이지 않는다.** runtime-misc §7(e)의 ⚠️ 경고("이걸 안 하면 Sentry를 붙여도 주소 나열만 온다")를 CI가 강제하게 만든 것이다.

### (d) 업로드

`sentry-cli debug-files upload --include-sources --wait bin/` + 릴리스 생성/커밋 연결/파이널라이즈.
`--include-sources`는 우리 코드 프레임에 소스 컨텍스트를 심는다 — 렌더러 크래시를 태그 체크아웃 없이 읽기 위함.

자격증명 미설정 시 **경고 후 스킵**(실패시키지 않음). 포크에 아직 Sentry 조직이 없어도 워크플로가 초록으로 돌아야 하기 때문.

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
- **(b) DSN 및 릴리스 식별자** — 런타임 릴리스 문자열이 CI가 업로드한 심볼의 릴리스와 **정확히 일치**해야 한다. 워크플로는 `inputs.sentry-release || github.sha`를 쓴다. 게임 측도 같은 값을 써야 하며, 이 연결이 어긋나면 심볼이 붙지 않는다. **최우선 검증 항목.**
- **(c) 초기화 실패 가시화** — §1의 결론대로 출하 빌드에는 폴백이 없다. Sentry init 실패를 조용히 넘기면 안 된다.
- **(d) 종단 검증** — 의도적 네이티브 크래시를 태그 빌드에서 발생시켜 Sentry 대시보드에 **함수명·파일·행이 붙은** 스택이 오는지 확인. 여기까지 해야 Task #6이 실제로 끝난 것이다.

---

## 5. 미해결 / 스코프 밖

- **미니덤프.** runtime-misc §7(c)는 Windows SEH에 `MiniDumpWriteDump` 추가(~30줄, 국소 코어)를 제안한다. sentry-native가 자체 미니덤프를 쓰므로 **중복 가능성**이 높다. 애드온 배선 후 실측으로 판정할 것.
- **프로덕션 텔레메트리**(세션·성능·이탈). runtime-misc §7(c)에 있으나 Task #6 범위 밖.
- **Android/iOS 심볼.** 워크플로는 데스크톱 3플랫폼만 다룬다. 모바일은 콘솔(W4)과 함께 별도 판단.
- **`--build-id` 실기 검증 미완.** 개발 머신이 macOS라 `platform=linuxbsd` 구성이 불가하다(`ERROR: Invalid target platform "linuxbsd"`). 플래그 경로는 코드 검토로만 확인했고, **실증은 워크플로의 `readelf -n` 게이트가 CI에서 수행**한다. 첫 릴리스 실행 시 이 스텝의 출력을 확인할 것.

---

*작성: 2026-08-25 · L9 / godot-contributer-3 · 브랜치 `c3/l9-sentry`*
