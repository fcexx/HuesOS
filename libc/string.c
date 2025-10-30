#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

char *strcpy(char *dest, const char *src) {
    char *ptr = dest;

    while ((*ptr++ = *src++) != '\0') {
        ; // Do nothing, just iterate through the strings
    }

    return dest;
}
int strlen(char s[]) {
    int i = 0;
    while (s[i] != '\0') {
        ++i;
    }
    return i;
}
char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;

    // Copy up to n characters from src to dest
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }

    // If we copied fewer than n characters, pad the rest of dest with null bytes
    for (; i < n; i++) {
        dest[i] = '\0';
    }

    return dest;
}
void strncat(char *s, char c)
{
  int len = strlen(s);
  s[len] = c;
  s[len + 1] = '\0';
}

char *strcat(char *s, char *append)
{
	char *save = s;

	for (; *s; ++s);
	while (*s++ = *append++);
	return(save);
}

// compare string
int strcmp(char s1[], char s2[]) {
    int i;
    for (i = 0; s1[i] == s2[i]; i++) {
        if (s1[i] == '\0') return 0;
    }
    return s1[i] - s2[i];
}

char *mem_set(char *dest, int val)
{
  unsigned char *ptr = dest;

  size_t len = strlen(dest);

  while (len-- > 0)
    *ptr++ = val;
  return dest;
}
void join(char s[], char n) {
    int len = strlen(s);
    s[len] = n;
    s[len + 1] = '\0';
}
void memcp(char *source, char *dest, int nbytes) {
    int i;
    for (i = 0; i < nbytes; i++) {
        *(dest + i) = *(source + i);
    }
}

void clearString(char *string)
{

  for (int i = strlen(string); i = 0; i--)
  {

    string[i] = 0x00;
  }
}

