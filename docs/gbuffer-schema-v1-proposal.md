# G1 — 통합 GBuffer 스키마 v1 동결 제안 (RFC)

> **상태: v1.1 (2026-08-25) — L3 검토 반영 개정.** 로드맵 게이트 G1(M4)의 동결 대상 문서.
> 근거 정본: [unified-gbuffer 리서치](./godot-unified-gbuffer-aov-research.md) §2 Tier 모델.
> **의견 수렴: C1(L1 리졸브) · C2(L2 vis-buffer) · C3(L3-대행, ReSTIR 소비자 관점 — Task #5 담당 자격) — 3인 서명 후 G1 동결.**
> v1.0→v1.1 델타: ① objectid 24-bit 축소(TLAS `instanceCustomIndex:24` 절단 — C3 블로커) ② `gb_geo_normal` 추가 ③ 히스토리 계약·지터 규약 신설 ④ Metal 성립성 검증을 동결 전제로 추가(C1) ⑤ 인용 정정.
> v1.1→v1.2 델타 (C2·C3 조건부 승인 반영): ⑥ 히스토리 계약을 4종으로(`gb_geo_normal` 포함 — C3-A안: 리저버 기각은 N-1 지오노멀 필요, RTXDI `previousFrame` surface 동형) ⑦ Nanite 모션의 LOD 전환 계약 신설(C2) ⑧ G2 파급 요구 명시: 지오 풀 정점당 **노멀+UV**, 탄젠트는 리졸브 해석적 유도로 저장 불요(C2) ⑨ §3 Nanite 경로 문구 정정: vis-buffer에 instance 필드 없음 — 클러스터 ID → 가시 클러스터 레코드 **역참조**(C2).

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
- 소비자: 비-Nanite opaque(`InstanceData.object_id` 신설) · **Nanite: vis-buffer의 클러스터 ID → 가시 클러스터 레코드 역참조로 얻은 instance ID를 리졸브가 기록**(vis-buffer 페이로드 34b는 클러스터 27b+트라이앵글 7b로 소진 — instance 필드를 넣을 자리도, 필요도 없음) · TLAS `AccelerationStructureInstance::id` — 셋이 **동일 값**. **설계 노트: objectid를 인스턴스 단위로 잡은 것은 LOD 전환에 불변이라 의도된 선택이다.**
- 센티넬: **`0x00FFFFFF`** = "no object" (sky/클리어). ~~0xFFFFFFFF~~는 24-bit 절단 시 최대 유효 ID와 충돌하므로 폐기.
- **무음 절단 금지:** ID 발급기는 2²⁴-2 초과 시 에러, 드라이버 대입부에는 `DEV_ASSERT((id & 0xFF000000) == 0)` 추가를 L1 배선 요구사항으로 동결.
- `AccelerationStructureInstance::mask`(8-bit, `:1385`)는 L3의 RT 가시성 클래스 용도로 자유 — objectid와 무관.

## 4. 히스토리 계약 (동결, v1.2에서 4종으로 확장)

**N-1 프레임 읽기 보장:** `gb_depth` · `gb_normal` · **`gb_geo_normal`** · `gb_objectid` **4종**. 리저버 기각(surface similarity)은 *현재*와 *재투영된 N-1* 서페이스를 비교하므로 지오메트릭 노멀도 N-1이 필요하다(RTXDI `RAB_GetGBufferSurface(previousFrame=true)`가 normal·geoNormal을 함께 반환하는 것과 동형). 히스토리 비용 +4 B/px. `gb_motion`은 현재 프레임만 보장. L1은 이 4종을 트랜지언트/에일리어싱 재사용 대상에서 제외해야 한다. TAA 컬러 히스토리는 별도(기존 경로).

### 4.1 Nanite 모션·히스토리의 LOD 전환 계약 (v1.2 신설, 동결)

Nanite 경로의 모션은 **이전 프레임 트라이앵글 동일성이 아니라 인스턴스 변환(이전/현재) + 현재 LOD의 오브젝트공간 위치**에서 유도한다 — DAG 컷이 프레임마다 움직여 N 프레임의 트라이앵글은 일반적으로 N-1에 존재하지 않는다. LOD 전환 시 표면 잔차는 **해당 클러스터 LOD 오차의 화면 투영으로 상한**이 잡히며(오프라인에 이미 알려진 값), 리졸브가 이 상한을 소비자(ReSTIR 기각 판단 등)에게 전달한다. §4의 N-1 `gb_normal`/`gb_geo_normal` 보장도 같은 캐비엇과 같은 상한을 받는다.

## 4.2 G2 파급 요구 (v1.2 신설 — G2 계약에 위임하되 여기 기록)

Tier-1 8채널 중 `gb_albedo`·`gb_orm`·`gb_emission`·`gb_normal`의 리졸브는 히트 지점 머티리얼 평가를 요구한다 → **지오 풀은 정점당 노멀 + UV를 보유해야 한다.** **탄젠트는 저장하지 않는다** — vis-buffer 리졸브는 트라이앵글 3정점의 위치+UV에서 탄젠트 프레임을 해석적으로 계산한다(정점 탄젠트도 화면공간 미분도 불요). 현 S1 DAG는 위치만 보존하므로 G2 확정 시 확장한다(L2).

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
| L2 (vis-buffer → 리졸브) | C2 | 🟡 조건부(v1.1) → **v1.2 반영으로 서명 요청** | 요구 ③(LOD-모션 계약)·④(G2 노멀+UV)·①(역참조 문구) 전건 반영 |
| L3-대행 (ReSTIR/RT 입력) | C3 | 🟡 조건부(v1.1) → **v1.2 반영으로 서명 요청** | §4 히스토리 4종(A안) 반영 |
| 메인테이너 | ✅ 제안 | | |

3인 승인 시 이 문서가 **G1 동결본**이 되고, 이후 변경은 4레인 합의 + 로드맵 개정을 요구한다.
