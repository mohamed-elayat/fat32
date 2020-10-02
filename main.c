

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#pragma ide diagnostic ignored "hicpp-signed-bitwise"
#pragma ide diagnostic ignored "readability-non-const-parameter"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FAT_NAME_LENGTH 11
#define FAT_EOC_TAG 0x0FFFFFF8
#define FAT_DIR_ENTRY_SIZE 32
#define HAS_NO_ERROR(err) ((err) >= 0)

#define NO_ERR 0
#define GENERAL_ERR -1
#define NOT_ENOUGH_LEVELS -2
#define OUT_OF_MEM -3
#define RES_NOT_FOUND -4
#define NOT_A_DIR -5

#define CLUSTER_TO_LBA_ERR -6
#define GET_CLUSTER_CHAIN_VALUE_ERR -7
#define READ_BOOT_BLOCK_ERR -8

char* errors[] = { " read_boot_block_err", " get_cluster_chain_value_err",
                   " cluster_to_chain_lba_err", " not_a_dir",
                   " res_not_found", " out_of_memory",
                   " not_enough_levels", " general_err",
                   " no_err"};

#define CAST(t, e) ((t) (e))
#define as_uint16(x) \
((CAST(uint16,(x)[1])<<8U)+(x)[0])
#define as_uint32(x) \
((((((CAST(uint32,(x)[3])<<8U)+(x)[2])<<8U)+(x)[1])<<8U)+(x)[0])

#define MIN(x, y) \
( ((x) < (y)) ? (x) : (y) )

typedef unsigned char uint8;
typedef uint8 bool;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef int error_code;

/**
 * Pourquoi est-ce que les champs sont construit de cette façon et non pas directement avec les bons types?
 * C'est une question de portabilité. FAT32 sauvegarde les données en BigEndian, mais votre système de ne l'est
 * peut-être pas. Afin d'éviter ces problèmes, on lit les champs avec des macros qui convertissent la valeur.
 * Par exemple, si vous voulez lire le paramètre BPB_HiddSec et obtenir une valeur en entier 32 bits, vous faites:
 *
 * BPB* bpb;
 * uint32 hidden_sectors = as_uint32(BPB->BPP_HiddSec);
 *
 */
typedef struct BIOS_Parameter_Block_struct {
    uint8 BS_jmpBoot[3];
    uint8 BS_OEMName[8];
    uint8 BPB_BytsPerSec[2];  // 512, 1024, 2048 or 4096
    uint8 BPB_SecPerClus;     // 1, 2, 4, 8, 16, 32, 64 or 128
    uint8 BPB_RsvdSecCnt[2];  // 1 for FAT12 and FAT16, typically 32 for FAT32
    uint8 BPB_NumFATs;        // should be 2
    uint8 BPB_RootEntCnt[2];
    uint8 BPB_TotSec16[2];
    uint8 BPB_Media;
    uint8 BPB_FATSz16[2];

    uint8 BPB_SecPerTrk[2];
    uint8 BPB_NumHeads[2];
    uint8 BPB_HiddSec[4];
    uint8 BPB_TotSec32[4];
    uint8 BPB_FATSz32[4];
    uint8 BPB_ExtFlags[2];
    uint8 BPB_FSVer[2];
    uint8 BPB_RootClus[4];
    uint8 BPB_FSInfo[2];
    uint8 BPB_BkBootSec[2];

    uint8 BPB_Reserved[12];
    uint8 BS_DrvNum;
    uint8 BS_Reserved1;
    uint8 BS_BootSig;
    uint8 BS_VolID[4];
    uint8 BS_VolLab[11];
    uint8 BS_FilSysType[8];
} BPB;

typedef struct FAT_directory_entry_struct {
    uint8 DIR_Name[FAT_NAME_LENGTH];
    uint8 DIR_Attr;
    uint8 DIR_NTRes;
    uint8 DIR_CrtTimeTenth;
    uint8 DIR_CrtTime[2];
    uint8 DIR_CrtDate[2];
    uint8 DIR_LstAccDate[2];
    uint8 DIR_FstClusHI[2];
    uint8 DIR_WrtTime[2];
    uint8 DIR_WrtDate[2];
    uint8 DIR_FstClusLO[2];
    uint8 DIR_FileSize[4];
} FAT_entry;

