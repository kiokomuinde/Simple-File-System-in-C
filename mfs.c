/*
 * Name: Kioko Muinde
 * Description: Fat32 file system reader
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdint.h>
#include <ctype.h>

char user_input[300]; // user input buffer
char *base_command; // base command (separated to help with finding path)
char *args[20]; // array of pointers to strings holding arguments
int counter, counter2; // counters
char *arg; // buffer to hold current argument being processed while memory allocation in progress
char *paths[4]; // array of pointers to paths 
int opened = 0; // boolean to see if a file is already open
FILE *fp; // file pointer to the fat32
FILE *temp_fp; //file pointer for calls to read

//The actual fat32 variables
char BS_OEMName[8]; // the name of the OEM
int16_t BPB_BytesPerSec; // bytes per sector
int8_t BPB_SecPerClus; // sectors per cluster
int16_t BPB_RsvdSecCnt; // number of reserved sectors in reserved region
int8_t BPB_NumFATs; // number of FAT data structures (should be 2)
int16_t BPB_RootEntCnt; // number of 32 byte directories in the root (should be 0)
char BS_VolLab[11]; // label of the volume
int32_t BPB_FATSz32; // number of sectors contained in one FAT
int32_t BPB_RootClus; // the number of the first cluster of the root directory

int32_t RootDirClusterAddr = 0; // offset location of the root directory
int32_t CurrentDirClusterAddr = 0; // offset location of the directory you are currently in

//struct to hold all of the entry data
struct DirectoryEntry {
    char DIR_Name[12];
    uint8_t DIR_Attr;
    uint8_t Unused1[8];
    uint16_t DIR_FirstClusterHigh;
    uint8_t Unused2[4];
    uint16_t DIR_FirstClusterLow;
    uint32_t DIR_FileSize;
};

struct DirectoryEntry dir[16]; // holds the directory entries for the current directory

/* 
 * Function: get_user_input
 * Parameters: None
 * Returns: Nothing
 * Description: Gets input from the user and removes the
 * trailing newline character
 */
void get_user_input() {
    // get user input from standard in
    fgets(user_input, 200, stdin );
    // remove new line char unless the command is empty
    if(strlen(user_input) != 1)
        user_input[strcspn(user_input, "\n")] = 0;
}

/* 
 * Function: cntl_c_handler
 * Parameters: int signal - not completely needed but I would
 * to add onto this later on
 * Returns: Nothing
 * Description: Handles signal event where the user pressed control-c
 * and directs them to use exit or quit to leave the msh
 */
void cntl_c_handler(int signal) {
    // explain proper exit procedure to user
    printf("\nPlease user \"exit\" or \"quit\" to exit the mav shell\nmsh>");
    fflush(stdout);
}

/* 
 * Function: cntl_z_handler
 * Parameters: int signal - not completely needed but I would
 * to add onto this later on
 * Returns: Nothing
 * Description: Handles signal event where the user pressed control-z
 * and directs them to use exit or quit to leave the msh
 */
void cntl_z_handler(int signal) {
    // explain proper exit procedure to user
    printf("\nPlease user \"exit\" or \"quit\" to exit the mav shell\nmsh>");
    fflush(stdout);
}

/* Function: make_name
 * Parameter: unedited name of the directory
 * returns: nothing
 * Description: takes in the unedited directory address
 * and edits it to be uppercase with spaces
 */
void make_name(char* dir_name){
    int whitespace = 11 - strlen(dir_name);
    //uppercase everything
    for(counter = 0; counter < 11; counter ++){
        dir_name[counter] = toupper(dir_name[counter]);
    }
    //add whitespace
    for(counter = 0; counter < whitespace; counter ++){
        strcat(dir_name, " ");
    }
}

/*
 * Function: make_file
 * Parameter: unedited name of the file
 * returns: nothing
 * Description: takes in unedited name of the file and 
 * converts it into proper fat32 format
 */
