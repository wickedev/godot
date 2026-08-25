# 통합 RD 능력 스파이크 리포트 (Wave 0 / Task #4)

> **레인:** 전 레인 공유 (VT S0.1~S0.3 + Lumen S0-2 + Nanite 스트리밍 전제의 합집합)
> **게이트:** G0 판정 자료 · 로드맵 §4
> **베이스라인:** `6235d6e34b` · **브랜치:** `c3/rd-capability-spike`
> **범위 제약:** RD 공개 API 4파일(`rendering_device.{h,cpp}`, `rendering_device_commons.h`, `rendering_device_driver.h`)은 **읽기 전용**으로만 접근했다(C1 독점).

---

## 0. 판정 요약

| # | 질문 | 판정 | G0에 미치는 영향 |
|:-:|------|:----:|------------------|
| **ⓐ** | `buffer_get_data_async` 왕복 무스톨 | 🟢 **PASS (조건부)** | 요청 시점 논블로킹 확인. **단 다운로드 스테이징 예산 초과 시 전면 스톨** — 프레임당 리드백 크기가 설계 제약이 된다 |
| **ⓑ** | 스레드 가드 #99750 현재 상태 | 🟡 **가드 존속 / 우회 경로 있음** | 백그라운드 워커에서 메인 RD 직접 호출은 **여전히 불가**. 승인 경로는 `call_on_render_thread` 마셜링 |
| **ⓒ** | bindless / descriptor indexing 노출 | 🔴 **FAIL** | 로드맵 G0의 "bindless 불가 → VT 축소 설계" 분기 **발동** |

**측정 상태:** ⓐ 왕복 지연·ⓑ 가드 동작 **실측 완료 — 2개 백엔드 교차 확인.** ⓒ는 코드 구조 문제라 정적 확정. 잔여 미측정은 §5.

| 실측 환경 | 백엔드 | 확인 |
|---|---|---|
| Apple M2 Pro / macOS | **Metal** | `rendering_driver_active=metal` |
| NVIDIA GB10 / Linux aarch64 | **Vulkan** | `rendering_driver_active=vulkan` |

> **⚠️ ⓐ의 수치는 3판이다.** 왕복 지연과 마셜링 지연은 **하니스 계측 결함으로 두 번 정정**됐다. 결론만 읽지 말고 §ⓐ의 경위를 함께 볼 것. 원시 로그: `misc/rd_capability_spike/measurements/`.

> ### ⚠️ 초판 정정 — 백엔드 귀속이 틀렸었다
> 초판은 macOS 측정을 **"MoltenVK/Vulkan"** 이라고 적었다. **틀렸다.** 하니스가 *프로젝트 설정*(`rendering/rendering_device/driver` = `"vulkan"`)을 출력했을 뿐, 실제 초기화된 드라이버는 **Metal**이었다. Apple Silicon에서는 설정과 무관하게 Metal로 해석된다.
>
> 하니스를 `RenderingServer.get_current_rendering_driver_name()`(**실제 활성 드라이버**)를 찍도록 고쳤고, 이후 모든 측정에 백엔드를 명시한다. **초판의 macOS 수치를 MoltenVK 근거로 인용한 곳이 있다면 전부 무효다.**
>
> 다행히 결론은 살아남았다 — Linux/Vulkan에서 재측정한 결과가 §ⓐ의 법칙을 **그대로 재현**했다. 이제 근거는 Metal·Vulkan **2개 독립 백엔드**다.

---

## ⓐ `buffer_get_data_async` — 🟢 PASS (조건부)

### 메커니즘 (코드 확인)

`buffer_get_data_async`(`rendering_device.cpp:1350`)는 복사 리전을 `frames[frame].download_buffer_*`에 적재하고 `BufferGetDataRequest`를 큐잉한 뒤 **즉시 리턴한다. 요청 시점에 펜스 대기가 없다.** ✅

콜백은 `_stall_for_frame(p_frame)`(`:8277`)에서 `driver->fence_wait(frames[p_frame].fence)`(`:8282`) **이후에** 호출된다. 이 함수는 `_begin_frame`(`:8111`)이 `:8114`에서 부른다.

