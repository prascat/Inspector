#!/bin/bash

echo "=== Spinnaker 카메라 리셋 스크립트 ==="

# 1. 공유 메모리 정리
echo "1. 공유 메모리 정리 중..."
for id in $(ipcs -m | grep $USER | awk '{print $2}'); do
    ipcrm -m $id 2>/dev/null
done

# 2. 세마포어 정리
echo "2. 세마포어 정리 중..."
for id in $(ipcs -s | grep $USER | awk '{print $2}'); do
    ipcrm -s $id 2>/dev/null
done

# 3. USB 리셋
echo "3. USB 장치 리셋 중..."
for dev in /dev/bus/usb/002/{003,004}; do
    if [ -e "$dev" ]; then
        sudo chmod 666 "$dev"
    fi
done

# 4. Spinnaker 임시 파일 정리
echo "4. Spinnaker 임시 파일 정리 중..."
rm -rf /tmp/spinnaker_* 2>/dev/null
rm -rf /var/tmp/spinnaker_* 2>/dev/null

echo "=== 리셋 완료 ==="
echo "이제 프로그램을 실행하세요."