void make_file(char* file_name){
    char * token; //token used for chopping the file
    char result[15]; // string to hold all the concats
    int whitespace = 8; // counter to calculate whitespace
    //grab the first part of the filename
    token = strtok(file_name, ".");
    if(strlen(token) > 8 || token == NULL){
        printf("Invalid filename\n");
        return;
    }
    //copy this into the result
    strcpy(result, token);
    //take the next token and check size again
    token = strtok(NULL, ""); 
    if(strlen(token) > 3 || token == NULL){
        printf("Invalid extension for filename\n");
        return;
    }
    //calculate whitespace
    whitespace -= strlen(result);
    for(counter = 0; counter < whitespace; counter ++){
       strcat(result, " ");
    }
    //calculate whitespace for the extension and then add both on
    whitespace = 3 - strlen(token);
    strcat(result, token);
    for(counter = 0; counter < whitespace; counter ++){
        strcat(result, " ");
    }
    result[11] = 0;
    //uppercase everything
    for(counter = 0; counter < 11; counter ++){
        result[counter] = toupper(result[counter]);
    }
    //copy that back into filename
    strcpy(file_name, result);
}

/* 
 * Function: populate_dir
 * Parameters: address of the directory cluster you are at and the dir struct you want to fill
 * Returns: Nothing
 * Description: Will populate the directory entry array
 * with the directory at the given cluster
 */
void populate_dir(int DirectoryAddress, struct DirectoryEntry* direct) {
    fseek(fp, DirectoryAddress, SEEK_SET);
    for(counter = 0; counter < 16; counter ++){
        fread(direct[counter].DIR_Name, 1, 11, fp);
        direct[counter].DIR_Name[11] = 0;
        fread(&direct[counter].DIR_Attr, 1, 1, fp);
        fread(&direct[counter].Unused1, 1, 8, fp);
        fread(&direct[counter].DIR_FirstClusterHigh, 2, 1, fp);
        fread(&direct[counter].Unused2, 1, 4, fp);
        fread(&direct[counter].DIR_FirstClusterLow, 2, 1, fp);
        fread(&direct[counter].DIR_FileSize, 4, 1, fp);
    }
}

/* 
 * Function: open_file
 * Parameters: the filename of the file
 * Returns: Nothing
 * Description: Tries to open the file
 * If it doesn't open then returns an error statement
 * If it does open then will change opened to 1
 * This will also populate the BPB variables as well as the root directory entry
 */
void open_file(char* filename) {
    //tries to open the file
    fp = fopen(filename, "r");

    //either indicate the file is opened or closed
    if(fp != NULL){
        printf("Opened %s\n", filename);
        opened = 1;

        //grab all of BPB variables
        fseek(fp, 3, SEEK_SET);
        fread(BS_OEMName, 1, 8, fp);
        fread(&BPB_BytesPerSec, 1, 2, fp);
        fread(&BPB_SecPerClus, 1, 1, fp);
        fread(&BPB_RsvdSecCnt, 1, 2, fp);
        fread(&BPB_NumFATs, 1, 1, fp);
        fread(&BPB_RootEntCnt, 1, 2, fp);
        fseek(fp, 36, SEEK_SET);
        fread(&BPB_FATSz32, 1, 4, fp);
        fseek(fp, 44, SEEK_SET);
        fread(&BPB_RootClus, 1, 4, fp);
        fseek(fp, 71, SEEK_SET);
        fread(BS_VolLab, 1, 11, fp); 

        //calculate the root directory location
        RootDirClusterAddr = (BPB_NumFATs * BPB_FATSz32 * BPB_BytesPerSec) +
                             (BPB_RsvdSecCnt * BPB_BytesPerSec);

        //start off in the root directory
        CurrentDirClusterAddr = RootDirClusterAddr;

        //fill dir by going to the address and reading in the data to structs
        populate_dir(CurrentDirClusterAddr, dir);
    }
    else
        printf("Could not locate file\n");

}

/*
 * Function: next_lb
 * Parameters: logical block
 * Return: next_lb
 * Description: given a logical block, return the next logical block
 */
int16_t next_lb(int16_t sector){
    uint32_t FATAddr = (BPB_RsvdSecCnt * BPB_BytesPerSec) + (sector * 4);
    int16_t val;
    fseek(fp, FATAddr, SEEK_SET);
    fread(&val, 2, 1, fp);
    return val;
}

/*
 * Function: LBtoAddr
 * Parameters: logical block
 * Return: address of the cluster
 * Description: given a logical block, return an address
 */
