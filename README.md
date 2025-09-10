# Machine Vision Processing Software (MV)

## Overview

Machine Vision Processing (MV) is a comprehensive industrial machine vision software solution featuring advanced pattern matching, real-time filtering, and automated inspection capabilities. Built with Qt5 and OpenCV, this software provides a robust platform for industrial quality control and automated optical inspection.

## Key Features

### 🎯 **Advanced Pattern Matching System**
- **FID (Fiducial) Pattern Recognition**: High-precision template matching with sub-pixel accuracy
- **INS (Inspection) Pattern Verification**: Multi-mode inspection including color, binary, and edge detection
- **ROI (Region of Interest) Management**: Flexible region definition and management
- **Filter Pattern Support**: Dynamic masking and filtering regions

### 🔍 **Intelligent Inspection Modes**
- **Color Inspection**: RGB color matching with customizable thresholds
- **Binary Inspection**: Threshold-based binary analysis
- **Edge Detection**: Sobel and Canny edge detection with contour analysis
- **Template Matching**: Cross-correlation based pattern recognition with rotation support

### 🎨 **Real-time Image Processing Pipeline**
- **10+ Filter Types**: Comprehensive filter library including:
  - Threshold (Binary/Adaptive)
  - Gaussian Blur & Motion Blur
  - Canny & Sobel Edge Detection
  - Laplacian Sharpening
  - Brightness/Contrast Adjustment
  - Contour Detection & Analysis
  - Dynamic Masking
- **Real-time Filter Application**: Live preview with immediate feedback
- **Filter Chain Management**: Sequential filter application with custom parameters
- **Template-aware Filtering**: Filters applied to both inspection and template images

### 🖥️ **Multi-Camera Support**
- **4-Camera Configuration**: Simultaneous multi-camera operation
- **Camera Switching**: Seamless switching between camera feeds
- **Individual Camera Settings**: Per-camera configuration and calibration
- **Real-time Frame Processing**: High-speed image acquisition and processing

### 🎛️ **Advanced User Interface**
- **Dual-Mode Operation**: 
  - **Recipe Mode**: Pattern creation and configuration
  - **Test Mode**: Live inspection and verification
- **Simulation Mode**: Offline testing with static images
- **Multi-language Support**: Korean/English language switching
- **Hierarchical Pattern Tree**: Organized pattern and filter management
- **Real-time Property Panels**: Live parameter adjustment with immediate feedback

### 💾 **Recipe Management System**
- **XML-based Recipe Storage**: Comprehensive pattern and filter configuration saving
- **Template Image Storage**: Base64-encoded template images with applied filters
- **Pattern Hierarchy**: Parent-child pattern relationships
- **Filter Configuration Persistence**: Complete filter chain and parameter storage
- **Backup and Restore**: Recipe backup and recovery functionality

### 🔧 **Advanced Filter Features**
- **White Region Masking**: Intelligent masking for improved template matching accuracy
- **Real-time Template Updates**: Dynamic template image generation with applied filters
- **Mask-based Template Matching**: OpenCV mask parameter support for enhanced accuracy
- **Filter Parameter Live Preview**: Real-time filter application during parameter adjustment
- **Cross-pattern Filter Effects**: Mask filters affecting multiple inspection patterns

## Technical Architecture

### Core Components
- **TeachingWidget**: Main UI controller with pattern management and real-time processing
- **CameraView**: Custom OpenGL-accelerated image display with interactive pattern editing
- **FilterDialog**: Advanced filter configuration with real-time preview
- **InsProcessor**: High-performance inspection engine with multi-mode support
- **ImageProcessor**: Optimized OpenCV-based filter processing pipeline
- **RecipeManager**: XML-based recipe serialization and management

### Pattern Types
1. **FID (Fiducial) Patterns**: Reference markers for coordinate system alignment
2. **INS (Inspection) Patterns**: Quality control regions with pass/fail criteria
3. **ROI (Region of Interest)**: General purpose regions for analysis
4. **Filter Patterns**: Dynamic masking regions for image preprocessing

### Inspection Workflow
1. **Pattern Registration**: Define inspection areas and reference templates
2. **Filter Application**: Apply preprocessing filters to enhance detection
3. **Template Generation**: Create filtered template images for matching
4. **Real-time Inspection**: Live pattern matching and quality verification
5. **Result Analysis**: Comprehensive pass/fail reporting with detailed metrics

## Technical Specifications

