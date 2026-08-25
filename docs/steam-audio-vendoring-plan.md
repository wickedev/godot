# Steam Audio 벤더링 계획 (Task #13 스코핑)

> 2026-08-25 upstream 실측 (`ValveSoftware/steam-audio` @ `480dd64`, shallow). 오디오 전략 확정([결정](./godot-audio-middleware-console-decision.md): A안)의 실행 계획.

## 1. 실측 요약

- 리포 구성: `core`(라이브러리 본체) + `fmod`/`unity`/`unreal`/`wwise`(타 엔진 통합 — **전부 불요**)
- 본체: `core/src/core` — **136 .cpp** (src 전체 .cpp/.h 391파일). Apache-2.0.
- 빌드: CMake. Godot은 SCons → **SCsub 신규 작성 필요** (CMake 벤더링 안 함, Jolt와 동일 패턴).

## 2. 의존성 매트릭스 (CMakeLists 실측)

| 의존성 | 필수 여부 | 처리 |
|---|---|---|
| **PFFFT** | REQUIRED | 벤더링 (BSD-like, 소형 FFT) |
| **MySOFA** | REQUIRED | 벤더링 (HRTF SOFA 파일 로더) — zlib 의존 확인 필요(Godot 번들 zlib 재사용) |
| **FlatBuffers** | REQUIRED | 벤더링 또는 헤더온리 서브셋 — Godot 트리 내 기존 flatbuffers 존재 여부 확인 후 결정 |
| IPP / MKL | optional (x86 가속) | **v1 제외** — pffft 폴백 |
| FFTS | optional | v1 제외 |
| **Embree 4** | optional (반사·베이크 레이트레이서) | **v1 제외, v2 재검토** — 실시간 회절·오클루전은 내장 레이트레이서로 동작. 베이크 품질 필요 시점에 재개봉. RT 백엔드는 장기적으로 우리 HW-RT(L3)와 통합 검토 |
| ISPC / RadeonRays / TrueAudioNext | optional (GPU 가속) | v1 제외 — G5 이후 재검토 (Jolt/Compute와 같은 판정 구조) |
| Java / Python | 바인딩 생성용 | 불요 |

## 3. 단계

1. **13a — 소스 벤더링**: `thirdparty/steam_audio/` = core/src/core + pffft + mysofa (+flatbuffers 판정) + `thirdparty/README.md` 갱신. SCsub 작성, `phonon.h` C API까지 컴파일 통과. 콘솔 고려: x86/ARM LE 순수 CPU 코드라 이식성 높음 — 플랫폼 gate는 SCsub에서 데스크톱 우선.
2. **13b — Godot 통합 (코어 0 경로)**: `AudioEffect`(GDVIRTUAL) 기반 공간화 이펙트 + `AudioStreamPlayback::_mix` 기반 per-source 바이노럴/오클루전. audio 리서치 §3·§4가 확정한 경로. 씬 지오메트리 → Steam Audio 메시 등록 브리지(정적 우선).
3. **13c — §6 연동**: 자체 보이스 매니저(코어 0)와 오클루전 레이 예산 연동. [무음 디코드 스킵 패치](../servers/audio/audio_server.cpp)(리뷰 중)와 합류.

## 4. 리스크

- FlatBuffers 버전 충돌 (다른 서브시스템이 도입할 경우) — 벤더링 시 네임스페이스 격리 확인.
- 콘솔 인증 선례 부재 — [W4 질의 #3](./w4-console-inquiry-checklist.md) 답변으로 조기 해소.
- 규모: 13a 1~2주 / 13b 3~6주 / 13c는 §6 일정에 종속. L8 담당 확보 시 착수.
