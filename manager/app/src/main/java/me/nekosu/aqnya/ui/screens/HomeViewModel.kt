package me.nekosu.aqnya.ui.screens

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.nekosu.aqnya.ncore
import me.nekosu.aqnya.util.RootDbHelper
import me.nekosu.aqnya.util.getAppVersion

class HomeViewModel(
    app: Application,
) : AndroidViewModel(app) {
    private val _installStatus = MutableStateFlow(InstallStatus.NOT_INSTALLED)
    val installStatus: StateFlow<InstallStatus> = _installStatus

    private val _suCount = MutableStateFlow(0)
    val suCount: StateFlow<Int> = _suCount

    private val _managerVersion = MutableStateFlow("")
    val managerVersion: StateFlow<String> = _managerVersion

    private val appContext = app.applicationContext
    private val rootDbHelper = RootDbHelper(appContext)

    init {
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                _installStatus.value =
                    if (ncore.ctl(1) == 0) InstallStatus.INSTALLED else InstallStatus.NOT_INSTALLED
                _managerVersion.value = getAppVersion(appContext)
            }
            refresh()
        }
    }

    fun refresh() {
        if (_installStatus.value != InstallStatus.INSTALLED) return
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                ncore.ctl(3)
                _suCount.value = rootDbHelper.getAllowedCount()
            }
        }
    }

    override fun onCleared() {
        super.onCleared()
        rootDbHelper.close()
    }
}

class HomeViewModelFactory(
    private val app: Application,
) : ViewModelProvider.Factory {
    @Suppress("UNCHECKED_CAST")
    override fun <T : ViewModel> create(modelClass: Class<T>): T = HomeViewModel(app) as T
}
