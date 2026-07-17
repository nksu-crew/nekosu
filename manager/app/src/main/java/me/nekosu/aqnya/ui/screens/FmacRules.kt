package me.nekosu.aqnya.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.InsertDriveFile
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Block
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import androidx.lifecycle.viewmodel.compose.viewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.nekosu.aqnya.R
import me.nekosu.aqnya.ncore
import me.nekosu.aqnya.util.FMAC_BIT_DENY
import me.nekosu.aqnya.util.FmacRule
import me.nekosu.aqnya.util.RuleDbHelper

class RulesViewModel(
    private val context: android.content.Context,
) : ViewModel() {
    private val db = RuleDbHelper(context)

    var rules by mutableStateOf<List<FmacRule>>(emptyList())
        private set
    var isLoaded by mutableStateOf(false)
        private set
    var error by mutableStateOf<String?>(null)
        private set

    override fun onCleared() {
        super.onCleared()
        db.close()
    }

    fun load() {
        viewModelScope.launch(Dispatchers.IO) {
            rules = db.getAll()
            isLoaded = true
        }
    }

    fun addRule(
        path: String,
        statusBits: Long,
        onDone: (Boolean) -> Unit,
    ) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val rule = FmacRule(path.trim(), statusBits)
                ncore.addRule(rule.path, rule.statusBits)
                db.insert(rule)
                rules = db.getAll()
                withContext(Dispatchers.Main) { onDone(true) }
            } catch (e: Exception) {
                error = e.message
                withContext(Dispatchers.Main) { onDone(false) }
            }
        }
    }

    fun deleteRule(
        rule: FmacRule,
        onDone: (Boolean) -> Unit,
    ) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                ncore.delRule(rule.path)
                db.delete(rule.path)
                rules = db.getAll()
                withContext(Dispatchers.Main) { onDone(true) }
            } catch (e: Exception) {
                error = e.message
                withContext(Dispatchers.Main) { onDone(false) }
            }
        }
    }
}

class RulesViewModelFactory(
    private val context: android.content.Context,
) : ViewModelProvider.Factory {
    @Suppress("UNCHECKED_CAST")
    override fun <T : ViewModel> create(modelClass: Class<T>): T = RulesViewModel(context) as T
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RulesScreen(extraBottomPadding: Dp = 96.dp) {
    val context = LocalContext.current.applicationContext
    val vm: RulesViewModel = viewModel(factory = RulesViewModelFactory(context))
    val snackbar = remember { SnackbarHostState() }
    val scope = rememberCoroutineScope()
    var showAddDialog by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) { vm.load() }

    Scaffold(
        snackbarHost = { SnackbarHost(snackbar) },
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text(
                            stringResource(R.string.fmac_rules),
                        )
                        if (vm.isLoaded) {
                            Text(
                                stringResource(R.string.fmac_rule_count, vm.rules.size),
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                },
                actions = {
                    IconButton(onClick = { showAddDialog = true }) {
                        Icon(
                            Icons.Default.Add,
                            contentDescription = stringResource(R.string.fmac_add_rule),
                            tint = MaterialTheme.colorScheme.primary,
                        )
                    }
                },
                colors =
                    TopAppBarDefaults.topAppBarColors(
                        containerColor = MaterialTheme.colorScheme.surface,
                    ),
            )
        },
    ) { padding ->
        Box(
            modifier =
                Modifier
                    .fillMaxSize()
                    .padding(padding)
                    .padding(horizontal = 16.dp),
            contentAlignment = Alignment.Center,
        ) {
            when {
                !vm.isLoaded -> {
                    CircularProgressIndicator(strokeWidth = 3.dp)
                }

                vm.rules.isEmpty() -> {
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        Text(stringResource(R.string.fmac_no_rules), color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }

                else -> {
                    LazyColumn(
                        modifier = Modifier.fillMaxSize(),
                        verticalArrangement = Arrangement.spacedBy(10.dp),
                        contentPadding = PaddingValues(top = 12.dp, bottom = extraBottomPadding),
                    ) {
                        items(vm.rules, key = { it.path }) { rule ->
                            RuleItem(
                                rule = rule,
                                onDelete = {
                                    vm.deleteRule(rule) { ok ->
                                        val message =
                                            if (ok) {
                                                context.getString(
                                                    R.string.toast_deleted,
                                                )
                                            } else {
                                                context.getString(R.string.toast_delete_failed)
                                            }
                                        android.widget.Toast
                                            .makeText(context, message, android.widget.Toast.LENGTH_SHORT)
                                            .show()
                                    }
                                },
                            )
                        }
                    }
                }
            }
        }
    }

    if (showAddDialog) {
        AddRuleDialog(
            onDismiss = { showAddDialog = false },
            onConfirm = { path, bits ->
                showAddDialog = false
                vm.addRule(path, bits) { ok ->
                    val message = if (ok) context.getString(R.string.toast_rule_added) else context.getString(R.string.toast_add_failed)
                    android.widget.Toast
                        .makeText(context, message, android.widget.Toast.LENGTH_SHORT)
                        .show()
                }
            },
        )
    }
}