uint8 ilog2(uint32 n) {
    uint8 i = 0;
    while ((n >>= 1U) != 0)
        i++;
    return i;
}

//--------------------------------------------------------------------------------------------------------
//                                           DEBUT DU CODE
//--------------------------------------------------------------------------------------------------------

/**
 * Exercice 1
 *
 * Prend cluster et retourne son addresse en secteur dans l'archive
 * @param block le block de paramètre du BIOS
 * @param cluster le cluster à convertir en LBA
 * @param first_data_sector le premier secteur de données, donnée par la formule dans le document
 * @return le LBA
 */
uint32 cluster_to_lba(BPB *block, uint32 cluster, uint32 first_data_sector) {

    //todo: is uint32 guaranteed to hold all possible values of sectors?
    // i.e. does the range of uint32 cover the totSec field?

    //todo: which to use?

    if(block == NULL || cluster < as_uint32( block->BPB_RootClus ) || first_data_sector < 0)
        return CLUSTER_TO_LBA_ERR;

    uint32 first_sector_of_cluster = ( (cluster - as_uint32( block->BPB_RootClus ) ) * block->BPB_SecPerClus ) + first_data_sector;

    return first_sector_of_cluster;
}

void out_of_memory(){

    perror("out_of_memory");
    exit(1);

}

/**
 * Exercice 2
 *
 * Va chercher une valeur dans la cluster chain
 * @param block le block de paramètre du système de fichier
 * @param cluster le cluster qu'on veut aller lire
 * @param value un pointeur ou on retourne la valeur
 * @param archive le fichier de l'archive
 * @return un code d'erreur
 */
error_code get_cluster_chain_value(BPB *block,
                                   uint32 cluster,
                                   uint32 *value,
                                   FILE *archive) {

    if(block == NULL || cluster < 0 || value == NULL || archive == NULL)
        return GET_CLUSTER_CHAIN_VALUE_ERR;

    uint32 FAT_offset = cluster * 4;

    uint32 this_FAT_sec_num = as_uint16(block->BPB_RsvdSecCnt) + (FAT_offset / as_uint16(block->BPB_BytsPerSec)); //todo: do I need to do a cast?
    uint32 this_FAT_ent_offset = FAT_offset % as_uint16(block->BPB_BytsPerSec);

    fseek(archive,
            this_FAT_sec_num * as_uint16( block->BPB_BytsPerSec ) + this_FAT_ent_offset,
            SEEK_SET);

    uint8 arr[4];
    fread(arr, 4, 1, archive);

    uint32 FAT32_clus_entry_val = as_uint32(arr) & 0x0FFFFFFF;

    *value = FAT32_clus_entry_val;

    return NO_ERR;
}



/**
 * Exercice 3
 *
 * Vérifie si un descripteur de fichier FAT identifie bien fichier avec le nom name
 * @param entry le descripteur de fichier
 * @param name le nom de fichier
 * @return 0 ou 1 (faux ou vrai)
 */
bool file_has_name(FAT_entry *entry, char *name) {

    uint8 dot_arr[] = { '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    uint8 dot_dot_arr[] = { '.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};

    if(memcmp(entry->DIR_Name, dot_arr, 11) == 0 && strcmp(name, ".") == 0){
        return 1;
    }

    if(memcmp(entry->DIR_Name, dot_dot_arr, 11) == 0 && strcmp(name, "..") == 0){
        return 1;
    }

    char* conv_name = malloc(11 * sizeof(char) + 1);
    if(conv_name == NULL) out_of_memory();
    char* start_conv_name = conv_name;

    for(int i = 0; i < 11; i++){
        *conv_name++ = ' ';
    }
    *conv_name = '\0';

    conv_name = start_conv_name;

    while(*name != '\0'){
        if(*name == '.'){
            conv_name = start_conv_name + 8;
            name++;
        }
        else{
            *conv_name++ = (char)toupper( *name++ );
        }
    }

    conv_name = start_conv_name;

    for(int i = 0; i < 11; i++){
        if(entry->DIR_Name[i] != conv_name[i]){
            free(conv_name);
            return 0;
        }
    }
    free(conv_name);
    return 1;
}

