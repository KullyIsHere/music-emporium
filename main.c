#include <conio.h>
#include <windows.h>
#include "music.h"
#include "audio.h"

int main() {
    //Used to store player inputs
    char input;
    //Used to check whether the program should still be running
    //Once it becomes 0 the program shuts
    int is_running = 1;
    //Array of structs, stores data
    //Static so it lives in safe storage instead of the stack
    //(99 of these structs are ~820KB) and starts zeroed out
    static MusicFile music[99];
    //Current data values initialized here
    current_data current;
    memset(&current, 0, sizeof(current));
    current.scene = 0;
    current.page = 1;
    current.file_sum = 0;
    current.scroll = 0;
    current.x = 0;
    current.y = 0;
    current.is_editing = 0;
    current.file_index = 0;
    //Temporary file used for appending ".me"
    char temp_file[105];

    //Sets up the audio system so tones can play together
    audio_init();

    while(is_running == 1) {

        //Updates upon player input
        draw(current,music[current.file_index]);

        input = _getch();  // instant key press

        switch(input) {
            case 'n':
                //printf("Opening new music file");
                if (current.scene == 0) {
                    //Only continues if the file was created successfully
                    if (create_new_file(current.file) == 0) {
                        sprintf(temp_file, "%s.me", current.file);
                        current.x = 0;
                        current.y = 0;
                        current.scene = 0; // Should set current scene to 1
                    }
                }
                break;

            case 'o':

                //printf("Opening old files");
                if (current.scene == 0) {
                    //Starts browsing from the program's folder
                    _fullpath(current.dir, ".", sizeof(current.dir));
                    list_directory(&current);
                    current.page = 1;
                    current.scroll = 0;
                    current.scene = 2;
                }
                break;

            case 'b':
                //printf("Going back");
                if (current.scene == 1) {
                    current.scene = 0;
                }
                else if (current.scene == 2) {
                    //Goes up a folder, or back to the menu at the root
                    if (!go_up_directory(&current)) {
                        current.scene = 0;
                    }
                }
                break;

            case 'q':
                if (current.scene == 0) {
                    is_running = 0;
                    printf("\n\nEnding software");
                }
                break;
            case 'w':
                if (current.scene == 1 && current.is_editing == 0) {
                    //Edit file code
                    current.y -= 1;

                    if (current.y < 0) { current.y = 0;}

                }
                else if (current.scene == 2) {
                    //Select file code
                    //printf("Here: %d",current.scroll);
                    current.scroll -= 1;
                    if (current.scroll < 0) {
                        int remainder = current.file_sum % 5;
                        if (current.page == Calculate_page_num(current) && remainder != 0) {

                        current.scroll = remainder - 1;
                        }
                        else {current.scroll = 4;}
                    }


                }
                break;
            case 's':
                if (current.scene == 1 && current.is_editing == 0) {
                    //Edit file code
                    current.y += 1;

                    if (current.y > (music[current.file_index].rows - 1)) {current.y = (music[current.file_index].rows - 1);}

                }
                else if (current.scene == 2) {
                    //Select file code
                    current.scroll += 1;
                    if (current.scroll > 4) {
                        current.scroll = 0;
                    }
                    if (current.page == Calculate_page_num(current)) {
                        int remainder = current.file_sum % 5;
                        if (current.scroll >= remainder && remainder != 0) {
                        current.scroll = 0;
                    }
                    }
                }
                break;
            case 'a':
                if (current.scene == 1 && current.is_editing == 0) {
                    //Edit file code
                    current.x -= 1;

                    if (current.x < 0) {current.x = 0;}
                }
                else if (current.scene == 2) {
                    current.page -= 1;
                    if (current.page < 1) {current.page = 1;}
                }
                break;
            case 'd':
                if (current.scene == 1 && current.is_editing == 0) {
                    //Edit file code
                    current.x += 1;
                    if (current.x > (music[current.file_index].columns - 1)) {current.x = (music[current.file_index].columns - 1);}
                }
                else if (current.scene == 2) {
                    int page_num = Calculate_page_num(current);
                    current.page += 1;
                    if (current.page > page_num) {current.page = page_num;}

                    //Makes it so that if we go next page and scroll is greater than remainder
                    if (current.page == page_num) {
                        int remainder = current.file_sum % 5;
                        if (current.scroll > (remainder-1)) {
                        current.scroll = remainder - 1;
                    }
                }}
                break;
            case 'e':
                if (current.scene == 1){
                    current.is_editing = 1;
                    //Enters frequency than duration
                    //music_data[x][y][0] is frequency music_data[x][y][1] is duration

                    output_frequency_table();

                    music[current.file_index].music_data[current.x][current.y][0] = enter_value("frequency",2500,200);
                    music[current.file_index].music_data[current.x][current.y][1] = enter_value("duration",1000,10);

                    save_music_file(&music[current.file_index]);
                    current.is_editing = 0;
                }
                else if (current.scene == 2){
                    current.file_index = ((current.page - 1)*5) +  current.scroll;
                    //Only act if there is actually something selected
                    if (current.file_sum > 0 && strcmp(current.file_list[current.file_index], "") != 0) {
                        if (current.is_dir[current.file_index]) {
                            //Opens the folder and lists what's inside
                            _fullpath(current.dir, current.file_list[current.file_index], sizeof(current.dir));
                            list_directory(&current);
                            current.page = 1;
                            current.scroll = 0;
                        }
                        else {
                            //Keeps only the .me files so they can be switched with [ and ]
                            printf("SELECTED A FILE");
                            int me_count = 0;
                            int selected_slot = 0;
                            for (int i = 0; i < current.file_sum; i++) {
                                if (current.is_dir[i]) continue;
                                if (i == current.file_index) selected_slot = me_count;
                                strcpy(current.file_list[me_count], current.file_list[i]);
                                me_count++;
                            }
                            current.file_sum = me_count;
                            current.file_index = selected_slot;
                            strcpy(current.file,(current.file_list[current.file_index]));
                            load_music_file(music, current);
                            current.x = 0;
                            current.y = 0;
                            current.scene = 1;
                        }
                    }
                }
                break;
            case 'p':
                if (current.scene == 1) {
                    printf("\nPlaying...");
                    //Code for playing
                    //Each column is played as a chord, every note sounding at the same time
                    //and then we move onto the next column once intervals are done.

                    for (int x = 0; x < music[current.file_index].columns; x++) {
                        printf("\nPlaying Column %d",x);
                        int max_duration = 0;
                        for (int y = 0; y < music[current.file_index].rows; y++) {
                            if (music[current.file_index].music_data[x][y][0] > 0 && music[current.file_index].music_data[x][y][1] > 0) {
                                play_tone(music[current.file_index].music_data[x][y][0],music[current.file_index].music_data[x][y][1]);
                                if (music[current.file_index].music_data[x][y][1] > max_duration) {
                                    max_duration = music[current.file_index].music_data[x][y][1];
                                }
                            }
                        }
                        //Waits for the longest note plus the interval before the next column
                        Sleep(max_duration + music[current.file_index].Intervals[x]);
                    }

                }
                break;
            case '[':
                if (current.scene == 1) {
                    current.file_index -= 1;
                    if (current.file_index < 0) {
                        current.file_index = 0;
                    }
                    strcpy(current.file, current.file_list[current.file_index]);
                }


                break;
            case ']':
                if (current.scene == 1) {
                    current.file_index += 1;
                    if (current.file_index > current.file_sum - 1) {
                        current.file_index = current.file_sum -1;
                    }
                    strcpy(current.file, current.file_list[current.file_index]);
                }


                break;
        }
    }

    //Releases the audio devices before closing
    audio_close();

    return 0;
}
