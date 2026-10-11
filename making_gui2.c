#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "wang.h"
#include "world.h"
#include "tileset.h"

#define GRID_FILE "grid.png"
#define GUI_MAX_GRID_TL 128 // keeps the rendered image within WANG_MAX_RENDER_TL tiles

static char *opened_tileset = NULL; // the tileset image chosen with "Open Image" (any size or tile order)
static BLTR *last_tiles = NULL; // last generated grid, kept for the 3D export
static size_t last_w = 0, last_h = 0;
static RGBA32 last_atlas[WANG_ATLAS_PX * WANG_ATLAS_PX];
static uint32_t last_seed = 1;
static GtkWidget *theme_combo;     // classic, dungeon, hedge, ...
static GtkWidget *ceiling_check;

static GtkWidget *image_preview; // Widget to display selected image in the main window
//Code for opening a preivew window for grid image
static gdouble scale_factor = 1.0;
// Global pointers for window and image widget
static GtkWidget *global_window = NULL;
static GtkWidget *global_image = NULL;

static GtkWidget *height_entry;
static GtkWidget *width_entry;
static GtkWidget *seed_entry;

static void show_error(GtkWindow *parent, const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void update_image(const char *filename) {
    if (!global_image) return;
    GdkPixbuf *original_pixbuf = gdk_pixbuf_new_from_file(filename, NULL);
    if (!original_pixbuf) return;
    GdkPixbuf *scaled_pixbuf = gdk_pixbuf_scale_simple(original_pixbuf, 
                                                       gdk_pixbuf_get_width(original_pixbuf) * scale_factor,
                                                       gdk_pixbuf_get_height(original_pixbuf) * scale_factor, 
                                                       GDK_INTERP_BILINEAR);
    if (scaled_pixbuf) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(global_image), scaled_pixbuf);
        g_object_unref(scaled_pixbuf);
    }
    g_object_unref(original_pixbuf);
}

static void zoom_in(GtkWidget *widget, gpointer data) {
    scale_factor *= 1.25;  // Increase scale by 25%
    update_image(GRID_FILE);  // Update with the current image path
}

static void zoom_out(GtkWidget *widget, gpointer data) {
    scale_factor /= 1.25;  // Decrease scale by 25%
    update_image(GRID_FILE);  // Update with the current image path
}

// Function to create and set an image preview
void set_image_preview(const char *filename) {
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(filename, 350, 350, TRUE, NULL);
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
    gboolean has_filename = (filename != NULL);
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
    gtk_file_chooser_set_preview_widget_active(file_chooser, has_filename);
}

static void view_reference_atlas(GtkWidget *widget, gpointer data) {
    static GtkWidget *atlas_window = NULL;
    static GtkWidget *atlas_image = NULL;

    if (!atlas_window) {
        atlas_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(atlas_window), "Reference Atlas");
        gtk_window_set_default_size(GTK_WINDOW(atlas_window), 300, 300);
        g_signal_connect(atlas_window, "destroy", G_CALLBACK(gtk_widget_destroyed), &atlas_window);

        GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
        gtk_widget_set_hexpand(scrolled_window, TRUE);
        gtk_widget_set_vexpand(scrolled_window, TRUE);
        gtk_container_add(GTK_CONTAINER(atlas_window), scrolled_window);

        atlas_image = gtk_image_new();  // Initialize the image widget
        gtk_container_add(GTK_CONTAINER(scrolled_window), atlas_image);

        gtk_image_set_from_file(GTK_IMAGE(atlas_image), "reference.png");
    }

    gtk_widget_show_all(atlas_window);
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

        // Check that it reads as a complete tileset before accepting it.
        Tileset ts;
        if (tileset_load(filename, TS_ORDER_AUTO, TS_MATCH_EDGE, &ts) == 0) {
            tileset_free(&ts);
            g_free(opened_tileset);
            opened_tileset = g_strdup(filename);
            set_image_preview(filename);
        } else {
            show_error(GTK_WINDOW(data), wang_last_error());
        }

        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void show_info_dialog(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(data),
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_INFO,
                                               GTK_BUTTONS_OK,
                                               "Project Information");

    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "Wang tiles were first proposed by mathematician, Hao Wang in 1961. A set of square tiles, with each tile edge of a fixed color, are arranged side by side in a rectangular grid. All four edges of each tile must 'match' (have the same color as) their adjoining neighbor. The tiles never overlap and usually all spaces (cells) in the grid are filled. Tiles have a 'fixed orientation', they are never rotated or reflected (turned over). \n\nWith careful tile design, a complete array can produce a large image without visual 'breaks' between tiles. This helps computer game designers create large tiled backgrounds from a small set of tile images. \n\nEdge matching Wang tiles tend to produce path or maze designs. A second type of tile set are corner tiles. These are matched by their corners and tend to produce patch or terrain designs. \nIn this project we're using edge matching to produce path or maze design.");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// The theme selected in the combo box, or NULL for "classic".
