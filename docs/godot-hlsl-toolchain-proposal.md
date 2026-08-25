# HLSL 셰이더 툴체인 — Task #11 (7b) 제안서

> ## ⛔ 정정 (2026-08-25, 실측 후) — 아래 §0의 결론은 **채택되지 않았다**
>
> **"새 외부 의존 0"은 런타임 기준으로만 참이었다.** glslang의 HLSL 프론트엔드는 벤더링으로 공짜지만,
> **그 출력을 쓸 수 있게 만드는 legalization(SPIRV-Tools)은 공짜가 아니다.** glslang의 HLSL 출력은
> `spirv-opt --legalize-hlsl`을 전제로 설계돼 있는데, Godot은 `ENABLE_OPT=0`이고 SPIRV-Tools를
> 벤더링하지 않는다(`spirv-headers`·`spirv-reflect`·`spirv-cross`·`re-spirv`는 있으나 본체는 없다).
>
> **벤더링된 Jolt 커널 17개 전수 실측:**
>
> | 경로 | 컴파일 | spirv-val |
> |---|---|---|
> | glslang HLSL 프론트엔드 (`ForbidIncluder`) | **0/17** | — |
> | glslang HLSL + VFS includer | 16/17 | 측정 불가 (검증기 없음) |
> | **DXC 오프라인** | **17/17** | **17/17** |
>
> → **채택된 방향은 ③이 아니라 오프라인 툴체인이다.** 커널은 벤더링된 고정 자산이라 런타임 컴파일이
> 필요 없고, 사용자 셰이더는 GLSL 경로를 쓴다. DXC를 **오프라인 도구로** 쓰고 `.spv` 산출물을 커밋한다.
> 런타임 HLSL 경로는 수요자가 없어 폐기한다. SPIRV-Tools 벤더링도 `ENABLE_OPT` 전환도 불요.
>
> **§1(e)의 판정도 축을 잘못 잡았다.** "SM6 전용 기능 grep 0매치 → 프론트엔드 사정권"이라 썼으나,
> 문제는 SM 레벨이 아니라 **프론트엔드 성숙도**였다. `HairSkinVertices`는 SM5 문법
> (`typedef float4 JPH_Mat44[4]` — 배열 매개변수)인데도 glslang이 오버로드를 못 푼다. DXC는 처리한다.
> **기능 목록 grep으로는 이 축이 보이지 않으며, 실제 커널을 컴파일해봐야 나온다.**
>
> 아래 본문은 판단 경위 기록으로 남긴다. §2의 glslang 벤더링 실측(16파일 실존·`ENABLE_HLSL` 코드
> 경로 잔존)과 §4의 spirv-cross 조사는 여전히 유효하다.

---

> **결정 요청:** Jolt 헤어의 GPU 백엔드(7c)를 켜려면 HLSL을 SPIR-V/metallib로 컴파일하는 수단이 필요하다.
> 7a 보고 시점에는 선택지를 "① DXC를 빌드 의존으로 추가 vs ② 사전 컴파일 바이너리 벤더링" 둘로 봤다.
> **실측 결과 셋째 선택지가 있고, 그게 답이다.**

---

## 0. 결론 먼저

**glslang의 HLSL 프론트엔드를 벤더링한다. 새 외부 의존은 0이다.**

Godot은 이미 glslang과 SPIRV-Cross를 둘 다 번들하고 있다. 빠진 건 glslang의 `HLSL/` 서브폴더 하나뿐이며,
**Godot이 고정한 바로 그 커밋에 존재한다.** §15-B의 Jolt 사례와 구조가 동일하다 —
*"업스트림에 없다"가 아니라 "업스트림에 있는데 Godot이 안 가져왔다"*.

```
HLSL  --glslang(HLSL 프론트엔드)-->  SPIR-V  --SPIRV-Cross(spirv_msl)-->  MSL --> metallib
        ↑ 벤더링 필요 (16파일)                  ↑ 이미 번들됨
```

---

## 1. 실측 근거

### (a) glslang HLSL 프론트엔드는 제외돼 있다 — 의도적으로

> `thirdparty/README.md:425-426`
> *"`glslang/` folder (except the `glslang/HLSL` and `glslang/ExtensionHeaders` subfolders), `SPIRV/` folder"*

로컬 실측: `thirdparty/glslang/glslang/` 하위는 `GenericCodeGen · Include · MachineIndependent · OSDependent ·
Public · ResourceLimits` 뿐. **`HLSL/` ABSENT.**

### (b) 그런데 코드 경로는 이미 벤더링된 소스 안에 있다

`ENABLE_HLSL` 매크로가 번들된 파일들에 그대로 살아 있다:

