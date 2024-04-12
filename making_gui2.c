#include <gtk/gtk.h>
#include "main2.c"

static GtkWidget *image_preview; // Widget to display selected image in the main window
//Code for opening a preivew window for grid image
static gdouble scale_factor = 1.0;
// Global pointers for window and image widget
static GtkWidget *global_window = NULL;
static GtkWidget *global_image = NULL;

static void update_image(const char *filename) {
    GdkPixbuf *original_pixbuf = gdk_pixbuf_new_from_file(filename, NULL);
    GdkPixbuf *scaled_pixbuf = gdk_pixbuf_scale_simple(original_pixbuf, 
                                                       gdk_pixbuf_get_width(original_pixbuf) * scale_factor,
                                                       gdk_pixbuf_get_height(original_pixbuf) * scale_factor, 
                                                       GDK_INTERP_BILINEAR);
    gtk_image_set_from_pixbuf(GTK_IMAGE(global_image), scaled_pixbuf);
    g_object_unref(scaled_pixbuf);
    g_object_unref(original_pixbuf);
}

static void zoom_in(GtkWidget *widget, gpointer data) {
    scale_factor *= 1.25;  // Increase scale by 25%
    update_image("grid.png");  // Update with the current image path
}

static void zoom_out(GtkWidget *widget, gpointer data) {
    scale_factor /= 1.25;  // Decrease scale by 25%
    update_image("grid.png");  // Update with the current image path
}

// Function to create and set an image preview
void set_image_preview(const char *filename) {
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(filename, 200, 200, TRUE, NULL);
    if (pixbuf) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(image_preview), pixbuf);
        g_object_unref(pixbuf);
    } else {
        gtk_image_set_from_icon_name(GTK_IMAGE(image_preview), "image-missing", GTK_ICON_SIZE_DIALOG);
    }
}

// Callback for update-preview signal
static void update_preview_cb(GtkFileChooser *file_chooser, gpointer data) {
    char *filename = gtk_file_chooser_get_preview_filename(file_chooser);
    if (filename) {
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(filename, 200, 200, TRUE, NULL);
        gboolean has_preview = (pixbuf != NULL);
        if (has_preview) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(data), pixbuf);
            g_object_unref(pixbuf);
        } else {
            gtk_image_set_from_icon_name(GTK_IMAGE(data), "image-missing", GTK_ICON_SIZE_DIALOG);
        }
        g_free(filename);
    }
    gtk_file_chooser_set_preview_widget_active(file_chooser, filename != NULL);
}

// Function to compress and save an image to 256x256 if larger
// Function to resize an image to 256x256 and save it
gboolean resize_image_to_256(const char *input_filename, const char *output_filename) {
    GdkPixbuf *src_pixbuf, *dest_pixbuf;
    GError *error = NULL;

    // Load the original image
    src_pixbuf = gdk_pixbuf_new_from_file(input_filename, &error);
    if (!src_pixbuf) {
        g_print("Error loading image: %s\n", error->message);
        g_error_free(error);
        return FALSE;
    }

    // Always resize the image to 256x256
    dest_pixbuf = gdk_pixbuf_scale_simple(src_pixbuf, 256, 256, GDK_INTERP_BILINEAR);
    if (!dest_pixbuf) {
        g_object_unref(src_pixbuf);
        return FALSE;
    }

    // Save the resized image
    gboolean result = gdk_pixbuf_save(dest_pixbuf, output_filename, "jpeg", &error, NULL);
    if (!result) {
        g_print("Error saving image: %s\n", error->message);
        g_error_free(error);
    }

    // Clean up
    g_object_unref(src_pixbuf);
    g_object_unref(dest_pixbuf);

    return result;
}

static void open_image(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog;
    GtkWidget *preview;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
    gint res;

    dialog = gtk_file_chooser_dialog_new("Open File",
                                         GTK_WINDOW(data),
                                         action,
                                         "_Cancel",
                                         GTK_RESPONSE_CANCEL,
                                         "_Open",
                                         GTK_RESPONSE_ACCEPT,
                                         NULL);

    preview = gtk_image_new();
    gtk_file_chooser_set_preview_widget(GTK_FILE_CHOOSER(dialog), preview);
    gtk_file_chooser_set_preview_widget_active(GTK_FILE_CHOOSER(dialog), TRUE);
    g_signal_connect(dialog, "update-preview", G_CALLBACK(update_preview_cb), preview);

    res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        char *output_filename = "atlas.png"; // Define the output filename

        // Compress and save the image
        if (resize_image_to_256(filename, output_filename)) {
            set_image_preview(output_filename); // Update the preview to show the compressed image
        }

        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

static void submit_clicked(GtkWidget *widget, gpointer data) {
    g_print("Submit button clicked.\n Generating grid...");
    
    int grid_generated = generateGrid(20,20);
    if(grid_generated == 1){
        g_print("Generated grid %d\n", grid_generated);
        if (!global_window) {
            global_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
            gtk_window_set_title(GTK_WINDOW(global_window), "Image Viewer");
            gtk_window_set_default_size(GTK_WINDOW(global_window), 400, 400);
            g_signal_connect(global_window, "destroy", G_CALLBACK(gtk_widget_destroyed), &global_window);

             // Main vertical box to hold the image viewer and button controls
            GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
            gtk_container_add(GTK_CONTAINER(global_window), vbox);

            // Scrolled window for the image
            GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
            gtk_widget_set_hexpand(scrolled_window, TRUE);
            gtk_widget_set_vexpand(scrolled_window, TRUE);
            gtk_container_add(GTK_CONTAINER(vbox), scrolled_window);

            global_image = gtk_image_new();  // Initialize the image widget
            gtk_container_add(GTK_CONTAINER(scrolled_window), global_image);


            // Horizontal box for the buttons
            GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
            gtk_container_add(GTK_CONTAINER(vbox), button_box);

            GtkWidget *zoom_in_button = gtk_button_new_with_label("Zoom In");
            GtkWidget *zoom_out_button = gtk_button_new_with_label("Zoom Out");
            gtk_box_pack_start(GTK_BOX(button_box), zoom_in_button, TRUE, TRUE, 0);
            gtk_box_pack_start(GTK_BOX(button_box), zoom_out_button, TRUE, TRUE, 0);

            g_signal_connect(zoom_in_button, "clicked", G_CALLBACK(zoom_in), global_image);
            g_signal_connect(zoom_out_button, "clicked", G_CALLBACK(zoom_out), global_image);
        } 
        update_image("grid.png");  // Load or update the image
        gtk_widget_show_all(global_window);
    }
}

int main(int argc, char **argv) {
    GtkWidget *window;
    GtkWidget *button;
    GtkWidget *submit_button;
    GtkWidget *vbox;

    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Wang Tile Grid Generator");
    gtk_window_set_default_size(GTK_WINDOW(window), 300, 400);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    button = gtk_button_new_with_label("Open Image");
    g_signal_connect(button, "clicked", G_CALLBACK(open_image), window);
    gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);

    image_preview = gtk_image_new(); // Initialize with no image
    gtk_box_pack_start(GTK_BOX(vbox), image_preview, TRUE, TRUE, 0);

    submit_button = gtk_button_new_with_label("Create Wang Grid");
    g_signal_connect(submit_button, "clicked", G_CALLBACK(submit_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), submit_button, FALSE, FALSE, 0);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
