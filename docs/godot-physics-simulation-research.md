# Godot 물리 시뮬레이션 (Jolt 통합 계층) — 딥리서치 & 기술 실행 로드맵

> [godot-openworld-engine-gaps.md](./godot-openworld-engine-gaps.md) 의 물리 항목을 실제 구현 관점에서 딥다이브한 문서.
> 대상: Godot **4.8-dev** 소스트리 (commit `eda2a482e9`), 번들 **Jolt Physics 5.6.0** (`e77f175`, MIT).
>
> **결론 요약 (한 줄):** 이 영역의 격차는 §1~5(렌더러)와 **성격이 완전히 다르다.** 렌더러 격차는 "엔진이 그 기능을 못 해서" man-year가 벽이었지만, 물리는 **필요한 C++ 코드가 이미 바이너리에 컴파일돼 들어와 있고 Godot이 호출만 안 하고 있다.** 차량·캐릭터·스냅샷·클로스 고급 제약은 전부 `thirdparty/jolt_physics/`에 있으나 `modules/jolt_physics/`에서 참조 0. 따라서 비용은 "신규 개발"이 아니라 **"바인딩 작성"** 이고, 대부분 **수백~수천 줄 규모의 국소 코어 모듈 추가**다.
>
> 예외는 단 하나 — **런타임 파괴/프랙처**는 Jolt에도 없으므로 진짜 신규 개발이다. 단 그것조차 "불가"가 아니라 "오프라인 프랙처 + 런타임 본딩"이며 필요한 런타임 프리미티브(접촉 임펄스 readback)는 **이미 GDScript에 노출돼 있다.**

---

## 0. 한 장 요약

| 항목 | 무엇이 없나 | 이미 있는 토대 | 경계 | 공수 | AAA 우선순위 |
|------|-------------|----------------|------|------|----------|
| **차량 물리** | Jolt `VehicleConstraint` 미노출. `VehicleBody3D`는 Bullet `btRaycastVehicle` 포팅(공식 문서가 "known issues, 사실적 물리용 아님"이라 명시) | Jolt 차량 12개 `.cpp` **전부 컴파일됨** (`SCsub:121-132`). 엔진/변속기/디퍼렌셜/안티롤바/타이어 슬립커브/3종 콜리전 테스터 완비 | 🟡 **국소 코어** (신규 joint type + 노드) / 순수 GDExtension은 전체 PhysicsServer 교체 시에만 | 1~2인·3~6주 (MVP 2주) | 🟢 **P0** |
| **파괴/프랙처** | 런타임 파괴 전무. `voronoi`/`fracture`/`breakable` grep **0매치** | `get_contact_impulse()`가 **이미 스크립트 바인딩됨**. Jolt joint의 `GetTotalLambda*` 기반 `get_applied_force()`가 C++엔 존재(미바인딩). `joint_clear()` 바인딩됨 | 🟢 **순수 GDExtension/GDScript** (오프라인 프랙처는 Blender). 조인트 스트레인 기반 고급 파괴만 국소 코어(10줄 바인딩) | 오프라인 파이프라인 1~2주 + 런타임 본딩 2~4주 | 🟡 **P1** (AAA 격상) |
| **클로스** | 굽힘(bend)·LRA·스킨드 제약 **전부 미사용**. drag 계수는 **no-op**. soft-vs-soft 충돌 Jolt 자체 미지원 | Jolt에 `DihedralBend`/`LRA`/`Skinned`/`Volume`/Cosserat rod 제약 전부 존재. XPBD 컴플라이언스 매핑 이미 구현. Area3D 풍력 면적분 적용 구현됨 | 🟡 **국소 코어** (`jolt_soft_body_3d.cpp` 1파일 + 서버 API 몇 줄) | B1+B3 1주, +B4 2~5주 | 🟡 **P1** (B4 AAA 격상) |
| **결정론·스냅샷** | `JPH_CROSS_PLATFORM_DETERMINISTIC` **미정의**. `StateRecorder` 참조 0. 물리 스냅샷/복원 API 전무 | `StateRecorderImpl.cpp` 컴파일됨. `JoltStream{Input,Output}Wrapper` 양방향 이미 존재. `StateRecorderFilter`로 부분 저장 가능 | 🟡 **국소 코어** (SCsub 1줄 + 서버 메서드 2개) | 1~2주 | 🟡 **P1** (넷코드 필요 시 🟢 P0) |
| **캐릭터 컨트롤러** | Jolt `CharacterVirtual` 참조 0. `PhysicsServer3D`에 `character` grep **0매치**. 계단 오르기(`stair` grep 0), 강체 밀어내기 없음 | `CharacterVirtual.cpp` 컴파일됨 — `WalkStairs`/`StickToFloor`/`ExtendedUpdate`/`SaveState`/캐릭터-vs-캐릭터/`mMaxStrength` 전부 구현돼 있음 | 🟡 **국소 코어** (신규 서버 API + 노드) / 계단만이면 🟢 GDScript 우회 | 2~4주 (계단 우회는 2일) | 🟢 **P0** |
| **멀티스레딩·성능** | 물리 바디 스트리밍/활성 영역 개념 없음(`physics_streaming`/`activation_region`/`physics_lod` grep 각 0). `max_bodies` 기본 10240 재시작 필요 | `JoltJobSystem`이 **`WorkerThreadPool` 정상 사용**. `AddBodiesPrepare/Finalize` 배치 삽입 이미 사용. `DISABLE_MODE_REMOVE`로 노드 단위 제거 가능 | 🟢 **순수 GDScript/GDExtension** (스트리밍 매니저) | 1~2주 | 🟢 **P0** (저비용 고효과) |
| **GPU 물리** | 파티클 충돌은 **단방향** — 월드가 반응 안 함. GPU→CPU는 AABB 전체 스톨 리드백 1개뿐. ~~Godot이 `Jolt/Compute`·`Shaders`·`Physics/Hair`를 번들에서 제외~~ [2026-08-25: 3폴더 벤더링 완료(병합 대기) — 단 Compute는 헤어 전용 추상화라 GPU 게임플레이 물리는 여전히 부재] | `buffer_get_data_async` 바인딩됨(§Nanite 문서). SDF/Heightfield 콜라이더 존재 | 🔴 **대규모 코어** (실제 양방향) / 🟡 근사 대체는 GDScript | — | 🔴 **P4 (비권장)** |

**전략적 결론 미리보기:** 🟢 P0 3개(캐릭터 계단 우회 → 물리 스트리밍 → 차량)가 AAA에서도 최우선 과제다. **AAA 재평가(2026-08-18):** 기존 "원신급" 목표에서는 런타임 파괴·클로스·GPU 물리가 낮은 우선순위였으나, AAA 포토리얼 목표에서는 파괴(§3)와 클로스(§4)의 우선순위가 상승한다. GPU 물리(§8)는 여전히 P4 — AAA에서도 CPU Jolt로 충분하고 업스트림과 충돌하기 때문. **[2026-08-25 갱신: GPU 게임플레이 물리는 자체 솔버(Wave 4)이며 upstream 충돌 개념은 소멸 — §8(e) 참조.]**

---

## 1. 대전제 — Jolt는 "일부만" 번들된 게 아니라 "거의 전부" 번들돼 있다

이 문서 전체를 관통하는 사실이며, 초판 가설("Jolt에 없는 기능이라 못 한다")을 뒤집는 근거다.

`thirdparty/README.md` (jolt_physics 항목):
```
Version: 5.6.0 (e77f175595e64cb44218cc9d9d56fc365ad0e36a, 2026)
Files extracted from upstream source:
- All files in `Jolt/`, except `Jolt/Jolt.cmake`, any files dependent on
  ENABLE_OBJECT_STREAM ..., and the `Jolt/Physics/Hair/`, `Jolt/Compute/`
  and `Jolt/Shaders/` folders.
```

즉 **제외된 것은 딱 3개 폴더**(Hair, Compute, Shaders — 전부 Jolt의 신규 GPU 경로)이고, **차량·캐릭터·소프트바디·StateRecorder는 전부 포함**된다. 그리고 이들은 빌드에서 빠진 것도 아니다:

| Jolt 기능 | `modules/jolt_physics/SCsub` (= 실제 컴파일됨) | Godot 코드에서의 참조 |
|-----------|-----------------------------------------------|----------------------|
| Vehicle (12파일) | `:121-132` — `VehicleConstraint.cpp`, `WheeledVehicleController.cpp`, `TrackedVehicleController.cpp`, `MotorcycleController.cpp`, `VehicleEngine.cpp`, `VehicleTransmission.cpp`, `VehicleDifferential.cpp`, `VehicleAntiRollBar.cpp`, `VehicleCollisionTester.cpp`, `VehicleTrack.cpp`, `Wheel.cpp`, `VehicleController.cpp` | **0** (`grep -rni "vehicle" modules/jolt_physics/ --exclude=SCsub` → 0매치) |
| Character (3파일) | `:53-55` — `Character.cpp`, `CharacterBase.cpp`, `CharacterVirtual.cpp` | **0** |
| StateRecorder | `:46` — `StateRecorderImpl.cpp` | **0** (`grep -rn "StateRecorder" modules/ servers/ scene/` → SCsub 1줄뿐) |
| DeterminismLog | `:40` — `DeterminismLog.cpp` | **0** |
| SoftBody (고급 제약) | 포함 | 부분 — 엣지 제약만 사용, 굽힘/LRA/스킨드 미사용 |

⇒ **바이너리에 코드는 이미 있다. 링커가 dead-strip할 뿐이다.** 따라서 이 문서의 모든 항목에서 "구현"이란 **알고리즘 개발이 아니라 래퍼/바인딩 작성**을 뜻한다. 이건 §1(Nanite)이나 §2(Lumen)와 근본적으로 다른 비용 구조다.

### 1.1 경계를 결정하는 두 가지 구조적 사실

**(a) GDExtension은 Godot 모듈의 `JPH::PhysicsSystem`에 도달할 수 없다.**
`PhysicsServer3D`에는 네이티브 핸들 탈출구가 없다 — `servers/physics_3d/physics_server_3d.h`에서 `native_handle`/`get_driver_resource`/`void *` grep **0매치**. 렌더러의 `RenderingServer.get_driver_resource()` 같은 우회로가 물리엔 없다. ⇒ 애드온이 RID를 받아 Jolt 객체를 조작하는 건 **불가능**.

**(b) 대신 GDExtension은 물리 서버를 통째로 갈아끼울 수 있다.**
- `PhysicsServer3DExtension` (`servers/physics_3d/physics_server_3d_extension.h`)에 **`EXBIND` 244개 + `GDVIRTUAL` 27개** — 물리 서버 전체 인터페이스가 확장 가능.
- `PhysicsServer3DManager::register_server()`가 **스크립트 바인딩됨** (`servers/physics_3d/physics_server_3d_manager.cpp:94`).
- 실제로 `modules/godot_physics_3d/register_types.cpp:57`, `modules/jolt_physics/register_types.cpp:60`이 같은 API로 등록한다.

