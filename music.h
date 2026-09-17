#ifndef MUSIC_H
#define MUSIC_H

/*
Core header - only portable headers live here so the GTK GUI can use it too.
Files that need platform APIs include their own headers:
conio.h (console input), windows.h/mmsystem.h (drawing, audio), dirent.h (browsing).
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
Author: Abdul-maliq Adegbite
Desc: This is a custom music software it makes it's own unique ".me" files,
these files store the custom music that players can create.
*/

//Stores file format data
typedef struct {
    char magic[4];        // file identifier
    char filename[100];   // Original file name
    int music_data[100][10][2];   //Stores all data on notes, basically frequency and duration
    int Intervals[100];      //Determines how long to wait to play the next column, by default 20ms
    int columns;          // Number of columns (x value)
    int rows;             // Number of rows (y value)
} MusicFile;

//Stores in use user data
typedef struct {
    /*
    Current scene is used to select which program to open
    0 - Main menu
    1 - music file
    2 - Select file
    */
    int scene;
    char file[512];      //Stores the file we're currently using
    int file_index;      //Gets the position from within the music file
    int page;           //used to store page data for draw select file
    int scroll;         // stores scroll data
    int file_sum;       //Used to store number of files
    char file_list[99][512]; //Stores all folders and me files (full paths)
    int is_dir[99];     //Whether each entry in file_list is a folder
    char dir[512];      //The folder currently being browsed
    int x;              //Stores location data for editing music files
    int y;
    int is_editing; //Checks if code is being edited
} current_data;

// function declaration
void draw_main_menu();
void draw_music_file(MusicFile music, current_data current);
int enter_value(char type[], int max_range, int min_range);
int create_new_file(char *current_file);
void draw_select_file(current_data current);
void draw(current_data current,MusicFile music);
void save_music_file(MusicFile *music);
int make_music_file(MusicFile *music, const char *name, int columns, int rows);
int load_music_file(MusicFile *music, current_data current);
int load_music_file_path(MusicFile *music, const char *path);
int Calculate_page_num(current_data current);
void output_frequency_table();
void list_directory(current_data *current);
int go_up_directory(current_data *current);
char *name_only(char *path);

#endif
