# G1 — 통합 GBuffer 스키마 v1 **[동결본]**

> **상태: ❄️ 동결 (2026-08-25, v1.3) — L1·L2·L3 3레인 서명 완주. 이후 변경은 4레인 합의 + 로드맵 개정을 요구한다.**
> 이력: v1.0 초안 → 5라운드 리뷰에서 스펙 결함 4건(24-bit 절단·geo-normal 누락·히스토리 공백·MRT 포화) 동결 전 제거. 전 항목 코드/실기 근거 부착(Metal 7/7 포맷·Vulkan 3플랫폼 MRT·GB10 BLAS). 로드맵 게이트 G1(M4)의 동결 대상 문서.
> 근거 정본: [unified-gbuffer 리서치](./godot-unified-gbuffer-aov-research.md) §2 Tier 모델.
> **의견 수렴: C1(L1 리졸브) · C2(L2 vis-buffer) · C3(L3-대행, ReSTIR 소비자 관점 — Task #5 담당 자격) — 3인 서명 후 G1 동결.**
> v1.0→v1.1 델타: ① objectid 24-bit 축소(TLAS `instanceCustomIndex:24` 절단 — C3 블로커) ② `gb_geo_normal` 추가 ③ 히스토리 계약·지터 규약 신설 ④ Metal 성립성 검증을 동결 전제로 추가(C1) ⑤ 인용 정정.
> v1.2→v1.3 델타 (C1 검토): ⑩ **`gb_normal`+`gb_geo_normal`을 단일 RGBA16 unorm으로 병합**(.xy 셰이딩 oct / .zw 지오 oct — 정밀도·대역폭 동일, **슬롯 7개로 축소**) — Tier-1 8채널이 MRT 상한 8과 여유 0으로 일치해 §2의 shading-model-ID 예약이 비-Nanite 프래그먼트 emit 경로에서 실행 불가능했던 문제 해소 ⑪ §3 인용 정정(:6480) + D3D12 `InstanceID:24` 교차 근거 + DEV_ASSERT를 공통 `tlas_build`로 ⑫ §5 Metal 성립성 해소 기록(컴퓨트 리졸브 = MRT 무관) ⑬ §5.1 (i) 채택 — `gb_emission.a` = LOD 잔차 상한.
> v1.1→v1.2 델타 (C2·C3 조건부 승인 반영): ⑥ 히스토리 계약을 4종으로(`gb_geo_normal` 포함 — C3-A안: 리저버 기각은 N-1 지오노멀 필요, RTXDI `previousFrame` surface 동형) ⑦ Nanite 모션의 LOD 전환 계약 신설(C2) ⑧ G2 파급 요구 명시: 지오 풀 정점당 **노멀+UV**, 탄젠트는 리졸브 해석적 유도로 저장 불요(C2) ⑨ §3 Nanite 경로 문구 정정: vis-buffer에 instance 필드 없음 — 클러스터 ID → 가시 클러스터 레코드 **역참조**(C2).

## 1. 동결되는 것 / 동결되지 않는 것

| 동결 (변경 시 4레인 재작업) | 비동결 (레인 내부 자유) |
|---|---|
| Tier-1 어태치먼트 목록·포맷·채널 시맨틱 | 어태치먼트의 내부 생성 경로 (래스터 vs vis-buffer 리졸브) |
| `gb_objectid` **24-bit** 네임스페이스 규약 (§3) | Tier-2 export 채널 추가 (게이트 플래그 뒤라 자유) |
| 노멀 인코딩 (oct 16:16/노멀, RGBA16 unorm에 셰이딩+지오 팩) | Tier-3 라이팅 분해 write-out 목록 |
| depth 시맨틱 (R32F view-Z, `-vertex.z`) | 프리패스/컬링용 내부 버퍼 |
| **히스토리 계약** (§4) — N-1 읽기 보장 채널 목록 | 히스토리의 구현 방식 (더블버퍼 vs 카피) |
| **모션 규약** — NDC 단위 · **지터 제거(jitter-free)** | |
| `RB_SCOPE_GBUFFER` 스코프 이름 | |

## 2. Tier-1 스키마 (동결 제안, v1.3)

| # | RB tex | 포맷 | 채널 | 소비자 |
|---|--------|------|------|--------|
| 0 | `gb_albedo` | RGBA8 unorm | albedo.rgb + alpha | 리졸브·AOV |
| 1 | `gb_normal` | **RGBA16 unorm** | **.xy = 셰이딩 노멀 oct / .zw = 지오메트릭 노멀 oct** (v1.3 병합 — 두 노멀은 리저버 기각에서 항상 함께 읽힘) | 리졸브·ReSTIR·SSR·AOV |
| 2 | `gb_orm` | RGBA8 unorm | ao / roughness / metallic / sss-mask | 리졸브·ReSTIR·AOV |
| 3 | `gb_emission` | RGBA16F | emission.rgb + **a = LOD 잔차 상한(정규화 화면공간, §4.1 — v1.3에서 §5.1-(i) 채택. 클리어 값 = 0 → "잔차 없음"). 출처는 G2 §4의 `analytic_error`이며 `error`가 아니다. **비-Nanite opaque emit 경로는 `.a = 0`을 명시적으로 기록한다** — 클리어 값은 미커버 픽셀에만 적용되고, 커버된 픽셀에 안 쓰면 `.a`가 미정의다(C1) — A등급 회람 중, 2026-08-25** | 리졸브·ReSTIR·AOV |
| 4 | `gb_depth` | R32F | view-space Z (`-vertex.z`) | 위치 재구성·전 소비자 |
| 5 | `gb_objectid` | R32_UINT (**유효 24-bit**, §3) | instance ID | Nanite 리졸브·Cryptomatte·RT 히트 매칭 |
| 6 | `gb_motion` | RG16F | screen-space motion, **NDC 단위·지터 제거** | TAA·ReSTIR·AOV |

**슬롯 총 7 / MRT 상한 8 — 여유 1 확보.** 비-Nanite opaque의 프래그먼트 MRT emit이 하드캡 8이므로(**3백엔드 실측 확정**: Metal 8 · D3D12 8 · Vulkan/GB10 `maxColorAttachments=8`) 이 여유가 shading-model-ID의 실행 가능성을 담보한다. Tier-1 전 포맷 + 예약 R8_UINT의 color-attachment 성립도 Vulkan 실측 확인(C3). 주의: `R32_UINT`/`R8_UINT`는 blend 불가 — GBuffer write는 블렌딩하지 않으므로 무해하나, 해당 어태치먼트에 블렌드를 켜면 파이프라인 생성이 실패한다(기록).

- 대역폭(1080p): 4+8+4+8+4+4+4 = **36 B/px ≈ 75 MB**, 4K ≈ 299 MB — v1.2와 바이트 동일, 슬롯만 8→7. 지오 노멀 유지 근거(depth-미분 재구성의 에지 노이즈 회피)는 불변.
- depth-motion(3채널째)은 **불요 확정** — 재투영 검증은 2D 모션 + 이전 프레임 `gb_depth` 비교로 성립.
- shading-model-ID: 필요 시점(라이트맵/SH deferred 편입 — [deferred 스코핑](./deferred-transition-impact-scoping.md) §2가 그 시점을 확정)에 **8번째 슬롯의 `gb_shading_control` RG32_UINT**(.x = ID 8b|라이트맵 슬롯 8b|플래그 16b, .y = UV2 16:16)로 추가한다 — ~~신규 R8_UINT~~ 는 UV2 동반 요구를 수용 못 해 스코핑 v1.1에서 정정(전망 주석의 텍스트 정정, Tier-1 표 불변 — 스키마 재합의 불요). `gb_objectid` 상위 비트 오염 금지. v1은 미포함.

## 3. `gb_objectid` 네임스페이스 (동결 제안, v1.3 — 24-bit)

**단일 24-bit 인스턴스 ID 공간** (가용 16,777,215). 근거: TLAS 경로의 하드 제약 —
- `AccelerationStructureInstance::id`는 `uint32_t`(`rendering_device.h:1384`)이나, Vulkan 드라이버가 이를 `VkAccelerationStructureInstanceKHR::instanceCustomIndex`(**24-bit 비트필드**, `vulkan_core.h:16241`)에 무마스킹 대입(`rendering_device_driver_vulkan.cpp:6480`, `acceleration_structure_instance_write()` — v1.3 인용 정정). **D3D12도 동일**: `D3D12_RAYTRACING_INSTANCE_DESC.InstanceID : 24`(`d3d12.h:15545`) — 24-bit는 드라이버 결함이 아니라 **범용 RT API 계약**이다 → 32-bit ID는 **조용히 절단**된다.

규약:
- 저장은 R32_UINT, **상위 8비트는 계약상 reserved-zero.**
- 소비자: 비-Nanite opaque(`InstanceData.object_id` 신설) · **Nanite: vis-buffer의 클러스터 ID → 가시 클러스터 레코드 역참조로 얻은 instance ID를 리졸브가 기록**(vis-buffer 페이로드 34b는 클러스터 27b+트라이앵글 7b로 소진 — instance 필드를 넣을 자리도, 필요도 없음) · TLAS `AccelerationStructureInstance::id` — 셋이 **동일 값**. **설계 노트: objectid를 인스턴스 단위로 잡은 것은 LOD 전환에 불변이라 의도된 선택이다.**
- 센티넬: **`0x00FFFFFF`** = "no object" (sky/클리어). ~~0xFFFFFFFF~~는 24-bit 절단 시 최대 유효 ID와 충돌하므로 폐기.
- **무음 절단 금지:** ID 발급기는 2²⁴-2 초과 시 에러. 검증은 **공통 진입점 `tlas_build`(RD 상위)에서 1회**(백엔드마다 각자 절단하므로 — v1.3), 드라이버 대입부 `DEV_ASSERT`는 보조. L1 배선 요구사항으로 동결.
- `AccelerationStructureInstance::mask`(8-bit, `:1385`)는 L3의 RT 가시성 클래스 용도로 자유 — objectid와 무관.

## 4. 히스토리 계약 (동결, v1.2에서 4종으로 확장)

**N-1 프레임 읽기 보장:** `gb_depth` · `gb_normal`(병합 — 셰이딩·지오 노멀 모두 포함) · `gb_objectid` **3텍스처**(보장 채널은 v1.2의 4종과 동일, 병합으로 텍스처 수만 감소 — 더블버퍼 대상 1개 절감). 리저버 기각(surface similarity)은 *현재*와 *재투영된 N-1* 서페이스를 비교하므로 지오메트릭 노멀도 N-1이 필요하다(RTXDI `RAB_GetGBufferSurface(previousFrame=true)`가 normal·geoNormal을 함께 반환하는 것과 동형). 히스토리 비용 +4 B/px. `gb_motion`은 현재 프레임만 보장. L1은 이 3종을 트랜지언트/에일리어싱 재사용 대상에서 제외해야 한다. TAA 컬러 히스토리는 별도(기존 경로).

### 4.1 Nanite 모션·히스토리의 LOD 전환 계약 (v1.2 신설, 동결)

Nanite 경로의 모션은 **이전 프레임 트라이앵글 동일성이 아니라 인스턴스 변환(이전/현재) + 현재 LOD의 오브젝트공간 위치**에서 유도한다 — DAG 컷이 프레임마다 움직여 N 프레임의 트라이앵글은 일반적으로 N-1에 존재하지 않는다. LOD 전환 시 표면 잔차는 **해당 클러스터의 `analytic_error`(G2 §4)를 화면 투영한 값으로 상한**이 잡히며(오프라인에 이미 알려진 값), 리졸브가 이 상한을 소비자(ReSTIR 기각 판단 등)에게 전달한다.

**필드명을 박는 이유 (A등급 개정 회람 중, 2026-08-25).** G2 §4가 단일 "LOD 오차"를 **측정값 `error`(런타임 컷 전용, 타이트)** 와 **`analytic_error`(증명된 상한)** 로 분리했다. v1.2~v1.3의 이 문단은 소스를 필드명으로 특정하지 않았는데, 이제 **둘 중 `analytic_error`만 상한이다.** 더 타이트한 `error`를 `gb_emission.a`에 배선하면 ReSTIR 기각 임계가 **과소보수적**이 되어 **틀린 기각이 조용히** 일어난다 — 화면에는 아무 오류도 나타나지 않고 조명만 미묘하게 틀린다. 그래서 이 절과 §2 표 양쪽에 필드명을 명시한다. §4의 N-1 `gb_normal`(.xy 셰이딩/.zw 지오 양쪽) 보장도 같은 캐비엇과 같은 상한을 받는다.

### 4.2 G2 파급 요구 (v1.2 신설 — G2 계약에 위임하되 여기 기록)

Tier-1 7채널 중 `gb_albedo`·`gb_orm`·`gb_emission`·`gb_normal`의 리졸브는 히트 지점 머티리얼 평가를 요구한다 → **지오 풀은 정점당 노멀 + UV를 보유해야 한다.** **탄젠트는 저장하지 않는다** — vis-buffer 리졸브는 트라이앵글 3정점의 위치+UV에서 탄젠트 프레임을 해석적으로 계산한다(정점 탄젠트도 화면공간 미분도 불요). 현 S1 DAG는 위치만 보존하므로 G2 확정 시 확장한다(L2).

## 5. 동결 전제 검증 항목 (서명 전 필수)

- **[C1 제기] Metal 성립성: ✅ 해소 (2026-08-25 실측).** 채우는 경로 — 64b 이미지 아토믹 Metal 네이티브 지원 확인(훅 ①). 리졸브 경로 — 컴퓨트 리졸브는 MRT 상한과 무관(Metal storage images per stage 1,000,000 실측). 잔여 Metal 결손은 draw-indirect-count 1건(서브밋 경로 — 스키마 무관, ICB Task #10). ~~HW-래스터 폴백 변종 동결 조건~~ 삭제.

### 5.1 L1 배선 시점 결정 항목 (비차단 후속 — C3, 서명과 무관)

~~택1 보류~~ → **v1.3에서 (i) 채택 확정** (C1·C3 양측 권고 일치): `gb_emission.a` = **LOD 잔차 상한(정규화 화면공간 단위)**. §2 표에 반영됨. ReSTIR는 LOD 전환 프레임에서만 기각 임계를 확장.

참고(정보 공유, L2/L1 판단 영역): 해석적 탄젠트는 DCC 베이크 탄젠트와 미세 차이가 가능 — 노멀맵 셰이딩 차이로 나타날 수 있음(ReSTIR 블로커 아님).

## 6. v1에서 의도적으로 뺀 것 (제기되면 기각 근거)

| 요구 | 기각 근거 | 재개봉 조건 |
|---|---|---|
| anisotropy/clearcoat 채널 | Tier-2 (Substrate식 비트스트림)로 — Tier-1 대역폭 보호 | 해당 머티리얼이 opaque 대다수가 될 때 |
| world-position 채널 | depth 재구성으로 충분 (전 엔진 공통) | 없음 |
| depth-motion 채널 | §2 — 2D 모션 + N-1 depth로 성립 | 없음 |
| 라이팅 분해(diffuse/spec AOV) | GBuffer가 아니라 리졸브 패스 write-out (Tier-3) | 없음 — 구조적 |
| RGBA16F albedo 기본 | 대역폭 2배. 베이크·AOV는 8bit로 충분 | HDR albedo 워크플로 실증 시 |

## 7. 서명란

| 레인 | 담당 | 판정 | 비고 |
|---|---|---|---|
| L1 (리졸브·FB 배선) | C1 | ✅ **승인/서명 (2026-08-25, v1.3)** | Tier-1 7포맷 전부 Metal 실측(COLOR_ATTACHMENT+STORAGE 7/7) 후 서명. tlas_build 검증·emission.a 배선 L1 접수 |
| L2 (vis-buffer → 리졸브) | C2 | ✅ **v1.3 재확인 (2026-08-25)** — 리졸브 기록 8→7슬롯, 지오노멀 해석적 산출 → .zw 인코딩, emission.a 원천(클러스터 error)의 물리 상한 보호까지 확인 | v1.2 ✅ → v1.3 ✅ |
| L3-대행 (ReSTIR/RT 입력) | C3 | ✅ **v1.3 재확인 (2026-08-25)** — 병합은 ReSTIR에 개선(1페치 2노멀). MRT=8을 Vulkan/GB10에서 3플랫폼째 확증 + Tier-1 전 포맷 color-attach 실측 | v1.0 🔴 → v1.1 🟡 → v1.2 ✅ → v1.3 ✅ |
| 메인테이너 | ✅ 제안 | | |

**✅ 3인 승인 완료 (2026-08-25) — 이 문서는 G1 동결본이다.** 이후 변경은 4레인 합의 + 로드맵 개정을 요구한다.
