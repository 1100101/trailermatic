/*
 * file.h
 *
 *  Created on: Oct 12, 2008
 *      Author: kylek
 */

#ifndef FILE_H_
#define FILE_H_

#include <stdint.h>

char* readFile(const char *fname, uint32_t * setme_len);
int saveFile(const char *name, const void *data, uint32_t size);
int8_t file_exists(const char *filename);
void get_filename(char *filename, const char *content_filename, const char *url, const char *tm_path);

#endif /* FILE_H_ */
