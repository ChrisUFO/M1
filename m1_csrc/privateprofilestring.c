/*
 This file is part of OpenLogos/LogOSMaTrans.  Copyright (C) 2005 Globalware AG
  
 OpenLogos/LogOSMaTrans has two licensing options:
  
 The Commercial License, which allows you to provide commercial software
 licenses to your customers or distribute Logos MT based applications or to use
 LogOSMaTran for commercial purposes. This is for organizations who do not want
 to comply with the GNU General Public License (GPL) in releasing the source
 code for their applications as open source / free software.
  
 The Open Source License allows you to offer your software under an open source
 / free software license to all who wish to use, modify, and distribute it
 freely. The Open Source License allows you to use the software at no charge
 under the condition that if you use OpenLogos/LogOSMaTran in an application you
 redistribute, the complete source code for your application must be available
 and freely redistributable under reasonable conditions. GlobalWare AG bases its
 interpretation of the GPL on the Free Software Foundation's Frequently Asked
 Questions.
  
 OpenLogos is distributed in the hope that it will be useful, but WITHOUT ANY
 WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 PARTICULAR PURPOSE.  See the GNU General Public License for more details.
  
 You should have received a copy of the License conditions along with this
 program. If not, write to Globalware AG, Hospitalstra�e 6, D-99817 Eisenach.
  
 Linux port modifications and additions by Bernd Kiefer, Walter Kasper,
 Deutsches Forschungszentrum fuer kuenstliche Intelligenz (DFKI)
 Stuhlsatzenhausweg 3, D-66123 Saarbruecken
 */
 /* -*- Mode: C++ -*- */
  
 /***************************************************************************
  PORTABLE ROUTINES FOR WRITING PRIVATE PROFILE STRINGS --  by Joseph J. Graf
  Header file containing prototypes and compile-time configuration.
 ***************************************************************************/
  
/*************************** I N C L U D E S **********************************/
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>

#include "stm32h5xx_hal.h"
#include "main.h"

#include "lfrfid.h"
#include "m1_file_util.h"
#include "ff.h"
#include "ff_gen_drv.h"
#include "privateprofilestring.h"

/*************************** D E F I N E S ************************************/
 #define DEFUALT_LINE_LENGTH    (512)

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

static size_t g_linebuf_size = DEFUALT_LINE_LENGTH;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/
char* ltrim(char *str);
char* rtrim(char *str);
char* trim(char *str);

static int read_line(FIL *fp, char *buf, int size);

static int stricmp_nocase(const char *a, const char *b);
static int parse_hex_array_space(const char *str, uint8_t *out, int max_len);
static bool parse_bool_text(const char *str, bool *out);
static int parse_hex_count(const char *str);
static bool parse_value(const char *text, ParsedValue *val);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void set_line_buffer_size(size_t size)
{
	if(size)
		g_linebuf_size = size;
}


