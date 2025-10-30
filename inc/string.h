#pragma once
#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

extern struct interrupt_regs *regs;

char *strcpy(char *dest, const char *src);
int strlen(char s[]);
char *strncpy(char *dest, const char *src, size_t n);
void strncat(char *s, char c);
int strcmp(char s1[], char s2[]);
int atoi(const char *str);
char *mem_set(char *dest, int val);
void join(char s[], char n);
void memcp(char *source, char *dest, int nbytes);
void clearString(char *string);
void reverse(char s[]);
void intToString(int n, char str[]);
char *strcat(char* s, char* append);
int startsWith(char s1[], char s2[]);
void strnone(char *str);
char **splitString(const char *str, int *count);
size_t strnlen(const char *s, size_t maxlen);
int isdigit(int c);
int memcmp(const void *s1, const void *s2, size_t n);
int to_integer(const char* str);
void *memmove(void *dest, const void *src, size_t n);
char *strdup(const char *str);
int isalpha(char c);
char* tostr(int value);
int strncmp(const char *s1, const char *s2, size_t n);
char* strtok(char* str, const char* delimiters);
char* strchr(const char* str, int character);
char tolower(char s1);
int istrncmp(const char *str1, const char *str2, int n);
char *strrchr(const char *str, int character);
void remove_null_chars(char *str);
void itoa(int value, char* str, int base);
char **split(const char *str, int *count, const char delimeter);
bool contain(const char *str, char contain);
void memset(void *dest, char val, uint32_t count);
char toupper(char c);
#endif