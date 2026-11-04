# GLFW 전환과 Windows 빌드

지원 범위는 Windows의 다른 PC와 Visual Studio 환경이다. 창·입력·실행 루프를 FreeGLUT에서
GLFW 3.4로 변경했다. GDI 한글 래스터화, WIC 이미지 로딩, Winsock 모니터는 Windows 기능을 사용한다.
Linux/macOS 포팅은 이번 범위에 포함하지 않는다.

## 다른 PC에서 빌드

Visual Studio 2022의 **C++를 사용한 데스크톱 개발**, MSVC v143, Windows 10/11 SDK가 필요하다.
저장소를 통째로 복제하고 `BaseProject_SE/SimpleGame.sln`을 연 뒤 `Release / x64`로 솔루션을 빌드한다.
`Debug / x64`, `Debug·Release / x86(Win32)`도 같은 의존성 소스로 빌드한다.
다른 Visual Studio 툴셋을 사용하려면 솔루션의 **SimpleGame·GLFW.Static·GLEW.Static 세 프로젝트**를
동일한 툴셋으로 재대상화한다. 라이브러리를 다른 아키텍처나 CRT 설정으로 따로 준비하지 않는다.

- `third_party/glfw`: 공식 3.4 소스 배포본 전체. Win32 백엔드와 공통 C 파일 21개를 정적 라이브러리로 빌드한다.
- `third_party/glew`: 공식 2.2.0 소스 배포본 전체. 생성된 `src/glew.c`와 일치하는 `include/GL` 헤더를 포함한다.
- 게임은 두 정적 프로젝트를 참조한다. C 소스에는 C++ 미리 컴파일 헤더를 적용하지 않는다.
- 게임과 두 라이브러리에 `/MT`·Debug `/MTd`를 맞춘다. GLFW/GLEW 또는 VC 런타임 DLL을 별도로
  복사하는 과정이 필요하지 않다. Windows 자체 DLL과 OpenGL 드라이버는 대상 PC에서 제공해야 한다.
- 중간 파일·정적 라이브러리는 LocalAppData의 프로젝트·컴파일러·플랫폼·구성별 경로에 분리한다.
  기존 OneDrive 작업 폴더와 컴파일러 매핑 파일이 충돌하지 않도록 한다.
- 빌드 후 실행 파일 옆에 `Shaders`, `Resources`, `Monitor`, `Fonts`, `Licenses`가 자동 복사된다.
  배포할 때는 exe만 옮기지 말고 이 폴더도 함께 옮긴다.
- 과거 `Dependencies` 폴더의 FreeGLUT/GLEW 헤더·lib 및 기존 출력 폴더의 DLL은 사용하지 않는다.
  기존 파일은 보존했으며 새 빌드는 그 경로를 링크하지 않는다.

## 선택 사항: CMake

CMake 3.21 이상과 Visual Studio C++ 툴체인에서도 같은 포함 소스를 사용할 수 있다.
라이브러리를 다운로드하거나 vcpkg·별도 설치본을 검색하지 않는다. 예를 들어 저장소 루트의 PowerShell에서:

```powershell
$gameBuild = Join-Path $env:LOCALAPPDATA 'SimpleGameBuild\cmake\haedong-glfw-x64'
cmake -S . -B $gameBuild -G 'Visual Studio 17 2022' -A x64
cmake --build $gameBuild --config Release
```

다른 체크아웃을 사용할 때는 별도 빌드 디렉터리를 지정한다. CMake를 지원하는 다른 Visual Studio 버전에서는
설치된 버전에 맞는 `-G`를 선택한다. CMake도 Windows SDK와 MSVC 또는 clang-cl을 요구한다.
위 명령은 사용자가 실행할 빌드 안내이며 이번 작업에서 실행하지 않았다.

## 입력·화면 결과 유지

게임의 1280×800 논리 해상도, 투영·게임 로직·델타 시간 상한·셰이더·아틀라스·인스턴싱·후처리는 유지한다.
OpenGL 3.3 compatibility context, 이중 버퍼, 기존 알파 블렌딩을 사용한다. MSAA와 sRGB 변환은 추가하지 않는다.

- `glfwPollEvents → Input/Update → SceneGraph 가시성/정렬 → Render → glfwSwapBuffers` 순서다.
- GLFW의 물리 키를 기존 소문자 WASD·숫자·SPACE·TAB·ESC·ENTER 명령에 대응시킨다.
  키 반복은 무시하고 키 해제는 처리한다. 숫자패드 ENTER도 일반 ENTER와 같다.
