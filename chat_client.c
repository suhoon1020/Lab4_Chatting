#include <gtk/gtk.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>

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

// 전역 변수
GtkWidget *login_window;
GtkWidget *chat_window;
GtkWidget *chat_view;
GtkWidget *message_entry;
GtkTextBuffer *chat_buffer;
int sock_fd = -1;
char username[50];
pthread_t receive_thread;
GtkBuilder *builder;

// 함수 선언
void* receive_message(void* arg);
void on_send_clicked(GtkButton *button, gpointer user_data);
void on_login_clicked(GtkButton *button, gpointer user_data);
void append_message(const char* message);
void show_error_message(const char* message);
void send_file(const char *filepath);
void on_file_button_clicked(GtkButton *button, gpointer user_data);
void handle_received_file(FileHeader* header, const char* data);

int main(int argc, char *argv[]) {
	gtk_init(&argc, &argv);

	// Glade 파일 로드
	builder = gtk_builder_new();
	GError *error = NULL;

	// 현재 디렉토리에서 glade 파일 로드 시도
	if (gtk_builder_add_from_file(builder, "chat_ui.xml", &error) == 0) {
		g_printerr("Error loading file: %s\n", error->message);
		g_clear_error(&error);
		show_error_message("Could not load UI file (chat.glade). Please check if the file exists.");
		return 1;
	}

	// 위젯 가져오기
	login_window = GTK_WIDGET(gtk_builder_get_object(builder, "login_window"));
	if (login_window == NULL) {
		show_error_message("Could not find login_window");
		return 1;
	}

	chat_window = GTK_WIDGET(gtk_builder_get_object(builder, "chat_window"));
	if (chat_window == NULL) {
		show_error_message("Could not find chat_window");
		return 1;
	}

	chat_view = GTK_WIDGET(gtk_builder_get_object(builder, "chat_view"));
	if (chat_view == NULL) {
		show_error_message("Could not find chat_view");
		return 1;
	}

	message_entry = GTK_WIDGET(gtk_builder_get_object(builder, "message_entry"));
	if (message_entry == NULL) {
		show_error_message("Could not find message_entry");
		return 1;
	}

	// 버튼 시그널 연결
	GtkWidget *login_button = GTK_WIDGET(gtk_builder_get_object(builder, "login_button"));
	if (login_button == NULL) {
		show_error_message("Could not find login_button");
		return 1;
	}
	g_signal_connect(login_button, "clicked", G_CALLBACK(on_login_clicked), NULL);

	GtkWidget *send_button = GTK_WIDGET(gtk_builder_get_object(builder, "send_button"));
	if (send_button == NULL) {
		show_error_message("Could not find send_button");
		return 1;
	}
	g_signal_connect(send_button, "clicked", G_CALLBACK(on_send_clicked), NULL);

	// 파일 버튼 시그널 연결 추가
	GtkWidget *file_button = GTK_WIDGET(gtk_builder_get_object(builder, "file_button"));
	if (file_button == NULL) {
		        show_error_message("Could not find file_button");
				        return 1;
	}
	g_signal_connect(file_button, "clicked", G_CALLBACK(on_file_button_clicked), NULL);



	// 채팅 버퍼 초기화
	chat_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat_view));

	// 윈도우 종료 시그널 연결
	g_signal_connect(login_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	g_signal_connect(chat_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	// 로그인 윈도우 표시
	gtk_widget_show_all(login_window);
	gtk_widget_hide(chat_window);

	gtk_main();
	return 0;
}

void show_error_message(const char* message) {
	GtkWidget *dialog = gtk_message_dialog_new(NULL,
			GTK_DIALOG_MODAL,
			GTK_MESSAGE_ERROR,
			GTK_BUTTONS_OK,
			"%s", message);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

void on_login_clicked(GtkButton *button, gpointer user_data) {
	GtkEntry *username_entry = GTK_ENTRY(gtk_builder_get_object(builder, "username_entry"));
	GtkEntry *server_entry = GTK_ENTRY(gtk_builder_get_object(builder, "server_entry"));
	GtkEntry *port_entry = GTK_ENTRY(gtk_builder_get_object(builder, "port_entry"));

	if (!username_entry || !server_entry || !port_entry) {
		show_error_message("Could not find input fields");
		return;
	}

	const char *server_ip = gtk_entry_get_text(server_entry);
	const char *port_str = gtk_entry_get_text(port_entry);
	const char *username_str = gtk_entry_get_text(username_entry);

	if (strlen(username_str) == 0) {
		show_error_message("Please enter a username");
		return;
	}

	// 서버 연결
	struct sockaddr_in server_addr;
	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(server_ip);
	server_addr.sin_port = htons(atoi(port_str));

	if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		show_error_message("Connection failed! Check server address and port.");
		return;
	}

	// 사용자 이름 저장 및 서버로 전송
	strncpy(username, username_str, sizeof(username) - 1);
	send(sock_fd, username, strlen(username), 0);

	// 수신 스레드 시작
	pthread_create(&receive_thread, NULL, receive_message, NULL);

	// 채팅 윈도우로 전환
	gtk_widget_hide(login_window);
	gtk_widget_show_all(chat_window);
}
void on_send_clicked(GtkButton *button, gpointer user_data) {
	const char *message = gtk_entry_get_text(GTK_ENTRY(message_entry));
	if (strlen(message) > 0) {
		// 메시지 포맷팅을 위한 버퍼
		char formatted_message[1100];  // username(50) + message(1024) + 추가 문자
		snprintf(formatted_message, sizeof(formatted_message), "%s: %s", username, message);

		// 서버로 전송
		send(sock_fd, formatted_message, strlen(formatted_message), 0);

		// 자신의 채팅창에도 표시
		append_message(formatted_message);

		// 입력창 초기화
		gtk_entry_set_text(GTK_ENTRY(message_entry), "");
	}
}
void append_message(const char* message) {
	GtkTextIter iter;
	gtk_text_buffer_get_end_iter(chat_buffer, &iter);
	gtk_text_buffer_insert(chat_buffer, &iter, message, -1);
	gtk_text_buffer_insert(chat_buffer, &iter, "\n", -1);
}

void* receive_message(void* arg) {
	char buffer[1024];
	int read_size;

	while ((read_size = recv(sock_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
		buffer[read_size] = '\0';
		gdk_threads_add_idle((GSourceFunc)append_message, g_strdup(buffer));
	}

	return NULL;
}

void send_file(const char *filepath) {
	FILE *file = fopen(filepath, "rb");
	if (!file) {
		show_error_message("Could not open file");
		return;
	}

	// 파일 크기 얻기
	fseek(file, 0, SEEK_END);
	size_t file_size = ftell(file);
	fseek(file, 0, SEEK_SET);

	// 파일 이름 추출
	char *filename = strrchr(filepath, '/');
	filename = filename ? filename + 1 : (char*)filepath;

	// 파일 헤더 전송
	FileHeader header;
	header.type = MSG_TYPE_FILE_START;
	header.size = file_size;
	strncpy(header.filename, filename, 255);
	strncpy(header.sender, username, 49);

	send(sock_fd, &header, sizeof(FileHeader), 0);

	// 파일 데이터 전송
	char buffer[8192];
	size_t bytes_read;
	while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		send(sock_fd, buffer, bytes_read, 0);
	}

	// 전송 완료 메시지
	header.type = MSG_TYPE_FILE_END;
	send(sock_fd, &header, sizeof(FileHeader), 0);

	fclose(file);

	// 채팅창에 알림 메시지 추가
	char msg[512];
	snprintf(msg, sizeof(msg), "File '%s' sent successfully", filename);
	append_message(msg);
}

// 파일 선택 버튼 핸들러
void on_file_button_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget *dialog = gtk_file_chooser_dialog_new("Choose a file",
			GTK_WINDOW(chat_window),
			GTK_FILE_CHOOSER_ACTION_OPEN,
			"_Cancel", GTK_RESPONSE_CANCEL,
			"_Open", GTK_RESPONSE_ACCEPT,
			NULL);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		send_file(filename);
		g_free(filename);
	}

	gtk_widget_destroy(dialog);
}

void handle_received_file(FileHeader* header, const char* data) {
	char filepath[512];
	snprintf(filepath, sizeof(filepath), "downloads/%s", header->filename);

	FILE* file = fopen(filepath, "wb");
	if (!file) {
		mkdir("downloads", 0755);  // downloads 디렉토리 생성
		file = fopen(filepath, "wb");
		if (!file) {
			show_error_message("Could not create file");
			return;
		}
	}

	fwrite(data, 1, header->size, file);
	fclose(file);

	char msg[512];
	snprintf(msg, sizeof(msg), "Received file from %s: %s", 
			header->sender, header->filename);
	append_message(msg);
}
