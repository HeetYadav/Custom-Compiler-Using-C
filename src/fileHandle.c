#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILENAME 256

char* read_hehee_file(const char* filepath, long* file_size) {
    // Check .hehee extension
    const char* ext = strrchr(filepath, '.');
    if (!ext || strcmp(ext, ".hehee") != 0) {
        fprintf(stderr, "Error: File must have a .hehee extension\n");
        exit(1);
    }

    // Open the file
    FILE* file = fopen(filepath, "r");
    if (!file) {
        fprintf(stderr, "Error: File '%s' not found.\n", filepath);
        exit(1);
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    *file_size = ftell(file);
    rewind(file);

    // Allocate buffer
    char* buffer = (char*)malloc(*file_size + 1);
    if (!buffer) {
        fprintf(stderr, "Error: Memory allocation failed.\n");
        fclose(file);
        exit(1);
    }

    // Read file into buffer
    size_t bytes_read = fread(buffer, 1, *file_size, file);
    buffer[bytes_read] = '\0';

    fclose(file);
    return buffer;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <filename>.hehee\n", argv[0]);
        return 1;
    }

    long file_size;
    char* source_code = read_hehee_file(argv[1], &file_size);

    printf("Successfully read '%s'\n", argv[1]);
    printf("File size: %ld characters\n\n", file_size);

    // ---- Plug in your lexer here ----
    // your_lexer(source_code);

    // For now, just print the raw source
    printf("---- Source Code ----\n");
    printf("%s\n", source_code);

    // Free allocated memory
    free(source_code);
    return 0;
}