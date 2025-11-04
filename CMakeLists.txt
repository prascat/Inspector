cmake_minimum_required(VERSION 3.10)

project(MV)  # 프로젝트 이름을 MV로 변경

set(CMAKE_CXX_STANDARD 17)  # Qt6는 C++17 필요
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# 플랫폼별 설정
if(APPLE)
    set(CMAKE_PREFIX_PATH "/opt/homebrew/lib/cmake")  # macOS Qt6 경로
    set(CMAKE_MACOSX_BUNDLE FALSE)
elseif(UNIX AND NOT APPLE)
    # Ubuntu/Linux 설정 - Qt 6.9.3
    set(CMAKE_PREFIX_PATH "/home/km/Qt/6.9.3/gcc_64;/home/km/Qt/6.9.3/gcc_64/lib/cmake")
    set(Qt6_DIR "/home/km/Qt/6.9.3/gcc_64/lib/cmake/Qt6")
    message(STATUS "Linux platform detected - Qt 6.9.3 at /home/km/Qt/6.9.3/gcc_64")
endif()

# 플랫폼별 추가 설정
if(APPLE)
    # macOS용 OpenCV 경로 설정 (Homebrew)
    set(OpenCV_DIR "/opt/homebrew/opt/opencv/lib/cmake/opencv4")
    set(OpenCV_INCLUDE_DIRS "/opt/homebrew/opt/opencv/include/opencv4")
    # 라이브러리 경로 추가
    link_directories(/opt/homebrew/opt/opencv/lib)
elseif(UNIX AND NOT APPLE)
    # Ubuntu/Linux용 OpenCV 경로 설정
    set(OpenCV_DIR "/usr/local/lib/cmake/opencv4")
    if(NOT EXISTS "${OpenCV_DIR}")
        set(OpenCV_DIR "/usr/lib/cmake/opencv4")
    endif()
    # OpenCV 라이브러리 경로 추가
    link_directories(/usr/local/lib)
endif()

# Qt6 Widgets 패키지 찾기
find_package(Qt6 REQUIRED COMPONENTS Widgets Xml Network SerialPort)

# OpenCV 패키지 찾기
find_package(OpenCV REQUIRED)

# 헤더 파일 경로 추가
include_directories(${OpenCV_INCLUDE_DIRS})

# AUTOMOC 활성화
set(CMAKE_AUTOMOC ON)

# Spinnaker SDK 지원 옵션
option(USE_SPINNAKER "Enable Spinnaker camera support" ON)

# 추가 라이브러리 목록 초기화
set(EXTRA_LIBS "")

