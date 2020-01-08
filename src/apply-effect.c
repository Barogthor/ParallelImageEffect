/*
	//gcc edge-detect.c bitmap.c -O2 -ftree-vectorize -fopt-info -mavx2 -fopt-info-vec-all
	//UTILISER UNIQUEMENT DES BMP 24bits
*/
#include "info.h"
#include "bitmap.h"
#include <pthread.h>

typedef struct matrix_convolution_t{
    float** matrix;
    const double constant;
    const int DIM_X;
    const int DIM_Y;
} MatrixConvolution;

void init_convolution_matrix(MatrixConvolution* convolution){
    convolution->matrix = malloc(convolution->DIM_X * sizeof(float*));
    for(int i = 0 ; i < convolution->DIM_X; i++)
        convolution->matrix[i] = malloc(convolution->DIM_Y * sizeof(float));
}
const MatrixConvolution* init_edge_detect(){
  MatrixConvolution convolution = { .constant = 1, .DIM_X = 3, .DIM_Y = 3};
  init_convolution_matrix(&convolution);
  convolution.matrix[0][0] = -1;
  convolution.matrix[0][1] = -1;
  convolution.matrix[0][2] = -1;
  convolution.matrix[1][0] = -1;
  convolution.matrix[1][1] = -8;
  convolution.matrix[1][2] = -1;
  convolution.matrix[2][0] = -1;
  convolution.matrix[2][1] = -1;
  convolution.matrix[2][2] = -1;
  return &convolution;
}
const MatrixConvolution* init_sharpen(){
  MatrixConvolution convolution = { .constant = 1, .DIM_X = 3, .DIM_Y = 3};
  init_convolution_matrix(&convolution);
  convolution.matrix[0][0] = -0;
  convolution.matrix[0][1] = -1;
  convolution.matrix[0][2] = 0;
  convolution.matrix[1][0] = -1;
  convolution.matrix[1][1] = 5;
  convolution.matrix[1][2] = -1;
  convolution.matrix[2][0] = 0;
  convolution.matrix[2][1] = -1;
  convolution.matrix[2][2] = 0;
    return &convolution;
}
const MatrixConvolution* init_box_blur(){
  MatrixConvolution convolution = { .constant = 1/9, .DIM_X = 3, .DIM_Y = 3};
  init_convolution_matrix(&convolution);
  convolution.matrix[0][0] = 1;
  convolution.matrix[0][1] = 1;
  convolution.matrix[0][2] = 1;
  convolution.matrix[1][0] = 1;
  convolution.matrix[1][1] = 1;
  convolution.matrix[1][2] = 1;
  convolution.matrix[2][0] = 1;
  convolution.matrix[2][1] = 1;
  convolution.matrix[2][2] = 1;
    return &convolution;
}

const MatrixConvolution* EDGE_KERNEL;
const MatrixConvolution* SHARPEN_KERNEL;
const MatrixConvolution* BOX_BLUR_KERNEL;
const MatrixConvolution* GAUSS_BLUR_KERNEL;

const MatrixConvolution* get_matrix_effect(float dest[DIM][DIM], enum ImageEffect const effect) {
    switch (effect) {
        case BOX_BLUR:
            return BOX_BLUR_KERNEL;
        case SHARPEN:
            return SHARPEN_KERNEL;
        case EDGE_DETECT:
        default:
            return EDGE_KERNEL;
    }
}

void apply_effect(Image* original, Image* new_i, enum ImageEffect const effect) {
    int w = original->bmp_header.width;
    int h = original->bmp_header.height;
    float KERNEL1[DIM][DIM];

    const MatrixConvolution* KERNEL = get_matrix_effect(KERNEL1, effect);
    *new_i = new_image(w, h, original->bmp_header.bit_per_pixel, original->bmp_header.color_planes);

    LOOP_y:
    for (int y = OFFSET; y < h - OFFSET; y++) {
        LOOP_x:
        for (int x = OFFSET; x < w - OFFSET; x++) {
            Color_e c = { .Red = 0, .Green = 0, .Blue = 0};
//            float* tmp_color[] = {&c.Red, &c.Green, &c.Blue};
            LOOP_a:
            for (int a = 0; a < KERNEL->DIM_X; a++) {
                LOOP_b:
                for (int b = 0; b < KERNEL->DIM_Y; b++) {
                    int xn = x + a - OFFSET;
                    int yn = y + b - OFFSET;

                    Pixel* p = &original->pixel_data[yn][xn];

//                    float o_color[] = {(float)p->r, (float)p->g, (float)p->b};
//                    LOOP_i: for(int i = 0; i < 3; i++){
//                        *tmp_color[i] += o_color[i] * KERNEL[a][b];
//                    }
                    c.Red += ((float) p->r) * KERNEL->matrix[a][b];
                    c.Green += ((float) p->g) * KERNEL->matrix[a][b];
                    c.Blue += ((float) p->b) * KERNEL->matrix[a][b];
                }
            }
            c.Red *= KERNEL->constant;
            c.Blue *= KERNEL->constant;
            c.Green *= KERNEL->constant;
            Pixel* dest = &new_i->pixel_data[y][x];
            dest->r = (uint8_t)(c.Red <= 0 ? 0 : c.Red >= 255 ? 255 : c.Red);
            dest->g = (uint8_t)(c.Green <= 0 ? 0 : c.Green >= 255 ? 255 : c.Green);
            dest->b = (uint8_t)(c.Blue <= 0 ? 0 : c.Blue >= 255 ? 255 : c.Blue);
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
        const int current_count = stack->count;
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

void *produce_image_transformation(void *shared_state) {
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
            char file_path[NAME_BUFFER_SIZE];
            sprintf(file_path, "%s/%s", settings->source_folder, state->list_image_files[index_file]);
            Image img = open_bitmap(file_path);
            Image new_i;
            apply_effect(&img, &new_i, state->settings->effect);
            stack->stack[stack->count].image = new_i;
            sprintf(stack->stack[stack->count].name, "%s", state->list_image_files[index_file]);
            stack->count++;
            pthread_mutex_unlock(&stack->lock);
            index_file++;
        } else {
            stack->thread_remaining_at_work--;
            pthread_mutex_unlock(&stack->lock);
            pthread_cond_signal(&stack->can_save_on_disk);
            return 0;
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
        pthread_create(&producer_threads[i], &attr, produce_image_transformation, thread_state);
    }
}

int main(int argc, char** argv) {
    Settings settings;
    Stack stack;
    State state;
    pthread_t consumer_thread;
    pthread_t *producer_threads;

    EDGE_KERNEL = init_edge_detect();
    BOX_BLUR_KERNEL = init_box_blur();
    SHARPEN_KERNEL = init_sharpen();
    int code = set_settings(argc, argv, &settings);
    if (code != 0)
        return code;
    print_settings(&settings);
    init_stack(&stack, &settings);
    state.stack = &stack;
    state.settings = &settings;
    producer_threads = malloc(sizeof(pthread_t) * settings.number_of_threads);
    code = empty_out(settings.destination_folder);
    if (code == -1) {
        return -1;
    }
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