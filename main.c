#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

#define WINDOW_WIDTH 1100
#define WINDOW_HEIGHT 700
#define TOOLBAR_HEIGHT 70
#define MAX_VERTICES 26
#define MAX_UNDO 64

typedef struct {
    char letter;
    float x;
    float y;
    size_t *connections;
    size_t num_connections;
} vertex_t;

typedef struct {
    vertex_t vertices[MAX_VERTICES];
    size_t num_vertices;
} graph_t;

static void set_color(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b)
{
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
}

static int add_edge(graph_t *graph, size_t from, size_t to)
{
    for (size_t i = 0; i < graph->vertices[from].num_connections; i++)
        if (graph->vertices[from].connections[i] == to)
            return 0;

    vertex_t *vertex = &graph->vertices[from];
    size_t *connections = realloc(vertex->connections,
                                  (vertex->num_connections + 1) * sizeof(*connections));
    if (!connections)
        return 0;
    vertex->connections = connections;
    vertex->connections[vertex->num_connections++] = to;
    return 1;
}

static int add_undirected_edge(graph_t *graph, size_t first, size_t second)
{
    int changed = add_edge(graph, first, second);
    changed |= add_edge(graph, second, first);
    return changed;
}

static int has_edge(const graph_t *graph, size_t from, size_t to)
{
    for (size_t i = 0; i < graph->vertices[from].num_connections; i++)
        if (graph->vertices[from].connections[i] == to)
            return 1;
    return 0;
}

static int remove_edge(graph_t *graph, size_t from, size_t to)
{
    vertex_t *vertex = &graph->vertices[from];
    for (size_t i = 0; i < vertex->num_connections; i++) {
        if (vertex->connections[i] == to) {
            memmove(&vertex->connections[i], &vertex->connections[i + 1],
                    (vertex->num_connections - i - 1) * sizeof(*vertex->connections));
            vertex->num_connections--;
            if (vertex->num_connections == 0) {
                free(vertex->connections);
                vertex->connections = NULL;
            } else {
                vertex->connections = realloc(vertex->connections,
                    vertex->num_connections * sizeof(*vertex->connections));
            }
            return 1;
        }
    }
    return 0;
}

static int remove_undirected_edge(graph_t *graph, size_t first, size_t second)
{
    int changed = remove_edge(graph, first, second);
    changed |= remove_edge(graph, second, first);
    return changed;
}

static size_t vertex_at(const graph_t *graph, int x, int y)
{
    for (size_t i = 0; i < graph->num_vertices; i++) {
        float dx = graph->vertices[i].x - x;
        float dy = graph->vertices[i].y - y;
        if (dx * dx + dy * dy <= 22.0f * 22.0f)
            return i;
    }
    return graph->num_vertices;
}

static void free_graph(graph_t *graph)
{
    for (size_t i = 0; i < graph->num_vertices; i++)
        free(graph->vertices[i].connections);
}

static int clone_graph(const graph_t *source, graph_t *copy)
{
    *copy = (graph_t){0};
    for (size_t i = 0; i < source->num_vertices; i++) {
        copy->vertices[i] = source->vertices[i];
        copy->vertices[i].connections = NULL;
        if (source->vertices[i].num_connections > 0) {
            copy->vertices[i].connections = malloc(
                source->vertices[i].num_connections * sizeof(size_t));
            if (!copy->vertices[i].connections) {
                free_graph(copy);
                return 0;
            }
            memcpy(copy->vertices[i].connections, source->vertices[i].connections,
                   source->vertices[i].num_connections * sizeof(size_t));
        }
        copy->num_vertices++;
    }
    return 1;
}

static int push_undo(graph_t *history, size_t *history_length, const graph_t *graph)
{
    if (*history_length == MAX_UNDO) {
        free_graph(&history[0]);
        memmove(history, history + 1, (MAX_UNDO - 1) * sizeof(history[0]));
        (*history_length)--;
    }
    if (!clone_graph(graph, &history[*history_length]))
        return 0;
    (*history_length)++;
    return 1;
}

static int undo_graph(graph_t *graph, graph_t *history, size_t *history_length)
{
    if (*history_length == 0)
        return 0;
    free_graph(graph);
    *graph = history[--(*history_length)];
    history[*history_length] = (graph_t){0};
    return 1;
}

