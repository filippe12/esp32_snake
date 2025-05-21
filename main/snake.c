#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_task_wdt.h"

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
static esp_task_wdt_config_t twdt_config;
int snake_highscore = 0;

void init_watchdog() {
    twdt_config.timeout_ms = 10000;
    twdt_config.idle_core_mask = 0;
    twdt_config.trigger_panic = true;

    esp_task_wdt_init(&twdt_config);  // Initialize the Task Watchdog
    esp_task_wdt_add(NULL);           // Add current task to the watchdog
}

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
        if(snake_head->y >= MAP_HEIGHT)
            snake_head->y = 0;
        snake_head->next_direction = UP;
        break;
    case UP:
        snake_head->y++;
        if(snake_head->y < 0)
            snake_head->y = MAP_HEIGHT - 1;
        snake_head->next_direction = DOWN;
        break;
    case RIGHT:
        snake_head->x++;
        if(snake_head->x >= MAP_WIDTH)
            snake_head->x = 0;
        snake_head->next_direction = LEFT;
        break;
    }

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
    free(curr);
    prev->next = NULL;
}

void draw_snake(snake_node* snake_head, direction snake_direction)
{
    short int x_offset = (DISPLAY_WIDTH - 4*MAP_WIDTH) / 2 - 1;
    short int y_offset = 4;
    short int x_pos, y_pos;

    //draw head
    x_pos = snake_head->x * 4;
    y_pos = snake_head->y * 4;
    u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
        DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
    u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
        DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
    u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
        DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
    u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
        DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));

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
            break;
    } 

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
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
        }
        else
        {
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
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
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 3 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            break;
        case LEFT:
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 0 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            break;
        case UP:
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 3 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            break;
        case DOWN:
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 1 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 1 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 2 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
            u8g2_DrawPixel(&u8g2, x_offset + (x_pos + 2 + 4 * MAP_WIDTH)  % (4 * MAP_WIDTH),
                DISPLAY_HEIGHT - (y_offset + (y_pos + 0 + 4 * MAP_HEIGHT) % (4 * MAP_HEIGHT)));
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

void snake_end_screen()
{
    u8g2_ClearBuffer(&u8g2);                        // Clear internal memory
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);      // Choose a font
    u8g2_DrawStr(&u8g2, 10, 30, "End Screen");    // Draw the string
    u8g2_SendBuffer(&u8g2);      
}

bool collision_check(snake_node* snake_head)
{
    int head_x = snake_head->x;
    int head_y = snake_head->y;

    snake_node* curr = snake_head->next;
    while(curr)
    {
        if(curr->x == head_x && curr->y == head_y)
            return true;
        curr = curr->next;
    }

    return false;
}

void draw_frame()
{

}

void draw_score()
{

}

void draw_rat_timer()
{

    
}

void app_main()
{
    init_display();
    init_buttons();
    init_watchdog();

    direction snake_direction;
    snake_node* snake_head = NULL;
    //bool snake_map[MAP_HEIGHT][MAP_WIDTH] = { false };
        
    while(true)
    {
        esp_task_wdt_reset();
        snake_direction = RIGHT;
        snake_head = init_snake();
        snake_start_screen(); // to be implemented

        //check for any button press for start
        while(true)
        {
            if(!gpio_get_level(LEFT_BUTTON))  break;
            if(!gpio_get_level(DOWN_BUTTON))  break;
            if(!gpio_get_level(RIGHT_BUTTON)) break;
            if(!gpio_get_level(UP_BUTTON))    break;
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }

        //game loop, to be implemented
        while(true)
        {
            u8g2_ClearBuffer(&u8g2);
            //u8g2_SendBuffer(&u8g2);  

            if(!gpio_get_level(LEFT_BUTTON) && snake_direction != RIGHT)
                snake_direction = LEFT;
            if(!gpio_get_level(DOWN_BUTTON) && snake_direction != UP)
                snake_direction = DOWN;
            if(!gpio_get_level(RIGHT_BUTTON) && snake_direction != LEFT)
                snake_direction = RIGHT;
            if(!gpio_get_level(UP_BUTTON) && snake_direction != DOWN)
                snake_direction = UP;

            snake_head = add_snake_segment(snake_head, snake_direction);
            //pop_last_segment(snake_head);
            if(collision_check(snake_head))
            {
                //stop the game logic
                break;
            }

            draw_snake(snake_head, snake_direction);

            u8g2_SendBuffer(&u8g2);
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        snake_end_screen();  //to be implemented
        free_snake_memory(snake_head);

        //wait for play again or exit button press
        while(true)
        {
            if(!gpio_get_level(DOWN_BUTTON))  return; //exit the game
            if(!gpio_get_level(UP_BUTTON))    break; //play again
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
    }
}
