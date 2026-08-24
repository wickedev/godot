# Godot 4.x 가상 텍스처링(Virtual Texturing) 구현 — 딥리서치

> 대상: `docs/godot-openworld-engine-gaps.md` §3 "가상 텍스처링 (Streaming Virtual Texturing)" 격차 해소.
> 방식: 5개 검색 각도 → 23개 소스 → 102개 주장 추출 → 상위 25개를 3표 적대적 검증(2/3 반박 시 폐기) → 종합.
> 검증 결과: **23 confirmed / 2 refuted / 0 unverified**. 각 주장은 아래 인용 소스로 뒷받침됨.
> 작성일: 2026-08-03. 이 문서는 검증된 사실(fact)과 엔지니어링 설계(design)를 명시적으로 구분한다.

---

## TL;DR (핵심 결론)

AAA 오픈월드 규모의 가상 텍스처링은 Godot 4.x에서 **"조건부 가능"**하다. 단, 어떤 변종을 고르느냐가 전부를 가른다.

1. **하드웨어 스파스 VT** (Vulkan sparse residency / D3D12 tiled resources 기반) → **코어 C++ 개조 불가피하나 bounded 작업, man-year 아님. 1급 후보.** Godot의 Vulkan 드라이버가 device 생성 시점에 `sparseBinding`·`sparseResidencyImage2D` 피처를 **의도적으로 비활성화**하므로, 포크에서 활성화 + RD API 추가 필요. clayjohn의 "Vulkan sparse textures is known to not be well performant on PC" 경고는 드라이버별 벤치마크 대상이지 아키텍처 블로커가 아님.
2. **소프트웨어 SVT** (고정 크기 물리 아틀라스 + 인디렉션 텍스처, 하드웨어 스파스 바인딩 없음) → **오늘 당장 RenderingDevice로 구축 가능.** 컴퓨트 셰이더 + 커스텀 셰이더 + **논블로킹 GPU→CPU 리드백**(`buffer_get_data_async`)을 이미 노출하고 있고, 이 리드백이 정확히 VT 피드백/분석 패스가 필요로 하는 메커니즘이다.
3. **RVT식 런타임 캐시** (UE의 Runtime Virtual Texture, 지형 머티리얼 블렌딩용) → **지형 블렌딩 병목을 직접 해소하나, AAA 유니크 텍스처 예산은 별도 기법(SW SVT 또는 HW 스파스)이 필요.** 하드웨어 스파스 바인딩이 전혀 필요 없는 런타임 GPU 셰이딩 캐시이며, 지형-오브젝트 블렌딩에 정확히 대응한다. 캐릭터 유니크·무기 스킨·건축물 유니크 텍스처는 RVT로 커버 불가.

**전략 요약**: §3 문서의 "❌ 엔진 미지원 → 불가" 결론은 **포크 전제에서 폐기.** RVT(지형), SW SVT(유니크 스트리밍), HW 스파스 VT(성능 최적화)의 3트랙 병행을 AAA에서 요구. Nanite 커밋 시 유니크 텍스처 물량이 폭증하므로, RVT만으로는 AAA 텍스처 예산을 감당할 수 없다.

---

## 1. 가상 텍스처링이란 무엇이고, 어떤 변종이 있나

### 1.1 소프트웨어/스파스 VT (Sean Barrett SVT, id MegaTexture) ✅ 검증 3-0

거대한 하나의 "가상 텍스처"를 훨씬 적은 물리 GPU 메모리로 시뮬레이션하는 기법. 3개 구성요소로 이뤄진다:

- **페이지 테이블 / 인디렉션 텍스처**: 가상 좌표 → 물리 아틀라스 좌표 변환 룩업.
- **물리 타일 캐시 (physical atlas)**: 실제로 GPU에 상주하는 타일들의 고정 크기 풀. "가상 텍스처 공간의 스파스 캐시"로 취급된다.
- **온디맨드 스트리밍**: 필요한 타일만 디스크→GPU로 로드.

프래그먼트/픽셀 셰이더가 매 샘플마다 virtual→physical 주소 변환을 수행한다. 이로써 아틀라스/타일링 이음새(seam) 없이 대규모 월드에 텍스처를 입힐 수 있다.

> 인용: "simulat[es] very large textures using much less texture memory... by downloading only the data that is needed, and using a pixel shader to map from the virtual large texture to the actual physical texture" — Sean Barrett SVT, GDC 2008.
> 소스(primary): <https://silverspaceship.com/src/svt/>, <https://archive.org/details/GDC2008Barrett>