static size_t edge_count(const graph_t *graph)
{
    size_t connections = 0;
    for (size_t i = 0; i < graph->num_vertices; i++)
        connections += graph->vertices[i].num_connections;
    return connections / 2;
}

static int save_graph(const graph_t *graph, const char *path)
{
    FILE *file = fopen(path, "w");
    if (!file)
        return 0;

    fprintf(file, "%zu\n", graph->num_vertices);
    for (size_t i = 0; i < graph->num_vertices; i++)
        fprintf(file, "%c %.2f %.2f\n", graph->vertices[i].letter,
                graph->vertices[i].x, graph->vertices[i].y);
    for (size_t i = 0; i < graph->num_vertices; i++)
        for (size_t j = 0; j < graph->vertices[i].num_connections; j++)
            fprintf(file, "%zu %zu\n", i, graph->vertices[i].connections[j]);

    fclose(file);
    return 1;
}

static int choose_file(char *path, size_t path_size, int save)
{
#ifdef _WIN32
    OPENFILENAMEA dialog = {0};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = path;
    dialog.nMaxFile = (DWORD)path_size;
    dialog.lpstrFilter = "Graph files (*.graph;*.txt)\0*.graph;*.txt\0All files (*.*)\0*.*\0";
    dialog.lpstrDefExt = "graph";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (save) {
        dialog.Flags |= OFN_OVERWRITEPROMPT;
        return GetSaveFileNameA(&dialog) != 0;
    }
    dialog.Flags |= OFN_FILEMUSTEXIST;
    return GetOpenFileNameA(&dialog) != 0;
#else
    const char *command = save
        ? "zenity --file-selection --save --confirm-overwrite --title='Save graph' "
          "--file-filter='Graph files | *.graph *.txt' --file-filter='All files | *'"
        : "zenity --file-selection --title='Load graph' "
          "--file-filter='Graph files | *.graph *.txt' --file-filter='All files | *'";
    FILE *dialog = popen(command, "r");
    if (!dialog)
        return 0;

    int found = fgets(path, (int)path_size, dialog) != NULL;
    int status = pclose(dialog);
    if (!found || status != 0)
        return 0;

    path[strcspn(path, "\r\n")] = '\0';
    return path[0] != '\0';
#endif
}

static int load_graph(graph_t *graph, const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return 0;

    graph_t loaded = {0};
    size_t vertex_count;
    if (fscanf(file, "%zu", &vertex_count) != 1 || vertex_count > MAX_VERTICES) {
        fclose(file);
        return 0;
    }

    for (size_t i = 0; i < vertex_count; i++) {
        vertex_t *vertex = &loaded.vertices[i];
        if (fscanf(file, " %c %f %f", &vertex->letter, &vertex->x, &vertex->y) != 3) {
            free_graph(&loaded);
            fclose(file);
            return 0;
        }
        loaded.num_vertices++;
    }

    size_t from, to;
    while (fscanf(file, "%zu %zu", &from, &to) == 2) {
        if (from >= loaded.num_vertices || to >= loaded.num_vertices || from == to) {
            free_graph(&loaded);
            fclose(file);
            return 0;
        }
        add_edge(&loaded, from, to);
    }
    if (!feof(file)) {
        free_graph(&loaded);
        fclose(file);
        return 0;
    }

    fclose(file);
    free_graph(graph);
    *graph = loaded;
    return 1;
}

static void search_path(const graph_t *graph, size_t current, size_t end,
                        size_t *path, size_t path_length, int *visited,
                        size_t *best, size_t *best_length)
{
    if (current == end) {
        if (path_length > *best_length) {
            *best_length = path_length;
            memcpy(best, path, path_length * sizeof(*best));
        }
        return;
    }

    for (size_t i = 0; i < graph->vertices[current].num_connections; i++) {
        size_t next = graph->vertices[current].connections[i];
        if (!visited[next]) {
            visited[next] = 1;
            path[path_length] = next;
            search_path(graph, next, end, path, path_length + 1, visited,
                        best, best_length);
            visited[next] = 0;
        }
    }
}

static int path_contains(const size_t *path, size_t path_length, size_t vertex)
{
    for (size_t i = 0; i < path_length; i++)
        if (path[i] == vertex)
            return 1;
    return 0;
}

