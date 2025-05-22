#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"

#include <u8g2.h>
#include "u8g2_esp32_hal.h"

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define MAP_WIDTH 20
#define MAP_HEIGHT 10

#define LEFT_BUTTON  16
#define DOWN_BUTTON  17
#define UP_BUTTON    18
#define RIGHT_BUTTON 19

#define PIN_SDA 21
#define PIN_SCL 22


typedef struct snake_node
{
    struct snake_node* next;
    short int x;
    short int y;
    short int next_direction;
    bool eaten;
    
} snake_node;

typedef enum direction
{
    LEFT, DOWN, RIGHT, UP
} direction;

static u8g2_t u8g2;
static u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
static bool snake_map[MAP_HEIGHT][MAP_WIDTH];
int snake_highscore = 0;

void init_buttons()
{
    int buttons[] = {LEFT_BUTTON, DOWN_BUTTON, UP_BUTTON, RIGHT_BUTTON};
    for(int i = 0; i < sizeof(buttons)/sizeof(buttons[0]); i++)
    {
        gpio_reset_pin(buttons[i]);
        gpio_set_direction(buttons[i], GPIO_MODE_INPUT);
        gpio_pullup_en(buttons[i]);    // Enable internal pull-up resistor
        gpio_pulldown_dis(buttons[i]); // Disable internal pull-down resistor
    }
}

void init_display()
{
    u8g2_esp32_hal.bus.i2c.sda = PIN_SDA;
    u8g2_esp32_hal.bus.i2c.scl = PIN_SCL;
    u8g2_esp32_hal_init(u8g2_esp32_hal);


    u8g2_Setup_sh1106_i2c_128x64_noname_f(&u8g2, U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb); 
    
    u8x8_SetI2CAddress(&u8g2.u8x8, 0x78);
    u8g2_InitDisplay(&u8g2);  // initialize display, display is in sleep mode after this
    u8g2_SetPowerSave(&u8g2, 0);  // wake up display
    u8g2_ClearBuffer(&u8g2);
    u8g2_SendBuffer(&u8g2);
}

snake_node* init_snake()
{
    snake_node* snake_segment1 = (snake_node*)malloc(sizeof(snake_node));
    snake_node* snake_segment2 = (snake_node*)malloc(sizeof(snake_node));
    snake_node* snake_segment3 = (snake_node*)malloc(sizeof(snake_node));
    snake_node* snake_segment4 = (snake_node*)malloc(sizeof(snake_node));
    
    snake_segment1->x = 12; snake_segment1->y = 5; snake_segment1->eaten = false;
    snake_segment2->x = 11; snake_segment2->y = 5; snake_segment2->eaten = false;
    snake_segment3->x = 10; snake_segment3->y = 5; snake_segment3->eaten = false;
    snake_segment4->x = 9;  snake_segment4->y = 5; snake_segment4->eaten = false;

    snake_segment1->next = snake_segment2; snake_segment1->next_direction = LEFT; 
    snake_segment2->next = snake_segment3; snake_segment2->next_direction = LEFT;
    snake_segment3->next = snake_segment4; snake_segment3->next_direction = LEFT;
    snake_segment4->next = NULL;           snake_segment4->next_direction = LEFT;

    snake_map[5][9] = true;  snake_map[5][10] = true;
    snake_map[5][11] = true; snake_map[5][12] = true;

    return snake_segment1;
}

void free_snake_memory(snake_node* snake_head)
{
    snake_node* prev = snake_head;
    while(snake_head)
    {
        snake_head = snake_head->next;
        free(prev);
        prev = snake_head;
    }
}

snake_node* add_snake_segment(snake_node* snake_head, direction snake_direction)
{
    snake_node* new_head = (snake_node*)malloc(sizeof(snake_node));
    new_head->next = snake_head;
    new_head->x = snake_head->x;
    new_head->y = snake_head->y;
    new_head->eaten = false;
    snake_head = new_head;
            
    switch (snake_direction)
    {
    case LEFT:
        snake_head->x--;
        if(snake_head->x < 0)
            snake_head->x = MAP_WIDTH - 1;
        snake_head->next_direction = RIGHT;
        break;
    case DOWN:
        snake_head->y--;
        if(snake_head->y < 0)
            snake_head->y = MAP_HEIGHT - 1;
        snake_head->next_direction = UP;
        break;
    case UP:
        snake_head->y++;
        if(snake_head->y >= MAP_HEIGHT)
            snake_head->y = 0;
        snake_head->next_direction = DOWN;
        break;
    case RIGHT:
        snake_head->x++;
        if(snake_head->x >= MAP_WIDTH)
            snake_head->x = 0;
        snake_head->next_direction = LEFT;
        break;
    }
    snake_map[snake_head->y][snake_head->x] = true;
    
    return snake_head;
}

