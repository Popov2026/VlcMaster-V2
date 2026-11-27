package fr.popov.homerun

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.os.LocaleListCompat
import androidx.lifecycle.lifecycleScope
import androidx.preference.ListPreference
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** Paramètres (lecture, guide, enregistrement, interface, tuner, mises à jour). */
class SettingsActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        title = getString(R.string.settings)
        if (savedInstanceState == null)
            supportFragmentManager.beginTransaction().replace(android.R.id.content, Frag()).commit()
    }

    class Frag : PreferenceFragmentCompat() {
        override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
            setPreferencesFromResource(R.xml.settings, rootKey)
            val ctx = requireContext()

            findPreference<Preference>("playlist_url")?.summaryProvider =
                Preference.SummaryProvider<Preference> { Prefs.get(ctx).playlistUrl }

            // Langue de l'interface : sélecteur par appli (AppCompat).
            findPreference<ListPreference>("ui_lang")?.apply {
                val tags = AppCompatDelegate.getApplicationLocales().toLanguageTags()
                value = tags.substringBefore('-').ifEmpty { "" }
                setOnPreferenceChangeListener { _, v ->
                    val t = v as String
                    AppCompatDelegate.setApplicationLocales(
                        if (t.isEmpty()) LocaleListCompat.getEmptyLocaleList() else LocaleListCompat.forLanguageTags(t)
                    )
                    true
                }
            }
            findPreference<Preference>("tuner")?.setOnPreferenceClickListener {
                startActivity(Intent(ctx, TunerActivity::class.java)); true
            }
            findPreference<Preference>("recordings")?.apply {
                summary = RecordService.dir(ctx).absolutePath
                setOnPreferenceClickListener { startActivity(Intent(ctx, RecordingsActivity::class.java)); true }
            }
            findPreference<Preference>("update_now")?.apply {
                summary = getString(R.string.version_current, BuildConfig.VERSION_NAME)
                setOnPreferenceClickListener { checkNow(this); true }
            }
        }

        private fun checkNow(p: Preference) {
            p.summary = getString(R.string.update_checking)
            lifecycleScope.launch {
                val r = withContext(Dispatchers.IO) { runCatching { Updater.check() } }
                val ctx = context ?: return@launch
                r.onSuccess { i ->
                    if (i.newer) {
                        p.summary = getString(R.string.update_banner, i.version)
                        p.setOnPreferenceClickListener {
                            startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(i.apkUrl ?: Updater.PAGE))); true
                        }
                    } else p.summary = ctx.getString(R.string.update_none, BuildConfig.VERSION_NAME, i.version)
                }.onFailure { e -> p.summary = ctx.getString(R.string.update_failed, e.message ?: "") }
            }
        }
    }
}