static int path_edge(const size_t *path, size_t path_length, size_t from, size_t to)
{
    for (size_t i = 0; i + 1 < path_length; i++)
        if ((path[i] == from && path[i + 1] == to) ||
            (path[i] == to && path[i + 1] == from))
            return 1;
    return 0;
}

static void solve_longest_path(const graph_t *graph, size_t start, size_t end,
                               size_t *solved_path, size_t *solved_length,
                               char *result, size_t result_size)
{
    size_t *path = malloc(graph->num_vertices * sizeof(*path));
    size_t *best = malloc(graph->num_vertices * sizeof(*best));
    int *visited = calloc(graph->num_vertices, sizeof(*visited));
    size_t best_length = 0;
    *solved_length = 0;

    if (!path || !best || !visited) {
        snprintf(result, result_size, "Out of memory");
    } else {
        path[0] = start;
        visited[start] = 1;
        search_path(graph, start, end, path, 1, visited, best, &best_length);
        if (best_length > 0) {
            memcpy(solved_path, best, best_length * sizeof(*solved_path));
            *solved_length = best_length;
        }
    }

    if (!path || !best || !visited) {
        snprintf(result, result_size, "Out of memory");
    } else if (best_length == 0) {
        snprintf(result, result_size, "No path found");
    } else {
        size_t used = 0;
        used += (size_t)snprintf(result + used, result_size - used,
                                 "Longest path (%zu): ", best_length);
        for (size_t i = 0; i < best_length && used < result_size; i++)
            used += (size_t)snprintf(result + used, result_size - used, "%c%s",
                                     graph->vertices[best[i]].letter,
                                     i + 1 == best_length ? "" : " -> ");
    }
    free(path);
    free(best);
    free(visited);
}

/* Small built-in 5x7 font keeps the editor independent of SDL_ttf. */
static const char *glyph(char c)
{
    static const char *patterns[] = {
        "01110 10001 10011 10101 11001 10001 01110", /* 0 */
        "00100 01100 00100 00100 00100 00100 01110", /* 1 */
        "01110 10001 00001 00010 00100 01000 11111", /* 2 */
        "11110 00001 00001 01110 00001 00001 11110", /* 3 */
        "00010 00110 01010 10010 11111 00010 00010", /* 4 */
        "11111 10000 10000 11110 00001 00001 11110", /* 5 */
        "00110 01000 10000 11110 10001 10001 01110", /* 6 */
        "11111 00001 00010 00100 01000 01000 01000", /* 7 */
        "01110 10001 10001 01110 10001 10001 01110", /* 8 */
        "01110 10001 10001 01111 00001 00010 01100", /* 9 */
    };
    static const char *letters[] = {
        "01110 10001 10001 11111 10001 10001 10001", /* A */
        "11110 10001 10001 11110 10001 10001 11110", /* B */
        "01111 10000 10000 10000 10000 10000 01111", /* C */
        "11110 10001 10001 10001 10001 10001 11110", /* D */
        "11111 10000 10000 11110 10000 10000 11111", /* E */
        "11111 10000 10000 11110 10000 10000 10000", /* F */
        "01111 10000 10000 10111 10001 10001 01111", /* G */
        "10001 10001 10001 11111 10001 10001 10001", /* H */
        "11111 00100 00100 00100 00100 00100 11111", /* I */
        "00111 00010 00010 00010 10010 10010 01100", /* J */
        "10001 10010 10100 11000 10100 10010 10001", /* K */
        "10000 10000 10000 10000 10000 10000 11111", /* L */
        "10001 11011 10101 10101 10001 10001 10001", /* M */
        "10001 11001 10101 10011 10001 10001 10001", /* N */
        "01110 10001 10001 10001 10001 10001 01110", /* O */
        "11110 10001 10001 11110 10000 10000 10000", /* P */
        "01110 10001 10001 10001 10101 10010 01101", /* Q */
        "11110 10001 10001 11110 10100 10010 10001", /* R */
        "01111 10000 10000 01110 00001 00001 11110", /* S */
        "11111 00100 00100 00100 00100 00100 00100", /* T */
        "10001 10001 10001 10001 10001 10001 01110", /* U */
        "10001 10001 10001 10001 10001 01010 00100", /* V */
        "10001 10001 10001 10101 10101 11011 10001", /* W */
        "10001 10001 01010 00100 01010 10001 10001", /* X */
        "10001 10001 01010 00100 00100 00100 00100", /* Y */
        "11111 00001 00010 00100 01000 10000 11111"  /* Z */
    };
    if (c >= '0' && c <= '9') return patterns[c - '0'];
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    return NULL;
}