⇒ **"순수 GDExtension으로 Jolt 차량을 쓰는" 유일한 방법은 자체 Jolt 사본을 번들해 물리 서버 전체를 재구현하는 것**이다(godot-jolt 확장이 4.4 인트리화 이전에 하던 방식). 기술적으로 열려 있으나, 빌트인 Jolt 모듈과 이중 유지보수가 되므로 **딥 코어 개조가 허용된 이 프로젝트에서는 명백히 열등한 선택**이다. 아래 모든 항목에서 "국소 코어"를 권장하는 이유다.

---

## 2. 차량 물리 🟢 P0

### (a) 무엇이 없나
- `modules/jolt_physics/joints/`에 vehicle constraint **없음** (파일 목록 6종: pin·hinge·slider·cone_twist·6dof + base).
- `PhysicsServer3D`의 `JointType` enum은 5종에서 닫혀 있음 — `servers/physics_3d/physics_server_3d_enums.h:139-144` (`JOINT_TYPE_PIN`, `HINGE`, `SLIDER`, `CONE_TWIST`, `6DOF`, `MAX`).
- Godot 코드 어디서도 `JPH::VehicleConstraint`를 참조하지 않음(검색어: `VehicleConstraint`, `WheeledVehicleController`, `TrackedVehicleController`, `MotorcycleController`, `Physics/Vehicle` → SCsub 12줄 외 0).
- `physics_system->AddStepListener()` 호출 **0매치** — `VehicleConstraint`는 `PhysicsStepListener`를 상속하므로 등록 훅이 필요한데 현재 없음.

### (b) Godot `VehicleBody3D`의 실제 한계 (실측)

`scene/3d/physics/vehicle_body_3d.{h,cpp}` — Bullet `btRaycastVehicle`의 **직역 포팅**이다. 내부 구조체 이름이 그대로 남아 있다: `struct btVehicleWheelContactPoint` (`vehicle_body_3d.h:214`), `_resolve_single_bilateral` (`:225`), `_calc_rolling_friction` (`:226`), `_update_suspension` (`:229`), `_ray_cast` (`:230`).

동작 모델:
- 바퀴당 **레이캐스트 1개** (`VehicleBody3D::_ray_cast`, `vehicle_body_3d.cpp:457`) → 접촉점 1개. 연석·돌·계단에서 바퀴가 폭을 못 느껴 관통·튐.
- 서스펜션은 스프링-댐퍼 스칼라(`m_suspensionStiffness`/`m_wheelsDampingCompression`/`m_wheelsDampingRelaxation`/`m_maxSuspensionForce`, `:90-98`).
- 타이어 마찰은 `m_frictionSlip` **단일 스칼라 + 임펄스 클램프**(`_update_friction`, `:729`). 슬립 비율/슬립 앵글 커브 없음.
- **엔진·변속기·디퍼렌셜 없음.** `engine_force`는 바퀴에 직접 가하는 힘이고, 공식 문서가 명시한다: *"The simulation does not take the effect of gears into account, you will need to add logic for this"* (`doc/classes/VehicleBody3D.xml`).
- 공식 경고: *"**This class has known issues and isn't designed to provide realistic 3D vehicle physics.** If you want advanced vehicle physics, you may have to write your own physics integration using CharacterBody3D or RigidBody3D."* (같은 파일)
- 안티롤바 없음. `MODEL_FRONT` 축 고정.

### (c) Jolt 업스트림 차량의 실제 능력 (로컬 헤더 실측)

**`VehicleConstraint`** (`thirdparty/jolt_physics/Jolt/Physics/Vehicle/VehicleConstraint.h`)
| 능력 | 근거 |
|------|------|
| 임의 개수 바퀴 + 개별 설정 | `mWheels` `:34` (`Array<Ref<WheelSettings>>`) |
| **안티롤바** | `mAntiRollBars` `:35`, `GetAntiRollBars()` `:167` |
| 전복 방지 (피치/롤 각 제한) | `mMaxPitchRollAngle` `:33` |
| 임의 up/forward 축 | `mUp` `:31`, `mForward` `:32` |
| **노면별 마찰 오버라이드 콜백** | `CombineFunction` `:85` — `(wheelIndex, ioLongitudinalFriction, ioLateralFriction, body2, subShapeID2)`. 서브셰이프 단위로 아스팔트/자갈/진흙 마찰 분기 가능 |
| **스냅샷/복원** | `SaveState`/`RestoreState` `:199-200` |
| 콜리전 테스트 빈도 조절(LOD) | `SetNumStepsBetweenCollisionTestActive` `:174` |
| 컨트롤러 교체 | `mController` `:36` (`VehicleControllerSettings`) |

**콜리전 테스터 3종** (`VehicleCollisionTester.h`) — `VehicleCollisionTesterRay` `:84` / `VehicleCollisionTesterCastSphere` `:105` / **`VehicleCollisionTesterCastCylinder` `:128`**. 마지막이 실제 바퀴 형상 스윕이라 연석·계단 품질이 레이캐스트와 차원이 다르다. Godot `VehicleBody3D`엔 레이 하나뿐.

**`WheeledVehicleController`** (`WheeledVehicleController.h`)
| 능력 | 근거 |
|------|------|
| **엔진** (RPM·토크 커브·관성) | `mEngine` `:82` (`VehicleEngineSettings`) |
| **변속기** (기어박스·클러치·자동/수동) | `mTransmission` `:83` (`VehicleTransmissionSettings`) |
| **디퍼렌셜 배열** | `mDifferentials` `:84` (`Array<VehicleDifferentialSettings>`) |
| **LSD (제한 슬립 디퍼렌셜)** | `mDifferentialLimitedSlipRatio` `:85` (기본 1.4, `FLT_MAX`=오픈 디퍼렌셜) |
| **타이어 종방향 마찰 커브** | `mLongitudinalFriction` `:34` — `LinearCurve`, X축=slip ratio `(ω·r − v)/|v|` |
| **타이어 횡방향 마찰 커브** | `mLateralFriction` `:35` — X축=slip angle(도) |
| 브레이크/핸드브레이크 토크 분리 | `mMaxBrakeTorque` `:36`, `mMaxHandBrakeTorque` `:37` |
| 조향각 제한 | `mMaxSteerAngle` `:33` (기본 70°) |
| 드라이버 입력 4채널 | `SetDriverInput(forward, right, brake, handBrake)` `:105` |

**추가 컨트롤러:** `TrackedVehicleController`(무한궤도 — 탱크/굴착기), `MotorcycleController`(린 앵글 자동 제어). 둘 다 컴파일돼 있음.

⇒ **이건 UE Chaos Vehicles / Unity WheelCollider와 같은 급의 모델**이다. 레이캐스트 vs 실린더 캐스트, 스칼라 마찰 vs 슬립 커브, 무(無) 파워트레인 vs 엔진+변속기+LSD — 원신급 이동 수단(글라이더는 무관, 차량·보트·마운트)에 충분하고도 남는다.

### (d) 노출 공수와 경계

**경계: 🟡 국소 코어 (모듈 내부 완결)** — GDExtension 불가(§1.1a), 대규모 포크 불필요.

작업 목록:
1. `servers/physics_3d/physics_server_3d_enums.h`에 `JOINT_TYPE_VEHICLE` 추가 — **또는** joint 계열을 건드리지 않고 별도 `vehicle_*` RID 계열 API 신설(호환성 측면에서 후자가 안전).
2. `modules/jolt_physics/joints/jolt_vehicle_constraint_3d.{h,cpp}` 신설 — 기존 `jolt_joint_3d.h` 패턴 그대로. `JoltSpace3D`에 `AddStepListener` 호출 1줄 추가(`jolt_space_3d.cpp:190` `step()` 주변).
3. `PhysicsServer3D` 가상 함수 추가 → `physics_server_3d_extension.h`에 `EXBIND` 추가 → `physics_server_3d_dummy.h`/`godot_physics_3d` 스텁 구현(GodotPhysics3D는 미지원 에러 반환).
4. `scene/3d/physics/jolt_vehicle_body_3d.{h,cpp}` 신설 노드 + `VehicleWheel3D` 대응 노드. 기존 `VehicleBody3D`는 **건드리지 않는다**(호환성).
5. `doc/classes/` XML 2~4개.

**공수 추정:** 기존 `jolt_generic_6dof_joint_3d.cpp`(가장 큰 조인트, ~600줄)가 벤치마크. 차량은 파라미터 표면적이 더 넓다(엔진 커브·기어비 배열·바퀴 배열).
- **MVP** (WheeledVehicleController + Ray tester + 하드코드 파라미터): 1인 **1~2주**.
- **프로덕션** (3종 테스터, 전 파라미터 노출, 커브 리소스, 에디터 기즈모, 문서, TrackedVehicle): 1인 **1~2개월**.

> ⚠️ 이 추정은 검증된 claim이 아니라 기존 조인트 코드 규모 대비 규모감이다.

### (e) 현실적 대안

| 대안 | 충실도 비용 | 평가 |
|------|-----------|------|
| **(즉시) `VehicleBody3D` + 자체 파워트레인 GDScript** | **중간** — 타이어 슬립 물리 품질이 저하됨. 레이캐스트 1개로 연석·계단·오프로드에서 관통 발생. AAA 차량엔 부적합 | 기어/RPM/토크 곡선은 스크립트로 `engine_force` 변조해 흉내 가능 |
| **(중간) `RigidBody3D` + `_integrate_forces` 자체 서스펜션** | **중간** — 셰이프 캐스트로 접촉 품질은 개선되나 힘 기반이라 고속·급제동 안정성이 Jolt 솔버 레벨 제약보다 떨어짐 | 바퀴마다 `PhysicsDirectSpaceState3D.cast_motion`(구/실린더 셰이프 캐스트)로 접촉 검출. 순수 GDScript |
| **(Jolt) 국소 코어 래핑 (권장)** | **없음** — AAA 수준 차량 물리. 엔진+변속기+LSD+실린더 캐스트+슬립 커브 완비 | Jolt 차량 12파일 이미 컴파일. 국소 코어(1~2주 MVP) |
| **(아트 디렉션) 캐릭터 기반 이동** | **높음** — 차량 없는 게임 디자인. AAA 오픈월드(GTA/Cyberpunk/RDR2)에서는 차량이 핵심이므로 이 대안은 AAA에서 유효하지 않음 | 마운트(탈것)는 `CharacterBody3D` 캡슐 + 애니메이션으로. 원신은 이 방식을 채택했으나 AAA 포토리얼 오픈월드에선 한계 |

### (f) 우선순위: 🟢 **P0 — 단, "차량을 쓸 것인가"에 조건부**
차량이 게임 디자인에 들어간다면 최우선(공수 대비 품질 점프가 이 문서에서 가장 큼). 안 들어가면 **P4로 강등**.

---

## 3. 파괴 / 프랙처 🟡 P2