> ⚠️ **주의(검증에서 걸러짐)**: "완전 고유(non-repeating) 텍스처링이 대규모 오픈월드의 *핵심 요구사항*"이라는 주장은 **1-2로 반박**되어 폐기됐다. 즉 유니크 텍스처링은 VT가 *가능케 하는 것*이지 오픈월드가 *반드시 요구하는 것*은 아니다. 또한 "no visual artifacts"는 이음새 제거를 뜻하며, 스트리밍 지연에 따른 팝인/블러는 여전히 발생한다.

### 1.2 하드웨어 스파스/타일드 리소스

소프트웨어 인디렉션 대신 GPU/드라이버의 **부분 메모리 바인딩** 하드웨어 기능을 쓰는 변종.

- **Vulkan sparse residency** ✅ 3-0: `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT` 이미지는 "sparse image block"이라는 직사각 단위로 메모리에 부분 바인딩되고, `vkQueueBindSparse`로 온디맨드 재바인딩된다. device 생성 시 `sparseBinding`(기본) + `sparseResidencyImage2D`(2D 싱글샘플용) 피처를 켜야 한다. **Vulkan 스펙 자체가 "megatexture fashion" 유스케이스를 명시적으로 인정**한다.
  > 소스(primary): <https://docs.vulkan.org/spec/latest/chapters/sparsemem.html>
- **D3D12 tiled(reserved) resources** ✅ 3-0: reserved 리소스는 생성 시 전부 언매핑(NULL) 상태로 시작하고, 큐 레벨 API `ID3D12CommandQueue::UpdateTileMappings`로 가상 타일 페이지 → 물리 힙 페이지를 온디맨드(순서 무관, NULL/SKIP/REUSE 플래그) 매핑한다. **Sampler Feedback**(SM 6.5 / DX12 Ultimate)은 텍스처 샘플링 위치를 기록해 어떤 밉/영역이 필요한지 감지 → 타일드 리소스 스트리밍을 구동하도록 설계됐다.
  > 소스(primary): <https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-updatetilemappings>, <https://microsoft.github.io/DirectX-Specs/d3d/SamplerFeedback.html>

Vulkan sparse와 D3D12 tiled+Sampler Feedback은 **동일한 VT 루프**(가시 타일만 메모리 백킹)를 서로 다른 API로 구현한 것이다. **둘 다 현재 Godot RenderingDevice에 노출되어 있지 않다.**

### 1.3 UE5 Streaming VT (SVT) vs Runtime VT (RVT)

Epic은 두 가지를 구분한다:

- **Streaming Virtual Texturing (SVT)**: 디스크에 사전 저작(pre-authored)된 데이터를 타일 단위로 스트리밍. §1.1의 고전 SVT에 해당.
- **Runtime Virtual Texture (RVT)** ✅ 3-0: 디스크에서 스트리밍하는 게 아니라 **런타임에 GPU로 텍셀 데이터를 온디맨드 생성**하는 별개 변종. 넓은 영역에 대한 **셰이딩 캐시**로 동작하며 매 프레임 전부 갱신되지 않는다.
  - 카메라 독립적 셰이딩 → `Runtime Virtual Texture Output` 노드로 RVT Asset에 **기록**.
  - 카메라 의존적 셰이딩 → `Runtime Virtual Texture Sample` 노드로 **판독**.
  > 인용: "A Runtime Virtual Texture (RVT) creates its texel data on demand using the GPU at runtime." / "content is effectively a shading cache." — Epic UE 4.27–5.8 docs.
  > 소스(primary): <https://dev.epicgames.com/documentation/unreal-engine/runtime-virtual-texturing-in-unreal-engine>

**Godot 지형 시스템에 가장 관련 깊은 변종은 RVT다.** 하드웨어 스파스 바인딩이 필요 없는 런타임 GPU 캐시이기 때문.

---

## 2. RVT의 정석 유스케이스 = 대규모 지형 머티리얼 블렌딩 ✅ 검증 3-0

Epic 1차 문서 기준, RVT의 **목적 자체**가 오픈월드 지형/랜드스케이프 블렌딩이다:

- 복잡한 Landscape 머티리얼이 셰이딩 결과를 RVT에 캐싱 → 성능 이득. ("Complex Landscape materials cache the shading results for a performance win.")
- 비-Landscape 액터(스플라인, 데칼)를 **같은 RVT Asset에 합성** → 지형과 매끄럽게 블렌딩. ("Blending of non-Landscape Actors with your Landscape is handled by the same RVT Asset.")
- 데칼 성격 머티리얼과 지형에 컨폼하는 스플라인에 특히 적합.