/**
 * Exercice 4
 *
 * Prend un path de la forme "/dossier/dossier2/fichier.ext et retourne la partie
 * correspondante à l'index passé. Le premier '/' est facultatif.
 * @param path l'index passé
 * @param level la partie à retourner (ici, 0 retournerait dossier)
 * @param output la sortie (la string)
 * @return -1 si les arguments sont incorrects, -2 si le path ne contient pas autant de niveaux
 * -3 si out of memory
 */
error_code break_up_path(char *path, uint8 level, char **output) {

    if(path == NULL || level < 0 || output == NULL){
        return GENERAL_ERR;
    }

    int i;
    char* path_begin = path;

    if(*path == '/')
        i = 0;
    else
        i = 1;

    while(*path != '\0'){
        if(*path++ == '/') {
            i++;
        }
    }

    path = path_begin;

    char* path_copy = malloc(strlen(path) * sizeof(*path) + 1 );
    if(path_copy == NULL) return OUT_OF_MEM;
    strcpy(path_copy, path);
    *(path_copy + strlen(path)) = '\0';

    char* token = strtok(path_copy, "/");

    int j = 0;

    while( token != NULL ){

        if(j == level){
            *output = malloc(strlen(token) * sizeof(*token) + 1 );
            if(*output == NULL) return OUT_OF_MEM;
            strcpy(*output, token);

            free(path_copy);

            return i ;
        }
        j++;
        token = strtok(NULL, "/");
    }

    free(path_copy);
    return NOT_ENOUGH_LEVELS;
}


/**
 * Exercice 5
 *
 * Lit le BIOS parameter block
 * @param archive fichier qui correspond à l'archive
 * @param block le block alloué
 * @return un code d'erreur
 */
error_code read_boot_block(FILE *archive, BPB **block) {

    if(archive == NULL || block == NULL){
        return READ_BOOT_BLOCK_ERR;
    }

    fseek(archive, 0, SEEK_SET);

    *block = malloc(sizeof(BPB));
    if(*block == NULL) return OUT_OF_MEM;

    fread(   (*block)->BS_jmpBoot, 3, 1, archive);
    fread(   (*block)->BS_OEMName, 8, 1, archive);
    fread(   (*block)->BPB_BytsPerSec, 2, 1, archive);
    fread(   &((*block)->BPB_SecPerClus), 1, 1, archive);
    fread(   (*block)->BPB_RsvdSecCnt, 2, 1, archive);
    fread(   &((*block)->BPB_NumFATs), 1, 1, archive);
    fread(   (*block)->BPB_RootEntCnt, 2, 1, archive);
    fread(   (*block)->BPB_TotSec16, 2, 1, archive);
    fread(   &((*block)->BPB_Media), 1, 1, archive);
    fread(   (*block)->BPB_FATSz16, 2, 1, archive);

    fread(   (*block)->BPB_SecPerTrk, 2, 1, archive);
    fread(   (*block)->BPB_NumHeads, 2, 1, archive);
    fread(   (*block)->BPB_HiddSec, 4, 1, archive);
    fread(   (*block)->BPB_TotSec32, 4, 1, archive);
    fread(   (*block)->BPB_FATSz32, 4, 1, archive);
    fread(   (*block)->BPB_ExtFlags, 2, 1, archive);
    fread(   (*block)->BPB_FSVer, 2, 1, archive);
    fread(   (*block)->BPB_RootClus, 4, 1, archive);
    fread(   (*block)->BPB_FSInfo, 2, 1, archive);
    fread(   (*block)->BPB_BkBootSec, 2, 1, archive);

    fread(   (*block)->BPB_Reserved, 12, 1, archive);
    fread(   &((*block)->BS_DrvNum), 1, 1, archive);
    fread(   &((*block)->BS_Reserved1), 1, 1, archive);
    fread(   &((*block)->BS_BootSig), 1, 1, archive);
    fread(   (*block)->BS_VolID, 4, 1, archive);
    fread(   (*block)->BS_VolLab, 11, 1, archive);
    fread(   (*block)->BS_FilSysType, 8, 1, archive);

    return NO_ERR;
}


