#include "ui/language_config.h"

#include <wx/config.h>

namespace {

const wchar_t* SettingName(i18n::Language language) {
    switch (language) {
        case i18n::Language::Chinese:
            return L"zh";
        case i18n::Language::English:
            return L"en";
        default:
            return L"auto";
    }
}

}  // namespace

i18n::Language LoadLanguageSetting() {
    wxConfigBase* config = wxConfigBase::Get();
    if (!config) return i18n::Language::Auto;

    wxString value;
    config->Read("/Language", &value, L"auto");
    if (value.IsSameAs(L"zh", false) || value.IsSameAs(L"chinese", false)) {
        return i18n::Language::Chinese;
    }
    if (value.IsSameAs(L"en", false) || value.IsSameAs(L"english", false)) {
        return i18n::Language::English;
    }
    return i18n::Language::Auto;
}

bool SaveLanguageSetting(i18n::Language language) {
    wxConfigBase* config = wxConfigBase::Get();
    if (!config) return false;
    config->Write("/Language", SettingName(language));
    return config->Flush();
}

void ApplyLanguageSetting(i18n::Language language) {
    i18n::SetLanguage(language);
}
