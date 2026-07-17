package me.nekosu.aqnya.ui.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.LockOpen
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.Saver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavController
import me.nekosu.aqnya.R
import me.nekosu.aqnya.ui.component.CardGroup
import me.nekosu.aqnya.ui.component.CardItem
import me.nekosu.aqnya.ui.component.ListRow

@Composable
fun CapsDialog(
    current: Set<LinuxCap>,
    onDismiss: () -> Unit,
    onConfirm: (Set<LinuxCap>) -> Unit,
) {
    val haptic = LocalHapticFeedback.current
    var draft by remember { mutableStateOf(current) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = {
            Text(
                stringResource(R.string.dialog_caps_title, draft.size, LinuxCap.entries.size),
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
            )
        },
        text = {
            Column {
                Row(
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    OutlinedButton(
                        onClick = {
                            haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
                            draft = LinuxCap.entries.toSet()
                        },
                        modifier = Modifier.weight(1f),
                        shape = RoundedCornerShape(10.dp),
                        contentPadding = PaddingValues(horizontal = 4.dp, vertical = 6.dp),
                    ) { Text(stringResource(R.string.dialog_select_all), fontSize = 12.sp) }
                    OutlinedButton(
                        onClick = {
                            haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
                            draft = DEFAULT_CAPS
                        },
                        modifier = Modifier.weight(1f),
                        shape = RoundedCornerShape(10.dp),
                        contentPadding = PaddingValues(horizontal = 4.dp, vertical = 6.dp),
                    ) { Text(stringResource(R.string.dialog_default), fontSize = 12.sp) }
                    OutlinedButton(
                        onClick = {
                            haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
                            draft = emptySet()
                        },
                        modifier = Modifier.weight(1f),
                        shape = RoundedCornerShape(10.dp),
                        contentPadding = PaddingValues(horizontal = 4.dp, vertical = 6.dp),
                    ) { Text(stringResource(R.string.dialog_clear), fontSize = 12.sp) }
                }

                Spacer(Modifier.height(8.dp))
                HorizontalDivider()
                Spacer(Modifier.height(4.dp))

                Column(
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .heightIn(max = 380.dp)
                            .verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.spacedBy(2.dp),
                ) {
                    LinuxCap.entries.forEach { cap ->
                        val checked = draft.contains(cap)
                        Row(
                            modifier =
                                Modifier
                                    .fillMaxWidth()
                                    .clip(RoundedCornerShape(10.dp))
                                    .clickable(
                                        interactionSource = remember { MutableInteractionSource() },
                                        indication = null,
                                    ) {
                                        haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                                        draft = if (checked) draft - cap else draft + cap
                                    }.padding(vertical = 4.dp, horizontal = 2.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Checkbox(checked = checked, onCheckedChange = null)
                            Spacer(Modifier.width(6.dp))
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = "CAP_${cap.label}",
                                    style = MaterialTheme.typography.bodySmall,
                                    fontWeight = FontWeight.SemiBold,
                                    fontFamily = FontFamily.Monospace,
                                )
                                Text(
                                    text = stringResource(cap.descriptionRes),
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                                )
                            }
                            Text(
                                text = cap.value.toString(),
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.35f),
                                modifier = Modifier.padding(start = 6.dp),
                            )
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = {
                haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
                onConfirm(draft)
            }) {
                Text(stringResource(R.string.dialog_confirm), fontWeight = FontWeight.SemiBold)
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text(stringResource(R.string.dialog_cancel)) }
        },
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppDetailScreen(
    app: AppInfo,
    config: AppConfig?,
    onSave: (AppConfig) -> Unit,
    onBack: () -> Unit,
    navController: NavController,
) {
    var allowed by rememberSaveable { mutableStateOf(config?.allowed ?: false) }

    val capsSaver =
        remember {
            val labelMap = LinuxCap.entries.associateBy { it.label }
            Saver<Set<LinuxCap>, List<String>>(
                save = { it.map { cap -> cap.label } },
                restore = { it.mapNotNull { label -> labelMap[label] }.toSet() },
            )
        }
    var caps by rememberSaveable(stateSaver = capsSaver) { mutableStateOf(config?.caps ?: DEFAULT_CAPS) }

    var domain by rememberSaveable { mutableStateOf(config?.selinuxDomain ?: "u:r:nksu:s0") }

    val nsSaver =
        remember {
            val valueMap = NksuNamespace.entries.associateBy { it.value }
            Saver<NksuNamespace, Int>(
                save = { it.value },
                restore = { valueMap[it] ?: NksuNamespace.INHERITED },
            )
        }
    var ns by rememberSaveable(stateSaver = nsSaver) {
        mutableStateOf(config?.namespace ?: NksuNamespace.INHERITED)
    }

    var showCapsDialog by rememberSaveable { mutableStateOf(false) }
    val haptic = LocalHapticFeedback.current

    if (showCapsDialog) {
        CapsDialog(
            current = caps,
            onDismiss = { showCapsDialog = false },
            onConfirm = { selected ->
                caps = selected
                showCapsDialog = false
            },
        )
    }

    Scaffold(
        contentWindowInsets = WindowInsets(0.dp),
        topBar = {
            TopAppBar(
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(
                            Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = stringResource(R.string.cd_back),
                        )
                    }
                },
                title = {
                    Column {
                        Text(
                            text = app.name,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                        Text(
                            text = app.packageName,
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                },
            )
        },
        floatingActionButton = {
            FloatingActionButton(
                onClick = {
                    haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
                    onSave(
                        AppConfig(
                            allowed = allowed,
                            caps = caps,
                            selinuxDomain = domain,
                            namespace = ns,
                        ),
                    )
                    onBack()
                },
            ) {
                Icon(Icons.Filled.Save, contentDescription = stringResource(R.string.cd_save))
            }
        },
    ) { innerPadding ->
        LazyColumn(
            modifier =
                Modifier
                    .fillMaxSize()
                    .padding(innerPadding)
                    .padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
            contentPadding = PaddingValues(top = 12.dp, bottom = 16.dp),
        ) {
            // ── 组 1：应用信息 + Root 开关  ──────────────────────────────────
            item {
                CardGroup {
                    // 1-0  应用信息头
                    CardItem(index = 0, total = 2) {
                        Row(
                            modifier =
                                Modifier
                                    .fillMaxWidth()
                                    .padding(16.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(14.dp),
                        ) {
                            AppIcon(
                                packageName = app.packageName,
                                modifier =
                                    Modifier
                                        .size(52.dp)
                                        .clip(RoundedCornerShape(12.dp)),
                            )
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = app.name,
                                    style = MaterialTheme.typography.titleMedium,
                                    fontWeight = FontWeight.SemiBold,
                                )
                                Text(
                                    text = stringResource(R.string.app_uid_format, app.uid),
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                                )
                            }
                            if (app.isSystem) {
                                AppTag(
                                    label = stringResource(R.string.app_tag_system),
                                    color = MaterialTheme.colorScheme.secondary,
                                )
                            }
                        }
                    }

                    // 1-1  Root 开关
                    CardItem(index = 1, total = 2) {
                        ListRow(
                            modifier =
                                Modifier
                                    .padding(horizontal = 4.dp)
                                    .toggleable(
                                        value = allowed,
                                        role = Role.Switch,
                                        onValueChange = { value ->
                                            haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
                                            allowed = value
                                            if (!value) {
                                                caps = emptySet()
                                                domain = "u:r:nksu:s0"
                                                ns = NksuNamespace.INHERITED
                                            } else if (caps.isEmpty()) {
                                                caps = DEFAULT_CAPS
                                            }
                                        },
                                    ),
                            icon = {
                                Icon(
                                    if (allowed) Icons.Default.LockOpen else Icons.Default.Lock,
                                    contentDescription = null,
                                )
                            },
                            headline = {
                                Text(
                                    stringResource(R.string.app_allow_root),
                                    style = MaterialTheme.typography.titleMedium,
                                    fontWeight = FontWeight.SemiBold,
                                )
                            },
                            supporting = {
                                Text(
                                    if (allowed) {
                                        stringResource(R.string.app_status_granted)
                                    } else {
                                        stringResource(R.string.app_status_denied)
                                    },
                                )
                            },
                            trailing = { Switch(checked = allowed, onCheckedChange = null) },
                        )
                    }
                }
            }

            // ── 组 2：Capabilities + SELinux Domain ──────────────────────────
            item {
                CardGroup {
                    // 2-0  Capabilities 行
                    CardItem(index = 0, total = 2) {
                        Row(
                            modifier =
                                Modifier
                                    .fillMaxWidth()
                                    .clickable(
                                        enabled = allowed,
                                        interactionSource = remember { MutableInteractionSource() },
                                        indication = null,
                                    ) {
                                        haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
                                        showCapsDialog = true
                                    }.padding(horizontal = 16.dp, vertical = 14.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Icon(
                                imageVector = Icons.Default.Tune,
                                contentDescription = null,
                                tint =
                                    if (allowed) {
                                        MaterialTheme.colorScheme.primary
                                    } else {
                                        MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                                    },
                            )
                            Spacer(Modifier.width(14.dp))
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = stringResource(R.string.app_caps_selected, caps.size, LinuxCap.entries.size),
                                    style = MaterialTheme.typography.bodyMedium,
                                    fontWeight = FontWeight.SemiBold,
                                    color =
                                        if (allowed) {
                                            MaterialTheme.colorScheme.onSurface
                                        } else {
                                            MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                                        },
                                )
                                Text(
                                    text =
                                        when {
                                            !allowed -> stringResource(R.string.app_enable_root_first)
                                            caps.isEmpty() -> stringResource(R.string.app_no_capabilities)
                                            else ->
                                                caps.take(4).joinToString(" · ") { it.label } +
                                                    if (caps.size > 4) " +${caps.size - 4}" else ""
                                        },
                                    style = MaterialTheme.typography.labelSmall,
                                    color =
                                        if (allowed) {
                                            MaterialTheme.colorScheme.primary.copy(alpha = 0.7f)
                                        } else {
                                            MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f)
                                        },
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis,
                                )
                            }
                            if (allowed) {
                                Icon(
                                    imageVector = Icons.AutoMirrored.Filled.KeyboardArrowRight,
                                    contentDescription = null,
                                    tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f),
                                    modifier = Modifier.size(20.dp),
                                )
                            }
                        }
                    }

                    // 2-1  SELinux Domain
                    CardItem(index = 1, total = 2) {
                        Column(
                            modifier = Modifier.padding(horizontal = 14.dp, vertical = 12.dp),
                        ) {
                            Text(
                                text = stringResource(R.string.app_selinux_domain),
                                style = MaterialTheme.typography.labelMedium,
                                fontWeight = FontWeight.SemiBold,
                                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                                modifier = Modifier.padding(bottom = 6.dp, start = 2.dp),
                            )
                            OutlinedTextField(
                                value = domain,
                                onValueChange = { domain = it },
                                label = {
                                    Text(
                                        "domain",
                                        fontFamily = FontFamily.Monospace,
                                        fontSize = 12.sp,
                                    )
                                },
                                singleLine = true,
                                textStyle =
                                    MaterialTheme.typography.bodySmall.copy(
                                        fontFamily = FontFamily.Monospace,
                                    ),
                                modifier = Modifier.fillMaxWidth(),
                                enabled = allowed,
                                trailingIcon = {
                                    if (domain != "u:r:nksu:s0") {
                                        IconButton(
                                            onClick = {
                                                haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
                                                domain = "u:r:nksu:s0"
                                            },
                                        ) {
                                            Icon(
                                                Icons.Default.Refresh,
                                                contentDescription = stringResource(R.string.cd_reset),
                                                modifier = Modifier.size(18.dp),
                                            )
                                        }
                                    }
                                },
                                shape = RoundedCornerShape(12.dp),
                            )
                            if (!allowed) {
                                Spacer(Modifier.height(6.dp))
                                Text(
                                    stringResource(R.string.app_enable_root_first),
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f),
                                )
                            }
                        }
                    }
                }
            }

            // ── 组 3：Mount Namespace ────────────────────────────────────────
            item {
                var showNsDialog by remember { mutableStateOf(false) }

                if (showNsDialog) {
                    AlertDialog(
                        onDismissRequest = { showNsDialog = false },
                        title = {
                            Text(
                                stringResource(R.string.app_mount_namespace),
                                style = MaterialTheme.typography.labelMedium,
                                fontWeight = FontWeight.SemiBold,
                            )
                        },
                        text = {
                            Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                                NksuNamespace.entries.forEach { option ->
                                    Row(
                                        modifier =
                                            Modifier
                                                .fillMaxWidth()
                                                .clip(RoundedCornerShape(10.dp))
                                                .clickable(
                                                    interactionSource = remember { MutableInteractionSource() },
                                                    indication = null,
                                                ) {
                                                    haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                                                    ns = option
                                                    showNsDialog = false
                                                }.padding(horizontal = 4.dp, vertical = 8.dp),
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.spacedBy(10.dp),
                                    ) {
                                        RadioButton(selected = ns == option, onClick = null)
                                        Column(modifier = Modifier.weight(1f)) {
                                            Text(
                                                text = stringResource(option.labelRes),
                                                style = MaterialTheme.typography.bodyMedium,
                                                fontWeight = FontWeight.SemiBold,
                                            )
                                            Text(
                                                text = stringResource(option.descriptionRes),
                                                style = MaterialTheme.typography.bodySmall,
                                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                            )
                                        }
                                    }
                                }
                            }
                        },
                        confirmButton = {},
                        dismissButton = {
                            TextButton(onClick = { showNsDialog = false }) {
                                Text(stringResource(R.string.dialog_cancel))
                            }
                        },
                    )
                }

                CardItem(index = 0, total = 1) {
                    Row(
                        modifier =
                            Modifier
                                .fillMaxWidth()
                                .clickable(
                                    enabled = allowed,
                                    interactionSource = remember { MutableInteractionSource() },
                                    indication = null,
                                ) {
                                    haptic.performHapticFeedback(HapticFeedbackType.VirtualKey)
                                    showNsDialog = true
                                }.padding(horizontal = 16.dp, vertical = 14.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = stringResource(R.string.app_mount_namespace),
                                style = MaterialTheme.typography.bodyLarge,
                                fontWeight = FontWeight.Bold,
                                color =
                                    if (allowed) {
                                        MaterialTheme.colorScheme.onSurface
                                    } else {
                                        MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                                    },
                            )
                            Text(
                                text = stringResource(ns.labelRes),
                                style = MaterialTheme.typography.bodyMedium,
                                color =
                                    if (allowed) {
                                        MaterialTheme.colorScheme.onSurfaceVariant
                                    } else {
                                        MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f)
                                    },
                            )
                        }
                        if (allowed) {
                            Icon(
                                imageVector = Icons.AutoMirrored.Filled.KeyboardArrowRight,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f),
                            )
                        }
                    }
                }
            }
        }
    }
}