error_code read_file(FILE *archive, BPB *block, FAT_entry *entry, void *buff, size_t max_len);
int get_no_clus_from_entry(FILE* archive, BPB* block, FAT_entry* entry);


/**
 * Exercice 6
 *
 * Trouve un descripteur de fichier dans l'archive
 * @param archive le descripteur de fichier qui correspond à l'archive
 * @param path le chemin du fichier
 * @param entry l'entrée trouvée
 * @return un code d'erreur
 */
error_code find_file_descriptor(FILE *archive, BPB *block, char *path, FAT_entry **entry) {


    if(archive == NULL || block == NULL || path == NULL || entry == NULL){
        return GENERAL_ERR;
    }

    *entry = malloc(sizeof(FAT_entry));
    if(*entry == NULL) return OUT_OF_MEM;
    char** output = malloc(1 * sizeof(*output) );
    if(output == NULL) return OUT_OF_MEM;
    *output = NULL;

//    memcpy( (*entry)->DIR_FstClusLO, block->BPB_RootClus, 2 );
//    memcpy( (*entry)->DIR_FstClusHI, block->BPB_RootClus + 2, 2 );

    (*entry)->DIR_FstClusLO[0] = block->BPB_RootClus[0];
    (*entry)->DIR_FstClusLO[1] = block->BPB_RootClus[1];

    (*entry)->DIR_FstClusHI[0] = block->BPB_RootClus[2];
    (*entry)->DIR_FstClusHI[1] = block->BPB_RootClus[3];

    uint32 bytes_per_cluster = as_uint16( block->BPB_BytsPerSec ) * block->BPB_SecPerClus; //todo: might overflow

    unsigned int i, j, k;
    unsigned int lvl = 0;

//    j = break_up_path(path, lvl, output);
//    if(j < 0) return j;

    char* buff;
    size_t max_len;

    do {
        // /dossier/dossier2/fichier.ext
        //        conv_name
        i = get_no_clus_from_entry(archive, block, *entry);
        if(i < 0) {
            if(*output != NULL){
                free(*output);
            }
            free(output);
            free(*entry);
            return i;
        }

        j = break_up_path(path, lvl, output);
        if(j < 0) {
            if(*output != NULL){
                free(*output);
            }
            free(output);
            free(*entry);
            return j;
        }

        k = ( (bytes_per_cluster) * i ) / sizeof(FAT_entry);

        buff = malloc(i * bytes_per_cluster);  //todo: char, because of byte? or use what's below?
        if(buff == NULL) return OUT_OF_MEM;
        max_len = i * bytes_per_cluster;

        int code = read_file(archive, block, *entry, buff, max_len);
        if(code < 0) {
            free(buff);
//            free(output);

            if(*output != NULL){
                free(*output);
            }
            free(output);
            free(*entry);

            return code;
        }


        for (int m = 0; m < k; m++) {

            memcpy(*entry, buff + m * sizeof(FAT_entry), sizeof(FAT_entry));
            if (file_has_name(*entry, *output)){

                if(  (lvl != j-1) && (*entry)->DIR_Attr != 16   ){
                    free(buff);
                    if(*output != NULL){
                        free(*output);
                    }
                    free(output);
                    free(*entry);
                    return NOT_A_DIR;
                }

                goto skip_error;
            }
        }

        free(buff);
        if(*output != NULL){
            free(*output);
        }
        free(output);
        free(*entry);
        return RES_NOT_FOUND;
        skip_error:
        lvl++;
        free(buff);
//        free(output);
//        free(*output);

        if(*output != NULL){
            free(*output);
        }
//        free(*entry);

    }
    while(lvl < j);
    free(output);
//    free(*entry);
    return NO_ERR;
}

