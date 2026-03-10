idf_component_register(
    SRCS "main.c" "ui/wekker_ui.c" "ui/fonts/lv_font_montserrat_120_digits.c"
    INCLUDE_DIRS "." "ui" "ui/fonts" "../components/lyrat_ctrl" "../components/tsl2591" "../components/aht20" "../components/io_extension" "../components/rgb_lcd_port" "../components/touch" "../components/lvgl_port"
    REQUIRES lyrat_ctrl tsl2591 aht20 i2c io_extension rgb_lcd_port touch lvgl_port wekker_protocol
    WHOLE_ARCHIVE
    )

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
