#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

// Include the provided font header
#include "font.h"

#define BYTES_PER_PIXEL 4
#define SLEEP_MS 300

typedef struct {
    uintptr_t phys_addr;
    uint32_t width;
    uint32_t height;
    uint32_t *fb_ptr;    // Pointer to mmapped FB
    uint32_t *line_buf;  // Local line buffer for "double buffering"
    size_t map_size;
    int scale;
} FBConfig;

// Helper to clear the entire framebuffer
void clear_fb(FBConfig *fb) {
    for (uint32_t i = 0; i < fb->width * fb->height; i++) {
        fb->fb_ptr[i] = 0x00000000;
    }
}

// Helper to clear the local line buffer
void clear_line_buffer(FBConfig *fb) {
    memset(fb->line_buf, 0, fb->width * (FONTH * fb->scale) * BYTES_PER_PIXEL);
}

// Copy the local line buffer to the specific Y position in the FB
void flush_line_buffer(FBConfig *fb, int current_y) {
    uint32_t *dest = fb->fb_ptr + (current_y * fb->width);
    memcpy(dest, fb->line_buf, fb->width * (FONTH * fb->scale) * BYTES_PER_PIXEL);
}

// Render a single character into the local line buffer with scaling
void draw_char_to_line(FBConfig *fb, int x_offset, unsigned char c) {
    for (int row = 0; row < FONTH; row++) {
        unsigned char bits = letters[font_index(c)][row];
        for (int col = 0; col < FONTW; col++) {
            // Check if the font bit is set
            if (bits & (0x80 >> col)) {
                // Draw a scale x scale block for each font pixel
                for (int sy = 0; sy < fb->scale; sy++) {
                    for (int sx = 0; sx < fb->scale; sx++) {
                        int buf_y = row * fb->scale + sy;
                        int buf_x = x_offset + (col * fb->scale) + sx;
                        // The line buffer height is FONTH * scale
                        fb->line_buf[buf_y * fb->width + buf_x] = 0xFFFFFFFF;
                    }
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {

    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Usage: %s <fb_address>,<width>,<height> <filename.txt> [scale]\n", argv[0]);
        return 1;
    }

    // Parse FB parameters
    char *params = strdup(argv[1]); // strdup because strtok modifies the string
    uintptr_t phys_addr = strtoull(strtok(params, ","), NULL, 16);
    uint32_t width = atoi(strtok(NULL, ","));
    uint32_t height = atoi(strtok(NULL, ","));
    char *filename = argv[2];
    
    int scale = 1;
    if (argc == 4) {
        scale = atoi(argv[3]);
    }
    if (scale < 1) scale = 1;

    // Open /dev/mem
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        perror("Error opening /dev/mem (run as sudo)");
        free(params);
        return 1;
    }

    // Handle page alignment for mmap
    long page_size = sysconf(_SC_PAGESIZE);
    uintptr_t base_addr = phys_addr & ~(page_size - 1);
    size_t offset = phys_addr - base_addr;
    size_t fb_size_bytes = width * height * BYTES_PER_PIXEL;
    size_t total_map_size = offset + fb_size_bytes;

    void *mapped_base = mmap(NULL, total_map_size, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, base_addr);
    if (mapped_base == MAP_FAILED) {
        perror("mmap failed");
        close(mem_fd);
        free(params);
        return 1;
    }

    FBConfig fb;
    fb.phys_addr = phys_addr;
    fb.width = width;
    fb.height = height;
    fb.fb_ptr = (uint32_t *)((uint8_t *)mapped_base + offset);
    fb.map_size = total_map_size;
    fb.scale = scale;
    
    // Allocate local buffer for one text line
    fb.line_buf = (uint32_t *)malloc(width * (FONTH * scale) * BYTES_PER_PIXEL);

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("Error opening file");
        munmap(mapped_base, total_map_size);
        free(fb.line_buf);
        free(params);
        return 1;
    }

    clear_fb(&fb);
    clear_line_buffer(&fb);

    int cur_x = 0;
    int cur_y = 0;
    int c;

    while ((c = fgetc(fp)) != EOF) {
        // Handle explicit newline
        if (c == '\n') {
            flush_line_buffer(&fb, cur_y);
            clear_line_buffer(&fb);
            cur_x = 0;
            cur_y += (FONTH * scale);
        } 
        else {
            // Check if character overflows line width
            if (cur_x + (FONTW * scale) > width) {
                flush_line_buffer(&fb, cur_y);
                clear_line_buffer(&fb);
                cur_x = 0;
                cur_y += (FONTH * scale);
            }

            // Draw character to internal line buffer
            draw_char_to_line(&fb, cur_x, (unsigned char)c);
            cur_x += (FONTW * scale);
        }

        // Check if Y overflows screen height
        if (cur_y + (FONTH * scale) > height) {
            usleep(SLEEP_MS * 1000);
            clear_fb(&fb);
            cur_y = 0;
        }
    }

    // Flush the final line if content remains
    if (cur_x > 0) {
        flush_line_buffer(&fb, cur_y);
    }

    // Cleanup
    fclose(fp);
    free(fb.line_buf);
    free(params);
    munmap(mapped_base, total_map_size);
    close(mem_fd);

    return 0;
}