int get_no_clus_from_entry(FILE* archive, BPB* block, FAT_entry* entry){

    if(archive == NULL || block == NULL || entry == NULL ){
        return GENERAL_ERR;
    }

    int i = 0;

    uint32* value = malloc(sizeof(uint32));
    if(value == NULL) out_of_memory();
    *value = (  (as_uint16( entry->DIR_FstClusHI ) << 16)  + as_uint16(entry->DIR_FstClusLO) );

    int code;

//    while(*value < FAT_EOC_TAG){
//        i++;
//        get_cluster_chain_value(block, *value, value , archive);
//    }

    while(*value < FAT_EOC_TAG){
        i++;
        code = get_cluster_chain_value(block, *value, value , archive);
        if( code < 0 ) {
            free(value);
            return code;
        }
    }

    free(value);
    return i;
}

int create_clus_arr(FILE* archive, BPB* block, FAT_entry* entry, uint32** clus_arr){

    uint32* value = malloc( sizeof(uint32) );
    if(value == NULL) out_of_memory();
    *value = (as_uint16(entry->DIR_FstClusHI) << 16) + as_uint16(entry->DIR_FstClusLO);

    int size_of_clus_nos = get_no_clus_from_entry(archive, block, entry);

    if(size_of_clus_nos < 0) {
        free(value);
        return size_of_clus_nos;
    }

    *clus_arr = malloc (size_of_clus_nos * sizeof(uint32) );
    if(*clus_arr == NULL) out_of_memory();

    uint32* clus_arr_start = *clus_arr;
    int code;

//    while(  *value < (uint32) FAT_EOC_TAG ){
//        **clus_arr = *value;
//        (*clus_arr)++;
//        get_cluster_chain_value(block, *value, value , archive);
//    }

    while(  *value < (uint32) FAT_EOC_TAG ){
        **clus_arr = *value;
        (*clus_arr)++;
        code = get_cluster_chain_value(block, *value, value , archive);
        if(code < 0){
            free(value);
            free(*clus_arr);
            return code;
        }
    }

    (*clus_arr) = clus_arr_start;

    free(value);

    return size_of_clus_nos;
}


/**
 * Exercice 7
 *
 * Lit un fichier dans une archive FAT
 * @param entry l'entrée du fichier
 * @param buff le buffer ou écrire les données
 * @param max_len la longueur du buffer
 * @return un code d'erreur qui va contenir la longueur des donnés lues
 */
error_code
read_file(FILE *archive, BPB *block, FAT_entry *entry, void *buff, size_t max_len) {

    if(archive == NULL || block == NULL || entry == NULL || buff == NULL || max_len < 0){
        return GENERAL_ERR;
    }

    uint32** clus_arr = malloc(1 * sizeof(*clus_arr));
    if(clus_arr == NULL) return OUT_OF_MEM;
    int clus_arr_size = create_clus_arr(archive, block, entry, clus_arr);

    if(clus_arr_size < 0) {
        free(clus_arr);
        return clus_arr_size;
    }

    uint32 first_data_sector = as_uint16( block->BPB_RsvdSecCnt ) + block->BPB_NumFATs * as_uint32( block->BPB_FATSz32);

    int sec_no;
    uint32 bytes_per_cluster = as_uint16( block->BPB_BytsPerSec ) * block->BPB_SecPerClus; //todo: might overflow

    size_t max_len_save = max_len;

    for(int j = 0; j < clus_arr_size; j++){
        sec_no = cluster_to_lba(block, *( (*clus_arr) + j), first_data_sector);

        if(sec_no < 0) {
            free(*clus_arr);
            free(clus_arr);
            return sec_no;
        }

        fseek(archive, sec_no * as_uint16(block->BPB_BytsPerSec), SEEK_SET);

        if(max_len < bytes_per_cluster){
            fread(buff, max_len, 1, archive);
            buff += max_len;
            max_len -= max_len;
            break;
        }
        else{
            fread(buff, bytes_per_cluster, 1, archive);
            buff += bytes_per_cluster;
            max_len -= bytes_per_cluster;
        }

    }

    free(*clus_arr);
    free(clus_arr);

    if(max_len == 0){
        *(  ( (char*) buff ) - 1 ) = '\0';
    }
    else{
        *( (char*) buff ) = '\0';
    }

    if(max_len == 0){
        return max_len_save;
    }
    else{
        return clus_arr_size * bytes_per_cluster;
    }

}

