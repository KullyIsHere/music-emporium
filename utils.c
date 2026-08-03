#include <conio.h>
#include <dirent.h>
#include <sys/stat.h>
#include "music.h"

//This is a function for entering integer values
int enter_value(char type[], int max_range, int min_range) {
    //Only allows within the range 1-10
    int num = -1;
    //Basic validation
    while (num > max_range || num < min_range) {
        printf("Enter the number of %s ",type);
        printf("between %d and %d: ",min_range,max_range);
        //Grabs values by reference, checking it was really a number
        if (scanf("%d", &num) != 1) {
            //Clears the bad input so the program doesn't get stuck in a loop
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {}
            num = -1;
            printf("\nPlease enter a number.\n");
            continue;
        }
        //Fail states
        if (num > max_range) {
            printf("\nPlease enter a number less than or equal to %d.\n",max_range);
        }
        else if (num < min_range) {
            printf("\nPlease enter a number greater than %d.\n",min_range);
        }
    }
    return num;
}

//This creates a new file via user input
int create_new_file(char *current_file){
    MusicFile music;
    char name[100] = "";

    printf("\nPlease name your new file: ");
    if (scanf("%99s", name) != 1 || name[0] == '\0') {
        printf("\nError reading the file name.\n");
        return 1;
    }

    strcpy(current_file, name);

    int columns = enter_value("columns",100,10);
    int rows = enter_value("rows",10,1);

    if (make_music_file(&music, name, columns, rows) != 0) {
        printf("\nInvalid columns or rows.\n");
        return 1;
    }

    //Code for saving, it can also make a new file.
    save_music_file(&music);

    printf("\n\nPress any key to continue: ");
    _getch();
    return 0;
}

//Calculates amount of page numbers system should have.
int Calculate_page_num(current_data current) {
    int page_num = current.file_sum / 5;
    if ((current.file_sum % 5) != 0) {
        page_num ++;
    }
    return page_num;
}

//Returns just the file/folder name from a full path.
char *name_only(char *path) {
    char *last = path;
    for (char *p = path; *p != '\0'; p++) {
        if (*p == '\\' || *p == '/') last = p + 1;
    }
    return last;
}

//Lists the folders and .me files inside current.dir.
//Folders are listed first, then .me files.
void list_directory(current_data *current) {
    current->file_sum = 0;

    DIR *folder = opendir(current->dir);
    if (folder == NULL) return;

    struct dirent *entry;
    //First pass: add every folder (except . and ..)
    while ((entry = readdir(folder)) != NULL && current->file_sum < 99) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s\\%s", current->dir, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0 && (st.st_mode & S_IFDIR)) {
            strcpy(current->file_list[current->file_sum], full_path);
            current->is_dir[current->file_sum] = 1;
            current->file_sum++;
        }
    }
    closedir(folder);

    //Second pass: add every .me file
    folder = opendir(current->dir);
    if (folder == NULL) return;

    while ((entry = readdir(folder)) != NULL && current->file_sum < 99) {
        if (strstr(entry->d_name, ".me") == NULL) continue;

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s\\%s", current->dir, entry->d_name);

        strcpy(current->file_list[current->file_sum], full_path);
        current->is_dir[current->file_sum] = 0;
        current->file_sum++;
    }
    closedir(folder);
}

//Moves the browser up one folder. Returns 1 if it moved, 0 if already at the root.
int go_up_directory(current_data *current) {
    //A root folder like C:\ ends with a backslash
    int len = (int)strlen(current->dir);
    if (len > 0 && current->dir[len - 1] == '\\') return 0;

    char parent[512];
    snprintf(parent, sizeof(parent), "%s\\..", current->dir);
    _fullpath(current->dir, parent, sizeof(current->dir));

    list_directory(current);
    current->page = 1;
    current->scroll = 0;
    return 1;
}

//Just some code for outputting useful data.
void output_frequency_table() {
    char *notes[] = {"C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5"};
    int frequencies[] = {261, 293, 329, 349, 392, 440, 494, 523};

    int count = sizeof(frequencies) / sizeof(frequencies[0]);

    printf("\n\nNote  Frequency (Hz)\n");

    for(int i = 0; i < count; i++) {
        printf("%-4s  %d\n", notes[i], frequencies[i]);
    }
}
