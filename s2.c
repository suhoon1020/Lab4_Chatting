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
#define MAX_FILENAME 256

// Message types
typedef enum {
	MSG_CHAT = 1,
	MSG_FILE_START,
	MSG_FILE_DATA,
	MSG_FILE_END,
	MSG_JOIN_ROOM,
	MSG_LEAVE_ROOM,
	MSG_LIST_ROOMS,
	MSG_LIST_USERS
} MessageType;

// Message structure
typedef struct {
	MessageType type;
	char sender[MAX_USERNAME];
	char content[BUFFER_SIZE];
	int room_id;
	size_t size;  // For file transfer
} Message;

// User structure
typedef struct {
	int socket;
	char username[MAX_USERNAME];
	int room_id;
	int is_active;
} User;

// Chat room structure
typedef struct {
	int id;
	char name[50];
	int user_count;
	User* users[MAX_CLIENTS];
} ChatRoom;

// Global variables
User* clients[MAX_CLIENTS];
ChatRoom* rooms[MAX_ROOMS];
int client_count = 0;
int room_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rooms_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function declarations
void* handle_client(void* arg);
void broadcast_message(Message* msg, int sender_socket);
int create_room(const char* room_name);
int join_room(int room_id, User* user);
void leave_room(User* user);
void remove_client(int socket);
void list_rooms(int socket);
void list_users(int room_id, int socket);
void handle_file_transfer(User* user, Message* msg);

int main(int argc, char* argv[]) {
	if (argc != 2) {
		printf("Usage: %s <port>\n", argv[0]);
		return 1;
	}

	// Create server socket
	int server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (server_socket == -1) {
		perror("Socket creation failed");
		return 1;
	}

	// Configure server address
	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(atoi(argv[1]));

	// Set socket options
	int opt = 1;
	if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		perror("Setsockopt failed");
		return 1;
	}

	// Bind socket
	if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		perror("Bind failed");
		return 1;
	}

	// Listen for connections
	if (listen(server_socket, 10) < 0) {
		perror("Listen failed");
		return 1;
	}

	printf("Chat server started on port %s\n", argv[1]);

	// Create default room
	create_room("General");

	// Accept client connections
	while (1) {
		struct sockaddr_in client_addr;
		socklen_t client_len = sizeof(client_addr);
		int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);

		if (client_socket < 0) {
			perror("Accept failed");
			continue;
		}

		// Create new client
		User* new_client = (User*)malloc(sizeof(User));
		new_client->socket = client_socket;
		new_client->is_active = 1;
		new_client->room_id = -1;

		// Add client to array
		pthread_mutex_lock(&clients_mutex);
		if (client_count >= MAX_CLIENTS) {
			pthread_mutex_unlock(&clients_mutex);
			close(client_socket);
			free(new_client);
			continue;
		}
		clients[client_count++] = new_client;
		pthread_mutex_unlock(&clients_mutex);

		// Create thread for client
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

void* handle_client(void* arg) {
	User* user = (User*)arg;
	Message msg;

	// Receive username
	recv(user->socket, user->username, MAX_USERNAME, 0);
	printf("User logged in: %s\n", user->username);

	// Join default room
	join_room(0, user);

	// Message handling loop
	while (1) {
		int bytes_received = recv(user->socket, &msg, sizeof(Message), 0);
		if (bytes_received <= 0) {
			break;
		}

		switch (msg.type) {
			case MSG_CHAT:
				broadcast_message(&msg, user->socket);
				break;
			case MSG_FILE_START:
				handle_file_transfer(user, &msg);
				break;
			case MSG_JOIN_ROOM:
				leave_room(user);
				join_room(msg.room_id, user);
				break;
			case MSG_LIST_ROOMS:
				list_rooms(user->socket);
				break;
			case MSG_LIST_USERS:
				list_users(user->room_id, user->socket);
				break;
			default:
				break;
		}
	}

	remove_client(user->socket);
	return NULL;
}

void broadcast_message(Message* msg, int sender_socket) {
	pthread_mutex_lock(&rooms_mutex);
	ChatRoom* room = rooms[msg->room_id];

	for (int i = 0; i < room->user_count; i++) {
		if (room->users[i]->socket != sender_socket) {
			send(room->users[i]->socket, msg, sizeof(Message), 0);
		}
	}
	pthread_mutex_unlock(&rooms_mutex);
}

