package me.nekosu.aqnya.ui.component

import androidx.compose.foundation.MutatePriority
import androidx.compose.foundation.pager.PagerState
import androidx.compose.runtime.withFrameNanos
import kotlin.math.abs

suspend fun PagerState.springScrollToPage(target: Int) {
    scroll(MutatePriority.UserInput) {
        val tension = 322.2f
        val damping = 32.31f
        val pageSize = layoutInfo.pageSize + layoutInfo.pageSpacing
        val distance = target - currentPage - currentPageOffsetFraction
        val scrollPixels = distance * pageSize
        var current = 0f
        var velocity = 0f
        var lastNanos = 0L
        var finished = false
        withFrameNanos { lastNanos = it }
        while (!finished) {
            withFrameNanos { frameNanos ->
                val dt = ((frameNanos - lastNanos) / 1e9f).coerceAtMost(0.016f)
                lastNanos = frameNanos
                velocity = velocity * (1f - damping * dt) +
                    tension * (scrollPixels - current) * dt
                val newPos = current + dt * velocity
                val delta = newPos - current
                if (abs(delta) > 0.5f) {
                    val consumed = scrollBy(delta)
                    current += consumed
                    if (abs(delta - consumed) > 0.1f) {
                        finished = true
                    }
                } else {
                    current = newPos
                }
                if (abs(velocity) < 0.1f && abs(scrollPixels - current) < 1.0f) {
                    finished = true
                }
            }
        }
        if (target in 0 until pageCount) {
            scrollToPage(target)
        }
    }
}