int LBtoAddr(int32_t sector){
    if(!sector)
        return RootDirClusterAddr;
    return (BPB_BytesPerSec * BPB_RsvdSecCnt) + ((sector - 2) * BPB_BytesPerSec) + (BPB_BytesPerSec * BPB_NumFATs * BPB_FATSz32);
}

/* 
 * Function: list_dir
 * Parameters: none
 * Returns: Nothing
 * Description: Will display the directory contents specified
 * If a file is not opened then it will display 
 * an error message instead
 */
void list_dir() {
    char *token; //token to hold what to do next
    char tempname[15]; //token to hold the directory name
    char dirname[15]; //this will be used to check against entries
    int32_t tempAddr = CurrentDirClusterAddr; //this will be used to hold the location of the directory we are in
    struct DirectoryEntry tempdir[16]; //this will be used to hold the location of the directory we are in    
    int found; //this will determine whether an entry was found
    //check to see if the filesystem is open
    if(!opened){
        printf("There is no filesystem open.\n");
        return;
    }
    //check the current directory
    if(args[0] == NULL || !strcmp(args[0],".")){
        for(counter = 0; counter < 16; counter ++){
            //if the attribute is 1, 16, 32 then we show it
            if(dir[counter].DIR_Attr == 1  ||
               dir[counter].DIR_Attr == 16 ||
               dir[counter].DIR_Attr == 32 ){
                printf("%s    %d Bytes\n", dir[counter].DIR_Name, dir[counter].DIR_FileSize);   
            }   
        }
        return;
    }
    //take the first task and start going through the tasks
    token = strtok(args[0], "/");
    while(token != NULL){
        //check to see if it is valid
        if(strlen(token) > 12){
            printf("Directory does not exist\n");
            return;
        }
        //take the name and make it uppercase and spaced properly  							       
        strcpy(tempname, token);
        make_name(tempname);      
        //populate the current directory for ease
        populate_dir(tempAddr, tempdir);
        //check it against the current directory
        found = 0;
        for(counter = 0; counter < 16; counter ++){
            if(!strcmp(tempdir[counter].DIR_Name, tempname)){
                tempAddr = LBtoAddr(tempdir[counter].DIR_FirstClusterLow);
                populate_dir(tempAddr, tempdir);
                found ++;
                break;
            }
        }
        //leave if you couldn't find the directory    
        if(!found){
            printf("Could not find directory\n");
            break;
        }
        //take the next token
        token = strtok(NULL,"/");
        //check to see if the next token is NULL
        if(token == NULL){
            //show all the contents   
            for(counter = 0; counter < 16; counter ++){
                //if the attribute is 1, 16, 32 then we show it
                if(tempdir[counter].DIR_Attr == 1  ||
                   tempdir[counter].DIR_Attr == 16 ||
                   tempdir[counter].DIR_Attr == 32 ){
                    printf("%s    %d Bytes\n", tempdir[counter].DIR_Name, tempdir[counter].DIR_FileSize);   
                }   
            }
            break;
        }
    }
    
}

/* 
 * Function: display_info
 * Parameters: None
 * Returns: Nothing
 * Description: Will display the filesystem info
 * If a file is not opened then it will display 
 * an error message instead
 */
void display_info() {
    if(!opened)
        printf("There is no file open!\n");
    else{
        //lines commented out are for debug only
        //printf("OEMName : %s\n", BS_OEMName);
        printf("Bytes Per Sector:\n    Hex - %x\n    Decimal - %d\n", 
                BPB_BytesPerSec, BPB_BytesPerSec);
        printf("Sectors Per Clusters:\n    Hex - %x\n    Decimal - %d\n", 
                BPB_SecPerClus, BPB_SecPerClus);
        printf("Reserved Sector Count:\n    Hex - %x\n    Decimal - %d\n", 
                BPB_RsvdSecCnt, BPB_RsvdSecCnt);
        printf("Number of FAT regions:\n    Hex - %x\n    Decimal - %d\n", 
                BPB_NumFATs, BPB_NumFATs);
        //printf("Root Entry Count : %d\n", BPB_RootEntCnt);
        printf("Sectors per FAT:\n    Hex - %x\n    Decimal - %d\n", 
                BPB_FATSz32, BPB_FATSz32);
        //next line won't print since the value of VolLab is 02, 11 times 
        //printf("Volume Label : %s\n", BS_VolLab);
        //printf("First Cluster of the Root Directory : %d\n", BPB_RootClus);

    } 
}

