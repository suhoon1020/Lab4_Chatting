#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024

typedef struct {
	int sock;
	char username[32];
} Client;

Client clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void broadcast_message(const char *message, int sender_sock) {
	pthread_mutex_lock(&clients_mutex);
	for (int i = 0; i < client_count; i++) {
		if (clients[i].sock != sender_sock) {
			send(clients[i].sock, message, strlen(message), 0);
		}
	}
	pthread_mutex_unlock(&clients_mutex);
}

void *handle_client(void *arg) {
	int client_sock = *((int *)arg);
	free(arg);
	char buffer[BUFFER_SIZE];
	char username[32];

	recv(client_sock, username, sizeof(username), 0);
	snprintf(buffer, sizeof(buffer), "%s has joined the chat.\n", username);
	broadcast_message(buffer, client_sock);

	pthread_mutex_lock(&clients_mutex);
	clients[client_count].sock = client_sock;
	strcpy(clients[client_count].username, username);
	client_count++;
	pthread_mutex_unlock(&clients_mutex);

	while (1) {
		int bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
		if (bytes_received <= 0) {
			break;
		}

		buffer[bytes_received] = '\0';
		broadcast_message(buffer, client_sock);
	}

	close(client_sock);

	pthread_mutex_lock(&clients_mutex);
	for (int i = 0; i < client_count; i++) {
		if (clients[i].sock == client_sock) {
			for (int j = i; j < client_count - 1; j++) {
				clients[j] = clients[j + 1];
			}
			client_count--;
			break;
		}
	}
	pthread_mutex_unlock(&clients_mutex);

	snprintf(buffer, sizeof(buffer), "%s has left the chat.\n", username);
	broadcast_message(buffer, -1);

	return NULL;
}

int main() {
	int server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock == -1) {
		perror("Socket creation failed");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(8080);

	if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
		perror("Bind failed");
		close(server_sock);
		exit(EXIT_FAILURE);
	}

	if (listen(server_sock, 10) == -1) {
		perror("Listen failed");
		close(server_sock);
		exit(EXIT_FAILURE);
	}

	printf("Server started on port 8080\n");

	while (1) {
		struct sockaddr_in client_addr;
		socklen_t client_len = sizeof(client_addr);
		int *client_sock = malloc(sizeof(int));
		*client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);

		if (*client_sock == -1) {
			perror("Accept failed");
			free(client_sock);
			continue;
		}

		pthread_t tid;
		pthread_create(&tid, NULL, handle_client, client_sock);
		pthread_detach(tid);
	}

	close(server_sock);
	return 0;
}

