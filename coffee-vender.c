/*
 * Use this for deb creation, because the paths used for the database are system folders.
 * gcc -o deb/coffee_shop/usr/bin/coffee_shop coffee_shop.c `pkg-config --cflags --libs gtk+-3.0` -lsqlite3
 * strip deb/coffee_shop/usr/bin/coffee_shop
 * dpkg-deb --build coffee_shop
 * sudo dpkg -i coffee_shop.deb
 * This files creates a sql db file in ~/.local/share/coffee_shop/. make sur to delete it manually
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h> // For getuid and getpwuid
#include <pwd.h>    // For getpwuid

// Structures
typedef struct
{
    char name[50];
    double price;
} Beverage;

typedef struct
{
    Beverage *beverage;
    int quantity;
    time_t start_time;
    time_t end_time;
} Order;

typedef struct
{
    GtkWidget *window;
    GtkWidget *menu_box;
    GtkWidget *queue_box;
    GtkWidget *start_button;
    GtkWidget *pause_button;
    GtkWidget *quantity_entry;
    Order *queue;
    int queue_size;
    int queue_capacity;
    gboolean is_processing;
    gboolean is_paused;
    sqlite3 *db;
    GtkWidget *quantity_combo;
    GtkWidget *history_window;
    GtkWidget *history_text_view;
} AppData;

// Function prototypes
void setup_database(AppData *app);
void create_ui(AppData *app);
void add_to_queue(AppData *app, Beverage *beverage, int quantity);
void update_queue_display(AppData *app);
void *process_queue(void *arg);
void on_beverage_clicked(GtkWidget *widget, gpointer data);
void on_start_clicked(GtkWidget *widget, gpointer data);
void on_pause_clicked(GtkWidget *widget, gpointer data);
void show_history(GtkWidget *widget, gpointer data);

// Main function
int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    AppData app = {0};
    app.queue_capacity = 10;
    app.queue = malloc(sizeof(Order) * app.queue_capacity);

    setup_database(&app);
    create_ui(&app);

    gtk_main();

    sqlite3_close(app.db);
    free(app.queue);
    return 0;
}

// Function to get the path to the user's local share directory
const char *get_user_data_directory()
{
    struct passwd *pw = getpwuid(getuid());
    const char *home_dir = pw->pw_dir;
    static char data_dir[256];
    snprintf(data_dir, sizeof(data_dir), "%s/.local/share/coffee_shop/", home_dir);
    return data_dir;
}

// Database setup
void setup_database(AppData *app)
{
    const char *db_dir = get_user_data_directory();

    // Create the directory if it doesn't exist
    mkdir(db_dir, 0755); // Create directory with appropriate permissions

    // Open the database file in the user-specific directory
    char full_db_path[256];
    snprintf(full_db_path, sizeof(full_db_path), "%s/coffee_shop.db", db_dir);

    int rc = sqlite3_open(full_db_path, &app->db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(app->db));
        sqlite3_close(app->db);
        exit(1);
    }

    const char *sql = "CREATE TABLE IF NOT EXISTS orders ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "beverage TEXT,"
                      "quantity INTEGER,"
                      "start_time INTEGER,"
                      "end_time INTEGER,"
                      "date TEXT);";

    char *err_msg = 0;
    rc = sqlite3_exec(app->db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    }
}

// UI creation
void create_ui(AppData *app)
{
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), "Coffee Shop");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 854, 653);
    gtk_window_set_resizable(GTK_WINDOW(app->window), FALSE);
    g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(app->window), grid);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);

    GtkWidget *menu_frame = gtk_frame_new("Menu");
    gtk_grid_attach(GTK_GRID(grid), menu_frame, 0, 0, 1, 1);

    GtkWidget *queue_frame = gtk_frame_new("Queue");
    gtk_grid_attach(GTK_GRID(grid), queue_frame, 1, 0, 1, 1);

    app->menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(menu_frame), app->menu_box);
    gtk_container_set_border_width(GTK_CONTAINER(app->menu_box), 10);

    app->queue_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(queue_frame), app->queue_box);
    gtk_container_set_border_width(GTK_CONTAINER(app->queue_box), 10);

    // Add beverages to menu
    Beverage beverages[] = {
        {"Coffee", 2.50},
        {"Tea", 1.75},
        {"Cappuccino", 3.00},
        {"Espresso", 2.75},
        {"Latte", 3.25}};

    for (int i = 0; i < 5; i++)
    {
        char label[100];
        snprintf(label, sizeof(label), "%s - $%.2f", beverages[i].name, beverages[i].price);
        GtkWidget *button = gtk_button_new_with_label(label);
        g_signal_connect(button, "clicked", G_CALLBACK(on_beverage_clicked), app);
        gtk_box_pack_start(GTK_BOX(app->menu_box), button, TRUE, TRUE, 0);
    }

    // Add quantity selector
    GtkWidget *quantity_label = gtk_label_new("Quantity:");
    gtk_grid_attach(GTK_GRID(grid), quantity_label, 0, 1, 1, 1);

    app->quantity_combo = gtk_combo_box_text_new();
    for (int i = 1; i <= 10; i++)
    {
        char num[3];
        snprintf(num, sizeof(num), "%d", i);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->quantity_combo), num);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->quantity_combo), 0);
    gtk_grid_attach(GTK_GRID(grid), app->quantity_combo, 1, 1, 1, 1);

    // Add control buttons
    GtkWidget *control_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_grid_attach(GTK_GRID(grid), control_box, 0, 2, 2, 1);

    app->start_button = gtk_button_new_with_label("Start");
    g_signal_connect(app->start_button, "clicked", G_CALLBACK(on_start_clicked), app);
    gtk_box_pack_start(GTK_BOX(control_box), app->start_button, TRUE, TRUE, 0);

    app->pause_button = gtk_button_new_with_label("Pause");
    g_signal_connect(app->pause_button, "clicked", G_CALLBACK(on_pause_clicked), app);
    gtk_box_pack_start(GTK_BOX(control_box), app->pause_button, TRUE, TRUE, 0);

    GtkWidget *history_button = gtk_button_new_with_label("Show History");
    g_signal_connect(history_button, "clicked", G_CALLBACK(show_history), app);
    gtk_box_pack_start(GTK_BOX(control_box), history_button, TRUE, TRUE, 0);

    gtk_widget_show_all(app->window);
}
// Queue management
void add_to_queue(AppData *app, Beverage *beverage, int quantity)
{
    if (app->queue_size >= app->queue_capacity)
    {
        app->queue_capacity *= 2;
        app->queue = realloc(app->queue, sizeof(Order) * app->queue_capacity);
    }

    Order new_order = {
        .beverage = beverage,
        .quantity = quantity,
        .start_time = 0,
        .end_time = 0};

    app->queue[app->queue_size++] = new_order;
    update_queue_display(app);
}

void update_queue_display(AppData *app)
{
    // Clear existing queue display
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(app->queue_box));
    for (iter = children; iter != NULL; iter = g_list_next(iter))
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);

    // Add current queue items
    for (int i = 0; i < app->queue_size; i++)
    {
        char label[200];
        time_t current_time = time(NULL);
        time_t start_time = app->queue[i].start_time;
        int elapsed_time = (start_time > 0) ? (int)(current_time - start_time) : 0;
        int total_time = app->queue[i].quantity * 10;
        int remaining_time = (start_time > 0) ? (total_time - elapsed_time) : total_time;

        if (remaining_time < 0)
            remaining_time = 0;

        snprintf(label, sizeof(label), "%dx %s - Remaining: %d sec",
                 app->queue[i].quantity, app->queue[i].beverage->name, remaining_time);

        GtkWidget *item = gtk_label_new(label);
        gtk_box_pack_start(GTK_BOX(app->queue_box), item, FALSE, FALSE, 0);
    }

    gtk_widget_show_all(app->queue_box);
}

// Queue processing
void *process_queue(void *arg)
{
    AppData *app = (AppData *)arg;
    app->is_processing = TRUE;

    while (app->queue_size > 0 && app->is_processing)
    {
        if (app->is_paused)
        {
            g_usleep(100000); // Sleep for 100ms
            continue;
        }

        Order *current_order = &app->queue[0];
        if (current_order->start_time == 0)
        {
            current_order->start_time = time(NULL);
        }

        // Update queue display every second
        g_idle_add((GSourceFunc)update_queue_display, app);

        // Simulate beverage preparation
        g_usleep(1000000); // Sleep for 1 second

        int elapsed_time = (int)(time(NULL) - current_order->start_time);
        if (elapsed_time >= current_order->quantity * 10)
        {
            current_order->end_time = time(NULL);

            // Save order to database
            char *sql = "INSERT INTO orders (beverage, quantity, start_time, end_time, date) VALUES (?, ?, ?, ?, date('now'));";
            sqlite3_stmt *stmt;
            if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK)
            {
                sqlite3_bind_text(stmt, 1, current_order->beverage->name, -1, SQLITE_STATIC);
                sqlite3_bind_int(stmt, 2, current_order->quantity);
                sqlite3_bind_int64(stmt, 3, current_order->start_time);
                sqlite3_bind_int64(stmt, 4, current_order->end_time);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }

            // Remove completed order from queue
            memmove(app->queue, app->queue + 1, --app->queue_size * sizeof(Order));
            g_idle_add((GSourceFunc)update_queue_display, app);
        }
    }

    app->is_processing = FALSE;
    return NULL;
}

// Button callbacks
void on_beverage_clicked(GtkWidget *widget, gpointer data)
{
    AppData *app = (AppData *)data;
    const char *label = gtk_button_get_label(GTK_BUTTON(widget));
    char beverage_name[50];
    double price;
    sscanf(label, "%[^-] - $%lf", beverage_name, &price);

    Beverage *beverage = malloc(sizeof(Beverage));
    strcpy(beverage->name, beverage_name);
    beverage->price = price;

    const char *quantity_text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->quantity_combo));
    int quantity = atoi(quantity_text);
    add_to_queue(app, beverage, quantity);
}

void on_start_clicked(GtkWidget *widget, gpointer data)
{
    AppData *app = (AppData *)data;
    if (!app->is_processing)
    {
        pthread_t thread;
        pthread_create(&thread, NULL, process_queue, app);
        pthread_detach(thread);
    }
}

void on_pause_clicked(GtkWidget *widget, gpointer data)
{
    AppData *app = (AppData *)data;
    app->is_paused = !app->is_paused;
    gtk_button_set_label(GTK_BUTTON(app->pause_button), app->is_paused ? "Resume" : "Pause");

    if (!app->is_paused && !app->is_processing)
    {
        // Restart processing if it was stopped
        pthread_t thread;
        pthread_create(&thread, NULL, process_queue, app);
        pthread_detach(thread);
    }
}
void clear_history(GtkWidget *widget, gpointer data)
{
    AppData *app = (AppData *)data;
    const char *sql = "DELETE FROM orders;";
    char *err_msg = 0;
    int rc = sqlite3_exec(app->db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    }
    show_history(NULL, app);
}

void show_history(GtkWidget *widget, gpointer data)
{
    AppData *app = (AppData *)data;

    if (app->history_window != NULL)
    {
        gtk_widget_destroy(app->history_window);
        app->history_window = NULL;
    }

    app->history_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->history_window), "Order History");
    gtk_window_set_default_size(GTK_WINDOW(app->history_window), 400, 300);
    gtk_window_set_resizable(GTK_WINDOW(app->history_window), FALSE);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(app->history_window), vbox);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);

    app->history_text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->history_text_view), FALSE);
    gtk_container_add(GTK_CONTAINER(scrolled_window), app->history_text_view);

    GtkWidget *clear_button = gtk_button_new_with_label("Clear History");
    g_signal_connect(clear_button, "clicked", G_CALLBACK(clear_history), app);
    gtk_box_pack_start(GTK_BOX(vbox), clear_button, FALSE, FALSE, 0);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->history_text_view));
    gtk_text_buffer_set_text(buffer, "", -1);

    const char *sql = "SELECT * FROM orders ORDER BY date DESC, start_time DESC LIMIT 100;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char *beverage = (const char *)sqlite3_column_text(stmt, 1);
            int quantity = sqlite3_column_int(stmt, 2);
            time_t start_time = sqlite3_column_int64(stmt, 3);
            time_t end_time = sqlite3_column_int64(stmt, 4);
            const char *date = (const char *)sqlite3_column_text(stmt, 5);

            char entry[256];
            snprintf(entry, sizeof(entry), "%s - %dx %s (Start: %s, End: %s)\n",
                     date, quantity, beverage, ctime(&start_time), ctime(&end_time));

            GtkTextIter end;
            gtk_text_buffer_get_end_iter(buffer, &end);
            gtk_text_buffer_insert(buffer, &end, entry, -1);
        }
        sqlite3_finalize(stmt);
    }

    gtk_widget_show_all(app->history_window);
}