> 소스(primary): <https://dev.epicgames.com/documentation/unreal-engine/runtime-virtual-texturing-in-unreal-engine>, <https://dev.epicgames.com/documentation/en-us/unreal-engine/runtimevirtual-texturing-quick-start-in-unreal-engine>

**시사점**: 젠신급 오픈월드에서 실제로 아픈 지점은 "8K×8K 유니크 텍스처 스트리밍"보다 **지형 위에 오브젝트/경로/데칼을 이음새 없이 블렌딩**하는 것이다. RVT식 런타임 캐시가 이 병목을 직접 친다. → **1순위 타깃으로 권장.**

---

## 3. Godot 4.x 렌더링 아키텍처 — 무엇이 있고 무엇이 없나

### 3.1 RenderingDevice 추상화 ✅ 검증 3-0

Godot 4는 Vulkan / Direct3D 12 / Metal을 **단일 내부 추상화 `RenderingDevice`** 뒤에 둔다. Forward+ 및 Mobile 렌더러가 이를 사용한다. VT를 RenderingDevice 위에서 만든다는 것은 네이티브 API가 아니라 **이 추상화 레이어를 타깃**한다는 뜻이고, 이것이 코어 개조 없이 도달 가능한 GPU 기능의 상한을 결정한다.

> 인용: "RenderingDevice is an abstraction for working with modern low-level graphics APIs such as Vulkan, Direct3D 12, and Metal."
> 소스(primary): <https://docs.godotengine.org/en/stable/classes/class_renderingdevice.html>

> ⚠️ **반박됨(0-3)**: "RenderingDevice가 WebGPU 수준의 추상화라서 스파스/피드백 노출이 막힌다"는 주장은 **만장일치 반박**됐다. RenderingDevice의 추상화 수준을 WebGPU 패리티로 가정하지 말 것 — 실제 제약은 "노출 API 부재"이지 "추상화 천장" 때문이 아니다.

### 3.2 결정적 이점: 논블로킹 GPU→CPU 리드백 ✅ 검증 3-0 (in-repo 확인)

RenderingDevice는 스톨하는 `buffer_get_data()`와 함께 **논블로킹** `buffer_get_data_async(RID, Callable callback, offset, size)`를 노출한다. 이것이 **소프트웨어 VT 피드백 루프를 기존 익스텐션 표면으로 만들 수 있게 하는 가장 결정적인 사실**이다 — 매 프레임 페이지 요청 버퍼를 스톨 없이 CPU로 리드백해야 하는데, 바로 그 메커니즘이다.

> 검증(이 저장소에서 직접 확인):
> - 선언: `servers/rendering/rendering_device.h:268-269`
> - 스크립트 바인딩: `servers/rendering/rendering_device.cpp:9116-9117`
> - 비동기 구현(리드백 defer 후 후속 프레임에 콜백 호출): `rendering_device.cpp:1329-1409`, 처리 `cpp:8222-8243`

### 3.3 결정적 블로커: 스파스 API 부재 + 스파스 피처 비활성화 ✅ 검증 3-0 (in-repo 확인)

하드웨어 스파스 VT는 **순수 GDExtension으로 불가능**하며 코어 C++ 개조가 필수다:

- `RenderingDevice.xml`에 sparse/tiled/residency/virtual_texture 관련 항목이 **0건**. 모든 `texture_create*` 경로는 fully-committed 리소스만 다룬다.
- `drivers/vulkan/rendering_device_driver_vulkan.cpp`(~L818-826)가 `VkDevice` 생성 시 스파스 피처 비트(`sparseBinding`, `sparseResidencyBuffer`, `sparseResidencyImage2D/3D`)를 **명시적으로 누락** — 코드 주석상 "we don't use sparse features".
- 결과: 익스텐션에서 `get_driver_resource`로 네이티브 `VkDevice`를 꺼내와도, device 자체에 스파스 피처 플래그가 없어 스파스 리소스 생성이 실패한다.

> 소스(primary, in-repo): `doc/classes/RenderingDevice.xml`, `servers/rendering/rendering_device_driver.h`, `drivers/vulkan/rendering_device_driver_vulkan.cpp`
> 검증: <https://github.com/godotengine/godot/blob/master/drivers/vulkan/rendering_device_driver_vulkan.cpp>

**→ 이것이 1차 기술 블로커다.** 하드웨어 SVT를 하려면 코어에서 (a) 스파스 device 피처 활성화, (b) tiled/sparse-binding 엔트리포인트를 RenderingDevice에 추가해야 한다. 소프트웨어 SVT(고정 물리 아틀라스 + 인디렉션 텍스처, 스파스 바인딩 없음)는 이 블로커를 우회한다.

