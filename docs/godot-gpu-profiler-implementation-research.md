# Godot 4.x 고급 GPU 프로파일러 구현 — 딥리서치

> 대상: `docs/godot-openworld-engine-gaps.md` §5 "고급 GPU 프로파일링" 격차 해소.
> 방식: (1) Godot master(4.8-dev) 소스트리 **실측**(file:line) + (2) 외부 기술 **딥리서치**(6각도 → 32소스 → 147주장 추출 → 상위 25개 3표 적대적 검증, 2/3 반박 시 폐기).
> 외부 검증 결과: **24 confirmed / 1 refuted / 0 unverified** (전부 3-0 만장일치). API 레벨 사실은 Khronos/Microsoft/Apple 1차 문서로 뒷받침.
> 작성일: 2026-08-03. 이 문서는 **검증된 사실(fact)**, **엔진 실측(source)**, **설계 권고(design)**를 명시적으로 구분한다.

---

## TL;DR (핵심 결론)

**§5의 "⚠️ 부분 — RenderDoc/Tracy + 커스텀 계측"은 과소평가다.** Godot에는 이미 **작동하는 GPU 타임스탬프 프로파일러가 end-to-end로 존재**하며(RenderingDevice API → 렌더 그래프 → Vulkan/D3D12 드라이버 → RenderingServer frame profile → 에디터 Visual Profiler / 콘솔), "Unreal Insights급 고급 프로파일러"는 **바닥부터 새로 만드는 게 아니라 이 토대를 확장**하는 문제다.

핵심 판단 3가지:

1. **토대는 이미 있다 (재사용).** `RenderingDevice::capture_timestamp()` + `RenderingServer::get_frame_profile()` + `EditorVisualProfiler`가 GPU 구간별 시간을 실측해 그래프로 보여준다. Vulkan은 `vkCmdWriteTimestamp` + `VkQueryPool`, D3D12는 `ID3D12QueryHeap`로 완전 구현됨.

2. **막힌 구멍이 명확하다 (실측 확정).** ① **Metal은 GPU 타이밍이 전무**(전부 stub, 결과 0), GLES3도 stub. ② **계층(hierarchy) 없음** — 현재는 flat한 이름표 목록이라 "패스 안의 서브패스" 드릴다운이 불가. ③ **CPU↔GPU 캘리브레이션 없음** — GPU 기간만 재고 CPU 타임라인과 같은 축에 못 놓음(`VK_EXT_calibrated_timestamps` 미사용). ④ **debug label이 `DEV_ENABLED`/verbose에 게이팅**됨. ⑤ **Tracy는 CPU zone만 연동**, `TracyVkZone`(GPU) 미연동. ⑥ **pipeline statistics 미노출**.

3. **고급 프로파일러 = 순수 GDExtension 불가, 그러나 코어 수정은 국소적.** 타임스탬프/캘리브레이션/pipeline stats는 전부 드라이버 레벨 API라 **코어 C++ 개조 필요**(GDExtension으로 `RenderingDevice` 내부 쿼리 풀에 접근 불가). 단 개조 범위는 각 드라이버의 `rendering_device_driver_*.cpp` 국소 함수와 `RenderingServer` 집계 계층으로 한정되며, Nanite/Lumen 같은 man-year급이 아니라 **수 주~수 개월급**이다.

**전략 요약**: §5는 "부분 우회 가능(RenderDoc/Tracy 외부 의존)"에서 **"코어 확장으로 통합 인엔진 프로파일러 실현 가능 — 3단계 비용"**으로 수정되어야 한다.

---

## 0. 지금 Godot에 뭐가 이미 있나 (엔진 실측, file:line)

> 모든 경로는 `/Users/ryan/Workspace/godot` 기준. 아래는 로컬 master 소스 직접 확인.

### 0.1 GPU 타임스탬프 파이프라인 — **완전 작동 (Vulkan/D3D12)**

공개 `RenderingDevice` API:
- `servers/rendering/rendering_device.h:1908-1913` — `capture_timestamp`, `get_captured_timestamps_count`, `get_captured_timestamp_gpu_time`, `get_captured_timestamp_cpu_time`, `get_captured_timestamp_name` 선언.
- `servers/rendering/rendering_device.cpp:8698` — `capture_timestamp()` 구현. draw/compute/raytracing list 중간 호출 방지, `max_timestamp_query_elements` 초과 방지. CPU 시각은 `OS::get_ticks_usec()`, GPU 쿼리는 렌더 그래프로 큐잉:
  ```cpp
  draw_graph.add_capture_timestamp(frames[frame].timestamp_pool, frames[frame].timestamp_count);
  frames[frame].timestamp_cpu_values[...] = OS::get_singleton()->get_ticks_usec();
  ```
- `rendering_device.cpp:8839` `get_captured_timestamp_gpu_time` → `driver->timestamp_query_result_to_time(...)`; `:8845` cpu_time; `:8851` name.
- 렌더 그래프 계층: `servers/rendering/rendering_device_graph.cpp:2575` `add_capture_timestamp(...)` — 커맨드 재정렬을 견디도록 그래프 커맨드로 기록.

드라이버 추상 인터페이스 (`servers/rendering/rendering_device_driver.h:826-833`):
```cpp
virtual QueryPoolID timestamp_query_pool_create(uint32_t p_query_count) = 0;
virtual void timestamp_query_pool_get_results(QueryPoolID, uint32_t, uint64_t *r_results) = 0;
virtual uint64_t timestamp_query_result_to_time(uint64_t p_result) = 0;
virtual void command_timestamp_query_pool_reset(CommandBufferID, QueryPoolID, uint32_t) = 0;
virtual void command_timestamp_write(CommandBufferID, QueryPoolID, uint32_t p_index) = 0;
```

**Vulkan** (`drivers/vulkan/rendering_device_driver_vulkan.cpp`):
- `:6755` `timestamp_query_pool_create` → `vkCreateQueryPool(VK_QUERY_TYPE_TIMESTAMP)`.
- `:6770` `..._get_results` → `vkGetQueryPoolResults(... VK_QUERY_RESULT_64_BIT)`.
- `:6774`/`:6801` `..._result_to_time` → `timestampPeriod`로 128비트 고정소수 곱(`mult64to128`). NVIDIA가 period 1.0으로 큰 카운트를 준다는 주석 존재.
- `:6813` `command_timestamp_write` → `vkCmdWriteTimestamp(..., VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, ...)`.

**D3D12** (`drivers/d3d12/rendering_device_driver_d3d12.cpp`):
- `:5576` `CreateQueryHeap` + `D3D12MA` 리드백 버퍼. `:5638` `command_timestamp_write` → `EndQuery(D3D12_QUERY_TYPE_TIMESTAMP)` + `ResolveQueryData`. `:6298` `GetTimestampFrequency`.
- ⚠️ `:5635` `command_timestamp_query_pool_reset`는 **no-op**(D3D12는 리셋 불필요).

**Metal** (`drivers/metal/rendering_device_driver_metal.cpp`) — ❌ **타임스탬프 미지원**:
- `:2351` `timestamp_query_pool_create`는 dummy `QueryPoolID(1)` 반환. `:2358` `..._get_results`는 `bzero`("Metal doesn't support timestamp queries, so we just clear the buffer"). `:2363` `..._result_to_time`는 입력 그대로. `:2367`/`:2370` reset/write no-op. → **Metal에서 GPU 시간은 항상 0.**

