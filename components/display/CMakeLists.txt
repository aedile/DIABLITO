idf_component_register(
    SRCS "src/display.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer
)
# idf.py -DDISPLAY_PORTRAIT=0 build: landscape 280x240 (default in display.h is portrait)
if(DEFINED DISPLAY_PORTRAIT)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC DISPLAY_PORTRAIT=${DISPLAY_PORTRAIT})
endif()