### 3.4 시한성 주의: 4.4 스레드 가드 ⚠️ 검증 3-0 (단, 진행 중 이슈)

Godot 4.4 개발 빌드가 `_THREAD_SAFE_METHOD_` 스레드 가드를 추가해, **백그라운드 스레드에서 RenderingDevice 텍스처 함수 호출을 차단**한다 — 4.3에서는 동작하던 오프-메인-스레드 GPU→CPU 텍스처/이미지 리드백 포함. 비동기 스트리밍/리드백 설계를 복잡하게 만든다.

- 이슈 #99750: "RenderingDevice - texture functions are no longer allowed in background threads due to thread guards." 리포터의 async viewport→Image 리드백이 4.3 stable에선 안정 동작했으나 4.4에서 깨짐.
- **단, 메인테이너는 이를 "회귀(regression)"가 아니라 "원래 unsafe했는데 우연히 동작하던 것"으로 규정**하며 전용 신규 함수를 선호. 해결 방향이 유동적이다.

> 소스(primary): <https://github.com/godotengine/godot/issues/99750>, `servers/rendering/rendering_device.cpp`
> **⚠️ 백그라운드 스레드 스트리밍 아키텍처를 확정하기 전에 반드시 현재 4.4/4.5 master 상태를 재확인할 것.**

---

## 4. 필요한 GPU/API 기능 vs Godot 노출 현황

| 기능 | 네이티브 API 지원 | Godot RenderingDevice 노출 | VT에서의 역할 |
|------|------------------|---------------------------|--------------|
| **Vulkan sparse residency** | ✅ (`sparseResidencyImage2D`) | ❌ device 생성 시 비활성화 | HW 타일 캐시 (부분 바인딩) |
| **D3D12 tiled resources + Sampler Feedback** | ✅ (`UpdateTileMappings`, SM6.5) | ❌ 미노출 | HW 페이지 테이블 + 피드백 |
| **Bindless / descriptor indexing** | ✅ (`VK_EXT_descriptor_indexing`, Vulkan 1.2 코어) | ⚠️ **미확인(open question)** | 타일 아틀라스/스트리밍 디스크립터 주소화 |
| **논블로킹 GPU→CPU 리드백** | ✅ | ✅ `buffer_get_data_async` | 피드백/분석 패스 (페이지 요청) |
| **컴퓨트 셰이더** | ✅ | ✅ | 인디렉션 갱신, 페이지 분석 |
| **커스텀 셰이더 샘플링** | ✅ | ✅ | virtual→physical 주소 변환 |

### 4.1 Bindless (미해결 질문) ✅ API 검증 3-0 / Godot 노출 ❓

`VK_EXT_descriptor_indexing`(Vulkan 1.2 코어)은 디스크립터 메모리를 하나의 거대 배열로 취급해 셰이더가 인덱싱한다. update-after-bind로 GPU가 접근 중이 아니면 언제든(멀티스레드 포함) 디스크립터 갱신 가능. per-invocation 비균일 인덱스는 `nonuniformEXT` 한정자로 감싸야 한다. VT 타일 캐시/아틀라스 주소화와 스트리밍 디스크립터 갱신의 기반이 된다.

> 소스(primary): <https://docs.vulkan.org/samples/latest/samples/extensions/descriptor_indexing/README.html>

- **긍정 신호**: reduz의 GPU-driven 렌더러 설계 문서가 `uniform texture2D textures[MAX_TEXTURES];` 형태의 **bindless 텍스처 배열** 방향을 제안 — Godot이 이 방향으로 가고 있음을 시사.
  > 소스: <https://gist.github.com/reduz/c5769d0e705d8ab7ac187d63be0099b5>
- **미해결**: 검증에 살아남은 주장들은 **API 역량**은 입증했으나 **Godot이 이를 익스텐션에 노출하는지는 입증하지 못했다.** → Godot bindless 지원은 열린 질문으로 취급.

---

## 5. 참조 구현 & Godot 커뮤니티 현황

> ⚠️ **증거 공백**: 검증에 살아남은 주장 중 "Godot 전용 VT 참조 OSS 구현"을 다루는 것은 Barrett의 정석 데모 외에 없다. 질문 (5)는 대체로 미충족. 아래는 소스 수집 단계에서 발견된 포인터이며, 개별 심층 검증은 거치지 않았다(참고용).