// ---------------------------------------------------------------------------------------------------------------------
//                                              MAIN HELPER FUNCTIONS
// ---------------------------------------------------------------------------------------------------------------------

void create_copies(FILE* archive, BPB* block, FAT_entry** entry){

    int max_len = 250000;
    char* buff = malloc (max_len * sizeof(*buff));
    if(buff == NULL) out_of_memory();
    FILE* fp;

    fp = fopen("../disk2/AFOLDER/ANOTHER/CANDIDE.TXT", "w");
    find_file_descriptor(archive, block, "afolder/another/candide.txt", entry);
    read_file(archive, block, *entry, buff, max_len);
    fprintf(fp, "%s", buff);
    fclose(fp);
    free(*entry);

    fp = fopen("../disk2/SPANISH/LOS.TXT", "w");
    find_file_descriptor(archive, block, "spanish/los.txt", entry);
    read_file(archive, block, *entry, buff, max_len);
    fprintf(fp, "%s", buff);
    fclose(fp);
    free(*entry);

    fp = fopen("../disk2/SPANISH/TITAN.TXT", "w");
    find_file_descriptor(archive, block, "spanish/titan.txt", entry);
    read_file(archive, block, *entry, buff, max_len);
    fprintf(fp, "%s", buff);
    fclose(fp);
    free(*entry);

    fp = fopen("../disk2/ZOLA.TXT", "w");
    find_file_descriptor(archive, block, "zola.txt", entry);
    read_file(archive, block, *entry, buff, max_len);
    fprintf(fp, "%s", buff);
    fclose(fp);
    free(*entry);

    fp = fopen("../disk2/HELLO.TXT", "w");
    find_file_descriptor(archive, block, "hello.txt", entry);
    read_file(archive, block, *entry, buff, max_len);
    fprintf(fp, "%s", buff);
    fclose(fp);
    free(*entry);

    free(buff);


}


int compare_files(char* f1, char* f2){

    FILE* fp1 = fopen(f1, "r");
    FILE* fp2 = fopen(f2, "r");

    char c1, c2;

    do {
        c1 = getc(fp1);
        c2 = getc(fp2);

        if(c1 != c2){
            return -1;
        }

    }
    while(c1 != EOF && c2 != EOF);

    fclose(fp1);
    fclose(fp2);

    return 0;

}

error_code readline(FILE *fp, char **out, size_t max_len) {

    // On alloue une byte de plus pour pouvoir inclure
    // le '/0'.
    char* tmp = malloc(max_len*sizeof(char) + 1);
    if(tmp == NULL) out_of_memory();
    char* tmp2 = tmp;

    if(tmp == NULL){
        return GENERAL_ERR;
    }

    int c;

    while(1){
        c = getc(fp);

        if(c == 10){
            break;
        }
        else{
            *tmp++ = (char)c;
        }

    }
    *tmp = '\0';

    tmp = tmp2;

    error_code ret = memcpy(*out, tmp, strlen(tmp) + 1);
    free(tmp);

    return ret;
}




// ---------------------------------------------------------------------------------------------------------------------
//                                              MAIN
// ---------------------------------------------------------------------------------------------------------------------


