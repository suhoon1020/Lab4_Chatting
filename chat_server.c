#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_CLIENTS 100
#define MAX_ROOMS 10
#define BUFFER_SIZE 1024
#define MAX_USERNAME 50

// 사용자 상태 정의
#define STATUS_ONLINE 1
#define STATUS_OFFLINE 0

// 메시지 타입 정의
#define MSG_TYPE_CHAT 1
#define MSG_TYPE_FILE_START 2
#define MSG_TYPE_FILE_DATA 3
#define MSG_TYPE_FILE_END 4

// 파일 전송을 위한 헤더 구조체
typedef struct {
	    int type;           // 메시지 타입
		    char filename[256]; // 파일 이름
			    size_t size;       // 파일 크기
				    char sender[50];   // 보내는 사람
} FileHeader;

// 사용자 구조체
typedef struct {
	char username[MAX_USERNAME];
	char user_id[MAX_USERNAME];
	int socket_fd;
	int status;
} User;

// 채팅방 구조체
typedef struct {
	int room_id;
	char room_name[50];
	User* users[MAX_CLIENTS];
	int user_count;
} ChatRoom;

// 전역 변수
User* clients[MAX_CLIENTS];
ChatRoom* rooms[MAX_ROOMS];
int client_count = 0;
int room_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rooms_mutex = PTHREAD_MUTEX_INITIALIZER;

// 함수 선언
void* handle_client(void* arg);
void broadcast_message(int sender_socket, const char* message, int room_id);
int create_room(const char* room_name);
int join_room(int room_id, User* user);
void remove_client(int socket_fd);
void handle_file_transfer(User* sender, FileHeader* header, int room_id);

int main(int argc, char* argv[]) {
	if (argc != 2) {
		printf("Usage: %s <port>\n", argv[0]);
		return 1;
	}

	int server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (server_socket == -1) {
		perror("Socket creation failed");
		return 1;
	}

	// 서버 주소 설정
	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(atoi(argv[1]));

	// 소켓 옵션 설정 (재사용)
	int opt = 1;
	if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		perror("Setsockopt failed");
		return 1;
	}

	// 바인딩
	if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		perror("Bind failed");
		return 1;
	}

	// 리스닝
	if (listen(server_socket, 10) < 0) {
		perror("Listen failed");
		return 1;
	}

	printf("Chat server started on port %s\n", argv[1]);

	// 기본 채팅방 생성
	create_room("General");

	// 클라이언트 연결 수락
	while (1) {
		struct sockaddr_in client_addr;
		socklen_t client_len = sizeof(client_addr);
		int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);

		if (client_socket < 0) {
			perror("Accept failed");
			continue;
		}

		// 새 클라이언트 생성
		User* new_client = (User*)malloc(sizeof(User));
		new_client->socket_fd = client_socket;
		new_client->status = STATUS_ONLINE;
		sprintf(new_client->user_id, "user_%d", client_count + 1);

		// 클라이언트 목록에 추가
		pthread_mutex_lock(&clients_mutex);
		if (client_count >= MAX_CLIENTS) {
			pthread_mutex_unlock(&clients_mutex);
			close(client_socket);
			free(new_client);
			continue;
		}
		clients[client_count++] = new_client;
		pthread_mutex_unlock(&clients_mutex);

		// 클라이언트 처리 스레드 생성
		pthread_t thread_id;
		if (pthread_create(&thread_id, NULL, handle_client, (void*)new_client) != 0) {
			perror("Thread creation failed");
			remove_client(client_socket);
			continue;
		}
		pthread_detach(thread_id);

		printf("New client connected: %s\n", inet_ntoa(client_addr.sin_addr));
	}

	close(server_socket);
	return 0;
}