static void draw_text(SDL_Renderer *renderer, int x, int y, const char *text, int scale)
{
    for (; *text; text++, x += 6 * scale) {
        const char *pattern = glyph(*text);
        if (!pattern) continue;
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (pattern[row * 6 + col] == '1') {
                    SDL_Rect pixel = {x + col * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &pixel);
                }
            }
        }
    }
}

static void draw_arrow(SDL_Renderer *renderer, const vertex_t *from, const vertex_t *to)
{
    float dx = to->x - from->x, dy = to->y - from->y;
    float length = SDL_sqrtf(dx * dx + dy * dy);
    if (length < 1.0f) return;
    float ux = dx / length, uy = dy / length;
    int x1 = (int)(from->x + ux * 22), y1 = (int)(from->y + uy * 22);
    int x2 = (int)(to->x - ux * 22), y2 = (int)(to->y - uy * 22);
    SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
    SDL_RenderDrawLine(renderer, x2, y2, x2 - (int)(ux * 10 - uy * 5),
                       y2 - (int)(uy * 10 + ux * 5));
    SDL_RenderDrawLine(renderer, x2, y2, x2 - (int)(ux * 10 + uy * 5),
                       y2 - (int)(uy * 10 - ux * 5));
}

static void draw_editor(SDL_Renderer *renderer, const graph_t *graph,
                        size_t start, size_t end, size_t drag_from, int mouse_x, int mouse_y,
                        const size_t *solved_path, size_t solved_length)
{
    set_color(renderer, 245, 247, 250);
    SDL_RenderClear(renderer);
    set_color(renderer, 31, 41, 55);
    SDL_Rect toolbar = {0, 0, WINDOW_WIDTH, TOOLBAR_HEIGHT};
    SDL_RenderFillRect(renderer, &toolbar);

    set_color(renderer, 255, 255, 255);
    SDL_Rect solve_button = {20, 16, 120, 38};
    SDL_Rect save_button = {180, 16, 120, 38};
    SDL_Rect load_button = {315, 16, 120, 38};
    SDL_RenderFillRect(renderer, &solve_button);
    SDL_RenderFillRect(renderer, &save_button);
    SDL_RenderFillRect(renderer, &load_button);
    set_color(renderer, 31, 41, 55);
    draw_text(renderer, 48, 27, "SOLVE", 2);
    draw_text(renderer, 208, 27, "SAVE", 2);
    draw_text(renderer, 343, 27, "OPEN", 2);
    draw_text(renderer, 870, 27, "EDGES", 2);
    char edge_total[16];
    snprintf(edge_total, sizeof(edge_total), "%zu", edge_count(graph));
    draw_text(renderer, 970, 27, edge_total, 2);

    set_color(renderer, 100, 116, 139);
    for (size_t i = 0; i < graph->num_vertices; i++)
        for (size_t j = 0; j < graph->vertices[i].num_connections; j++) {
            size_t connected = graph->vertices[i].connections[j];
            if (path_edge(solved_path, solved_length, i, connected))
                set_color(renderer, 245, 158, 11);
            else
                set_color(renderer, 100, 116, 139);
            draw_arrow(renderer, &graph->vertices[i],
                       &graph->vertices[connected]);
        }
    if (drag_from < graph->num_vertices) {
        set_color(renderer, 59, 130, 246);
        SDL_RenderDrawLine(renderer, (int)graph->vertices[drag_from].x,
                           (int)graph->vertices[drag_from].y, mouse_x, mouse_y);
    }

    for (size_t i = 0; i < graph->num_vertices; i++) {
        if (i == start) set_color(renderer, 34, 197, 94);
        else if (i == end) set_color(renderer, 239, 68, 68);
        else if (path_contains(solved_path, solved_length, i))
            set_color(renderer, 245, 158, 11);
        else set_color(renderer, 59, 130, 246);
        SDL_Rect node = {(int)graph->vertices[i].x - 22, (int)graph->vertices[i].y - 22, 44, 44};
        SDL_RenderFillRect(renderer, &node);
        set_color(renderer, 255, 255, 255);
        char label[2] = {graph->vertices[i].letter, '\0'};
        draw_text(renderer, (int)graph->vertices[i].x - 5, (int)graph->vertices[i].y - 8,
                  label, 2);
    }
}

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("Longest Path Graph Editor",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!window || !renderer) {
        fprintf(stderr, "Could not create editor window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    graph_t graph = {0};
    graph_t undo_history[MAX_UNDO] = {0};
    size_t undo_length = 0;
    size_t start = MAX_VERTICES, end = MAX_VERTICES, drag_from = MAX_VERTICES;
    size_t solved_path[MAX_VERTICES];
    size_t solved_length = 0;
    char status[256] = "Click to add points; left-drag connects; right-drag removes edges";
    char save_path[512];
    save_path[0] = '\0';
    int running = 1, dragging = 0, mouse_x = 0, mouse_y = 0;
    Uint8 drag_button = SDL_BUTTON_LEFT;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = 0;
            if (event.type == SDL_MOUSEMOTION) {
                mouse_x = event.motion.x; mouse_y = event.motion.y;
            }
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                size_t hit = vertex_at(&graph, event.button.x, event.button.y);
                if (event.button.y < TOOLBAR_HEIGHT && event.button.x >= 20 &&
                    event.button.x < 140) {
                    if (start < graph.num_vertices && end < graph.num_vertices) {
                        char path[220];
                        solve_longest_path(&graph, start, end, solved_path, &solved_length,
                                           path, sizeof(path));
                        snprintf(status, sizeof(status), "%s",
                                 solved_length > 0 ? "Path highlighted" : "No path found");
                    } else {
                        solved_length = 0;
                        snprintf(status, sizeof(status), "Select a start and end point first");
                    }
                } else if (event.button.y < TOOLBAR_HEIGHT && event.button.x >= 180 &&
                           event.button.x < 300) {
                    if (!choose_file(save_path, sizeof(save_path), 1)) {
                        snprintf(status, sizeof(status), "Save cancelled");
                    } else {
                        snprintf(status, sizeof(status), "%s",
                                 save_graph(&graph, save_path) ? "Graph saved" :
                                                                  "Could not save graph");
                    }
                } else if (event.button.y < TOOLBAR_HEIGHT && event.button.x >= 315 &&
                           event.button.x < 435) {
                    char load_path[512];
                    if (!choose_file(load_path, sizeof(load_path), 0)) {
                        snprintf(status, sizeof(status), "Open cancelled");
                        continue;
                    }
                    graph_t loaded = {0};
                    if (!load_graph(&loaded, load_path)) {
                        snprintf(status, sizeof(status), "Could not load graph");
                    } else if (!push_undo(undo_history, &undo_length, &graph)) {
                        free_graph(&loaded);
                        snprintf(status, sizeof(status), "Could not record undo state");
                    } else {
                        free_graph(&graph);
                        graph = loaded;
                        snprintf(save_path, sizeof(save_path), "%s", load_path);
                        start = end = MAX_VERTICES;
                        solved_length = 0;
                        snprintf(status, sizeof(status), "Graph opened");
                    }
                } else if (hit < graph.num_vertices) {
                    solved_length = 0;
                    drag_from = hit; dragging = 1;
                } else if (event.button.y >= TOOLBAR_HEIGHT && graph.num_vertices < MAX_VERTICES) {
                    if (!push_undo(undo_history, &undo_length, &graph)) {
                        snprintf(status, sizeof(status), "Could not record undo state");
                        continue;
                    }
                    vertex_t *vertex = &graph.vertices[graph.num_vertices];
                    vertex->letter = (char)('A' + graph.num_vertices);
                    vertex->x = (float)event.button.x; vertex->y = (float)event.button.y;
                    vertex->connections = NULL; vertex->num_connections = 0;
                    graph.num_vertices++;
                    solved_length = 0;
                    snprintf(status, sizeof(status), "Point %c added", vertex->letter);
                }
            }
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
                size_t hit = vertex_at(&graph, event.button.x, event.button.y);
                if (hit < graph.num_vertices) {
                    solved_length = 0;
                    drag_from = hit;
                    drag_button = SDL_BUTTON_RIGHT;
                    dragging = 1;
                }
            }
            if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT && dragging) {
                size_t hit = vertex_at(&graph, event.button.x, event.button.y);
                if (hit < graph.num_vertices && hit != drag_from) {
                    if (!has_edge(&graph, drag_from, hit) || !has_edge(&graph, hit, drag_from)) {
                        if (!push_undo(undo_history, &undo_length, &graph)) {
                            snprintf(status, sizeof(status), "Could not record undo state");
                        } else {
                            add_undirected_edge(&graph, drag_from, hit);
                            solved_length = 0;
                            snprintf(status, sizeof(status), "Connected %c <-> %c",
                                     graph.vertices[drag_from].letter, graph.vertices[hit].letter);
                        }
                    }
                } else if (hit == drag_from) {
                    solved_length = 0;
                    if (start == MAX_VERTICES || (start < graph.num_vertices && end < graph.num_vertices)) {
                        start = hit;
                        end = MAX_VERTICES;
                        snprintf(status, sizeof(status), "Start point: %c; select another point as the end",
                                 graph.vertices[hit].letter);
                    } else {
                        end = hit;
                        snprintf(status, sizeof(status), "End point: %c; press SOLVE",
                                 graph.vertices[hit].letter);
                    }
                }
                dragging = 0; drag_from = MAX_VERTICES;
            }
            if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_RIGHT &&
                dragging && drag_button == SDL_BUTTON_RIGHT) {
                size_t hit = vertex_at(&graph, event.button.x, event.button.y);
                if (hit < graph.num_vertices && hit != drag_from &&
                    (has_edge(&graph, drag_from, hit) || has_edge(&graph, hit, drag_from))) {
                    if (!push_undo(undo_history, &undo_length, &graph)) {
                        snprintf(status, sizeof(status), "Could not record undo state");
                    } else {
                        remove_undirected_edge(&graph, drag_from, hit);
                        solved_length = 0;
                        snprintf(status, sizeof(status), "Removed line %c <-> %c",
                                 graph.vertices[drag_from].letter, graph.vertices[hit].letter);
                    }
                } else if (hit == drag_from) {
                    snprintf(status, sizeof(status), "Right-drag from one point to another to remove a line");
                }
                dragging = 0;
                drag_from = MAX_VERTICES;
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_s &&
                (event.key.keysym.mod & KMOD_CTRL)) {
                if (save_path[0] == '\0' && !choose_file(save_path, sizeof(save_path), 1)) {
                    snprintf(status, sizeof(status), "Save cancelled");
                } else {
                    snprintf(status, sizeof(status), "%s",
                             save_graph(&graph, save_path) ? "Graph saved" :
                                                              "Could not save graph");
                }
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_o &&
                (event.key.keysym.mod & KMOD_CTRL)) {
                char load_path[512];
                graph_t loaded = {0};
                if (!choose_file(load_path, sizeof(load_path), 0)) {
                    snprintf(status, sizeof(status), "Open cancelled");
                } else if (!load_graph(&loaded, load_path)) {
                    snprintf(status, sizeof(status), "Could not load graph");
                } else if (!push_undo(undo_history, &undo_length, &graph)) {
                    free_graph(&loaded);
                    snprintf(status, sizeof(status), "Could not record undo state");
                } else {
                    free_graph(&graph);
                    graph = loaded;
                    snprintf(save_path, sizeof(save_path), "%s", load_path);
                    start = end = MAX_VERTICES;
                    solved_length = 0;
                    snprintf(status, sizeof(status), "Graph opened");
                }
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_z &&
                (event.key.keysym.mod & KMOD_CTRL)) {
                if (undo_graph(&graph, undo_history, &undo_length)) {
                    start = end = MAX_VERTICES;
                    solved_length = 0;
                    snprintf(status, sizeof(status), "Undid last graph change");
                } else {
                    snprintf(status, sizeof(status), "Nothing to undo");
                }
            }
        }
        draw_editor(renderer, &graph, start, end, dragging ? drag_from : MAX_VERTICES,
                    mouse_x, mouse_y, solved_path, solved_length);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
    free_graph(&graph);
    for (size_t i = 0; i < undo_length; i++)
        free_graph(&undo_history[i]);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
