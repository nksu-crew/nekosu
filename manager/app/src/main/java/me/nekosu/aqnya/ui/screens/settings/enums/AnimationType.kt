package me.nekosu.aqnya.ui.screens.enums

enum class AnimationType(
    val label: String,
    val value: String,
) {
    LINEAR("Linear", "linear"),
    SPATIAL("Spatial", "spatial"),
    FADE("Fade", "fade"),
    VERTICAL("Vertical", "vertical"),
    DIAGONAL("Diagonal", "diagonal"),
    ;

    companion object {
        fun fromValue(value: String) = entries.find { it.value == value } ?: LINEAR
    }
}
