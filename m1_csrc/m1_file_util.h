/* See COPYING.txt for license details. */

/*
 * m1_file_util.h
 */

#ifndef M1_FILE_UTIL_H_
#define M1_FILE_UTIL_H_

void fu_get_filename_without_ext(const char *path, char *outName, size_t outSize);
const char* fu_get_filename(const char *path);
const char* fu_get_file_extension(const char *filename);
void fu_get_directory_path(const char *fullPath, char *outDir, size_t dirSize);
void fu_path_combine(char *out, size_t outSize, const char *path, const char *file);
int fs_file_exists(const char *path);
int fs_directory_exists(const char *path);
FRESULT fs_directory_ensure(const char *path);
FRESULT fs_get_free_space(uint64_t *pFree);
FRESULT fs_save_file_safe(const char *path, const void *buf, uint32_t size);

#endif /* M1_FILE_UTIL_H_ */
