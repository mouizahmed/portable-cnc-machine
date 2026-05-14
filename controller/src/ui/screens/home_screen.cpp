#include "ui/screens/home_screen.h"

#include "ui/components/ui_components.h"
#include "ui/ui_types.h"

void home_screen_draw(void)
{
    ui_draw_menu_card((ui_rect_t){0, 42, 240, 116}, "LOAD JOB", "PICK G-CODE", true);
    ui_draw_menu_card((ui_rect_t){240, 42, 240, 116}, "JOG", "MOVE AXES", true);
    ui_draw_menu_card((ui_rect_t){0, 158, 240, 116}, "SET ZERO", "WORK OFFSET", false);
    ui_draw_menu_card((ui_rect_t){240, 158, 240, 116}, "TOOL SETUP", "TOUCH-OFF / CHANGE", false);
}