### 📏 왕복 지연 — 실측 (⚠️ **2차 정정. 앞선 "법칙"은 계측 결함이었다**)

이 절은 두 번 틀렸다. 경위를 남긴다.

1. **초판:** 코드만 보고 *"왕복 = `frames.size()`"* → 실측으로 반박됨.
2. **2판:** *"단일 = `fq-1` · 렌더스레드 분리 = `fq`"* 라 적고 +1을 "파이프라이닝 오프셋"이라 **설명**함 → **하니스가 스레드를 넘나드는 카운터로 스탬프하고 있었다.** 2백엔드에서 재현됐지만, 두 백엔드가 **같은 카운터를 같은 방식으로 잘못 읽었을 뿐**이다. 재현성은 정확성을 보증하지 않는다.

**결함:** `Engine.get_frames_drawn()`은 **메인 스레드**가 `Main::iteration()`에서 증가시키는 **비원자** 카운터다(`main/main.cpp`, `core/config/engine.h:59`). async 콜백은 **렌더 스레드**에서 돈다. `thread_model=2`에서 렌더 스레드가 메인 스레드 카운터를 읽고, 두 스레드는 정확히 한 프레임 어긋나 있다.

**수정:** 하니스가 **렌더 스레드가 소유·증가시키는 자체 카운터**(`_rt_frame`)로 요청과 콜백을 스탬프한다. 두 스탬프가 한 스레드의 한 시계에서 나온다. `get_frames_drawn()`은 **오직 스큐를 드러내기 위해서만** 함께 기록한다.

#### 정정된 실측 (NVIDIA GB10 · Linux aarch64 · Vulkan · 구성당 독립 3회 × 10샘플, 편차 0)

| `thread_model` | `frame_queue_size` | **왕복(렌더스레드 시계)** | 메인↔렌더 시계 스큐 |
|:---:|:---:|:---:|:---:|
| 1 = Safe | 2 | **1** | −1 |
| 1 = Safe | 3 | **2** | −1 |
| 2 = Separate | 2 | **1** | **0** |
| 2 = Separate | 3 | **2** | **0** |

⇒ **왕복 지연 = `frame_queue_size - 1`. `thread_model`과 무관하다.**

**스큐 열이 결정적 증거다.** `thread_model=1`에서 −1, `2`에서 0 — **정확히 1 차이**. 메인 스레드 카운터로 스탬프하면 `thread_model=2`에서만 지연이 1 높게 나온다. **2판이 "파이프라이닝 오프셋"이라 설명했던 +1의 정체가 이것이다.**

#### 뒤집힌 실무 결론

2판은 *"AAA는 스레드 렌더링을 켜므로 2~3프레임을 전제하라"* 고 적었다. **틀렸다.** 스레드 모델은 리드백 지연을 **바꾸지 않는다.** 기본값(`fq=2`)에서 **1프레임**, `fq=3`일 때만 2프레임이다. 팝인 예산은 `frame_queue_size`만 보면 된다.

원시 로그·빌드 해시·실행 명령: `misc/rd_capability_spike/measurements/`.

### 📏 스톨 임계 — **측정 시도했으나 수치를 얻지 못했다 (정직한 실패)**

프레임당 리드백을 256 KB → 32 MB로 램프하며 프레임타임을 관측했다. 결과:

| 리드백 | 평균 프레임 ms | 최악 ms |
|---:|---:|---:|
| 256 KB | 12.91 | 24.87 |
| 1 MB | 6.02 | 26.07 |
| 8 MB | 16.54 | 20.21 |
| 16 MB | **5.00** | 17.26 |
| 32 MB | 7.87 | 37.50 |

**이 데이터는 쓸 수 없다.** 단조증가가 아니다 — 16 MB가 256 KB보다 *빠르게* 나온다. 즉 측정이 리드백 비용이 아니라 **노이즈(에디터 윈도우·컴포지터·할당 처닝, 스텝당 표본 9개)에 지배**되고 있다. 이 수치로 임계를 주장하면 틀린 예산을 동결하게 된다.

