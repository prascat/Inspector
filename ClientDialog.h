#ifndef CLIENTDIALOG_H
#define CLIENTDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTcpSocket>
#include <QTimer>
#include <QCheckBox>
#include <QThread>
#include <QJsonObject>
#include <QByteArray>
#include <cstdint>

// 메시지 타입
enum class MessageType : uint32_t {
    RECIPE_ALL_REQUEST = 0x01,   // Vision → Lims: 회로 목록 전체 요청
    RECIPE_ALL_RESPONSE = 0x02,  // Lims → Vision: JSON(회로명, 전선, 전선길이, 단자, 씰)
    RECIPE_READY = 0x03,         // Lims → Vision: Lims가 선택한 회로선택 Vision에 요청
    RECIPE_OK = 0x04,            // Vision → Lims: Vision에서 동일한 회로 레시피 로드 성공
    RECIPE_EMPTY = 0x05,         // Vision → Lims: Lims에 회로의 레시피가 Vision에 없음
    INSPECT_RESPONSE = 0x06,     // Vision → Lims: Vision 검사결과 (해당 불량 카메라)
    HEARTBEAT_OK = 0x07,         // Vision → Lims: 30초 주기
    ERROR = 0xFF                 // 오류
};

// 프로토콜 헤더 (32바이트)
#pragma pack(push, 1)
struct ProtocolHeader {
    uint32_t stx;              // 0x02020202
    uint32_t messageType;      // MessageType
    uint32_t sequenceNumber;   // 자동 증가
    int32_t dataLength;        // 본문 길이
    int64_t timestamp;         // Unix epoch milliseconds
    uint32_t checksum;         // CRC32 (현재 미사용)
    uint32_t reserved;         // 예약
};
#pragma pack(pop)

class ClientDialog : public QDialog {
    Q_OBJECT

public:
    static ClientDialog* instance(QWidget* parent = nullptr);
    ~ClientDialog();
    
    int exec() override;
    
    // 연결 관리
    void initialize();
    bool isServerConnected() const { return isConnected; }
    bool isSocketConnected() const { return testSocket && testSocket->state() == QAbstractSocket::ConnectedState; }

    // 프로토콜 메시지 전송
    bool sendInspectionResult(const QJsonObject& result);
    bool sendHeartbeat();
    bool sendProtocolMessage(MessageType type, const QByteArray& jsonData);
    
    // 레거시 메시지 전송 (하위 호환)
    bool sendMessage(const QString& message);
    bool sendData(const QByteArray& data);

    // 설정 값 가져오기
    QString getServerIp() const { return serverIp; }
    int getServerPort() const { return serverPort; }
    bool isAutoConnect() const { return autoConnect; }
    int getReconnectInterval() const { return reconnectInterval; }
    
    // 설정 값 설정하기
    void setServerIp(const QString& ip);
    void setServerPort(int port);
    void setAutoConnect(bool enable);
    void setReconnectInterval(int seconds);
    
    // 재연결 스레드 제어
    void startReconnectThread();
    void stopReconnectThread();

signals:
    void settingsChanged();
    void inspectionRequestReceived(const QJsonObject& request);
    void recipeListReceived(const QJsonArray& recipes);  // 서버에서 레시피 목록 수신
    void recipeReadyReceived(const QJsonObject& request);  // 서버에서 레시피 준비 요청 수신

private slots:
    void onTestConnection();
    void onSaveSettings();
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onDataReceived();
    void updateConnectionStatus();
    void tryReconnect();

private:
    explicit ClientDialog(QWidget* parent = nullptr);
    static ClientDialog* m_instance;
    
    void setupUI();
    void loadSettings();
    void saveSettings();
    void updateLanguage();
    
    // 프로토콜 처리
    void processReceivedData();
    bool parseHeader(const QByteArray& headerData, ProtocolHeader& header);
    void handleRecipeReady(const QJsonObject& request);
    void handleRecipeAllResponse(const QJsonDocument& response);

    // UI 요소
    QLineEdit* ipEdit;
    QLineEdit* portEdit;
    QLineEdit* reconnectIntervalEdit;
    QLineEdit* heartbeatIntervalEdit;
    QCheckBox* autoConnectCheckBox;
    QPushButton* testButton;
    QPushButton* saveButton;
    QPushButton* cancelButton;
    QLabel* statusLabel;
    QLabel* connectionStatusLabel;
    
    // 설정 값
    QString serverIp;
    int serverPort;
    bool autoConnect;
    int reconnectInterval;
    int heartbeatInterval;
    
    // TCP 소켓
    QTcpSocket* testSocket;
    QTimer* statusTimer;
    QTimer* heartbeatTimer;  // Heartbeat 타이머
    
    // 재연결 스레드
    QThread* reconnectThread;
    bool shouldReconnect;
    
    // 연결 상태
    bool isConnected;
    
    // 프로토콜 상태
    QByteArray receiveBuffer;
    uint32_t sequenceNumber;
    static constexpr uint32_t STX = 0x02020202;
    static constexpr int MAX_DATA_LENGTH = 10 * 1024 * 1024;  // 10MB
    static constexpr int HEARTBEAT_INTERVAL = 30000;  // 30초 (milliseconds)
};

#endif // CLIENTDIALOG_H