int create_room(const char* room_name) {
	pthread_mutex_lock(&rooms_mutex);
	if (room_count >= MAX_ROOMS) {
		pthread_mutex_unlock(&rooms_mutex);
		return -1;
	}

	ChatRoom* new_room = (ChatRoom*)malloc(sizeof(ChatRoom));
	new_room->id = room_count;
	strncpy(new_room->name, room_name, 49);
	new_room->user_count = 0;

	rooms[room_count++] = new_room;
	pthread_mutex_unlock(&rooms_mutex);
	return new_room->id;
}

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
	user->room_id = room_id;

	// Announce user joined
	Message msg = {
		.type = MSG_CHAT,
		.room_id = room_id
	};
	snprintf(msg.content, BUFFER_SIZE, "%s joined the room", user->username);
	broadcast_message(&msg, user->socket);

	pthread_mutex_unlock(&rooms_mutex);
	return 0;
}

void leave_room(User* user) {
	if (user->room_id == -1) return;

	pthread_mutex_lock(&rooms_mutex);
	ChatRoom* room = rooms[user->room_id];

	for (int i = 0; i < room->user_count; i++) {
		if (room->users[i]->socket == user->socket) {
			// Remove user from room
			for (int j = i; j < room->user_count - 1; j++) {
				room->users[j] = room->users[j + 1];
			}
			room->user_count--;
			break;
		}
	}

	// Announce user left
	Message msg = {
		.type = MSG_CHAT,
		.room_id = user->room_id
	};
	snprintf(msg.content, BUFFER_SIZE, "%s left the room", user->username);
	broadcast_message(&msg, user->socket);

	user->room_id = -1;
	pthread_mutex_unlock(&rooms_mutex);
}

void remove_client(int socket) {
	pthread_mutex_lock(&clients_mutex);
	for (int i = 0; i < client_count; i++) {
		if (clients[i]->socket == socket) {
			// Remove from room first
			leave_room(clients[i]);

			// Free memory and remove from clients array
			free(clients[i]);
			for (int j = i; j < client_count - 1; j++) {
				clients[j] = clients[j + 1];
			}
			client_count--;
			break;
		}
	}
	pthread_mutex_unlock(&clients_mutex);
	close(socket);
}

void list_rooms(int socket) {
	Message msg = {
		.type = MSG_LIST_ROOMS
	};

	pthread_mutex_lock(&rooms_mutex);
	for (int i = 0; i < room_count; i++) {
		snprintf(msg.content, BUFFER_SIZE, "Room %d: %s (%d users)",
				rooms[i]->id, rooms[i]->name, rooms[i]->user_count);
		send(socket, &msg, sizeof(Message), 0);
	}
	pthread_mutex_unlock(&rooms_mutex);
}

void list_users(int room_id, int socket) {
	Message msg = {
		.type = MSG_LIST_USERS
	};

	pthread_mutex_lock(&rooms_mutex);
	if (room_id >= 0 && room_id < room_count) {
		ChatRoom* room = rooms[room_id];
		for (int i = 0; i < room->user_count; i++) {
			snprintf(msg.content, BUFFER_SIZE, "%s", room->users[i]->username);
			send(socket, &msg, sizeof(Message), 0);
		}
	}
	pthread_mutex_unlock(&rooms_mutex);
}

void handle_file_transfer(User* user, Message* msg) {
	// Broadcast file start message
	broadcast_message(msg, user->socket);

	// Receive and broadcast file data
	char buffer[BUFFER_SIZE];
	size_t remaining = msg->size;
	while (remaining > 0) {
		size_t to_read = remaining < BUFFER_SIZE ? remaining : BUFFER_SIZE;
		ssize_t received = recv(user->socket, buffer, to_read, 0);
		if (received <= 0) break;

		Message data_msg = {
			.type = MSG_FILE_DATA,
			.room_id = user->room_id,
			.size = received
		};
		memcpy(data_msg.content, buffer, received);
		broadcast_message(&data_msg, user->socket);
		remaining -= received;
	}

	// Broadcast file end message
	msg->type = MSG_FILE_END;
	broadcast_message(msg, user->socket);
}