void pop_last_segment(snake_node* snake_head)
{
    snake_node* curr = snake_head;
    snake_node* prev = snake_head;
    while(curr->next)
    {
        prev = curr;
        curr = curr->next;
    }
    snake_map[curr->y][curr->x] = false;
    free(curr);
    prev->next = NULL;
}

bool apple_in_front(snake_node* snake_head, direction snake_direction, short int apple_x, short int apple_y)
{
    switch(snake_direction)
    {
        case LEFT:
            if(snake_head->y == apple_y &&
                (((snake_head->x - 1 + MAP_WIDTH) % MAP_WIDTH) == apple_x ||
                ((snake_head->x - 2 + MAP_WIDTH) % MAP_WIDTH) == apple_x))
                return true;
            else
                return false;
        case RIGHT:
            if(snake_head->y == apple_y &&
                (((snake_head->x+1) % MAP_WIDTH) == apple_x ||
                ((snake_head->x+2) % MAP_WIDTH) == apple_x))
                return true;
            else
                return false;
        case DOWN:
            if(snake_head->x == apple_x &&
                (((snake_head->y - 1 + MAP_HEIGHT) % MAP_HEIGHT) == apple_y ||
                ((snake_head->y - 2 + MAP_HEIGHT) % MAP_HEIGHT) == apple_y))
                return true;
            else
                return false;
        case UP:
            if(snake_head->x == apple_x &&
                (((snake_head->y+1) % MAP_HEIGHT) == apple_y ||
                ((snake_head->y+2) % MAP_HEIGHT) == apple_y))
                return true;
            else
                return false;
        default:
            return false;
    }
}