/*****************************************************************
* Function:     trim()
* Arguments:
*               <char *> str - a pointer to the copy buffer
* Returns:
******************************************************************/
char* ltrim(char *str)
{
    char *p = str;
    if (str == NULL) return NULL;

    while (*p && isspace((unsigned char)*p))
        p++;

    if (p != str)
        memmove(str, p, strlen(p) + 1);

    return str;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
char* rtrim(char *str)
{
    char *end;

    if (str == NULL) return NULL;

    end = str + strlen(str);

    while (end > str && isspace((unsigned char)*(end - 1)))
        end--;

    *end = '\0';
    return str;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
char* trim(char *str)
{
    if (str == NULL) return NULL;

    rtrim(str);
    ltrim(str);

    return str;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool IsValidFileSpec(const S_M1_file_info *f, const char* ext)
{
	uint8_t uret;
	const char *ext1;

	uret = strlen(f->file_name);
	if ( !uret )
		return false;

	ext1 = fu_get_file_extension(f->file_name);

	if ( ext1 == 0 || strcmp(ext1, ext) )
		return false;

	return true;
}


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
bool isValidHeaderField(ParsedValue *data, const char* filetype, const char* version, const char *file_path)
{
	if(data == NULL || filetype == NULL || version == NULL || file_path == NULL){
		return false;
	}

	GetPrivateProfileString(data,"Filetype", file_path);
	if(strcmp(data->buf, filetype))
		return false;

	GetPrivateProfileString(data,"Version", file_path);
	//if(data->v.f != 0.8)
	if(strcmp(data->buf, version))
		return false;

	return true;
}


/*============================================================================*/
 /**
  * @brief Reads a single line from a file using FatFS f_gets (handles CR/LF/CRLF).
  *
  * This function reads one line of text from the specified FatFS file object.
  * It supports line termination by '\n', '\r', or a combination of both ("\r\n").
  * The resulting line is stored in the provided buffer without trailing
  * newline characters.
  *
  * @param fp    Pointer to the FatFS file object.
  * @param buf   Buffer to store the resulting line (null-terminated string).
  * @param size  Size of the buffer in bytes.
  *
  * @return
  * - 1 if a line was successfully read
  * - 0 if end-of-file was reached or no more lines are available
  */
/*============================================================================*/
 int read_line(FIL *fp, char *buf, int size)
 {
     char *p;

     if (size <= 0) return 0;

     p = f_gets(buf, size, fp);

     if (p == NULL) {
         return 0; // EOF or error
     }

     int len = strlen(buf);

     if (len > 0)
     {
         if (buf[len-1] == '\n') buf[--len] = '\0';
         if (len > 0 && buf[len-1] == '\r') buf[--len] = '\0';
     }

     return 1;
 }


 /************************************************************************
 * Function:     get_private_profile_int()
 * Arguments:    <char *> section - the name of the section to search for
 *               <char *> entry - the name of the entry to find the value of
 *               <int> def - the default value in the event of a failed read
 *               <char *> file_name - the name of the .ini file to read from
 * Returns:      the value located at entry
 *************************************************************************/
#define TOKEN_COLON ':'

 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 /* ================================
  *  ?�?�문??무시 문자??비교 (strcasecmp ?��?
  * ================================ */
 static int stricmp_nocase(const char *a, const char *b)
 {
     unsigned char ca, cb;

     if (a == NULL || b == NULL) {
         return (a == b) ? 0 : (a ? 1 : -1);
     }

     while (*a && *b) {
         ca = (unsigned char)tolower((unsigned char)*a);
         cb = (unsigned char)tolower((unsigned char)*b);
         if (ca != cb) return (int)ca - (int)cb;
         a++;
         b++;
     }

     ca = (unsigned char)tolower((unsigned char)*a);
     cb = (unsigned char)tolower((unsigned char)*b);
     return (int)ca - (int)cb;
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 /* ================================
  *  ?��? ?�퍼: 공백 구분 Hex 문자????배열
  *  ?? "36 00 88 F8 82" ??{0x36,0x00,0x88,0xF8,0x82}
  * ================================ */
 static int parse_hex_array_space(const char *str, uint8_t *out, int max_len)
 {
     int count = 0;
     char token[16];

     if (str == NULL || out == NULL || max_len <= 0)
         return 0;

     while (*str && count < max_len) {

         /* ?�쪽 공백 ?�킵 */
         while (*str == ' ')
             str++;

         if (*str == '\0')
             break;

         /* ?�큰 ?�나 복사 */
         int t = 0;
         while (*str != ' ' && *str != '\0' && t < (int)sizeof(token) - 1) {
             token[t++] = *str++;
         }
         token[t] = '\0';

         /* 16진수�?변??*/
         unsigned int value;
         if (sscanf(token, "%x", &value) == 1) {
             out[count++] = (uint8_t)value;
         }
         /* ?�못???�큰?� 그냥 ?�킵 */
     }

     return count; // 변?�된 바이????
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 /* ================================
  *  ?��? ?�퍼: bool ?�스???�싱
  *  지?? "1","0","true","false","on","off" (?�?�문??무시)
  * ================================ */
 static bool parse_bool_text(const char *str, bool *out)
 {
     if (str == NULL || out == NULL) return false;

     if (stricmp_nocase(str, "1") == 0 ||
         stricmp_nocase(str, "true") == 0 ||
         stricmp_nocase(str, "on") == 0) {
         *out = true;
         return true;
     }

     if (stricmp_nocase(str, "0") == 0 ||
         stricmp_nocase(str, "false") == 0 ||
         stricmp_nocase(str, "off") == 0) {
         *out = false;
         return true;
     }

     return false;
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 static int parse_hex_count(const char *str)
 {
     int count = 0;
#if 0
     while (*str) {

         /* ?�쪽 공백 ?�킵 */
         while (*str == ' ')
             str++;

         /* 종료 */
         if (*str == '\0')
             break;

         /* ?�나???�큰 ?�작 ??개수 증�? */
         count++;

         /* ?�음 공백 ?�는 종료까�? ?�동 */
         while (*str != ' ' && *str != '\0')
             str++;
     }
#else
     char *tok = strtok((char*)str, " ");

     while (tok != NULL) {
    	 count++;
    	 tok = strtok(NULL, " ");
     }
#endif
     return count;
 }


 /* ================================
  *  ?�합 ?�서: parse_value()
  *
  *  text:  ?�싱??문자??(?? "36 00 88 F8 82", "123", "true", "3.14")
  *  val :  type / buf / max_len ?�정 ???�출
  *
  *  - VALUE_TYPE_HEX_ARRAY:
  *      buf     : uint8_t* 버퍼
  *      max_len : buf ?�기
  *      결과    : buf??바이??채우�? v.hex.out_len ??개수 ?�??
  *
  *  - VALUE_TYPE_STRING:
  *      buf     : char* 버퍼
  *      max_len : 버퍼 ?�기
  *      결과    : buf??문자??복사 (null-terminated)
  *
  *  반환�? true = ?�공, false = ?�패
  * ================================ */
 bool parse_value(const char *text, ParsedValue *val)
 {
     if (text == NULL || val == NULL)
         return false;

     switch (val->type)
     {
     case VALUE_TYPE_HEX_ARRAY:
         if (val->buf == NULL || val->max_len <= 0)
             return false;
         val->v.hex.out_len = parse_hex_array_space(
                                 text,
                                 (uint8_t*)val->buf,
                                 val->max_len);
         return (val->v.hex.out_len > 0);

     case VALUE_TYPE_INT:
     {
         int tmp;
         if (sscanf(text, "%d", &tmp) == 1) {
             val->v.i32 = tmp;
             return true;
         }
         return false;
     }

     case VALUE_TYPE_UINT32:
     {
         unsigned int tmp;
         if (sscanf(text, "%u", &tmp) == 1) {
             val->v.u32 = (uint32_t)tmp;
             return true;
         }
         return false;
     }

     case VALUE_TYPE_BOOL:
         return parse_bool_text(text, &val->v.b);
#if 1
     case VALUE_TYPE_FLOAT:
     {
    	 char *endptr;
         float f;
         errno = 0;

         f = strtod(text,&endptr);

         if (endptr == text) {
             // 유효한 숫자를 찾지 못했습니다.
             return false;
         }

         if (errno == ERANGE) {
             // 범위 초과 오류 발생
             return false;
         }

         if (*endptr != '\0') {
             //ex : "123.45abc")
             return false;
         }

         val->v.f = trunc(f * 1000.0) / 1000.0;
         return true;
#if 0
         if (sscanf(text, "%f", &f) == 1) {
             val->v.f = f;
             return true;
         }
         return false;
#endif
     }
#endif
     case VALUE_TYPE_STRING:
         if (val->buf == NULL || val->max_len <= 0)
             return false;
         strncpy((char*)val->buf, text, val->max_len - 1);
         ((char*)val->buf)[val->max_len - 1] = '\0';
         return true;

     case VALUE_TYPE_COUNT:
    	 val->v.hex.out_len = parse_hex_count(text);
    	 return true;
     }

     return false;
 }


 /************************************************************************
 * Function:     get_private_profile()
 * Arguments:    <char *> section - the name of the section to search for
 *               <char *> entry - the name of the entry to find the value of
 *               <int> def - the default value in the event of a failed read
 *               <char *> file_name - the name of the .ini file to read from
 * Returns:      the value located at entry
 *************************************************************************/
 int get_private_profile(ParsedValue *val, const char *entry, const char *file_name)
 {
	FIL fp;
    //char buff[MAX_LINE_LENGTH];
	char *line_buff;
    char *ep;
    //char value[6];
    int len = strlen(entry);
    //int i;

    if( f_open(&fp, file_name,FA_READ) != FR_OK )
		return(0);
#if 1
    line_buff = malloc(g_linebuf_size);
    if(line_buff == NULL)
    	return 0;
#endif
    /* Now that the section has been found, find the entry. */
    do
    {
    	if( !read_line(&fp,line_buff,g_linebuf_size))
        {
#if 1
    		safe_free((void*)&line_buff);
#endif
      		f_close(&fp);
            return (0); // EOF???�달, ?��? 찾�? 못함
        }

        if (line_buff[0] == '#') {
          	continue; // 주석 무시
        }

        // --- ?�� 콜론 기반???�확????검??로직 ?�용 ?�� ---

        char *colon_pos = strchr(line_buff, TOKEN_COLON);

        if (colon_pos != NULL) {

        	// 1. 콜론 ?�치????문자(\0)�??�어 ??문자?�을 ?�시 분리
        	*colon_pos = '\0';

            // 2. ??부분의 ?�뒤 공백???�거
            char *current_key = trim(line_buff);

            // 3. 추출???��? 검?�하?�는 ?��? ?�확???�치?�는지 ?�인 (strcmp ?�용)
            if (strlen(current_key) == len && !strcmp(current_key, entry)) {

            // 4. ?��? 찾았?��?�? 콜론??복원?�고 루프 ?�출
            	*colon_pos = TOKEN_COLON;
                break;
            }
            // 5. 불일�???콜론 복원?�고 ?�음 줄로 진행
            *colon_pos = TOKEN_COLON;
        }
    }while( 1 ); // ?��? 찾을 ?�까지 무한 반복

    ep = strrchr(line_buff,TOKEN_COLON);    /* 구분???��???(?�재 줄에 콜론???�음??보장) */

    // ?�전??콜론???�었지�??�시 모�? ?�러 처리
    if (ep == NULL) {
#if 1
    	safe_free((void*)&line_buff);
#endif
    	f_close(&fp);
    	return 0;
    }

    ep++; // �??�작 ?�치
    if( !strlen(ep) )          /* 값이 ?�는 경우 */
    {
#if 1
    	safe_free((void*)&line_buff);
#endif
      	f_close(&fp);
        return(0);
    }
    /* Copy only numbers fail on characters */

	char *psz = trim((char*)ep);
	strcpy(line_buff,psz);
	//safe_debugprintf("search value =%s\r\n", buff);

	parse_value(line_buff, val);

#if 1
    safe_free((void*)&line_buff);
#endif
    f_close(&fp);                /* Clean up and return the value */

	return 1;
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 int get_private_profile_hex_count(ParsedValue *val, const char *entry, const char *file_name)
 {
	val->type = VALUE_TYPE_COUNT;
	return get_private_profile(val, entry, file_name);
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 int get_private_profile_hex(ParsedValue *val, const char *entry, const char *file_name)
 {
	val->type = VALUE_TYPE_HEX_ARRAY;
	return get_private_profile(val, entry, file_name);
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 int get_private_profile_uint(ParsedValue *val, const char *entry, const char *file_name)
 {
	val->type = VALUE_TYPE_UINT32;
	return get_private_profile(val, entry, file_name);
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 int get_private_profile_int(ParsedValue *val, const char *entry, const char *file_name)
 {
	val->type = VALUE_TYPE_INT;
	return get_private_profile(val, entry, file_name);
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 int get_private_profile_string(ParsedValue *val, const char *entry, const char *file_name)
 {
	val->type = VALUE_TYPE_STRING;
	return get_private_profile(val, entry, file_name);
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 int get_private_profile_bool(ParsedValue *val, const char *entry, const char *file_name)
 {
	val->type = VALUE_TYPE_BOOL;
	return get_private_profile(val, entry, file_name);
 }


 /*============================================================================*/
 /**
   * @brief
   * @param
   * @retval
   */
 /*============================================================================*/
 int get_private_profile_float(ParsedValue *val, const char *entry, const char *file_name)
 {
	val->type = VALUE_TYPE_FLOAT;
	return get_private_profile(val, entry, file_name);
 }

  
 /***** Routine for writing private profile strings --- by Joseph J. Graf *****/
    
 /*************************************************************************
  * Function:    write_private_profile_string()
  * Arguments:   <char *> section - the name of the section to search for
  *              <char *> entry - the name of the entry to find the value of
  *              <char *> buffer - pointer to the buffer that holds the string
  *              <char *> file_name - the name of the .ini file to read from
  * Returns:     TRUE if successful, otherwise FALSE
  *************************************************************************/
 int write_private_profile_string(const char *entry, const char *buffer, const char *file_name)
{  
	FIL rfp, wfp;
    char tmp_name[] = "temp.rfid";
    //char buff[MAX_LINE_LENGTH];
    char *line_buff;
    int len = strlen(entry);
	
	 if ((f_open(&rfp, file_name, FA_READ)) != FR_OK)	
    {  
		if ((f_open(&wfp, file_name, FA_CREATE_NEW|FA_WRITE)) != FR_OK)
        {   return(0);   }
		
		f_printf(&wfp,"%s: %s\n",entry,buffer);
		f_close(&wfp);
		return(1);
    }

	if ((f_open(&wfp, tmp_name, FA_CREATE_ALWAYS|FA_WRITE)) != FR_OK)
    {   
		f_close(&rfp);	
		return(0);   
	}	
     /* Move through the file one line at a time until a section is
      * matched or until EOF. Copy to temp file as it is read. */
	// serach key - write value
     /* Now that the section has been found, find the entry. Stop searching
      * upon leaving the section's area. Copy the file as it is read
      * and create an entry if one is not found.  */
#if 1
    line_buff = malloc(g_linebuf_size);
    if(line_buff == NULL)
    	return 0;
#endif

     while( 1 )
     {
    	if( !read_line(&rfp,line_buff,g_linebuf_size) )
        {   /* EOF without an entry so make one */
            f_printf(&wfp,"%s: %s\n",entry,buffer);
            /* Clean up and rename */
#if 1
            safe_free((void*)&line_buff);
#endif
            f_close(&rfp);
            f_close(&wfp);
            f_unlink(file_name);
            f_rename(tmp_name,file_name);
            return(1);
        }
  
     	if (line_buff[0] == '#') {
     		f_printf(&wfp, "%s\n", line_buff);
            continue;
        }

     	// 2. ?�재 줄에??콜론(:)???�치�?찾습?�다. (TOKEN_COLON ?�용)
     	char *colon_pos = strchr(line_buff, TOKEN_COLON);

     	// 3. 콜론??존재?�고, 콜론 ?�에 ?��? ?�다�?
     	if (colon_pos != NULL) {

     		// 4. 콜론 ??문자?�을 분리?�니??
     	    //    (콜론 ?�치????문자(\0)�??�어 문자?�을 ?�시�??�름)
     	    *colon_pos = '\0';

     	    // 5. 콜론 ?�의 ??문자?�의 ?�뒤 공백???�거?�니??
     	    char *current_key = trim(line_buff);

     	    // 6. ?�재 ?�의 길이?� 검?�하?�는 ?�의 길이가 같고, ???��? ?�확???�치?�는지 ?�인?�니??
     	    if (strlen(current_key) == len && !strcmp(current_key, entry)) {

     	    // 7. 루프 ?�출 ?�에 콜론???�시 복원?�줍?�다 (값을 추출?�야 ?��?�?.
     	    	*colon_pos = TOKEN_COLON;
     	        break; // ?�확???�치?�는 ?��? 찾았?��?�??�출
     	    }

     	    // 8. ?�치?��? ?�으�???문자�??�시 콜론?�로 복원?�고 ?�음 줄로 진행?�니??
     	    *colon_pos = TOKEN_COLON;
     	}

     	// 9. �?�?처리 (콜론???�거???��? 불일�???
     	if (line_buff[0] == '\0') {
     		break; // EOF 처리�??�어가 ????�� 추�?
     	}

     	f_printf(&wfp,"%s\n",line_buff); // ?��? 찾�? 못한 줄�? ?�시 ?�일??복사
    }
  
    if( line_buff[0] == '\0' )
    {   f_printf(&wfp,"%s: %s\n",entry,buffer);
        do
        {
            f_printf(&wfp,"%s\n",line_buff);
        } while( read_line(&rfp,line_buff,g_linebuf_size) );
    }
    else
    {   f_printf(&wfp,"%s: %s\n",entry,buffer);
        while( read_line(&rfp,line_buff,g_linebuf_size) )
        {
            f_printf(&wfp,"%s\n",line_buff);
        }
    }

    /* Clean up and rename */
#if 1
    safe_free((void*)&line_buff);
#endif
    f_close(&wfp);
    f_close(&rfp);
    f_unlink(file_name);
    f_rename(tmp_name,file_name);
    return(1);
 }
  
 #undef MAX_LINE_LENGTH