void reverse(char s[]) {
    int c, i, j;
    for (i = 0, j = strlen(s)-1; i < j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

void intToString(int n, char str[]) {
    int i, sign;
    if ((sign = n) < 0) n = -n;
    i = 0;
    do {
        str[i++] = n % 10 + '0';
    } while ((n /= 10) > 0);

    if (sign < 0) str[i++] = '-';
    str[i] = '\0';

    reverse(str);
}
int startsWith(char s1[], char s2[]) {
    int i;
    for (i = 0; s2[i] != '\0'; i++) {
        if (s1[i] != s2[i]) return 0;
    }
    return 1;
}

void strnone(char *str)
{
    for (int i = 0; i < strlen(str); i++)
    {
        str[i] = 0;
    }
    
}

char **splitString(const char *str, int *count) {
    /* Static buffer and array of pointers – one allocation for the entire time
       works, rewritten on each call.  
       Limitations: maximum 64 words of 64 bytes each. */
    static char words_buf[64][64];
    static char *result[64];

    int n = 0;
    const char *ptr = str;

    while (*ptr && n < 64) {
        while (*ptr == ' ') ptr++;          /* skip spaces */
        if (!*ptr) break;

        char *dst = words_buf[n]; int len = 0;
        while (*ptr && *ptr != ' ' && len < 63) {
            dst[len++] = *ptr++;
        }
        dst[len] = 0;
        result[n] = dst;
        n++;
    }

    *count = n;
    return result;
}

size_t strnlen(const char *s, size_t maxlen) {
    size_t len;
    for (len = 0; len < maxlen; len++) {
        if (s[len] == '\0') {
            break;
        }
    }
    return len;
}

int isdigit(int c) {
    return (c >= '0' && c <= '9');
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1, *p2 = s2;
    
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }
    
    return 0;
}

int atoi(const char *str) {
    int result = 0;
    int sign = 1;
    int i = 0;

    // Handle whitespace
    while (str[i] == ' ' || str[i] == '\t' || str[i] == '\n') {
        i++;
    }

    // Handle sign
    if (str[i] == '-' || str[i] == '+') {
        sign = (str[i] == '-') ? -1 : 1;
        i++;
    }

    // Process digits
    while (str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (str[i] - '0');
        i++;
    }

    return sign * result;
}

int to_integer(const char* str) {
    return atoi(str);
}

#define MAX_HISTORY 1024
#define MAX_COMMAND_LENGTH 1024

void *memmove(void *dest, const void *src, size_t n) {
    // Cast pointers to char* for working with bytes
    char *d = (char *)dest;
    const char *s = (const char *)src;

    // If the memory areas do not overlap, just copy
    if (d < s || d >= s + n) {
        // Copy from the beginning to the end
        while (n--) {
            *d++ = *s++;
        }
    } else {
        // If the areas overlap, copy from the end
        d += n;
        s += n;
        while (n--) {
            *(--d) = *(--s);
        }
    }

    return dest; // Return the pointer to the target area
}

int isalpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

int strncmp(const char *s1, const char *s2, size_t n) {
    // If the length for comparison is 0, strings are considered equal
    if (n == 0)
        return 0;

    // Compare characters one by one up to n characters
    while (n-- > 0) {
        // If the current characters are not equal, return their difference
        if (*s1 != *s2) {
            return *(unsigned char *)s1 - *(unsigned char *)s2;
        }

        // If the end of one of the strings is reached, stop the comparison
        if (*s1 == '\0') {
            return 0;
        }

        // Go to the next characters
        s1++;
        s2++;
    }

    // If all characters up to n are equal
    return 0;
}

char* strtok(char* str, const char* delimiters) {
    static char* last_token = NULL;
    
    // If str is not NULL, start a new tokenization process
    // If NULL, continue from the last position
    if (str != NULL) {
        last_token = str;
    } else if (last_token == NULL) {
        return NULL;
    }
    
    // Skip initial delimiters
    while (*last_token != '\0' && strchr(delimiters, *last_token) != NULL) {
        last_token++;
    }
    
    // If the end of the string is reached, return NULL
    if (*last_token == '\0') {
        last_token = NULL;
        return NULL;
    }
    
    // Start of the current token
    char* token_start = last_token;
    
    // Find the end of the current token
    while (*last_token != '\0' && strchr(delimiters, *last_token) == NULL) {
        last_token++;
    }
    
    // If the delimiter is found, replace it with '\0' and save the position
    if (*last_token != '\0') {
        *last_token = '\0';
        last_token++;
    } else {
        // If the end of the string is reached, reset the pointer
        last_token = NULL;
    }
    
    return token_start;
}

// Helper function strchr (if it's not there)
char* strchr(const char* str, int character) {
    while (*str != '\0') {
        if (*str == character) {
            return (char*)str;
        }
        str++;
    }
    if (character == '\0') {
        return (char*)str;
    }
    return NULL;
}

char tolower(char s1) {
  if (s1 >= 64 && s1 <= 90) {
    s1 += 32;
  }

  return s1;
}

int istrncmp(const char *str1, const char *str2, int n) {
  unsigned char u1, u2;

  while (n--) {
    u1 = (unsigned char)*str1++;
    u2 = (unsigned char)*str2++;

    if (u1 != u2 && tolower(u1) != tolower(u2)) {
      return u1 - u2;
    }

    if (u1 == 0) {
      return 0;
    }
  }

  return 0;
}

char *strrchr(const char *str, int character) {
    const char *last_occurrence = NULL; // Pointer to the last occurrence of the character
    while (*str) {
        if (*str == (char)character) {
            last_occurrence = str; // Update the pointer if the character is found
        }
        str++; // Go to the next character
    }
    return (char *)last_occurrence; // Return the last occurrence or NULL
}

void remove_null_chars(char *str) {
    char *src = str; // Pointer to the original string
    char *dst = str; // Pointer to the place for the result

    while (*src) { // Until the end of the string is reached
        if (*src != '\0') { // If the current character is not '\0'
            *dst++ = *src; // Copy it to the resulting string
        }
        src++; // Go to the next character
    }
    *dst = '\0'; // End the resulting string with a null character
}

void itoa(int value, char* str, int base) {
    int i = 0;
    int is_negative = 0;

    // Handle 0 explicitly
    if (value == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    // Handle negative numbers
    if (value < 0 && base == 10) {
        is_negative = 1;
        value = -value;
    }

    // Process each digit
    while (value != 0) {
        int rem = value % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'A' : rem + '0'; // Convert to character
        value = value / base;
    }

    // If the number is negative, append '-'
    if (is_negative) {
        str[i++] = '-';
    }

    str[i] = '\0';

    // Reverse the string
    reverse(str);
}

char **split(const char *str, int *count, const char delimeter) {
    // First, count the number of words
    int n = 0;
    const char *ptr = str;
    while (*ptr) {
        // Skip spaces
        while (*ptr == delimeter) {
            ptr++;
        }
        if (*ptr) {
            n++; // Found a word
            // Skip the word
            while (*ptr && *ptr != delimeter) {
                ptr++;
            }
        }
    }

    // Allocate memory for the array of strings
    char *result[n * sizeof(char *)];
    // Fill the array with words
    int index = 0;
    ptr = str;
    while (*ptr) {
        // Skip spaces
        while (*ptr == delimeter) {
            ptr++;
        }
        if (*ptr) {
            const char *start = ptr;
            // Find the end of the word
            while (*ptr && *ptr != delimeter) {
                ptr++;
            }
            // Allocate memory for the word and copy it
            int length = ptr - start;
            if (!result[index]) {
                return NULL; // Memory allocation error
            }
            strncpy(result[index], start, length);
            result[index][length] = '\0'; // End the string with a null character
            index++;
        }
    }

    *count = n; // Return the number of found words
    return result;
}

bool contain(const char *str, char contain)
{
    int i = 0;
    while (str[i] != '\0') {
        if (strcmp(str[i], contain) == 0) return true;
    } 
    return false;  
}

void *memcpy(const void *src, void *dest, uint32_t n)
{
    const uint8_t *s = (const uint8_t*)src;
    uint8_t *d = (uint8_t*)dest;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void memset(void *dest, char val, uint32_t count)
{
    char *temp = (char*) dest;
    for (; count != 0; count --){
        *temp++ = val;
    }
}

char toupper(char c) {
    if (c >= 'a' && c <= 'z') return c - ('a' - 'A');
    return c;
}

int isspace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack; // Пустая строка — всегда совпадение

    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (!*n) return (char*)haystack; // Нашли совпадение
    }
    return NULL; // Не найдено
}

int strncasecmp(const char* s1, const char* s2, int n) {
    for (int i = 0; i < n; i++) {
        char c1 = s1[i], c2 = s2[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 += 'a' - 'A';
        if (c2 >= 'A' && c2 <= 'Z') c2 += 'a' - 'A';
        if (c1 != c2) return c1 - c2;
        if (c1 == 0) break;
    }
    return 0;
}