void draw_snake(snake_node* snake_head, direction snake_direction)
{
    short int x_offset = (DISPLAY_WIDTH - 4*MAP_WIDTH) / 2 - 1;
    short int y_offset = 4;
    short int x_pos, y_pos; 

    //draw the middle part
    snake_node* curr = snake_head->next;
    direction prev_direction = snake_head->next_direction;
    while(curr->next)
    {
        x_pos = curr->x * 4;
        y_pos = curr->y * 4;

        bool orientation = true;
        if(prev_direction == DOWN || prev_direction == RIGHT)
            orientation = false;
        if(curr->next_direction != prev_direction && 
            (curr->next_direction == DOWN || curr->next_direction == RIGHT))
            orientation = !orientation;
        if(orientation)
        {
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 1, DISPLAY_HEIGHT - (y_offset + y_pos + 2));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 2, DISPLAY_HEIGHT - (y_offset + y_pos + 1));
        }
        else
        {
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 1, DISPLAY_HEIGHT - (y_offset + y_pos + 1));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 2, DISPLAY_HEIGHT - (y_offset + y_pos + 2));
        }

        if(curr->eaten)
        {
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 0, DISPLAY_HEIGHT - (y_offset + y_pos + 1));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 0, DISPLAY_HEIGHT - (y_offset + y_pos + 2));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 3, DISPLAY_HEIGHT - (y_offset + y_pos + 1));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 3, DISPLAY_HEIGHT - (y_offset + y_pos + 2));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 1, DISPLAY_HEIGHT - (y_offset + y_pos + 0));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 2, DISPLAY_HEIGHT - (y_offset + y_pos + 0));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 1, DISPLAY_HEIGHT - (y_offset + y_pos + 3));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 2, DISPLAY_HEIGHT - (y_offset + y_pos + 3));
        }

        switch(curr->next_direction)
        {
            case LEFT:
                x_pos -= 2; break;
            case RIGHT:
                x_pos += 2; break;
            case DOWN:
                y_pos -= 2; break;
            case UP:
                y_pos += 2; break;
        }
        u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
            DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
        u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
            DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
        u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
            DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
        u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
            DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));

        prev_direction = curr->next_direction;
        curr = curr->next;
    }

    //draw the tail
    x_pos = 4 * curr->x;
    y_pos = 4 * curr->y;
    switch(prev_direction)
    {
        case RIGHT:
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 1, DISPLAY_HEIGHT - (y_offset + y_pos + 1));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 2, DISPLAY_HEIGHT - (y_offset + y_pos + 1));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 1, DISPLAY_HEIGHT - (y_offset + y_pos + 2));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 3, DISPLAY_HEIGHT - (y_offset + y_pos + 1));
            break;
        case LEFT:
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 1, DISPLAY_HEIGHT - (y_offset + y_pos + 1));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 2, DISPLAY_HEIGHT - (y_offset + y_pos + 1));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 2, DISPLAY_HEIGHT - (y_offset + y_pos + 2));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 0, DISPLAY_HEIGHT - (y_offset + y_pos + 1));
            break;
        case UP:
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 1, DISPLAY_HEIGHT - (y_offset + y_pos + 1));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 2, DISPLAY_HEIGHT - (y_offset + y_pos + 1));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 2, DISPLAY_HEIGHT - (y_offset + y_pos + 2));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 2, DISPLAY_HEIGHT - (y_offset + y_pos + 3));
            break;
        case DOWN:
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 1, DISPLAY_HEIGHT - (y_offset + y_pos + 2));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 2, DISPLAY_HEIGHT - (y_offset + y_pos + 1));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 2, DISPLAY_HEIGHT - (y_offset + y_pos + 2));
            u8g2_DrawPixel(&u8g2, x_offset + x_pos + 2, DISPLAY_HEIGHT - (y_offset + y_pos + 0));
            break;
    }

    //draw head
    x_pos = snake_head->x * 4;
    y_pos = snake_head->y * 4;
    u8g2_DrawPixel(&u8g2, x_offset + x_pos + 1, DISPLAY_HEIGHT - (y_offset + y_pos + 1));
    u8g2_DrawPixel(&u8g2, x_offset + x_pos + 2, DISPLAY_HEIGHT - (y_offset + y_pos + 1));
    u8g2_DrawPixel(&u8g2, x_offset + x_pos + 1, DISPLAY_HEIGHT - (y_offset + y_pos + 2));
    u8g2_DrawPixel(&u8g2, x_offset + x_pos + 2, DISPLAY_HEIGHT - (y_offset + y_pos + 2));

    //draw neck and eye
    switch(snake_head->next_direction)
    {
        case RIGHT:
            x_pos += 2;
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 3 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_SetDrawColor(&u8g2, 0);
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_SetDrawColor(&u8g2, 1);
            break;
        case LEFT:
            x_pos -= 2;
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 3 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_SetDrawColor(&u8g2, 0);
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_SetDrawColor(&u8g2, 1);
            break;
        case DOWN:
            y_pos -= 2;
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 0 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_SetDrawColor(&u8g2, 0);
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_SetDrawColor(&u8g2, 1);
            break;
        case UP:
            y_pos += 2;
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 0 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_SetDrawColor(&u8g2, 0);
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_SetDrawColor(&u8g2, 1);
            break;
    }
}

void snake_start_screen()
{
    u8g2_ClearBuffer(&u8g2);                        // Clear internal memory
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);      // Choose a font
    u8g2_DrawStr(&u8g2, 10, 30, "Start Screen");    // Draw the string
    u8g2_SendBuffer(&u8g2);      
}

void snake_end_screen(int score)
{
    if(score > 9000) return;
    u8g2_ClearBuffer(&u8g2);                        // Clear internal memory
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);      // Choose a font
    u8g2_DrawStr(&u8g2, 10, 30, "End Screen");    // Draw the string
    u8g2_SendBuffer(&u8g2);      
}

bool collision_check(snake_node* snake_head, direction snake_direction)
{
    int head_x = snake_head->x;
    int head_y = snake_head->y;
    switch(snake_direction)
    {
        case LEFT:
            head_x--; break;
        case RIGHT:
            head_x++; break;
        case UP:
            head_y++; break;
        case DOWN:
            head_y--; break;
    }

    return snake_map[head_y % MAP_HEIGHT][head_x % MAP_WIDTH];
}

void draw_frame()
{
    short int x1 = (DISPLAY_WIDTH - 4*MAP_WIDTH - 4) / 2 - 1;
    short int x2 = x1 + 3 + 4 * MAP_WIDTH;
    short int y1 = 2;
    short int y2 = y1 + 3 + 4 * MAP_HEIGHT;
    u8g2_DrawLine(&u8g2, x1, DISPLAY_HEIGHT - y1 ,x1, DISPLAY_HEIGHT - y2);
    u8g2_DrawLine(&u8g2, x2, DISPLAY_HEIGHT - y1 ,x2, DISPLAY_HEIGHT - y2);
    u8g2_DrawLine(&u8g2, x1, DISPLAY_HEIGHT - y1 ,x2, DISPLAY_HEIGHT - y1);
    u8g2_DrawLine(&u8g2, x1, DISPLAY_HEIGHT - y2 ,x2, DISPLAY_HEIGHT - y2);
    u8g2_DrawLine(&u8g2, x1, DISPLAY_HEIGHT - (y2 + 2) ,x2, DISPLAY_HEIGHT - (y2 + 2));
}

