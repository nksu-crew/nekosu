package me.nekosu.aqnya.ui.screens

import androidx.annotation.StringRes
import me.nekosu.aqnya.R

enum class LinuxCap(
    val value: Int,
    val label: String,
    @StringRes val descriptionRes: Int,
) {
    CAP_CHOWN(0, "CHOWN", R.string.cap_desc_chown),
    CAP_DAC_OVERRIDE(1, "DAC_OVERRIDE", R.string.cap_desc_dac_override),
    CAP_DAC_READ_SEARCH(2, "DAC_READ_SEARCH", R.string.cap_desc_dac_read_search),
    CAP_FOWNER(3, "FOWNER", R.string.cap_desc_fowner),
    CAP_FSETID(4, "FSETID", R.string.cap_desc_fsetid),
    CAP_KILL(5, "KILL", R.string.cap_desc_kill),
    CAP_SETGID(6, "SETGID", R.string.cap_desc_setgid),
    CAP_SETUID(7, "SETUID", R.string.cap_desc_setuid),
    CAP_SETPCAP(8, "SETPCAP", R.string.cap_desc_setpcap),
    CAP_LINUX_IMMUTABLE(9, "LINUX_IMMUTABLE", R.string.cap_desc_linux_immutable),
    CAP_NET_BIND_SERVICE(10, "NET_BIND_SERVICE", R.string.cap_desc_net_bind_service),
    CAP_NET_BROADCAST(11, "NET_BROADCAST", R.string.cap_desc_net_broadcast),
    CAP_NET_ADMIN(12, "NET_ADMIN", R.string.cap_desc_net_admin),
    CAP_NET_RAW(13, "NET_RAW", R.string.cap_desc_net_raw),
    CAP_IPC_LOCK(14, "IPC_LOCK", R.string.cap_desc_ipc_lock),
    CAP_IPC_OWNER(15, "IPC_OWNER", R.string.cap_desc_ipc_owner),
    CAP_SYS_MODULE(16, "SYS_MODULE", R.string.cap_desc_sys_module),
    CAP_SYS_RAWIO(17, "SYS_RAWIO", R.string.cap_desc_sys_rawio),
    CAP_SYS_CHROOT(18, "SYS_CHROOT", R.string.cap_desc_sys_chroot),
    CAP_SYS_PTRACE(19, "SYS_PTRACE", R.string.cap_desc_sys_ptrace),
    CAP_SYS_PACCT(20, "SYS_PACCT", R.string.cap_desc_sys_pacct),
    CAP_SYS_ADMIN(21, "SYS_ADMIN", R.string.cap_desc_sys_admin),
    CAP_SYS_BOOT(22, "SYS_BOOT", R.string.cap_desc_sys_boot),
    CAP_SYS_NICE(23, "SYS_NICE", R.string.cap_desc_sys_nice),
    CAP_SYS_RESOURCE(24, "SYS_RESOURCE", R.string.cap_desc_sys_resource),
    CAP_SYS_TIME(25, "SYS_TIME", R.string.cap_desc_sys_time),
    CAP_SYS_TTY_CONFIG(26, "SYS_TTY_CONFIG", R.string.cap_desc_sys_tty_config),
    CAP_MKNOD(27, "MKNOD", R.string.cap_desc_mknod),
    CAP_LEASE(28, "LEASE", R.string.cap_desc_lease),
    CAP_AUDIT_WRITE(29, "AUDIT_WRITE", R.string.cap_desc_audit_write),
    CAP_AUDIT_CONTROL(30, "AUDIT_CONTROL", R.string.cap_desc_audit_control),
    CAP_SETFCAP(31, "SETFCAP", R.string.cap_desc_setfcap),
    CAP_MAC_OVERRIDE(32, "MAC_OVERRIDE", R.string.cap_desc_mac_override),
    CAP_MAC_ADMIN(33, "MAC_ADMIN", R.string.cap_desc_mac_admin),
    CAP_SYSLOG(34, "SYSLOG", R.string.cap_desc_syslog),
    CAP_WAKE_ALARM(35, "WAKE_ALARM", R.string.cap_desc_wake_alarm),
    CAP_BLOCK_SUSPEND(36, "BLOCK_SUSPEND", R.string.cap_desc_block_suspend),
    CAP_AUDIT_READ(37, "AUDIT_READ", R.string.cap_desc_audit_read),
    CAP_PERFMON(38, "PERFMON", R.string.cap_desc_perfmon),
    CAP_BPF(39, "BPF", R.string.cap_desc_bpf),
    CAP_CHECKPOINT_RESTORE(40, "CHECKPOINT_RESTORE", R.string.cap_desc_checkpoint_restore),
}

val DEFAULT_CAPS: Set<LinuxCap> =
    setOf(
        LinuxCap.CAP_CHOWN,
        LinuxCap.CAP_DAC_OVERRIDE,
        LinuxCap.CAP_DAC_READ_SEARCH,
        LinuxCap.CAP_FOWNER,
        LinuxCap.CAP_SETUID,
        LinuxCap.CAP_SETGID,
        LinuxCap.CAP_SYS_ADMIN,
    )

enum class NksuNamespace(
    val value: Int,
    @StringRes val labelRes: Int,
    @StringRes val descriptionRes: Int,
) {
    INHERITED(0, R.string.namespace_inherited_label, R.string.namespace_inherited_desc),
    INDIVIDUAL(1, R.string.namespace_individual_label, R.string.namespace_individual_desc),
    GLOBAL(2, R.string.namespace_global_label, R.string.namespace_global_desc),
}

data class AppConfig(
    val allowed: Boolean = false,
    val caps: Set<LinuxCap> = DEFAULT_CAPS,
    val selinuxDomain: String = "u:r:nksu:s0",
    val namespace: NksuNamespace = NksuNamespace.INHERITED,
)
