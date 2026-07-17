package me.nekosu.aqnya.ui.screens

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import me.nekosu.aqnya.KeyUtils
import me.nekosu.aqnya.R
import me.nekosu.aqnya.ui.animation.AnimatedAlertDialog

@Composable
fun KeyInputDialog(
    show: Boolean,
    onDismiss: () -> Unit,
) {
    val context = LocalContext.current
    var inputText by remember { mutableStateOf("") }
    var errorType by remember { mutableIntStateOf(0) }
    val scrollState = rememberScrollState()

    AnimatedAlertDialog(
        visible = show,
        onDismiss = onDismiss,
        title = {
            Text(
                text = stringResource(R.string.dialog_key_set),
                style = TextStyle(fontSize = 16.sp),
            )
        },
        text = {
            Column(
                modifier =
                    Modifier
                        .verticalScroll(scrollState)
                        .fillMaxWidth(),
            ) {
                Text(stringResource(R.string.dialog_key_please_input))
                Spacer(modifier = Modifier.height(8.dp))
                OutlinedTextField(
                    value = inputText,
                    onValueChange = {
                        inputText = it
                        errorType = 0
                    },
                    label = { Text("ECC Key (PEM/Base64)", fontSize = 14.sp) },
                    placeholder = {
                        Text("-----BEGIN EC PRIVATE KEY-----...", fontSize = 14.sp)
                    },
                    singleLine = false,
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .heightIn(min = 120.dp, max = 240.dp),
                    isError = errorType != 0,
                    supportingText = {
                        when (errorType) {
                            1 -> {
                                Text(
                                    stringResource(R.string.dialog_key_input_no_empty),
                                    color = MaterialTheme.colorScheme.error,
                                )
                            }
                            2 -> {
                                Text(
                                    stringResource(R.string.dialog_key_input_invalid),
                                    color = MaterialTheme.colorScheme.error,
                                )
                            }
                        }
                    },
                )
            }
        },
        confirmButton = {
            Button(
                onClick = {
                    val trimmedKey = inputText.trim()
                    errorType =
                        when {
                            trimmedKey.isBlank() -> 1
                            !KeyUtils.isValidECCKey(trimmedKey) -> 2
                            else -> 0
                        }
                    if (errorType == 0) {
                        KeyUtils.saveKey(context, trimmedKey)
                        onDismiss()
                    }
                },
            ) {
                Text(stringResource(R.string.dialog_key_save))
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(R.string.dialog_key_later))
            }
        },
    )
}