- **Framework**: Qt 5.15+ with C++17
- **Image Processing**: OpenCV 4.5+
- **Build System**: CMake 3.16+
- **Platform Support**: Windows, Linux, macOS
- **Camera Interfaces**: Industrial camera APIs (configurable)
- **Image Formats**: RGB, Grayscale, Binary
- **Performance**: Real-time processing at 30+ FPS
- **Memory Management**: Optimized for continuous operation

## Advanced Features

### 🎯 **Precision Template Matching**
- **Sub-pixel Accuracy**: Template matching with interpolation
- **Rotation Compensation**: Angular template matching with configurable ranges
- **Scale Invariance**: Multi-scale template detection
- **Mask-based Matching**: White region exclusion for improved accuracy

### 🔄 **Real-time Processing**
- **Live Filter Preview**: Immediate visual feedback during parameter adjustment
- **Template Auto-update**: Dynamic template regeneration when filters change
- **Multi-threaded Processing**: Parallel filter application for optimal performance
- **Memory Optimization**: Efficient image buffer management

### 📊 **Quality Control**
- **Pass/Fail Criteria**: Configurable threshold-based inspection
- **Statistical Analysis**: Mean, standard deviation, and histogram analysis
- **Defect Detection**: Automated anomaly identification
- **Measurement Tools**: Dimensional analysis and verification

### 🎨 **User Experience**
- **Intuitive Interface**: Drag-and-drop pattern creation
- **Visual Feedback**: Real-time highlighting and overlay graphics
- **Keyboard Shortcuts**: Efficient workflow navigation
- **Context Menus**: Right-click access to common functions
- **Undo/Redo Support**: Action history management

## Recent Enhancements (2025)

### 🆕 **Latest Updates**
- **Enhanced FID Matching**: Improved accuracy with white region masking
- **Real-time Template Updates**: Filter changes immediately reflected in templates
- **Simulation Mode Support**: Complete offline testing capability
- **Cross-pattern Filter Effects**: Mask filters affecting overlapping patterns
- **Property Panel Integration**: Unified real-time parameter adjustment
- **Debug Logging**: Comprehensive debugging and performance monitoring

### 🔧 **Filter System Improvements**
- **Live Parameter Adjustment**: Real-time filter application during value changes
- **Template-Filter Synchronization**: Automatic template updates when filters change
- **Mask Filter Optimization**: Improved performance for dynamic masking
- **Filter Chain Validation**: Automatic parameter validation and adjustment

## Installation and Usage

### Build Requirements
```bash
# Dependencies
Qt 5.15+
OpenCV 4.5+
CMake 3.16+
C++17 compatible compiler

# Build Commands
mkdir build && cd build
cmake ..
make -j4
```

### Quick Start
1. **Launch Application**: Run `./MV` from build directory
2. **Configure Cameras**: Set up camera connections and parameters
3. **Create Recipe**: Define patterns and inspection criteria
4. **Test Inspection**: Verify pattern detection and quality control
5. **Save Configuration**: Store recipe for production use

## Applications

### Industrial Use Cases
- **Electronics Manufacturing**: PCB inspection and component verification
- **Automotive Quality Control**: Part alignment and defect detection
- **Pharmaceutical Packaging**: Label verification and container inspection
- **Food Processing**: Quality assessment and contamination detection
- **Textile Industry**: Pattern matching and defect identification

### Inspection Capabilities
- **Dimensional Measurement**: Precise measurement with calibrated cameras
- **Surface Quality Assessment**: Scratch, dent, and discoloration detection
- **Assembly Verification**: Component presence and correct orientation
- **Code Reading**: Barcode and QR code verification
- **Color Matching**: Precise color consistency checking

## Future Development Roadmap

### Planned Features
- **AI/ML Integration**: Deep learning-based pattern recognition
- **Cloud Connectivity**: Remote monitoring and data analytics
- **Advanced Scripting**: Python integration for custom workflows
- **Enhanced Reporting**: Comprehensive quality control reporting
- **Mobile Interface**: Tablet-based remote control and monitoring

### Performance Optimizations
- **GPU Acceleration**: CUDA-based image processing
- **Parallel Processing**: Multi-core CPU optimization
- **Memory Management**: Reduced memory footprint for embedded systems
- **Network Optimization**: Efficient data transfer for distributed systems

---

**© 2025 KM DigiTech. Machine Vision Processing Software. All rights reserved.**  
*Professional industrial machine vision solution with advanced pattern matching and real-time inspection capabilities.*