- **Sean Barrett stb SVT 데모** (primary, 정석 참조): 퍼블릭 도메인 OpenGL 소스 + 실행파일 + 슬라이드. 소프트웨어 SVT의 레퍼런스 구현. <https://silverspaceship.com/src/svt/>
- **Godot 제안 #1834** "Add support for virtual/sparse/mega textures" (2020-11-13 개설, **여전히 오픈**): "거대 텍스처의 현재 렌더링되는 부분만 GPU에 업로드"라는 정확히 이 개념을 기술. Godot이 VT를 네이티브 지원하지 않음을 확인하는 canonical 제안. <https://github.com/godotengine/godot-proposals/issues/1834>
- **Godot 이슈 #17470** (2018-03-12, 2020-05-26 closed): 초기 sparse/virtual 텍스처 요청. 구현 없이 proposals 트래커로 이관되며 종료.
- **Godot 제안 #4371** (primary): GDExtension 실현 가능성 관련 논의 소스.
- **reduz GPU-driven 렌더러 설계 gist**: bindless 방향 + GPU-driven 렌더링. VT 인디렉션과 직접 관련. <https://gist.github.com/reduz/c5769d0e705d8ab7ac187d63be0099b5>
- **Granite/Graphine 등 VT 미들웨어 개념**: 검색 단계에서 언급됐으나 검증 통과 주장 없음 → 참고만.

---

## 6. 단계별 구현 로드맵 (엔지니어링 설계 — 검증된 사실 아님)

> **주의**: 아래 로드맵은 §1–§5의 검증된 사실 위에서 구성한 *설계 제안*이다. Godot 내부에 대해 완전 검증된 로드맵이 아니며, 각 단계 착수 전 해당 내부 구조를 실측해야 한다.

### Phase 0 — 스파이크/정찰 (리스크 해소)
1. **bindless 노출 확인**: RenderingDevice가 descriptor indexing(update-after-bind, non-uniform)을 익스텐션에 노출하는가? → 소프트웨어 VT 타일 아틀라스를 코어 개조 없이 주소화 가능한지 결정.
2. **#99750 현재 상태 확인**: post-4.4/4.5 master에서 오프-메인-스레드 텍스처/버퍼 리드백 경로가 승인됐는가? `buffer_get_data_async`가 백그라운드 스트리밍 워커에서 여전히 호출 가능한가?
3. **Forward+ 텍스처 바인딩 조사**: 머티리얼/셰이더 파이프라인에서 텍스처가 실제로 어떻게 바인딩되는가? 커스텀 RD 컴퓨트+드로우 VT 패스를 렌더러를 포크하지 않고 기존 지형 머티리얼 경로에 합성 가능한가?

### Phase 1 — RVT식 런타임 캐시 (1순위, 최저 리스크)
- 하드웨어 스파스 불필요. 기존 RenderingDevice 컴퓨트 + `buffer_get_data_async`로 프로토타입.
- 지형 머티리얼 셰이딩 결과를 고정 크기 물리 아틀라스에 캐싱 → 인디렉션 텍스처로 샘플.
- 오브젝트/데칼/스플라인을 같은 캐시에 합성해 지형 블렌딩 (§2 유스케이스).
- **가치**: 젠신급 지형-오브젝트 블렌딩 병목을 직접 해소. §3 문서의 "수동 아틀라스 관리" 대안을 상당 부분 대체.

### Phase 2 — 소프트웨어 SVT (스트리밍)
- 고정 물리 아틀라스 + 인디렉션 텍스처 + 피드백 패스(`buffer_get_data_async`로 페이지 요청 리드백) + 디스크 타일 스트리밍.
- Barrett stb 데모를 참조 알고리즘으로.
- 스파스 바인딩 없이 fully-committed 아틀라스만 사용 → §3.3 블로커 우회.
- **가치**: 초대형 유니크 텍스처의 VRAM 예산을 자동화(§3 문서의 수동 밉 스트리밍 대체).

### Phase 3 — 하드웨어 스파스 VT (코어 개조 필요, 최고 리스크)
- 코어 C++ 변경: (a) Vulkan device에서 스파스 피처 활성화, (b) `vkQueueBindSparse`/`UpdateTileMappings` 상당 엔트리포인트를 RenderingDevice에 추가, (c) Vulkan/D3D12/Metal 3개 백엔드에 걸쳐 균일하게 노출.
- Metal 백엔드(4.4부터 네이티브 RD 드라이버)의 sparse texture/heap 등가물도 설계 범위 — **현재 검증 소스 없음(증거 공백)**.
- 기존 Godot 제안 #1834에 기여하는 형태가 이상적.
- **가치**: 진짜 하드웨어 페이지 테이블 → 최대 규모/효율. 하지만 코어 유지보수·리뷰 비용 큼.