| 파일 | 위치 |
|---|---|
| `SPIRV/GlslangToSpv.cpp` | 2152, 2216, 6522 |
| `glslang/Include/Types.h` | 102, 154 |
| `glslang/Public/ShaderLang.h` | `EShSourceHlsl` (136) |

→ **소스를 넣고 `ENABLE_HLSL`을 정의하면 켜지는 구조다.** 개조가 아니다.

### (c) 상류 실존 확인

`KhronosGroup/glslang` 을 클론해 Godot이 고정한 커밋 `b5782e52ee2f7b3e40bb9c80d15b47016e008bc9`
(= `vulkan-sdk-1.4.335.0`, 커밋 메시지 *"Update CHANGES for 16.1.0"*)에서 확인:

```
glslang/HLSL/hlslAttributes.{cpp,h}    hlslGrammar.{cpp,h}      hlslOpMap.{cpp,h}
glslang/HLSL/hlslParseHelper.{cpp,h}   hlslParseables.{cpp,h}   hlslScanContext.{cpp,h}
glslang/HLSL/hlslTokenStream.{cpp,h}   hlslTokens.h             pch.h
--> 16 파일
```

**버전 스큐 없음.** 번들 glslang과 정확히 같은 커밋에서 가져온다.

### (d) Metal 절반은 이미 완성돼 있다

`spirv-cross`가 번들돼 있고 **`spirv_msl.cpp`가 포함**된다(`thirdparty/README.md:1022`는
`spirv_hlsl.*`만 제외 — 그건 HLSL *출력*이라 우리에게 불필요). Godot 자신이 이걸로 Metal 셰이더를 만든다:

- `drivers/metal/rendering_shader_container_metal.cpp:43,325,360` — `#include <spirv_msl.hpp>`, `CompilerMSL`
- `drivers/metal/metal_device_properties.cpp:59`

→ **SPIR-V → MSL 경로는 새로 만들 게 없다.**

### (e) Jolt 셰이더는 glslang HLSL 프론트엔드로 감당된다

glslang의 HLSL 프론트엔드는 레거시다(Khronos는 SM6+에 DXC를 권한다). 그래서 **Jolt가 실제로 쓰는 기능만
따졌다.** `Jolt/Shaders/ShaderCore.h:60-74`의 HLSL 브랜치 전체:

```hlsl
[numthreads(x, y, z)]              // SM5
uint3 name : SV_DispatchThreadID   // SM5
StructuredBuffer<T> / RWStructuredBuffer<T>   // SM5
InterlockedAdd                     // SM5
```

전 셰이더에서 SM6 전용 기능 grep(`Wave*` 인트린식 · `QuadRead*` · `globallycoherent` · `RayQuery` ·
템플릿) → **0매치.** 전부 SM5 수준이고 glslang HLSL 프론트엔드의 사정권이다.

바인딩 호환도 문제없다. `ComputeShaderVK`는 SPIR-V를 **자체 리플렉션**해 이름으로 바인딩을 찾는다
(`NameToBufferInfoIndex`, `mLayoutBindings` @ `Jolt/Compute/VK/ComputeShaderVK.h`) — 특정 디스크립터
레이아웃을 강요하지 않는다.

---

## 2. 선택지 비교

| | **① DXC 빌드 의존** | **② 사전 컴파일 바이너리 벤더링** | **③ glslang HLSL 벤더링 (제안)** |
|---|---|---|---|
| 새 외부 의존 | **DXC (LLVM 기반, 대형)** | 없음 | **없음** |
| 추가 벤더링 | DXC 전체 | `.spv`/`.metallib` 바이너리 | **16 파일 (~텍스트)** |
| 버전 스큐 | DXC ↔ SPIR-V ↔ Vulkan SDK 3중 | 바이너리가 소스와 어긋나도 안 보임 | **없음 — 번들 glslang과 동일 커밋** |
| 셰이더 수정 시 | 재빌드로 반영 | **수동 재컴파일 필요.** 잊으면 무음 불일치 | 재빌드로 반영 |
| 감사 가능성 | 소스에서 재현 가능 | **바이너리는 감사 불가** | 소스에서 재현 가능 |
| CI 부담 | DXC 툴체인 설치·플랫폼별 | 없음 | **없음** |
| SM6 확장성 | ✅ | 해당 없음 | ⚠️ SM6 필요해지면 막힘 |
| 라이선스 | LLVM/NCSA — 신규 검토 필요 | Jolt MIT 유지 | **glslang(BSD 계열) — 이미 리포에 있음** |

