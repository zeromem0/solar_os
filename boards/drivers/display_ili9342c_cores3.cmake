# CoreS3's ILI9342C panel. Separate from display_ili9341 because this
# glass needs its own MADCTL and colour inversion, and because its reset
# and backlight lines are behind the AW9523B expander and the AXP2101
# PMIC rather than on GPIOs the shared device bindings can name. Boards
# including this fragment must also include drivers/i2c_esp_idf.cmake,
# drivers/pmic_axp2101.cmake and drivers/io_expander_aw9523b.cmake.
set(SOLAR_OS_BOARD_DISPLAY_DRIVER "ili9342c_cores3")
include("${CMAKE_CURRENT_LIST_DIR}/spi_esp_idf.cmake")
list(APPEND SOLAR_OS_BOARD_REQUIRED_PACKAGES driver_display_ili9342c_cores3)