---

## 7. 1차 기술 블로커 요약

| # | 블로커 | 영향 범위 | 우회 |
|---|--------|-----------|------|
| B1 | RenderingDevice에 스파스/타일드 API 부재 + Vulkan 드라이버가 스파스 피처 비활성화 | HW SVT를 GDExtension으로 불가 | 소프트웨어 SVT / RVT 캐시로 우회, 또는 코어 개조 |
| B2 | 4.4 스레드 가드 — 백그라운드 스레드 텍스처 호출 차단 | 오프-메인-스레드 스트리밍/리드백 | 현재 master 상태 재확인, 전용 함수 대기 |
| B3 | bindless/descriptor-indexing의 익스텐션 노출 여부 미확인 | SW VT 타일 아틀라스 주소화 효율 | Phase 0에서 확인 |
| B4 | Forward+ 머티리얼 텍스처 바인딩 경로 미조사 | VT 패스를 렌더러에 합성 | Phase 0에서 조사 |
| B5 | Metal 백엔드 sparse 등가물 증거 공백 | 크로스백엔드 HW SVT | 별도 리서치 필요 |

---

## 8. §3 문서(엔진 격차)에 대한 수정 제언 (AAA 기준, 2026-08-18 개정)

기존 §3의 결론 "**❌ 불가 / 엔진 미지원**"은 지나치게 단정적이다. 포크+디스커넥트 전제 하에 코어 수정은 허용된 작업이므로, 판단 축은 "man-year급인가" 하나다. 검증 결과에 따라 다음처럼 세분화하는 것을 권장:

- **하드웨어 스파스 VT** → ⚠️ **코어 C++ 개조 불가피하나 bounded 작업**(VkDevice 피처 활성화 + RD API 추가). man-year 규모 아님. 1급 후보.
- **소프트웨어 SVT** → ⚠️ **GDExtension으로 프로토타입 가능** (RenderingDevice 컴퓨트 + async 리드백). AAA 유니크 텍스처 예산의 필수 컴포넌트.
- **RVT식 지형 블렌딩 캐시** → ⚠️ **가장 현실적, 코어 개조 없이 대부분 가능** — 그러나 AAA 유니크 텍스처(캐릭터·무기·건축물)는 커버 불가. SW SVT 또는 HW 스파스와 병행 필수.

즉 §3 문서의 "현실적 대안(수동 아틀라스 + 밉 스트리밍)"은 **최소선**이고, 그 위에 **RVT식 런타임 캐시 + SW SVT(또는 HW 스파스) 병행**이라는 AAA 스택이 필요하다. Nanite가 커밋된 이상 유니크 텍스처 물량이 폭증하므로, RVT 단독으로는 충분하지 않다.

---

## 부록 A. 검증 통계

- 검색 각도: 5 (canonical-vt-theory, engine-vt-implementations, godot-rendering-internals, gpu-api-required-features, gdextension-feasibility-practitioner)
- 수집 소스: 23 / 추출 주장: 102 / 검증: 25 → **confirmed 23, killed 2, unverified 0** → 종합 후 10개 핵심 finding.
- 검증 방식: 주장별 3표 적대적 검증(refute 우선), 2/3 반박 시 폐기.

## 부록 B. 반박되어 폐기된 주장 (오해 방지용 기록)

1. **1-2 폐기** — "VT는 대규모 환경에 *완전 고유* 텍스처를 입히게 해주며 이것이 오픈월드 텍스처링의 *핵심 요구사항*이다." → 유니크 텍스처링은 VT가 가능케 하는 것이지 오픈월드가 반드시 요구하는 것은 아님.
2. **0-3 폐기** — "RenderingDevice는 대략 WebGPU 수준 추상화라서 (스파스/피드백 등) 노출 가능 기능이 그 수준으로 제한된다." → 만장일치 반박. 제약은 "노출 API 부재"이지 추상화 천장이 아님.

## 부록 C. 미해결 질문 (다음 리서치/스파이크 대상)

1. Godot RenderingDevice가 bindless/descriptor-indexing을 GDExtension에 노출하는가?
2. #99750의 post-4.4/4.5 현재 해결 상태 — 승인된 오프-메인-스레드 리드백 경로 존재 여부.
3. Forward+ 렌더러의 머티리얼/셰이더 텍스처 바인딩 경로 — 렌더러 포크 없이 VT 패스 합성 가능한가?
4. Metal(4.4+ 네이티브 RD) 및 D3D12 백엔드에서 sparse/tiled를 균일 노출하기 위한 구체적 코어 C++ 변경. 기존 제안/PR 존재 여부.