**GLES3**: `drivers/gles3/storage/utilities.{h,cpp}`에 stub만, 실제 GL 타이머 쿼리 없음.

### 0.2 집계·전송·에디터 표시 계층

- `servers/rendering/rendering_server.h:987-989` — `set_frame_profiling_enabled(bool)`, `get_frame_profile()`(→`Vector<FrameProfileArea>`), `get_frame_profile_frame()`.
- `servers/rendering/rendering_server_default.cpp:138-161` — 매 프레임 캡처 타임스탬프로 `new_profile` 구성: `gpu_msec = (time_gpu - base_gpu)/1e6`, `cpu_msec = (time_cpu - base_cpu)/1e3`, 이름은 `get_captured_timestamp_name(i)`. `vp_`로 시작하는 이름은 뷰포트 스코프로 라우팅.
- `renderer_rd/storage_rd/utilities.cpp:222` — `capture_timestamps_begin()`가 `"Frame Begin"` 삽입.
- 에디터 UI: `editor/debugger/editor_visual_profiler.{h,cpp}` — `struct Metric { Vector<Area> areas; }`, 각 `Area`에 `float cpu_time` + `float gpu_time`(`.h:57-58`). **핵심 데이터 모델 = 프레임별 (이름, CPU시간, GPU시간) 삼중항의 flat 리스트.**
- 전송: `servers/debugger/servers_debugger.cpp:356-388` `VisualProfiler : EngineProfiler`, `tick`이 `RS::get_frame_profile()`을 읽어 직렬화.
- 콘솔: `main/main.cpp:594` `--gpu-profile`; `rendering_server_default.cpp:165-208` `set_print_gpu_profile` → 초당 1회 "GPU PROFILE (total Xms)" 출력.

### 0.3 debug label / GPU 마커 — 존재하나 **게이팅됨**

- 공개 API: `rendering_device.cpp:7819` `draw_command_begin_label` — **`!context->is_debug_utils_enabled()`면 조기 반환**. `:7830` `draw_command_insert_label`는 **deprecated no-op**(커맨드 재정렬 때문). `:7835` `end_label`.
- Vulkan 게이팅: `drivers/vulkan/rendering_context_driver_vulkan.cpp:450-461` — `VK_EXT_debug_utils`는 `want_debug_utils`(= `DEV_ENABLED` 또는 `OS::is_stdout_verbose()`)일 때만 요청. `:1053` `is_debug_utils_enabled()`.
- Vulkan 구현: `rendering_device_driver_vulkan.cpp:6822` `command_begin_label` → `CmdBeginDebugUtilsLabelEXT`, 폴백 `CmdDebugMarkerBeginEXT`(legacy `VK_EXT_debug_marker`).
- D3D12: `rendering_device_driver_d3d12.cpp:61` `#include <WinPixEventRuntime/pix3.h>`; `:5646` `PIXBeginEvent`; `:5653` `PIXEndEvent`.
- Metal: `rendering_device_driver_metal.cpp:2375` `command_begin_label` → `pushDebugGroup`; `metal3_objects.cpp:79`.
- 렌더러가 곳곳에서 label 방출: `render_forward_clustered.cpp`, `render_forward_mobile.cpp`, `sky.cpp`, `gi.cpp`, `taa.cpp`, `ss_effects.cpp` 등.

### 0.4 통계·메모리·네이티브 핸들

- `Performance` 싱글턴 (`main/performance.h:74-79`): `RENDER_TOTAL_OBJECTS_IN_FRAME`, `RENDER_TOTAL_PRIMITIVES_IN_FRAME`, `RENDER_TOTAL_DRAW_CALLS_IN_FRAME`, `RENDER_VIDEO_MEM_USED`, `RENDER_TEXTURE_MEM_USED`, `RENDER_BUFFER_MEM_USED`. `add_custom_monitor(id, callable, ...)`로 커스텀 대시보드 가능.
- `RENDERING_INFO` (`rendering_server_enums.h:911-922`): 위 3개 카운터 + mem + `PIPELINE_COMPILATIONS_{CANVAS,MESH,SURFACE,DRAW,SPECIALIZATION}`.
- 메모리 리포트: `rendering_device.h:1928-1934` `get_memory_usage(MemoryType)`; `:1959` `get_driver_and_device_memory_report()`; VMA 예산은 `rendering_device_driver_vulkan.cpp:7254` `vmaGetHeapBudgets(...)`.
- 네이티브 핸들: `rendering_device.cpp:8713` `get_driver_resource(DriverResource, RID, index)` → `LOGICAL_DEVICE / PHYSICAL_DEVICE / COMMAND_QUEUE / TEXTURE / ...`의 native handle. **외부 캡처 툴(RenderDoc/PIX) 브리징에 필요한 것이 이미 노출됨.**
- Breadcrumb(디바이스 로스 진단, Aftermath/DRED 대체): `rendering_device_driver_vulkan.cpp:6867-7011` — 전용 512엔트리 버퍼에 `vkCmdFillBuffer`로 마커, `Engine::is_accurate_breadcrumbs_enabled()` 게이팅.

### 0.5 외부 프로파일러 훅 — **Tracy는 CPU만**

- Tracy: `core/profiling/profiling.h:45-81` — `#if defined(GODOT_USE_TRACY)` → `TRACY_ENABLE` + `<tracy/Tracy.hpp>`, 매크로 `GodotProfileZone` 등. GPU 프로파일 출력 경로에서도 `rendering_server_default.cpp:167` `GodotProfileZoneGrouped(_profile_zone, "gpu_profile")` 사용 — **단 이건 CPU-타이밍 Tracy zone. `TracyVkZone`(GPU) API는 미사용.**
- RenderDoc: `main/main.cpp:599` `--generate-spirv-debug-info` 헬프에 언급, `:2087` VK_LAYER 순서 주석. **인앱 캡처 트리거 없음.**
- Nsight/Aftermath: 전용 통합 없음(Nsight는 표준 debug_utils label + 타임스탬프를 그냥 소비). PIX만 D3D12에 통합.

### 0.6 관련 프로젝트 설정·플래그

- `main/main.cpp:594` `--gpu-profile`, `:595` `--gpu-validation`, `:597` `--gpu-abort`.
- `project_settings.cpp:1806` `debug/settings/profiler/max_timestamp_query_elements` (기본 256, 범위 256~65535).
- `main.cpp:2200` `debug/settings/stdout/print_gpu_profile`.

### 0.7 관련 Godot 이슈/PR (외부 검색, 미심층검증)

- `godotengine/godot#103014`, `godotengine/godot/issues/102968` — RenderingDevice GPU 프로파일 관련.
- `godot-proposals#5346` — 프로파일러 개선 제안.
- `doc/classes/RenderingDevice.xml` — 위 API 서피스의 공식 문서.

> ⚠️ 이 4건은 외부 검색 결과이며 본 리서치의 3표 검증 대상에 포함되지 않았다. 구현 착수 전 실제 상태(open/merged/closed)를 직접 확인할 것.

---

## 1. GPU 타이밍 기초 (검증된 fact, 3-0)

