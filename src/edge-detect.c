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


const float KERNEL[DIM][DIM] = {{-1,-1,-1},
                                {-1, 8,-1},
                                {-1,-1,-1}};
const float SHARPEN_KERNEL[DIM][DIM] = {{ 0,-1, 0},
                                        {-1, 5,-1},
                                        { 0,-1, 0}};

const float BOX_BLUR_KERNEL[DIM][DIM] = {{1/9,1/9,1/9},
                                         {1/9,1/9,1/9},
                                         {1/9,1/9,1/9}};

//float** get_matrix_effect(enum ImageEffect const effect){
//    switch(effect){
//        case BOX_BLUR:
//            return BOX_BLUR_KERNEL;
//        case SHARPEN:
//            return SHARPEN_KERNEL;
//        case EDGE_DETECT:
//        default:
//            return KERNEL;
//    }
//}

void apply_effect(Image* original, Image* new_i, enum ImageEffect const effect) {

    int w = original->bmp_header.width;
    int h = original->bmp_header.height;

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
    while (1) {
        //TODO réutiliser le nom de l'image d'origine
        pthread_mutex_lock(&state->stack->lock);
        if (state->stack->count <= 0) {
            pthread_cond_signal(&state->stack->can_transform_image);
            pthread_cond_wait(&state->stack->can_save_on_disk, &state->stack->lock);
        }
        char file_name[255];
        sprintf(file_name, "%s/%d", state->settings->destination_folder, tmp_file_id);
//        save_bitmap(state->stack->stack[state->stack->count], file_name);
        state->stack->count--;
        tmp_file_id++;
        pthread_mutex_unlock(&state->stack->lock);
    }

}

void *transform_image(void *shared_state) {
    State *state = (State *) shared_state;
    while (1) {
        pthread_mutex_lock(&state->stack->lock);
        if (state->stack->count >= state->stack->max) {
            pthread_cond_signal(&state->stack->can_save_on_disk);
            pthread_cond_wait(&state->stack->can_transform_image, &state->stack->lock);
        }
        char file_name[255];
        sprintf(file_name, "%s/%s", state->settings->source_folder, "un_nom");

//        Image img = open_bitmap(file_name);
//        Image new_i;
//        apply_effect(&img, &new_i, state->settings->effect);
//        state->stack->stack[state->stack->count++] = new_i;
        pthread_mutex_unlock(&state->stack->lock);
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
    } else
        state.image_amount = number_of_files;
    for (int i = 0; i < state.image_amount; i++) {
        printf("%s\n", state.list_image_files[i]);
    }

    int integer_part, remains, end;
    integer_part = state.image_amount / state.settings->number_of_threads;
    remains = state.image_amount % state.settings->number_of_threads;
    printf("int part: %d\nremains: %d\n", integer_part, remains);
    end = 0;
    printf("amount: %d\n", number_of_files);
    for (int i = 0; i < settings.number_of_threads; i++) {
        int start = end;
        end += integer_part;
        if (remains > 0) {
            remains--;
            end++;
        }
        State thread_state;
        clone_state(&state, &thread_state);
        thread_state.start = start;
        thread_state.end = end;
        printf("[%d;%d[\n", thread_state.start, thread_state.end);
        pthread_create(&producer_threads[i], &attr, transform_image, &state);
    }
//    pthread_create(&consumer_thread, NULL, save_processed_image, &state);
//    pthread_join(consumer_thread, NULL);
    Image img = open_bitmap("in/bmp_tank.bmp");
    Image new_i;
    apply_effect(&img, &new_i, settings.effect);
    save_bitmap(new_i, "out/test_out.bmp");
    free(state.list_image_files);
    free(producer_threads);
    return 0;
}