**얻은 것은 하나뿐이고, 그건 유효하다:**
> **32 MB/프레임까지 램프하는 동안 전면 스톨로 볼 만한 사건이 관측되지 않았다** (이 구성의 풀 상한은 128 MB). VT 피드백의 현실 크기(1/8 해상도 R32_UINT — 1080p ≈ 0.25 MB, 4K ≈ 1 MB)는 여기서 **2~3자릿수 아래**다.

⇒ **실용적 결론:** VT/Nanite 피드백 규모에서 스테이징 스톨은 걱정거리가 아니다. 스톨은 코드상 **풀(`max_size`, 기본 128 MB) 고갈 시**에만 발생하며, 그 수준의 프레임당 리드백은 애초에 설계 실패다.

**제대로 측정하려면** GPU 타임스탬프 기반 계측(L9 GPU 프로파일러 P1/P2 산출물)과 통제된 씬이 필요하다. **프로파일러가 서면 재측정한다** — 그때까지 이 항목은 미확정으로 둔다.

#### 부수: 램프 도중 재현되는 크래시 1건 (미해결, 후속 과제)

대형 버퍼를 매 프레임 재할당하며 리드백을 계속 거는 램프 중 **SIGABRT가 재현**됐다(macOS, **Metal** 활성).

> ⚠️ 당시 백트레이스에 MoltenVK `SPIRVToMSLConverter::convert` 프레임이 보였고 초판은 이를 MoltenVK 문제로 적었다. **그 귀속은 철회한다** — 실제 활성 드라이버는 Metal이었으므로(§0 정정) 그 프레임의 의미를 신뢰할 수 없다. **원인 미규명으로 되돌린다.**

> **첫 가설은 검증해서 기각했다.** "in-flight 리드백이 걸린 버퍼를 `free_rid`하면 터진다"고 의심했다 — `free_rid`(`:7672`) → `_free_internal` 경로에 `download_buffer_get_data_requests`를 확인하는 코드가 없기 때문이다. **최소 재현기로 직접 시험한 결과 정상 동작했다**: 같은 프레임에 async 리드백을 걸고 즉시 `free_rid`해도 콜백이 올바른 크기로 정상 발화하고 25프레임을 더 살아남았다. **이 가설은 틀렸다.**
>
> 실제 원인은 미규명이다. 지속적 대용량 프레임당 리드백이라는 조건이 스톨 경로와 인접하므로 **스테이징 고갈 경로 자체의 결함 가능성**이 남아 있으나, 근거 없이 주장하지 않는다. 크래시를 유발한 램프 프로브는 **하니스에서 제거**했다(노이즈 데이터 + 크래시 이중 결함). 재현 조건만 여기 기록한다: 256 KB→32 MB 램프, 스텝당 12프레임, 매 프레임 전체 버퍼 리드백.

⇒ **다운로드 전용 노브가 없다.**

정확히 말하면: **업로드 풀과 다운로드 풀은 물리적으로 별개**다(`upload_staging_buffers` / `download_staging_buffers`). 용량을 놓고 서로 경쟁하지 **않는다.** 결합돼 있는 것은 **설정값 두 개뿐**이다 — `download_staging_buffers.{block_size,max_size}`가 업로드 쪽 값을 그대로 복사받는다(`rendering_device.cpp:8612-8613`).

**실무상 문제는 튜닝 불가다.** VT 피드백은 작고 잦은 리드백을, 텍스처/메시 스트리밍은 크고 드문 업로드를 원하는데 **둘의 블록/풀 크기를 따로 잡을 수 없다.** 한쪽에 맞추면 다른 쪽이 어긋난다.

> **`max_size`는 프레임당 예산이 아니다.** 풀 **전체 상한**이고, 블록은 `frame_used <= frames_drawn - frames.size()`(`:993`)일 때만 재활용되므로 **in-flight 프레임들이 합산 점유**한다. 프레임당 실효 여유는 대략 `max_size / frames.size()`다. 128 MB를 "프레임당 128 MB"로 읽으면 안 된다.

