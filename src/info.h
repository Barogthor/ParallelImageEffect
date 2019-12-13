

#ifndef PROJET_PARALLELE_INFO_H
#define PROJET_PARALLELE_INFO_H

#include <pthread.h>
#include "bitmap.h"

#define IMAGE_STACK_SIZE 12
#define DIM 3
#define LENGHT DIM
#define OFFSET DIM /2
#define MAX_FILE_INPUT 200


typedef struct Color_t {
    float Red;
    float Green;
    float Blue;
} Color_e;

enum ImageEffect {
    BOX_BLUR,
    EDGE_DETECT,
    SHARPEN
};

typedef struct setting_t {
    char *source_folder;
    char *destination_folder;
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
    char **list_image_files;
    int start;
    int end;
    int thread_id;
} State;

void clone_state(const State *state_to_copy, State *new_state);

int exit_with_error(char *msg);

void set_default_settings(Settings *settings);

void print_settings(Settings const *settings);

void print_help();

int set_settings(int const argc, char **argv, Settings *settings);

void init_stack(Stack *stack);

int list_dir(const char *path, State *state);

#endif //PROJET_PARALLÈLE_INFO_H