static const Theme *selected_theme(void) {
    char *name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(theme_combo));
    const Theme *theme = name ? theme_by_name(name) : NULL;
    g_free(name);
    return theme;
}

// Atlas: the user's opened tileset if there is one (its order is worked out from the pixels), otherwise the
// theme's own, otherwise the blobs. Only edge-matched tilesets can drive the maze here; the command line tool
// can also use corner-matched ones for floors and textures.
static int load_atlas(const Theme *theme) {
    if (opened_tileset) {
        Tileset ts;
        if (tileset_load(opened_tileset, TS_ORDER_AUTO, TS_MATCH_EDGE, &ts) != 0) return -1;
        tileset_to_atlas(&ts, last_atlas);
        tileset_free(&ts);
        return 0;
    }
    if (theme) theme_generate_atlas(theme, last_atlas);
    else wang_atlas_generate(last_atlas, WANG_SHADER_BLOBS);
    return 0;
}

// Reads a positive integer from an entry; falls back to 'def' when empty or invalid.
static size_t entry_size(GtkWidget *entry, size_t def, size_t max) {
    long v = atol(gtk_entry_get_text(GTK_ENTRY(entry)));
    if (v <= 0) return def;
    return (size_t)v > max ? max : (size_t)v;
}

static void submit_clicked(GtkWidget *widget, gpointer data) {
    GtkWindow *parent = GTK_WINDOW(data);
    size_t width = entry_size(width_entry, 10, GUI_MAX_GRID_TL);
    size_t height = entry_size(height_entry, 10, GUI_MAX_GRID_TL);
    uint32_t seed = (uint32_t)entry_size(seed_entry, (size_t)time(NULL), 0xFFFFFFFFu);
    g_print("Generating %zux%zu grid (seed %u)...\n", width, height, seed);

    if (load_atlas(selected_theme()) != 0) {
        show_error(parent, wang_last_error());
        return;
    }

    BLTR *tiles = malloc(width * height);
    if (!tiles) {
        show_error(parent, "Out of memory.");
        return;
    }
    RGBA32 *px = NULL;
    if (wang_generate_grid(width, height, seed, tiles) != 0 ||
        !(px = wang_render_grid(tiles, width, height, last_atlas)) ||
        wang_save_image(GRID_FILE, px, width * WANG_TILE_PX, height * WANG_TILE_PX) != 0) {
        show_error(parent, wang_last_error());
        free(px);
        free(tiles);
        return;
    }
    free(px);

    free(last_tiles);
    last_tiles = tiles;
    last_w = width;
    last_h = height;
    last_seed = seed;

    if (!global_window) {
        global_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(global_window), "Image Viewer");
        gtk_window_set_default_size(GTK_WINDOW(global_window), 400, 400);
        g_signal_connect(global_window, "destroy", G_CALLBACK(gtk_widget_destroyed), &global_window);
        g_signal_connect(global_window, "destroy", G_CALLBACK(gtk_widget_destroyed), &global_image);

        // Main vertical box to hold the image viewer and button controls
        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_container_add(GTK_CONTAINER(global_window), vbox);

        // Scrolled window for the image
        GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
        gtk_widget_set_hexpand(scrolled_window, TRUE);
        gtk_widget_set_vexpand(scrolled_window, TRUE);
        gtk_container_add(GTK_CONTAINER(vbox), scrolled_window);

        global_image = gtk_image_new();
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
    update_image(GRID_FILE);
    gtk_widget_show_all(global_window);
}

