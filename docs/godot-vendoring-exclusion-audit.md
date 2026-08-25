# 벤더링 제외 목록 전수 감사

> **목적:** §15-B에서 처음 드러난 패턴 — *"업스트림에 없는 게 아니라, 업스트림에 있는데 Godot이 안 가져왔다"* — 을
> `thirdparty/README.md` 전체에 대해 체계적으로 확인한다. 지금까지 이 패턴이 세 번 나왔고, 매번 견적을
> man-year급에서 벤더링 작업으로 끌어내렸다.
>
> **방법:** README의 67개 라이브러리 절에서 제외 표현(`except` · `Remove` · `minus` · `omit` 등) 29건을
> 기계적으로 수집 → 각 제외분을 **기능 배제인지 빌드 잡음인지** 분류 → 기능 배제만 upstream 실존·규모·용처 확인.

---

## 0. 결론

**새 발견 1건, 이미 처리 2건, 조건부 1건, 나머지는 전부 빌드/테스트/툴 잡음.**

제외 29건 중 **기능 배제는 6건뿐**이고, 그중 실제로 스코프에 영향을 줄 만한 것은 **harfbuzz `hb-gpu*` 하나**다.
**패턴은 실재하지만 이미 대부분 캐냈다** — 이 감사의 주된 값어치는 *"더 있을 것"이라는 미확인 가정을 닫는 것*이다.

---

## 1. 기능 배제 (6건)

