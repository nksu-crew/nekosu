package me.nekosu.aqnya.ui.component

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

fun groupShape(
    index: Int,
    total: Int,
    radius: Dp = 20.dp,
    innerRadius: Dp = 4.dp,
): Shape =
    when {
        total <= 1 -> RoundedCornerShape(radius)

        index == 0 ->
            RoundedCornerShape(
                topStart = radius,
                topEnd = radius,
                bottomStart = innerRadius,
                bottomEnd = innerRadius,
            )

        index == total - 1 ->
            RoundedCornerShape(
                topStart = innerRadius,
                topEnd = innerRadius,
                bottomStart = radius,
                bottomEnd = radius,
            )

        else ->
            RoundedCornerShape(innerRadius)
    }

@Composable
fun CardGroup(
    spacing: Dp = 2.dp,
    content: @Composable ColumnScope.() -> Unit
) {
    Column(
        verticalArrangement = Arrangement.spacedBy(spacing),
        content = content
    )
}

@Composable
fun CardItem(
    index: Int,
    total: Int,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    Card(
        modifier = modifier.fillMaxWidth(),
        shape = groupShape(index, total),
        colors =
            CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceContainer,
            ),
        elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
    ) {
        content()
    }
}

@Composable
fun ListRow(
    modifier: Modifier = Modifier,
    icon: @Composable () -> Unit,
    headline: @Composable () -> Unit,
    supporting: @Composable (() -> Unit)? = null,
    trailing: @Composable () -> Unit = {},
    verticalPadding: Dp = 8.dp,
    horizontalPadding: Dp = 16.dp,
) {
    Row(
        modifier = modifier.fillMaxWidth().padding(horizontal = horizontalPadding, vertical = verticalPadding),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        icon()
        Spacer(Modifier.width(16.dp))
        Column(Modifier.weight(1f)) {
            headline()
            supporting?.let { sup ->
                CompositionLocalProvider(
                    LocalTextStyle provides MaterialTheme.typography.bodyMedium,
                    LocalContentColor provides MaterialTheme.colorScheme.onSurfaceVariant,
                ) {
                    sup()
                }
            }
        }
        Spacer(Modifier.width(8.dp))
        trailing()
    }
}
