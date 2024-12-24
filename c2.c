#include <gtk/gtk.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>

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
	size_t size;
} Message;

// GUI components
typedef struct {
	GtkWidget *login_window;
	GtkWidget *chat_window;
	GtkWidget *username_entry;
	GtkWidget *server_entry;
	GtkWidget *port_entry;
	GtkWidget *chat_view;
	GtkWidget *message_entry;
	GtkWidget *room_list;
	GtkWidget *user_list;
	GtkTextBuffer *chat_buffer;
	GtkWidget *status_bar;
	GtkWidget *file_progress;
} ChatGUI;

// Global variables
ChatGUI gui;
int sock_fd = -1;
char username[MAX_USERNAME];
pthread_t receive_thread;
int current_room = 0;

// Function declarations
void* receive_message(void* arg);
void on_send_clicked(GtkButton *button, gpointer user_data);
void on_connect_clicked(GtkButton *button, gpointer user_data);
void on_file_clicked(GtkButton *button, gpointer user_data);
gboolean append_message(const gchar *message);
void show_error(const char* message);
void create_login_window(void);
void create_chat_window(void);
void handle_received_file(Message *msg);
void send_file(const char *filepath);
void update_list_box(GtkListBox *list_box, const char *content);

int main(int argc, char *argv[]) {
	gtk_init(&argc, &argv);

	// Create windows
	create_login_window();
	create_chat_window();

	// Show login window
	gtk_widget_show_all(gui.login_window);
	gtk_widget_hide(gui.chat_window);

	gtk_main();
	return 0;
}

void create_login_window(void) {
	// Create login window
	gui.login_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(gui.login_window), "Chat Login");
	gtk_window_set_default_size(GTK_WINDOW(gui.login_window), 300, 200);
	gtk_container_set_border_width(GTK_CONTAINER(gui.login_window), 10);
	g_signal_connect(gui.login_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	// Create main box
	GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	gtk_container_add(GTK_CONTAINER(gui.login_window), main_box);

	// Create input fields
	gui.username_entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(gui.username_entry), "Username");
	gtk_box_pack_start(GTK_BOX(main_box), gui.username_entry, FALSE, FALSE, 5);

	gui.server_entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(gui.server_entry), "127.0.0.1");
	gtk_entry_set_placeholder_text(GTK_ENTRY(gui.server_entry), "Server IP");
	gtk_box_pack_start(GTK_BOX(main_box), gui.server_entry, FALSE, FALSE, 5);

	gui.port_entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(gui.port_entry), "8888");
	gtk_entry_set_placeholder_text(GTK_ENTRY(gui.port_entry), "Port");
	gtk_box_pack_start(GTK_BOX(main_box), gui.port_entry, FALSE, FALSE, 5);

	// Create connect button
	GtkWidget *connect_button = gtk_button_new_with_label("Connect");
	g_signal_connect(connect_button, "clicked", G_CALLBACK(on_connect_clicked), NULL);
	gtk_box_pack_start(GTK_BOX(main_box), connect_button, FALSE, FALSE, 5);
}