### 추가 제약
- **콜백은 렌더 스레드에서 실행된다**(`_stall_for_frame` 문맥). 콜백 안에서 무거운 페이지 분석을 하면 그대로 렌더 스레드를 막는다. → 콜백은 **버퍼 복사 + 워커 큐잉만** 하고 끝내야 한다.
- `texture_get_data_async`(`:2803`)도 동일 구조·동일 스톨 경로(`:2866`).

### 각 레인에 주는 설계 제약
1. 프레임당 async 리드백 총량을 **다운로드 스테이징 예산 아래로** 유지할 것. VT 피드백 버퍼는 풀 해상도가 아니라 **축소 해상도 + 압축 포맷**이어야 한다.
2. 왕복 2프레임을 **설계 전제로 수용**할 것 (runtime-misc §4가 지적한 "N프레임 지연 → 팝인" 그대로다).
3. 콜백에서 즉시 반환할 것.

---

## ⓑ 스레드 가드 #99750 — 🟡 가드 존속 / 승인 우회 경로 존재

### 가드는 그대로다

```cpp
// rendering_device.cpp:56-57
#define ERR_RENDER_THREAD_GUARD()      ERR_FAIL_COND_MSG(render_thread_id != Thread::get_caller_id(), ERR_RENDER_THREAD_MSG);
#define ERR_RENDER_THREAD_GUARD_V(m_ret) ERR_FAIL_COND_V_MSG(render_thread_id != Thread::get_caller_id(), (m_ret), ERR_RENDER_THREAD_MSG);
```

스트리밍이 필요로 하는 함수 전부가 가드 하에 있다:

| 함수 | 가드 위치 |
|---|---|
| `buffer_get_data_async` | `:1351` |
| `texture_get_data_async` | `:2804` |
| `buffer_update` | `:1182` |
| `texture_update` | `:2263` |
| `buffer_get_data` | `:1304` |
| `buffer_get_device_address` | `:1434` |

⇒ **VT 문서 §S0.2의 질문 "백그라운드 스트리밍 워커에서 여전히 호출 가능한가?"에 대한 답은 "아니오"다.** #99750의 제약은 post-4.4/4.5 마스터에서도 해소되지 않았다.

### 📏 실측 확인

워커 스레드에서 `buffer_get_data_async`를 직접 호출하면:
```
ERROR: This function (buffer_get_data_async) can only be called from the render thread.
   at: buffer_get_data_async (servers/rendering/rendering_device.cpp:1351)
→ 반환 err=2 (ERR_UNAVAILABLE)
```
**4개 구성(단일/멀티 × fq 2/3) 전부 동일.** 가드는 살아 있다.

### 📏 `call_on_render_thread` — **수치를 철회한다 (2차 정정)**

2판은 *"8회 측정 전부 0프레임 — 마셜링에 프레임 비용이 없다"*, 3판은 *"단일 0 / 분리 1프레임 — 공짜가 아니다"* 라고 적었다. **둘 다 무효다.**

**결함:** push 시점 스탬프를 `_advance()`에서 찍었는데 그건 **메인 스레드**이고, 거기서 **렌더 스레드 소유 `_rt_frame`을 읽고 있었다.** A1에서 찾아 고친 바로 그 버그를 **같은 파일의 B1에 그대로 남겨뒀다.** 두 스레드가 한 프레임 어긋나 있으므로 "분리 모델의 1프레임"은 **시계가 손을 바꾼 값**이지 지연이 아니다.

**왜 다시 재지 않는가:** 메인→렌더 지연은 **정의상 두 스레드에 걸쳐 있어 단일 시계로 측정할 수 없다.** 없는 방법으로 숫자를 만드느니 **숫자를 내지 않는다.**

#### 대신 측정한 것 (단일 시계, 4구성 전부)