| # | 라이브러리 | 제외분 | upstream 실존 | 규모 | 이 프로젝트 용처 | 판정 |
|---|---|---|:---:|---|---|:---:|
| 1 | **jolt_physics** | `Jolt/Compute` · `Jolt/Shaders` · `Jolt/Physics/Hair` | ✅ `e77f1755` | **108파일 / 9,432줄** | 스트랜드 헤어 시뮬 | ✅ **처리 완료** (Task #7) |
| 2 | **glslang** | `glslang/HLSL` | ✅ `b5782e52` | **16파일 / 19,3xx줄** | HLSL→SPIR-V 툴체인 | ✅ **처리 완료** (Task #11) |
| 3 | **harfbuzz** | `hb-gpu*` · `wasm/*` · `rust/*` | ✅ `b0ffab42` | **39파일 / 10,821줄** | GPU 글리프 래스터화 | 🔴 **신규 — 아래 §2** |
| 4 | **glslang** | `glslang/ExtensionHeaders` | ✅ `b5782e52` | **1파일** | `GL_EXT_shader_realtime_clock` | 🟡 §3 |
| 5 | **spirv-cross** | `spirv_hlsl.*` · `spirv_cpp.*` | ✅ | 2 백엔드 | HLSL/C++ **출력** | ⚪ §4 |
| 6 | **libtheora** | `arm/` · `c64x/` | ✅ | ARM 어셈블리 | Theora 디코드 SIMD | 🟡 §5 |

---

## 2. 🔴 신규 발견 — harfbuzz `hb-gpu*`

**39파일 / 10,821줄. Godot이 고정한 커밋 `b0ffab42`(14.2.0)에 그대로 존재한다.**

내용물은 **GPU 벡터 글리프 렌더링**이고, **4개 셰이더 언어 전부**를 들고 있다:

```
hb-gpu-fragment.{glsl,wgsl,msl,hlsl}   hb-gpu-vertex.{glsl,wgsl,msl,hlsl}
hb-gpu-draw.cc                          hb-gpu-cu2qu.hh   (큐빅→쿼드라틱 변환)
src/hb-gpu.h                            ← 공개 C API
```

공개 API가 있다 (`hb_gpu_draw_create_or_fail` · `hb_gpu_shader_source` · `hb_gpu_draw_*` 참조카운팅).
**죽은 코드가 아니라 정식 기능**이며 meson 빌드에 배선돼 있다.

### 그런데 지금 당장의 값어치는 제한적이다 — 근거를 같이 남긴다

- **Godot에 이미 대안이 있다.** TextServer + `msdfgen` 기반 MSDF 폰트가 스케일러블 텍스트를 커버한다.
  hb-gpu는 MSDF의 아티팩트(코너 라운딩·얇은 획)를 해결하지만, 그건 품질 상향이지 결손 보완이 아니다.
- **미성숙 신호가 있다.** 고정 커밋의 파일명(`hb-gpu-fragment.glsl`)과 현재 upstream main의 파일명
  (`hb-gpu-draw-fragment.glsl`)이 다르다 → **API가 아직 움직이는 중**이다.
- **통합 비용이 셰이더 수준이다.** Godot의 텍스트 렌더링은 `TextServerAdvanced` → `RenderingServer` 캔버스
  경로로 가는데, hb-gpu는 자체 정점/프래그먼트 셰이더로 그린다. **캔버스 렌더러에 별도 경로를 내야 한다.**
  Jolt 헤어처럼 "벤더링 + SCons 등록"으로 끝나지 않는다.

**권고: 스코프에 넣되 우선순위 낮음(Wave 3+), L7(저작툴/UI) 배정.** 지금 착수할 근거는 없고,
**"검토했고 알고 있다"로 닫는 것**이 이 감사의 목적에 부합한다.

---

## 3. 🟡 glslang `ExtensionHeaders` — 1파일, L9에 소소하게 관련

`GL_EXT_shader_realtime_clock.glsl` 하나뿐이다. 셰이더 내부에서 실시간 클록을 읽는 확장으로,
**L9의 GPU 프로파일러 작업과 맞닿는다** — 셰이더 레벨 타이밍을 원하면 필요해진다.

1파일이라 필요해지는 시점에 넣으면 된다. **지금 넣을 이유도, 나중에 못 넣을 이유도 없다.**

---

## 4. ⚪ spirv-cross `spirv_hlsl.*` — 우리 경로에 불필요

HLSL **출력** 백엔드다(입력이 아니다). 확인 결과 **Godot의 D3D12 경로는 이걸 쓰지 않는다** —
`drivers/d3d12/rendering_shader_container_d3d12.cpp:493`의 `_convert_spirv_to_dxil()`이 SPIR-V에서
DXIL로 직행한다. `spirv_cpp.*`는 CPU 상에서 셰이더를 돌리는 실험적 백엔드로 용처가 없다.

**단 하나의 조건부 용처:** 셰이더 디버깅 시 SPIR-V를 사람이 읽는 HLSL로 덤프하고 싶을 때.
그건 도구 편의지 기능이 아니다.

---

## 5. 🟡 libtheora `arm/` · `c64x/` — 실재하는 갭이나 값어치가 낮음

x86 어셈블리(`x86/`, `x86_vc/`)는 벤더링돼 있는데 **ARM은 빠져 있다.** 즉 Apple Silicon과 ARM 콘솔에서
Theora 디코드에 SIMD가 없다. (`c64x`는 TI DSP용이라 무관하다.)

**그러나 이 프로젝트에 Theora가 쓰일 일이 사실상 없다.** AAA 타이틀의 컷신/무비는 Theora를 쓰지 않는다.
비디오 코덱 자체가 별도 검토 대상이지 여기서 최적화할 지점이 아니다. **기록만 하고 닫는다.**

---

## 6. ⚪ 기능 배제가 아닌 것들 (23건) — 분류 근거

전수 확인했고 전부 다음 중 하나다. 개별 검증은 생략했다.

| 유형 | 사례 |
|---|---|
| **빌드 시스템 파일** | `CMakeLists.txt`(manifold·glslang) · `files.mk`(graphite) · `.am`/`.rc`/`.in`(libwebp) · `Makefile.*`(libvorbis) · `updateGrammar`(glslang) |
| **테스트·샘플·툴** | `MakeTables`/`etc2packer`(cvtt) · `test*.cc`/`main.cc`(harfbuzz) · `main.cpp`(spirv-cross) · 샘플(miniupnpc) · `example.c`/`pngtest.c`(libpng) · `tools`(freetype) |
| **C 인터페이스 래퍼** | `CInterface/`·`*_c[_.]*`(glslang) · `spirv_cross_c.*` |
| **Godot이 자체 제공** | `unix.c`/`win32.c`(enet — 소켓) · SDL `audio`/`camera` 서브시스템 · `gz*.c`(zlib 파일 I/O) |
| **중복 벤더링 제거** | basis_universal의 `3rdparty/{qoi,tinydds,tinyexr}` — Godot이 별도로 번들 · glslang `SPIRV/spirv.hpp11` → `spirv-headers` 사용 |
| **데이터 블롭** | brotli `dictionary.bin*` · icu `data/out` |
| **미사용 기능** | msdfgen `export-svg`/`save-*` · manifold `cross_section.h`(2D 불리언 — clipper2가 있음)·`meshIO`(OBJ 입출력 — 자체 임포터 있음) · libktx `.clang-format` · rvo2 우산 헤더 |

### ⚠️ 감사 중 제 초기 독해가 틀린 것 1건 — 기록해둔다

**libpng SIMD는 배제돼 있지 않다.** README의
*"`arm/`, `intel/`, `loongarch/`, `powerpc/` folders, **except** `arm/filter_neon.S`"*
를 처음엔 "SIMD 폴더 전부 제외"로 읽었으나, 로컬 실측하니 **네 폴더가 모두 존재**하고
`arm/filter_neon.S`(수기 어셈블리) **한 파일만** 빠져 있다 — intrinsics 버전
(`filter_neon_intrinsics.c`)이 대체한다.

README의 `except` 문구는 **폴더 제외**와 **폴더 내 파일 제외** 양쪽에 쓰이므로,
**텍스트만 읽고 판정하면 안 되고 로컬 트리를 확인해야 한다.** 이 감사에서 유일하게 오판할 뻔한 지점이었다.

---

## 7. 이 감사가 닫는 것

- **"제외 목록에 아직 뭔가 더 있을 것"이라는 미확인 가정을 닫는다.** 29건 중 기능 배제 6건,
  그중 미처리 신규는 harfbuzz 하나뿐이다.
- **§15-B 패턴은 실재하지만 이미 소진에 가깝다.** Jolt와 glslang이 큰 두 건이었고 둘 다 처리됐다.
- **다음에 upstream 버전을 올릴 때 이 문서를 갱신 대상으로 삼는다** — 상류가 새 폴더를 추가하면
  Godot의 제외 목록이 자동으로 그것도 제외할 수 있다. hb-gpu가 바로 그 사례로 보인다.