### (a) 무엇이 없나 — 검색으로 확정
`grep -rni "voronoi|fracture|destructib|breakable" --include='*.cpp' --include='*.h'` (thirdparty 제외) → **28매치 전부 `std::is_trivially_destructible`** (core/templates/*, core/config/project_settings.h). **실질 0매치.**

- 런타임 프랙처 시스템 없음.
- 파괴 가능 에셋 타입 없음 (UE Geometry Collection 대응물 없음).
- `Joint3D` 노드에 **break force/torque 속성 없음** — 바인딩 목록이 `node_a`/`node_b`/`solver_priority`/`exclude_nodes_from_collision`/`get_rid` 5개뿐 (`scene/3d/physics/joints/joint_3d.cpp:217-235`).
- **Jolt에도 없다.** Jolt 5.6.0 트리에 프랙처/보로노이 헬퍼 없음. Jolt `Constraint` 기본 클래스에도 break threshold 개념이 없다(`SetEnabled` on/off만).

### (b) 이미 있는 토대 — 여기가 반전 지점

파괴 시스템의 런타임 코어는 결국 **"본드(joint) + 손상 누적 + 임계 초과 시 본드 해제"** 다. 그 3요소가 이미 있다:

| 프리미티브 | 상태 | 근거 |
|-----------|------|------|
| **접촉 임펄스 readback** | ✅ **스크립트 바인딩됨** | `PhysicsDirectBodyState3D::get_contact_impulse(idx)` (`servers/physics_3d/direct_states/physics_direct_body_state_3d.h:96`). Jolt 구현이 `JPH::EstimateCollisionResponse`로 실제 임펄스 추정 (`jolt_contact_listener_3d.cpp:212-228`) — 마찰 임펄스까지 합산(`:227-228`) |
| 접촉 리포팅 제어 | ✅ | `body_set_max_contacts_reported` (`physics_server_3d.h:233`) |
| **조인트 스트레인(반력) readback** | ⚠️ **C++엔 있으나 스크립트 미바인딩** | `JoltPhysicsServer3D::{pin,hinge,slider,cone_twist,generic_6dof}_joint_get_applied_{force,torque}` (`jolt_physics_server_3d.h:465-501`), 내부는 Jolt `GetTotalLambdaPosition()/GetTotalLambdaRotation() / last_step` (`jolt_hinge_joint_3d.cpp:352-376`). **그런데 `JoltPhysicsServer3D::_bind_methods()`가 빈 함수** (`jolt_physics_server_3d.h:120` — `static void _bind_methods() {}`) → GDScript에서 호출 불가 |
| 조인트 해제 | ✅ | `PhysicsServer3D::joint_clear(RID)` (`physics_server_3d.h:320`), 또는 `Joint3D` 노드 `queue_free()` |
| 바디 대량 삽입/제거 | ✅ | `AddBodiesPrepare/Finalize` 이미 사용 (`jolt_space_3d.cpp:456-464`) |
| 조각 렌더링 | ✅ | `MultiMeshInstance3D`, 씬 인스턴싱 |

⇒ **UE Chaos Destruction의 알고리즘 골격(오프라인 보로노이 프랙처 → 조각 그래프 → 본드 손상 전파 → 임계 초과 시 클러스터 해제)은 오늘 Godot에서 재현 가능하다.** 없는 것은 **저작 도구와 데이터 모델**이지 런타임 능력이 아니다 — 이건 §7(VFX) 문서가 발견한 것과 정확히 같은 패턴이다.

### (c) 실현 경로

**Phase A — 오프라인 프랙처 (코어 0, 엔진 밖)**
- Blender **Cell Fracture** 애드온(빌트인, 보로노이)으로 메시를 조각내 glTF로 익스포트. 조각 인접성(공유 면)을 스크립트로 계산해 본드 리스트를 JSON/Resource로 동봉.
- 또는 C++ 모듈에 보로노이 라이브러리 링크. **주의: `voro++`는 LGPL/커스텀 라이선스라 MIT 트리 인트리화에 부적합** — 오프라인 툴(에디터 전용 또는 외부 CLI)로 격리하는 게 안전하다. CGAL도 GPL/LGPL 이중. ⇒ **런타임 인트리 링크는 피하고 임포트타임/외부 툴로 격리**가 정답.

**Phase B — 런타임 본딩 (순수 GDScript/GDExtension, 코어 0)**
- 조각들을 `RigidBody3D` + `freeze = true` (`FREEZE_MODE_STATIC`)로 시작 (`scene/3d/physics/rigid_body_3d.h:43-44,150`).
- 인접 조각 쌍에 `Generic6DOFJoint3D`(전 축 잠금)를 본드로 생성.
- `contact_monitor`를 켜고 `_integrate_forces`에서 `get_contact_impulse()` 합을 조각별 damage에 누적.
- damage > threshold → 해당 조각의 본드 `queue_free()` + `freeze = false`. 이웃으로 damage를 감쇠 전파(Chaos의 strain propagation 등가).
- **성능 상한 관리:** `max_bodies` 기본 10240 (`jolt_project_settings.cpp:71`, `GLOBAL_DEF_RST` = 재시작 필요). 조각 풀링 + 정적 복귀 필수.

**Phase C — 조인트 스트레인 기반 파괴 (국소 코어, ~10줄)**
- 위 접촉 임펄스 방식은 "맞은 조각"만 안다. 진짜 구조 붕괴(다리·건물)는 **조인트 반력**을 봐야 한다. 이미 구현된 `*_joint_get_applied_force/torque`를 노출하면 된다:
  - (최소) `JoltPhysicsServer3D::_bind_methods()`에 `ClassDB::bind_method` 10줄 추가 → GDScript에서 `PhysicsServer3D`를 Jolt로 캐스트해 호출.
  - (권장) `PhysicsServer3D`에 `joint_get_applied_force/torque` 가상 함수 추가 + `EXBIND` + GodotPhysics3D 스텁 → `Joint3D`에 `break_force`/`break_torque` 속성과 `broken` 시그널 추가. **업스트림 기여 가치가 큰 작은 PR.**

### (d) 대응물 대조 + 충실도 비용

| 대안 | 충실도 비용 | 비고 |
|------|-----------|------|
| **Phase B (접촉 임펄스 기반, GDScript)** | **중간** — "맞은 조각"만 감지. 구조적 붕괴(다리·건물)는 부정확. 소규모 파괴(울타리·유리·기둥)에는 충분 | Phase A(오프라인 프랙처) + Phase B(런타임 본딩)로 UE Chaos 등가 달성 |
| **Phase C (조인트 스트레인 기반, 국소 코어)** | **낮음** — 구조적 붕괴 정확도 상승. 조인트 반력 기반으로 다리·건물·라그돌 관절 파열까지 자연스럽게 처리 | AAA 환경 파괴에 필수적인 경로. Phase B와 결합 시 최고 품질 |
| **UE5 Chaos Destruction** | **없음** (기준) | 오프라인 보로노이 + 런타임 strain 전파. Phase A+B+C로 등가 달성 가능 |
| **사전 제작 애니메이션 + 파티클** | **높음** — AAA 환경 파괴의 자유도·반응성·물리적 설득력이 크게 제한됨. 사전 제작 한계를 벗어난 상호작용 불가 | 원신 방식. 원신급 타깃에선 유효하나 AAA 포토리얼 오픈월드에선 부적합 |

⇒ **세 상용 솔루션 모두 "런타임에 메시를 자르지" 않는다.** 전부 오프라인 프랙처 + 런타임 본드 관리다. 이 사실이 "Godot에서 불가"라는 직관을 무력화한다.

### (e) Jolt의 SoftBody/breakable 관련 실측
- Jolt `Constraint`에 break 기능 **없음** — `SetEnabled(false)`로 끄는 것이 전부 (`jolt_joint_3d.cpp:75`에서 이미 사용).
- Jolt `SoftBodySharedSettings::mEdgeConstraints`는 `Array`라 런타임에 원소를 제거하면 **찢어짐(tearing)** 구현이 이론상 가능하나, `SoftBodySharedSettings`는 `RefConst`로 공유되고 `Optimize()`로 사전 정렬된 상태라(`jolt_soft_body_3d.cpp:229`) 실시간 수정은 재생성 비용을 유발. 실용성 낮음.

### (f) 우선순위: 🟡 **P2** (AAA 재평가: 🟡 **P1**)

**AAA 재평가 (2026-08-18):** 기존 문서는 "원신은 런타임 파괴가 없다"는 근거로 P2로 배치했으나, AAA 포토리얼 오픈월드(Red Faction Guerrilla, Battlefield, Just Cause, Horizon, Cyberpunk 급)에서 **환경 파괴는 사실상 장르 관습**이다. 단 전체 파괴 시스템보다 **Phase C(조인트 break force)**가 **저비용·범용**이라 P1으로 격상 — 라그돌 관절 파열, 로프 끊김, 매달린 오브젝트 낙하, 가벼운 환경 파괴(울타리·유리·기둥)에 두루 쓰인다. Phase A+B(오프라인 프랙처 + 런타임 본딩)는 P2 유지 — 게임 디자인이 대규모 파괴를 요구할 때만 진입.

---

## 4. 클로스 (SoftBody) 🟡 P1

### (a) 무엇이 없나

`modules/jolt_physics/objects/jolt_soft_body_3d.cpp` (857줄) 실측 결과, **Jolt가 제공하는 제약 6종 중 1종만 쓰고 있다.**

```cpp
// jolt_soft_body_3d.cpp:223
settings->CreateConstraints(&vertex_attrib, 1, JPH::SoftBodySharedSettings::EBendType::None);
```

`EBendType::None` — **굽힘 제약을 아예 만들지 않는다.** 이것이 Godot 천이 "종잇장처럼 접히고 주름이 안 잡히는" 근본 원인이다.

| Jolt 제약 (`SoftBodySharedSettings.h`) | Godot 사용 |
|---------------------------------------|-----------|
| `mEdgeConstraints` (거리) `:349` | ✅ 유일하게 사용 |
| **`mDihedralBendConstraints`** (2면각 굽힘) `:350` | ❌ `EBendType::None`으로 명시적 비활성 |
| `mVolumeConstraints` (사면체 체적) `:351` | ❌ (체적은 `mPressure` 근사로만) |
| **`mSkinnedConstraints`** (스켈레톤 스키닝에 구속) `:352` | ❌ — 의류의 핵심 |
| **`mLRAConstraints`** (Long Range Attachment) `:354` | ❌ — 늘어짐 방지의 핵심 |
| `mRodStretchShearConstraints` / `mRodBendTwistConstraints` (Cosserat rod) `:355-356` | ❌ — 머리카락/끈 |

추가 실측 격차:
- **`drag_coefficient`는 완전한 no-op.** 코드 주석이 자백한다:
  ```cpp
  // jolt_soft_body_3d.cpp:643-650
  float JoltSoftBody3D::get_drag() const {
      // Drag is not a thing in Jolt, and not supported by Godot Physics either.
      return 0.0f;
  }
  void JoltSoftBody3D::set_drag(float p_drag) { /* 빈 함수 */ }
  ```
  그런데 `SoftBody3D`엔 `drag_coefficient` 속성이 인스펙터에 노출돼 있다(`scene/3d/physics/soft_body_3d.cpp:397`). **속성은 보이는데 아무 효과가 없다.** 공기 저항이 없어 자유낙하 시 천이 비현실적으로 빠르다.
- **소프트바디 vs 소프트바디 충돌 미지원 (Jolt 자체 한계).** 업스트림 코드에 TODO가 명시:
  ```cpp
  // thirdparty/jolt_physics/Jolt/Physics/SoftBody/SoftBodyMotionProperties.cpp:147
  if (body.IsRigidBody() // TODO: We should support soft body vs soft body
  ```
  ⇒ 캐릭터가 망토와 치마를 동시에 입으면 서로 관통한다.
- **스킨드 메시 연동 없음.** `SoftBody3D`의 부착 수단은 정점별 `NodePath` 핀뿐 (`soft_body_3d.h:75-83` `struct PinnedPoint { NodePath spatial_attachment_path; ... }`). 즉 옷을 캐릭터에 붙이려면 `BoneAttachment3D`에 정점을 하나씩 핀해야 하고, **max distance / backstop 페인팅 개념이 없다.**
- **셀프 콜리전 노출 없음** — `SoftBodyCreationSettings`에도 셀프 콜리전 플래그가 없다(`mFacesDoubleSided` `:72`가 전부). 옷이 자기 자신을 통과한다.
- **LOD 없음.** `simulation_precision`(=`mNumIterations`) 하나만 있고 거리 기반 자동 조절 없음.

### (b) 이미 있는 토대 (의외로 탄탄)

- **XPBD 컴플라이언스 매핑이 이미 정교하게 구현돼 있다** — Godot의 PBD stiffness(0~1)를 Jolt XPBD compliance로 수학적으로 유도하는 주석 20줄 + 구현 (`jolt_soft_body_3d.cpp:190-220`). 여기에 `vertex_attrib.mCompliance`/`mShearCompliance`가 이미 세팅되므로, `mBendCompliance` 추가 + `EBendType::Distance`(또는 `Dihedral`)로 바꾸는 것만으로 굽힘이 켜진다.
- **풍력이 면적분으로 제대로 적용된다** (`_apply_environmental_forces`, `:232-311`): 삼각형별 중심·법선·면적을 계산하고, `Area3D`의 wind 소스로부터 거리 감쇠(`pow(projection, -attenuation)`)와 법선 투영(`normal.dot(wind_direction)`)을 적용해 정점 3개에 임펄스 분배. 이건 상용 클로스의 압력 모델과 같은 형태다.
- `Area3D` 풍력 API: `wind_force_magnitude`/`wind_attenuation_factor`/`wind_source_path` (`scene/3d/physics/area_3d.h:62-64`).
- `mVertexRadius`가 프로젝트 설정으로 노출됨 (`jolt_project_settings.cpp:44`, 기본 0.01m) — z-fighting 방지용 두께.
- 서버 API 표면 자체는 넓다 (`physics_server_3d.h:255-314`, 소프트바디 함수 30개).
- **`shrinking_factor`** — Jolt 원본에 없는 Godot 확장(`:224-227`에서 `mRestLength *= multiplier`). 천을 수축시켜 몸에 밀착시키는 실용적 트릭이 이미 있다.

### (c) 의류급(Chaos Cloth / NvCloth) 대비 격차

| 기능 | Chaos Cloth / NvCloth | Jolt 능력 | Godot 노출 |
|------|----------------------|-----------|-----------|
| 솔버 | XPBD | ✅ XPBD | ✅ |
| 거리 제약 | ✅ | ✅ | ✅ |
| 굽힘 제약 | ✅ (bending stiffness) | ✅ `DihedralBend` | ❌ |
| **Max Distance 페인팅** | ✅ (핵심 워크플로) | ~ `LRA` + `Skinned`로 등가 | ❌ |
| **Backstop** | ✅ | ~ `Skinned` + 내부 셰이프로 근사 | ❌ |
| 셀프 콜리전 | ✅ | ❌ (soft-vs-soft 미지원) | ❌ |
| 애니메이션 드리븐(스키닝 블렌드) | ✅ | ✅ `SkinVertices()` + `mEnableSkinConstraints` (`SoftBodyMotionProperties.h:94,140`) | ❌ |
| 공기 저항/양력 | ✅ | ❌ (풍압만) | ❌ (no-op) |
| LOD | ✅ | ❌ | ❌ |
| GPU 솔버 | Chaos는 CPU+ISPC / NvCloth는 GPU(사실상 EOL) | ❌ (upstream `Compute/`는 헤어 전용 — 클로스/강체 GPU 솔버는 upstream에도 부재. 벤더링 여부와 무관) | ❌ |

⇒ **격차의 대부분이 "Jolt엔 있는데 Godot이 안 씀"이고, 진짜 없는 건 셀프 콜리전·공기저항·LOD 3개다.**

### (d) 경계와 공수

**경계: 🟡 국소 코어 — 파일 1~2개.**

| 단계 | 작업 | 파일 | 공수 |
|------|------|------|------|
| **B1** | `EBendType::Dihedral` + `mBendCompliance` 노출 (`bending_stiffness` 서버 파라미터 신설) | `jolt_soft_body_3d.cpp` + `physics_server_3d.h`(+EXBIND, 스텁) + `soft_body_3d.cpp` + XML | **2~4일** |
| **B2** | `mLRAConstraints` 활성화 (핀 정점 기준 최대 거리) — 늘어짐 방지 | 위와 동일 | 2~3일 |
| **B3** | 공기 저항 구현 — `_apply_environmental_forces`의 면 루프에 상대속도 항 추가(`F = -0.5·ρ·Cd·A·(n·v)·|v|·n`). Jolt 수정 불요, Godot 코드만 | `jolt_soft_body_3d.cpp:232-311` | **1~2일. drag 속성이 이미 인스펙터에 있으니 죽은 속성을 살리는 셈 — ROI 최고.** |
| **B4** | 스킨드 제약 — `Skeleton3D` 본 행렬을 매 프레임 `SkinVertices()`에 전달, `mSkinnedConstraints` + max distance 정점 가중치 | 신규 서버 API + `SoftBody3D` 스켈레톤 연동 | **2~4주** (데이터 모델·임포트 워크플로 포함) |
| **B5** | 셀프 콜리전 | ~~Jolt 업스트림 작업 필요~~ [2026-08-25 확정: upstream 5.6.0 미지원 — 자체 구현(L5)] | 🔴 자체 구현 |
| **B6** | 클로스 LOD (거리별 `simulation_precision` 조절 / 원거리 스프링본 대체) | 순수 GDScript | 2~3일 |

### (e) 현실적 대안 — AAA에서의 전략

**AAA 재평가 (2026-08-18):** 기존 문서는 "원신은 클로스 시뮬을 거의 안 쓴다"는 근거로 SpringBone 대체를 권장했으나, AAA 포토리얼 캐릭터(Horizon, RDR2, TLOU Part II, Cyberpunk)에서 의류 시뮬레이션은 **캐릭터 존재감의 핵심 요소**다. SpringBone은 머리카락·장식 흔들림에 적합하지만, 치마·망토·코트의 자연스러운 드레이프와 주름은 **XPBD 클로스 시뮬레이션이 필요**하다.

**AAA 권장 전략 (충실도 비용 포함):**

| 대안 | 충실도 비용 | 적용 대상 |
|------|-----------|----------|
| **SpringBone만** | **높음** — 치마·망토·코트에서 드레이프·주름이 없음. AAA 캐릭터 존재감에 치명적. 원신급 타깃에서만 유효 | 머리카락·장식 흔들림 (AAA에서도 충분) |
| **SoftBody B1+B3 (굽힘+공기저항)** | **중간** — 드레이프는 자연스러우나 스킨드 제약 없이 캐릭터에 부착 불가. 환경 천(깃발·커튼)에만 충분 | 환경 오브젝트 |
| **SoftBody B1+B3+B4 (굽힘+공기저항+스킨드)** | **낮음** — AAA 캐릭터 의류 시뮬레이션의 최소 요구선. UE Chaos Cloth 대비 셀프 콜리전만 결여 | **캐릭터 의상 (권장)** |
| **UE Chaos Cloth / NvCloth** | **없음** (기준) | 셀프 콜리전 + LOD 포함. B5(셀프 콜리전)는 자체 구현 확정(upstream 미지원 실측) |

### (f) 우선순위: 🟡 **P1** (AAA 재평가: 유지)
B1+B3(굽힘+공기저항)은 P1, B4(스킨드 제약)는 AAA에서 P1으로 격상, B6(클로스 LOD)는 P2.

---

## 5. 결정론 · 스냅샷 · 롤백 넷코드 🟡 P1

### (a) 무엇이 없나
- Godot이 물리 상태를 저장/복원하는 API **전무**. `snapshot|save_state|restore_state|rollback` grep → `JoltPhysicsServer3D::dump_debug_snapshots` 관련 12매치뿐이고, 이건 **디버그용 씬 덤프**(`PhysicsScene::SaveBinaryState`, `jolt_space_3d.cpp:557-586`)라 **시뮬레이션 상태(속도·접촉 캐시·슬립 상태)를 포함하지 않는다.** 롤백에 못 쓴다.
- `StateRecorder` 참조 0 (검색어: `StateRecorder`, `SaveState`, `RestoreState` in `modules/ servers/ scene/` → SCsub 1줄뿐).
- **`JPH_CROSS_PLATFORM_DETERMINISTIC` 미정의.** `modules/jolt_physics/SCsub`의 `CPPDEFINES`는 `JPH_ENABLE_ASSERTS`(dev), `JPH_DEBUG_RENDERER`(editor), `JPH_DOUBLE_PRECISION`(precision=double)뿐 (`SCsub:150-157`).
  - 결과: `Jolt/Core/Core.h:133-135,175-182`가 `#ifndef JPH_CROSS_PLATFORM_DETERMINISTIC` 가드 하에 **FMA를 켠다** (`__ARM_FEATURE_FMA`, `__FMA__`, `__AVX2__`). Jolt 주석이 명시: *"FMA is not compatible with cross platform determinism"*.
  - ⇒ **현재 Godot 빌드는 x86 ↔ ARM(맥/모바일) 간 물리 결과가 갈라진다.**

### (b) 이미 있는 토대 — 롤백에 필요한 모든 조각이 있다

| 프리미티브 | 상태 | 근거 |
|-----------|------|------|
| **`PhysicsSystem::SaveState/RestoreState`** | ✅ 컴파일됨 | `PhysicsSystem.h:165,168` — `SaveState(StateRecorder&, EStateRecorderState = All, const StateRecorderFilter* = nullptr)` |
| 바디 단위 저장 | ✅ | `SaveBodyState`/`RestoreBodyState` `PhysicsSystem.h:171,174` |
| **부분 저장 필터** | ✅ | `StateRecorderFilter::ShouldSaveBody / ShouldSaveConstraint / ShouldSaveContact` (`StateRecorder.h:88,91,94`) + `ShouldRestoreContact` `:101` — **롤백 대상 바디만 저장**하는 데 정확히 맞는 API |
| 저장 범위 선택 | ✅ | `EStateRecorderState` 비트마스크 (`StateRecorder.h:20-76`) — Global/Bodies/Contacts/Constraints 선택 |
| **자기 검증 모드** | ✅ | `StateRecorder::mIsValidating` (`:114,120`) — 재현 시 상태가 갈라지는 지점을 자동 검출 |
| `CharacterVirtual` 상태 | ✅ | `CharacterVirtual.h:496-497` `SaveState/RestoreState` override |
| `VehicleConstraint` 상태 | ✅ | `VehicleConstraint.h:199-200` |
| **스트림 어댑터 (양방향)** | ✅ **이미 Godot에 존재** | `JoltStreamOutputWrapper : JPH::StreamOut` + `JoltStreamInputWrapper : JPH::StreamIn` (`modules/jolt_physics/misc/jolt_stream_wrappers.h:42,58`). `StateRecorder`는 `StreamIn`+`StreamOut` 다중상속(`StateRecorder.h:109`)이므로 **`PackedByteArray` 백엔드로 바꾸는 것만 남았다** |
| 결정론 진단 로그 | ✅ 컴파일됨 | `DeterminismLog.cpp` (`SCsub:40`) |

⇒ **롤백 넷코드에 필요한 엔진측 능력이 사실상 전부 준비돼 있고, 없는 건 `StateRecorder` ↔ `PackedByteArray` 어댑터와 서버 메서드 2개다.**

### (c) 결정론 보장 조건 (실측 + 업스트림 설계)

| 조건 | Godot 현황 |
|------|-----------|
| 동일 바이너리/플랫폼 | ✅ Jolt는 **스레드 수와 무관하게** 결정론적으로 설계됨(잡 시스템이 결과 순서를 고정). Godot의 `JoltJobSystem`은 `WorkerThreadPool`에 위임하되 Jolt의 배리어 구조를 그대로 쓰므로 이 성질이 유지된다 |
| 동일 Jolt 버전·빌드 플래그 | ⚠️ 사용자 프로젝트가 `precision=double` 여부에 따라 `JPH_DOUBLE_PRECISION` 유무로 갈림 (`SCsub:156-157`) |
| **크로스 플랫폼 (x86 ↔ ARM)** | ❌ `JPH_CROSS_PLATFORM_DETERMINISTIC` 미정의 → FMA 활성 |
| **바디 ID 결정성** | ⚠️ 주의 필요 — Jolt 문서가 body ID 순서 의존을 경고. Godot은 RID 할당 순서에 의존하므로 **씬 로드 순서가 같아야** 함. `CharacterVirtualSettings`엔 결정론용 ID 지정 필드가 있다(`CharacterVirtual.h:63` 부근 주석: *"For a deterministic simulation, it is important to have a deterministic body ID"*) |
| 고정 타임스텝 | ✅ `physics_ticks_per_second` 기본 60 (`main/main.cpp:2179`), `max_physics_steps_per_frame` 8 (`:2180`). 단 프레임 초과 시 스텝 수가 달라짐 → 넷코드에선 직접 스텝 구동 필요 |
| **콜리전 스텝 수 하드코딩** | ⚠️ `physics_system->Update(p_step, **1**, temp_allocator, job_system)` (`jolt_space_3d.cpp:196`) — collision steps가 1로 고정. 30Hz 틱에선 2가 권장이나 조절 불가 |

### (d) 경계와 공수

**경계: 🟡 국소 코어 (아주 작음).**

1. **크로스 플랫폼 결정론 (1줄 + 검증):**
   `SCsub`에 `env_jolt.Append(CPPDEFINES=["JPH_CROSS_PLATFORM_DETERMINISTIC"])`. 성능 손실(FMA·일부 SIMD 경로 포기)이 있으므로 **프로젝트 설정 또는 SCons 옵션으로 게이팅**하는 게 옳다. → **1일 + 벤치마크.**
2. **스냅샷 API (~300줄):**
   - `JoltStateRecorder : JPH::StateRecorder`를 `PackedByteArray` 위에 구현 (기존 `jolt_stream_wrappers.h` 패턴 복제 — 반나절).
   - `PhysicsServer3D`에 `space_save_state(RID, ...) -> PackedByteArray` / `space_restore_state(RID, PackedByteArray) -> bool` 가상 추가 + `EXBIND` + GodotPhysics3D 스텁.
   - 부분 저장은 `StateRecorderFilter` 서브클래스에 RID 집합을 받아 구현.
   - → **1~2주.**
3. **콜리전 스텝 수 노출:** `jolt_space_3d.cpp:196`을 프로젝트 설정으로 → **1시간.**

### (e) 현실적 대안
- **롤백 대신 상태 동기 + 보간(원신 방식).** 원신은 롤백 넷코드가 아니다 — 서버 권위 + 클라이언트 예측/보간의 협동 멀티플레이다. 물리 결정론 요구가 낮다.
- 롤백이 필요해도 **물리 바디를 롤백 대상에서 빼는 설계**(캐릭터 이동은 결정론적 커스텀 코드, 물리는 장식)가 격투/FPS 롤백 게임의 표준 관행이다.
- 커뮤니티 롤백 애드온(netfox 등)은 노드 프로퍼티 단위 저장/복원 프레임워크를 제공하지만, **물리 엔진 내부 상태(접촉 캐시·슬립·솔버 warm start)는 복원하지 못한다** — 정확히 이 갭이 (d)-2로 메워진다.

### (f) 우선순위: 🟡 **P1** — 단 (d)-1과 (d)-3은 **비용이 사실상 0이므로 즉시 처리 권장.**

---

## 6. 캐릭터 컨트롤러 🟢 P0

### (a) 무엇이 없나 — 검색으로 확정
- `PhysicsServer3D`에 캐릭터 관련 API **전무**: `grep -n "character\|Character" servers/physics_3d/physics_server_3d.h` → **0매치**.
- Jolt `CharacterVirtual`/`Character` 참조 **0** (SCsub 3줄 제외).
- **계단 오르기 없음:** `grep -rni "stair" scene/ servers/ modules/jolt_physics/` → **0매치**.
- **동적 강체 밀어내기 없음:** `character_body_3d.cpp`에서 `apply_impulse`/`apply_central_impulse` grep → **0매치** (`push_back`은 `Vector` 메서드). 즉 캐릭터가 상자를 밀 수 없다.

### (b) `CharacterBody3D`가 실제로 쓰는 것

`scene/3d/physics/character_body_3d.cpp` (968줄) — **물리 엔진과 무관한 순수 Godot 알고리즘**이다. `body_test_motion()` (셰이프 캐스트 + 리커버리)을 반복 호출하는 slide 루프:
- `_move_and_slide_grounded` `:140` / `_move_and_slide_floating` `:402`, `max_slides` 루프 `:165,412`.
- Jolt 쪽 구현은 `JoltPhysicsServer3D::body_test_motion` `:958` → `JoltPhysicsDirectSpaceState3D::body_test_motion` — 즉 **Jolt는 셰이프 캐스트 백엔드로만 쓰이고, 캐릭터 로직은 Godot이 직접 짠다.**

**이미 잘 되는 것:**
- 이동 플랫폼: `platform_rid`/`platform_velocity`/`platform_angular_velocity`를 `body_get_direct_state()->get_velocity_at_local_position()`으로 취득 (`:77-82`), 플랫폼 이탈 시 속도 계승 정책 3종(`platform_on_leave`, `:939-942`). **회전 플랫폼까지 지원 — 여기는 AAA급에 근접.**
- 경사: `floor_max_angle`, `floor_stop_on_slope`, `floor_constant_speed`, `floor_block_on_wall`, `floor_snap_length` (`:932-937`).
- 천장 밀어내림 처리 `:159,184-188`.

**부족한 것:**
- **계단.** slide 루프는 수직 벽을 옆으로 미끄러뜨릴 뿐 위로 올려주지 않는다. `floor_snap_length`는 내려가는 것만 처리. → 커뮤니티가 매번 GDScript로 step-up을 재구현하는 유명한 문제.
- **강체 밀어내기.** 위 grep 0매치.
- **캐릭터 vs 캐릭터.** 서로 통과하거나 일반 충돌만.
- 예측 접촉(predictive contact) 없음 → 고속 이동 시 벽 관통·끼임.

### (c) Jolt `CharacterVirtual`이 주는 것 (로컬 헤더 실측)

`thirdparty/jolt_physics/Jolt/Physics/Character/CharacterVirtual.h`

| 기능 | 근거 |
|------|------|
| **계단 오르기 (전용 알고리즘)** | `CanWalkStairs(velocity)` `:400`, `WalkStairs(dt, stepUp, stepForward, stepForwardTest, stepDownExtra, ...)` `:414` |
| **바닥 흡착** | `StickToFloor(stepDown, ...)` `:426` |
| **통합 업데이트 (계단+흡착+플랫폼 일괄)** | `ExtendedUpdate(dt, gravity, ExtendedUpdateSettings, ...)` `:451` |
| **이동 플랫폼 지반 속도** | `UpdateGroundVelocity()` `:458` |
| **동적 바디 밀기** | `mMass` `:37` + `mMaxStrength` `:40` (뉴턴 단위 최대 밀기 힘) + `mInnerBodyShape` `:61` (실제 강체를 심어 남이 나를 밀 수 있게) |
| **관통 복구 속도** | `mPenetrationRecoverySpeed` `:55` (0=복구 안 함, 1=1스텝 완전복구) |
| **예측 접촉** | `mPredictiveContactDistance` `:47` — 슬라이딩 방향을 미리 계산해 끼임 방지 |
| **내부 엣지 제거** | `mEnhancedInternalEdgeRemoval` `:339-340` — 삼각형 메시 이음새 걸림 제거 |
| **캐릭터 vs 캐릭터** | `mCharacterVsCharacterCollision` `:696` |
| 히트 수 제한·병합 | `mMaxNumHits` `:53` (256), `mHitReductionCosMaxAngle` `:54` |
| 백페이스 정책 | `mBackFaceMode` `:46` |
| 런타임 셰이프 교체(웅크리기) | `SetShape(shape, maxPenetrationDepth, ...)` `:469` — 관통 검사 포함 |
| 활성 접촉 리스트 | `GetActiveContacts()` `:511` |
| 반복 횟수 튜닝 | `mMaxCollisionIterations` `:48`(5), `mMaxConstraintIterations` `:49`(15) |
| **스냅샷** | `SaveState/RestoreState` `:496-497` |

⇒ 이건 UE `CharacterMovementComponent` / Unity `KinematicCharacterController`와 **같은 급의 완성된 컨트롤러**이며, 이미 컴파일돼 있다.

**`Character` vs `CharacterVirtual`:** 전자는 실제 `Body`를 만들어 물리 시스템이 밀어주는 방식(간단·저렴, 정밀도 낮음), 후자는 `PhysicsSystem`이 추적하지 않고 사용자가 직접 업데이트하는 스윕 기반(정밀·튜닝 가능). 주석이 명시: *"A CharacterVirtual is not tracked by the PhysicsSystem so you need to update it yourself"* (`:266`). 게임플레이 캐릭터는 후자가 정답.

### (d) 경계와 공수

**경계: 🟡 국소 코어.** (§1.1a — GDExtension은 Jolt 객체에 못 닿음)

작업:
1. `modules/jolt_physics/objects/jolt_character_3d.{h,cpp}` 신설 — `CharacterVirtual` 소유 + `_pre_step`에서 `ExtendedUpdate` 호출.
2. `PhysicsServer3D`에 `character_*` API 신설(create/set_space/set_shape/set_up/move/get_ground_state/…) + `EXBIND` + GodotPhysics3D 스텁.
3. `scene/3d/physics/jolt_character_body_3d.{h,cpp}` 신설 노드. **기존 `CharacterBody3D`는 유지**.
4. 문서.

**공수:** 1인 **2~4주** (MVP 1주). Jolt 샘플의 `CharacterVirtual` 사용 코드가 사실상 레퍼런스 구현이라 설계 리스크가 낮다.

### (e) 현실적 대안 — 여기가 중요
- **계단만이면 GDScript로 2일.** `move_and_slide` 실패 시 (1) `move_and_collide`로 `step_height`만큼 위로 → (2) 원래 수평 이동 재시도 → (3) 아래로 스냅. 커뮤니티 검증된 패턴이고 `CharacterBody3D`를 상속해 override하면 된다. **먼저 이걸 하고, 부족할 때 (d)로 간다.**
- **강체 밀어내기도 GDScript.** `get_slide_collision(i).get_collider()`가 `RigidBody3D`면 `apply_central_impulse()`. 20줄.
- **캐릭터 vs 캐릭터**는 원신급 협동(4인)에선 서로 통과시키는 게 오히려 관례.

⇒ **(e)만으로 원신급 캐릭터 무브먼트의 90%가 나온다.** (d)는 "AAA 폴리시"를 살 때의 옵션.

### (f) 우선순위: 🟢 **P0** — 단 **먼저 (e) GDScript 우회(2~3일)**, 품질 부족이 실측되면 (d).

---

## 7. 멀티스레딩 · 성능 · 대규모 오픈월드 🟢 P0

### (a) 실측 — `WorkerThreadPool` 통합은 이미 제대로 돼 있다

`modules/jolt_physics/spaces/jolt_job_system.{h,cpp}` — Jolt `JobSystemWithBarrier`를 상속한 커스텀 구현이며, Godot의 스레드풀에 정확히 위임한다:

```cpp
// jolt_job_system.cpp:105
task_id = WorkerThreadPool::get_singleton()->add_native_task(&_execute, this, true, task_name);
// :72
WorkerThreadPool::get_singleton()->wait_for_task_completion(task_id);
// :162
thread_count(MAX(1, WorkerThreadPool::get_singleton()->get_thread_count()))
```
- `GetMaxConcurrency()` `:108` → Godot 스레드 수 반환.
- `FixedSizeFreeList<Job>` + lock-free 완료 리스트(`Job::push_completed`/`pop_completed`)로 잡 재활용.
- `DEBUG_ENABLED` 시 잡별 타이밍을 수집해 `EngineDebugger`로 전송 (`jolt_job_system.h:73-79`).

⇒ **물리 틱은 실제로 병렬 실행된다.** Jolt의 broadphase/narrowphase/solver island가 Godot 워커 스레드로 퍼진다. 이건 렌더러 격차 문서들에 비하면 **드물게 "이미 잘 돼 있는" 항목**이다.

**추가로 이미 있는 것:**
- **배치 삽입/제거:** `AddBodiesPrepare`/`AddBodiesFinalize`를 실제로 사용 (`jolt_space_3d.cpp:456-464`), sleeping/awake 두 큐로 분리. **스트리밍 친화적.**
- 솔버 반복 횟수 설정: `velocity_steps`(10)/`position_steps`(2) (`jolt_project_settings.cpp:37-38`).
- 대형 정적 지오메트리 전용 브로드페이즈 레이어: `BODY_STATIC_BIG` (`jolt_broad_phase_layer.h`) — 지형처럼 거대한 정적 바디를 분리해 쿼리 비용을 낮춤. **오픈월드 지형에 정확히 필요한 최적화가 이미 들어 있다.**
- 별도 물리 스레드 옵션: `physics/3d/run_on_separate_thread` (`core/config/project_settings.cpp:1797`) → `PhysicsServer3DWrapMT`로 커맨드 큐잉.
- `precision=double` 빌드 시 `JPH_DOUBLE_PRECISION` 자동 (`SCsub:156-157`) → **대형 월드 좌표 정밀도 대응 존재.**

### (b) 무엇이 없나

| 격차 | 근거 |
|------|------|
| **물리 바디 스트리밍 / 활성 영역** | `physics_streaming`/`activation_region`/`physics_lod` grep 각 **0매치**. Jolt엔 `ActivateBodiesInAABox(box, bpFilter, objFilter)`(`BodyInterface.h:144`)와 `DeactivateBodies`(`:146`)가 있으나 **Godot 미노출** |
| **`max_bodies` 하드 캡** | 기본 **10240**, `GLOBAL_DEF_RST` = **재시작 필요** (`jolt_project_settings.cpp:71`). 대형 오픈월드에서 상향은 가능하나 런타임 조절 불가. `max_body_pairs` 65536 `:72`, `max_contact_constraints` 20480 `:73`도 동일 |
| **콜리전 스텝 하드코딩** | `Update(p_step, 1, ...)` (`jolt_space_3d.cpp:196`) |
| **브로드페이즈 재최적화 미호출(런타임)** | `physics_system->OptimizeBroadPhase()`가 **서버 비활성 시에만** 호출됨 (`jolt_space_3d.cpp:439`, 에디터 뷰포트 대응). 대량 스트리밍 인 직후 명시적 재최적화 훅 없음 (Jolt 쿼드트리가 매 스텝 증분 갱신하므로 치명적이진 않음) |
| 오버플로 시 조용한 품질 저하 | `EPhysicsUpdateError` 3종을 `WARN_PRINT_ONCE`로만 보고 (`:198-217`) — 릴리스 빌드에서 접촉이 소리 없이 누락될 수 있음 |

### (c) 경계와 공수

**물리 스트리밍은 🟢 순수 GDScript/GDExtension으로 가능하다.** 필요한 모든 프리미티브가 노출돼 있다:
- `CollisionObject3D::DISABLE_MODE_REMOVE` (`collision_object_3d.h:46`) + `Node.process_mode = DISABLED` → 물리 월드에서 바디 제거/복귀.
- `RigidBody3D.freeze` + `FREEZE_MODE_STATIC` (`rigid_body_3d.h:43,150`) → 원거리 동적 바디를 정적화.
- `PhysicsServer3D::body_set_state(BODY_STATE_SLEEPING/CAN_SLEEP)` (`physics_server_3d_enums.h:123-124`) → 강제 슬립.
- 씬 청크 로드/언로드는 `ResourceLoader.load_threaded_request` + `PackedScene`.

설계:
```
StreamingRegion (Area3D 또는 그리드 셀)
  ├ 플레이어 거리 < R_near  → 씬 인스턴스 활성, 동적 바디 정상
  ├ R_near ~ R_far          → 동적 바디 freeze(STATIC), 정적 콜라이더 유지
  └ > R_far                 → 노드 트리에서 제거(DISABLE_MODE_REMOVE) 또는 청크 언로드
```
→ **1~2주, 코어 수정 0.** 이 문서에서 **투입 대비 효과가 가장 높은 항목.**

**국소 코어로 추가하면 좋은 것 (합쳐 2~3일):**
- `space_set_param`으로 `max_bodies` 런타임 재구성(Jolt는 `PhysicsSystem::Init` 시점 고정이라 실제로는 공간 재생성 필요 → 대신 **경고 임계치 노출**이 현실적).
- 콜리전 스텝 수 프로젝트 설정화.
- `ActivateBodiesInAABox` / `DeactivateBodies` 노출 (`space_activate_bodies_in_aabb(RID, AABB)`).
- `EPhysicsUpdateError`를 시그널/`Performance` 카운터로 노출.

### (d) 우선순위: 🟢 **P0** (GDScript 스트리밍 매니저). 코어 훅은 🟡 P2.

---

## 8. GPU 물리 / 대량 시뮬 ~~🔴 P4 (비권장)~~ → **🟡 스코프 내 (2026-08-25 반전)**

> ⛔ **[2차 정정 — 2026-08-25 upstream 감사 후 최종]** 1차 반전(“벤더링하면 GPU 물리 확보”)은 **절반만 맞았다.** upstream `Jolt/Compute`는 범용 GPU 물리가 아니라 **헤어 솔버 전용 컴퓨트 추상화**다 — `Physics/` 전체에서 소비자가 `Hair/` 하나뿐, GPU 리지드바디/브로드페이즈/콜리전/소프트바디 전무(C4 upstream 실측).
> **현재 상태(벤더링 완료 후):** `Jolt/{Compute,Shaders,Physics/Hair}` 3폴더는 **포크에 벤더링됐고**(c4/l5-jolt-vendoring, CPU 백엔드 스모크 통과) 감사 기준은 “upstream `e77f1755` + `thirdparty/README.md` 열거 패치”. SoftBody 셀프 콜리전은 **소스 감사로 미지원 확정**(스파이크 완료 — 더 이상 미검증 아님).
> **판정:** 스트랜드 헤어 시뮬 = 벤더링으로 확보(CPU 경로, GPU는 G5+HLSL 툴체인 선행). **GPU 게임플레이 물리 = 여전히 자체 솔버**(Wave 4, Jolt/Compute 추상화를 토대로만 재사용). 정본: [로드맵](./godot-openworld-implementation-roadmap.md) §15-B.

### (a) 실측 — 파티클 충돌은 완전한 단방향이다

`servers/rendering/renderer_rd/shaders/particles.glsl`의 충돌 루프(`:496-670`)는 콜라이더 4종(구/박스/SDF/하이트필드)을 순회해 `collision_normal`/`collision_depth`를 계산하고, 그 결과를 **파티클 프로세스 셰이더의 내장 변수로만 넘긴다** (`:657-665`, 이후 `#CODE : PROCESS` `:672`).
- **월드로 되돌아가는 쓰기가 전혀 없다.** 피드백 버퍼도, 카운터도 없다.
- SDF 콜라이더는 `GPUParticlesCollisionSDF3D`로 **오프라인 베이크**(`_compute_sdf` CPU 멀티스레드, `gpu_particles_collision_3d.h:163-164`), 해상도 최대 512³ (`:101-108`).
- 하이트필드만 `UPDATE_MODE_ALWAYS`로 동적 갱신 가능 (`:223-225`), 최대 8192² (`:213-220`).

### (b) GPU→CPU 응답 경로

**존재하는 유일한 경로가 전체 스톨 리드백이다:**
```cpp
// servers/rendering/renderer_rd/storage_rd/particles_storage.cpp:657-667
AABB ParticlesStorage::particles_get_current_aabb(RID p_particles) {
    ...
    Vector<uint8_t> buffer = RD::get_singleton()->buffer_get_data(particles->particle_buffer);
```
- `buffer_get_data`는 동기 리드백 = full GPU stall. `RenderingServer::particles_get_current_aabb`로 **스크립트 바인딩됨** (`rendering_server.cpp:2754`).
- 반환값은 **AABB 하나**. 파티클별 위치/충돌 이벤트는 얻을 수 없다.
- 파티클 버퍼 RID 자체가 외부에 노출되지 않으므로, `buffer_get_data_async`(Nanite 문서에서 바인딩 확인됨)를 걸 대상이 없다.

⇒ **GPU 파티클은 게임플레이 물리와 상호작용할 수 없다.** 파티클이 세계를 느끼기만 하고, 세계는 파티클을 모른다.

### (c) ~~Jolt의 GPU 경로는 Godot이 의도적으로 제외했다~~ [2026-08-25: 벤더링 완료(병합 대기) — 단 그 경로의 정체가 달랐다]

`thirdparty/README.md`가 명시하듯 번들에서 `Jolt/Compute/`, `Jolt/Shaders/`, `Jolt/Physics/Hair/` 3개 폴더가 빠져 있다. 즉 **업스트림 Jolt 5.6에는 GPU 컴퓨트 경로와 헤어 시뮬레이션이 존재하지만 Godot은 가져오지 않았다.**

이건 두 가지를 뜻한다:
1. (부정) 오늘 Godot에서 Jolt GPU 경로를 쓸 수 없다.
2. ~~(긍정) 업스트림이 그 방향으로 가고 있으므로 번들 정책만 바꾸면 따라갈 수 있다. 자체 GPU 물리는 업스트림과 정면충돌한다.~~ **[2026-08-25 기각]** upstream 감사 결과 `Jolt/Compute`는 헤어 전용 추상화 — 업스트림에 GPU 게임플레이 물리 방향성 자체가 없다. 자체 솔버(Wave 4)가 충돌하는 대상은 존재하지 않으며, 벤더링된 추상화는 그 솔버의 토대로 재사용한다.

### (d) 경계와 대안

| 목표 | 경계 | 평가 |
|------|------|------|
| 파티클이 월드에 힘을 가함 | 🔴 대규모 코어 — 파티클 버퍼에 피드백 섹션 추가 + 비동기 리드백 + 물리 서버 연결. 렌더러·물리 양쪽 개조 | ~~**비권장.** 원신도 안 한다~~ → **스코프 내.** 원신은 하한 참조점일 뿐. G5(async compute) 이후 리드백 비용이 내려가면 착수 |
| GPU 강체 시뮬 | 🔴 **자체 솔버 (Wave 4)** | ~~벤더링으로 확보~~ **[2차 정정]** upstream `Jolt/Compute`는 헤어 전용 추상화 — GPU 리지드바디는 존재하지 않음. 자체 솔버를 그 추상화 위에 구축(외부 VkDevice 주입·순수가상 얼로케이터 재사용) |
| **대량 오브젝트 "물리처럼 보이는" 연출** | 🟢 GDScript | 낙엽·잔해·물결은 **셰이더 버텍스 애니메이션 + GPUParticles**로. 물리 시뮬 불필요 |
| **대량 오브젝트 실제 물리** | 🟢 CPU Jolt로 충분 | Jolt는 수천 바디를 60fps로 돌린다. `max_bodies`만 올리고 §7 스트리밍으로 활성 수를 제한 |
| 지형 SDF 재사용 | 🟡 | `GPUParticlesCollisionSDF3D` 베이크 결과를 Jolt `HeightFieldShape`와 별도로 유지해야 함(중복) |

### (e) 우선순위: 🔴 **자체 솔버 — Wave 4 (2026-08-25 2차 정정 확정)**

스코프 내 유지(전부-구현 방침)이나 작업 성격은 1차 반전이 틀렸다: ~~“벤더링 + 배선(수 주)”~~ → **자체 GPU 솔버 개발**이다. upstream `Jolt/Compute`에는 게임플레이 물리가 없고(헤어 전용), 벤더링으로 얻는 것은 컴퓨트 추상화 토대뿐. G5(async compute 멀티큐) 이후 착수, 그 시점 재견적.

---

## 9. 경계 종합 — GDExtension vs 국소 코어 vs 대규모 포크

| 작업 | 순수 GDExtension | 국소 코어 | 대규모 포크 |
|------|------------------|-----------|-------------|
| 캐릭터 계단 오르기 (GDScript step-up) | ✅ | | |
| 캐릭터 → 강체 밀기 | ✅ | | |
| 물리 바디 스트리밍/활성 영역 매니저 | ✅ | | |
| 오프라인 보로노이 프랙처 + 런타임 본딩(접촉 임펄스 기반) | ✅ | | |
| 클로스 LOD 매니저 | ✅ | | |
| 커스텀 물리 서버 전체 재구현(자체 Jolt 번들) | ✅ (244 EXBIND) | | |
| 크로스 플랫폼 결정론 활성화 | | ✅ (SCsub 1줄) | |
| 콜리전 스텝 수 노출 | | ✅ (1시간) | |
| 조인트 break force/torque | | ✅ (~50줄, 구현은 이미 존재) | |
| 클로스 굽힘·LRA·공기저항 | | ✅ (1~2파일) | |
| **클로스 스킨드 제약 (AAA 캐릭터 의류)** | | ✅ (2~4주) | |
| `StateRecorder` 스냅샷/복원 API | | ✅ (~300줄) | |
| **Jolt 차량 노출** | | ✅ (신규 constraint + 노드) | |
| **Jolt `CharacterVirtual` 노출** | | ✅ (신규 서버 API + 노드) | |
| 소프트바디 셀프 콜리전 | | | 🔴 자체 구현 (upstream 미지원 확정) |
| **스트랜드 헤어 물리** | | | ✅ 벤더링+바람 패치 (CPU 경로, 병합 대기) |
| GPU 게임플레이 물리 | | | 🔴 렌더러+물리 동시 개조 |

**핵심 통찰:** 이 표에 **"대규모 포크"가 단 3줄뿐**이고, 그 셋의 최종 상태(2026-08-25)는: 셀프콜리전=자체 구현 · 헤어 시뮬=벤더링 확보 · GPU 게임플레이 물리=자체 솔버(Wave 4)다. 렌더러 격차 문서들(§1~3)에서 절벽이 항상 "코어 통합"에 있었던 것과 대조적으로, **물리 영역엔 절벽이 없다.** 전부 완만한 경사다. **AAA에서도 이 구조는 변하지 않는다** — 달라진 것은 우선순위(클로스 B4, 파괴 Phase C의 P1 격상)뿐.

---

## 10. 우선순위 로드맵 — AAA 포토리얼 목표 기준 (2026-08-18 개정)

### 🟢 P0 — 즉시 (합계 3~5주, 코어 수정 거의 0)
1. **캐릭터 계단 오르기 + 강체 밀기 (GDScript, 2~3일).** `CharacterBody3D` 상속 클래스로 step-up 3단계 + `apply_central_impulse`. 체감 품질 상승이 가장 즉각적.
2. **물리 바디 스트리밍 매니저 (GDScript, 1~2주).** 거리 기반 3단계(활성 / freeze / 제거). 오픈월드 물리 예산의 근본 해결.
3. **결정론·안정성 저비용 훅 (코어, 2~3일).** `JPH_CROSS_PLATFORM_DETERMINISTIC` 옵션화, 콜리전 스텝 수 노출, `EPhysicsUpdateError` 카운터 노출.
4. *(차량이 디자인에 있다면)* **Jolt 차량 MVP (코어, 1~2주).**

### 🟡 P1 — 다음 (합계 6~11주)
5. **클로스 B1+B3 (코어, ~1주).** `EBendType::Dihedral` + `mBendCompliance` 노출, 공기 저항 구현(죽은 `drag_coefficient` 속성 살리기).
6. **조인트 break force/torque + 파괴 Phase C (코어, ~3일).** `get_applied_force/torque`가 이미 구현돼 있으므로 바인딩+노드 속성만. 라그돌·로프·매달린 오브젝트·가벼운 환경 파괴에 광범위 활용. **AAA에서 P2→P1 격상.**
7. **클로스 B4 스킨드 제약 (코어, 2~4주).** AAA 캐릭터 의류 시뮬레이션의 핵심. **AAA에서 P3→P1 격상.**
8. **`StateRecorder` 스냅샷 API (코어, 1~2주).** 넷코드 계획이 있으면 P0로 승격.
9. **Jolt `CharacterVirtual` 노드 (코어, 2~4주).** P0-1의 GDScript 우회가 품질 미달로 판명될 때만.

### 🟡 P2 — 조건부
10. **파괴 Phase A+B (오프라인 프랙처 + 런타임 본딩)** — Blender Cell Fracture 파이프라인 + 런타임 본딩(GDScript). 게임 디자인이 대규모 환경 파괴를 요구할 때만.
11. **Jolt 차량 프로덕션화** (3종 테스터·커브 리소스·기즈모·TrackedVehicle).
12. 물리 스트리밍의 코어 훅(`ActivateBodiesInAABox` 노출 등).
13. 클로스 LOD 매니저 (GDScript, 2~3일).

### ~~🔴 P3~P4 — 하지 말 것~~ → **P2 스코프 내 (2026-08-25 반전)**

> ⛔ 세 항목 모두 근거가 *"Jolt 업스트림 대기"* 였는데 **그 전제가 사실이 아니다**(§8 배너). 전부 구현 대상으로 복귀한다.

14. **소프트바디 셀프 콜리전** — ✅ 스파이크 완료(C4): Jolt 5.6.0 **미지원 확정**(인트라바디 경로·전용 constraint·브로드페이즈 전무) → **자체 구현 확정**(XPBD 셀프 콜리전, L5).
15. **GPU 게임플레이 물리** — 🔴 **자체 솔버 (Wave 4).** ~~벤더링으로 확보~~ [2차 정정] upstream Compute는 헤어 전용 추상화. 벤더링(완료)은 토대만 제공.
16. **스트랜드 헤어 물리 시뮬레이션** — ✅ **벤더링 완료**(c4/l5-jolt-vendoring, CPU 스모크 통과) + 바람 외력 주입 패치(#20). GPU 경로는 G5+HLSL 툴체인 선행. 렌더 측 스트랜드 래스터라이저는 별도(misc §6, L1+L6).

---

## 11. 자기 반증 — "불가"라고 쓸 뻔한 주장들의 재검증

기존 문서 전부가 초판 "❌ 불가" → "⚠️ 단계별 비용"으로 정정된 이력이 있으므로, 이 문서에서 "없다/못 한다"로 적으려던 주장을 한 번 더 공격했다.

| 초안 주장 | 반증 시도 | 결론 |
|-----------|----------|------|
| "Jolt 차량은 Godot에 없으니 Jolt를 별도로 링크해야 한다" | **틀림.** `SCsub:121-132`가 차량 12파일을 이미 컴파일한다. 링크가 아니라 래핑 문제 | **정정 반영** |
| "GDExtension으로는 Jolt 기능을 못 쓴다 → 반드시 코어" | **부분적으로 틀림.** `PhysicsServer3DExtension`에 244 EXBIND + `register_server()` 스크립트 바인딩 → 자체 Jolt를 번들해 물리 서버 전체를 GDExtension으로 교체 가능(godot-jolt 확장의 원래 방식). 다만 이중 유지보수라 이 프로젝트엔 열등 | **경계 재기술** |
| "런타임 파괴는 Godot에서 불가" | **틀림.** UE Chaos·Havok·NvBlast 모두 오프라인 프랙처 + 런타임 본드 관리이며, 필요한 런타임 프리미티브(`get_contact_impulse`)가 **이미 스크립트 바인딩돼 있다**. 조인트 스트레인도 C++엔 구현 완료 | **"불가" → "저작 툴 부재"** |
| "조인트 반력을 읽을 수 없다" | **틀림.** `JoltPhysicsServer3D::*_joint_get_applied_force/torque`가 `GetTotalLambda*` 기반으로 이미 구현됨(`jolt_hinge_joint_3d.cpp:339-378`). 다만 `_bind_methods() {}`가 비어 있어 스크립트 미노출 | **"없음" → "미바인딩"** |
| "Godot 클로스는 Jolt 한계 때문에 나쁘다" | **틀림.** Jolt엔 Dihedral bend·LRA·Skinned·Volume·Cosserat rod가 전부 있다. Godot이 `EBendType::None`으로 **꺼놨다**(`jolt_soft_body_3d.cpp:223`) | **"엔진 한계" → "설정 미노출"** |
| "결정론은 Jolt 한계" | **틀림.** Jolt는 스레드 수 무관 결정론으로 설계됐고 크로스플랫폼 모드도 제공한다. Godot이 `JPH_CROSS_PLATFORM_DETERMINISTIC`을 정의하지 않을 뿐(`SCsub`) | **"불가" → "빌드 플래그 1줄"** |
| "롤백 넷코드는 스냅샷 API가 없어 불가" | **틀림.** `StateRecorder`+`StateRecorderFilter`가 컴파일돼 있고, Godot에 `StreamIn`/`StreamOut` 어댑터가 **이미 양방향으로 존재**(`jolt_stream_wrappers.h:42,58`) | **"불가" → "~300줄"** |
| "Godot의 Jolt는 멀티스레드를 안 쓴다" | **틀림.** `JoltJobSystem`이 `WorkerThreadPool::add_native_task`로 정상 위임(`jolt_job_system.cpp:105`) | **주장 철회** |
| "`CharacterBody3D`는 Jolt CharacterVirtual을 쓴다" | **틀림.** `body_test_motion` 위의 순수 Godot 알고리즘. `character` grep이 `physics_server_3d.h`에서 0매치 | **정정 반영** |
| "이동 플랫폼이 부실하다" | **틀림.** `get_velocity_at_local_position()`으로 회전 플랫폼까지 지원하고 이탈 정책 3종 제공(`character_body_3d.cpp:56-135`) | **주장 철회** |
| "GPU 파티클로 게임플레이 물리를 흉내낼 수 있다" | **맞음(부정 방향).** 셰이더에 월드 쓰기 경로 없음, GPU→CPU는 AABB 전체 스톨뿐(`particles_storage.cpp:667`). 여기만은 진짜 벽 | **유지** |
| "소프트바디끼리 충돌한다" | **틀림(부정 방향).** Jolt 업스트림 TODO로 미지원 확인(`SoftBodyMotionProperties.cpp:147`) | **유지** |

---

## 12. gaps 문서 반영

기존 [godot-openworld-engine-gaps.md](./godot-openworld-engine-gaps.md)엔 물리 항목이 없다. 아래 행을 요약 표에 추가한다:

| 격차 | 대체 난이도 | 해결 경로 |
|------|-------------|-----------|
| 물리 시뮬레이션 (차량·파괴·클로스·결정론·캐릭터) | ⚠️ **가장 얕음** — 코드는 이미 번들, 바인딩만 없음 | Jolt 5.6 차량/`CharacterVirtual`/`StateRecorder`/고급 클로스 제약이 **전부 컴파일돼 있으나 참조 0** → 국소 코어 래핑(각 1~4주). 파괴만 신규지만 런타임 프리미티브는 이미 스크립트 노출 ([상세](./godot-physics-simulation-research.md)) |

---

## 13. 남은 미확인 질문 (Open Questions)

1. `JPH_CROSS_PLATFORM_DETERMINISTIC` 활성 시 Godot 씬에서의 **실측 성능 손실률** (Jolt는 일반적으로 한 자릿수 %로 보고하나 Godot 워크로드 미측정).
2. Jolt 차량을 신규 `JointType`으로 넣을지 별도 RID 계열로 넣을지 — 업스트림 기여를 노린다면 API 설계 합의가 선행돼야 함.
3. `CharacterVirtual`을 `PhysicsServer3D` 추상에 넣으면 GodotPhysics3D 백엔드가 구현 불가 → **백엔드별 선택적 기능(capability query)** 패턴이 Godot 물리 서버에 아직 없음. 이 설계 문제가 차량·캐릭터·스냅샷 전부에 공통으로 걸린다.
4. ~~Jolt GPU 3폴더의 성숙도와 번들 편입 일정~~ [2026-08-25 해소: 편입 완료(병합 대기). 성숙도는 실측됨 — Hair "still in development"(바람 없음·LOD 없음·ConvexHull 한정), Compute는 헤어 전용 추상화].
5. 스트리밍 시 `max_bodies` 재구성 — Jolt `PhysicsSystem::Init` 이후 변경 불가이므로, 공간 재생성 없이 늘리는 방법이 있는지.

---

*리서치 방법: Godot 4.8-dev(`eda2a482e9`) 로컬 소스트리 실측 우선 — 모든 부정 주장을 grep 결과와 검색어로 명시. Jolt 5.6.0 번들 헤더 직접 판독. 웹 리서치로 업스트림 문서·Godot proposal/PR·커뮤니티 애드온 실재 여부 교차검증.*

## 14. 2026-08-18 개정 — AAA 기준 재채점

### 델타 표: 뒤집힌 판정

| 항목 | 기존 판정 | AAA 판정 | 뒤집힌 이유 |
|------|----------|----------|------------|
| **§3 파괴/프랙처** | 🟡 P2 (원신은 런타임 파괴 없음) | 🟡 P1 (Phase C 격상) | AAA 포토리얼 오픈월드에서 환경 파괴는 장르 관습. Phase C(조인트 break force)는 저비용·범용으로 P1 격상. Phase A+B(오프라인 프랙처)는 P2 유지 |
| **§4 클로스 — B4 스킨드 제약** | P3 (SpringBone 대체) | 🟡 P1 (AAA 캐릭터 의류 필수) | AAA 캐릭터(Horizon/RDR2/TLOU II/Cyberpunk)에서 의류 시뮬레이션은 존재감의 핵심. SpringBone은 머리카락만, 치마·망토·코트는 XPBD 클로스 필요 |
| **§4 클로스 — 의류 전략** | "의류는 SpringBone, SoftBody는 환경만" | "의류는 SoftBody B1+B3+B4, 머리카락은 SpringBone" | AAA 요구는 SpringBone이 감당할 수 없는 수준의 드레이프·주름 품질 |
| **§10 우선순위 로드맵** | P1: 4항목(4~8주) | P1: 5항목(6~11주) | 파괴 Phase C, 클로스 B4 스킨드 제약이 P1으로 진입, 총 작업량 증가 |
| **§8 GPU 물리** | 🔴 P4 (비권장) | 🔴 **자체 솔버, Wave 4 (2026-08-25 2차 정정)** | 1차 반전(“벤더링하면 됨”)은 upstream 감사로 기각 — Compute는 헤어 전용 추상화(소비자 = Hair뿐). 스코프 내 유지, 추상화 토대만 재사용, G5 이후 재견적 |
| **§0 요약표** | "원신급" 기준 | AAA 우선순위로 갱신 | 파괴 P2→P1, 클로스 B4 P3→P1, 스트랜드 헤어 물리 P4 추가 |
| **§2 차량 — 대안 (e)** | "원신형 회피" (원신엔 플레이어 차량 없음) | 대안 텍스트 유지, **단 AAA에서는 차량이 더 가치 있음** | AAA 오픈월드(GTA/Cyberpunk/RDR2)에서 차량 물리는 핵심. 디자인 의존이므로 P0 조건부 유지 |

### 유지된 판정 (AAA에서도 변함없음)

| 항목 | 판정 | 이유 |
|------|------|------|
| §2 차량 물리 | 🟢 P0 (조건부) | Jolt 차량 12파일 컴파일 완료. 디자인 의존. AAA에서도 동일 |
| §5 결정론·스냅샷 | 🟡 P1 | SCsub 1줄 + ~300줄. 저비용 고효과 |
| §6 캐릭터 컨트롤러 | 🟢 P0 | GDScript 우회 2~3일. AAA에서도 여전히 최우선 |
| §7 멀티스레딩·성능 | 🟢 P0 | GDScript 스트리밍 매니저. 가장 저비용 고효과 |
| §8 GPU 물리 | 🔴 Wave 4 | 자체 솔버(벤더링된 Compute 추상화를 토대로) — 2026-08-25 확정 |
| §11 자기 반증 | 12개 중 10개 반증 성공 | AAA에서도 실측 근거가 동일하므로 변경 없음 |

### AAA 마스터 요약표 행

| # | 격차 | AAA 신호 | AAA 한 줄 |
|---|------|---------|-----------|
| 물리-1 | 차량 물리 | 🟢 P0 (조건부) | Jolt 차량 12파일 이미 컴파일. 국소 코어 래핑(1~2주 MVP). 디자인 의존 |
| 물리-2 | 파괴/프랙처 | 🟡 P1 (Phase C) | 조인트 break force(이미 구현, 미바인딩 ~10줄). Phase A+B는 P2 |
| 물리-3 | 클로스 | 🟡 P1 (B1+B3+B4) | 굽힘+공기저항+스킨드 제약. AAA 의류 시뮬레이션 필수 |
| 물리-4 | 결정론·스냅샷 | 🟡 P1 | SCsub 1줄 + ~300줄. 넷코드 시 P0 |
| 물리-5 | 캐릭터 컨트롤러 | 🟢 P0 | GDScript step-up 2~3일. Jolt CharacterVirtual은 P1 |
| 물리-6 | 멀티스레딩·성능 | 🟢 P0 | GDScript 스트리밍 매니저 1~2주. 코어 0 |
| 물리-7 | GPU 물리 | 🔴 **Wave 4 자체 솔버** | [2차 정정] 벤더링은 헤어+추상화 토대만 제공. 게임플레이 GPU 물리는 자체 개발 |