### 1.1 Vulkan 타임스탬프 쿼리 ✅ 3-0

- **풀 생성**: `VkQueryPool`(`queryType = VK_QUERY_TYPE_TIMESTAMP`, `queryCount` = 최대 타임스탬프 수). 한 패스 측정은 **before/after 쌍**(예: `TOP_OF_PIPE` / `BOTTOM_OF_PIPE`)으로 기록.
- **기록**: `vkCmdWriteTimestamp` / `vkCmdWriteTimestamp2`가 커맨드 버퍼 실행 중 정의된 지점에 타임스탬프를 래치. `vkCmdWriteTimestamp2`는 **정확히 하나의 파이프라인 스테이지**에만 기록해야 함(VUID-vkCmdWriteTimestamp2-stage-03859: "stage must only include a single pipeline stage"). 풀은 반드시 `VK_QUERY_TYPE_TIMESTAMP`, 쿼리 인덱스 < `queryCount`.
  > 소스: <https://docs.vulkan.org/spec/latest/chapters/queries.html>, <https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdWriteTimestamp2.html>, <https://docs.vulkan.org/samples/latest/samples/api/timestamp_queries/README.html>

### 1.2 틱 → 나노초 변환과 게이팅 조건 ✅ 3-0

- **변환**: `raw_ticks × VkPhysicalDeviceLimits::timestampPeriod`(틱당 나노초). Vulkan-Samples는 `float(t1-t0) * timestampPeriod / 1e6`로 ms 계산.
- **사전 검사(필수)**: ① `timestampPeriod`가 0이 아닐 것. ② `timestampComputeAndGraphics`가 false면, **대상 큐 패밀리의 `timestampValidBits`가 0이 아닐 것**(VUID-...-timestampValidBits-03863). `timestampValidBits`(유효 36~64, 0=미지원)는 의미 있는 비트 수이자 **어떤 큐가 타이밍 가능한지를 게이팅**.
  > 소스: 위 spec + samples. **→ 설계 함의**: async-compute/transfer 큐에서 타임스탬프를 쓰려면 이 값을 반드시 확인해야 함(§5.3).

### 1.3 타임스탬프 쓰기는 공짜가 아니다 ✅ 3-0

- Vulkan 타임스탬프 쓰기는 **이전에 제출된 모든 커맨드에 대한 실행 의존성(execution dependency, 배리어 유사)**을 정의한다(지정 스테이지로 스코프됨). Vulkan-Samples: "vkCmdWriteTimestamp *defines an execution dependency similar to a barrier on all commands that were submitted before it*."
- **핵심 caveat**: 타임스탬프가 파이프라인을 직렬화해 **측정 대상 타이밍 자체를 교란**할 수 있다. → 쿼리 개수를 남발하면 안 됨(§5).
  > 소스: samples README + spec.

### 1.4 D3D12 등가 ✅ 3-0

- `ID3D12QueryHeap`, 전용 힙 타입 `D3D12_QUERY_HEAP_TYPE_TIMESTAMP` / `..._PIPELINE_STATISTICS`, 쿼리 타입 `D3D12_QUERY_TYPE_TIMESTAMP` / `..._PIPELINE_STATISTICS`. `CreateQueryHeap`로 생성, 커맨드 리스트에서 `EndQuery`/`ResolveQueryData`로 해소. D3D12 출시(2015)부터 안정.
- 순수 GPU 기간(정렬 아님)은 `GetTimestampFrequency`(틱/초) + 역수 프리컴퓨트(`1000.0 / gpuFreq` → ms).
  > 소스: <https://microsoft.github.io/DirectX-Specs/d3d/CountersAndQueries.html>

### 1.5 Metal 등가 ✅ 3-0

- `MTLCounterSampleBuffer`(`MTLDevice.makeCounterSampleBuffer` + `MTLCounterSampleBufferDescriptor`). `MTLRenderCommandEncoder.sampleCounters(sampleBuffer:sampleIndex:barrier:)`(ObjC `sampleCountersInBuffer:atSampleIndex:withBarrier:`)가 렌더 패스 중 하드웨어 카운터를 지정 `sampleIndex`에 샘플. `barrier=true`면 이전 커맨드 완료 후 샘플(stage-boundary), **런타임 성능 비용 존재**.
  > 소스: <https://developer.apple.com/documentation/metal/mtlrendercommandencoder/samplecounters(samplebuffer:sampleindex:barrier:)>
  > **→ 실측 대비**: Godot Metal 드라이버(`rendering_device_driver_metal.cpp:2351-2373`)는 이 API를 **전혀 쓰지 않는다**. 여기가 Metal 타이밍 구현 지점.

---

## 2. CPU ↔ GPU 타임라인 정렬 (검증된 fact, 3-0) — **가장 어려운 문제이자 핵심 격차**

> **왜 중요한가**: 코어 Vulkan/D3D12/Metal 타임스탬프는 **각자의 GPU 클럭 도메인**이라 CPU(렌더 스레드) 타임라인과 **같은 축에 못 놓는다**. Unreal Insights/Tracy식 "CPU-GPU 겹친 타임라인"은 이 정렬 없이는 불가능. Godot 현재 구현(`rendering_server_default.cpp:138-161`)은 CPU ticks와 GPU period를 **따로** 재서 근사할 뿐, 두 클럭을 교차 캘리브레이션하지 않는다 → **이게 §5 격차의 진짜 알맹이.**

### 2.1 Vulkan: `VK_EXT_calibrated_timestamps` ✅ 3-0

- 코어 Vulkan은 1.0부터 타임스탬프를 노출하지만 **wall-clock/시스템 시간과 해석할 방법을 제공하지 않는다**(스펙 원문). 이 확장은 **두 시간 도메인에서 준-동시(quasi-simultaneously) 샘플**된 캘리브레이션 타임스탬프를 질의한다.
- `vkGetCalibratedTimestampsEXT(device, timestampCount, pTimestampInfos, pTimestamps, pMaxDeviation)` — 각 `VkCalibratedTimestampInfoEXT`가 시간 도메인 지정, 샘플 + `pMaxDeviation`(정확도 상한) 반환.
- **4개 도메인**: `VK_TIME_DOMAIN_DEVICE_EXT`(GPU 클럭), `..._CLOCK_MONOTONIC_EXT`(POSIX), `..._CLOCK_MONOTONIC_RAW_EXT`(Linux), `..._QUERY_PERFORMANCE_COUNTER_EXT`(Windows QPC). 디바이스 지원 부분집합은 `vkGetPhysicalDeviceCalibrateableTimeDomainsEXT`로 열거.
- **참고(시의성)**: `VK_KHR_calibrated_timestamps`로 승격됨(동일 시맨틱, KHR 이름 선호).
  > 소스: <https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_calibrated_timestamps.html>, <https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_calibrated_timestamps.html>

### 2.2 D3D12: `GetClockCalibration` ✅ 3-0

- `ID3D12CommandQueue::GetClockCalibration([out] UINT64 *pGpuTimestamp, [out] UINT64 *pCpuTimestamp)` — **GPU 타임스탬프 카운터와 CPU 카운터(QPC)를 거의 같은 순간에 샘플**. Microsoft Learn: "samples the CPU and GPU timestamp counters at the same moment in time."
- 정확한 정렬엔 안정 클럭(`SetStablePowerState`) 권장.
  > 소스: <https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-getclockcalibration>