void create_chat_window(void) {
	// Create chat window
	gui.chat_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(gui.chat_window), "Chat");
	gtk_window_set_default_size(GTK_WINDOW(gui.chat_window), 800, 600);
	g_signal_connect(gui.chat_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	// Create main horizontal box
	GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_container_add(GTK_CONTAINER(gui.chat_window), main_box);

	// Create left panel for rooms and users
	GtkWidget *left_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	gtk_box_pack_start(GTK_BOX(main_box), left_panel, FALSE, FALSE, 5);

	// Room list
	GtkWidget *room_label = gtk_label_new("Rooms");
	gtk_box_pack_start(GTK_BOX(left_panel), room_label, FALSE, FALSE, 5);

	GtkWidget *room_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_size_request(room_scroll, 150, 200);
	gtk_box_pack_start(GTK_BOX(left_panel), room_scroll, TRUE, TRUE, 0);

	gui.room_list = gtk_list_box_new();
	gtk_container_add(GTK_CONTAINER(room_scroll), gui.room_list);

	// User list
	GtkWidget *user_label = gtk_label_new("Users");
	gtk_box_pack_start(GTK_BOX(left_panel), user_label, FALSE, FALSE, 5);

	GtkWidget *user_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_size_request(user_scroll, 150, 200);
	gtk_box_pack_start(GTK_BOX(left_panel), user_scroll, TRUE, TRUE, 0);

	gui.user_list = gtk_list_box_new();
	gtk_container_add(GTK_CONTAINER(user_scroll), gui.user_list);

	// Create right panel for chat
	GtkWidget *right_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	gtk_box_pack_start(GTK_BOX(main_box), right_panel, TRUE, TRUE, 5);

	// Chat view
	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_box_pack_start(GTK_BOX(right_panel), scroll, TRUE, TRUE, 0);

	gui.chat_view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(gui.chat_view), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(gui.chat_view), GTK_WRAP_WORD_CHAR);
	gtk_container_add(GTK_CONTAINER(scroll), gui.chat_view);
	gui.chat_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gui.chat_view));

	// Input area
	GtkWidget *input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_box_pack_start(GTK_BOX(right_panel), input_box, FALSE, FALSE, 5);

	gui.message_entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(input_box), gui.message_entry, TRUE, TRUE, 0);

	GtkWidget *send_button = gtk_button_new_with_label("Send");
	g_signal_connect(send_button, "clicked", G_CALLBACK(on_send_clicked), NULL);
	gtk_box_pack_start(GTK_BOX(input_box), send_button, FALSE, FALSE, 0);

	GtkWidget *file_button = gtk_button_new_with_label("Send File");
	g_signal_connect(file_button, "clicked", G_CALLBACK(on_file_clicked), NULL);
	gtk_box_pack_start(GTK_BOX(input_box), file_button, FALSE, FALSE, 0);

	// Status bar and progress bar
	gui.status_bar = gtk_statusbar_new();
	gtk_box_pack_start(GTK_BOX(right_panel), gui.status_bar, FALSE, FALSE, 0);

	gui.file_progress = gtk_progress_bar_new();
	gtk_box_pack_start(GTK_BOX(right_panel), gui.file_progress, FALSE, FALSE, 0);
}

void on_connect_clicked(GtkButton *button, gpointer user_data) {
	const char *server_ip = gtk_entry_get_text(GTK_ENTRY(gui.server_entry));
	const char *port_str = gtk_entry_get_text(GTK_ENTRY(gui.port_entry));
	const char *username_str = gtk_entry_get_text(GTK_ENTRY(gui.username_entry));

	if (strlen(username_str) == 0) {
		show_error("Please enter a username");
		return;
	}

	// Create socket
	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (sock_fd < 0) {
		show_error("Failed to create socket");
		return;
	}

	// Connect to server
	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(server_ip);
	server_addr.sin_port = htons(atoi(port_str));

	if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		show_error("Connection failed");
		close(sock_fd);
		return;
	}

	// Save username and send to server
	strncpy(username, username_str, MAX_USERNAME - 1);
	send(sock_fd, username, strlen(username), 0);

	// Start receive thread
	pthread_create(&receive_thread, NULL, receive_message, NULL);

	// Switch to chat window
	gtk_widget_hide(gui.login_window);
	gtk_widget_show_all(gui.chat_window);
}

void on_send_clicked(GtkButton *button, gpointer user_data) {
	const char *text = gtk_entry_get_text(GTK_ENTRY(gui.message_entry));
	if (strlen(text) == 0) return;

	Message msg = {
		.type = MSG_CHAT,
		.room_id = current_room
	};
	strncpy(msg.sender, username, MAX_USERNAME - 1);
	strncpy(msg.content, text, BUFFER_SIZE - 1);

	send(sock_fd, &msg, sizeof(Message), 0);
	gtk_entry_set_text(GTK_ENTRY(gui.message_entry), "");
}

void on_file_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget *dialog = gtk_file_chooser_dialog_new("Choose a file",
			GTK_WINDOW(gui.chat_window),
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

void send_file(const char *filepath) {
	FILE *file = fopen(filepath, "rb");
	if (!file) {
		show_error("Could not open file");
		return;
	}

	// Get file size
	fseek(file, 0, SEEK_END);
	size_t file_size = ftell(file);
	fseek(file, 0, SEEK_SET);

	// Get filename from path
	const char *filename = strrchr(filepath, '/');
	filename = filename ? filename + 1 : filepath;

	// Send file start message
	Message msg = {
		.type = MSG_FILE_START,
		.room_id = current_room,
		.size = file_size
	};
	strncpy(msg.sender, username, MAX_USERNAME - 1);
	strncpy(msg.content, filename, BUFFER_SIZE - 1);

	send(sock_fd, &msg, sizeof(Message), 0);

	// Send file data
	char buffer[8192];
	size_t bytes_read;
	size_t total_sent = 0;

	while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		send(sock_fd, buffer, bytes_read, 0);
		total_sent += bytes_read;

		// Update progress bar
		double fraction = (double)total_sent / file_size;
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gui.file_progress), fraction);

		while (gtk_events_pending()) gtk_main_iteration();
	}

	// Send file end message
	msg.type = MSG_FILE_END;
	send(sock_fd, &msg, sizeof(Message), 0);

	fclose(file);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gui.file_progress), 0.0);
}

