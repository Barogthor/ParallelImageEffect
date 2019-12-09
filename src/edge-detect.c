/*
	//gcc edge-detect.c bitmap.c -O2 -ftree-vectorize -fopt-info -mavx2 -fopt-info-vec-all
	//UTILISER UNIQUEMENT DES BMP 24bits
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "bitmap.h"
#include <stdint.h>
#include <string.h>

#define IMAGE_STACK_SIZE 12
#define DIM 3
#define LENGHT DIM
#define OFFSET DIM /2

const float KERNEL[DIM][DIM] = {{-1,-1,-1},
                                {-1, 8,-1},
                                {-1,-1,-1}};
const float SHARPEN_KERNEL[DIM][DIM] = {{ 0,-1, 0},
                                        {-1, 5,-1},
                                        { 0,-1, 0}};

const float BOX_BLUR_KERNEL[DIM][DIM] = {{1/9,1/9,1/9},
                                         {1/9,1/9,1/9},
                                         {1/9,1/9,1/9}};

typedef struct Color_t {
    float Red;
    float Green;
    float Blue;
} Color_e;

enum ImageEffect{
    BOX_BLUR,
    EDGE_DETECT,
    SHARPEN
};

typedef struct setting_t{
    char* source_folder;
    char* destination_folder;
    int number_of_threads;
    enum ImageEffect effect;
} Settings;

typedef struct stack_t {
    Image stack[IMAGE_STACK_SIZE];
    int count;
    int max;
    pthread_mutex_t lock;
    pthread_cond_t can_save_on_disk;
    pthread_cond_t can_transform_image;
} Stack;

typedef struct state_t {
    const Settings *settings;
    Stack *stack;
} State;

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

int exit_with_error(char* msg){
    perror(msg);
    return -1;
}

void set_default_settings(Settings *settings){
    settings->source_folder = "in";
    settings->destination_folder = "out";
    settings->number_of_threads = 1;
    settings->effect = EDGE_DETECT;
}

void print_settings(Settings const* settings){
    printf("SOURCE              : %s\n",settings->source_folder);
    printf("DESTINATION         : %s\n",settings->destination_folder);
    printf("NUMBER OF CONSUMERS : %d\n",settings->number_of_threads);
    switch(settings->effect){
        case BOX_BLUR:
            printf("EFFECT              : Box blurring\n");
            break;
        case EDGE_DETECT:
            printf("EFFECT              : Edge Detection\n");
            break;
        case SHARPEN:
            printf("EFFECT              : Sharpen\n");
            break;
    }
}

void print_help(){
    printf("-s or --source <a_string_source>           : Argument to customize source folder (./in as default)\n");
    printf("-d or --destination <a_string_destination> : Argument to customize destination folder (./out as default)\n");
    printf("-t or --threads <an_int>                   : Argument to customize number of productors thread working [1;16] (1 as default)\n");
    printf("-e or --effect <a_string_effect>           : Argument to customize image effect <boxblur|sharpen|edgedetect> (edgedetect as default)\n");
}

int set_settings(int const argc, char** argv, Settings *settings){
    int i = 1;
    set_default_settings(settings);
    while(i < argc){
        char* current_argument = argv[i];

        if(strcmp(current_argument, "-h") == 0 || strcmp(current_argument, "--help") == 0){
            print_help();
            return 1;
        }
        else if(strcmp(current_argument, "-s") == 0 || strcmp(current_argument, "--source") == 0){
            if(argc<=++i) return exit_with_error("Missing argument for source");
            if(strcmp(argv[i], "") == 0 ) return exit_with_error("source folder value can't be empty");
            settings->source_folder = argv[i];
        }
        else if(strcmp(current_argument, "-d") == 0 || strcmp(current_argument, "--destination") == 0){
            if(argc<=++i) return exit_with_error("Missing argument for destination");
            if(strcmp(argv[i], "") == 0 ) return exit_with_error("destination folder value can't be empty");
            settings->destination_folder = argv[i];
        }
        else if(strcmp(current_argument, "-t") == 0 || strcmp(current_argument, "--threads") == 0){
            if(argc<=++i) return exit_with_error("Missing argument for number of thread consumer");
            int number_of_threads = atoi(argv[i]);
            if(number_of_threads<1 || number_of_threads > 16 ) return exit_with_error("Invalid value for number of thread consumer. Should be [1;16]");
            settings->number_of_threads = number_of_threads;
        }
        else if(strcmp(current_argument, "-e") == 0 || strcmp(current_argument, "--effect") == 0){
            if(argc<=++i) return exit_with_error("Missing argument for image effect");
            char* effect = argv[i];
            if(strcmp(effect, "") == 0 ) return exit_with_error("image effect can't be empty");
            else if(strcmp(effect, "boxblur")==0){
                settings->effect = BOX_BLUR;
            }
            else if(strcmp(effect, "edgedetect")==0){
                settings->effect = EDGE_DETECT;
            }
            else if(strcmp(effect, "sharpen")==0){
                settings->effect = SHARPEN;
            }
            else{
                return exit_with_error("Unknown image effect");
            }
        }
        i++;
    }
    return 0;
}

void init_stack(Stack *stack) {
    stack->count = 0;
    stack->max = IMAGE_STACK_SIZE;
    pthread_cond_init(&stack->can_transform_image, NULL);
    pthread_cond_init(&stack->can_save_on_disk, NULL);
    pthread_mutex_init(&stack->lock, NULL);
}

int main(int argc, char** argv) {
    Settings settings;
    Stack stack;
    State state;

    int code = set_settings(argc, argv, &settings);
    if(code != 0) return code;
    print_settings(&settings);
    init_stack(&stack);
    state.stack = &stack;
    state.settings = &settings;

    Image img = open_bitmap("in/bmp_tank.bmp");
    Image new_i;
    apply_effect(&img, &new_i, settings.effect);
    save_bitmap(new_i, "out/test_out.bmp");
    return 0;
}