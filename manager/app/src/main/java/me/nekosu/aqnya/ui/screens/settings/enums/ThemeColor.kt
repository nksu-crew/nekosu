package me.nekosu.aqnya.ui.screens.enums

enum class ThemeColor(
    val label: String,
    val value: Int,
) {
    MATERIAL_YOU("Material You", 0),
    CATPPUCCIN_BLUE("Catppuccin Blue", 1),
    CATPPUCCIN_LAVENDER("Catppuccin Lavender", 2),
    CATPPUCCIN_GREEN("Catppuccin Green", 3),
    ;

    companion object {
        fun fromValue(value: Int) = entries.find { it.value == value } ?: MATERIAL_YOU
    }
}