// 기존 receive_message 함수의 room/user list 처리 부분을 수정
void update_list_box(GtkListBox *list_box, const char *content) {
	// Remove all existing items
	GList *children, *iter;
	children = gtk_container_get_children(GTK_CONTAINER(list_box));
	for(iter = children; iter != NULL; iter = g_list_next(iter))
		gtk_widget_destroy(GTK_WIDGET(iter->data));
	g_list_free(children);

	// Add new items
	char *items = strdup(content);
	char *item = strtok(items, ",");
	while (item != NULL) {
		GtkWidget *label = gtk_label_new(item);
		gtk_widget_show(label);
		gtk_list_box_insert(list_box, label, -1);
		item = strtok(NULL, ",");
	}
	free(items);
}

void* receive_message(void* arg) {
	Message msg;

	while (1) {
		ssize_t bytes_received = recv(sock_fd, &msg, sizeof(Message), 0);
		if (bytes_received <= 0) break;

		switch (msg.type) {
			case MSG_CHAT: {
							   gchar *display_text = g_strdup_printf("%s: %s\n", msg.sender, msg.content);
							   gdk_threads_add_idle((GSourceFunc)append_message, display_text);
							   break;
						   }
			case MSG_FILE_START:
						   handle_received_file(&msg);
						   break;
			case MSG_LIST_ROOMS:
						   gdk_threads_add_idle((GSourceFunc)update_list_box, g_object_new(G_TYPE_OBJECT,
									   "list-box", GTK_LIST_BOX(gui.room_list),  // GTK_LIST_BOX 캐스팅 추가
									   "content", g_strdup(msg.content),          // content를 복사
									   NULL));
						   break;
			case MSG_LIST_USERS:
						   gdk_threads_add_idle((GSourceFunc)update_list_box, g_object_new(G_TYPE_OBJECT,
									   "list-box", GTK_LIST_BOX(gui.user_list),  // GTK_LIST_BOX 캐스팅 추가
									   "content", g_strdup(msg.content),          // content를 복사
									   NULL));
						   break;
			default:
						   break;
		}
	}

	// Connection lost
	gdk_threads_add_idle((GSourceFunc)append_message, 
			g_strdup("Disconnected from server\n"));
	close(sock_fd);
	return NULL;
}

void handle_received_file(Message *msg) {
	char filepath[PATH_MAX];  // Use PATH_MAX from limits.h

	// Create downloads directory if it doesn't exist
	struct stat st = {0};
	if (stat("downloads", &st) == -1) {
#ifdef _WIN32
		_mkdir("downloads");
#else
		mkdir("downloads", 0777);
#endif
	}

	// Safely create filepath
	if (snprintf(filepath, sizeof(filepath), "downloads/%s", msg->content) >= sizeof(filepath)) {
		show_error("Filename too long");
		return;
	}

	FILE *file = fopen(filepath, "wb");
	if (!file) {
		show_error("Could not create file");
		return;
	}

	// Update status bar
	gchar *status = g_strdup_printf("Receiving file: %s", msg->content);
	gtk_statusbar_push(GTK_STATUSBAR(gui.status_bar), 0, status);
	g_free(status);

	// Receive file data
	size_t remaining = msg->size;
	char buffer[8192];
	size_t total_received = 0;

	while (remaining > 0) {
		size_t to_read = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
		ssize_t bytes_received = recv(sock_fd, buffer, to_read, 0);

		if (bytes_received <= 0) {
			show_error("File transfer failed");
			break;
		}

		fwrite(buffer, 1, bytes_received, file);
		remaining -= bytes_received;
		total_received += bytes_received;

		// Update progress bar
		double fraction = (double)total_received / msg->size;
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gui.file_progress), fraction);

		while (gtk_events_pending()) gtk_main_iteration();
	}

	fclose(file);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gui.file_progress), 0.0);
	gtk_statusbar_pop(GTK_STATUSBAR(gui.status_bar), 0);
}

gboolean append_message(const gchar *message) {
	GtkTextIter end;
	gtk_text_buffer_get_end_iter(gui.chat_buffer, &end);
	gtk_text_buffer_insert(gui.chat_buffer, &end, message, -1);
	g_free((gchar*)message);
	return G_SOURCE_REMOVE;
}

void show_error(const char* message) {
	GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(gui.chat_window),
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_ERROR,
			GTK_BUTTONS_CLOSE,
			"%s", message);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}
