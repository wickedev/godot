# G1 — 통합 GBuffer 스키마 v1 동결 제안 (RFC)

> **상태: v1.1 (2026-08-25) — L3 검토 반영 개정.** 로드맵 게이트 G1(M4)의 동결 대상 문서.
> 근거 정본: [unified-gbuffer 리서치](./godot-unified-gbuffer-aov-research.md) §2 Tier 모델.
> **의견 수렴: C1(L1 리졸브) · C2(L2 vis-buffer) · C3(L3-대행, ReSTIR 소비자 관점 — Task #5 담당 자격) — 3인 서명 후 G1 동결.**
> v1.0→v1.1 델타: ① objectid 24-bit 축소(TLAS `instanceCustomIndex:24` 절단 — C3 블로커) ② `gb_geo_normal` 추가 ③ 히스토리 계약·지터 규약 신설 ④ Metal 성립성 검증을 동결 전제로 추가(C1) ⑤ 인용 정정.

## 1. 동결되는 것 / 동결되지 않는 것

| 동결 (변경 시 4레인 재작업) | 비동결 (레인 내부 자유) |
|---|---|
| Tier-1 어태치먼트 목록·포맷·채널 시맨틱 | 어태치먼트의 내부 생성 경로 (래스터 vs vis-buffer 리졸브) |
| `gb_objectid` **24-bit** 네임스페이스 규약 (§3) | Tier-2 export 채널 추가 (게이트 플래그 뒤라 자유) |
| 노멀 인코딩 (RG16 unorm octahedral) — 셰이딩·지오메트릭 공통 | Tier-3 라이팅 분해 write-out 목록 |
| depth 시맨틱 (R32F view-Z, `-vertex.z`) | 프리패스/컬링용 내부 버퍼 |
| **히스토리 계약** (§4) — N-1 읽기 보장 채널 목록 | 히스토리의 구현 방식 (더블버퍼 vs 카피) |
| **모션 규약** — NDC 단위 · **지터 제거(jitter-free)** | |
| `RB_SCOPE_GBUFFER` 스코프 이름 | |

## 2. Tier-1 스키마 (동결 제안, v1.1)

| # | RB tex | 포맷 | 채널 | 소비자 |
|---|--------|------|------|--------|
| 0 | `gb_albedo` | RGBA8 unorm | albedo.rgb + alpha | 리졸브·AOV |
| 1 | `gb_normal` | RG16 unorm oct | **셰이딩** 노멀 (노멀맵 적용 후) | 리졸브·ReSTIR·SSR·AOV |
| 2 | `gb_geo_normal` | RG16 unorm oct | **지오메트릭** 노멀 (트라이앵글 평면) | ReSTIR 레이 오프셋·리저버 기각 (RTXDI `geoNormal` 등가) |
| 3 | `gb_orm` | RGBA8 unorm | ao / roughness / metallic / sss-mask | 리졸브·ReSTIR·AOV |
| 4 | `gb_emission` | RGBA16F | emission.rgb (+a 예약) | 리졸브·AOV |
| 5 | `gb_depth` | R32F | view-space Z (`-vertex.z`) | 위치 재구성·전 소비자 |
| 6 | `gb_objectid` | R32_UINT (**유효 24-bit**, §3) | instance ID | Nanite 리졸브·Cryptomatte·RT 히트 매칭 |
| 7 | `gb_motion` | RG16F | screen-space motion, **NDC 단위·지터 제거** | TAA·ReSTIR·AOV |

- 대역폭(1080p): 4+4+4+4+8+4+4+4 = **36 B/px ≈ 75 MB**, 4K ≈ 299 MB. `gb_geo_normal` +4B는 depth-미분 재구성의 에지 노이즈(리저버 기각이 가장 중요한 지점에서 최악)를 피하는 대가로 수용 — 나중에 추가하는 것이 정확히 G1이 막으려는 재작업이므로 지금 넣는다.
- depth-motion(3채널째)은 **불요 확정** — 재투영 검증은 2D 모션 + 이전 프레임 `gb_depth` 비교로 성립.
- shading-model-ID: 필요 시점(라이트맵/SH deferred 편입)에 **신규 R8_UINT**로 추가. objectid 비트 오염 금지. v1 미포함.

## 3. `gb_objectid` 네임스페이스 (동결 제안, v1.1 — 24-bit)

**단일 24-bit 인스턴스 ID 공간** (가용 16,777,215). 근거: TLAS 경로의 하드 제약 —
- `AccelerationStructureInstance::id`는 `uint32_t`(`rendering_device.h:1384`)이나, Vulkan 드라이버가 이를 `VkAccelerationStructureInstanceKHR::instanceCustomIndex`(**24-bit 비트필드**, `vulkan_core.h:16241`)에 무마스킹 대입(`rendering_device_driver_vulkan.cpp:6448`) → 32-bit ID는 **조용히 절단**된다.

규약:
- 저장은 R32_UINT, **상위 8비트는 계약상 reserved-zero.**
- 소비자: 비-Nanite opaque(`InstanceData.object_id` 신설) · Nanite vis-buffer 리졸브 · TLAS `AccelerationStructureInstance::id` — 셋이 **동일 값**.
- 센티넬: **`0x00FFFFFF`** = "no object" (sky/클리어). ~~0xFFFFFFFF~~는 24-bit 절단 시 최대 유효 ID와 충돌하므로 폐기.
- **무음 절단 금지:** ID 발급기는 2²⁴-2 초과 시 에러, 드라이버 대입부에는 `DEV_ASSERT((id & 0xFF000000) == 0)` 추가를 L1 배선 요구사항으로 동결.
- `AccelerationStructureInstance::mask`(8-bit, `:1385`)는 L3의 RT 가시성 클래스 용도로 자유 — objectid와 무관.

## 4. 히스토리 계약 (신설, 동결)

**N-1 프레임 읽기 보장:** `gb_depth` · `gb_normal` · `gb_objectid` 3종 (ReSTIR 시간적 재사용 최소 요구). `gb_motion`은 현재 프레임만 보장. L1은 이 3종을 트랜지언트/에일리어싱 재사용 대상에서 제외해야 한다. TAA 컬러 히스토리는 별도(기존 경로).

## 5. 동결 전제 검증 항목 (서명 전 필수)

- **[C1 제기] Metal 성립성:** 스키마를 *채우는* 경로(vis-buffer 래스터)가 Metal에서 성립하는지 별도 판정 — Metal 결손 누적 중(draw-indirect-count 스텁, 64b image atomic 부재). 스키마 포맷 자체는 Metal 성립 예상이나, **"Metal에서 성립하는 리졸브 경로 존재"를 C1이 확인 후 서명**할 것. 불성립 시 Metal은 HW-래스터 폴백 변종으로 같은 스키마를 채우는 설계를 동결 조건으로 명시.

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
| L1 (리졸브·FB 배선) | C1 | ⬜ 대기 | §5 Metal 성립성 확인 포함 |
| L2 (vis-buffer → 리졸브) | C2 | ⬜ 대기 | |
| L3-대행 (ReSTIR/RT 입력) | C3 | 🟡 조건부 — v1.1 반영으로 재판정 요청 | v1.0에 🔴(24-bit 블로커·geo-normal·히스토리) → 전건 반영됨 |
| 메인테이너 | ✅ 제안 | | |

3인 승인 시 이 문서가 **G1 동결본**이 되고, 이후 변경은 4레인 합의 + 로드맵 개정을 요구한다.
