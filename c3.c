#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFFER_SIZE 1024

GtkWidget *login_window;
GtkWidget *chat_window;
GtkWidget *username_entry;
GtkWidget *server_entry;
GtkWidget *port_entry;
GtkWidget *chat_view;
GtkWidget *message_entry;

int server_sock;
char username[32];

void display_message(const char *message) {
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat_view));
	GtkTextIter end;
	gtk_text_buffer_get_end_iter(buffer, &end);
	gtk_text_buffer_insert(buffer, &end, message, -1);
}

void *receive_messages(void *arg) {
	char buffer[BUFFER_SIZE];
	while (1) {
		int bytes_received = recv(server_sock, buffer, sizeof(buffer) - 1, 0);
		if (bytes_received <= 0) {
			display_message("Disconnected from server.\n");
			break;
		}
		buffer[bytes_received] = '\0';
		display_message(buffer);
	}
	close(server_sock);
	return NULL;
}

void on_login_button_clicked(GtkButton *button, gpointer data) {
	const char *server_ip = gtk_entry_get_text(GTK_ENTRY(server_entry));
	const char *port_str = gtk_entry_get_text(GTK_ENTRY(port_entry));
	strncpy(username, gtk_entry_get_text(GTK_ENTRY(username_entry)), sizeof(username));

	server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock == -1) {
		display_message("Failed to create socket.\n");
		return;
	}

	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(atoi(port_str));
	inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

	if (connect(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
		display_message("Failed to connect to server.\n");
		close(server_sock);
		return;
	}

	send(server_sock, username, strlen(username), 0);

	pthread_t tid;
	pthread_create(&tid, NULL, receive_messages, NULL);

	gtk_widget_hide(login_window);
	gtk_widget_show_all(chat_window);
}

void on_send_button_clicked(GtkButton *button, gpointer data) {
	const char *message = gtk_entry_get_text(GTK_ENTRY(message_entry));
	if (strlen(message) == 0) return;

	char buffer[BUFFER_SIZE];
	snprintf(buffer, sizeof(buffer), "%s: %s\n", username, message);
	send(server_sock, buffer, strlen(buffer), 0);
	display_message(buffer);
	gtk_entry_set_text(GTK_ENTRY(message_entry), "");
}

void on_file_button_clicked(GtkButton *button, gpointer data) {
	GtkWidget *dialog = gtk_file_chooser_dialog_new("Select File", GTK_WINDOW(chat_window),
			GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel", GTK_RESPONSE_CANCEL, "Send", GTK_RESPONSE_ACCEPT, NULL);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		FILE *file = fopen(filename, "rb");
		if (file) {
			char buffer[BUFFER_SIZE];
			while (1) {
				size_t bytes_read = fread(buffer, 1, sizeof(buffer), file);
				if (bytes_read == 0) break;
				send(server_sock, buffer, bytes_read, 0);
			}
			fclose(file);
		}
		g_free(filename);
	}
	gtk_widget_destroy(dialog);
}

int main(int argc, char *argv[]) {
	GtkBuilder *builder;
	GtkWidget *send_button, *file_button;

	gtk_init(&argc, &argv);

	builder = gtk_builder_new_from_file("chat_ui.xml");

	login_window = GTK_WIDGET(gtk_builder_get_object(builder, "login_window"));
	chat_window = GTK_WIDGET(gtk_builder_get_object(builder, "chat_window"));
	username_entry = GTK_WIDGET(gtk_builder_get_object(builder, "username_entry"));
	server_entry = GTK_WIDGET(gtk_builder_get_object(builder, "server_entry"));
	port_entry = GTK_WIDGET(gtk_builder_get_object(builder, "port_entry"));
	chat_view = GTK_WIDGET(gtk_builder_get_object(builder, "chat_view"));
	message_entry = GTK_WIDGET(gtk_builder_get_object(builder, "message_entry"));

	send_button = GTK_WIDGET(gtk_builder_get_object(builder, "send_button"));
	file_button = GTK_WIDGET(gtk_builder_get_object(builder, "file_button"));

	g_signal_connect(gtk_builder_get_object(builder, "login_button"), "clicked", G_CALLBACK(on_login_button_clicked), NULL);
	g_signal_connect(send_button, "clicked", G_CALLBACK(on_send_button_clicked), NULL);
	g_signal_connect(file_button, "clicked", G_CALLBACK(on_file_button_clicked), NULL);

	g_signal_connect(login_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	g_signal_connect(chat_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	gtk_widget_show_all(login_window);
	gtk_main();

	return 0;
}