- 커서 좌표는 **창 크기**, 최종 viewport는 **프레임버퍼 픽셀 크기**를 쓴다. DPI가 달라도
  인벤토리·거래창에 논리 좌표를 한 번만 적용하고 패널 Actor 이동량을 뺀다.
- 커서가 창을 떠나거나 포커스를 잃으면 입력 상태를 지워 이동·공격이 눌린 채 남는 것을 막는다.
- 최소화·0픽셀 화면에서는 이벤트 대기로 CPU 반복을 줄이고 월드를 정지한다. 복원 시 경과 시간을
  누적 적용하지 않으며 FPS 통계의 첫 간격도 제외한다. 모니터는 일시정지 상태를 계속 게시한다.
- 종료 시 모니터·GPU 질의·렌더러·폰트부터 해제한 다음 GLFW 창과 context를 없앤다.
- 기본 swap interval은 0이다. 기존 연속 렌더링 루프에 임의의 60FPS 제한을 추가하지 않는다.
  수직 동기화가 필요하면 실행 전 `$env:SIMPLEGAME_VSYNC='1'`을 지정한다. 드라이버의 강제 설정은 별도다.

## 한글 폰트

맑은 고딕 15픽셀, 같은 CreateFontW 옵션과 GetGlyphOutlineW 회색조 변환, 글리프 아틀라스·advance 캐시를 유지한다.
새 `KoreanFont`는 GLFW 창의 DC 대신 전용 GDI 메모리 DC를 소유해 창 시스템과 수명을 분리한다.
매 프레임 폰트를 생성하거나 글리프를 다시 래스터화하지 않는다.

한글 글리프를 사용할 수 없는 시스템에서는 동봉한 나눔고딕을 `FR_PRIVATE`로 해당 프로세스에만 등록하고
종료 시 해제한다. 관리자 권한이나 시스템 폰트 설치는 필요 없다. 폰트와 OFL 라이선스가 함께 배포된다.
현재 소스의 Unicode escape 문자 529개와 기본 영문·숫자 및 한글 범위 끝 글리프가 폰트 cmap에 있음을 검사했다.
이는 TTF 데이터 검사이며 GDI 화면 확인은 아니다. 대체 폰트를 쓰는 PC에서는 글꼴 모양·폭에 차이가 날 수 있다.

## 성능과 검증 범위

기존 렌더 데이터와 드로우 제출 구조를 유지했다. 프로파일 스키마 14에 `window_init`, `window_events` 시간과
키·마우스·프레임버퍼 크기·포커스 이벤트 수를 추가했다. 실제 CPU/GPU 시간·업로드량·캐시 적중·FPS는
기존 JSONL로 비교한다. 콘솔 프레임 로그는 다시 추가하지 않는다.

`node tools/Test-GlfwMigration.cjs`로 1,848개 공식 배포 파일의 해시, 빌드 참조·C 소스 목록·폰트 문자 지원,
입력 대응·36가지 창/DPI 좌표·종료 순서를 검사한다. 솔루션/프로젝트 XML·구성 평가는 별도로 확인한다.
이 작업은 게임·라이브러리 빌드나 실행, 화면·실측 성능 확인을 수행하지 않았다. 다른 PC에서의 성공이나
프레임률·화면의 완전한 동일성은 재빌드와 실행 비교 전에는 보장할 수 없다.

재빌드 후 WASD/대각선·공격, 인벤토리/거래창 클릭, 한글 줄바꿈, 창 크기·DPI 변경, 최소화/복원,
창 닫기를 확인한다. 성능 비교는 같은 Release 설정·해상도·VSync·상황에서 `frame_interval_ms`,
`cpu_ms.window_events`, 기존 `swap_wait`와 비동기 GPU 구간을 비교한다. 월드 생성 시드는 로그에서 확인한다.

공식 근거: [GLFW 빌드와 정적 링크](https://www.glfw.org/docs/3.4/build_guide.html),
[GLFW 입력](https://www.glfw.org/docs/3.4/input_guide.html),
[창과 프레임버퍼](https://www.glfw.org/docs/3.4/window_guide.html),
[나눔고딕 배포·라이선스](https://github.com/google/fonts/tree/main/ofl/nanumgothic).
버전·원본 URL·아카이브와 파일 트리 해시는 `third_party/DEPENDENCIES.json`에 기록한다.
