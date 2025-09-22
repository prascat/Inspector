cmake_minimum_required(VERSION 3.5)

project(MV)  # 프로젝트 이름을 MV로 변경

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_PREFIX_PATH "/opt/homebrew/opt/qt@5")
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# macOS에서 앱 번들이 아닌 일반 실행 파일로 빌드되도록 설정
set(CMAKE_MACOSX_BUNDLE FALSE)

# Qt5 Widgets 패키지 찾기
find_package(Qt5Widgets REQUIRED)
find_package(Qt5Xml REQUIRED)
find_package(Qt5Network REQUIRED)
find_package(Qt5SerialPort REQUIRED)

# OpenCV 패키지 찾기
find_package(OpenCV REQUIRED)

# 헤더 파일 경로 추가
include_directories(${OpenCV_INCLUDE_DIRS})

# AUTOMOC 활성화
set(CMAKE_AUTOMOC ON)

# Spinnaker SDK 지원 옵션
option(USE_SPINNAKER "Enable Spinnaker camera support" OFF)

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
    AITrainer.cpp
    ConfigManager.cpp
    SerialCommunication.cpp
)

# Qt5, OpenCV 및 추가 라이브러리 연결
target_link_libraries(MV 
    Qt5::Widgets 
    Qt5::Xml
    Qt5::Network
    Qt5::SerialPort
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

# 필요한 디렉토리들 생성
add_custom_command(TARGET deploy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPLOY_DIR}/models
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPLOY_DIR}/results
    COMMENT "Creating necessary directories"
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

# Docker 관련 파일들이 있다면 복사 (선택사항)
if(EXISTS "${CMAKE_SOURCE_DIR}/Dockerfile")
    add_custom_command(TARGET deploy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            ${CMAKE_SOURCE_DIR}/Dockerfile ${DEPLOY_DIR}/
        COMMENT "Copying Dockerfile"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/requirements.txt")
    add_custom_command(TARGET deploy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            ${CMAKE_SOURCE_DIR}/requirements.txt ${DEPLOY_DIR}/
        COMMENT "Copying requirements.txt"
    )
endif()

# OpenCV 디버그 정보 출력 (선택 사항)
message(STATUS "OpenCV_LIBS: ${OpenCV_LIBS}")
message(STATUS "Deploy directory: ${DEPLOY_DIR}")

# 설치 타겟 설정 (make install 명령으로 실행)
install(TARGETS MV 
    DESTINATION bin
    COMPONENT runtime
)

install(DIRECTORY DESTINATION models
    COMPONENT runtime
)

install(DIRECTORY DESTINATION results  
    COMPONENT runtime
)

# 설치 디렉토리 기본값 설정
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Install prefix" FORCE)
endif()