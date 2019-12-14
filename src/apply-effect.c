/*
	//gcc edge-detect.c bitmap.c -O2 -ftree-vectorize -fopt-info -mavx2 -fopt-info-vec-all
	//UTILISER UNIQUEMENT DES BMP 24bits
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "bitmap.h"
#include <stdint.h>
#include <pthread.h>
#include "info.h"


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

    for (int y = OFFSET; y < h - OFFSET; y++) {
        for (int x = OFFSET; x < w - OFFSET; x++) {
            Color_e c = { .Red = 0, .Green = 0, .Blue = 0};

            for(int a = 0; a < LENGHT; a++){
                for(int b = 0; b < LENGHT; b++){
                    int xn = x + a - OFFSET;
                    int yn = y + b - OFFSET;

                    Pixel* p = &original->pixel_data[yn][xn];

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
    int tmp_file_id = 1;
    Stack *stack = state->stack;
    Settings *settings = state->settings;
    while (1) {
        //TODO réutiliser le nom de l'image d'origine
        pthread_mutex_lock(&stack->lock);
        if (stack->count <= 0) {
            printf("[CONSUMER] Waiting refilling stack\n");
            pthread_cond_signal(&stack->can_transform_image);
            pthread_cond_wait(&stack->can_save_on_disk, &stack->lock);
        }
        if (stack->count > 0) {
            char file_name[400];
            sprintf(file_name, "%s/%d.bmp", settings->destination_folder, tmp_file_id);
            printf("[CONSUMER] count: %d | Saving transformed in : %s\n", stack->count, file_name);
            save_bitmap(stack->stack[stack->count - 1], file_name);
//            free(stack->stack[stack->count]);
//            stack->stack[stack->count] = 0;
            destroy_image(&stack->stack[stack->count - 1]);
            printf("is saved\n");
            stack->count--;
            printf("123\n");
            tmp_file_id++;
            printf("456\n");
        }
        printf("plop\n");
        pthread_mutex_unlock(&stack->lock);
        printf("bruh\n");
    }

}

void *transform_image(void *shared_state) {
    State *state = (State *) shared_state;
//    printf("[PRODUCER] id: %d | [%d;%d[ | %p\n", state->thread_id, state->start, state->end, state->list_image_files);
//    for(int i = state->start; i < state->end ; i++) {
//        printf("[PRODUCER] id: %2d - %2d - %p - %d | %s\n", state->thread_id, i, state->list_image_files[i], strlen(state->list_image_files[i]), state->list_image_files[i]);
//    }
    int index_file = state->start;
    Stack *stack = state->stack;
    Settings *settings = state->settings;
    const int *MAX = stack->max;
    while (1) {
        pthread_mutex_lock(&stack->lock);
        if (stack->count >= MAX) {
            pthread_cond_signal(&stack->can_save_on_disk);
            pthread_cond_wait(&stack->can_transform_image, &stack->lock);
        }
        printf("[PRODUCER] id: %d | count: %d, max: %d\n", state->thread_id, stack->count, stack->max);
        if (index_file < state->end && stack->count < MAX) {
            char file_name[400];
            sprintf(file_name, "%s/%s", settings->source_folder, state->list_image_files[index_file]);
//            printf("[PRODUCER] id: %d | %s\n", state->thread_id, file_name);

            Image img = open_bitmap(file_name);
            Image new_i;
//            stack->stack[stack->count] = (Image*)malloc(sizeof(Image));
            apply_effect(&img, &new_i, state->settings->effect);
            stack->stack[stack->count] = new_i;
            stack->count++;
            pthread_mutex_unlock(&stack->lock);
            index_file++;
        } else {
            pthread_mutex_unlock(&stack->lock);
            pthread_cond_signal(&stack->can_save_on_disk);
            return;
        }
    }
}

int main(int argc, char** argv) {
    Settings settings;
    Stack stack;
    State state;
    pthread_t consumer_thread;
    pthread_t *producer_threads;
    pthread_attr_t attr;

    int code = set_settings(argc, argv, &settings);
    if(code != 0) return code;
    print_settings(&settings);
    init_stack(&stack);
    state.stack = &stack;
    state.settings = &settings;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    producer_threads = (pthread_t *) malloc(sizeof(pthread_t) * settings.number_of_threads);
//    void* arg = &state;
    int number_of_files = list_dir(settings.source_folder, &state);
    if (number_of_files == -1) {
        return -1;
    }

    int integer_part, remains, end;
    integer_part = number_of_files / state.settings->number_of_threads;
    remains = number_of_files % state.settings->number_of_threads;
    end = 0;
    for (int i = 0; i < settings.number_of_threads; i++) {
        int start = end;
        end += integer_part;
        if (remains > 0) {
            remains--;
            end++;
        }
        State *thread_state = (State *) malloc(sizeof(State));
        clone_state(&state, thread_state);
        thread_state->start = start;
        thread_state->end = end;
        thread_state->thread_id = i;
//        printf("[MAIN] id: %d | [%d;%d[ | %p | size: %ld\n", thread_state->thread_id, thread_state->start, thread_state->end, thread_state->list_image_files,
//               sizeof(thread_state));
//        printf("[MAIN] i: %d | %p\n", i, thread_state);
        pthread_create(&producer_threads[i], &attr, transform_image, thread_state);
    }
    pthread_create(&consumer_thread, NULL, save_processed_image, &state);
    pthread_join(consumer_thread, NULL);
    Image img = open_bitmap("in/bmp_tank.bmp");
    Image new_i;
    apply_effect(&img, &new_i, settings.effect);
    save_bitmap(new_i, "out/test_out.bmp");
    free(state.list_image_files);
    free(producer_threads);
    //TODO ne pas oublier de free tous les var dynamiques
    return 0;
}