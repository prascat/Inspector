cmake_minimum_required(VERSION 3.10)

project(Inspector)  # 프로젝트 이름

set(CMAKE_CXX_STANDARD 17)  # Qt6는 C++17 필요
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# 플랫폼별 설정
if(APPLE)
    # ===== macOS (Apple Silicon) 설정 =====
    # macOS 배포 타겟 설정 (라이브러리와 일치)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0" CACHE STRING "OS X deployment version" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS architectures" FORCE)
    
    # Qt6 경로 (Homebrew)
    set(CMAKE_PREFIX_PATH "/opt/homebrew/lib/cmake")
    set(CMAKE_MACOSX_BUNDLE TRUE)  # macOS GUI 앱 번들
    
    message(STATUS "macOS Build Configuration:")
    message(STATUS "  Deployment Target: 26.0")
    message(STATUS "  Architecture: arm64")
    message(STATUS "  Qt6 Path: /opt/homebrew/lib/cmake")
    message(STATUS "  Bundle: YES (GUI App)")
    
elseif(UNIX AND NOT APPLE)
    # ===== Linux (Ubuntu) 설정 =====
    # Qt 6.10.1 경로 (custom installation)
    set(CMAKE_PREFIX_PATH "/home/km/Qt/6.10.1/gcc_64;/home/km/Qt/6.10.1/gcc_64/lib/cmake")
    set(Qt6_DIR "/home/km/Qt/6.10.1/gcc_64/lib/cmake/Qt6")
    
    message(STATUS "Linux Build Configuration:")
    message(STATUS "  Qt6 Path: /home/km/Qt/6.10.1/gcc_64")
    message(STATUS "  Linux platform detected - Qt 6.10.1 at /home/km/Qt/6.10.1/gcc_64")
endif()

# 플랫폼별 추가 설정
if(APPLE)
    # macOS용 OpenCV 경로 설정 (Homebrew)
    set(OpenCV_DIR "/opt/homebrew/opt/opencv/lib/cmake/opencv4")
    set(OpenCV_INCLUDE_DIRS "/opt/homebrew/opt/opencv/include/opencv4")
    
    # OpenCV 라이브러리 경로 추가
    link_directories(/opt/homebrew/opt/opencv/lib)
elseif(UNIX AND NOT APPLE)
    # Ubuntu/Linux용 OpenCV 경로 설정
    set(OpenCV_DIR "/usr/local/lib/cmake/opencv4")
    if(NOT EXISTS "${OpenCV_DIR}")
        set(OpenCV_DIR "/usr/lib/cmake/opencv4")
    endif()
    # OpenCV 라이브러리 경로 추가
    link_directories(/usr/local/lib)
    
    # JETSON 감지 (aarch64 + CUDA)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        if(EXISTS "/usr/local/cuda")
            message(STATUS "JETSON Platform Detected - Enabling TensorRT")
            set(USE_TENSORRT ON)
            add_definitions(-DUSE_TENSORRT)
            
            # TensorRT 경로 (JETSON JetPack)
            set(TensorRT_DIR "/usr/lib/aarch64-linux-gnu")
            include_directories(/usr/include/aarch64-linux-gnu)
            link_directories(${TensorRT_DIR})
            
            # CUDA 경로
            include_directories(/usr/local/cuda/include)
            link_directories(/usr/local/cuda/lib64)
            
            message(STATUS "  TensorRT: ${TensorRT_DIR}")
            message(STATUS "  CUDA: /usr/local/cuda")
        else()
            # JETSON이 아닌 Linux - ONNX Runtime 사용
            message(STATUS "Linux x86 Platform Detected - Enabling ONNX Runtime")
            set(USE_ONNX ON)
            add_definitions(-DUSE_ONNX)
        endif()
    else()
        # x86_64 Linux - ONNX Runtime 사용
        message(STATUS "Linux x86_64 Platform Detected - Enabling ONNX Runtime")
        set(USE_ONNX ON)
        add_definitions(-DUSE_ONNX)
    endif()
endif()

# Qt6 Widgets 패키지 찾기
find_package(Qt6 REQUIRED COMPONENTS Widgets Xml Network SerialPort)

# OpenCV 패키지 찾기
find_package(OpenCV REQUIRED)

# OpenVINO 패키지 찾기 (Python 설치 경로 사용)
set(OPENVINO_BASE_DIR "/home/km/.local/lib/python3.10/site-packages/openvino")
set(OpenVINO_DIR "${OPENVINO_BASE_DIR}/cmake")