---

## 부록 D. 주요 1차 소스

| 소스 | 유형 | 다룬 주제 |
|------|------|----------|
| <https://silverspaceship.com/src/svt/> | primary | Barrett SVT 정석 데모 |
| <https://archive.org/details/GDC2008Barrett> | primary | SVT GDC 2008 강연 |
| <https://dev.epicgames.com/documentation/unreal-engine/runtime-virtual-texturing-in-unreal-engine> | primary | UE RVT (지형 블렌딩) |
| <https://docs.godotengine.org/en/stable/classes/class_renderingdevice.html> | primary | RenderingDevice / async 리드백 |
| <https://docs.vulkan.org/spec/latest/chapters/sparsemem.html> | primary | Vulkan sparse residency |
| <https://microsoft.github.io/DirectX-Specs/d3d/SamplerFeedback.html> | primary | D3D12 Sampler Feedback |
| <https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-updatetilemappings> | primary | D3D12 tiled resources |
| <https://docs.vulkan.org/samples/latest/samples/extensions/descriptor_indexing/README.html> | primary | bindless / descriptor indexing |
| <https://github.com/godotengine/godot/issues/99750> | primary | 4.4 스레드 가드 |
| `drivers/vulkan/rendering_device_driver_vulkan.cpp` | primary(in-repo) | 스파스 피처 비활성화 |
| <https://github.com/godotengine/godot-proposals/issues/1834> | forum | Godot VT 제안(오픈) |
| <https://gist.github.com/reduz/c5769d0e705d8ab7ac187d63be0099b5> | primary | reduz bindless/GPU-driven 방향 |

---

## 2026-08-18 개정 — AAA 기준 재채점

> **전제 변경:** 포크 + 업스트림 디스커넥트 + Full-Nanite 커밋. "코어 수정 필요"는 탈락 사유가 아님. 목표는 UE5.4+ / Horizon Forbidden West / Cyberpunk 2077 급 AAA. 스타일라이즈드 축소(원신 하한)는 폐기. 판단 축은 "man-year급인가" 하나뿐.

### 뒤집힌 판정 (Delta Table)

| 항목 | 기존 판정 (2026-08-03) | AAA 재판정 (2026-08-18) | 근거 |
|------|------------------------|--------------------------|------|
| **HW 스파스 VT** | Phase 3 "코어 개조 필요, 최고 리스크, 별도 트랙, Phase 1/2로 요구 충족 시 보류 가능" | **1급 후보로 승격.** 코어 개조는 허용. 판단 축은 "man-year급인가" 하나. Vulkan sparse residency는 API 표면이 잘 정의된 bounded engineering (수주~수개월). clayjohn의 "Vulkan sparse textures is known to not be well performant on PC" 경고는 벤치마크 대상이지 아키텍처 블로커가 아님. | §3.3의 "결정적 블로커"는 더 이상 블로커가 아님 — 포크 시 코어 수정 자유. |
| **RVT 단독으로 AAA 텍스처 예산 감당** | Phase 1 "가장 현실적, 최저 리스크, 젠신급 오픈월드 병목 직접 해소" | **RVT만으로는 불충분.** RVT는 지형 블렌딩(terrain-object blending)만 커버. 캐릭터 유니크, 무기 스킨, 아머 텍스처, 건축물 유니크 텍스처는 RVT로 커버 불가. Nanite 커밋 시 유니크 텍스처 물량이 폭증하므로 RVT + SW SVT 또는 HW 스파스 VT가 병행 필수. | RVT = 셰이딩 캐시(런타임 GPU 생성). 유니크 텍스처 = 디스크→GPU 스트리밍. 별개 문제. |
| **소프트웨어 SVT** | Phase 2 "코어 개조 없음, GDExtension 범위" | **위상 유지하나 1급 요구사항으로 격상.** AAA 유니크 텍스처 예산은 SW SVT 또는 HW 스파스 없이 감당 불가. RVT의 보조가 아니라 독립 필수 컴포넌트. | Nanite 유니크 텍스처 폭증 → SW SVT는 선택이 아니라 필수. |
| **PR #113429 (텍스처 밉레벨 스트리밍)** | 미평가 (문서 작성 시점 미포함) | **업스트림 공식 우선순위로 신규 평가.** PR #113429는 텍스처 밉레벨 스트리밍을 목표. 자체 VT와 경쟁軸이 아니라 "업스트림이 제공하는 하위 인프라" 위에 VT를 얹는 전략 검토 필요. | 업스트림 공식 우선순위. 포크가 이 PR을 머지/포워드포팅하면서 자체 VT를 그 위에 구축하는 전략이 효율적. |

