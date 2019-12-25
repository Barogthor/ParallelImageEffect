/*
	//gcc edge-detect.c bitmap.c -O2 -ftree-vectorize -fopt-info -mavx2 -fopt-info-vec-all
	//UTILISER UNIQUEMENT DES BMP 24bits
*/
#include "info.h"
#include "bitmap.h"
#include <pthread.h>

const float EDGE_KERNEL[DIM][DIM] = {{-1, -1, -1},
                                     {-1, 8,  -1},
                                     {-1, -1, -1}};
const float SHARPEN_KERNEL[DIM][DIM] = {{ 0,-1, 0},
                                        {-1, 5,-1},
                                        { 0,-1, 0}};

const float BOX_BLUR_KERNEL[DIM][DIM] = {{1/9,1/9,1/9},
                                         {1/9,1/9,1/9},
                                         {1/9,1/9,1/9}};

void get_matrix_effect(float dest[DIM][DIM], enum ImageEffect const effect) {
    switch (effect) {
        case BOX_BLUR:
            for (int i = 0; i < DIM; i++) {
                for (int j = 0; j < DIM; j++)
                    dest[i][j] = BOX_BLUR_KERNEL[i][j];
            }
            break;
        case SHARPEN:
            for (int i = 0; i < DIM; i++) {
                for (int j = 0; j < DIM; j++)
                    dest[i][j] = SHARPEN_KERNEL[i][j];
            }
            break;
        case EDGE_DETECT:
        default:
            for (int i = 0; i < DIM; i++) {
                for (int j = 0; j < DIM; j++)
                    dest[i][j] = EDGE_KERNEL[i][j];
            }
    }
}

void apply_effect(Image* original, Image* new_i, enum ImageEffect const effect) {
    int w = original->bmp_header.width;
    int h = original->bmp_header.height;
    float KERNEL[DIM][DIM];

    get_matrix_effect(KERNEL, effect);
    *new_i = new_image(w, h, original->bmp_header.bit_per_pixel, original->bmp_header.color_planes);

    LOOP_y:
    for (int y = OFFSET; y < h - OFFSET; y++) {
        LOOP_x:
        for (int x = OFFSET; x < w - OFFSET; x++) {
            Color_e c = { .Red = 0, .Green = 0, .Blue = 0};
//            float* tmp_color[] = {&c.Red, &c.Green, &c.Blue};
            LOOP_a:
            for (int a = 0; a < LENGHT; a++) {
                LOOP_b:
                for (int b = 0; b < LENGHT; b++) {
                    int xn = x + a - OFFSET;
                    int yn = y + b - OFFSET;

                    Pixel* p = &original->pixel_data[yn][xn];

//                    float o_color[] = {(float)p->r, (float)p->g, (float)p->b};
//                    LOOP_i: for(int i = 0; i < 3; i++){
//                        *tmp_color[i] += o_color[i] * KERNEL[a][b];
//                    }
                    c.Red += ((float) p->r) * KERNEL[a][b];
                    c.Green += ((float) p->g) * KERNEL[a][b];
                    c.Blue += ((float) p->b) * KERNEL[a][b];
                }
            }

            Pixel* dest = &new_i->pixel_data[y][x];
            dest->r = (uint8_t)(c.Red <= 0 ? 0 : c.Red >= 255 ? 255 : c.Red);
            dest->g = (uint8_t) (c.Green <= 0 ? 0 : c.Green >= 255 ? 255 : c.Green);
            dest->b = (uint8_t) (c.Blue <= 0 ? 0 : c.Blue >= 255 ? 255 : c.Blue);
        }
    }
}