int main(int argc, char *argv[]) {
    /*
     * Vous êtes libre de faire ce que vous voulez ici.
     */

    FILE* archive = fopen("../floppy.img", "r");
    BPB** block = malloc (1 * sizeof(*block));
    if(block == NULL) out_of_memory();
//    char* path = "afolder/another/../another/././././../another/./././../another/candide.txt";
    FAT_entry** entry = malloc(1 * sizeof(*entry) );
    if(entry == NULL) out_of_memory();

    size_t max_len;
    char* buff= malloc (1);
    if(buff == NULL) out_of_memory();

    int code = read_boot_block(archive, block);
    if(code < 0) {
        free(entry);
        free(*block);
        free(block);

        return code;
    }

//    fopen(argv[1], "r");

    FILE* fp_r = fopen("../tests/unit_tests.txt", "r");
    FILE* fp_w = fopen("../tests/results", "w");

    long pos = 0;
    char c;

    char** ln_1 = malloc(1 * sizeof(*ln_1)  ); if(ln_1 == NULL) out_of_memory();
    char** ln_2 = malloc(1 * sizeof(*ln_2)  ); if(ln_2 == NULL) out_of_memory();

    *ln_1 = malloc(200 * sizeof(char));   if(*ln_1 == NULL) out_of_memory();
    *ln_2 = malloc(200 * sizeof(char));  if(*ln_2 == NULL) out_of_memory();

    int read = 0;
    char read_str[20];
    char* nl = "\n";
    char* error = "error";
    int error_code;

    while(1){

        pos = ftell(fp_r);
        c = getc(fp_r);

        if(c == 10){
            break;
        }
        else{

            fseek(fp_r, pos, SEEK_SET);

            readline(fp_r, ln_1, 200);
            readline(fp_r, ln_2, 200);

            max_len = atoi(*ln_2);
            buff = realloc (buff, max_len * sizeof(*buff) + 1);

            error_code = find_file_descriptor(archive, *block, *ln_1, entry);

            if(  error_code >= 0 ){
                read = read_file(archive, *block, *entry, buff, max_len);

                free(*entry);

                if(read < 0){

                    free(buff);

                    free(*ln_1);
                    free(*ln_2);

                    free(ln_1);
                    free(ln_2);

                    free(entry);
                    free(*block);
                    free(block);
                    return read;
                }
            }
            else{
//                free(*entry);
                read = error_code;
            }

            sprintf(read_str, "%d", read);

            fwrite(*ln_1, strlen(*ln_1), 1, fp_w);
            fwrite(nl, strlen(nl), 1, fp_w);

            fwrite(*ln_2, strlen(*ln_2), 1, fp_w);
            fwrite(nl, strlen(nl), 1, fp_w);

            fwrite(read_str, strlen(read_str), 1, fp_w);
            if(read < 0)
                fwrite(*(errors + read + 8), strlen( *(errors + read + 8) ), 1, fp_w);
            fwrite(nl, strlen(nl), 1, fp_w);

//            if( error_code >= 0){
//                fwrite(buff, MIN(strlen(buff), 50), 1, fp_w);
//            }
//            else{
//                fwrite(error, strlen(error), 1, fp_w);
//            }
//
            fwrite(nl, strlen(nl), 1, fp_w);//fwrite(nl, strlen(nl), 1, fp_w);

        }

    }

    fclose(fp_r);
    fclose(fp_w);

    free(buff);

    free(*ln_1);
    free(*ln_2);

    free(ln_1);
    free(ln_2);

    create_copies(archive, *block, entry);
    free(entry);
    free(*block);
    free(block);

    if(
            (compare_files("../tests/results_good", "../tests/results") == 0) &&
            (compare_files("../disk/HELLO.TXT", "../disk2/HELLO.TXT") == 0) &&
            (compare_files("../disk/ZOLA.TXT", "../disk2/ZOLA.TXT") == 0) &&
            (compare_files("../disk/SPANISH/LOS.TXT", "../disk2/SPANISH/LOS.TXT") == 0) &&
            (compare_files("../disk/SPANISH/TITAN.TXT", "../disk2/SPANISH/TITAN.TXT") == 0) &&
            (compare_files("../disk/AFOLDER/ANOTHER/CANDIDE.TXT", "../disk2/AFOLDER/ANOTHER/CANDIDE.TXT") == 0)   ){

        printf("PASSED\n");
    }
    else{
        printf("FAILED\n");
    }

    return 0;
}