if(EXISTS "${OpenVINO_DIR}")
    find_package(OpenVINO QUIET COMPONENTS Runtime)
    if(OpenVINO_FOUND)
        message(STATUS "Found OpenVINO: ${OpenVINO_VERSION}")
        message(STATUS "  CMake Dir: ${OpenVINO_DIR}")
        message(STATUS "  Include: ${OPENVINO_BASE_DIR}/include")
        message(STATUS "  Libs: ${OPENVINO_BASE_DIR}/libs")
        
        # 헤더 및 라이브러리 경로 추가
        include_directories(${OPENVINO_BASE_DIR}/include)
        link_directories(${OPENVINO_BASE_DIR}/libs)
        
        # 런타임 경로 설정
        set(CMAKE_INSTALL_RPATH "${OPENVINO_BASE_DIR}/libs")
        set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
        
        add_definitions(-DUSE_OPENVINO)
    else()
        message(WARNING "OpenVINO CMake config found but package import failed.")
    endif()
else()
    message(WARNING "OpenVINO not found. YOLO segmentation will be disabled.")
endif()

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
add_executable(Inspector 
    main.cpp 
    TeachingWidget.cpp 
    ImageProcessor.cpp 
    InsProcessor.cpp 
    CameraView.cpp 
    FilterDialog.cpp 
    CameraSettingsDialog.cpp 
    FilterPropertyWidget.cpp
    LanguageManager.cpp
    LanguageSettingsDialog.cpp
    SerialSettingsDialog.cpp
    ClientDialog.cpp
    TestDialog.cpp
    RecipeManager.cpp
    ConfigManager.cpp
    SerialCommunication.cpp
    CustomMessageBox.cpp
    CustomFileDialog.cpp
    TrainDialog.cpp
)

# Qt6, OpenCV, OpenVINO 및 추가 라이브러리 연결
target_link_libraries(Inspector 
    PRIVATE
    Qt6::Widgets 
    Qt6::Xml
    Qt6::Network
    Qt6::SerialPort
    ${OpenCV_LIBS}
    ${EXTRA_LIBS}
)

# OpenVINO 링크 (조건부)
if(OpenVINO_FOUND)
    target_link_libraries(Inspector PRIVATE openvino::runtime)
endif()

# TensorRT 링크 (JETSON 전용)
if(USE_TENSORRT)
    target_link_libraries(Inspector PRIVATE
        nvinfer
        nvonnxparser
        cudart
    )
    message(STATUS "TensorRT libraries linked for JETSON")
endif()

# ONNX Runtime 링크 (x86 Linux)
if(USE_ONNX)
    # ONNX Runtime 경로 찾기
    find_path(ONNXRUNTIME_INCLUDE_DIRS 
        NAMES onnxruntime_cxx_api.h
        PATHS 
            /usr/include/onnxruntime
            /usr/local/include/onnxruntime
            /opt/onnxruntime/include
    )
    
    find_library(ONNXRUNTIME_LIB
        NAMES onnxruntime
        PATHS
            /usr/lib
            /usr/local/lib
            /opt/onnxruntime/lib
    )
    
    if(ONNXRUNTIME_INCLUDE_DIRS AND ONNXRUNTIME_LIB)
        message(STATUS "ONNX Runtime found:")
        message(STATUS "  Include: ${ONNXRUNTIME_INCLUDE_DIRS}")
        message(STATUS "  Library: ${ONNXRUNTIME_LIB}")
        
        include_directories(${ONNXRUNTIME_INCLUDE_DIRS})
        target_link_libraries(Inspector PRIVATE ${ONNXRUNTIME_LIB})
    else()
        message(WARNING "ONNX Runtime not found. Please install: apt install libonnxruntime-dev")
    endif()
endif()

# 실행 파일 배포 디렉토리 설정
set(DEPLOY_DIR "${CMAKE_SOURCE_DIR}/deploy")

# 배포용 폴더 생성 및 파일 복사
add_custom_target(deploy ALL
    DEPENDS Inspector
    COMMENT "Deploying application to ${DEPLOY_DIR}"
)

# 실행 파일 복사
add_custom_command(TARGET deploy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPLOY_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:Inspector> ${DEPLOY_DIR}/
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

# train_patchcore_anomalib.py 스크립트 복사
if(EXISTS "${CMAKE_SOURCE_DIR}/train_patchcore_anomalib.py")
    add_custom_command(TARGET deploy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            ${CMAKE_SOURCE_DIR}/train_patchcore_anomalib.py ${DEPLOY_DIR}/
        COMMENT "Copying training script"
    )
endif()

# OpenCV 디버그 정보 출력 (선택 사항)
message(STATUS "OpenCV_LIBS: ${OpenCV_LIBS}")
message(STATUS "Deploy directory: ${DEPLOY_DIR}")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# 설치 타겟 설정 (make install 명령으로 실행)
install(TARGETS Inspector 
    DESTINATION bin
    COMPONENT runtime
)

# 설치 디렉토리 기본값 설정
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Install prefix" FORCE)
endif()