`_process`에서 연속으로 enqueue한 두 콜러블이 **같은 렌더 프레임에 소화되는가** — 양쪽 스탬프를 **렌더 스레드에서** 찍는다:

| `thread_model` | `fq` | 같은 렌더 프레임? |
|:---:|:---:|:---:|
| 1 = Safe | 2 / 3 | **true** (delta 0) |
| 2 = Separate | 2 / 3 | **true** (delta 0) |

⇒ **`_process`에서 밀어 넣은 것들은 배치되어 한 프레임 안에 소화된다.** 이건 커맨드 큐의 순서·배치 속성이고 **지연 측정이 아니다.**

#### 측정하지 않은 것 (설계에 쓰지 말 것)
- **워커 스레드에서 enqueue한 경우의 지연** — VT/Nanite 스트리밍이 실제로 쓸 경로다. **미측정.**
- 프레임 후반·물리 틱 enqueue — 미측정.

**⇒ "마셜링 비용"에 대해 이 리포트는 아무 수치도 제공하지 않는다.** 이전 두 판의 수치를 인용한 곳이 있으면 전부 무효다.

### 각 레인에 주는 설계 제약
- **VT 피드백 · Nanite 페이지 요청 리드백은 반드시 `call_on_render_thread` 경유**로 설계할 것. "워커 스레드에서 직접 `buffer_get_data_async`"는 성립하지 않는다.
- 로컬 디바이스는 **별도 리소스 공간**임을 전제할 것.

---

## ⓒ bindless / descriptor indexing — 🔴 FAIL

### 디바이스 생성에서 활성화되지 않는다 (결정적 근거)

`RenderingDeviceDriverVulkan::_initialize_device`(`drivers/vulkan/rendering_device_driver_vulkan.cpp:1347`)가 `VkDeviceCreateInfo.pNext`에 체인하는 것:

```cpp
// :1467-1489
VkPhysicalDeviceVulkan11Features vulkan_1_1_features = {};
const bool enable_1_2_features = physical_device_properties.apiVersion >= VK_API_VERSION_1_2;
if (enable_1_2_features) {
    // In Vulkan 1.2 and newer we use a newer struct to enable various features.
    vulkan_1_1_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;   // ← 1_1
    ...
    create_info_next = &vulkan_1_1_features;
}
```

⇒ **`VkPhysicalDeviceVulkan12Features`는 디바이스 생성 체인에 한 번도 들어가지 않는다.** (주석은 "newer struct"라 말하지만 실제로 쓰는 건 `Vulkan11Features`다.)
따라서 Vulkan 1.2 코어로 승격된 descriptor-indexing 비트(`descriptorIndexing`, `shaderSampledImageArrayNonUniformIndexing`, `runtimeDescriptorArray`, `descriptorBindingPartiallyBound`, `descriptorBindingVariableDescriptorCount`, update-after-bind 계열)는 **전부 비활성 상태로 남는다.**

기본 `VkPhysicalDeviceFeatures`에서 활성화되는 배열 인덱싱은 **Dynamic 계열뿐이다**(`:866-869`):
```
shaderUniformBufferArrayDynamicIndexing / shaderSampledImageArrayDynamicIndexing
shaderStorageBufferArrayDynamicIndexing / shaderStorageImageArrayDynamicIndexing
```

### RD 추상화 자체에 bindless 유니폼 타입이 없다

`rendering_device_commons.h`의 `UniformType` 열거에는 `SAMPLER` ~ `ACCELERATION_STRUCTURE`까지만 있고 **variable-count / partially-bound / update-after-bind 개념이 존재하지 않는다.** 백엔드가 지원하더라도 **표현할 수단이 없다.**

⇒ **GDExtension 노출 여부를 따질 단계 이전에, 코어에서 기능 자체가 꺼져 있다.**

### 🔎 관찰된 불일치 (기록용)
쿼리 경로(`:908`, `:925-929`)는 `VkPhysicalDeviceVulkan12Features`를 채우고, macOS에서는 `shaderSampledImageArrayNonUniformIndexing`이 없으면 **하드 실패**시킨다(`:1027`):
> `"Your GPU doesn't support shaderSampledImageArrayNonUniformIndexing which is required to use the Vulkan-based renderers in Godot."`