// 클라이언트 처리 함수
void* handle_client(void* arg) {
	User* user = (User*)arg;
	char buffer[BUFFER_SIZE];
	FileHeader file_header;

	// 사용자 이름 수신
	int read_size = recv(user->socket_fd, buffer, sizeof(buffer), 0);
	if (read_size > 0) {
		buffer[read_size] = '\0';
		strncpy(user->username, buffer, MAX_USERNAME - 1);
		join_room(0, user);
	}

	// 메시지 수신 루프
	while ((read_size = recv(user->socket_fd, buffer, sizeof(buffer), 0)) > 0) {
		if (read_size >= sizeof(FileHeader)) {
			memcpy(&file_header, buffer, sizeof(FileHeader));
			if (file_header.type == MSG_TYPE_FILE_START) {
				handle_file_transfer(user, &file_header, 0);
				continue;
			}
		}

		// 일반 채팅 메시지 처리
		buffer[read_size] = '\0';
		broadcast_message(user->socket_fd, buffer, 0);
	}

	remove_client(user->socket_fd);
	return NULL;
}


// 메시지 브로드캐스트 함수
void broadcast_message(int sender_socket, const char* message, int room_id) {
	pthread_mutex_lock(&rooms_mutex);
	if (room_id >= room_count) {
		pthread_mutex_unlock(&rooms_mutex);
		return;
	}

	ChatRoom* room = rooms[room_id];
	for (int i = 0; i < room->user_count; i++) {
		if (room->users[i]->socket_fd != sender_socket) {
			send(room->users[i]->socket_fd, message, strlen(message), 0);
		}
	}
	pthread_mutex_unlock(&rooms_mutex);
}

// 채팅방 생성 함수
int create_room(const char* room_name) {
	pthread_mutex_lock(&rooms_mutex);
	if (room_count >= MAX_ROOMS) {
		pthread_mutex_unlock(&rooms_mutex);
		return -1;
	}

	ChatRoom* new_room = (ChatRoom*)malloc(sizeof(ChatRoom));
	new_room->room_id = room_count;
	strncpy(new_room->room_name, room_name, 49);
	new_room->user_count = 0;

	rooms[room_count++] = new_room;
	pthread_mutex_unlock(&rooms_mutex);
	return new_room->room_id;
}

// 채팅방 입장 함수
int join_room(int room_id, User* user) {
	pthread_mutex_lock(&rooms_mutex);
	if (room_id >= room_count) {
		pthread_mutex_unlock(&rooms_mutex);
		return -1;
	}

	ChatRoom* room = rooms[room_id];
	if (room->user_count >= MAX_CLIENTS) {
		pthread_mutex_unlock(&rooms_mutex);
		return -1;
	}

	room->users[room->user_count++] = user;
	pthread_mutex_unlock(&rooms_mutex);
	return 0;
}

// 클라이언트 제거 함수
void remove_client(int socket_fd) {
	pthread_mutex_lock(&clients_mutex);
	for (int i = 0; i < client_count; i++) {
		if (clients[i]->socket_fd == socket_fd) {
			// 채팅방에서도 제거
			pthread_mutex_lock(&rooms_mutex);
			for (int j = 0; j < room_count; j++) {
				ChatRoom* room = rooms[j];
				for (int k = 0; k < room->user_count; k++) {
					if (room->users[k]->socket_fd == socket_fd) {
						// 유저를 채팅방에서 제거하고 배열 정리
						for (int l = k; l < room->user_count - 1; l++) {
							room->users[l] = room->users[l + 1];
						}
						room->user_count--;
						break;
					}
				}
			}
			pthread_mutex_unlock(&rooms_mutex);

			// 클라이언트 메모리 해제 및 배열 정리
			free(clients[i]);
			for (int j = i; j < client_count - 1; j++) {
				clients[j] = clients[j + 1];
			}
			client_count--;
			break;
		}
	}
	pthread_mutex_unlock(&clients_mutex);
	close(socket_fd);
}

void handle_file_transfer(User* sender, FileHeader* header, int room_id) {
	// 파일 수신 시작 알림 브로드캐스트
	char notification[512];
	snprintf(notification, sizeof(notification), 
			"User %s is sending file: %s (%zu bytes)",
			sender->username, header->filename, header->size);
	broadcast_message(sender->socket_fd, notification, room_id);

	// 파일 데이터 수신 및 브로드캐스트
	char buffer[8192];
	size_t remaining = header->size;
	while (remaining > 0) {
		size_t to_read = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
		ssize_t received = recv(sender->socket_fd, buffer, to_read, 0);
		if (received <= 0) break;

		broadcast_message(sender->socket_fd, buffer, room_id);
		remaining -= received;
	}
}