/*
 * Function: changes the directory
 * Parameters: None
 * Returns: nothing
 * Description: Changes the directory
 * If the file/directory can't be found then it will pop an error
 */
void change_dir(){
    char tempname[15]; // name to hold the directory name
    char * token; // token to temporary hold the next task
    int found; // boolean to determine if the directory is found
    //if the filesystem isn't open you can't do anything
    if(!opened){
        printf("There is no filesystem open!\n");
        return;
    }
    //if the first argument doesn't exist just go to the root
    if(args[0] == NULL){
        CurrentDirClusterAddr = RootDirClusterAddr;
        populate_dir(CurrentDirClusterAddr, dir);
        return;
    }
    //take the first task and start going through the tasks
    token = strtok(args[0], "/");
    while(token != NULL){
        //check to see if it is valid
        if(strlen(token) > 12){
            printf("Directory does not exist\n");
            return;
        }
        //take the name and make it uppercase and spaced properly  							       
        strcpy(tempname, token);
        make_name(tempname);      
        //check it against the current directory
        found = 0;
        for(counter = 0; counter < 16; counter ++){
            if(!strcmp(dir[counter].DIR_Name, tempname)){
                CurrentDirClusterAddr = LBtoAddr(dir[counter].DIR_FirstClusterLow);
                populate_dir(CurrentDirClusterAddr, dir);
                found ++;
                break;
            }
        }
        //leave if you couldn't find the directory    
        if(!found){
            printf("Could not find directory '%s', stopped at 1 directory before\n", token);
            break;
        }
        //take the next token
        token = strtok(NULL, "/");
        if(token == NULL){
            break;
        }
    }
}

/*
 * Function: read_file
 * Parameters: none
 * Returns: nothing
 * Description: Will attempt to read a file starting from a position
 * in the file and displaying a certain amount of bytes after
 */
void read_file(){
    char * token; // token to hold the strtok'ed things
    char tempname[15]; // string to hold the filename or directory of what to look for
    int found; // boolean to determine if their was a match
    int32_t tempAddr = CurrentDirClusterAddr; //this will be used to hold the location of the directory we are in
    struct DirectoryEntry tempdir[16]; //this will be used to hold the location of the directory we are in    
    int32_t size; // int to hold the size of the file
    int16_t LB; // used to hold the current logical block
    int data = atoi(args[2]); // will be used to hold the data needed
    int BlockOff = atoi(args[1])/512; // int used to hold block offset
    int ByteOff = atoi(args[1])%512; // int used to hold the initial byte offset
    int grab; // will be used to hold the max of data or 512
    char buffer[513]; // string to hold the data retrieved from fread and then displayed
    //if the file isn't open get out of here
    if(!opened){
        printf("There is no filesystem open!\n");
        return;
    }
    //tokenize
    token = strtok(args[0], "/");
    //do a while loop to read through the argument, changing directories all the while
    while(1){
        //if the command is too long it isn't valid
        if(strlen(token) > 12){
            printf("Invalid argument!\n");
            return;
        } 
        //copy the token over into a useable buffer
        strcpy(tempname, token);
        //take the next token then check if it is NULL, if it is then tempname is a file
        token = strtok(NULL, "/");
        if(token == NULL){
            break;
        } 
        //since this is a directory then change the name into proper format and search
        make_name(tempname);
        populate_dir(tempAddr, tempdir);
        found = 0;
        for(counter = 0; counter < 16; counter ++){
            //if a match was found then change the temp directory
            if(!strcmp(tempdir[counter].DIR_Name, tempname)){
                tempAddr = LBtoAddr(tempdir[counter].DIR_FirstClusterLow);
                found ++;
                break;
            }
        }
        //if there was no matches found end it.
        if(!found){
            printf("Invalid Directory Name!\n");
            return;
        }
    }
    //change the command into file format and search the tempdir
    make_file(tempname);
    populate_dir(tempAddr, tempdir);
    found = 0;
    for(counter = 0; counter < 16; counter ++){
        //if a match is found then grab the LB and the size
        if(!strcmp(tempdir[counter].DIR_Name, tempname)){
            size = tempdir[counter].DIR_FileSize;
            LB = tempdir[counter].DIR_FirstClusterLow;
            found ++;
            break;
        }
    }
    //if it isn't found then tell the user
    if(!found){
        printf("File Not Found!\n");
        return;
    }
    //if the file size is smaller than the size requested deny them
    if(size < data + atoi(args[1])){
        printf("More data requested than can be given!\n");
        return;
    }
    //loop through block off to find the start
    for(counter = 0; counter < BlockOff; counter ++){
        LB = next_lb(LB);
    }
    //calculate address of LB
    tempAddr = LBtoAddr(LB);
    //go to the offset
    fseek(fp, tempAddr + ByteOff, SEEK_SET);
    //while loop until you get all of the data
    while(1){
        //choose the minimum
        if(data < 512)
            grab = data;
        else
            grab = 512;
        //read the spot and null terminate
        fread(buffer, 1, 512, fp);
        buffer[grab] = 0;
        //display the buffer
        //as hex values
        for(counter = 0; counter < grab; counter ++){
            printf("%x ", buffer[counter]);
        }
        //as text values
        //printf("%s", buffer);
        //subtract data then check if we need anymore
        data -= grab;
        if(data == 0){
            printf("\n");
            return;
        }
        //grab the next block and calculate the next address
        LB = next_lb(LB);
        tempAddr = LBtoAddr(LB);
        //go to the next address and start again
        fseek(fp, tempAddr, SEEK_SET); 
    }
}

