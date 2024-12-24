# open_source_LAB4
오픈소스 프로젝트 교과목 LAB 4 과제용 레포지토리

<h2> FTP 기반 파일 업로드 적용 방법</h2>

1. FTP 서버 설치
```
sudo apt install vsftpd
```

2. ftp 서버 설정 파일 구성
```
local_enable=YES
write_enable=YES
chroot_local_user=YES
allow_writeable_chroot=YES
```

3. 서버 재시작
```
sudo systemctl restart vsftpd
```

4. 업로드 디렉토리 생성
```
sudo mkdir -p /home/linux/uploads
sudo chmod 777 /home/linux/uploads
sudo chown linux:root /home/linux/uploads
```

5. FTP 클라이언트 테스트
```
ftp 127.0.0.1
```
아이디 비밀번호 설정
id : linux
password : 1234

6. libcurl 설치
```
sudo apt install curl libcurl4-openssl-dev
curl --version
```

7. libcurl 링크 옵션 포함 컴파일
```
gcc -o chat_client chat_client.c -lcurl `pkg-config --cflags --libs gtk+-3.0`
gcc -o chat_server chat_server.c -lcurl `pkg-config --cflags --libs gtk+-3.0`
```