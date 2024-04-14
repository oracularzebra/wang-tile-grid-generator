#include <gtk/gtk.h>

void take_image_input(GtkWidget *widget, gpointer data)
{
    g_print("Button clicked");
}
static void activate(GtkApplication* app, gpointer user_data)
{
    
    GtkWidget *window;
    GtkWidget *grid;
    GtkWidget *button;

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Wang tile Generator");

    grid = gtk_grid_new();

    gtk_window_set_child(GTK_WINDOW(window), grid);

    button = gtk_button_new_with_label("SUBMIT ATLAS");
    g_signal_connect(button, "clicked", G_CALLBACK(take_image_input), NULL);
    gtk_grid_attach(GTK_GRID(grid), button, 0, 0, 4, 1);
    

    gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);
    gtk_window_present(GTK_WINDOW(window));

}

int main(int argc, char **argv)
{
    GtkApplication *app;
    int status;

    app = gtk_application_new("com.github.oracularzebra.wang_tile_generator", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}