**②를 권하지 않는 이유가 가장 분명하다.** 벤더링 바이너리는 소스와 어긋나도 **아무 신호가 없다.**
codespell 사건에서 확인한 것과 같은 종류의 위험이다 — 무음 불일치는 감사 가능성을 파괴한다.

**①은 과잉이다.** DXC는 LLVM 기반 대형 툴체인이고, 얻는 건 우리가 쓰지도 않는 SM6 기능이다.

---

## 3. 제안하는 작업 (Task #11 실행 계획)

| 단계 | 내용 | 규모 |
|---|---|---|
| 11-a | `glslang/HLSL/` 16파일 벤더링 + `thirdparty/README.md` 갱신 | 반나절 |
| 11-b | `modules/glslang/SCsub`에 소스 등록 + `ENABLE_HLSL` 정의 (**모듈 env 한정** — 7a 리뷰 교훈) | 반나절 |
| 11-c | HLSL→SPIR-V 컴파일 진입점. `compile_glslang_shader()`(`modules/glslang/register_types.cpp:49`)가 이미 있으므로 **HLSL 소스 언어 분기 추가**로 끝날 가능성이 높다 | 1~2일 |
| 11-d | Jolt 15개 헤어 커널을 빌드 스텝에서 `.spv`로 굽기. `ComputeSystemVK`가 `mShaderLoader` 콜백으로 읽으므로(`ComputeSystemVK.cpp:108`) 굽는 위치는 자유 | 2~3일 |
| 11-e | macOS: SPIR-V → MSL(`CompilerMSL`) → `Jolt.metallib`. `ComputeSystemMTL`은 라이브러리 **하나**를 통째로 읽는다(`ComputeSystemMTL.mm:43`) | 2~3일 |
| 11-f | 테스트: 컴파일된 SPIR-V가 유효하고, 바인딩 이름이 `ComputeShaderVK` 리플렉션과 맞는지 | 1일 |

**합계 대략 1.5~2주.** DXC 도입 대비 큰 절감이다.

### 11-b 관련 주의

7a 리뷰에서 지적된 전역 env 오염을 반복하지 않는다. `ENABLE_HLSL`은 **glslang 모듈 env에만** 정의한다.
전역에 두면 무관한 TU의 컴파일 시그니처가 바뀌고, `Types.h`·`ShaderLang.h`가 선언하는 내용이 달라져
ODR 위험이 생긴다 (`Types.h:102,154`가 `ENABLE_HLSL`로 멤버를 추가/제거한다).

---

## 4. 이 제안이 바꾸지 않는 것

- **7c(GPU 백엔드 배선)는 여전히 G5에 막혀 있다.** `ComputeSystemVK::Initialize()`가 전용 컴퓨트 큐
  인덱스를 요구하는데 `drivers/vulkan/rendering_device_driver_vulkan.cpp:1325`의
  `max_queue_count_per_family = 1`이 그대로다. **툴체인이 생겨도 GPU 헤어는 못 켠다.**
- 따라서 Task #11의 산출물은 **7c의 선행 조건을 하나 제거하는 것**이지 GPU 헤어를 켜는 게 아니다.
- 헤어 자체의 성숙도 캐비엇(바람 입력 부재 · LOD 부재 · ConvexHull 한정 충돌 · CPU/GPU 이중 저장)도
  그대로다.

## 5. 부수 효과 — L2·L7에도 열린다

이건 Jolt만의 이득이 아니다. HLSL 프론트엔드가 생기면 **HLSL로 배포되는 모든 상류 컴퓨트 커널을
포크에서 쓸 수 있다.** Nanite 참고 구현·업스케일러 SDK·벤더 샘플 상당수가 HLSL이다.
L2(지오메트리)·L7(VFX)이 나중에 같은 문을 쓸 가능성이 높으므로, 진입점은 Jolt 전용이 아니라
**glslang 모듈의 범용 API**로 만든다.

---

## 6. 결정 요청

1. **③(glslang HLSL 벤더링)으로 진행해도 되는가?** ①·②를 배제한 근거는 위와 같다.
2. **소유권 확인:** 7a 배정 시 "7b는 귀하(L5) 소유, CI 편입 시점에만 C3와 협의"로 정해졌다.
   그런데 ③은 **`thirdparty/glslang` + `modules/glslang`** 을 건드린다 — Jolt가 아니라 렌더링/빌드
   공용 자산이다. L5가 계속 들고 가도 되는지, 아니면 L9(C3)로 넘길지 판단이 필요하다.
   *개인적으로는 L5가 유일한 소비자인 지금 단계에선 L5가 맞고, 다른 레인이 쓰기 시작하면 넘기는 게
   맞다고 본다.*