@Composable
fun RuleItem(
    rule: FmacRule,
    onDelete: () -> Unit,
) {
    val haptic = LocalHapticFeedback.current
    val isDeny = (rule.statusBits shr FMAC_BIT_DENY) and 1L == 1L
    val accentColor =
        if (isDeny) {
            MaterialTheme.colorScheme.error
        } else {
            MaterialTheme.colorScheme.primary
        }
    val isDir = rule.path.endsWith("/")

    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(20.dp),
        elevation = CardDefaults.cardElevation(0.dp),
        colors =
            CardDefaults.cardColors(
                containerColor = accentColor.copy(alpha = 0.06f),
            ),
    ) {
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Box(
                contentAlignment = Alignment.Center,
                modifier =
                    Modifier
                        .size(40.dp)
                        .background(accentColor.copy(alpha = 0.12f), RoundedCornerShape(12.dp)),
            ) {
                Icon(
                    imageVector =
                        if (isDir) {
                            Icons.Default.Folder
                        } else {
                            Icons.AutoMirrored.Filled.InsertDriveFile
                        },
                    contentDescription = null,
                    tint = accentColor,
                    modifier = Modifier.size(20.dp),
                )
            }

            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(3.dp),
            ) {
                Text(
                    text = rule.path,
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.SemiBold,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    BitChip(
                        label = if (isDeny) stringResource(R.string.fmac_label_deny) else stringResource(R.string.fmac_label_allow),
                        color = accentColor,
                    )
                    Text(
                        text = "0x%x".format(rule.statusBits),
                        style = MaterialTheme.typography.labelSmall,
                        fontSize = 10.sp,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                    )
                }
            }

            IconButton(onClick = {
                haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                onDelete()
            }) {
                Icon(
                    Icons.Default.Delete,
                    contentDescription = stringResource(R.string.cd_delete),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.5f),
                )
            }
        }
    }
}

@Composable
private fun BitChip(
    label: String,
    color: androidx.compose.ui.graphics.Color,
) {
    Box(
        modifier =
            Modifier
                .clip(RoundedCornerShape(6.dp))
                .background(color.copy(alpha = 0.12f))
                .padding(horizontal = 6.dp, vertical = 2.dp),
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            fontSize = 10.sp,
            fontWeight = FontWeight.Bold,
            color = color,
        )
    }
}

@Composable
fun AddRuleDialog(
    onDismiss: () -> Unit,
    onConfirm: (path: String, statusBits: Long) -> Unit,
) {
    var path by remember { mutableStateOf("") }
    var deny by remember { mutableStateOf(true) }
    var pathError by remember { mutableStateOf(false) }

    fun computeBits() = if (deny) (1L shl FMAC_BIT_DENY) else 0L

    AlertDialog(
        onDismissRequest = onDismiss,
        shape = RoundedCornerShape(28.dp),
        title = { Text(stringResource(R.string.fmac_add_rule), fontWeight = FontWeight.Bold) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
                OutlinedTextField(
                    value = path,
                    onValueChange = {
                        path = it
                        pathError = false
                    },
                    label = { Text(stringResource(R.string.fmac_path)) },
                    placeholder = { Text("/data/local/tmp/") },
                    isError = pathError,
                    supportingText = if (pathError) ({ Text(stringResource(R.string.fmac_path_not_empty)) }) else null,
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(16.dp),
                )
                Text(
                    stringResource(R.string.fmac_permission_bits),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FilterChip(
                        selected = deny,
                        onClick = { deny = !deny },
                        label = { Text(stringResource(R.string.fmac_label_deny)) },
                        leadingIcon =
                            if (deny) {
                                { Icon(Icons.Default.Block, null, Modifier.size(16.dp)) }
                            } else {
                                null
                            },
                    )
                }
                Box(
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(12.dp))
                            .background(
                                MaterialTheme.colorScheme.surfaceContainerHigh,
                            ).padding(horizontal = 14.dp, vertical = 10.dp),
                ) {
                    Text(
                        text = "status_bits = 0x%x".format(computeBits()),
                        style = MaterialTheme.typography.labelMedium,
                        fontWeight = FontWeight.Medium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        },
        confirmButton = {
            Button(
                onClick = {
                    if (path.isBlank()) {
                        pathError = true
                        return@Button
                    }
                    onConfirm(path.trim(), computeBits())
                },
                shape = RoundedCornerShape(14.dp),
            ) { Text(stringResource(R.string.dialog_add)) }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text(stringResource(R.string.dialog_cancel)) }
        },
    )
}