void draw_score(int score)
{
    char score_str[12] = "Score:0000";
    score_str[9]  = '0' + (score % 10);
    score_str[8]  = '0' + (score / 10) % 10;
    score_str[7]  = '0' + (score / 100) % 10;
    score_str[6]  = '0' + (score / 1000) % 10;
    u8g2_SetFont(&u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(&u8g2, 21, DISPLAY_HEIGHT - 48, score_str);
}

void draw_animal(int x_map, int y_map, int animal_id)
{
    int x = (DISPLAY_WIDTH - 4*MAP_WIDTH) / 2 + x_map * 4;
    int y = 6 + y_map * 4; 
    switch(animal_id)
    {
        case 0: //lizard
            u8g2_DrawBox(&u8g2, x + 1, DISPLAY_HEIGHT - (y + 1), 5, 2);
            u8g2_DrawPixel(&u8g2, x - 1, DISPLAY_HEIGHT - y);
            u8g2_DrawPixel(&u8g2, x - 1, DISPLAY_HEIGHT - (y + 1));
            u8g2_DrawPixel(&u8g2, x, DISPLAY_HEIGHT - y);
            u8g2_DrawPixel(&u8g2, x, DISPLAY_HEIGHT - (y + 2));
            u8g2_DrawPixel(&u8g2, x + 1, DISPLAY_HEIGHT - (y - 1));
            u8g2_DrawPixel(&u8g2, x + 2, DISPLAY_HEIGHT - (y + 2));
            u8g2_DrawPixel(&u8g2, x + 4, DISPLAY_HEIGHT - (y - 1));
            u8g2_DrawPixel(&u8g2, x + 4, DISPLAY_HEIGHT - (y + 2));
            u8g2_DrawPixel(&u8g2, x + 6, DISPLAY_HEIGHT - y);
            break;
        case 1: //crab
            u8g2_DrawBox(&u8g2, x + 1, DISPLAY_HEIGHT - (y + 2), 4, 3);
            u8g2_DrawLine(&u8g2, x-1, DISPLAY_HEIGHT - (y-1), x-1, DISPLAY_HEIGHT - (y+1));
            u8g2_DrawLine(&u8g2, x+6, DISPLAY_HEIGHT - (y-1), x+6, DISPLAY_HEIGHT - (y+1));
            u8g2_DrawPixel(&u8g2, x, DISPLAY_HEIGHT - (y + 1));
            u8g2_DrawPixel(&u8g2, x + 1, DISPLAY_HEIGHT - (y - 1));
            u8g2_DrawPixel(&u8g2, x + 4, DISPLAY_HEIGHT - (y - 1));
            u8g2_DrawPixel(&u8g2, x + 5, DISPLAY_HEIGHT - (y + 1));
            break;
        case 2: //fish
            u8g2_DrawBox(&u8g2, x + 3, DISPLAY_HEIGHT - (y + 1), 3, 2);
            u8g2_DrawBox(&u8g2, x - 1, DISPLAY_HEIGHT - (y + 2), 2, 2);
            u8g2_DrawPixel(&u8g2, x + 1, DISPLAY_HEIGHT - y);
            u8g2_DrawPixel(&u8g2, x + 2, DISPLAY_HEIGHT - y);
            u8g2_DrawPixel(&u8g2, x + 3, DISPLAY_HEIGHT - (y - 1));
            u8g2_DrawPixel(&u8g2, x + 4, DISPLAY_HEIGHT - (y + 2));
            u8g2_DrawPixel(&u8g2, x + 5, DISPLAY_HEIGHT - (y - 1));
            u8g2_DrawPixel(&u8g2, x + 6, DISPLAY_HEIGHT - y);
            break;
    }
}

void draw_animal_timer(int animal_timer)
{
    if(animal_timer <= 0) return;
    char animal_time_str[3] = "00";
    animal_time_str[0] += animal_timer / 10;
    animal_time_str[1] += animal_timer % 10;
    u8g2_SetFont(&u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(&u8g2, 90, DISPLAY_HEIGHT - 47, animal_time_str);
}

void generate_apple(short int *apple_x, short int *apple_y)
{
    short int apple_pos = rand() % (MAP_HEIGHT * MAP_WIDTH);
    for(short int i = 0; i < MAP_HEIGHT * MAP_WIDTH; i++)
    {
        short int x = (apple_pos + i) % MAP_WIDTH;
        short int y = ((apple_pos + i)  / MAP_WIDTH) % MAP_HEIGHT;
        if(!snake_map[y][x])
        {
            *apple_x = x;
            *apple_y = y;
            return;
        }
    }
    *apple_x = -1;
    *apple_y = -1;
}

void generate_animal(short int *animal_x, short int *animal_y)
{
    short int animal_pos = rand() % (MAP_HEIGHT * MAP_WIDTH);
    for(short int i = 0; i < MAP_HEIGHT * MAP_WIDTH; i++)
    {
        short int x = (animal_pos + i) % MAP_WIDTH;
        short int y = ((animal_pos + i)  / MAP_WIDTH) % MAP_HEIGHT;
        if(x != (MAP_WIDTH - 1) && !snake_map[y][x] && !snake_map[y][x+1])
        {
            *animal_x = x;
            *animal_y = y;
            return;
        }
    }
    *animal_x = -1;
    *animal_y = -1;
}

void draw_apple(short int x_map, short int y_map)
{
    if(x_map == -1 || y_map == -1)
        return;

    short int x = (DISPLAY_WIDTH - 4*MAP_WIDTH) / 2 + x_map * 4;
    short int y =  6 + y_map * 4;
    u8g2_DrawPixel(&u8g2, x - 1, DISPLAY_HEIGHT - y);
    u8g2_DrawPixel(&u8g2, x + 1, DISPLAY_HEIGHT - y);
    u8g2_DrawPixel(&u8g2, x, DISPLAY_HEIGHT - (y - 1));
    u8g2_DrawPixel(&u8g2, x, DISPLAY_HEIGHT - (y + 1));
}

void open_snake_mouth(snake_node* snake_head, direction snake_direction)
{
    short int x = (DISPLAY_WIDTH - 4*MAP_WIDTH) / 2 + snake_head->x * 4;
    short int y = 5 + snake_head->y * 4;
    switch(snake_direction)
    {
        case LEFT:
            u8g2_DrawPixel(&u8g2, x, DISPLAY_HEIGHT - (y - 1));
            u8g2_DrawPixel(&u8g2, x, DISPLAY_HEIGHT - (y + 2));
            u8g2_SetDrawColor(&u8g2, 0);
            u8g2_DrawPixel(&u8g2, x, DISPLAY_HEIGHT - y);
            u8g2_DrawPixel(&u8g2, x, DISPLAY_HEIGHT - (y + 1));
            u8g2_SetDrawColor(&u8g2, 1);
            break;
        case RIGHT:
            u8g2_DrawPixel(&u8g2, x + 1, DISPLAY_HEIGHT - (y - 1));
            u8g2_DrawPixel(&u8g2, x + 1, DISPLAY_HEIGHT - (y + 2));
            u8g2_SetDrawColor(&u8g2, 0);
            u8g2_DrawPixel(&u8g2, x + 1, DISPLAY_HEIGHT - y);
            u8g2_DrawPixel(&u8g2, x + 1, DISPLAY_HEIGHT - (y + 1));
            u8g2_SetDrawColor(&u8g2, 1);
            break;
        case DOWN:
            u8g2_DrawPixel(&u8g2, x - 1, DISPLAY_HEIGHT - y);
            u8g2_DrawPixel(&u8g2, x + 2, DISPLAY_HEIGHT - y);
            u8g2_SetDrawColor(&u8g2, 0);
            u8g2_DrawPixel(&u8g2, x, DISPLAY_HEIGHT - y);
            u8g2_DrawPixel(&u8g2, x + 1, DISPLAY_HEIGHT - y);
            u8g2_SetDrawColor(&u8g2, 1);
            break;
        case UP:
            u8g2_DrawPixel(&u8g2, x - 1, DISPLAY_HEIGHT - (y + 1));
            u8g2_DrawPixel(&u8g2, x + 2, DISPLAY_HEIGHT - (y + 1));
            u8g2_SetDrawColor(&u8g2, 0);
            u8g2_DrawPixel(&u8g2, x, DISPLAY_HEIGHT - (y + 1));
            u8g2_DrawPixel(&u8g2, x + 1, DISPLAY_HEIGHT - (y + 1));
            u8g2_SetDrawColor(&u8g2, 1);
            break;
    }
}

void death_scene(snake_node* snake_head, direction snake_direction, int score)
{
    for(int i = 0; i < 9; i++)
    {
        u8g2_ClearBuffer(&u8g2);
        draw_frame();
        draw_score(score);
        if(i % 2)
            draw_snake(snake_head, snake_direction);
        u8g2_SendBuffer(&u8g2);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void app_main()
{
    init_display();
    init_buttons();

    direction snake_direction;
    snake_node* snake_head = NULL;
    int score;
    short int apple_x, apple_y, apples_till_animal,
        animal_timer, animal_id, animal_x, animal_y;
        
    while(true)
    {
        //initialize variables
        snake_direction = RIGHT;
        snake_head = init_snake();
        memset(snake_map, 0, sizeof(snake_map));
        apple_x = -1; apple_y = -1, animal_x = -1, animal_y = -1;
        apples_till_animal = 4, animal_timer = 0, score = 0;
        animal_id = rand() % 3;
        snake_start_screen(); // to be implemented

        //check for any button press to start
        while(true)
        {
            if(!gpio_get_level(LEFT_BUTTON))  break;
            if(!gpio_get_level(DOWN_BUTTON))  break;
            if(!gpio_get_level(RIGHT_BUTTON)) break;
            if(!gpio_get_level(UP_BUTTON))    break;
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }

        //play loop
        while(true)
        {
            u8g2_ClearBuffer(&u8g2);

            if(!gpio_get_level(LEFT_BUTTON) && snake_direction != RIGHT)
                snake_direction = LEFT;
            if(!gpio_get_level(DOWN_BUTTON) && snake_direction != UP)
                snake_direction = DOWN;
            if(!gpio_get_level(RIGHT_BUTTON) && snake_direction != LEFT)
                snake_direction = RIGHT;
            if(!gpio_get_level(UP_BUTTON) && snake_direction != DOWN)
                snake_direction = UP;

            if(collision_check(snake_head, snake_direction))
            {
                death_scene(snake_head, snake_direction, score);
                break;
            }

            snake_head = add_snake_segment(snake_head, snake_direction);

            //check if apple is eaten
            if(snake_head->x == apple_x && snake_head->y == apple_y)
            {
                score += 7;
                apple_x = -1;
                apple_y = -1;
                snake_head->eaten = true;
                apples_till_animal--;
            }
            else
                pop_last_segment(snake_head);

            //generate new apple if previous one got eaten
            if(apple_x == -1 || apple_y == -1)
                generate_apple(&apple_x, &apple_y);

            //check if animal is eaten
            if(animal_timer > 0 && animal_y == snake_head->y &&
                (animal_x == snake_head->x || (animal_x + 1) == snake_head->x))
            {
                score += animal_timer;
                animal_timer = 0;
                animal_x = -1; animal_y = -1;
                snake_head->eaten = true;
            }
            if(animal_timer > 0)
                animal_timer--;

            //generate animal on every 5th apple
            if(apples_till_animal == 0)
            {
                apples_till_animal = 5;
                animal_timer = 20;
                animal_id = rand() % 3;
                if(apple_x != -1 && apple_y != -1)
                {
                    snake_map[apple_y][apple_x] = true;
                    generate_animal(&animal_x, &animal_y);
                    snake_map[apple_y][apple_x] = false;
                }
                else
                    generate_animal(&animal_x, &animal_y);
            }

            //render everything
            draw_snake(snake_head, snake_direction);
            if(apple_in_front(snake_head, snake_direction, apple_x, apple_y))
                open_snake_mouth(snake_head, snake_direction);
            draw_frame();
            draw_score(score);
            draw_apple(apple_x, apple_y);
            if(animal_x != -1 && animal_y != -1 && animal_timer > 0)
            {
                draw_animal_timer(animal_timer);
                draw_animal(animal_x, animal_y, animal_id);
            }

            u8g2_SendBuffer(&u8g2);
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }

        snake_end_screen(score);  //to be implemented
        free_snake_memory(snake_head);

        //wait for play again or exit button press
        while(true)
        {
            if(!gpio_get_level(DOWN_BUTTON))  return; //exit the game
            if(!gpio_get_level(UP_BUTTON))    break; //play again
            if(!gpio_get_level(LEFT_BUTTON))  break; //play again
            if(!gpio_get_level(RIGHT_BUTTON)) break; //play again
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
    }
}