**즉 Godot은 이 능력을 macOS에서 요구하면서 정작 디바이스 생성에서 켜지 않는다.** 어서션이 잔재인지, MoltenVK가 암묵적으로 켜주는지는 미확인이다. 포크에서 Vulkan12Features를 체인할 때 이 어서션 의미를 함께 정리할 것.

---

## 2. 선행 리서치 문서 정정 2건

### (1) nanite-perf §③ — **"v1엔 게이팅 아님" 결론의 전제가 성립하지 않는다** ⚠️

해당 절은 이렇게 적는다:
> bindless가 필요한 건 **머티리얼 텍스처 팬아웃**뿐이고, **vis-buffer + 고정 샘플러 배열 nonuniform 인덱싱**이 정석 회피로다. 따라서 **v1엔 게이팅 아님**, 중기 과제.

**회피로 자체가 지금 동작하지 않는다.** 구분이 필요하다:

| | 필요 기능 | 현재 상태 |
|---|---|---|
| **Dynamic** indexing | `shaderSampledImageArrayDynamicIndexing` (VK 1.0 코어) | ✅ 활성 (`:867`) — 단 인덱스가 **dynamically uniform**해야 함 |
| **NonUniform** indexing | `shaderSampledImageArrayNonUniformIndexing` (VK 1.2) + `nonuniformEXT` | ❌ **비활성** |

vis-buffer 리졸브에서 머티리얼 인덱스는 **픽셀마다 다르다 = 정의상 non-uniform**이다. 따라서 제안된 회피로는 **VK 1.2 기능 활성화라는 코어 변경을 여전히 요구한다.**

**다만 규모는 다르다.** 문서의 "2~4 man-month"는 *풀 bindless*(UAB 디스크립터 풀 + RD 신규 유니폼 타입 + 셰이더 컨테이너 리플렉션) 견적이다. **회피로만 살리는 최소 변경**은 그보다 훨씬 작다:
> `Vulkan12Features` 체인 + `shaderSampledImageArrayNonUniformIndexing` 비트 활성 + 셰이더에 `nonuniformEXT` 허용
고정 크기 배열은 기존 `Uniform`이 이미 복수 RID를 담을 수 있어 추가 추상화가 필요 없다.

⇒ **L1/L2에 제안:** 풀 bindless를 뒤로 미루더라도 **이 최소 변경은 Wave 0~1에 당겨두는 것이 이득이다.** 코어 훅 4건과 성격·규모가 같다.

### (2) nanite-impl §"Bindless / descriptor indexing" — 근거 grep이 너무 좁다
> `bindless`/`descriptor_indexing`/`runtimeDescriptorArray` grep 0 matches — 코어에도 없음

해당 토큰 3개의 grep 0은 **재현된다**(비-thirdparty 기준 0매치). 그러나 `shaderSampledImageArrayNonUniformIndexing`은 `:1027`에 **실재한다**. **결론(부재)은 옳지만 근거가 불완전했다.** 정확한 근거는 grep이 아니라 **디바이스 생성 체인에 `Vulkan12Features`가 없다는 사실**이다.

---

## 3. G0 게이트 권고

| 로드맵 G0 조항 | 본 스파이크 결과 |
|---|---|
| "RD 스파이크 3건 판정" | ⓐ🟢 ⓑ🟡 ⓒ🔴 — **판정 완료(정적 분석 기준)** |
| "bindless 불가 → VT는 단일 아틀라스+UV 인디렉션으로 축소 설계" | **분기 발동.** ⓒ가 FAIL이므로 VT는 축소 설계로 확정 |