### 2.3 Metal: `sampleTimestamps:gpuTimestamp:` ✅ 3-0

- `MTLDevice.sampleTimestamps(cpuTimestamp:gpuTimestamp:)`(ObjC 두 `MTLTimestamp*` out-param, `MTLTimestamp = UInt64`) — "captures and returns a CPU timestamp and a GPU timestamp from the **same moment in time**."
- **두 번(간격 두고) 호출**해 CPU 클럭 대비 GPU 타임스탬프 period를 유도 → raw GPU 카운터를 CPU 도메인으로 스케일/오프셋. 주기적 재샘플로 드리프트 처리.
  > 소스: <https://developer.apple.com/documentation/metal/mtldevice/sampletimestamps:gputimestamp:>

**→ 설계 결론**: 세 백엔드 모두 "두 클럭을 같은 순간 샘플" 프리미티브를 제공한다. 이것이 Tracy-class 툴이 GPU zone을 공유 타임라인에 놓는 바로 그 메커니즘이다. Godot의 프로파일러를 고급화하려면 **드라이버 인터페이스에 `get_calibrated_timestamps()` 류를 추가**하고 프레임마다(또는 N프레임마다) 앵커를 갱신해야 한다. → §8 로드맵 P2.

---

## 3. 오버헤드·정확성 (fact + design)

### 3.1 N-프레임 지연 리드백 링버퍼 ✅ 3-0 (fact)

- Microsoft 공식 D3D12 샘플(`PerformanceTimers.cpp`)의 검증된 패턴:
  - `GetTimestampFrequency(&gpuFreq); m_gpuFreqInv = 1000.0/double(gpuFreq);`
  - 리드백 버퍼를 **`(maxFrameCount + 1)` 프레임 인스턴스** 크기로 잡고, **`maxFrameCount` 프레임 전** 결과를 읽음 → GPU 완료가 보장되어 **맵/리드백에 스톨 없음**.
  - `readBackFrameID = (resolveToFrameID+1) % (maxFrameCount+1)`.
  > 소스: <https://github.com/microsoft/DirectX-Graphics-Samples/.../util/PerformanceTimers.cpp>
  > ⚠️ **부분 반박(1-2)**: "고정 슬롯 힙 + start/stop마다 연속 2개 `EndQuery`"라는 **슬롯 레이아웃 세부**는 반박됨. 링버퍼/N-프레임 지연 개념은 확정이나, **정확한 슬롯 부킹은 실제 샘플 소스를 다시 읽고 구현**할 것.
  > **→ 실측 대비**: Godot은 이미 프레임별 `timestamp_pool`(`rendering_device.cpp:8473`)로 이 패턴을 부분 구현 중. 고급화 시 프레임 인플라이트 수에 맞춘 링 크기와 지연 리드백을 재확인.

### 3.2 쿼리 예산·타일드 GPU (open — 미검증)

- 프레임당 적정 쿼리 수, 타일드(모바일/Apple) GPU에서 write-timestamp 배리어 × 타일 경계 상호작용의 **정량적** 오버헤드는 **본 검증 패스에서 확정 못 함**. §1.3의 배리어 비용은 정성적으로만 확정.
- **→ 권고(design)**: 프로파일러를 **기본 off**, 켜도 **패스 경계급 계층(수십 개 쿼리)**으로 제한. 서브-드로우콜급 계측은 opt-in. 모바일에선 배리어 비용이 커 별도 벤치 필요.

### 3.3 멀티 큐 / async compute (open — 미검증)

- 각 큐가 **독립적·비교불가 타임스탬프 도메인**을 가질 때 통합 타임라인에 올리는 법은 미검증. `timestampValidBits`(§1.2)로 큐별 지원 확인이 선행 조건. 큐별 캘리브레이션 앵커가 필요할 가능성 높음(design 가설).

---

## 4. 프로덕션 엔진/툴 아키텍처 (design/reference — **본 검증 패스에서 미확정**)

> ⚠️ **정직성 고지**: 딥리서치의 상위 25개 검증 슬롯은 API 레벨 사실(§1~3)에 집중됐고, **엔진 아키텍처(Unreal Insights Trace, Unity ProfilerMarker/Recorder, RenderDoc counter 모델, AMD RGP/SQTT, Nsight, PIX, Tracy `TracyVkZone`/`TracyD3D12Zone` 상관)에 대해 살아남은 3-0 검증 주장은 0건.** 아래는 **설계 참조**로만 취급하고, 채택 전 각 벤더 1차 문서로 재확인할 것.

**패턴 요약(미검증 설계 참조):**
- **계층 스코프 GPU zone**: 중첩 begin/end 마커가 **타임스탬프 쿼리 쌍**으로 매핑됨. `TracyVkZone(ctx, cmdbuf, "name")` 류가 스코프 진입/탈출에 타임스탬프를 써 flame-graph를 구성.
- **Tracy GPU context**: `VK_EXT_calibrated_timestamps`를 켜 CPU/GPU를 정렬하고, 캘리브레이션이 없으면 주기적 재동기화(resync)로 드리프트 보정 — 이는 §2와 정합적(방향성은 신뢰, 세부는 미검증).
- **Unreal Insights**: Trace 프레임워크로 CPU/GPU 이벤트를 스트림, Timing 패널에서 겹친 타임라인. **Godot의 유사물** = `servers_debugger.cpp`의 `EngineProfiler` 스트림(§0.2)을 계층 이벤트로 확장.
- **AMD RGP**: SQTT(하드웨어 스레드 트레이스)는 벤더 전용 → 인엔진 재현 불가, 외부 툴 연동(§5)이 답.

**Godot에의 사상(design)**: Godot의 자연스러운 경로는 자체 툴을 새로 만드는 게 아니라 ① 기존 `capture_timestamp`를 **계층형**으로 확장(begin/end 스택) + ② `EditorVisualProfiler`를 flat 막대에서 **flame-graph**로 확장(§0.2 데이터 모델에 depth 필드 추가) + ③ Tracy GPU context 연동(§0.5)으로 외부 타임라인도 덤으로 얻는 것.

---

## 5. 마커/레이블 시스템 (Godot 실측 + 외부)

### 5.1 세 백엔드 마커 API (외부, 부분 검증 + 실측)

- **Vulkan `VK_EXT_debug_utils`**: `vkCmdBeginDebugUtilsLabelEXT` / `...End` / `...Insert` — 중첩 스코프 label 영역. RenderDoc/Nsight가 이걸 소비해 이벤트 브라우저에 계층 표시.
  > 소스: <https://registry.khronos.org/VulkanSC/specs/1.0-extensions/man/html/VK_EXT_debug_utils.html>, <https://renderdoc.org/docs/how/how_annotate_capture.html>
- **D3D12 PIX**: `PIXBeginEvent`/`PIXEndEvent`. **실측**: Godot이 이미 사용(`rendering_device_driver_d3d12.cpp:5646`).
- **Metal**: `pushDebugGroup`/`popDebugGroup` + signpost. Xcode GPU 캡처가 소비. **실측**: Godot 사용(`metal3_objects.cpp:79`).