if(USE_SPINNAKER)
    # Spinnaker SDK 자동 검색
    find_path(SPINNAKER_INCLUDE_DIR
        NAMES Spinnaker.h
        PATHS
            /usr/include/spinnaker
            /usr/local/include/spinnaker
            /opt/spinnaker/include
            /Applications/Spinnaker/include
            "C:/Program Files/Point Grey Research/Spinnaker/include"
        DOC "Path to Spinnaker include directory"
    )
    
    find_library(SPINNAKER_LIBRARY
        NAMES Spinnaker Spinnaker_v140
        PATHS
            /usr/lib
            /usr/local/lib
            /opt/spinnaker/lib
            /Applications/Spinnaker/lib
            "C:/Program Files/Point Grey Research/Spinnaker/lib64/vs2015"
        DOC "Path to Spinnaker library"
    )
    
    # 수동으로 설정된 경로도 허용
    set(SPINNAKER_DIR "" CACHE PATH "Path to Spinnaker SDK (optional)")
    
    # 수동 경로가 설정되어 있으면 우선 사용
    if(SPINNAKER_DIR)
        set(SPINNAKER_INCLUDE_DIR "${SPINNAKER_DIR}/include")
        if(WIN32)
            find_library(SPINNAKER_LIBRARY
                NAMES Spinnaker_v140
                PATHS "${SPINNAKER_DIR}/lib64/vs2015"
                NO_DEFAULT_PATH
            )
        else()
            find_library(SPINNAKER_LIBRARY
                NAMES Spinnaker
                PATHS "${SPINNAKER_DIR}/lib"
                NO_DEFAULT_PATH
            )
        endif()
    endif()
    
    # Spinnaker가 발견되었는지 확인
    if(SPINNAKER_INCLUDE_DIR AND SPINNAKER_LIBRARY)
        message(STATUS "Found Spinnaker SDK")
        message(STATUS "  Include: ${SPINNAKER_INCLUDE_DIR}")
        message(STATUS "  Library: ${SPINNAKER_LIBRARY}")
        
        # 헤더 및 라이브러리 경로 추가
        include_directories(${SPINNAKER_INCLUDE_DIR})
        list(APPEND EXTRA_LIBS ${SPINNAKER_LIBRARY})
        
        # 컴파일 정의 추가
        add_definitions(-DUSE_SPINNAKER)
        
        # macOS에서 필요한 추가 링크 플래그 및 런타임 경로 설정
        if(APPLE)
            get_filename_component(SPINNAKER_LIB_DIR ${SPINNAKER_LIBRARY} DIRECTORY)
            set(CMAKE_INSTALL_RPATH "${SPINNAKER_LIB_DIR}")
            set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
            set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
            
            # 추가적인 GenApi 라이브러리들 링크
            find_library(GENAPI_LIBRARY
                NAMES GenApi_clang140_v3_0 libGenApi_clang140_v3_0
                PATHS "${SPINNAKER_LIB_DIR}"
                NO_DEFAULT_PATH
            )
            if(GENAPI_LIBRARY)
                list(APPEND EXTRA_LIBS ${GENAPI_LIBRARY})
                message(STATUS "  GenApi Library: ${GENAPI_LIBRARY}")
            endif()
            
            # 다른 필요한 Spinnaker 종속성 라이브러리들 찾기
            find_library(SPINNAKER_C_LIBRARY
                NAMES Spinnaker_C libSpinnaker_C
                PATHS "${SPINNAKER_LIB_DIR}"
                NO_DEFAULT_PATH
            )
            if(SPINNAKER_C_LIBRARY)
                list(APPEND EXTRA_LIBS ${SPINNAKER_C_LIBRARY})
                message(STATUS "  Spinnaker_C Library: ${SPINNAKER_C_LIBRARY}")
            endif()
        elseif(UNIX AND NOT APPLE)
            # Ubuntu/Linux에서 런타임 경로 설정
            get_filename_component(SPINNAKER_LIB_DIR ${SPINNAKER_LIBRARY} DIRECTORY)
            set(CMAKE_INSTALL_RPATH "${SPINNAKER_LIB_DIR}")
            set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
            set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
        endif()
        
    else()
        message(WARNING "Spinnaker SDK not found. Spinnaker support will be disabled.")
        message(WARNING "  Include searched: ${SPINNAKER_INCLUDE_DIR}")
        message(WARNING "  Library searched: ${SPINNAKER_LIBRARY}")
        set(USE_SPINNAKER OFF)
    endif()
endif()

# 실행 파일 추가 (일반 실행 파일로 설정)
add_executable(MV 
    main.cpp 
    TeachingWidget.cpp 
    ImageProcessor.cpp 
    InsProcessor.cpp 
    CameraView.cpp 
    FilterDialog.cpp 
    LogViewer.cpp 
    CameraSettingsDialog.cpp 
    FilterPropertyWidget.cpp
    LanguageManager.cpp
    LanguageSettingsDialog.cpp
    SerialSettingsDialog.cpp
    RecipeManager.cpp
    ConfigManager.cpp
    SerialCommunication.cpp
)

# Qt6, OpenCV 및 추가 라이브러리 연결
target_link_libraries(MV 
    Qt6::Widgets 
    Qt6::Xml
    Qt6::Network
    Qt6::SerialPort
    ${OpenCV_LIBS}
    ${EXTRA_LIBS}
)

# 실행 파일 배포 디렉토리 설정
set(DEPLOY_DIR "${CMAKE_SOURCE_DIR}/deploy")

# 배포용 폴더 생성 및 파일 복사
add_custom_target(deploy ALL
    DEPENDS MV
    COMMENT "Deploying application to ${DEPLOY_DIR}"
)

# 실행 파일 복사
add_custom_command(TARGET deploy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPLOY_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:MV> ${DEPLOY_DIR}/
    COMMENT "Copying executable to deploy directory"
)

# lang.xml 파일이 있으면 복사
if(EXISTS "${CMAKE_SOURCE_DIR}/deploy/lang.xml")
    add_custom_command(TARGET deploy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            ${CMAKE_SOURCE_DIR}/deploy/lang.xml ${DEPLOY_DIR}/
        COMMENT "Copying lang.xml"
    )
endif()

# config.xml 파일이 있으면 복사
if(EXISTS "${CMAKE_SOURCE_DIR}/deploy/config.xml")
    add_custom_command(TARGET deploy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            ${CMAKE_SOURCE_DIR}/deploy/config.xml ${DEPLOY_DIR}/
        COMMENT "Copying config.xml"
    )
endif()

# OpenCV 디버그 정보 출력 (선택 사항)
message(STATUS "OpenCV_LIBS: ${OpenCV_LIBS}")
message(STATUS "Deploy directory: ${DEPLOY_DIR}")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# 설치 타겟 설정 (make install 명령으로 실행)
install(TARGETS MV 
    DESTINATION bin
    COMPONENT runtime
)

# 설치 디렉토리 기본값 설정
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Install prefix" FORCE)
endif()