**추가 권고**
1. **VT/Nanite 피드백 경로를 `call_on_render_thread` 전제로 재설계**할 것 (ⓑ). 워커 직접 호출 전제로 그린 설계는 지금 고쳐야 한다.
2. **프레임당 리드백 예산을 명시적 수치로 고정**할 것 (ⓐ). 다운로드 스테이징 전용 프로젝트 설정 분리를 L1 코어 훅 후보로 제안한다(현재 업로드와 공유, `:8612-8613`, 2줄).
3. **nonuniform indexing 최소 활성화를 Wave 0~1로 당길 것** (§2-1).

---

## 4. 백엔드 범위 한정

**ⓒ의 코드 조사는 Vulkan 백엔드에 한정된다.** `drivers/metal/`·`drivers/d3d12/`의 동등 경로는 **미조사이며, "미지원"이 아니라 "확인하지 않음"이다** — 이 둘을 혼동하면 안 된다.
- D3D12는 descriptor heap 모델이라 bindless 사정이 다를 수 있다(SM6.6 ResourceDescriptorHeap 등).
- Metal은 argument buffer 모델이다.
- ⓐ·ⓑ는 Metal에서도 실측했다(위 표) — 코드 조사만 Vulkan 한정이라는 뜻이다.
- 다만 **ⓒ의 RD 추상화 레벨 결론(`UniformType`에 bindless 개념 부재)은 백엔드 무관하게 성립한다.**

콘솔(W4)은 범위 밖.

---

## 5. ⚠️ 미완 — 실측이 필요한 항목

ⓐ 왕복 지연과 ⓑ 가드는 **실기 측정됐다**(마셜링 지연은 §ⓐ대로 **철회**)(§ⓐ·§ⓑ, 원시 로그 `misc/rd_capability_spike/measurements/`). 아래는 **여전히 수치가 없는 항목**이다:

| 항목 | 필요한 측정 | 왜 코드만으론 부족한가 |
|---|---|---|
| ⓐ **스톨 임계·비용** | GPU 타임스탬프 계측 + 통제된 씬 | 프레임타임 관측은 노이즈에 묻혔다(위 표). **L9 GPU 프로파일러 P1/P2가 선행되어야 한다** |
| ⓐ·ⓑ **Windows / AMD / Intel / 모바일 재측정** | 하니스를 각 플랫폼에서 실행 | 지금까지 Metal(Apple M2 Pro)·Vulkan(NVIDIA GB10) 두 조합뿐이다. 왕복 = `fq-1`이 드라이버·벤더 무관인지는 미확인 |
| ⓐ **마셜링 지연 전반** | 워커 스레드·물리 틱·프레임 후반 enqueue | **단일 시계로 측정 불가.** 메인→렌더는 정의상 두 스레드에 걸친다. 별도 방법론이 필요하고 이 리포트는 수치를 내지 않는다 |
| `thread_model=2` 종료 시 SIGABRT | 원인 규명 | 전 프로브 완료 후 엔진 종료 중 발생. `RenderingDevice::finalize`가 자기 렌더스레드 가드에 걸린다(`rendering_device.cpp:8925`). 하니스 문제인지 엔진 문제인지 **미판정** |
| ⓒ macOS 어서션 실체 | MoltenVK에서 nonuniform indexing이 실제 동작하는지 | `:1027` 불일치의 해석이 갈림 |

**하니스:** `misc/rd_capability_spike/` (GDScript, 실행 1줄). 다른 레인이 자기 플랫폼에서 그대로 돌려 `RESULT` 라인을 회신하면 위 표가 채워진다.

```
bin/godot.<platform>.editor.<arch> --path misc/rd_capability_spike
```

**G0 판정에 쓸 때의 신뢰도 구분:**
- **ⓑ·ⓒ — 확정.** 코드 구조 문제이며 ⓑ는 실측 확인까지 됐다.
- **ⓐ 왕복 지연 — 확정(단 macOS/MoltenVK).** 4구성 × 10샘플 편차 0.
- **ⓐ 스톨 임계 — 미확정.** 상한만 안다("32 MB까지 사건 없음"). 설계 예산으로 쓰기엔 충분하나 정밀한 수치는 아니다.

---

*작성: 2026-08-25 · L9 / godot-contributer-3 · 베이스라인 `6235d6e34b`*