### 5.2 실측 격차: **label이 최종 사용자에겐 꺼져 있다**

- §0.3대로 Godot은 `VK_EXT_debug_utils`를 **`DEV_ENABLED`/verbose에서만** 요청(`rendering_context_driver_vulkan.cpp:455-461`). → 릴리스 템플릿에서 RenderDoc/Nsight를 붙여도 **label이 안 보인다**.
- **→ 권고(design)**: 프로파일러 활성화 시 **런타임에 `VK_EXT_debug_utils`를 강제 요청**하는 경로(명령행 `--gpu-profile` 또는 프로젝트 설정 연동)를 추가. 이건 코어 수정이지만 국소적.

### 5.3 인엔진 타임라인과의 이중 활용

- **핵심 사상**: 동일한 begin/end label 스택을 ① 외부 툴 마커(§5.1)와 ② 인엔진 타임스탬프 쌍(§1) **양쪽에 동시 방출**하면, 하나의 계측으로 RenderDoc 캡처와 인엔진 flame-graph를 모두 얻는다. Godot의 `draw_command_begin_label`/`capture_timestamp`가 이미 별개로 존재하므로 **둘을 한 스코프 헬퍼로 묶는 것**이 최소 변경 고부가 지점.

---

## 6. 데이터 모델·UI (design)

> 본 검증 패스에서 flame-graph/VRAM 예산/spike 검출에 대한 3-0 확정 주장은 0건. 아래는 §0의 실측 데이터 모델 위에 세운 **설계 권고**.

- **계층 타임라인**: 현재 `Metric.areas`(flat, `.h:57-58`)에 `depth`/`parent` 필드 추가 → GPU 패스의 flame-graph. 패스별 GPU ms + draw call/primitive 수(§0.4 `RENDERING_INFO`)를 같은 노드에 부착.
- **VRAM 예산**: Vulkan `VK_EXT_memory_budget`(VMA `vmaGetHeapBudgets`, 이미 `:7254`에서 호출) / D3D12 DXGI residency budget을 힙별 usage-vs-budget으로 표시. VMA는 **매 프레임 예산 질의 비권장** → 캐시 + 주기 갱신.
  > 소스: <https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/staying_within_budget.html>
- **CPU↔GPU 상관**: §2 캘리브레이션으로 두 타임라인을 같은 축에 오버레이(Insights식).
- **롤링 히스토리·spike 검출**: `EditorVisualProfiler`는 이미 `Vector<Metric> frame_metrics` 링을 가짐(`.h:85`) → 프레임 통계(평균/최대/p95)와 임계 초과 하이라이트를 얹기. (구체 알고리즘은 미검증 설계.)

---

## 7. 오픈소스 레퍼런스 (연구 대상)

- **Tracy Profiler** — GPU context(`TracyVkZone`/`TracyD3D12Zone`), `VK_EXT_calibrated_timestamps` 연동, CPU/GPU resync. Godot은 이미 Tracy CPU zone 연동(§0.5)이라 **GPU context만 추가**하면 됨. <https://github.com/wolfpld/tracy>
- **Vulkan-Samples / Sascha Willems** — `timestamp_queries`, `calibrated_timestamps` 예제(§1, §2의 코드 근거). <https://docs.vulkan.org/samples/latest/samples/api/timestamp_queries/README.html>
- **wgpu-profiler / wgpu** — Rust 진영 GPU 타임스탬프 패스 참조. <https://github.com/Wumpf/wgpu-profiler>, <https://github.com/gfx-rs/wgpu/issues/6406>
- **Microsoft DirectX-Graphics-Samples `PerformanceTimers.cpp`** — N-프레임 지연 리드백(§3.1)의 정본. <https://github.com/microsoft/DirectX-Graphics-Samples>
- **Godot** — `doc/classes/RenderingDevice.xml`(API 서피스), 이슈/PR §0.7.

---

## 7.5. 확정된 6개 구멍 — 메우는 법 (구현 상세)

> 2차·3차 딥리서치(2 워크플로, 각 ~100 에이전트) + 코드베이스 2차 실측의 산출물. §5(gaps 문서)에서 "막힌 구멍 (실측 확정)"으로 못박은 6개를 **각각 어떻게 닫는가**로 전개한다. 검증 강도를 항목별로 명시(3-0 확정 / 미검증 / 반박).
>
> **먼저 관통하는 제약 하나 (실측 확정).** Godot 렌더 그래프는 배리어 배칭을 위해 **커맨드를 재정렬**한다(`rendering_device_graph.cpp:2608~` Kahn 위상정렬 → longest-path level → `(level, priority, index)` 안정정렬). 그래서 `draw_command_insert_label`이 deprecated no-op다. **그러나 `capture_timestamp`는 안전하다** — `_add_command_to_graph`(`:375-387`)가 각 타임스탬프를 "직전 타임스탬프 이후 모든 커맨드의 의존 후속 + 이후 모든 커맨드의 의존 선행"으로 묶어 **완전 직렬화 배리어**로 만든다. 따라서 타임스탬프는 재정렬을 넘지 못하고 전역 순서가 보존된다 → **begin/end 쌍은 재정렬 후에도 coherent**. **대가**: 매 타임스탬프가 GPU 오버랩을 죽인다(bottom-of-pipe 배리어). 이래서 엔진이 타임스탬프를 적게(프레임당 ~40~80) 쓴다. → **세밀한 계층은 파이프라이닝을 파괴**하는 트레이드오프가 존재.

### 구멍 ① 계층(hierarchy) 없음 → record-time scope 스택 ✅ 3-0

- **핵심 사실(검증)**: 프로덕션 프로파일러(wgpu-profiler, Tracy)는 계층을 **GPU 실행 순서에서 재구성하지 않는다.** **커맨드 record 시점의 lexical scope 스택**으로 추적한다. Tracy `VkCtxScope` 생성자가 `queryId = NextQueryId()` 할당 + `GpuZoneBeginSerial` 큐잉, 소멸자가 **별개의 두 번째** `queryId` + `GpuZoneEndSerial`. Begin/End serial 이벤트가 record 순서로 큐잉되므로 **트리는 직렬 순서로 클라이언트에서 재구성**되고, 쿼리 슬롯은 나중 리드백에서 타임스탬프 값만 받는다. → **재정렬과 무관.**
  > 소스: <https://github.com/Wumpf/wgpu-profiler>, <https://github.com/wolfpld/tracy/blob/master/public/tracy/TracyVulkan.hpp>
- **VK_EXT_debug_utils label도 같은 불변식(검증)**: begin/end는 "단일 큐로의 선형 제출 스트림"에서 **matched & balanced**여야 한다(한 커맨드버퍼에서 열고 다른 데서 닫아도 됨). 즉 페어링은 record 스트림의 구조이지 실행 순서의 함수가 아니다.
  > 소스: <https://docs.vulkan.org/spec/latest/chapters/debugging.html>