/*
 * Function: show_stat
 * Parameters: None
 * Returns: nothing
 * Description: Will display attributes, starting cluster number, and size of
 * a specified filename or directory.
 * If the file/directory can't be found then it will pop an error
 */
void show_stat(){
    char namebuffer[20]; // string for the name
    char extbuffer[4]; // string for the extension
    char statbuffer[20]; // string that will become the concatenation of the previous two
    char *token; // token for the string to strtok into
    int whitespace; // the offset white space
    int found = 0; // boolean to see if an entry was found

    //if the filesytem is open then do the process
    //otherwise notify the user
    if(opened){
        //the max name length could only be 8 + 1 + 3
        if(strlen(args[0]) < 13){
            //copy the first part of the string, delimited by .
            token = strtok(args[0], ".");
            if(token == NULL){
                printf("Not a valid filename\n");
                return;
            }
            strcpy(namebuffer, token);
            //grab the second part of the string after the .
            token = strtok(NULL, "");
            //if the token didn't receive anything then check if the file is a directory
            //otherwise check if it a valid file
            if(token == NULL && strlen(namebuffer) < 12){
                //uppercase everything
                for(counter = 0; counter < 11; counter ++){
                    namebuffer[counter] = toupper(namebuffer[counter]);
                }
                //add spaces
                whitespace = 11 - strlen(namebuffer);
                //concat whitespace
                for(counter = 0; counter < whitespace; counter ++){
                    strcat(namebuffer, " ");
                }
                //check through the directory
                found = 0;
                for(counter = 0; counter < 16; counter ++){
                    //if you find the entry then break out
                    if(!strcmp(dir[counter].DIR_Name, namebuffer)){
                        //print attribute, starting cluster number, and size
                        printf("Attribute: %x\n", dir[counter].DIR_Attr);
                        printf("Starting cluster number:\n    Hex - %x\n    Decimal - %d\n", 
                                   dir[counter].DIR_FirstClusterLow, dir[counter].DIR_FirstClusterLow);
                        //becuase it is a directory
                        printf("Size: 0 Bytes\n"); 
                        found = 1;
                        break;
                    }
                }    
                if(!found)
                    printf("Couldn't find the directory\n");
            }
            else if(strlen(token) > 3){
                printf("File extension too long (3 characters)\n");
            }
            else if(strlen(namebuffer) > 8){
                printf("File name too long (8 characters).\n");
            }
            else{
                //add spaces to the name
                whitespace = 8 - strlen(namebuffer);
                //concat whitespace into filename and then extension onto filename
                for(counter = 0; counter < whitespace; counter ++){
                    strcat(namebuffer, " ");
                }
                strcpy(extbuffer, token);
                //add spaces to the file extension
                whitespace = 3 - strlen(extbuffer);
                //concat whitespace into filename and then extension onto filename
                for(counter = 0; counter < whitespace; counter ++){
                    strcat(extbuffer, " ");
                }
                //concatenate together
                sprintf(statbuffer, "%s%s", namebuffer, extbuffer);
                //uppercase everything
                for(counter = 0; counter < 11; counter ++){
                    statbuffer[counter] = toupper(statbuffer[counter]);
                }
                //check through directory   
                found = 0;
                for(counter = 0; counter < 16; counter ++){
                    //if you find the entry then break out
                    if(!strcmp(dir[counter].DIR_Name, statbuffer)){
                        //print attribute, starting cluster number, and size
                        printf("Attribute: %x\n", dir[counter].DIR_Attr);
                        printf("Starting cluster number:\n    Hex - %x\n    Decimal - %d\n", 
                                   dir[counter].DIR_FirstClusterLow, dir[counter].DIR_FirstClusterLow);
                        printf("Size: %d Bytes\n", dir[counter].DIR_FileSize); 
                        found = 1;
                        break;
                    }
                }
                if(!found)
                    printf("File not found\n");    
            }
        }
        else
            printf("That is an invalid file or directory name\n");
    }
    else
        printf("There is no filesystem open.\n");
}



