#include <windows.h>
#include "music.h"

//This draws the main menu, allows players to move to different parts.
void draw_main_menu(){
    //Draws title screen
    //Handle code is used to change text output colour.
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    SetConsoleTextAttribute(hConsole, 9);
    printf(" __  __           _      \n");
    printf("|  \\/  |_   _ ___(_) ___ \n");
    printf("| |\\/| | | | / __| |/ __|\n");
    printf("| |  | | |_| \\__ \\ | (__ \n");
    printf("|_|  |_|\\__,_|___/_|\\___|\n\n");

    printf(" _____                            _                 \n");
    printf("| ____|_ __ ___  _ __   ___  _ __(_)_   _ _ __ ___  \n");
    printf("|  _| | '_ ` _ \| '_ \ / _ \| '__| | | | | '_ ` _ \ \n");
    printf("| |___| | | | | | |_) | (_) | |  | | |_| | | | | | |\n");
    printf("|_____|_| |_| |_| .__/ \___/|_|  |_|\__,_|_| |_| |_|\n");
    printf("                |_|                                 \n");

    SetConsoleTextAttribute(hConsole, 7);
    //Instructions are shown at the bottom
    printf("\n  N - New file      O - Open file       Q - Quit\n\n");
}

//This draws the current music file
void draw_music_file(MusicFile music, current_data current){
    //Clamps values so bad data can never read outside the arrays
    int rows = music.rows;
    if (rows < 0 || rows > 10) rows = 0;
    int columns = music.columns;
    if (columns < 0 || columns > 100) columns = 0;

    printf("\nDrawing a music file");
    printf("\n%s.me\n",music.filename);
    printf("Rows: %d\n", rows);
    printf("Columns: %d\n", columns);

    //outputs 2d array
    //This will represent player editing
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < columns; x++) {
            if (x == current.x && y == current.y){
                printf(" P ");
            }
            else if (music.music_data[x][y][0] > 0 && music.music_data[x][y][1] > 0) {
                printf(" O ");
            }
            else {printf(" . ");}
        }
    printf("\n");
    }

    //Data outputs
    printf("Selected square, row: %d column: %d",current.y,current.x);
    printf("\nFrequency: %d Duration: %d",music.music_data[current.x][current.y][0],music.music_data[current.x][current.y][1]);
    //Info outputs
    printf("\n\nW - Up S - Down \nA - left D - right");
    printf("\nE - Edit");
    printf("\nP - play/pause");
    printf("\n[ - Previous file ] - Next file");

    printf("\n\nB - Back");
    ;
}

//Draws the files you can select
void draw_select_file(current_data current) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    printf("\nFolder: %s\n\n", current.dir);
    printf("Files to select:\n");
    int page_num = Calculate_page_num(current);
    char selected_file[512] = "";
    //boundary checking for current.page
    int start = (current.page - 1) * 5;
    int end = current.page * 5;

    if (end > current.file_sum)
    end = current.file_sum;

    if (current.file_sum == 0) {
        printf("\nNo folders or .me files here.\n");
    }

    //This lists out everything for the current page.
    for (int i = start; i < end; i++) {
        if (strcmp(current.file_list[i], "") == 0) continue;
        //Shows a folder or .me marker before the name
        if ((i % 5) == current.scroll) {
                //Shows selected file
                SetConsoleTextAttribute(hConsole, 9);
                printf("\n[%s] %s", current.is_dir[i] ? "FOLDER" : ".me", name_only(current.file_list[i]));
                printf(" - Selected");
                SetConsoleTextAttribute(hConsole, 7);
                strcpy(selected_file, current.file_list[i]);
        }
        else {
            printf("\n[%s] %s", current.is_dir[i] ? "FOLDER" : ".me", name_only(current.file_list[i]));
        }
    }

    printf("\n\nShowing pages %d of %d",current.page,page_num);
    SetConsoleTextAttribute(hConsole, 9);
    printf("\nSelected: %s",selected_file);
    SetConsoleTextAttribute(hConsole, 2);
    printf("\n\nW - Up S - Down \nA - Previous page D - Next Page");
    SetConsoleTextAttribute(hConsole, 11);
    printf("\n\nE - Open folder / Select file\n");
    SetConsoleTextAttribute(hConsole, 7);
    printf("\nB - Go up / Back");
}

//Selects what to output based on the current scene.
void draw(current_data current,MusicFile music) {
    system("cls");

    //printf("%s",current_file);

    switch(current.scene) {
    case 0:
        draw_main_menu();
        break;
    case 1:
        draw_music_file(music,current);
        break;
    case 2:
        draw_select_file(current);
        break;
    }
}