- **→ Godot 적용(design)**: Godot엔 이미 **`>`/`<` 이름 접두사 계층 관례**가 존재하나 **에디터 UI에서만 문자열로 재구성**되고 데이터 모델은 flat이다(`renderer_viewport.cpp:307/334` `RENDER_TIMESTAMP("> Render 3D Scene")` … `("< …")` → `EditorVisualProfiler::_update_frame` `cpp:358-392`가 `>`로 TreeItem push, `<`로 pop). **가장 싼 경로**: 이 관례를 정식 `depth`/`parent` 필드로 승격(`FrameProfileArea`, `Metric::Area`에 추가) — record 시점 begin/end 스택을 그대로 반영하면 재정렬 안전성은 위 사실로 보장됨. **주의**: 세밀한 zone마다 타임스탬프를 넣으면 관통 제약대로 직렬화 → 패스 경계급으로 제한.

### 구멍 ② Tracy GPU zone 미연동 → `TracyVkContext` 배선 ✅ 3-0

- **Tracy 내부(검증, 1차 소스)**: `VkCtx`는 **고정 크기 링버퍼 `VkQueryPool`** — `QueryCount = 64*1024`(비-Apple) / `4*1024`(Apple `__APPLE__`), 생성 실패 시 절반씩 감소. 슬롯은 lock-free: `std::atomic<uint64_t> m_head.fetch_add(1)` → `NextQueryId() = id % m_queryCount`, `m_tail`은 수집 스레드만 진행. **zone당 슬롯 2개**(begin+end), 전부 `VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT`.
- **`TracyVkCollect`가 스톨 없이 지연 리드백(검증)**: `vkGetQueryPoolResults`를 **`VK_QUERY_RESULT_64_BIT | ..._WITH_AVAILABILITY_BIT`**로 호출(**`WAIT_BIT` 없음** → 블로킹 안 함). availability 워드 `m_res[idx*2+1]==0`이면 중단하고 `m_oldCnt`에 잔여 기록 → 다음 `Collect()`에서 재시도. 완료된 쿼리만 `GpuTime`으로 변환.
- **캘리브레이션 전략(검증 — 1차 리서치 open question 해소)**: **per-frame 재캘리브레이션이 아니다.** context init에서 `FindCalibratedTimestampDeviation()`이 `NumProbes=32`회 `vkGetCalibratedTimestampsEXT` → `m_deviation = minDeviation * 3/2`(1회). 이후 `Collect()` 안에서 `m_timeDomain != DEVICE`일 때만 **주기적 드리프트 resync**(`Calibrate(tcpu, tgpu, 10)`, `delta = tcpu - m_prevCalibration > 0`일 때만 `GpuCalibration` 아이템 큐잉). 호스트 도메인은 QPC(Windows)/`CLOCK_MONOTONIC_RAW`(Linux).
  > 소스: <https://github.com/wolfpld/tracy/blob/master/public/tracy/TracyVulkan.hpp> (전 항목 verbatim 인용 검증)
- **→ Godot 배선점(실측)**: Tracy는 인트리 벤더링이 아니라 빌드 시 `profiler_path`를 `CPPPATH`에 prepend(`core/profiling/SCsub`)하므로 `<tracy/TracyVulkan.hpp>`도 동일 `public/`에서 resolve됨 → **벤더링 블로커 없음**. `TracyVkContext`는 `get_driver_resource(LOGICAL_DEVICE/PHYSICAL_DEVICE/COMMAND_QUEUE)`(`rendering_device.cpp:8713`)로 1회 생성. `TracyVkCollect(ctx, cmd)`는 **`frames[frame].command_buffer`**(`_begin_frame` `:8068`에서 begin, 기존 타임스탬프 리드백/리셋이 도는 `:8089-8091` 직후)에 자연 삽입. 단 zone은 raw `vkCmdWriteTimestamp`가 아니라 드라이버 `command_timestamp_write` 추상(`rendering_device_driver.h:833`)을 경유해야 관통 제약(직렬화)을 제어 가능.

### 구멍 ③ Metal 타이밍 0 → 인코더-경계 카운터 샘플링 ✅ 3-0 (단 버퍼 생성/resolve 코드 경로는 반박)

- **아키텍처 분기(검증, 결정적)**: **Apple Silicon(TBDR)은 `atStageBoundary`만 지원** — 모든 프리미티브 처리 후 프래그먼트를 처리하므로 draw call이 개별 샘플 단위가 아니다. 이미디엇 모드(Intel/AMD)는 `atDrawBoundary` 지원. **반드시 지점별로** `device.supportsCounterSampling(MTLCounterSamplingPoint)`로 확인(`atStageBoundary`/`atDrawBoundary`/`atBlitBoundary`/`atDispatchBoundary`/`atTileDispatchBoundary`).
  > 소스: <https://developer.apple.com/documentation/metal/mtldevice/supportscountersampling(_:)>, Apple Tech Talk 10001
- **TBDR 경로(검증)**: render-pass descriptor의 **`sampleBufferAttachments`**(`MTLRenderPassSampleBufferAttachmentDescriptor`)에 `MTLCounterSampleBuffer`를 붙이고 4개 인덱스 `startOfVertexSampleIndex`/`endOfVertexSampleIndex`/`startOfFragmentSampleIndex`/`endOfFragmentSampleIndex` 설정 — **mid-pass 샘플 커맨드가 아니라 패스 경계**. compute/blit은 `startOfEncoderSampleIndex`/`endOfEncoderSampleIndex`만.
- **이미디엇 모드 경로(검증)**: 인코더의 `sampleCounters(sampleBuffer:sampleIndex:barrier:)`(ObjC `sampleCountersInBuffer:atSampleIndex:withBarrier:`)를 커맨드 사이에 호출.
- **카운터셋 발견(검증)**: `MTLDevice.counterSets`가 nil이면 미지원, 아니면 각 `MTLCounterSet.name`을 `MTLCommonCounterSetTimestamp` rawValue와 매칭. **셋이 지원돼도 개별 카운터는 미지원일 수 있어** per-counter 확인 필요.
  > 소스: <https://developer.apple.com/documentation/metal/gpu_counters_and_counter_sample_buffers/sampling_gpu_data_into_counter_sample_buffers>
- ⚠️ **반박(0-3)**: `MTLCounterSampleBufferDescriptor`(sampleCount/storageMode/counterSet) **생성**과 `resolveCounterRange` → `MTLCounterResultTimestamp`(ns)·`MTLCounterErrorValue` **리드백**의 정확한 코드 경로는 검증 실패 → **구현 전 Apple 1차 문서 직접 확인 필수**.
- **→ Godot 적용(실측)**: `rendering_device_driver_metal.cpp:2351-2373`의 5개 stub 교체. **구조적 불일치**: Godot `command_timestamp_write(cmd, pool, index)`는 자유 호출인데(`rendering_device_graph.cpp:1234`) Metal 샘플링은 **인코더 부착**이라 `MDCommandBuffer`의 현재 인코더 접근이 필요. metal-cpp `MTL::` 사용, floor는 Metal 3(`metal3_objects.*`, metal4 없음). CPU↔GPU는 `MTL::Device::sampleTimestamps(cpuTimestamp:gpuTimestamp:)`(§2.3).

### 구멍 ④ pipeline statistics 미노출 → 쿼리 타입 파라미터 추가 ✅ 3-0

