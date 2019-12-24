#include "info.h"
#include <dirent.h>

int exit_with_error(char *msg) {
    perror(msg);
    return -1;
}

void set_default_settings(Settings *settings) {
    settings->source_folder = "in";
    settings->destination_folder = "out";
    settings->number_of_threads = 1;
    settings->effect = EDGE_DETECT;
}

void print_settings(Settings const *settings) {
    printf("SOURCE              : %s\n", settings->source_folder);
    printf("DESTINATION         : %s\n", settings->destination_folder);
    printf("NUMBER OF CONSUMERS : %d\n", settings->number_of_threads);
    switch (settings->effect) {
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

void print_help() {
    printf("-s or --source <a_string_source>           : Argument to customize source folder (./in as default)\n");
    printf("-d or --destination <a_string_destination> : Argument to customize destination folder (./out as default)\n");
    printf("-t or --threads <an_int>                   : Argument to customize number of productors thread working [1;16] (1 as default)\n");
    printf("-e or --effect <a_string_effect>           : Argument to customize image effect <boxblur|sharpen|edgedetect> (edgedetect as default)\n");
}

int set_settings(int const argc, char **argv, Settings *settings) {
    int i = 1;
    set_default_settings(settings);
    while (i < argc) {
        char *current_argument = argv[i];

        if (strcmp(current_argument, "-h") == 0 || strcmp(current_argument, "--help") == 0) {
            print_help();
            return 1;
        } else if (strcmp(current_argument, "-s") == 0 || strcmp(current_argument, "--source") == 0) {
            if (argc <= ++i) return exit_with_error("Missing argument for source");
            if (strcmp(argv[i], "") == 0) return exit_with_error("source folder value can't be empty");
            settings->source_folder = argv[i];
        } else if (strcmp(current_argument, "-d") == 0 || strcmp(current_argument, "--destination") == 0) {
            if (argc <= ++i) return exit_with_error("Missing argument for destination");
            if (strcmp(argv[i], "") == 0) return exit_with_error("destination folder value can't be empty");
            settings->destination_folder = argv[i];
        } else if (strcmp(current_argument, "-t") == 0 || strcmp(current_argument, "--threads") == 0) {
            if (argc <= ++i) return exit_with_error("Missing argument for number of thread consumer");
            int number_of_threads = atoi(argv[i]);
            if (number_of_threads < 1 || number_of_threads > 16)
                return exit_with_error("Invalid value for number of thread consumer. Should be [1;16]");
            settings->number_of_threads = number_of_threads;
        } else if (strcmp(current_argument, "-e") == 0 || strcmp(current_argument, "--effect") == 0) {
            if (argc <= ++i) return exit_with_error("Missing argument for image effect");
            char *effect = argv[i];
            if (strcmp(effect, "") == 0) return exit_with_error("image effect can't be empty");
            else if (strcmp(effect, "boxblur") == 0) {
                settings->effect = BOX_BLUR;
            } else if (strcmp(effect, "edgedetect") == 0) {
                settings->effect = EDGE_DETECT;
            } else if (strcmp(effect, "sharpen") == 0) {
                settings->effect = SHARPEN;
            } else {
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

void clone_state(const State *state_to_copy, State *new_state) {
    new_state->settings = state_to_copy->settings;
    new_state->stack = state_to_copy->stack;
    new_state->list_image_files = state_to_copy->list_image_files;
}

int list_dir(const char *path, State *state) {
    struct dirent *entry;
    DIR *dp;

    dp = opendir(path);
    if (dp == NULL) {
        perror("Error opening directory");
        return -1;
    }
    int i = 0, number_of_files = 0;
    while ((entry = readdir(dp)))
        number_of_files++;
    rewinddir(dp);
    state->list_image_files = (char **) malloc(number_of_files * sizeof(char *));
    while ((entry = readdir(dp))) {
        char *current_file_name = entry->d_name;
        int length = strlen(current_file_name);
        if (strcmp(current_file_name, ".") != 0 && strcmp(current_file_name, "..") != 0) {
            state->list_image_files[i] = (char *) malloc(length * sizeof(char) + sizeof(char));
            strncpy(state->list_image_files[i], current_file_name, length);
            state->list_image_files[i][length] = '\0';
            i++;

        }
    }
    closedir(dp);
    return i;
}