// Writes the last grid as a 3D world: obj, mtl, textures and a json file for the viewer.
static void export_3d_clicked(GtkWidget *widget, gpointer data) {
    GtkWindow *parent = GTK_WINDOW(data);
    if (!last_tiles) {
        show_error(parent, "Create a Wang grid first.");
        return;
    }

    GtkWidget *dialog = gtk_file_chooser_dialog_new("Export 3D world", parent,
                                                    GTK_FILE_CHOOSER_ACTION_SAVE,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Save", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "world.obj");

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *obj_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        char *full_obj = g_str_has_suffix(obj_path, ".obj") ? g_strdup(obj_path) : g_strconcat(obj_path, ".obj", NULL);

        // The theme can have been changed since the grid was made, so pick the atlas again for it.
        WorldOptions world = world_default_options();
        world.theme = selected_theme();
        world.ceiling = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ceiling_check));
        world.seed = last_seed;
        if (load_atlas(world.theme) != 0 ||
            wang_export_world(last_tiles, last_w, last_h, last_atlas, full_obj, &world) != 0) {
            show_error(parent, wang_last_error());
        }

        g_free(full_obj);
        g_free(obj_path);
    }
    gtk_widget_destroy(dialog);
}

int main(int argc, char **argv) {
    GtkWidget *window;
    GtkWidget *button;
    GtkWidget *submit_button;
    GtkWidget *view_atlas_button;
    GtkWidget *export_button;
    GtkWidget *vbox;
    GtkWidget *hbox;
    GtkWidget *button_box;  // Box to contain buttons at the bottom
    GtkWidget *info_button;

    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Wang Tile Grid Generator");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    //Image selection
    button = gtk_button_new_with_label("Open Image");
    g_signal_connect(button, "clicked", G_CALLBACK(open_image), window);
    gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);

    // Horizontal box for dimension and seed entries
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    width_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(width_entry), "Width (default 10)");
    gtk_box_pack_start(GTK_BOX(hbox), width_entry, TRUE, TRUE, 5);

    height_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(height_entry), "Height (default 10)");
    gtk_box_pack_start(GTK_BOX(hbox), height_entry, TRUE, TRUE, 5);

    seed_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(seed_entry), "Seed (random)");
    gtk_box_pack_start(GTK_BOX(hbox), seed_entry, TRUE, TRUE, 5);

    // Theme for the 3D export (and for the grid, when no atlas image was opened)
    GtkWidget *theme_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), theme_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(theme_row), gtk_label_new("Theme:"), FALSE, FALSE, 5);
    theme_combo = gtk_combo_box_text_new();
    const char *theme_names[] = {"classic", "dungeon", "hedge", "scifi", "desert", "ice"};
    for (size_t i = 0; i < G_N_ELEMENTS(theme_names); ++i)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_combo), theme_names[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(theme_combo), 0);
    gtk_box_pack_start(GTK_BOX(theme_row), theme_combo, TRUE, TRUE, 5);
    ceiling_check = gtk_check_button_new_with_label("Ceiling");
    gtk_box_pack_start(GTK_BOX(theme_row), ceiling_check, FALSE, FALSE, 5);

    image_preview = gtk_image_new(); // Shows the atlas that was opened
    gtk_box_pack_start(GTK_BOX(vbox), image_preview, TRUE, TRUE, 0);

    // Create a horizontal box for buttons at the bottom
    button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_set_homogeneous(GTK_BOX(button_box), TRUE);

    submit_button = gtk_button_new_with_label("Create Wang Grid");
    g_signal_connect(submit_button, "clicked", G_CALLBACK(submit_clicked), window);
    gtk_box_pack_start(GTK_BOX(button_box), submit_button, FALSE, FALSE, 0);

    // Button to view reference atlas
    view_atlas_button = gtk_button_new_with_label("View Reference Atlas");
    g_signal_connect(view_atlas_button, "clicked", G_CALLBACK(view_reference_atlas), NULL);
    gtk_box_pack_start(GTK_BOX(button_box), view_atlas_button, FALSE, FALSE, 0);

    export_button = gtk_button_new_with_label("Export 3D (.obj)");
    g_signal_connect(export_button, "clicked", G_CALLBACK(export_3d_clicked), window);
    gtk_box_pack_start(GTK_BOX(button_box), export_button, FALSE, FALSE, 0);

    // Info button setup
    info_button = gtk_button_new_with_label("Info");
    g_signal_connect(info_button, "clicked", G_CALLBACK(show_info_dialog), window);
    gtk_box_pack_start(GTK_BOX(button_box), info_button, TRUE, TRUE, 0);

    // The button row goes at the bottom of the main window
    gtk_box_pack_end(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    gtk_widget_show_all(window);
    gtk_main();

    free(last_tiles);
    return 0;
}
