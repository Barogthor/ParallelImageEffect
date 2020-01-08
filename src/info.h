

#ifndef PROJET_PARALLELE_INFO_H
#define PROJET_PARALLELE_INFO_H

#include <pthread.h>
#include "bitmap.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define IMAGE_STACK_SIZE 12
#define DIM 3
#define LENGHT DIM
#define OFFSET DIM /2
#define MAX_FILE_INPUT 200
#define NAME_BUFFER_SIZE 500

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

typedef struct Image_Name_tuple_t {
    char name[NAME_BUFFER_SIZE];
    Image image;
} ImageNameTuple;

typedef struct setting_t {
    char *source_folder;
    char *destination_folder;
    int number_of_threads;
    enum ImageEffect effect;
} Settings;

typedef struct file_t {
    ImageNameTuple file[IMAGE_STACK_SIZE];
    int head;
    int tail;
    int max;
    int thread_remaining_at_work;
    pthread_mutex_t lock;
    pthread_cond_t can_save_on_disk;
    pthread_cond_t can_transform_image;
} File;

int is_filled(const File* file);

int is_empty(const File* file);

const ImageNameTuple* peek(const File* file);

void push(File* file, ImageNameTuple *item);

ImageNameTuple* pop(File* file);

typedef struct stack_t {
    ImageNameTuple stack[IMAGE_STACK_SIZE];
    int count;
    int max;
    int thread_remaining_at_work;
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

void init_stack(Stack *stack, const Settings *settings);

int list_dir(const char *path, State *state);

int empty_out(const char* path);
#endif //PROJET_PARALLÈLE_INFO_H
