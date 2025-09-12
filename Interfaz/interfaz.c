#include <gtk/gtk.h>
#include <libgen.h> // para basename()

// Función para el botón EXIT
void on_exit_clicked(GtkWidget *widget, gpointer data) {
    gtk_main_quit();
}

// Función para el botón Enviar
void on_enviar_clicked(GtkWidget *widget, gpointer data) {
    g_print("Botón Enviar presionado\n");
}

// Función para el botón Conectar
void on_conectar_clicked(GtkWidget *widget, gpointer data) {
    g_print("Botón Conectar presionado\n");
}

// Función para buscar imagen (imprime nombre y dirección)
void on_buscar_imagen_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
    gint res;

    dialog = gtk_file_chooser_dialog_new("Selecciona una imagen",
                                         GTK_WINDOW(data),
                                         action,
                                         "_Cancelar", GTK_RESPONSE_CANCEL,
                                         "_Abrir", GTK_RESPONSE_ACCEPT,
                                         NULL);

    res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        char *filepath;
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        filepath = gtk_file_chooser_get_filename(chooser);

        // Obtener solo el nombre del archivo
        char *filename = basename(filepath);

        g_print("Imagen cargada: %s\n", filename);
        g_print("Dirección completa: %s\n", filepath);

        g_free(filepath);
    }

    gtk_widget_destroy(dialog);
}

int main(int argc, char *argv[]) {
    GtkWidget *window;
    GtkWidget *grid;
    GtkWidget *btn_exit, *btn_enviar, *btn_conectar, *btn_buscar;

    gtk_init(&argc, &argv);

    // Crear ventana principal
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Interfaz Cliente");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 200);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Crear grid para organizar botones
    grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(window), grid);

    // Crear botones
    btn_exit = gtk_button_new_with_label("EXIT");
    btn_enviar = gtk_button_new_with_label("Enviar");
    btn_conectar = gtk_button_new_with_label("Conectar");
    btn_buscar = gtk_button_new_with_label("Buscar Imagen");

    g_signal_connect(btn_exit, "clicked", G_CALLBACK(on_exit_clicked), NULL);
    g_signal_connect(btn_enviar, "clicked", G_CALLBACK(on_enviar_clicked), NULL);
    g_signal_connect(btn_conectar, "clicked", G_CALLBACK(on_conectar_clicked), NULL);
    g_signal_connect(btn_buscar, "clicked", G_CALLBACK(on_buscar_imagen_clicked), window);

    // Añadir botones al grid
    gtk_grid_attach(GTK_GRID(grid), btn_exit, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), btn_enviar, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), btn_conectar, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), btn_buscar, 3, 0, 1, 1);

    gtk_widget_show_all(window);

    gtk_main();
    return 0;
}