//TODO utiliser une autre strcuture de données pouvant lire et écrire sans concurrence
void *save_processed_image(void *shared_state) {
    State *state = (State *) (shared_state);
    Stack *stack = state->stack;
    const Settings *settings = state->settings;
    while (1) {
        pthread_mutex_lock(&stack->lock);
        const current_count = stack->count;
        if (current_count <= 0) {
            if (stack->thread_remaining_at_work <= 0) {
                pthread_mutex_unlock(&stack->lock);
                printf("stopping consummer \n");
                return 0;
            } else {
                printf("[CONSUMER] Waiting refilling stack\n");
                pthread_cond_signal(&stack->can_transform_image);
                pthread_cond_wait(&stack->can_save_on_disk, &stack->lock);
            }
        }
        if (current_count > 0) {
            const int NAME_LENGTH =
                    strlen(settings->destination_folder) + strlen(stack->stack[current_count - 1].name) + 1;
            char file_name[NAME_LENGTH];
            memset(file_name, '\0', NAME_LENGTH);
            sprintf(file_name, "%s/%s", settings->destination_folder, stack->stack[current_count - 1].name);
            printf("[CONSUMER] count: %d | Saving transformed in : %s\n", current_count, file_name);
            save_bitmap(stack->stack[current_count - 1].image, file_name);
            destroy_image(&stack->stack[current_count - 1].image);
            memset(stack->stack[current_count - 1].name, '\0', NAME_BUFFER_SIZE);
            stack->count--;
        }
        pthread_mutex_unlock(&stack->lock);
    }
}

void *transform_image(void *shared_state) {
    State *state = (State *) shared_state;
    int index_file = state->start;
    Stack *stack = state->stack;
    const Settings *settings = state->settings;
    const int MAX = stack->max;
    while (1) {
        pthread_mutex_lock(&stack->lock);
        if (stack->count >= MAX) {
            pthread_cond_signal(&stack->can_save_on_disk);
            pthread_cond_wait(&stack->can_transform_image, &stack->lock);
        }
        printf("[PRODUCER] id: %d | count: %d, max: %d\n", state->thread_id, stack->count, stack->max);
        if (index_file < state->end && stack->count < MAX) {
            const int SOURCE_LEN = strlen(settings->source_folder);
            const int FILE_NAME_LEN = strlen(state->list_image_files[index_file]);
            char file_path[NAME_BUFFER_SIZE];
            sprintf(file_path, "%s/%s", settings->source_folder, state->list_image_files[index_file]);

            Image img = open_bitmap(file_path);
            Image new_i;
            apply_effect(&img, &new_i, state->settings->effect);
            stack->stack[stack->count].image = new_i;
            sprintf(stack->stack[stack->count].name, "%s\0", state->list_image_files[index_file]);
            stack->count++;
            pthread_mutex_unlock(&stack->lock);
            index_file++;
        } else {
            stack->thread_remaining_at_work--;
            pthread_mutex_unlock(&stack->lock);
            pthread_cond_signal(&stack->can_save_on_disk);
            return;
        }
    }
}

void start_producers(pthread_t *producer_threads, const State *state, const int number_of_files) {
    pthread_attr_t attr;
    int integer_part, remains, end;
    integer_part = number_of_files / state->settings->number_of_threads;
    remains = number_of_files % state->settings->number_of_threads;
    end = 0;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    for (int i = 0; i < state->settings->number_of_threads; i++) {
        int start = end;
        end += integer_part;
        if (remains > 0) {
            remains--;
            end++;
        }
        State *thread_state = (State *) malloc(sizeof(State));
        clone_state(state, thread_state);
        thread_state->start = start;
        thread_state->end = end;
        thread_state->thread_id = i;
        pthread_create(&producer_threads[i], &attr, transform_image, thread_state);
    }
}

int main(int argc, char** argv) {
    Settings settings;
    Stack stack;
    State state;
    pthread_t consumer_thread;
    pthread_t *producer_threads;

    int code = set_settings(argc, argv, &settings);
    if (code != 0)
        return code;
    print_settings(&settings);
    init_stack(&stack, &settings);
    state.stack = &stack;
    state.settings = &settings;
    producer_threads = malloc(sizeof(pthread_t) * settings.number_of_threads);
    int number_of_files = list_dir(settings.source_folder, &state);
    if (number_of_files == -1) {
        return -1;
    }
    start_producers(producer_threads, &state, number_of_files);
    pthread_create(&consumer_thread, NULL, save_processed_image, &state);
    pthread_join(consumer_thread, NULL);
    for (int i = 0; i < number_of_files; i++)
        free(state.list_image_files[i]);
    free(state.list_image_files);
    free(producer_threads);
    //TODO ne pas oublier de free toutes les var dynamiques
    return 0;
}