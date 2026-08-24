# Godot 오픈월드(AAA급) — 코드/CLI로 못 메우는 엔진 레벨 격차

> **2026-08-18 전제 개정 (2건). 이 문서의 초판 프레임 두 개가 모두 무효화됐다.**
>
> **① 목표 수준이 스타일라이즈드에서 진짜 AAA로 올라갔다.** 초판과 이후 §1~§7 정정판은 전부 *"원신급 스타일라이즈드"* 를 목표로 놓고, 비싼 항목을 **"아트 디렉션으로 우회 가능하니 불필요"** 로 할인해 우선순위를 매겼다. 사용자가 이 전제를 취소했다 — *"스타일라이즈드 한정 아니야"*, *"정말 AAA급 말하는거"*.
> **이제 원신은 목표가 아니라 하한 참조점이다.** 비교 기준은 **UE5.4+ Nanite/Lumen · Horizon Forbidden West · Cyberpunk 2077 RT Overdrive · RDR2 · TLOU Part II** 급이다.
> 따라서 이 문서에서 **어떤 항목도 "스타일라이즈드니까 불필요"로 강등되지 않는다.** 기술 난이도와 ROI만으로 채점하고, 우회 대안을 남길 때는 반드시 **충실도 비용**(이 우회로를 택하면 무엇이 열화되는가)을 함께 적는다.
>
> **② "코어 수정 필요"는 더 이상 탈락 사유가 아니다.** 프로젝트는 upstream에서 **분리된 포크**이고 **딥 코어 개조가 승인**돼 있다(async-compute 렌더그래프 재작업, bindless, `RenderingDeviceDriver` 인터페이스 변경 포함). 판단 축은 "코어를 건드리는가"가 아니라 **"man-year급인가"** 하나뿐이다.
>
> **③ 초판의 범위 전제도 틀렸다.** 초판은 *"GUI 저작툴만 없는 것(월드 스트리밍·지형·폴리지·임포트·베이크)은 헤드리스/CLI로 대체 가능"* 이라며 이들을 범위 밖으로 뺐다. **실측 결과 이건 사실이 아니다** — 월드 스트리밍·지형·폴리지는 *에디터 툴*이 없는 게 아니라 **런타임 시스템 자체가 없다**. 게다가 이들은 §1 Nanite와 §3 가상 텍스처링의 **선행 조건**이다(스트리밍할 월드가 없으면 지오메트리/텍스처 스트리밍이 성립하지 않는다). 그래서 아래에 **§0으로 승격**해 넣었다.
>
> 이 문서는 **코드/CLI로 해결 불가**, 즉 렌더러·플랫폼·런타임 자체의 부재라 메우려면 **C++ 엔진 개조(GDExtension/모듈)** 또는 **서드파티**가 필요한 부분을 모은다.

---

## 요약 표 (AAA 기준, 2026-08-18 재채점)

> 난이도 신호는 **아트 디렉션 할인 없이** 기술 난이도·ROI로만 매겼다. 🟢=순수 GDExtension(코어 0) · 🟡=국소 코어(수주~수개월) · 🔴=대규모 코어(man-year급). **"코어 수정 필요"는 더 이상 탈락 사유가 아니다**(포크 + 딥 개조 승인).

| 격차 | AAA 난이도 | 해결 경로 |
|------|-----------|-----------|
| **§0 월드 스트리밍·지형·폴리지** | 🔴 (일부 🟡) | 스트리밍 매니저·HLOD는 신설(man-year), 지형은 Terrain3D 국소 개조, 폴리지 GPU 컬링은 `drawIndirectCount` 상위배선(3백엔드 완성·미배선) ([상세](./godot-world-streaming-terrain-research.md)) |
| **§1 Nanite** | 🔴 (경로 C) | 경로 C(네이티브 라이팅 통합)가 **목표**, A/B는 단계. man-year 무게중심은 라이팅이 아니라 **async compute 렌더그래프 재작업** ([상세](./godot-nanite-implementation-research.md)) |
| **§2 Lumen급 동적 GI** | 🔴 (HW-RT) | HW-RT **프로덕션화(GH-99119 `experimental` API 직접 개조)가 1급 목표**. SW-SDF(b)는 (c)의 준비 단계. 미러 반사·스킨드 메시 GI 필수 ([상세](./godot-lumen-gi-implementation-research.md)) |
| **§3 가상 텍스처링** | 🟡 | RVT(지형) + SW SVT/HW 스파스(유니크 텍스처) 병행 필수. PR #113429 머지/포워드포팅 ([상세](./godot-virtual-texturing-research.md)) |
| **§4 콘솔 export** | 라이선스(기술 아님) | W4 Games 위탁이 정답, 직접 구현도 가능. self-managed 전환은 포크 전제로 재검토 ([상세](./godot-console-export-research.md)) |
| **§5 고급 GPU 프로파일러** | 🟡 | 인엔진 타임스탬프 프로파일러 이미 존재, 국소 코어 확장 ([상세](./godot-gpu-profiler-implementation-research.md)) |
| **§6 애니메이션** | 🟡 (2건 🔴) | 페이셜·물리블렌딩·클로스/헤어·압축/스트리밍 신규 격차 확인. 군중 스키닝·애니 압축만 man-year ([상세](./godot-animation-pipeline-research.md)) |
| **§7 VFX / 시네마틱** | 🟡 (2건 🔴) | Niagara/Sequencer 패리티 재개봉(EditorPlugin, 코어 0). GPU 이벤트·유체 시뮬만 man-year ([상세](./godot-vfx-cinematic-research.md)) |
| **§8 물리 시뮬** | 🟡 (GPU 물리 🔴) | Jolt가 거의 전부 번들. 클로스 P1(의류 XPBD), 파괴 P1(조인트 break force). GPU 물리는 CPU Jolt로 충분 ([상세](./godot-physics-simulation-research.md)) |
| **§9 환경 시뮬(오션·비·대기)** | 🟡→🔴 | 오션·수면·수중은 GDExtension 가능(개념 0, `CompositorEffect`+RD 완비). 물리기반 대기·볼류메트릭 구름은 코어 개조 필요 ([오션](./godot-ocean-water-research.md) · [기상](./godot-weather-atmosphere-research.md)) |
| **§10 오디오 전파** | 🟡→🔴 | Steam Audio 미들웨어가 정답. 실시간 회절·전파는 미들웨어 채택의 강한 근거 ([상세](./godot-audio-propagation-research.md)) |
| **§11 기타 런타임(모션블러·업스케일러·SSS·헤어·네트워킹·텔레메트리)** | 🟡 (헤어 🔴) | 모션블러·SSS LUT·업스케일러는 애드온/GDExtension. 스트랜드 헤어는 P3 장기. Sentry는 §1 착수 전 필수 ([상세](./godot-runtime-gaps-misc-research.md)) |

