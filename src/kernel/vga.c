#include "vga.h"
#include "types.h"

static uint16_t* const VGA_BUFFER = (uint16_t*) 0xB8000;
static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static uint16_t* terminal_buffer;

void vga_init()
{
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_buffer = VGA_BUFFER;
}

void vga_clear()
{
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }
}

void vga_write_string(const char* str, size_t row, size_t col, uint8_t color)
{
    size_t i = 0;
    while (str[i] != '\0') {
        const size_t index = row * VGA_WIDTH + col + i;
        terminal_buffer[index] = vga_entry(str[i], color);
        i++;
    }
}
