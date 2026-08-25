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

**측정 상태:** ⓐ 왕복 지연·ⓑ 가드 동작은 **실측 완료**(macOS/M2 Pro/MoltenVK, `misc/rd_capability_spike/`). ⓒ는 코드 구조 문제라 정적 확정. 잔여 미측정 항목은 §5.

> **⚠️ 실측은 macOS/MoltenVK 단일 플랫폼이다.** Windows/Linux 재측정 필요. 하니스를 동봉했으니 각 레인이 자기 플랫폼에서 그대로 돌릴 수 있다.

---

## ⓐ `buffer_get_data_async` — 🟢 PASS (조건부)

### 메커니즘 (코드 확인)

`buffer_get_data_async`(`rendering_device.cpp:1350`)는 복사 리전을 `frames[frame].download_buffer_*`에 적재하고 `BufferGetDataRequest`를 큐잉한 뒤 **즉시 리턴한다. 요청 시점에 펜스 대기가 없다.** ✅

콜백은 `_stall_for_frame(p_frame)`(`:8277`)에서 `driver->fence_wait(frames[p_frame].fence)`(`:8282`) **이후에** 호출된다. 이 함수는 `_begin_frame`(`:8111`)이 `:8114`에서 부른다.

### 📏 왕복 지연 — 실측 (⚠️ 코드 분석 예측이 틀렸다)

이 리포트 초안은 메커니즘만 보고 *"왕복 지연 = `frames.size()`"* 라고 적었다. **실측 결과 틀렸다.** 4개 구성 × 각 10샘플, 정상상태(60프레임 워밍업 후) 측정, **편차 0**:

| `thread_model` | `frame_queue_size` | 왕복 프레임 |
|:---:|:---:|:---:|
| 1 (단일) | 2 (기본) | **1** |
| 1 (단일) | 3 | **2** |
| 2 (멀티스레드) | 2 | **2** |
| 2 (멀티스레드) | 3 | **3** |

⇒ **단일 스레드 = `frame_queue_size - 1` · 멀티스레드 = `frame_queue_size`**

멀티스레드의 +1은 메인 스레드가 렌더 스레드보다 한 프레임 앞서는 파이프라이닝 오프셋이다.

**설계에 쓸 숫자는 멀티스레드 쪽이다.** 기본값(`thread_model=1`, `fq=2`)에서는 1프레임이지만 AAA 타이틀은 거의 확실히 스레드 렌더링을 켜므로 **2~3프레임을 전제로 팝인 예산을 잡아야 한다.** 1프레임 기준으로 설계하면 안 된다.

관련 설정: `rendering/rendering_device/vsync/frame_queue_size`(`core/config/project_settings.cpp:1897`, 기본 2·범위 2~3), `rendering/driver/threads/thread_model`.

해당 프레임 슬롯 재사용 시점엔 펜스가 이미 시그널된 상태라 `fence_wait`는 즉시 반환한다. **정상 경로에 추가 스톨 없음** — 아래 스테이징 고갈 경로만 예외다.

### ⚠️ 스톨하는 경로 — 이게 진짜 설계 제약이다

`_staging_buffer_allocate`(`:931`)에서 **다운로드 스테이징 블록이 전부 현재 프레임에 점유되고 풀을 더 늘릴 수 없으면** `STAGING_REQUIRED_ACTION_FLUSH_AND_STALL_ALL`을 반환한다(`:984`).
이를 `_staging_buffer_execute_required_action`(`:1022`)이 받아 **`_flush_and_stall_for_all_frames()` = 전면 파이프라인 스톨**을 실행한다. `buffer_get_data_async`는 `:1381`에서 이 경로를 탄다.

**예산 (실측):**

| 항목 | 값 | 출처 |
|---|---|---|
| `block_size` | `staging_buffer/block_size_kb` **기본 256 KB** | `project_settings.cpp:1899` |
| `max_size` | `staging_buffer/max_size_mb` **기본 128 MB** | `project_settings.cpp:1900` |
| 다운로드 풀 설정 | **업로드 값을 그대로 복사** | `rendering_device.cpp:8612-8613` |

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

대형 버퍼를 매 프레임 재할당하며 리드백을 계속 거는 램프 중 **SIGABRT가 재현**됐다. 스택은 `buffer_get_data_async` 호출 지점에서 MoltenVK `SPIRVToMSLConverter::convert`로 들어간다.

> **첫 가설은 검증해서 기각했다.** "in-flight 리드백이 걸린 버퍼를 `free_rid`하면 터진다"고 의심했다 — `free_rid`(`:7672`) → `_free_internal` 경로에 `download_buffer_get_data_requests`를 확인하는 코드가 없기 때문이다. **최소 재현기로 직접 시험한 결과 정상 동작했다**: 같은 프레임에 async 리드백을 걸고 즉시 `free_rid`해도 콜백이 올바른 크기로 정상 발화하고 25프레임을 더 살아남았다. **이 가설은 틀렸다.**
>
> 실제 원인은 미규명이다. 지속적 대용량 프레임당 리드백이라는 조건이 스톨 경로와 인접하므로 **스테이징 고갈 경로 자체의 결함 가능성**이 남아 있으나, 근거 없이 주장하지 않는다. 크래시를 유발한 램프 프로브는 **하니스에서 제거**했다(노이즈 데이터 + 크래시 이중 결함). 재현 조건만 여기 기록한다: 256 KB→32 MB 램프, 스텝당 12프레임, 매 프레임 전체 버퍼 리드백.