- **실측(확정)**: 현재 `QueryPool` 추상은 **타임스탬프 전용 하드코딩** — `rendering_device_driver.h:826-833`에 쿼리 타입 파라미터가 없고, Vulkan(`:6758` `VK_QUERY_TYPE_TIMESTAMP`)/D3D12(`:5580` `..._HEAP_TYPE_TIMESTAMP`) 생성자가 타입을 박아둔다. **pipeline statistics/occlusion 쿼리는 코드 전무**(`vkCmdBeginQuery` 비-타임스탬프 0건, `occlusionQueryPrecise`/`pipelineStatisticsQuery` 피처 미요청 `vulkan.cpp:814-815`). ⚠️ **함정**: `RECORD_PIPELINE_STATISTICS`(`vulkan.cpp:74`)는 **셰이더 컴파일 타이밍 CSV 덤프**이지 GPU 카운터가 아님 — 무관.
- **Vulkan(검증, 1차)**: `VK_QUERY_TYPE_PIPELINE_STATISTICS` — 11개 `VkQueryPipelineStatisticFlagBits`(`INPUT_ASSEMBLY_VERTICES=0x1` … `COMPUTE_SHADER_INVOCATIONS=0x400`, +EXT mesh/task 0x800/0x1000), `VkQueryPoolCreateInfo.pipelineStatistics`로 선택, **활성 비트당 uint64 하나**. `vkCmdBeginQuery`/`vkCmdEndQuery`로 구간 래핑, `VkPhysicalDeviceFeatures.pipelineStatisticsQuery` 피처 필요.
  > 소스: <https://registry.khronos.org/vulkan/specs/latest/man/html/VkQueryPipelineStatisticFlagBits.html>
- **D3D12(검증, 1차)**: `D3D12_QUERY_DATA_PIPELINE_STATISTICS` = **11개 UINT64** (IAVertices, IAPrimitives, VSInvocations, GSInvocations, GSPrimitives, CInvocations, CPrimitives, PSInvocations, HSInvocations, DSInvocations, CSInvocations). `..._HEAP_TYPE_PIPELINE_STATISTICS` 힙, **Direct 커맨드리스트 전용**, `BeginQuery`+`EndQuery` 페어(타임스탬프만 EndQuery-only), `ResolveQueryData` 8바이트 정렬.
  > 소스: <https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_query_data_pipeline_statistics>, <https://microsoft.github.io/DirectX-Specs/d3d/CountersAndQueries.html>
- **액션 가능 지표(검증)**: **오버드로 = PSInvocations / 렌더된 픽셀 수**; **삼각형 처리량 = IAPrimitives**(토폴로지 민감 — 6버텍스 스트립=4삼각형 vs 리스트=2); **클리핑 효율 = CPrimitives vs CInvocations**. ⚠️ **caveat(스펙 명시)**: 이 카운터들은 **아키텍처 상대적 추정치** — PS invocation은 helper invocation·early-Z 포함, **타일드 GPU는 씬을 여러 번 replay**할 수 있어 값이 달라짐 → **UI에 "근사치" 라벨 필수**.
- **→ 구현(design)**: `QueryPool` 생성 API에 쿼리 타입 인자 추가 + begin/end-query 드라이버 메서드 신설. 기존 timestamp 패턴 외 재사용 스캐폴딩 없음. §8 P2에 배치.

### 구멍 ⑤ (관통 제약의 정량화) 타일드 오버헤드 + 멀티큐 통합 타임라인 ✅ 3-0

- **타임스탬프 신뢰 스테이지(검증)**: `TOP_OF_PIPE`/`BOTTOM_OF_PIPE`에서만 유의미 — "많은 스테이지 조합·순서는 파이프라인 오버랩 때문에 의미 있는 결과를 안 준다"(Vulkan-Samples). TBDR은 프래그먼트를 defer/replay하므로 **mid-render-pass per-draw 타이밍은 무의미**, **render-pass load/store 경계에서만 신뢰**. (구멍 ③의 Metal `atStageBoundary`-only와 동일 원리.)
  > 소스: <https://docs.vulkan.org/samples/latest/samples/api/timestamp_queries/README.html> ⚠️ ARM/Qualcomm/Imagination 개별 벤더 문서의 "tile flush 강제" 정량치는 미인용(Khronos 근거로 방향만 확정).
- **큐별 게이팅(검증)**: `VkQueueFamilyProperties.timestampValidBits`(유효 36~64, **0=미지원**)를 **큐 패밀리별로** 확인해야 하며 **transfer-only 큐는 흔히 0을 보고**. VUID-vkCmdWriteTimestamp2-timestampValidBits-03863이 non-zero를 요구.
  > 소스: <https://docs.vulkan.org/refpages/latest/refpages/source/VkQueueFamilyProperties.html>
- **통합 타임라인(검증)**: `vkCmdWriteTimestamp2` 타임스탬프는 **`VK_TIME_DOMAIN_DEVICE_KHR` 단일 디바이스 도메인** → **같은 디바이스의 큐 간 직접 비교 가능**. calibrated_timestamps 활성 시 "happens-after 타임스탬프는 더 낮은 값을 갖지 않는다"는 단조성이 **모든 제출에 걸쳐** 확장(인과 순서 한정, 임의 동시 쓰기는 보장 안 함). async-compute 타임스탬프는 `VkPipelineStageFlags2`에 컴퓨트 큐 유효 스테이지(`COMPUTE_SHADER_BIT`/`ALL_COMMANDS_BIT`) 지정(VUID-...-stage-03860).
  > 소스: <https://registry.khronos.org/VulkanSC/specs/1.0-extensions/man/html/vkCmdWriteTimestamp2KHR.html>, <https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_calibrated_timestamps.html>
- **→ Godot 함의(design)**: 단일 DEVICE 도메인이므로 멀티큐 통합에 **큐별 별도 앵커 불필요** — 디바이스 캘리브레이션 1개로 충분. 단 큐별 `timestampValidBits` 체크는 선행. 모바일 타깃에선 프로파일러를 **render-pass 경계급으로 강제**.

### 구멍 ⑥ VRAM/residency 실시간 추적 ✅ Vulkan 3-0 / ⚠️ D3D12·Metal 미검증

- **Vulkan+VMA(검증, 1차)**: `VK_EXT_memory_budget`(device ext, `VK_KHR_get_physical_device_properties2` instance ext 필요) + `VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT`. **매 프레임 `vmaSetCurrentFrameIndex()`**(내부에서 예산 질의해 오버헤드 회피). `vmaGetHeapBudgets()`→`VmaBudget`는 **매 프레임/매 할당 전 호출해도 될 만큼 가벼움**, 반면 `vmaCalculateStatistics()`는 무거워 드물게. GPUOpen 지침: 예산은 저빈도 폴링, full stats는 per-frame 금지.
  > 소스: <https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/staying_within_budget.html>
  > **실측 정합**: Godot은 이미 `vmaGetHeapBudgets`를 호출(`rendering_device_driver_vulkan.cpp:7254`) → 라이브 패널로 승격이 최소 작업.
