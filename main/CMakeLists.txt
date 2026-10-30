idf_component_register(
    SRCS "app_main.c"
         "mastermind_core.c"
         "solver_core.c"
         "game_rules.c"
         "achieve.c"
         "store.c"
         "sound_player.c"
         "ui_theme.c"
         "ui_font.c"
         "ui_shell.c"
         "page_home.c"
         "page_game.c"
         "page_daily.c"
         "page_achieve.c"
         "page_settings.c"
         "page_about.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer esp_hw_support nvs_flash
)

# 应用中文字体子集(由 tools/gen_cipher_font.py 生成,勿手改)。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/cipher_font_12.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/cipher_font_14.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/cipher_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/cipher_font_20.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/cipher_font_28.c"
)