⇒ **다운로드 전용 노브가 없다.** VT 피드백 리드백 예산과 텍스처/메시 업로드 예산이 **같은 프로젝트 설정 하나**를 공유한다(풀 자체는 분리되어 있으나 크기 설정이 공유). 스트리밍이 무거운 프레임에 피드백 리드백이 겹치면 양쪽이 같은 노브를 놓고 경쟁한다.

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

**그리고 `call_on_render_thread` 마셜링 지연은 4개 구성 모두 0프레임이었다** — 밀어 넣은 콜러블이 *같은 프레임의 렌더 중에* 실행된다. **우회 경로에 프레임 비용이 붙지 않는다는 뜻으로, 이 스파이크에서 가장 반가운 결과다.**

### 승인된 우회 경로 두 가지

**① `RenderingServer.call_on_render_thread(Callable)` — 이게 정답이다**
- ClassDB 바인딩 확인: `servers/rendering/rendering_server.cpp:3586`
- 구현(`rendering_server_default.h:1223-1230`): 호출자가 서버 스레드면 즉시 실행, 아니면 `command_queue.push`로 렌더 스레드에 마셜링.
- ⇒ 워커 스레드는 **RD를 직접 부르지 않고** 이 API로 작업을 렌더 스레드에 밀어 넣는다. 엔진 자신도 이 패턴을 쓴다(`xr_server.cpp:248,256,264`, `renderer_scene_cull.cpp:3009`).

**② `RenderingDevice.create_local_device()` — 용도가 다르다**
- ClassDB 바인딩: `rendering_device.cpp:9275`. 구현 `:9089`.
- `RenderingDevice()` 생성자(`:9942-9948`)가 `render_thread_id = Thread::get_caller_id()`를 찍는다 → **워커 스레드에서 생성하면 그 스레드가 해당 로컬 RD의 "렌더 스레드"가 되어 모든 가드를 통과한다.**
- `submit()`/`sync()`(`:9267-9268`)로 구동하며, `sync()` → `_begin_frame(true)` → `_stall_for_frame` 경로로 async 콜백이 **그 워커 스레드에서** 디스패치된다.

**⚠️ 그러나 로컬 디바이스는 VT 피드백에 쓸 수 없다.**
`initialize()`가 `driver = context->driver_create()`로 **독립 드라이버/디바이스 인스턴스**를 만든다. **메인 RD와 리소스(RID·버퍼·텍스처)를 공유하지 않는다.** 즉 로컬 디바이스로 메인 렌더러의 피드백 버퍼를 읽을 수 없다. 또 서피스가 없으므로 `frame_count = 1`이다.
⇒ 로컬 디바이스의 용도는 **메인 렌더러 리소스를 건드리지 않는 독립 GPU 작업**(오프라인 트랜스코딩, DAG 빌드 보조 등)에 한정된다.

**③ `make_current()`는 스크립트에서 못 쓴다**
헤더 `:1981`에 선언되어 있고 `:9930`에서 `render_thread_id`를 재할당하지만, **ClassDB에 바인딩되어 있지 않다** → GDExtension/GDScript에서 도달 불가.

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

본 리포트는 **Vulkan 백엔드만** 조사했다. `drivers/metal/`·`drivers/d3d12/`의 동등 경로는 미조사다.
- D3D12는 descriptor heap 모델이라 bindless 사정이 다를 수 있다.
- Metal은 argument buffer 모델이다.
- 다만 **ⓒ의 RD 추상화 레벨 결론(`UniformType`에 bindless 개념 부재)은 백엔드 무관하게 성립한다.**

콘솔(W4)은 범위 밖.

---

## 5. ⚠️ 미완 — 실측이 필요한 항목

**본 리포트는 코드 정적 분석이다. GPU에서 아무것도 실행하지 않았다.** 아래는 아직 수치가 없다:

| 항목 | 필요한 측정 | 왜 코드만으론 부족한가 |
|---|---|---|
| ⓐ **스톨 임계·비용** | GPU 타임스탬프 계측 + 통제된 씬 | 프레임타임 관측은 노이즈에 묻혔다(위 표). **L9 GPU 프로파일러 P1/P2가 선행되어야 한다** |
| ⓐ·ⓑ **Windows/Linux 재측정** | 하니스를 각 플랫폼에서 실행 | 실측 전부가 macOS/MoltenVK 단일 플랫폼. 특히 왕복 법칙이 드라이버 무관인지 미확인 |
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
