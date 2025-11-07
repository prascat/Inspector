// 헤더 파일에서 존재하지 않는 함수들 제거

#ifndef RECIPEMANAGER_H
#define RECIPEMANAGER_H

#include <QString>
#include <QList>
#include <QMap>
#include <QXmlStreamWriter>
#include <QXmlStreamReader>
#include <QTreeWidgetItem>
#include <functional>
#include <opencv2/opencv.hpp>
#include "CommonDefs.h"

class CameraView;

class RecipeManager {
public:
    RecipeManager();
    
    // 메인 저장/로드 함수
    bool saveRecipe(const QString& fileName, 
                   const QVector<CameraInfo>& cameraInfos, 
                   int currentCameraIndex,
                   const QMap<QString, CalibrationInfo>& calibrationMap,
                   CameraView* cameraView,
                   const QStringList& simulationImagePaths = QStringList(),
                   int simulationCurrentIndex = 0,
                   const QStringList& trainingImagePaths = QStringList(),
                   class TeachingWidget* teachingWidget = nullptr);
                   
    bool loadRecipe(const QString& fileName,
                   QVector<CameraInfo>& cameraInfos,
                   QMap<QString, CalibrationInfo>& calibrationMap,
                   CameraView* cameraView,
                   QTreeWidget* patternTree,
                   std::function<void(const QStringList&)> trainingImageCallback = nullptr,
                   class TeachingWidget* teachingWidget = nullptr);
    
    // 시뮬레이션 모드 저장/로드 함수
    bool saveSimulationRecipe(const QString& fileName,
                             const QString& projectName,
                             const QStringList& imagePaths,
                             int currentIndex);
                             
    bool loadSimulationRecipe(const QString& fileName,
                             const QString& projectName,
                             QStringList& imagePaths,
                             int& currentIndex);
    
    // 에러 메시지 가져오기
    QString getLastError() const { return lastError; }
    
    // === 새로운 개별 레시피 관리 함수들 ===
    // 개별 레시피 파일 관리 (시뮬레이션용)
    bool saveRecipeByName(const QString& recipeName, const QVector<PatternInfo>& patterns);
    bool loadRecipeByName(const QString& recipeName, QVector<PatternInfo>& patterns);
    QStringList getAvailableRecipes();
    bool deleteRecipe(const QString& recipeName);
    bool renameRecipe(const QString& oldName, const QString& newName);
    
    // 레시피 디렉토리 관리
    QString getRecipesDirectory();
    bool createRecipesDirectory();
    
    // 레시피에서 카메라 정보 읽기 (시뮬레이션용)
    QStringList getRecipeCameraUuids(const QString& recipeName);
    
    // 기존 레시피에서 메인 카메라 티칭 이미지 불러오기
    bool loadMainCameraImage(const QString& recipeName, cv::Mat& outImage, QString& outCameraName);
    
private:
    // 헬퍼 함수들
    QString copyImageToRecipeFolder(const QString& originalPath, const QString& recipeName);
    QStringList copyImagesToRecipeFolder(const QStringList& imagePaths, const QString& recipeName);
    
private:
    QString lastError;
    QList<PatternInfo> tempChildPatterns; // **임시 자식 패턴 저장용**
    
    QStringList readChildPatterns(QXmlStreamReader& xml, const QString& cameraUuid, 
        const QUuid& parentId);
    // === 저장 관련 함수들 ===
    void writeCalibrationInfo(QXmlStreamWriter& xml, const CalibrationInfo& calibInfo);
    void writeCameraSettings(QXmlStreamWriter& xml, const CameraInfo& cameraInfo);
    
    // 패턴 저장
    void writeROIPatterns(QXmlStreamWriter& xml, const QList<PatternInfo>& allPatterns, 
                         const QString& cameraUuid, QList<QUuid>& processedPatterns);
    void writeFIDPatterns(QXmlStreamWriter& xml, const QList<PatternInfo>& allPatterns, 
                         const QString& cameraUuid, QList<QUuid>& processedPatterns);
    void writeIndependentPatterns(QXmlStreamWriter& xml, const QList<PatternInfo>& allPatterns, 
                                 const QString& cameraUuid, QList<QUuid>& processedPatterns);
    
    // 패턴 세부 정보 저장
    void writePatternHeader(QXmlStreamWriter& xml, const PatternInfo& pattern);
    void writePatternRect(QXmlStreamWriter& xml, const PatternInfo& pattern);
    void writeROIDetails(QXmlStreamWriter& xml, const PatternInfo& pattern);
    void writeFIDDetails(QXmlStreamWriter& xml, const PatternInfo& pattern);
    void writeINSDetails(QXmlStreamWriter& xml, const PatternInfo& pattern);
    void writePatternFilters(QXmlStreamWriter& xml, const PatternInfo& pattern);
    void writePatternChildren(QXmlStreamWriter& xml, const PatternInfo& pattern, 
                             const QList<PatternInfo>& allPatterns, 
                             QList<QUuid>& processedPatterns);
    
    // === 로드 관련 함수들 ===
    bool readCameraSection(QXmlStreamReader& xml, 
                          QVector<CameraInfo>& cameraInfos,
                          QMap<QString, CalibrationInfo>& calibrationMap,
                          CameraView* cameraView,
                          QTreeWidget* patternTree,
                          QMap<QString, QStringList>& childrenMap,
                          QMap<QString, QTreeWidgetItem*>& itemMap,
                          int& totalLoadedPatterns,
                          QString& loadedCameraNames,
                          std::function<void(const QStringList&)> trainingImageCallback = nullptr,
                          TeachingWidget* teachingWidget = nullptr);
    
    CalibrationInfo readCalibrationInfo(QXmlStreamReader& xml);
    PatternInfo readPattern(QXmlStreamReader& xml, const QString& cameraUuid);
    void readPatternRect(QXmlStreamReader& xml, PatternInfo& pattern);
    void readPatternDetails(QXmlStreamReader& xml, PatternInfo& pattern);
    void readROIDetails(QXmlStreamReader& xml, PatternInfo& pattern);
    void readFIDDetails(QXmlStreamReader& xml, PatternInfo& pattern);
    void readINSDetails(QXmlStreamReader& xml, PatternInfo& pattern);
    void readPatternFilters(QXmlStreamReader& xml, PatternInfo& pattern);
    QStringList readPatternChildren(QXmlStreamReader& xml);
    
    // 패턴 관계 복원
    void restorePatternRelationships(const QMap<QString, QStringList>& childrenMap,
                                   const QMap<QString, QTreeWidgetItem*>& itemMap,
                                   CameraView* cameraView);
    
    // 유틸리티 함수들
    QTreeWidgetItem* createPatternTreeItem(const PatternInfo& pattern);
    
    void setError(const QString& error) { lastError = error; }
};
#endif // RECIPEMANAGER_H