/* 
 * Function: main
 * Parameters: None
 * Returns: int (return code)
 * Description: Initializes signal for control-c and control-z and
 * then begins an infinite while loop to parse user input and decide
 * what to do with the input. Calls execute command when nessesary 
 * or exits.
 */
int main(void) {
    // set up signal catching for control-c and control-z
    signal(SIGINT, cntl_c_handler);
    signal(SIGTSTP, cntl_z_handler);

    // allocate memory for current argument buffer
    arg = (char*) malloc(200);
    while (1) {

        // print shell input ready
        printf("mfs>");

        // get user input 
        get_user_input();

        // load up first token of user input for memory allocation 
        arg = strtok(user_input, " ");
 
        // allocate memory for base command
        base_command = (char*) malloc(sizeof(arg)+1);
        base_command = arg;

        // set up counter to count tokens
        counter = 0;
        do { 
            arg = strtok(NULL, " ");
           
            // allocate memory for arguments
            args[counter] = (char*) malloc(sizeof(arg)+1);
            args[counter] = arg;
            counter++;
        // loop until no more arguments
        } while(counter < 10);

        // handle all exits
       	if(!strcmp(base_command, "exit") || !strcmp(base_command, "quit")){
            if(opened)
                fclose(fp);
            exit (0);
        }
	
        // open the file or tell them a file is already open
        if(!strcmp(base_command, "open")){
            //if a file isn't opened try to open a file
            if(!opened)
                open_file(args[0]);
            else
                printf("There is already a filesystem open!\n");
            continue;
        }
 
        // close the file or tell them no files are open
        if(!strcmp(base_command, "close")){
            //if a file is opened close it
            if(opened){
                fclose(fp);
                printf("Closed filesystem.\n");	
                opened = 0;
            }
            else
                printf("There is no filesystem open!\n");
            continue;
        }

        // print out some of the stats inside the file
        if(!strcmp(base_command, "info")){
            display_info();
            continue;
        }

        // print out some of the stats inside the file
        if(!strcmp(base_command, "stat") && args[0] != NULL && args[1] == NULL){
            show_stat();
            continue;
        }

        // print out some of the stats inside the file
        if(!strcmp(base_command, "ls") && args[1] == NULL){
            list_dir();
            continue;
        }

        //change the directory
        if(!strcmp(base_command, "cd") && args[1] == NULL){
            change_dir();
            continue;
        }

        //read a file
        if(!strcmp(base_command, "read") && args[1] != NULL && args[2] != NULL  && args[3] == NULL){
            read_file(); 
            continue;
        }

        //display the volume name
        if(!strcmp(base_command, "volume") && args[0] == NULL){
             if(!opened){
                 printf("There is no filesystem open!\n");
                 continue;
             }
             //we can print the volume name we found in the boot record since it
             //is the same as the one in the root directory 
             if(!strcmp(BS_VolLab, ""))
                 printf("Volume name not found\n");
             else
	         printf("Volume name: '%s'\n", BS_VolLab);
             continue;
        }

        //if the base command is something and not used 
        if(strcmp(base_command, "\n"))
            printf("That isn't a valid command\n");


    }
    exit(0);
}
