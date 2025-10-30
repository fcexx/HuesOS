void kernel_main(void) {
    print_string("Welcome to HuesOS! it works");
    for(;;);
}

void print_string(char *str) {
    unsigned char *video_memory = (unsigned char *)0xb8000;
    for(int i = 0; str[i] != '\0'; i++) {
        video_memory[i * 2] = str[i];
        video_memory[i * 2 + 1] = 0x07;
    }
}