### RVT 단독 커버리지 한계 — 충실도 비용 명시 (AAA 기준)

| 텍스처 종류 | RVT 커버 | 필요한 추가 기법 | 비고 |
|-------------|----------|-------------------|------|
| 지형 스플랫 블렌딩 (albedo/normal/roughness) | ✅ | — | RVT의 정석 유스케이스 |
| 지형-오브젝트 이음새 블렌딩 | ✅ | — | 데칼·스플라인·경로 합성 |
| **캐릭터 유니크 텍스처** (바디/페이스) | ❌ | SW SVT or HW 스파스 VT | AAA 히어로 캐릭터는 4K~8K 유니크 텍스처 |
| **무기/아머 스킨 유니크** | ❌ | SW SVT or HW 스파스 VT | 스킨 배리에이션당 개별 텍스처 |
| **건축물 유니크 텍스처** | ❌ | SW SVT or HW 스파스 VT | AAA 오픈월드 건물·구조물 |
| **초대형 월드 유니크** (non-repeating) | ❌ | SW SVT or HW 스파스 VT | MegaTexture 유스케이스 |
| VFX/파티클 텍스처 | ❌ | 기존 텍스처 배열/아틀라스 | VT 대상 아님 |

### HW 스파스 VT 1급 후보 산정 근거

1. **"코어 개조 필요"는 더 이상 탈락 사유가 아님** — 포크+디스커넥트 전제 하에 코어 C++ 수정은 허용된 작업.
2. **API 표면은 bounded** — `VkDevice` 생성 시 `sparseBinding`·`sparseResidencyImage2D` 활성화 + `vkQueueBindSparse` 상당 엔트리포인트를 RenderingDevice에 추가 + Metal/D3D12 등가물. 신규 렌더러 아키텍처가 아니라 기존 RD 추상화에 증분 기능 추가로, man-year 규모가 아님.
3. **clayjohn 경고의 실체:** "Vulkan sparse textures is known to not be well performant on PC" — 이는 드라이버 구현 품질 문제로, AMD/NVIDIA/Intel별로 다를 수 있음. 벤치마크로 검증해야 할 엔지니어링 리스크이지, 아키텍처 블로커가 아님. UE5도 NVTT(Virtual Texture)는 HW 스파스 없이 소프트웨어 페이지 테이블로 동작 — HW 스파스는 필수가 아니라 성능 최적화.
4. **PR #113429와의 관계:** 업스트림이 텍스처 밉레벨 스트리밍을 공식 우선순위로 추진 중. 이는 SW VT의 디스크→GPU 스트리밍 파이프라인과 겹치는 영역. 포크가 이 PR을 머지/포워드포팅하면서 자체 VT를 그 위에 구축하는 전략이 효율적.

### 전략 재정렬 (AAA 기준)

| 우선순위 | 항목 | 난이도 | 성격 |
|----------|------|--------|------|
| **P0** | RVT 런타임 캐시 (지형 블렌딩) | GDExtension, 코어 불요 | 지형 병목 1차 해소 |
| **P1** | HW 스파스 VT (Vulkan sparse residency) | 코어 C++ 개조 (bounded) | 유니크 텍스처의 하드웨어 가속 스트리밍 |
| **P1** | SW SVT (유니크 텍스처 스트리밍) | GDExtension 또는 코어 | HW 스파스와 병행 or 폴백 |
| **P2** | PR #113429 머지/포워드포팅 | 코어 (업스트림 추적) | 업스트림 공식 텍스처 스트리밍 인프라 |
| **P2** | Metal/D3D12 sparse 등가물 | 코어 (Vulkan 이후) | 크로스플랫폼 |

### 인라인 수정 내역

- **TL;DR** (L12): "젠신급 오픈월드 규모" → "AAA 오픈월드 규모"로 대상 변경.
- **TL;DR** (L14): "순수 GDExtension으로 불가. 코어 C++ 개조 필수" → 1급 후보 문구 추가. "코어 개조 불가피하나 bounded 작업, man-year 아님"으로 재평가.
- **TL;DR** (L17): "젠신급 오픈월드의 지형 블렌딩이라는 실제 유스케이스에 정확히 대응" → "지형 블렌딩 병목을 직접 해소하나, AAA 유니크 텍스처 예산은 별도 기법(SW SVT 또는 HW 스파스)이 필요"로 한계 명시.
- **§8** (L203-209): §3 문서 수정 제언을 AAA 기준으로 재작성.