---

## 0. 월드 스트리밍 · 지형 · 폴리지 — §1·§3의 선행 조건 🔴

> **2026-08-18 신설.** 초판은 *"GUI 저작툴만 없는 것(월드 스트리밍·지형·폴리지)은 헤드리스/CLI로 대체 가능"* 이라며 이 축을 범위 밖으로 뺐다. **실측 결과 이건 틀렸다** — 이들은 GUI가 아니라 **런타임 시스템 자체가 없다.** 게다가 **스트리밍할 월드 표현이 없으면 §1 Nanite도 §3 VT도 실체가 없다.** 그래서 §0으로 승격했다. 상세: [godot-world-streaming-terrain-research.md](./godot-world-streaming-terrain-research.md)

**무엇이 없나 (실측)**
- **월드 파티션/스트리밍** — `ResourceLoader.load_threaded_request()`만 존재. 자동 공간 파티션·셀 단위 비동기 로드/언로드·LOD 통합·우선순위 기반 스트리밍 전무. → 🔴 man-year(스트리밍 매니저 신설 + IO 파이프라인).
- **지형** — Terrain3D(클립맵, 최대 65,536m/side)와 godot_voxel(복셀 옥트리, 동굴 가능)이 GDExtension으로 존재하나 **리전 스트리밍(PR #1020 unmerged)·인스턴스 충돌(PR #699 draft)·동굴/오버행**이 미구현. → 🟡 국소 코어(직접 구현).
- **폴리지/인스턴싱** — `MultiMesh`는 AABB all-or-nothing 컬링(개별 인스턴스 프러스텀 컬링 불가). **핵심:** GPU 구동 인스턴싱의 `drawIndirectCount`가 드라이버 3백엔드(Vulkan/D3D12/Metal)에 **전부 구현 완료**인데 `RenderingDevice` 상위에 미배선(0매치). HZB 인프라(`renderer_scene_occlusion_cull.h:45` HZBuffer)도 이미 존재. → 🟡 국소 코어(HZB GPU 컬링 compute 패스 + drawIndirect 상위배선).
- **오브젝트/메시 스트리밍** — `lod_bias` per-instance만 있고 **HLOD 전무**. → 🔴 man-year(LOD 관리자 + HLOD 빌더 + 스트리밍 IO).
- **충돌/내비게이션 스트리밍** — Jolt `space_create`/`space_set_active` 분할, 내비게이션 `map_create`/`map_set_active` 분할 가능. → 🟢 GDExtension.

**업스트림에 기대할 수 있는 건 하나뿐 — 텍스처 스트리밍.** reduz가 proposal #6121(180👍)에서 *"Terrain will not be added to Godot"* 로 명시 거부, fire가 #11728을 *"stuck"*, *"move it out of the engine core"* 로 정리, clayjohn이 #6109에 *"Nobody works on the renderer full time"*. 공식 우선순위 페이지에 terrain·partition·open world·foliage·nanite 전부 0매치. **지오메트리 스트리밍은 기다려도 오지 않는다 — 포크에서 직접 만든다.**

---

## 1. Nanite — 가상화 지오메트리 ⚠️ (정정됨: "불가"가 아니라 "단계별 비용")

> **2026-08-03 정정.** 초판은 "❌ 불가 / C++ 렌더러 신규 개발 / 사실상 비현실적"으로 적었으나, 딥리서치 + Godot 4.8-dev 소스트리 실측 결과 **"원천적으로 못 한다"가 아니라 "단계별로 비용이 다르다"**가 정확하다. 근거·file:line은 [godot-nanite-implementation-research.md](./godot-nanite-implementation-research.md) 참조.

**무엇이 없나**
- 화면 픽셀 밀도에 맞춰 폴리곤을 실시간으로 스트리밍/디시메이션하는 가상화 지오메트리 파이프라인이 빌트인으로 없다.

**실현 가능성 — 3단계 경계 (실측 확정)**

| 목표 수준 | 코어 C++ 수정 | 방법 |
|-----------|---------------|------|
| **A. HW 래스터 + 자체 셰이딩 오버레이** (Bevy 0.14 등가) | **0 (순수 GDExtension)** | RenderingDevice compute/raster 6개 능력 + `CompositorEffect` 전부 노출 확인. meshoptimizer도 이미 번들 |
| **B. SW 래스터 (마이크로폴리곤 최적화)** | 소규모 코어 (Vulkan device-init 4곳) 또는 `VulkanHooks` 네이티브 플러그인 | 64비트 image atomic 활성화 (현재 미노출, R64 포맷 계층은 완비) |
| **C. Godot 네이티브 라이팅/GI/그림자 완전 통합** | **대규모 코어/포크** | opaque 패스 확장 or clustered 라이팅 전체 재구현 |

- **비용 절벽은 SW 래스터(B)가 아니라 네이티브 셰이딩 통합(C)에 있다.** `CompositorEffect`로 visibility buffer 생성·depth 통합·자체 합성까지는 GDExtension으로 되지만, Godot의 clustered 라이트/그림자/GI로 셰이딩하는 지점(`RenderForwardClustered`)이 바인딩 없는 내부 C++이라 외부에서 못 부른다.
- mesh shader는 **필수가 아니다** (compute SW 래스터로 우회). 최난관은 **DAG(클러스터 계층) 품질** — Bevy조차 미완인 오프라인 전처리 문제.

**현실적 대안 (2026-08-18 재채점 — 충실도 비용 명시)**
- 수동 LOD + 임포스터 + HLOD 병합(코드 배치)으로 폴리곤 예산을 관리 → **fallback일 뿐이다.** Nanite의 화면 픽셀 밀도 적응형 디시메이션을 대체하지 못해, 근경에서 실루엣 열화·LOD 팝이 그대로 남는다.
- ~~에셋 폴리곤을 애초에 낮게 잡는 아트 디렉션(스타일라이즈드)~~ → **폐기.** AAA 목표와 양립 불가.
- **경로 A(코어 0)는 검증 하니스이지 출하 대상이 아니다.** 자체 deferred-lite를 짓지 말고, 목표인 **경로 C**(네이티브 라이팅 통합)로 가는 단계로만 쓴다. man-year 무게중심은 라이팅이 아니라 **async compute 렌더그래프 재작업**이다 — 라이팅은 `_inc.glsl` 재사용으로 예상보다 저렴.

---

## 2. Lumen급 동적 글로벌 일루미네이션 ⚠️ (재평가 — "불가"에서 "단계별 부분 가능"으로)

> 딥리서치(23소스, 24/25 주장 검증) + 로컬 master 코드 교차검증 결과 §2를 재평가함. 전체: [godot-lumen-gi-implementation-research.md](./godot-lumen-gi-implementation-research.md), 실행 계획: [godot-lumen-gi-implementation-roadmap.md](./godot-lumen-gi-implementation-roadmap.md).

**무엇이 없나**
- 낮/밤 변화·동적 지오메트리에 실시간으로 반응하는 소프트 GI + 반사. (미러 반사·완전 동적 캐릭터 GI는 여전히 부재.)

**Godot의 상한선**
- `SDFGI` (semi-real-time, 동적 라이트 O / 동적 오클루더·이미시브 X, ~25프레임 수렴 지연, 동적 오브젝트는 광을 받기만·기여 못 함)
- `VoxelGI` (품질 좋으나 씬 크기 제약)
- `LightmapGI` (정적 베이크, 시간대 변화 불가)

**재평가 (핵심 반전)**
- **Lumen의 기본 경로는 HW 레이트레이싱이 아니다** — Mesh/Global SDF에 대한 **소프트웨어 레이트레이싱**(GTX-1070/SM6급 비-RT GPU 동작). 이는 Godot SDFGI의 SDF 트레이싱과 **동일 알고리즘 계열** → 소프트 확산 GI는 엔진 신규 하드웨어 기능 없이 접근 가능.
- **Godot HW-RT 토대가 이미 착지** — PR **GH-99119**(4.7 dev 1)로 BLAS/TLAS·ray dispatch·RenderingDevice API 병합. 로컬 master에서 `blas_create()`/`tlas_create()`/`raytracing_pipeline_create()` 실재 확인(단 전부 `experimental`).
- **여전히 게이팅**: 샤프한 미러 반사·스킨드 메시(동적 캐릭터) GI 기여는 소프트웨어-only로 불가 → HW-RT의 **프로덕션화**에 게이팅.

**경로 (난이도순)**
- 🟢 (a) SDFGI 개선·확장 (HW 불요) → 🟢 (b) 소프트웨어-SDF 커스텀 GI(Surface Cache + radiance cache) → 🟡 (c) HW-RT GI/반사(엔진 RT 성숙 편승, Bevy Solari 참조) → 🟡 (d) Radiance Cascades(미검증).

**현실적 대안 (2026-08-18 재채점 — 충실도 비용 명시)**
- 시간대별 라이트맵 사전 베이크 + 블렌딩 → **fallback.** 동적 오클루더·동적 이미시브·미러 반사·스킨드 메시 GI 기여 중 **4/6 항목을 포기**하므로 AAA(Horizon/CP2077)에선 성립 불가.
- SDFGI를 근거리 한정 + 원거리 라이트프로브/스카이 근사 → SW-SDF 경로(b)의 일부. 단 이는 **HW-RT 경로(c)의 준비 단계**이지 종착점이 아니다.
- ~~원신급 목표엔 소프트 GI로 충분~~ → **완전 폐기.** AAA의 미러 반사·스킨드 메시 GI 기여는 소프트웨어-only로 불가능하며, HW-RT **프로덕션화**가 1급 목표다.

---

## 3. 가상 텍스처링 (Streaming Virtual Texturing) ⚠️ (변종에 따라 갈림)

> 딥리서치 결과 "❌ 일괄 불가"는 과단정으로 판명. 변종별로 난이도가 다르다.
> 상세·검증 근거: [`godot-virtual-texturing-research.md`](./godot-virtual-texturing-research.md), 실행 계획: [`godot-virtual-texturing-implementation-plan.md`](./godot-virtual-texturing-implementation-plan.md)

**무엇이 없나**
- 초대형 텍스처를 타일 단위로 GPU에 온디맨드 스트리밍하는 시스템(네이티브 미지원).

**변종별 실현 난이도**
- **하드웨어 스파스 VT** (Vulkan sparse residency / D3D12 tiled) → ❌ 순수 GDExtension 불가. Godot Vulkan 드라이버가 device 생성 시 `sparseBinding`·`sparseResidencyImage2D`를 **의도적으로 비활성화** → 코어 C++ 개조 필수.
- **소프트웨어 SVT** (고정 물리 아틀라스 + 인디렉션 텍스처) → ⚠️ **GDExtension으로 프로토타입 가능**. RenderingDevice 컴퓨트 + 논블로킹 리드백(`buffer_get_data_async`)이 피드백 패스에 그대로 쓰인다.
- **RVT식 런타임 캐시** (지형 머티리얼 블렌딩) → ⚠️ **필수지만 단독으로는 불충분**. 지형-오브젝트 블렌딩 병목에는 직접 대응하나, 캐릭터 유니크·무기 스킨·건축물 유니크 텍스처는 RVT가 커버하지 못한다 → **SW SVT 또는 HW 스파스 VT와 병행 필수.** HW 스파스는 clayjohn의 "PC에서 성능 낮음" 경고가 벤치마크 리스크일 뿐, 코어 개조 자체는 bounded(man-year 아님)로 **1급 후보 격상.**

**영향**
- 대규모 오픈월드의 텍스처 메모리 예산 관리가 수동이 된다(밉 스트리밍 수준으로 대응).

**현실적 대안 (난이도 오름차순)**
- (최소선) 텍스처 아틀라스 + 해상도 예산 수동 관리, 지역별 에셋 셋을 월드 스트리밍 셀과 로드/언로드.
- (중간) **RVT식 런타임 캐시**를 GDExtension으로 구축 → 지형 블렌딩 자동화. **1순위 권장.**
- (상한) 소프트웨어 SVT로 초대형 유니크 텍스처 스트리밍 자동화.
- (최대·고비용) HW 스파스 VT는 코어 개조로 Godot 제안 #1834에 기여.

---

## 4. 콘솔 Export (PS5 / Xbox / Switch) ⚠️ (정정됨: "기술 불가"가 아니라 "라이선스가 막음")

> **2026-08-03 정정.** 초판은 "❌ 불가"로 적었으나, 딥리서치 + 로컬 소스트리 실측 결과 **§1~3과 성격이 다르다**. 저기선 man-year 엔지니어링이 벽이었지만, 여기선 **엔진 코드는 이미 준비돼 있고 계약·법률이 벽**이다. 근거·file:line·출처는 [godot-console-export-research.md](./godot-console-export-research.md) 참조.

**무엇이 없나**
- 공식 콘솔 빌드 타깃. 원신 스케일의 상업 프로젝트엔 사실상 필수인데 Godot 코어엔 없다.

**핵심 반전 — 아키텍처는 오히려 포팅 친화적**
- 그래픽 API는 `RenderingDeviceDriver`(`servers/rendering/rendering_device_driver.h:90`, 136개 순수 가상) 단일 추상 뒤에 완전 격리. Vulkan·D3D12·Metal이 전부 이 서브클래스 → 새 콘솔 GPU 백엔드(PS5 AGC/GNM, Switch NVN)를 **상위 렌더러 무수정으로** 드라이버 쌍만 추가 가능.
- **Xbox는 구조적으로 가장 가깝다** — GDK가 D3D12 기반이고 D3D12 드라이버가 이미 오픈 레포에 병합됨(2023-12). 코어 PR이 명시적으로 "Xbox는 공식 지원 못 하지만 D3D12는 필수"라고 적음.
- Export도 `EditorExportPlatform`(`editor/export/editor_export_platform.h:51`) 순수 가상 구현으로 자동 등록(코어 수정 0).

**그럼에도 "불가"인 진짜 이유**
- 플랫폼 SDK는 NDA·라이선스로 봉인 → MIT 오픈소스 코어에 포함 불가. Godot 재단이 **정책적으로** 공식 콘솔 포트를 만들지 않는다("no NDAs, no restricted tools, no legal liability"). 코드 문제가 아니라 계약 구조 문제.

**현실적 대안 (실질 경로 둘)**
- **(1순위) W4 Games 위탁** — 세 플랫폼 전부 판매 중(Switch 2 베타), **매출 셰어·런타임 수수료 0**, 연 $800~$10,000. 원클릭 배포 + 온-디바이스 디버깅. 등록·NDA·인증 관문을 통째로 아웃소싱.
- **(직접) 폐쇄소스 platform 모듈** — 기술적으로 열려 있으나 각 플랫폼 개발자 등록(ID@Xbox 등)→NDA→SDK→인증(TRC/Lotcheck)을 스스로 통과해야. "콘솔 포팅 자체가 제품"인 조직에서만 정당화.
- **지금 할 것:** 경로와 무관하게 [준비 작업 8항목](./godot-console-export-research.md#6-오픈-영역에서-지금-미리-해둘-수-있는-준비-작업-nda-불요)(RD 백엔드 강제, 입력/세이브/실적 추상화 등)으로 "콘솔 레디" 정렬 — 순이득.

---

## 5. 고급 GPU 프로파일링 ⚠️ (정정됨: "부분 우회"가 아니라 "국소 코어 확장")

> **2026-08-03 정정.** 초판은 "⚠️ 부분 / RenderDoc·Tracy 외부 의존"으로 적었으나, 딥리서치(32소스, 24/25 3-0 검증) + 로컬 master 실측 결과 **Godot엔 이미 작동하는 인엔진 GPU 타임스탬프 프로파일러가 end-to-end로 존재**하며, "고급화"는 바닥부터가 아니라 **국소 코어 확장** 문제다. 근거·file:line·로드맵은 [godot-gpu-profiler-implementation-research.md](./godot-gpu-profiler-implementation-research.md) 참조.

**무엇이 약한가**
- Unreal Insights / Unity Deep Profiler급의 통합 GPU·메모리·프레임 분석이 없다. (단, "전무"가 아니라 "flat·부분적".)

**이미 있는 토대 (실측)**
- `RenderingDevice::capture_timestamp()` → 렌더 그래프 → Vulkan(`vkCmdWriteTimestamp`+`VkQueryPool`)/D3D12(`ID3D12QueryHeap`) 드라이버 → `RenderingServer::get_frame_profile()` → 에디터 **Visual Profiler**(`EditorVisualProfiler`) / 콘솔(`--gpu-profile`)이 GPU 구간별 시간을 실측해 그래프로 표시.
- `Performance` 싱글턴 + `RENDERING_INFO`(draw call/primitive/mem), `get_driver_resource`(네이티브 핸들), Tracy **CPU** zone 연동, breadcrumb(디바이스 로스 진단)도 존재.

**막힌 구멍 (실측 확정)**
- **Metal은 GPU 타이밍 전무**(드라이버가 전부 stub, 결과 0), GLES3도 stub.
- **계층(hierarchy) 없음** — flat 이름표 목록이라 패스 드릴다운 불가.
- **CPU↔GPU 캘리브레이션 없음** — `VK_EXT_calibrated_timestamps`/`GetClockCalibration` 미사용 → Insights식 겹친 타임라인 불가.
- **debug label이 `DEV_ENABLED`/verbose에 게이팅** → 릴리스에서 RenderDoc/Nsight에 label 안 보임.
- **Tracy GPU zone(`TracyVkZone`) 미연동**, **pipeline statistics 미노출**.

**경로 (난이도순, 순수 GDExtension 불가 — 전부 국소 코어 수정)**
- 🟢 P0 (수일): 기존 label/타임스탬프를 계층화 + Visual Profiler에 flame-graph. → 🟢 P1 (1~3주): Tracy GPU context 연동. → 🟡 P2 (2~6주): CPU↔GPU 캘리브레이션 + pipeline stats. → 🟡 P3 (수 주): Metal 타이밍 실구현(`MTLCounterSampleBuffer`).

**남는 한계**
- man-year급은 아니나(P0~P2 합쳐 1~2개월), 순수 스크립트로는 못 넘고 **코어 C++ 개조가 답**이다. AMD RGP/SQTT식 하드웨어 스레드 트레이스는 벤더 전용이라 외부 툴 연동으로만.

---

## 6. 애니메이션 고급 파이프라인 ⚠️ (정정됨: "없음/제한적"이 아니라 "훅·빌트인은 있고 4개 격차만 남음")

> **2026-08-03 정정.** 초판은 "Motion Matching·Control Rig·런타임 리타게팅·고급 블렌딩 전반이 약함"으로 적었으나, 딥리서치(113에이전트·30소스·25/25 주장 확정) + 4.8-dev 소스트리 실측 결과 **상당 부분 이미 들어와 있다.** 근거·file:line·출처는 [godot-animation-pipeline-research.md](./godot-animation-pipeline-research.md) 참조.

**핵심 반전 — 이미 있는 것**
- **모션 매칭 = 순수 GDExtension 가능.** `AnimationNodeExtension`(PR #99181, 4.4 머지, proposal #11123이 명시적으로 모션매칭 타깃)이 커스텀 애니 노드 훅을 제공하고, **동작하는 애드온 `godot-motion-matching`이 실재**(코어 포크 불요).
- **컨트롤 릭 = 대규모 IK/제약 스위트 이미 빌트인.** `TwoBoneIK3D`·`CCDIK3D`·`FABRIK3D`·`JacobianIK3D`·`SplineIK3D`·`AimModifier3D`·`CopyTransformModifier3D`·`LookAtModifier3D`·`SpringBoneSimulator3D` 등. 특수 릭은 `SkeletonModifier3D` 확장(무코어). GUI 노드그래프 저작 UX만 없음(§7 성격).
- **런타임 리타게팅 = 부분 존재.** `RetargetModifier3D`가 프로파일 이름매칭으로 매 프레임 리타게팅(초판 "import-time only"는 부정확). 단 트랙-경로 바인딩 탓에 UE IK-Rig급 이종 스켈레톤 공유 완전 패리티는 미달.

**진짜로 남은 4개 격차**
- ① **인러셜라이제이션 부재** — 블렌딩이 전부 크로스페이드. Bollo 5차 최소저크 포스트프로세스가 대량 캐릭터 전이 비용을 절반으로. → 🟢 `SkeletonModifier3D` 후처리로 GDExtension 구현. **ROI 최고.**
- ② **레이어드/본마스크 스테이트머신 부재** — 동시 2상태 블렌드만. → 🟡 커스텀 노드/모디파이어.
- ③ **완전 런타임 리타게팅 패리티** — 🟡 코어 or 확장 매핑.
- ④ **군중 스키닝 인프라 부재** — 애니 LOD·인스턴싱·MultiMesh 스켈레톤 전무. → 🔴 오프라인 VAT 우회 + 자체 애니 LOD.

**경로**
- 우선순위: 모션매칭 애드온 평가 → 인러셜라이제이션 모디파이어 자체 구현 → 빌트인 IK 활용 → 레이어드 애니 → 런타임 리타게팅 → 군중 VAT. ①②③은 GDExtension, ④는 오프라인 VAT + LOD 매니저. 상세 로드맵: [godot-animation-pipeline-research.md](./godot-animation-pipeline-research.md#71-우선순위-로드맵-난이도roi-순).

---

## 7. VFX / 시네마틱 저작 UX ⚠️ (정정됨: "렌더러 부재"가 아니라 "저작툴 미구축")

> **2026-08-03 정정.** 초판은 "저작 도구가 없다 / 코드로는 되나 생산성 병목"으로 적었으나, 딥리서치(105 에이전트, Niagara vs Godot 파티클 24/25 검증) + 로컬 소스트리 실측 결과 **§1~5와 성격이 정반대**임이 확인됐다. 저기선 **렌더러가 그 기능을 못 해서** man-year가 벽이었지만, 여기선 **런타임 시뮬 능력은 이미 기능 경쟁력 있게 존재하고 저작 UX(에디터/데이터모델)만 없다.** 근거·file:line·구현 경로는 [godot-vfx-cinematic-research.md](./godot-vfx-cinematic-research.md) 참조.

**무엇이 약한가**
- Niagara(모듈러 노드 VFX) / Sequencer(마스터 시네마틱 타임라인) 급의 아티스트 저작 도구가 없다. (로컬 grep: `sequencer`/`cutscene` 0매치, 파티클 프리셋 라이브러리 전무.)

**핵심 반전 — 시뮬 능력은 있고 저작 추상화만 없다**
- 파티클 런타임은 **컴퓨트 셰이더 GPU 시뮬 + 어트랙터/충돌(SDF)/서브에미터/벡터필드/터뷸런스/커브**가 이미 존재(`particles.glsl`). 애니메이션은 **9종 트랙(값/메서드/베지어/오디오/중첩)**과 결정론적 오프라인 렌더러 `MovieWriter`(`--write-movie`)가 존재. 없는 건 **모듈 스택·마스터 시퀀서·프리셋 라이브러리·라이브 시킹**이라는 **에디터/데이터모델 계층**.
- 따라서 **§1~5 중 진입장벽이 가장 낮다** — 순수 렌더러 개조가 아니라 **대부분 `EditorPlugin`+GDExtension으로 코어 수정 없이** 접근 가능(파티클 시킹·MovieWriter AOV 등 일부만 국소 코어 훅).

**경로 (난이도순)**
- 🟢 P0: 서드파티/콘텐츠 즉시 채택 — **Phantom Camera**(성숙, Cinemachine 대체)로 시네마틱 카메라. → 🟡 P1: **모듈 스택·노드 그래프·마스터 시퀀서 EditorPlugin 자체 구축(코어 0)** — AAA에선 필수 경로. → 🟡 P2: 파티클 시킹/리심·MovieWriter AOV/EXR 국소 코어 훅, GPU 파티클↔씬 깊이/GI, 파티클 라이팅·그림자 수광. → 🔴 P3: GPU 이벤트/데이터 인터페이스 + 유체·연기 시뮬(둘만 man-year급).

**현실적 대안 (2026-08-18 재채점 — 충실도 비용 명시)**
- ~~원신급 목표엔 P0+P1로 충분~~ → **폐기.** AAA 시네마틱(다중 액터·카메라·VFX·오디오 동시 제어)은 마스터 시퀀서 없이 물량 감당 불가. 다만 모듈 스택·시퀀서·노드 그래프는 **EditorPlugin으로 코어 0**이므로 "에디터 툴 엔지니어 1명" 구도는 유효하다 — 단 그 산출물은 AAA 저작 파이프라인이지 스타일라이즈드 축소가 아니다. 진짜 man-year는 **GPU 이벤트 인터페이스·유체 시뮬** 두 곳뿐.

---

## 8. 물리 시뮬레이션 🟡 (Jolt가 거의 전부 번들 — GPU 물리만 제외)

> **2026-08-18 신설 (AAA 기준).** 상세: [godot-physics-simulation-research.md](./godot-physics-simulation-research.md)

**핵심 — Jolt Physics가 이미 빌트인** (`modules/jolt_physics/`): 강체·영역·소프트바디·6종 조인트. **GPU 물리(🔴 P4)를 제외하면 AAA에서도 CPU Jolt로 충분**하다 — GPU 물리는 업스트림과도 충돌하는 장기 항목이고, CPU Jolt가 AAA 강체·차량·의류 시뮬을 감당한다.

| 격차 | AAA 신호 | 요점 |
|------|---------|------|
| 차량 물리 | 🟢 P0 (조건부) | Jolt 차량 12파일 이미 컴파일됨. 국소 코어 래핑(1~2주 MVP) |
| 파괴/프랙처 | 🟡 P1 (Phase C) | 조인트 break force 이미 구현(미바인딩 ~10줄). AAA 환경 파괴는 장르 관습 |
| 클로스 | 🟡 P1 (B1+B3+B4) | 굽힘+공기저항+스킨드 제약. 의류 시뮬은 XPBD 필요 — Horizon/RDR2/TLOU II 급 |
| 결정론·스냅샷·롤백 | 🟡 P1 | SCsub 1줄 + ~300줄. 넷코드 시 P0 |
| 캐릭터 컨트롤러 | 🟢 P0 | GDScript step-up 2~3일 |
| 멀티스레딩·성능 | 🟢 P0 | GDScript 스트리밍 매니저 1~2주(코어 0) |
| GPU 물리 | 🔴 P4 | CPU Jolt로 충분. 업스트림 대기 |

**AAA 재채점 반전:** 파괴(🟡 P2→P1)와 클로스(의류가 SpringBone→SoftBody B1+B3+B4로 격상)가 스타일라이즈드 근거로 강등돼 있던 것을 되돌렸다. GPU 물리만 🔴 유지.

---

## 9. 환경 시뮬 — 오션 · 비/젖음 · 대기/구름 🟡→🔴

> **2026-08-18 신설 (AAA 기준).** 상세: [오션](./godot-ocean-water-research.md) · [기상/대기](./godot-weather-atmosphere-research.md)

**오션·수면·수중 — 🟡 (GDExtension 가능).** Godot에 오션은 **개념 자체가 0**(`ocean`/`tessendorf`/`Gerstner`/`caustic`/`foam`/`refraction`/`underwater` 전부 0매치). 그러나 `CompositorEffect` + `RenderingDevice` compute API가 완비돼 있어 **FFT 오션 파이프라인 전체를 코어 수정 없이 GDExtension으로 구현 가능**하고, `godot4-oceanfft` 오픈소스가 기반을 제공한다. 진짜 병목은 오션 자체가 아니라 `buffer_get_data_async`의 1~2프레임 지연 — **부력의 GPU→CPU readback**이 async-compute/multi-queue 부재와 얽혀 있다. 1순위 우회는 **CPU-side Gerstner 근사 부력**(Sea of Thieves 방식). 공수 16~29주(그래픽스 1명).

**비/젖음/대기/구름 — 🟡→🔴 (AAA 기준 반전).** 스타일라이즈드 전제에서는 *"물리기반 대기·볼류메트릭 구름 자체가 불필요"*, *"만들지 않는 것이 이득"* 으로 종결돼 있었다. **AAA 기준에서는 둘 다 필수 항목으로 복귀**한다:
- **물리기반 대기 산란** — Bruneton 다중산란 + aerial perspective LUT(UE5 Sky Atmosphere가 쓰는 그 기법). 코어 개조 필요.
- **볼류메트릭 구름** — 레이마칭 구름(Decima/Horizon 방식). 근경 관통(비행/등반 시 구름 통과)도 AAA에선 요구.
- **햇빛·그림자** — 거리필드 소프트 섀도우·HW-RT 그림자 재개봉.
- 토대는 이미 유효: 글로벌 셰이더 유니폼 버스가 spatial/particles/sky/fog에 도달하고, `GPUParticlesCollisionHeightField3D`가 탑다운 depth FB를 이미 렌더링하며, `CompositorEffect.access_resolved_depth`가 노출돼 있다.

---

## 10. 오디오 전파 · 공간 오디오 🟡→🔴

> **2026-08-18 신설 (AAA 기준).** 상세: [godot-audio-propagation-research.md](./godot-audio-propagation-research.md)

Godot는 **3D 감쇠·오클루전·HRTF·컨볼루션·미들웨어·앰비언스 축이 다른 어떤 리서치에도 없던 별도 축**이다. 실측된 구조적 한계 3가지(AAA 규모에서 순서대로 터짐)가 있고, **실시간 회절·전파(diffraction/propagation)는 Steam Audio 미들웨어 채택의 가장 강한 근거**다. 공간 오디오의 AAA 비교 기준은 Wwise/FMOD 기반 AAA 프로덕션, Steam Audio의 실시간 회절·전파, The Last of Us / Hitman / Cyberpunk 급. 판정은 🟡(기본 3D·오클루전은 빌트인)에서 🔴(실시간 회절·전파는 미들웨어/코어)로 갈린다.

---

## 11. 기타 런타임 격차 — 모션블러 · 업스케일러 · 캐릭터 표현 · 네트워킹 · 텔레메트리 🟡

> **2026-08-18 신설 (AAA 기준).** 상세: [godot-runtime-gaps-misc-research.md](./godot-runtime-gaps-misc-research.md)

| 격차 | AAA 신호 | 요점 |
|------|---------|------|
| 모션 블러 | 🟢 (P1) | GDExtension 애드온으로 해결. AAA 포토리얼에서 필름 룩 필수 |
| 업스케일러 | 🟡 (P1~P2) | DLSS GDExtension 또는 FSR3.1 업그레이드. §1~3 안정화 후 |
| GPU 오클루전 컬링 | 🟡 | §0 폴리지와 §1 Nanite의 부분집합. 독립 과제 금지 |
| AI | 🟢 | LimboAI + 자작 EQS |
| 네트워킹 | 🟡 | 4~8인 co-op 충분. MMO는 자작 |
| 캐릭터 표현 | 🟡 | 카드형 헤어+세퍼러블 SSS가 최소선. **프리인테그레이티드 SSS LUT(Penner 2011, P1)** + **스트랜드 헤어(P3 장기, 🔴)** 로 격상 |
| 크래시/텔레메트리 | 🟢 | **Sentry GDExtension — §1 착수 전 필수** |
| 셰이더 스터터 | ✅ 해결됨 | ubershader+shader_cache(zstd). 감시만 |

**핵심:** misc 문서의 결론 *"코어 개조 인력 배분은 오배분"* 은 **AAA에서도 유지**된다 — 이 축의 엔진 격차는 0이고, 늘어나는 건 셰이더/GDExtension 작업량뿐이다. 단 SSS LUT와 스트랜드 헤어는 스타일라이즈드에서 "불필요"로 강등돼 있던 것을 AAA 기준으로 되돌렸다.

---

## 판단 기준 정리

**초판 기준 (부분 폐기).**
- ~~**에디터(GUI)가 없다** → 코드가 정공법 (범위 밖).~~ → **§0에서 반증됨.** 월드 스트리밍·지형·폴리지는 GUI만 없는 게 아니라 런타임이 없다. "GUI가 없다"는 판정을 내리기 전에 **런타임 시스템이 실재하는지 grep으로 먼저 확인**해야 한다.
- **렌더러/플랫폼/런타임이 없다** → 이 문서의 항목들. C++ 엔진 레벨 작업이거나 서드파티가 답이며, 스크립트로는 못 넘는다. **(유효)**

**AAA 기준 개정 후의 판단 축 (2026-08-18).**

| 축 | 초판 | 개정 |
|----|------|------|
| 코어 수정 필요? | 탈락 신호 | **무의미** — 포크 + 딥 개조 승인됨 |
| 아트 디렉션으로 우회 가능? | 우회 = 해결 | **할인 금지** — 우회로는 fallback으로만 기록하고 충실도 비용 명시 |
| 유일하게 남은 축 | — | **man-year급인가**, 그리고 **의존 순서상 언제 필요한가** |

### 전략적 결론 (2026-08-18 전면 개정)

**폐기된 결론.** 초판은 *"규모를 줄인 스타일라이즈드 오픈월드(사전 베이크 GI, 낮은 폴리 예산, PC/모바일 우선) → 위 격차 대부분을 아트 디렉션과 사전 계산으로 우회 가능"* 이라는 탈출로를 제시했다. **이 탈출로는 사용자 결정으로 닫혔다.** 초판이 이 탈출로를 근거로 강등한 항목들(§1 네이티브 라이팅 통합, §2 HW-RT 미러 반사·동적 캐릭터 GI, §7 Niagara/Sequencer 패리티, 물리기반 대기·볼류메트릭 구름, 헤어 스트랜드·SSS 등)은 **전부 필수 경로로 복귀한다.**

**남은 결론.**

1. **"엔진 개조 비용 과다 → Godot 부적합"은 더 이상 성립하지 않는다** — 비용 회피가 아니라 **비용 지불**이 확정된 전략이기 때문이다. 남는 질문은 *할 것인가*가 아니라 **어떤 순서로 할 것인가**다.

2. **선행 순서가 결론의 핵심이다.**
   - **§0 월드 스트리밍·지형·폴리지 런타임이 §1·§3보다 먼저다.** 스트리밍할 월드 표현이 없으면 지오메트리 스트리밍(Nanite)도 텍스처 스트리밍(VT)도 실체가 없다.
   - **크래시·텔레메트리 수집(Sentry 공식 Godot SDK)을 §1 착수 *전에* 붙일 것.** 커스텀 렌더러를 필드 크래시 수집 없이 출하하는 건 무모하다. 비용이 사실상 0이고 §1의 디버깅 가능성을 좌우한다.

3. **업스트림에서 기대할 수 있는 건 사실상 하나뿐이다 — 텍스처 스트리밍.** 공식 우선순위 페이지 전문 검색 결과 "terrain"·"partition"·"open world"·"foliage"·"nanite" **전부 0매치**. 텍스처 밉레벨 스트리밍만 proposal #3177(142👍)이 공식 우선순위에 등재되고 PR #113429(마일스톤 4.x)로 살아 있다. 지형은 reduz가 proposal #6121(180👍)에서 *"Terrain will not be added to Godot"* 로 못박고 3.5년 전 약속한 공식 GDExtension 플러그인은 존재하지 않으며, 월드 파티션 논의(#11728)는 fire가 *"stuck"*, *"probably best to move it out of the engine core"* 로 정리했고, 메시 스트리밍(#6109)엔 clayjohn이 *"Nobody works on the renderer full time… We don't have anyone lined up."* 라고 답했다.
   → **즉 §0·§1·§3의 지오메트리 쪽은 기다려도 오지 않는다. 포크에서 직접 만드는 것이 유일한 경로다.** 반대로 텍스처 스트리밍은 업스트림 작업과 중복 투자하지 않도록 주시할 것.

4. **AAA 목표에서 man-year급으로 재확인되는 항목** — 이들은 회피 대상이 아니라 **일정과 인력 계획의 대상**이다: §1 경로 C(Nanite 네이티브 라이팅 통합, full-Nanite 커밋으로 이미 확정), §2 HW-RT 프로덕션화(GH-99119의 `experimental` API를 실사용 가능하게), §0 월드 스트리밍·지형 런타임 신설.

> 각 축의 재채점 상세는 아래 §0~§11 및 링크된 개별 리서치 문서의 **"2026-08-18 개정 — AAA 기준 재채점"** 절 참조.
