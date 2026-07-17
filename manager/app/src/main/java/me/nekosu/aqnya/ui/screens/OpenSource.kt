package me.nekosu.aqnya.ui.screens

import android.content.Intent
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults.pinnedScrollBehavior
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.core.net.toUri
import androidx.navigation.NavHostController
import androidx.navigation.compose.rememberNavController
import me.nekosu.aqnya.R

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun OpenSourceScreen(navController: NavHostController) {
    val scrollBehavior = pinnedScrollBehavior(rememberTopAppBarState())
    val context = LocalContext.current

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = stringResource(R.string.open_source_title),
                    )
                },
                navigationIcon = {
                    IconButton(onClick = { navController.popBackStack() }) {
                        Icon(
                            imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = stringResource(R.string.cd_back),
                        )
                    }
                },
                scrollBehavior = scrollBehavior,
            )
        },
        modifier = Modifier.nestedScroll(scrollBehavior.nestedScrollConnection),
        contentWindowInsets =
            WindowInsets.safeDrawing
                .only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
    ) { innerPadding ->
        LazyColumn(
            modifier =
                Modifier
                    .fillMaxSize()
                    .padding(innerPadding)
                    .padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.spacedBy(2.dp),
            contentPadding = PaddingValues(top = 8.dp, bottom = 96.dp),
        ) {
            itemsIndexed(licenseList) { index, item ->
                LicenseItemView(
                    item = item,
                    shape =
                        when {
                            licenseList.size <= 1 -> RoundedCornerShape(20.dp)
                            index == 0 -> RoundedCornerShape(topStart = 20.dp, topEnd = 20.dp)
                            index == licenseList.lastIndex -> RoundedCornerShape(bottomStart = 20.dp, bottomEnd = 20.dp)
                            else -> RoundedCornerShape(0.dp)
                        },
                    onClick = {
                        val intent = Intent(Intent.ACTION_VIEW, item.url.toUri())
                        context.startActivity(intent)
                    },
                )
            }
        }
    }
}

@Composable
fun LicenseItemView(
    item: LicenseItem,
    shape: androidx.compose.ui.graphics.Shape,
    onClick: () -> Unit,
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = shape,
        colors =
            CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceContainer,
            ),
        elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
    ) {
        Column(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .clickable(onClick = onClick)
                    .padding(horizontal = 16.dp, vertical = 12.dp),
        ) {
            Text(
                text = "${item.name} - ${item.author}",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
            )
            Text(
                text = item.url,
                style =
                    MaterialTheme.typography.bodySmall.copy(
                        color = MaterialTheme.colorScheme.primary,
                        textDecoration = TextDecoration.Underline,
                    ),
            )
            Text(
                text = getLicense(item.type),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

private val licenseList =
    listOf(
        LicenseItem(
            "Google",
            "Jetpack Compose",
            "https://github.com/androidx/androidx",
            LicenseType.Apache2,
        ),
        LicenseItem(
            "The Go Authors",
            "Go Programming Language",
            "https://github.com/golang/go",
            LicenseType.BSD3,
        ),
        LicenseItem(
            "JetBrains",
            "Kotlin",
            "https://github.com/JetBrains/kotlin",
            LicenseType.Apache2,
        ),
        LicenseItem(
            "Google",
            "Material Design 3",
            "https://m3.material.io/",
            LicenseType.Apache2,
        ),
        LicenseItem(
            "square",
            "okhttp",
            "https://github.com/square/okhttp",
            LicenseType.Apache2,
        ),
    )

data class LicenseItem(
    val author: String,
    val name: String,
    val url: String,
    val type: LicenseType,
)

enum class LicenseType {
    Apache2,
    MIT,
    GPL3,
    BSD3,
}

private fun getLicense(type: LicenseType): String =
    when (type) {
        LicenseType.Apache2 -> "Apache Software License 2.0"
        LicenseType.MIT -> "MIT License"
        LicenseType.GPL3 -> "GNU general public license Version 3"
        LicenseType.BSD3 -> "BSD 3-Clause \"New\" or \"Revised\" License"
    }

@Preview(showBackground = true)
@Composable
fun OpenSourceScreenPreview() {
    OpenSourceScreen(
        navController = rememberNavController(),
    )
}