- ⚠️ **D3D12·Metal(미검증 — 3표 통과 0건)**: D3D12 `IDXGIAdapter3::QueryVideoMemoryInfo`→`DXGI_QUERY_VIDEO_MEMORY_INFO{Budget, CurrentUsage, AvailableForReservation, CurrentReservation}`(LOCAL/NON_LOCAL 분리), `RegisterVideoMemoryBudgetChangeNotificationEvent`, `MakeResident`/`Evict`; Metal `recommendedMaxWorkingSetSize`/`currentAllocatedSize`/`hasUnifiedMemory`. **표준 API로 널리 쓰이나 본 패스에서 1차 소스 재확인 실패** → 구현 전 MS/Apple 문서 직접 확인.
- **per-resource-type 귀속(open)**: 세 API 모두 **텍스처/버퍼/렌더타깃별 분해를 보고하지 않음** → 엔진이 자체 할당 원장(ledger)을 유지해야. Godot은 `get_memory_usage(MEMORY_TEXTURES/BUFFERS)`(§0.4)로 부분 보유 → 이걸 driver heapUsage와 대조.

---

## 8. 구현 로드맵 (design — 난이도 오름차순)

> 전제: 전부 **코어 C++ 수정**(GDExtension으로 RD 내부 쿼리 풀 접근 불가). 그러나 범위는 국소적.

- **P0 (수일, 저리스크) — 있는 걸 켜고 계층화.** (구멍 ①)
  - `draw_command_begin_label`을 프로파일러 활성 시 **강제 활성**(§5.2). 기존 `>`/`<` 이름 관례를 정식 `depth`/`parent` 필드로 승격(`FrameProfileArea`/`Metric::Area`, §7.5-①) → record-time 스택 반영, 재정렬 안전은 관통 제약으로 보장. `EditorVisualProfiler`에 depth 렌더. → 코어 0의 순수 이득(기존 API 재배선). **주의: zone 세분화는 패스 경계급으로 제한**(관통 제약: 매 타임스탬프가 직렬화 배리어).
- **P1 (1~3주) — Tracy GPU context.** (구멍 ②)
  - `core/profiling/`에 `TracyVkContext`(1회, `get_driver_resource`) + `TracyVkCollect`(`frames[frame].command_buffer`, `_begin_frame :8091` 직후) 배선(§7.5-②). 벤더링 블로커 없음(`CPPPATH` prepend). zone은 드라이버 `command_timestamp_write` 경유. Vulkan/D3D12 즉시, Metal은 P3 대기.
- **P2 (2~6주, 중리스크) — CPU↔GPU 캘리브레이션 + pipeline statistics.** (구멍 ④+캘리브레이션)
  - 드라이버 인터페이스에 `get_calibrated_timestamps()` 추가 → Vulkan `VK_EXT_calibrated_timestamps`(단일 DEVICE 도메인 → 멀티큐 앵커 1개로 충분, §7.5-⑤), D3D12 `GetClockCalibration`. **캘리브레이션은 init 1회 + 주기 드리프트 resync**(Tracy 패턴, per-frame 아님, §7.5-②). `QueryPool` 생성 API에 **쿼리 타입 인자 추가** + begin/end-query 메서드 신설 → pipeline statistics 11카운터 노출(§7.5-④, UI에 "근사치" 라벨). 큐별 `timestampValidBits` 체크 선행.
- **P3 (수 주, 중리스크) — Metal 타이밍 구현.** (구멍 ③)
  - `rendering_device_driver_metal.cpp:2351-2373` stub 교체. **구조 재설계 필요**: Metal은 인코더-부착 샘플링이라 `command_timestamp_write` 자유 호출과 불일치 → `MDCommandBuffer` 현재 인코더 접근. TBDR은 render-pass `sampleBufferAttachments`(`atStageBoundary`), 이미디엇은 `sampleCounters`(§7.5-③). **버퍼 생성/`resolveCounterRange`는 반박(0-3)이라 Apple 문서 직접 확인.** CPU↔GPU는 `sampleTimestamps:gpuTimestamp:`.
- **P4 (지속) — 데이터 모델 확장.** (구멍 ⑥)
  - VRAM 예산: Vulkan은 `vmaGetHeapBudgets` 이미 호출 중이라 라이브 패널 승격이 최소 작업(§7.5-⑥). D3D12/Metal은 문서 재확인 후. draw/primitive 부착(§0.4), per-resource 원장, spike 검출, 외부 캡처 트리거.

---

## 판단 기준 정리

- **§5 "부분 우회(RenderDoc/Tracy 외부 의존)"는 과소평가.** Godot엔 이미 작동하는 GPU 타임스탬프 프로파일러가 있고(§0), "고급화"는 **바닥부터가 아니라 국소 코어 확장**이다.
- **순수 GDExtension으로는 불가** — 타임스탬프/캘리브레이션/pipeline stats는 드라이버 레벨. 단 Nanite/Lumen급 man-year가 아니라 **P0~P2 합쳐 1~2개월급** 코어 작업.
- **명확한 구멍(실측)**: Metal 타이밍 0, 계층 없음, CPU↔GPU 캘리브레이션 없음, label 릴리스 게이팅, Tracy GPU zone 미연동, pipeline stats 미노출 — **6개 전부 메우는 법을 §7.5에서 구현 상세로 확정**. 각각 §8의 P0~P3에 대응.
- **관통 제약(실측 확정)**: 렌더 그래프 재정렬 하에서도 begin/end 타임스탬프 쌍은 coherent(각 타임스탬프가 직렬화 배리어). **단 세밀한 zone은 GPU 오버랩을 죽여 파이프라이닝 파괴** → 패스 경계급으로 제한이 설계 원칙.

### 검증 한계 (정직성 고지)

- **높은 신뢰(3-0, 1차 소스)**: §1 타임스탬프 프리미티브, §2 캘리브레이션 API, §3.1 N-프레임 리드백. §7.5 구멍 ①(record-time 계층)·②(Tracy 내부·캘리브레이션 전략)·④(pipeline stats 11카운터)·⑤(멀티큐 단일 DEVICE 도메인)·⑥ Vulkan/VMA. 전부 Khronos/Microsoft/Apple/GPUOpen 공식 문서.
- **미검증(설계 참조로만)**: §4 프로덕션 엔진 아키텍처(Insights/Unity/RGP/Nsight/PIX/Tracy 상관 세부), §6 UI/데이터 모델(flame-graph/spike). §7.5 구멍 ⑥의 **D3D12·Metal VRAM API**(3표 통과 0건 — 표준 API이나 재확인 필요). §7.5 구멍 ⑤의 **ARM/Qualcomm/Imagination 벤더별 tile-flush 정량치**(Khronos 근거로 방향만). 채택 전 벤더 1차 문서 재확인.
- **반박(폐기)**: §3.1 D3D12 샘플 고정 슬롯 힙 레이아웃(1-2, 개념 유효·슬롯 부킹 재독). §7.5 구멍 ③ **Metal `MTLCounterSampleBufferDescriptor` 생성·`resolveCounterRange` 리드백 코드 경로(0-3)** — 샘플링 지점·인코더 배선은 확정이나 버퍼 생성/resolve는 Apple 문서 직접 확인 필수.
- **§0 엔진 실측**: 로컬 master 직접 확인. 단 §0.7 이슈/PR 상태는 미검증 — 착수 전 확인.
- **open questions**: Tracy의 정확한 쿼리 풀 재활용·리드백 모델, 타일드 GPU 정량 오버헤드, 멀티큐 통합 타임라인 방식.
