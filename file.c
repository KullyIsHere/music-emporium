#include "music.h"

//Fills a new MusicFile struct with default values.
//Returns 0 on success, 1 if the columns/rows are out of range.
int make_music_file(MusicFile *music, const char *name, int columns, int rows) {
    if (columns < 10 || columns > 100 || rows < 1 || rows > 10) {
        return 1;
    }

    memset(music, 0, sizeof(MusicFile));
    strncpy(music->filename, name, 99);
    music->filename[99] = '\0';
    music->columns = columns;
    music->rows = rows;

    memcpy(music->magic, "MEF", 4); // identifier for your format

    //Initialize intervals
    for (int i = 0; i < 100; i++) {
        music->Intervals[i] = 400; //Uses 400ms as a base for each interval
    }

    return 0;
}

//This function initializes data for the new music file
//And then saves it as an accessible binary format
//This means it's a completely unique file format. ".me" file.
void save_music_file(MusicFile *music) {
    //The temp file is used to set the file format
    //It saves it as a ".me" file.
    char temp_file[105];
    sprintf(temp_file, "%s.me", music->filename);

    //A new file is created and validated
    FILE *file = fopen(temp_file, "wb");
    if (file == NULL) {
        printf("\nError creating file.");
        return;
    }
    //The data is then written and closed
    size_t written = fwrite(music, sizeof(MusicFile), 1, file);
    if (written != 1) {
        printf("\nError writing to file.");
    }

    fclose(file);
    printf("\nFile saved as %s", music->filename);
}

//This loads the saved music data
int load_music_file(MusicFile *music, current_data current) {
    //Files loading, validates

    for (int i = 0; i < current.file_sum; i++) {
        FILE *file = fopen(current.file_list[i], "rb");
        if (file == NULL) {
            printf("\nError opening %s.", current.file_list[i]);
            //Zeroes the slot so it's safe to keep going
            memset(&music[i], 0, sizeof(MusicFile));
            continue;
        }

        //Saves data to music struct.
        size_t read = fread(&music[i], sizeof(MusicFile), 1, file);
        if (read != 1) {
            printf("\nError reading %s.", current.file_list[i]);
            memset(&music[i], 0, sizeof(MusicFile));
        }

        fclose(file);
    }


    return 1;
}

//Loads exactly one .me file for the graphical editor.
//Unlike the legacy multi-file loader, this reports malformed and partial files.
int load_music_file_path(MusicFile *music, const char *path) {
    if (music == NULL || path == NULL || path[0] == '\0')
        return 0;

    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return 0;

    MusicFile loaded;
    memset(&loaded, 0, sizeof(loaded));
    size_t read = fread(&loaded, sizeof(loaded), 1, file);
    int has_extra_data = fgetc(file) != EOF;
    fclose(file);

    if (read != 1 || has_extra_data || memcmp(loaded.magic, "MEF", 3) != 0 ||
        loaded.columns < 10 || loaded.columns > 100 ||
        loaded.rows < 1 || loaded.rows > 10) {
        return 0;
    }

    *music = loaded;
    return 1;
}
